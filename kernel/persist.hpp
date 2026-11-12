#pragma once

#include <cstddef>

/**
 * @file persist.hpp
 * 永続ディスク。NVMe / SATA(AHCI) / virtio-blk 上の、ボリュームラベルが XINOSDISK の
 * FAT32 ボリューム(ディスク全体またはGPT/MBRのパーティション)をRAMに読み込んでルートとして使い、
 * 変更があったブロックだけを書き戻す。ラベルが一致しないボリュームには一切書き込まない。
 */
namespace persist {

/** @brief 永続ディスクを探してボリュームをRAMに読み込む．見つかれば true． */
bool Mount();

/** @brief Mount 成功後のボリュームイメージ(FAT の BPB で始まる領域) */
void* Volume();

/** @brief ボリュームのバイト数 */
size_t VolumeBytes();

/** @brief 永続ディスクが使われているか */
bool IsMounted();

/** @brief "nvme0n1 (partition, LBA 2048)" のようなマウント元の説明 */
const char* Description();

/** @brief 変更されたブロックをディスクへ書き戻す．戻り値は書き戻したバイト数(失敗は負) */
long Sync();

}  // namespace persist
