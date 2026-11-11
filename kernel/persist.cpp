#include "persist.hpp"

#include <cstdint>
#include <cstring>

#include "fat.hpp"
#include "logger.hpp"
#include "memory_manager.hpp"
#include "virtio_blk.hpp"

namespace {

constexpr size_t kBlock = 4096;                    // 差分検出の単位
constexpr size_t kMaxRun = 128 * kBlock;            // 1回の書き込み上限(512KiB)
constexpr uint64_t kMaxVolumeBytes = 256ull << 20;  // 256MiB まで

uint8_t* volume = nullptr;
uint8_t* shadow = nullptr;  // ディスクと同期済みの内容
size_t volume_bytes = 0;
volatile bool syncing = false;

void* AllocFrames(size_t bytes) {
  const size_t frames = (bytes + kBytesPerFrame - 1) / kBytesPerFrame;
  auto [ frame, err ] = memory_manager->Allocate(frames);
  return err ? nullptr : frame.Frame();
}

}  // namespace

namespace persist {

bool Mount() {
  if (volume) {
    return true;
  }
  if (!virtio_blk::Initialize()) {
    return false;
  }
  alignas(16) static uint8_t sector0[virtio_blk::kSectorSize];
  if (!virtio_blk::Read(0, sector0, 1)) {
    return false;
  }
  const auto* bpb = reinterpret_cast<const fat::BPB*>(sector0);
  if (sector0[510] != 0x55 || sector0[511] != 0xaa ||
      bpb->bytes_per_sector != virtio_blk::kSectorSize ||
      bpb->total_sectors_32 == 0 || bpb->fat_size_32 == 0) {
    Log(kWarn, "persist: disk has no FAT32 volume (run scripts/mkdisk.sh)\n");
    return false;
  }
  const uint64_t bytes = uint64_t{bpb->total_sectors_32} * bpb->bytes_per_sector;
  if (bytes > kMaxVolumeBytes || bpb->total_sectors_32 > virtio_blk::SectorCount()) {
    Log(kWarn, "persist: unsupported volume size\n");
    return false;
  }

  auto* vol = static_cast<uint8_t*>(AllocFrames(bytes));
  auto* shd = static_cast<uint8_t*>(AllocFrames(bytes));
  if (!vol || !shd) {
    Log(kWarn, "persist: not enough memory for the volume\n");
    return false;
  }
  if (!virtio_blk::Read(0, vol, bpb->total_sectors_32)) {
    Log(kWarn, "persist: failed to read the volume\n");
    return false;
  }
  memcpy(shd, vol, bytes);
  volume = vol;
  shadow = shd;
  volume_bytes = bytes;
  return true;
}

void* Volume() { return volume; }
size_t VolumeBytes() { return volume_bytes; }
bool IsMounted() { return volume != nullptr; }

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
    const size_t sectors = (run + virtio_blk::kSectorSize - 1) / virtio_blk::kSectorSize;
    if (!virtio_blk::Write(off / virtio_blk::kSectorSize, volume + off, sectors)) {
      written = -1;
      break;
    }
    memcpy(shadow + off, volume + off, run);
    written += run;
    off = end;
  }
  __atomic_clear(&syncing, __ATOMIC_RELEASE);
  return written;
}

}  // namespace persist
