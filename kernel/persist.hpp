#pragma once

#include <cstddef>

/**
 * @file persist.hpp
 * 永続ディスク。virtio-blk ディスク上の FAT32 ボリュームをRAMに読み込んで
 * ルートボリュームとして使い、変更があったブロックだけをディスクへ書き戻す。
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

/** @brief 変更されたブロックをディスクへ書き戻す．戻り値は書き戻したバイト数(失敗は負) */
long Sync();

}  // namespace persist
