#include "nvme.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "logger.hpp"
#include "paging.hpp"
#include "pci.hpp"

namespace {

constexpr size_t kAdminQueueSize = 16;
constexpr size_t kIoQueueSize = 64;
constexpr uint32_t kMaxTransferBytes = 128 * 1024;
constexpr long kSpinLimit = 400000000;

enum Reg : uint32_t {
  kCap = 0x00, kVs = 0x08, kCc = 0x14, kCsts = 0x1c, kAqa = 0x24,
  kAsq = 0x28, kAcq = 0x30,
};

struct Sqe {
  uint32_t cdw0, nsid, cdw2, cdw3;
  uint64_t mptr, prp1, prp2;
  uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
};
static_assert(sizeof(Sqe) == 64);

struct Cqe {
  uint32_t dw0, dw1;
  uint16_t sq_head, sq_id;
  uint16_t cid, status;
};
static_assert(sizeof(Cqe) == 16);

class Queue {
 public:
  bool Init(uint16_t qid, uint16_t size, volatile uint8_t* regs, uint32_t dstrd) {
    sq_ = static_cast<volatile Sqe*>(AllocDmaPages(size * sizeof(Sqe)));
    cq_ = static_cast<volatile Cqe*>(AllocDmaPages(size * sizeof(Cqe)));
    if (!sq_ || !cq_) return false;
    size_ = size;
    sq_db_ = reinterpret_cast<volatile uint32_t*>(regs + 0x1000 + (2 * qid) * (4u << dstrd));
    cq_db_ = reinterpret_cast<volatile uint32_t*>(regs + 0x1000 + (2 * qid + 1) * (4u << dstrd));
    return true;
  }

  uint64_t SqAddr() const { return reinterpret_cast<uint64_t>(sq_); }
  uint64_t CqAddr() const { return reinterpret_cast<uint64_t>(cq_); }
  uint16_t Size() const { return size_; }

  // コマンドを1つ発行して完了をポーリングする。成功なら true。
  bool Submit(Sqe cmd, uint32_t* result = nullptr) {
    cmd.cdw0 = (cmd.cdw0 & 0xffff) | (static_cast<uint32_t>(++cid_) << 16);
    volatile Sqe* slot = &sq_[tail_];
    memcpy(const_cast<Sqe*>(slot), &cmd, sizeof(Sqe));
    tail_ = (tail_ + 1) % size_;
    __asm__ volatile("" ::: "memory");
    *sq_db_ = tail_;

    for (long spin = 0; spin < kSpinLimit; ++spin) {
      volatile Cqe* e = &cq_[head_];
      if ((e->status & 1) == phase_) {
        const uint16_t status = e->status >> 1;
        if (result) *result = e->dw0;
        if (++head_ == size_) {
          head_ = 0;
          phase_ ^= 1;
        }
        *cq_db_ = head_;
        if (status != 0) {
          Log(kWarn, "nvme: command failed, status=0x%x\n", status);
          return false;
        }
        return true;
      }
      __builtin_ia32_pause();
    }
    Log(kError, "nvme: command timeout\n");
    return false;
  }

 private:
  volatile Sqe* sq_{nullptr};
  volatile Cqe* cq_{nullptr};
  volatile uint32_t* sq_db_{nullptr};
  volatile uint32_t* cq_db_{nullptr};
  uint16_t size_{0}, tail_{0}, head_{0}, cid_{0};
  uint16_t phase_{1};
};

class NvmeDevice : public BlockDevice {
 public:
  explicit NvmeDevice(int index) {
    snprintf(name_, sizeof(name_), "nvme%dn1", index);
  }

  bool Init(pci::Device& dev);

  const char* Name() const override { return name_; }
  uint64_t SectorCount() const override { return native_sectors_ * (sector_size_ / 512); }
  uint32_t LogicalSectorSize() const override { return sector_size_; }

  bool Read(uint64_t lba, void* buf, size_t n) override { return Transfer(false, lba, buf, n); }
  bool Write(uint64_t lba, const void* buf, size_t n) override {
    return Transfer(true, lba, const_cast<void*>(buf), n);
  }

 private:
  uint32_t Read32(uint32_t off) const { return *reinterpret_cast<volatile uint32_t*>(regs_ + off); }
  void Write32(uint32_t off, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(regs_ + off) = v; }
  uint64_t Read64(uint32_t off) const {
    return Read32(off) | (static_cast<uint64_t>(Read32(off + 4)) << 32);
  }
  void Write64(uint32_t off, uint64_t v) { Write32(off, v & 0xffffffff); Write32(off + 4, v >> 32); }

  bool WaitReady(bool ready) {
    for (long spin = 0; spin < kSpinLimit; ++spin) {
      if (((Read32(kCsts) & 1) != 0) == ready) return true;
      __builtin_ia32_pause();
    }
    return false;
  }

  bool Identify(uint32_t cns, void* buf) {
    Sqe c{};
    c.cdw0 = 0x06;
    c.nsid = cns == 0 ? nsid_ : 0;
    c.prp1 = reinterpret_cast<uint64_t>(buf);
    c.cdw10 = cns;
    return admin_.Submit(c);
  }

  // buf は 4KiB 境界、native_count は max_bytes_ 以内
  bool Rw(bool write, uint64_t native_lba, void* buf, uint32_t native_count) {
    const uint64_t bytes = static_cast<uint64_t>(native_count) * sector_size_;
    const uint64_t pages = (bytes + 4095) / 4096;
    Sqe c{};
    c.cdw0 = write ? 0x01 : 0x02;
    c.nsid = nsid_;
    const uint64_t base = reinterpret_cast<uint64_t>(buf);
    c.prp1 = base;
    if (pages == 2) {
      c.prp2 = base + 4096;
    } else if (pages > 2) {
      for (uint64_t i = 1; i < pages; ++i) prp_list_[i - 1] = base + i * 4096;
      c.prp2 = reinterpret_cast<uint64_t>(prp_list_);
    }
    c.cdw10 = static_cast<uint32_t>(native_lba & 0xffffffff);
    c.cdw11 = static_cast<uint32_t>(native_lba >> 32);
    c.cdw12 = native_count - 1;
    return io_.Submit(c);
  }

  bool Transfer(bool write, uint64_t lba, void* buf, size_t sectors);

  char name_[16];
  volatile uint8_t* regs_{nullptr};
  Queue admin_, io_;
  uint32_t nsid_{1};
  uint32_t sector_size_{512};
  uint64_t native_sectors_{0};
  uint32_t max_bytes_{kMaxTransferBytes};
  uint8_t* bounce_{nullptr};
  uint64_t* prp_list_{nullptr};
};

bool NvmeDevice::Init(pci::Device& dev) {
  // メモリ空間 + バスマスタを有効化し、INTx は無効化(ポーリングで使う)
  pci::WriteConfReg(dev, 0x04, pci::ReadConfReg(dev, 0x04) | 0x6 | 0x400);

  auto [ bar, err ] = pci::ReadBar(dev, 0);
  if (err) return false;
  const uint64_t base = bar & ~0xfull;
  if (base == 0) return false;
  if (auto e = MapIdentityMMIO(base, 0x4000)) {
    Log(kWarn, "nvme: failed to map registers\n");
    return false;
  }
  regs_ = reinterpret_cast<volatile uint8_t*>(base);

  const uint64_t cap = Read64(kCap);
  const uint32_t dstrd = (cap >> 32) & 0xf;
  const uint32_t mpsmin = (cap >> 48) & 0xf;
  if (mpsmin != 0) {
    Log(kWarn, "nvme: controller needs pages larger than 4KiB\n");
    return false;
  }

  // 一度無効化してからキューを設定して有効化
  Write32(kCc, 0);
  if (!WaitReady(false)) return false;

  if (!admin_.Init(0, kAdminQueueSize, regs_, dstrd) ||
      !io_.Init(1, kIoQueueSize, regs_, dstrd)) {
    return false;
  }
  Write32(kAqa, ((kAdminQueueSize - 1) << 16) | (kAdminQueueSize - 1));
  Write64(kAsq, admin_.SqAddr());
  Write64(kAcq, admin_.CqAddr());
  Write32(kCc, (6u << 16) | (4u << 20) | 1u);  // IOSQES=64B IOCQES=16B EN=1
  if (!WaitReady(true)) {
    Log(kWarn, "nvme: controller did not become ready\n");
    return false;
  }

  auto* id = static_cast<uint8_t*>(AllocDmaPages(4096));
  prp_list_ = static_cast<uint64_t*>(AllocDmaPages(4096));
  if (!id || !prp_list_) return false;

  // 転送サイズ上限(MDTS)
  if (!Identify(1, id)) return false;
  const uint8_t mdts = id[77];
  if (mdts != 0) {
    max_bytes_ = std::min<uint64_t>(kMaxTransferBytes, 4096ull << mdts);
  }

  // I/O キュー作成(CQ → SQ の順)
  Sqe c{};
  c.cdw0 = 0x05;
  c.prp1 = io_.CqAddr();
  c.cdw10 = ((io_.Size() - 1) << 16) | 1;
  c.cdw11 = 1;  // PC=1, 割り込みなし
  if (!admin_.Submit(c)) return false;
  c = Sqe{};
  c.cdw0 = 0x01;
  c.prp1 = io_.SqAddr();
  c.cdw10 = ((io_.Size() - 1) << 16) | 1;
  c.cdw11 = (1u << 16) | 1;  // CQID=1, PC=1
  if (!admin_.Submit(c)) return false;

  // 名前空間1
  memset(id, 0, 4096);
  if (!Identify(0, id)) return false;
  native_sectors_ = *reinterpret_cast<uint64_t*>(id);
  const uint8_t flbas = id[26] & 0xf;
  const uint32_t lbaf = *reinterpret_cast<uint32_t*>(id + 128 + 4 * flbas);
  const uint32_t lbads = (lbaf >> 16) & 0xff;
  if (lbads != 9 && lbads != 12) {
    Log(kWarn, "nvme: unsupported sector size 2^%d\n", lbads);
    return false;
  }
  sector_size_ = 1u << lbads;
  if (native_sectors_ == 0) return false;

  bounce_ = static_cast<uint8_t*>(AllocDmaPages(max_bytes_));
  return bounce_ != nullptr;
}

// 512 バイト単位の要求を、デバイス固有のセクタ(512 or 4096)単位の転送に変換する
bool NvmeDevice::Transfer(bool write, uint64_t lba, void* buf, size_t sectors) {
  if (lba + sectors > SectorCount()) return false;
  const uint32_t ratio = sector_size_ / 512;
  auto* p = static_cast<uint8_t*>(buf);

  // 直接転送できる場合(バッファが4KiB境界で、セクタ境界に揃っている)
  if ((reinterpret_cast<uintptr_t>(p) & 0xfff) == 0 && lba % ratio == 0 && sectors % ratio == 0) {
    uint64_t native = lba / ratio;
    uint64_t left = sectors / ratio;
    const uint32_t chunk = max_bytes_ / sector_size_;
    while (left > 0) {
      const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(left, chunk));
      if (!Rw(write, native, p, n)) return false;
      native += n; left -= n; p += static_cast<uint64_t>(n) * sector_size_;
    }
    return true;
  }

  // バウンスバッファ経由(必要なら読み出し→書き換え→書き込み)
  while (sectors > 0) {
    const uint64_t native = lba / ratio;
    const uint32_t offset = (lba % ratio) * 512;
    const size_t max_sectors = (max_bytes_ - offset) / 512;
    const size_t take = std::min(sectors, max_sectors);
    const uint32_t bytes = take * 512;
    const uint32_t native_count = (offset + bytes + sector_size_ - 1) / sector_size_;
    if (write) {
      if ((offset != 0 || bytes % sector_size_ != 0) && !Rw(false, native, bounce_, native_count)) {
        return false;
      }
      memcpy(bounce_ + offset, p, bytes);
      if (!Rw(true, native, bounce_, native_count)) return false;
    } else {
      if (!Rw(false, native, bounce_, native_count)) return false;
      memcpy(p, bounce_ + offset, bytes);
    }
    lba += take; sectors -= take; p += bytes;
  }
  return true;
}

}  // namespace

namespace nvme {

void Probe(std::vector<BlockDevice*>& out) {
  int index = 0;
  for (auto& dev : pci::devices) {
    if (!dev.class_code.Match(0x01, 0x08, 0x02)) continue;
    auto* d = new NvmeDevice(index);
    if (d->Init(dev)) {
      out.push_back(d);
      ++index;
    } else {
      Log(kWarn, "nvme: initialization failed (%d:%d.%d)\n", dev.bus, dev.device, dev.function);
      delete d;
    }
  }
}

}  // namespace nvme
