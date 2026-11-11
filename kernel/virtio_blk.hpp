#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @file virtio_blk.hpp
 * virtio-blk (legacy / I/Oポート版) のポーリング式ドライバ。
 * QEMU では `-device virtio-blk-pci,disable-modern=on` で使える。
 */
namespace virtio_blk {

constexpr size_t kSectorSize = 512;

/** @brief PCIから virtio-blk を探して初期化する．PCIスキャン後に呼ぶ． */
bool Initialize();

/** @brief ディスクの総セクタ数(未初期化なら0) */
uint64_t SectorCount();

/** @brief セクタを読む．buf は物理メモリ上で連続している(恒等写像の)領域であること． */
bool Read(uint64_t lba, void* buf, size_t sectors);

/** @brief セクタを書く．buf の条件は Read と同じ． */
bool Write(uint64_t lba, const void* buf, size_t sectors);

}  // namespace virtio_blk
