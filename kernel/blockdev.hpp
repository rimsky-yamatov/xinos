#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * @file blockdev.hpp
 * ブロックデバイスの共通インターフェース。virtio-blk / NVMe / AHCI(SATA) が実装する。
 * 上位層(永続ディスク)は 512 バイト単位のセクタ番号だけを扱う。
 */
class BlockDevice {
 public:
  virtual ~BlockDevice() = default;

  /** @brief "nvme0n1" のような表示名 */
  virtual const char* Name() const = 0;

  /** @brief 総容量(512 バイト単位) */
  virtual uint64_t SectorCount() const = 0;

  /** @brief パーティションテーブルが使う論理セクタのバイト数(512 または 4096) */
  virtual uint32_t LogicalSectorSize() const { return 512; }

  /** @brief 512 バイト単位の lba から sectors 個を読む */
  virtual bool Read(uint64_t lba, void* buf, size_t sectors) = 0;

  /** @brief 512 バイト単位の lba へ sectors 個を書く */
  virtual bool Write(uint64_t lba, const void* buf, size_t sectors) = 0;
};

/** @brief 見つかったブロックデバイスの一覧 */
const std::vector<BlockDevice*>& BlockDevices();

/** @brief PCI スキャン後に1度だけ呼ぶ。virtio-blk / NVMe / AHCI を探して登録する。 */
void InitializeBlockDevices();

/** @brief DMA 用に 4KiB 境界の物理連続領域を確保してゼロクリアする。失敗時は nullptr。 */
void* AllocDmaPages(size_t bytes);
