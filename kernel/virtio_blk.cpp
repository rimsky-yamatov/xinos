#include "virtio_blk.hpp"

#include <cstring>

#include "asmfunc.h"
#include "logger.hpp"
#include "memory_manager.hpp"
#include "pci.hpp"

namespace {

// legacy virtio PCI レジスタ(I/Oポートからのオフセット)
constexpr uint16_t kRegGuestFeatures = 4;
constexpr uint16_t kRegQueuePfn = 8;
constexpr uint16_t kRegQueueSize = 12;
constexpr uint16_t kRegQueueSel = 14;
constexpr uint16_t kRegQueueNotify = 16;
constexpr uint16_t kRegStatus = 18;
constexpr uint16_t kRegIsr = 19;
constexpr uint16_t kRegConfig = 20;  // capacity (u64)

constexpr uint8_t kStatusAck = 1, kStatusDriver = 2, kStatusDriverOk = 4,
                  kStatusFailed = 0x80;

constexpr uint16_t kDescNext = 1, kDescWrite = 2;
constexpr uint32_t kTypeIn = 0, kTypeOut = 1;
constexpr size_t kMaxSectorsPerRequest = 256;

struct Desc { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct ReqHeader { uint32_t type; uint32_t reserved; uint64_t sector; };

uint16_t io_base = 0;
uint64_t capacity = 0;
uint16_t queue_size = 0;
Desc* desc = nullptr;
volatile uint16_t* avail = nullptr;       // [0]=flags [1]=idx [2..]=ring
volatile uint16_t* used = nullptr;        // [0]=flags [1]=idx (以降 {id,len})
ReqHeader* header = nullptr;
volatile uint8_t* status_byte = nullptr;
uint16_t avail_idx = 0, last_used = 0;

inline void Barrier() { __asm__ volatile("" ::: "memory"); }

void* AllocZeroed(size_t bytes) {
  const size_t frames = (bytes + kBytesPerFrame - 1) / kBytesPerFrame;
  auto [ frame, err ] = memory_manager->Allocate(frames);
  if (err) {
    return nullptr;
  }
  void* p = frame.Frame();
  memset(p, 0, frames * kBytesPerFrame);
  return p;
}

bool Request(uint32_t type, uint64_t lba, void* buf, size_t sectors) {
  header->type = type;
  header->reserved = 0;
  header->sector = lba;
  *status_byte = 0xff;

  desc[0] = {reinterpret_cast<uint64_t>(header), sizeof(ReqHeader), kDescNext, 1};
  desc[1] = {reinterpret_cast<uint64_t>(buf),
             static_cast<uint32_t>(sectors * virtio_blk::kSectorSize),
             static_cast<uint16_t>(kDescNext | (type == kTypeIn ? kDescWrite : 0)), 2};
  desc[2] = {reinterpret_cast<uint64_t>(const_cast<uint8_t*>(status_byte)), 1,
             kDescWrite, 0};

  avail[2 + (avail_idx % queue_size)] = 0;
  Barrier();
  ++avail_idx;
  avail[1] = avail_idx;
  Barrier();
  IoOut16(io_base + kRegQueueNotify, 0);

  for (long spin = 0; used[1] == last_used; ++spin) {
    if (spin > 200000000) {
      Log(kError, "virtio-blk: request timeout\n");
      return false;
    }
    Barrier();
  }
  ++last_used;
  IoIn8(io_base + kRegIsr);  // 割り込みラインを下ろす
  return *status_byte == 0;
}

bool Transfer(uint32_t type, uint64_t lba, uint8_t* buf, size_t sectors) {
  if (capacity == 0 || lba + sectors > capacity) {
    return false;
  }
  while (sectors > 0) {
    const size_t n = sectors < kMaxSectorsPerRequest ? sectors : kMaxSectorsPerRequest;
    if (!Request(type, lba, buf, n)) {
      return false;
    }
    lba += n;
    buf += n * virtio_blk::kSectorSize;
    sectors -= n;
  }
  return true;
}

}  // namespace

namespace virtio_blk {

bool Initialize() {
  if (capacity != 0) {
    return true;
  }
  const pci::Device* dev = nullptr;
  for (const auto& d : pci::devices) {
    const uint32_t id = pci::ReadConfReg(d, 0x00);
    if ((id & 0xffff) == 0x1af4 && (id >> 16) == 0x1001) {
      dev = &d;
      break;
    }
  }
  if (!dev) {
    return false;
  }
  const uint32_t bar0 = pci::ReadConfReg(*dev, 0x10);
  if (!(bar0 & 1)) {
    Log(kWarn, "virtio-blk: BAR0 is not an I/O port (modern-only device?)\n");
    return false;
  }
  io_base = static_cast<uint16_t>(bar0 & ~3u);
  // I/O空間とバスマスタを有効化
  pci::WriteConfReg(*dev, 0x04, pci::ReadConfReg(*dev, 0x04) | 0x5);

  IoOut8(io_base + kRegStatus, 0);  // reset
  IoOut8(io_base + kRegStatus, kStatusAck);
  IoOut8(io_base + kRegStatus, kStatusAck | kStatusDriver);
  IoOut32(io_base + kRegGuestFeatures, 0);

  IoOut16(io_base + kRegQueueSel, 0);
  queue_size = IoIn16(io_base + kRegQueueSize);
  if (queue_size == 0 || queue_size > 1024) {
    IoOut8(io_base + kRegStatus, kStatusFailed);
    return false;
  }
  const size_t desc_bytes = 16 * queue_size;
  const size_t avail_bytes = 6 + 2 * queue_size;
  const size_t used_off = (desc_bytes + avail_bytes + 4095) & ~size_t{4095};
  const size_t total = used_off + 6 + 8 * queue_size;
  uint8_t* q = static_cast<uint8_t*>(AllocZeroed(total));
  auto* req = static_cast<uint8_t*>(AllocZeroed(4096));
  if (!q || !req) {
    IoOut8(io_base + kRegStatus, kStatusFailed);
    return false;
  }
  desc = reinterpret_cast<Desc*>(q);
  avail = reinterpret_cast<volatile uint16_t*>(q + desc_bytes);
  used = reinterpret_cast<volatile uint16_t*>(q + used_off);
  header = reinterpret_cast<ReqHeader*>(req);
  status_byte = req + 64;
  avail[0] = 1;  // VRING_AVAIL_F_NO_INTERRUPT
  avail_idx = last_used = 0;

  IoOut32(io_base + kRegQueuePfn, static_cast<uint32_t>(reinterpret_cast<uint64_t>(q) >> 12));
  IoOut8(io_base + kRegStatus, kStatusAck | kStatusDriver | kStatusDriverOk);

  const uint64_t lo = IoIn32(io_base + kRegConfig);
  const uint64_t hi = IoIn32(io_base + kRegConfig + 4);
  capacity = lo | (hi << 32);
  Log(kInfo, "virtio-blk: %lu sectors, queue size %d\n", capacity, queue_size);
  return capacity != 0;
}

uint64_t SectorCount() { return capacity; }

bool Read(uint64_t lba, void* buf, size_t sectors) {
  return Transfer(kTypeIn, lba, static_cast<uint8_t*>(buf), sectors);
}

bool Write(uint64_t lba, const void* buf, size_t sectors) {
  return Transfer(kTypeOut, lba, const_cast<uint8_t*>(static_cast<const uint8_t*>(buf)), sectors);
}

}  // namespace virtio_blk
