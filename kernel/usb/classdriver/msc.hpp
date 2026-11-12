#pragma once

#include <cstdint>
#include <vector>

#include "usb/classdriver/base.hpp"

/**
 * @file usb/classdriver/msc.hpp
 * USB Mass Storage クラス(Bulk-Only Transport + SCSI 透過コマンドセット)のドライバ。
 * USB メモリや USB 接続の SSD/HDD を、他のストレージと同じ BlockDevice として使えるようにする。
 */
namespace usb::msc {

class MSCDriver : public ClassDriver {
 public:
  MSCDriver(Device* dev, uint8_t interface_number);

  Error Initialize() override { return MAKE_ERROR(Error::kNotImplemented); }
  Error SetEndpoint(const std::vector<EndpointConfig>& configs) override;
  Error OnEndpointsConfigured() override;
  Error OnControlCompleted(EndpointID ep_id, SetupData setup_data,
                           const void* buf, int len) override {
    return MAKE_ERROR(Error::kSuccess);
  }
  Error OnNormalCompleted(EndpointID ep_id, const void* buf, int len) override;

  /** @brief SCSI の初期化(INQUIRY / TEST UNIT READY / READ CAPACITY)。イベント処理の外から呼ぶこと。 */
  bool Probe();

  uint64_t Sectors() const { return sectors_; }  // 512 バイト単位
  bool Read(uint64_t lba, void* buf, size_t sectors);
  bool Write(uint64_t lba, const void* buf, size_t sectors);
  void Flush();

 private:
  bool WaitFor(volatile int* flag, int timeout_ms);
  bool Command(const uint8_t* cdb, uint8_t cdb_len, void* data, uint32_t len, bool data_in);

  uint8_t interface_number_;
  EndpointID ep_in_, ep_out_;
  bool has_in_{false}, has_out_{false};
  uint8_t* cbw_{nullptr};     // 31 バイト
  uint8_t* csw_{nullptr};     // 13 バイト
  uint8_t* bounce_{nullptr};  // 64KiB 境界に揃えたデータ用バッファ
  uint32_t tag_{0};
  uint64_t sectors_{0};
  volatile int out_done_{0}, in_done_{0};
  volatile int in_len_{0};
};

/** @brief エンドポイント設定が完了し、まだ Probe していない USB ストレージ */
std::vector<MSCDriver*>& Pending();

/** @brief Pending の各デバイスを Probe し、使えるものを BlockDevice として登録する。
 *  戻り値は新たに登録できたデバイス数。イベント処理の外(メインタスク等)から呼ぶこと。 */
int ProbePending();

}  // namespace usb::msc
