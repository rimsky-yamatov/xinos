#pragma once

#include <cstddef>
#include <cstdint>

#include "error.hpp"

/**
 * @file fsops.hpp
 * ファイルマネージャ等から使うファイル操作(ディレクトリ一覧・ゴミ箱)。
 * パスはすべて絶対パスで渡す。
 */
namespace fsops {

/** アプリ側(apps/syscall.h の struct DirInfo)と同じレイアウト */
struct DirInfo {
  char name[16];     // "NAME.EXT" 形式(大文字)
  uint8_t is_dir;
  uint8_t pad[3];
  uint32_t size;
};

/** @brief path の index 番目の項目を取得する。なければ false。 */
bool ReadDir(const char* path, int index, DirInfo* out);

/** @brief src を dst へ移動・改名する。dst が既存ディレクトリならその中へ移す。 */
Error Move(const char* src, const char* dst);

/** @brief ディレクトリごと完全に削除する。 */
Error RemoveRecursive(const char* path);

/** @brief ファイル/ディレクトリを /trash へ移す(元の場所を記録する)。 */
Error Trash(const char* path);

/** @brief /trash の中身を全て完全に削除する。 */
Error EmptyTrash();

/** @brief /trash 内の name を、記録した元の場所(無ければ /home)へ戻す。 */
Error Restore(const char* name);

/** @brief ゴミ箱が空か */
bool TrashIsEmpty();

}  // namespace fsops
