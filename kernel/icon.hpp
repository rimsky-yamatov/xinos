#pragma once

#include <cstdint>
#include <vector>

#include "graphics.hpp"

/**
 * @file icon.hpp
 * XIC アイコン("XIC1" + u8 幅 + u8 高さ + 32bit ARGB の並び)の読み込みと描画。
 */
struct Icon {
  int w{0}, h{0};
  std::vector<uint32_t> px;  // ARGB (A=0 は透過)
  bool Valid() const { return w > 0 && h > 0; }
};

/** @brief ボリューム上の絶対パスの XIC を読み込む。 */
bool LoadIcon(const char* abs_path, Icon* icon);

/** @brief icon を size x size に(最近傍で)拡縮し、bg の上に重ねて描く。 */
void DrawIcon(PixelWriter& writer, Vector2D<int> pos, const Icon& icon, int size,
              const PixelColor& bg);
