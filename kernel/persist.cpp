#include "persist.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "blockdev.hpp"
#include "fat.hpp"
#include "logger.hpp"
#include "memory_manager.hpp"

namespace {

constexpr size_t kBlock = 4096;                    // 差分検出の単位
constexpr size_t kMaxRun = 128 * kBlock;            // 1回の書き込み上限(512KiB)
constexpr uint64_t kMaxVolumeBytes = 256ull << 20;  // 256MiB まで
constexpr char kLabel[11] = {'X','I','N','O','S','D','I','S','K',' ',' '};

uint8_t* volume = nullptr;
uint8_t* shadow = nullptr;  // ディスクと同期済みの内容
size_t volume_bytes = 0;
BlockDevice* mounted_dev = nullptr;
uint64_t base_lba = 0;      // ボリューム先頭の 512 バイト単位の位置
char description[96] = "";
volatile bool syncing = false;

alignas(4096) uint8_t scratch[4096 * 8];  // パーティションテーブル読み取り用

void* AllocFrames(size_t bytes) {
  const size_t frames = (bytes + kBytesPerFrame - 1) / kBytesPerFrame;
  auto [ frame, err ] = memory_manager->Allocate(frames);
  return err ? nullptr : frame.Frame();
}

// base から始まる領域(最大 limit セクタ)に XINOSDISK の FAT32 があれば読み込む
bool TryVolume(BlockDevice* dev, uint64_t base, uint64_t limit, const char* what) {
  if (limit < 64 || base + 1 > dev->SectorCount()) return false;
  if (!dev->Read(base, scratch, 1)) return false;
  const auto* bpb = reinterpret_cast<const fat::BPB*>(scratch);
  if (scratch[510] != 0x55 || scratch[511] != 0xaa) return false;
  if (memcmp(bpb->volume_label, kLabel, 11) != 0) return false;  // 他のボリュームには触れない
  if (memcmp(bpb->fs_type, "FAT32", 5) != 0) return false;
  if (bpb->bytes_per_sector != 512 || bpb->total_sectors_32 == 0 || bpb->fat_size_32 == 0) {
    return false;
  }
  const uint64_t bytes = uint64_t{bpb->total_sectors_32} * 512;
  if (bytes > kMaxVolumeBytes || bpb->total_sectors_32 > limit) {
    Log(kWarn, "persist: unsupported volume size on %s\n", dev->Name());
    return false;
  }
  auto* vol = static_cast<uint8_t*>(AllocFrames(bytes));
  auto* shd = static_cast<uint8_t*>(AllocFrames(bytes));
  if (!vol || !shd) {
    Log(kWarn, "persist: not enough memory for the volume\n");
    return false;
  }
  if (!dev->Read(base, vol, bpb->total_sectors_32)) {
    Log(kWarn, "persist: failed to read the volume from %s\n", dev->Name());
    return false;
  }
  memcpy(shd, vol, bytes);
  volume = vol;
  shadow = shd;
  volume_bytes = bytes;
  mounted_dev = dev;
  base_lba = base;
  snprintf(description, sizeof(description), "%s (%s, LBA %lu)", dev->Name(), what, base);
  return true;
}

bool TryGpt(BlockDevice* dev) {
  const uint32_t ratio = dev->LogicalSectorSize() / 512;
  if (!dev->Read(ratio, scratch, ratio)) return false;  // LBA1 = GPT ヘッダ
  if (memcmp(scratch, "EFI PART", 8) != 0) return false;
  const uint64_t entries_lba = *reinterpret_cast<uint64_t*>(scratch + 72);
  const uint32_t count = *reinterpret_cast<uint32_t*>(scratch + 80);
  const uint32_t entry_size = *reinterpret_cast<uint32_t*>(scratch + 84);
  if (entry_size < 128 || entry_size > 4096 || count == 0 || count > 256) return false;

  for (uint32_t i = 0; i < count; ++i) {
    // 1エントリずつ読む(エントリ配列の先頭 LBA はデバイスの論理セクタ単位)
    const uint64_t byte_off = uint64_t{i} * entry_size;
    const uint64_t lba512 = entries_lba * ratio + byte_off / 512;
    if (!dev->Read(lba512, scratch, 8)) return false;
    const uint8_t* e = scratch + (byte_off % 512);
    bool empty = true;
    for (int k = 0; k < 16; ++k) if (e[k]) { empty = false; break; }
    if (empty) continue;
    const uint64_t first = *reinterpret_cast<const uint64_t*>(e + 32);
    const uint64_t last = *reinterpret_cast<const uint64_t*>(e + 40);
    if (last < first) continue;
    if (TryVolume(dev, first * ratio, (last - first + 1) * ratio, "GPT partition")) {
      return true;
    }
  }
  return false;
}

bool TryMbr(BlockDevice* dev) {
  if (!dev->Read(0, scratch, 1)) return false;
  if (scratch[510] != 0x55 || scratch[511] != 0xaa) return false;
  uint8_t table[64];
  memcpy(table, scratch + 446, 64);
  for (int i = 0; i < 4; ++i) {
    const uint8_t* p = table + 16 * i;
    const uint8_t type = p[4];
    if (type == 0 || type == 0xee || type == 0x05 || type == 0x0f) continue;
    const uint32_t start = *reinterpret_cast<const uint32_t*>(p + 8);
    const uint32_t size = *reinterpret_cast<const uint32_t*>(p + 12);
    if (start == 0 || size == 0) continue;
    if (TryVolume(dev, start, size, "MBR partition")) return true;
  }
  return false;
}

}  // namespace

namespace persist {

bool Mount() {
  if (volume) {
    return true;
  }
  InitializeBlockDevices();
  for (BlockDevice* dev : BlockDevices()) {
    // ディスク全体がボリューム → GPT → MBR の順に探す
    if (TryVolume(dev, 0, dev->SectorCount(), "whole disk") || TryGpt(dev) || TryMbr(dev)) {
      return true;
    }
  }
  return false;
}

void* Volume() { return volume; }
size_t VolumeBytes() { return volume_bytes; }
bool IsMounted() { return volume != nullptr; }
const char* Description() { return description; }

long Sync() {
  if (!volume) {
    return 0;
  }
  if (__atomic_test_and_set(&syncing, __ATOMIC_ACQUIRE)) {
    return 0;  // 他のタスクが同期中
  }
  long written = 0;
  for (size_t off = 0; off < volume_bytes;) {
    const size_t len = volume_bytes - off < kBlock ? volume_bytes - off : kBlock;
    if (memcmp(volume + off, shadow + off, len) == 0) {
      off += kBlock;
      continue;
    }
    // 連続して異なるブロックをまとめて書く
    size_t end = off + len;
    while (end < volume_bytes && end - off < kMaxRun) {
      const size_t l2 = volume_bytes - end < kBlock ? volume_bytes - end : kBlock;
      if (memcmp(volume + end, shadow + end, l2) == 0) break;
      end += l2;
    }
    const size_t run = end - off;
    const size_t sectors = (run + 511) / 512;
    if (!mounted_dev->Write(base_lba + off / 512, volume + off, sectors)) {
      written = -1;
      break;
    }
    memcpy(shadow + off, volume + off, run);
    written += run;
    off = end;
  }
  if (written > 0) {
    mounted_dev->Flush();
  }
  __atomic_clear(&syncing, __ATOMIC_RELEASE);
  return written;
}

}  // namespace persist
