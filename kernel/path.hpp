#pragma once

#include <cstddef>

/**
 * @file path.hpp
 * パス文字列の正規化(カレントディレクトリ基準の絶対パス化)。
 */
namespace path {

/** @brief path を cwd(絶対パス)基準で解決し、"." と ".." を取り除いた絶対パスを out に書く．
 *
 * 結果は常に '/' で始まり、ルート以外は末尾に '/' を付けない．
 * @return 成功なら true(結果が out_size に収まらないと false)
 */
bool Resolve(const char* cwd, const char* path, char* out, size_t out_size);

}  // namespace path
