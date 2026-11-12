#include "ahci.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "logger.hpp"
#include "paging.hpp"
#include "pci.hpp"

namespace {

constexpr long kSpinLimit = 400000000;
constexpr size_t kMaxSectorsPerCommand = 256;

// HBA レジスタ
constexpr uint32_t kHbaCap = 0x00, kHbaGhc = 0x04, kHbaIs = 0x08, kHbaPi = 0x0c;
// ポートレジスタ(0x100 + 0x80 * n からのオフセット)
constexpr uint32_t kPxClb = 0x00, kPxClbu = 0x04, kPxFb = 0x08, kPxFbu = 0x0c,
                   kPxIs = 0x10, kPxIe = 0x14, kPxCmd = 0x18, kPxTfd = 0x20,
                   kPxSig = 0x24, kPxSsts = 0x28, kPxSerr = 0x30, kPxSact = 0x34,
                   kPxCi = 0x38;
constexpr uint32_t kCmdSt = 1u << 0, kCmdFre = 1u << 4, kCmdFr = 1u << 14, kCmdCr = 1u << 15;

class AhciDisk : public BlockDevice {
 public:
  AhciDisk(int index, volatile uint8_t* port, bool s64a) : port_{port}, s64a_{s64a} {
    snprintf(name_, sizeof(name_), "sata%d", index);
  }

  bool Init();

  const char* Name() const override { return name_; }
  uint64_t SectorCount() const override { return sectors_; }

  bool Read(uint64_t lba, void* buf, size_t n) override { return Transfer(false, lba, buf, n); }
  bool Write(uint64_t lba, const void* buf, size_t n) override {
    return Transfer(true, lba, const_cast<void*>(buf), n);
  }

 private:
  uint32_t R(uint32_t off) const { return *reinterpret_cast<volatile uint32_t*>(port_ + off); }
  void W(uint32_t off, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(port_ + off) = v; }

  bool Wait(uint32_t off, uint32_t mask, uint32_t value) {
    for (long spin = 0; spin < kSpinLimit; ++spin) {
      if ((R(off) & mask) == value) return true;
      __builtin_ia32_pause();
    }
    return false;
  }

  bool StopEngine() {
    W(kPxCmd, R(kPxCmd) & ~kCmdSt);
    if (!Wait(kPxCmd, kCmdCr, 0)) return false;
    W(kPxCmd, R(kPxCmd) & ~kCmdFre);
    return Wait(kPxCmd, kCmdFr, 0);
  }

  bool Issue(uint8_t command, uint64_t lba, uint16_t count, void* buf, size_t bytes,
             bool write);
  bool Transfer(bool write, uint64_t lba, void* buf, size_t sectors);

  char name_[16];
  volatile uint8_t* port_;
  bool s64a_;
  uint8_t* page_{nullptr};  // +0: コマンドリスト  +1024: 受信FIS  +2048: コマンドテーブル
  uint64_t sectors_{0};
};

bool AhciDisk::Init() {
  page_ = static_cast<uint8_t*>(AllocDmaPages(4096));
  if (!page_) return false;
  const uint64_t base = reinterpret_cast<uint64_t>(page_);
  if (!s64a_ && base >= (1ull << 32)) return false;

  if (!StopEngine()) return false;
  W(kPxClb, base & 0xffffffff);
  W(kPxClbu, base >> 32);
  W(kPxFb, (base + 1024) & 0xffffffff);
  W(kPxFbu, (base + 1024) >> 32);
  W(kPxSerr, 0xffffffff);
  W(kPxIs, 0xffffffff);
  W(kPxIe, 0);
  if (!Wait(kPxTfd, 0x88, 0)) return false;  // BSY | DRQ が落ちるまで
  W(kPxCmd, R(kPxCmd) | kCmdFre);
  W(kPxCmd, R(kPxCmd) | kCmdSt);

  // IDENTIFY DEVICE
  auto* id = static_cast<uint16_t*>(AllocDmaPages(4096));
  if (!id || !Issue(0xec, 0, 0, id, 512, false)) return false;
  uint64_t sectors = 0;
  if (id[83] & (1u << 10)) {  // LBA48
    sectors = id[100] | (uint64_t{id[101]} << 16) | (uint64_t{id[102]} << 32) |
              (uint64_t{id[103]} << 48);
  }
  if (sectors == 0) {
    sectors = id[60] | (uint64_t{id[61]} << 16);
  }
  // 論理セクタが 512 バイトのディスクだけ扱う(512e は物理4Kでも論理512なので可)
  uint32_t logical = 512;
  if ((id[106] & 0xc000) == 0x4000 && (id[106] & (1u << 12))) {
    logical = (id[117] | (uint32_t{id[118]} << 16)) * 2;
  }
  if (logical != 512 || sectors == 0) {
    Log(kWarn, "ahci: unsupported disk (logical sector %u bytes)\n", logical);
    return false;
  }
  sectors_ = sectors;
  return true;
}

bool AhciDisk::Issue(uint8_t command, uint64_t lba, uint16_t count, void* buf,
                     size_t bytes, bool write) {
  const uint64_t buf_addr = reinterpret_cast<uint64_t>(buf);
  if (!s64a_ && buf_addr + bytes > (1ull << 32)) return false;
  const uint64_t ct = reinterpret_cast<uint64_t>(page_ + 2048);

  // コマンドヘッダ(スロット0)
  auto* hdr = reinterpret_cast<volatile uint32_t*>(page_);
  hdr[0] = 5 | (write ? (1u << 6) : 0) | (1u << 16);  // CFL=5dw, W, PRDTL=1
  hdr[1] = 0;
  hdr[2] = ct & 0xffffffff;
  hdr[3] = ct >> 32;

  // コマンドテーブル: CFIS + PRDT
  auto* t = page_ + 2048;
  memset(t, 0, 256);
  t[0] = 0x27;  // Register H2D
  t[1] = 0x80;  // C=1
  t[2] = command;
  if (command != 0xec) {
    t[7] = 0x40;  // LBA モード
    t[4] = lba & 0xff; t[5] = (lba >> 8) & 0xff; t[6] = (lba >> 16) & 0xff;
    t[8] = (lba >> 24) & 0xff; t[9] = (lba >> 32) & 0xff; t[10] = (lba >> 40) & 0xff;
    t[12] = count & 0xff; t[13] = count >> 8;
  }
  auto* prd = reinterpret_cast<uint32_t*>(t + 0x80);
  prd[0] = buf_addr & 0xffffffff;
  prd[1] = buf_addr >> 32;
  prd[2] = 0;
  prd[3] = static_cast<uint32_t>(bytes - 1);

  __asm__ volatile("" ::: "memory");
  W(kPxIs, 0xffffffff);
  W(kPxCi, 1);
  for (long spin = 0; spin < kSpinLimit; ++spin) {
    if ((R(kPxCi) & 1) == 0) {
      if (R(kPxIs) & (1u << 30) || (R(kPxTfd) & 0x01)) {
        Log(kWarn, "ahci: %s: command 0x%x failed (TFD=0x%x)\n", name_, command, R(kPxTfd));
        return false;
      }
      return true;
    }
    if (R(kPxIs) & (1u << 30)) {  // Task File Error
      Log(kWarn, "ahci: %s: task file error\n", name_);
      return false;
    }
    __builtin_ia32_pause();
  }
  Log(kError, "ahci: %s: command timeout\n", name_);
  return false;
}

bool AhciDisk::Transfer(bool write, uint64_t lba, void* buf, size_t sectors) {
  if (lba + sectors > sectors_) return false;
  auto* p = static_cast<uint8_t*>(buf);
  while (sectors > 0) {
    const size_t n = std::min(sectors, kMaxSectorsPerCommand);
    if (!Issue(write ? 0x35 : 0x25, lba, static_cast<uint16_t>(n), p, n * 512, write)) {
      return false;
    }
    lba += n; sectors -= n; p += n * 512;
  }
  return true;
}

}  // namespace

namespace ahci {

void Probe(std::vector<BlockDevice*>& out) {
  int index = 0;
  for (auto& dev : pci::devices) {
    if (!dev.class_code.Match(0x01, 0x06, 0x01)) continue;

    pci::WriteConfReg(dev, 0x04, pci::ReadConfReg(dev, 0x04) | 0x6 | 0x400);
    auto [ bar, err ] = pci::ReadBar(dev, 5);
    if (err) continue;
    const uint64_t abar = bar & ~0xfull;
    if (abar == 0 || MapIdentityMMIO(abar, 0x2000)) {
      continue;
    }
    auto* hba = reinterpret_cast<volatile uint8_t*>(abar);
    auto reg = [&](uint32_t off) -> volatile uint32_t& {
      return *reinterpret_cast<volatile uint32_t*>(hba + off);
    };
    reg(kHbaGhc) = (reg(kHbaGhc) | (1u << 31)) & ~2u;  // AHCI有効 / 割り込み無効
    const bool s64a = (reg(kHbaCap) >> 31) & 1;
    const uint32_t implemented = reg(kHbaPi);

    for (int port = 0; port < 32; ++port) {
      if (!(implemented & (1u << port))) continue;
      volatile uint8_t* p = hba + 0x100 + 0x80 * port;
      const uint32_t ssts = *reinterpret_cast<volatile uint32_t*>(p + kPxSsts);
      const uint32_t sig = *reinterpret_cast<volatile uint32_t*>(p + kPxSig);
      if ((ssts & 0xf) != 3 || ((ssts >> 8) & 0xf) != 1) continue;  // リンク確立済みのみ
      if (sig != 0x00000101) continue;                                // SATA ディスクのみ
      auto* d = new AhciDisk(index, p, s64a);
      if (d->Init()) {
        out.push_back(d);
        ++index;
      } else {
        delete d;
      }
    }
  }
}

}  // namespace ahci
