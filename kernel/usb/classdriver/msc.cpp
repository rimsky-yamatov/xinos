#include "usb/classdriver/msc.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "blockdev.hpp"
#include "logger.hpp"
#include "timer.hpp"
#include "usb/device.hpp"
#include "usb/xhci/xhci.hpp"

extern TimerManager* timer_manager;

namespace usb::msc {

namespace {

constexpr uint32_t kCbwSignature = 0x43425355;  // "USBC"
constexpr uint32_t kCswSignature = 0x53425355;  // "USBS"
constexpr uint32_t kChunkSectors = 64;          // 1回の転送は 32KiB

std::vector<MSCDriver*> pending;
int disk_count = 0;

void PutBe32(uint8_t* p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
void PutLe32(uint8_t* p, uint32_t v) {
  p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
uint32_t GetLe32(const uint8_t* p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t{p[3]} << 24);
}
uint32_t GetBe32(const uint8_t* p) {
  return (uint32_t{p[0]} << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

class UsbDisk : public BlockDevice {
 public:
  UsbDisk(MSCDriver* drv, int index) : drv_{drv} {
    snprintf(name_, sizeof(name_), "usb%d", index);
  }
  const char* Name() const override { return name_; }
  uint64_t SectorCount() const override { return drv_->Sectors(); }
  bool Read(uint64_t lba, void* buf, size_t n) override { return drv_->Read(lba, buf, n); }
  bool Write(uint64_t lba, const void* buf, size_t n) override { return drv_->Write(lba, buf, n); }
  void Flush() override { drv_->Flush(); }

 private:
  MSCDriver* drv_;
  char name_[16];
};

}  // namespace

MSCDriver::MSCDriver(Device* dev, uint8_t interface_number)
    : ClassDriver{dev}, interface_number_{interface_number} {}

Error MSCDriver::SetEndpoint(const std::vector<EndpointConfig>& configs) {
  for (const auto& c : configs) {
    if (c.ep_type != EndpointType::kBulk) continue;
    if (c.ep_id.IsIn()) { ep_in_ = c.ep_id; has_in_ = true; }
    else { ep_out_ = c.ep_id; has_out_ = true; }
  }
  return MAKE_ERROR(Error::kSuccess);
}

Error MSCDriver::OnEndpointsConfigured() {
  if (has_in_ && has_out_) {
    pending.push_back(this);  // SCSI の初期化はイベント処理の外で行う
  }
  return MAKE_ERROR(Error::kSuccess);
}

Error MSCDriver::OnNormalCompleted(EndpointID ep_id, const void* buf, int len) {
  if (ep_id == ep_out_) {
    out_done_ = 1;
  } else if (ep_id == ep_in_) {
    in_len_ = len;
    in_done_ = 1;
  } else {
    return MAKE_ERROR(Error::kEndpointNotInCharge);
  }
  return MAKE_ERROR(Error::kSuccess);
}

// イベントを処理しながら flag が立つのを待つ
bool MSCDriver::WaitFor(volatile int* flag, int timeout_ms) {
  const unsigned long deadline =
    timer_manager->CurrentTick() + static_cast<unsigned long>(timeout_ms) * kTimerFreq / 1000 + 1;
  while (!*flag) {
    usb::xhci::ProcessEvents();
    if (timer_manager->CurrentTick() > deadline) {
      return false;
    }
    __builtin_ia32_pause();
  }
  return true;
}

bool MSCDriver::Command(const uint8_t* cdb, uint8_t cdb_len, void* data, uint32_t len,
                        bool data_in) {
  auto* dev = ParentDevice();
  memset(cbw_, 0, 31);
  PutLe32(cbw_, kCbwSignature);
  const uint32_t tag = ++tag_;
  PutLe32(cbw_ + 4, tag);
  PutLe32(cbw_ + 8, len);
  cbw_[12] = data_in ? 0x80 : 0;
  cbw_[14] = cdb_len;
  memcpy(cbw_ + 15, cdb, cdb_len);

  out_done_ = 0;
  if (dev->NormalOut(ep_out_, cbw_, 31)) return false;
  if (!WaitFor(&out_done_, 3000)) {
    Log(kWarn, "usb-msc: CBW timeout\n");
    return false;
  }

  if (len > 0) {
    if (data_in) {
      in_done_ = 0;
      if (dev->NormalIn(ep_in_, bounce_, len)) return false;
      if (!WaitFor(&in_done_, 5000)) { Log(kWarn, "usb-msc: data-in timeout\n"); return false; }
      memcpy(data, bounce_, std::min<uint32_t>(len, in_len_));
    } else {
      memcpy(bounce_, data, len);
      out_done_ = 0;
      if (dev->NormalOut(ep_out_, bounce_, len)) return false;
      if (!WaitFor(&out_done_, 5000)) { Log(kWarn, "usb-msc: data-out timeout\n"); return false; }
    }
  }

  in_done_ = 0;
  memset(csw_, 0, 13);
  if (dev->NormalIn(ep_in_, csw_, 13)) return false;
  if (!WaitFor(&in_done_, 5000)) { Log(kWarn, "usb-msc: CSW timeout\n"); return false; }
  return GetLe32(csw_) == kCswSignature && GetLe32(csw_ + 4) == tag && csw_[12] == 0;
}

bool MSCDriver::Probe() {
  // CBW/CSW 用に1ページ、データ用に 64KiB 境界に揃えた領域を確保する
  auto* page = static_cast<uint8_t*>(AllocDmaPages(4096));
  auto* raw = static_cast<uint8_t*>(AllocDmaPages(128 * 1024 + 64 * 1024));
  if (!page || !raw) return false;
  cbw_ = page;
  csw_ = page + 512;
  bounce_ = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(raw) + 0xffff) & ~uintptr_t{0xffff});

  uint8_t inq[36] = {};
  const uint8_t cdb_inq[6] = {0x12, 0, 0, 0, 36, 0};
  if (Command(cdb_inq, 6, inq, 36, true)) {
    char vendor[9] = {}, product[17] = {};
    memcpy(vendor, inq + 8, 8);
    memcpy(product, inq + 16, 16);
    Log(kInfo, "usb-msc: %s %s\n", vendor, product);
  }

  // メディアの準備ができるまで待つ(失敗したら REQUEST SENSE でUnit Attentionを消す)
  bool ready = false;
  for (int i = 0; i < 15 && !ready; ++i) {
    const uint8_t cdb_tur[6] = {0x00, 0, 0, 0, 0, 0};
    if (Command(cdb_tur, 6, nullptr, 0, false)) {
      ready = true;
    } else {
      uint8_t sense[18] = {};
      const uint8_t cdb_rs[6] = {0x03, 0, 0, 0, 18, 0};
      Command(cdb_rs, 6, sense, 18, true);
      const unsigned long until = timer_manager->CurrentTick() + kTimerFreq / 5;
      while (timer_manager->CurrentTick() < until) usb::xhci::ProcessEvents();
    }
  }
  if (!ready) {
    Log(kWarn, "usb-msc: device not ready\n");
    return false;
  }

  uint8_t cap[8] = {};
  const uint8_t cdb_cap[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  if (!Command(cdb_cap, 10, cap, 8, true)) {
    Log(kWarn, "usb-msc: READ CAPACITY failed\n");
    return false;
  }
  const uint32_t last_lba = GetBe32(cap);
  const uint32_t block_size = GetBe32(cap + 4);
  if (block_size != 512) {
    Log(kWarn, "usb-msc: unsupported block size %u\n", block_size);
    return false;
  }
  sectors_ = uint64_t{last_lba} + 1;
  return true;
}

bool MSCDriver::Read(uint64_t lba, void* buf, size_t sectors) {
  if (lba + sectors > sectors_) return false;
  auto* p = static_cast<uint8_t*>(buf);
  while (sectors > 0) {
    const uint32_t n = std::min<size_t>(sectors, kChunkSectors);
    uint8_t cdb[10] = {0x28, 0};
    PutBe32(cdb + 2, static_cast<uint32_t>(lba));
    cdb[7] = n >> 8; cdb[8] = n;
    if (!Command(cdb, 10, p, n * 512, true)) return false;
    lba += n; sectors -= n; p += n * 512;
  }
  return true;
}

bool MSCDriver::Write(uint64_t lba, const void* buf, size_t sectors) {
  if (lba + sectors > sectors_) return false;
  auto* p = const_cast<uint8_t*>(static_cast<const uint8_t*>(buf));
  while (sectors > 0) {
    const uint32_t n = std::min<size_t>(sectors, kChunkSectors);
    uint8_t cdb[10] = {0x2a, 0};
    PutBe32(cdb + 2, static_cast<uint32_t>(lba));
    cdb[7] = n >> 8; cdb[8] = n;
    if (!Command(cdb, 10, p, n * 512, false)) return false;
    lba += n; sectors -= n; p += n * 512;
  }
  return true;
}

void MSCDriver::Flush() {
  const uint8_t cdb[10] = {0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // SYNCHRONIZE CACHE(10)
  Command(cdb, 10, nullptr, 0, false);  // 非対応デバイスもあるので失敗は無視
}

std::vector<MSCDriver*>& Pending() { return pending; }

int ProbePending() {
  int registered = 0;
  while (!pending.empty()) {
    MSCDriver* drv = pending.back();
    pending.pop_back();
    if (drv->Probe()) {
      RegisterBlockDevice(new UsbDisk(drv, disk_count++));
      ++registered;
    }
  }
  return registered;
}

}  // namespace usb::msc
