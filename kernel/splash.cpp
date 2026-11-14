#include "splash.hpp"

#include <algorithm>
#include <cstdint>

#include "acpi.hpp"
#include "font.hpp"
#include "graphics.hpp"

namespace {

constexpr const char kLogo[] = "XinOS";
constexpr int kScale = 10;         // 8x16 フォントを何倍に拡大するか
constexpr int kLetterGap = 10;
constexpr int kBarW = 360, kBarH = 22;
constexpr int kBlockW = 18, kBlockH = 14, kBlockGap = 4, kBlocks = 4;

uint64_t tsc_per_ms = 0;
uint64_t start_tsc = 0;

inline uint64_t Rdtsc() { return __builtin_ia32_rdtsc(); }
unsigned long ElapsedMs() { return static_cast<unsigned long>((Rdtsc() - start_tsc) / tsc_per_ms); }

PixelColor Lerp(const PixelColor& a, const PixelColor& b, int num, int den) {
  return {static_cast<uint8_t>(a.r + (b.r - a.r) * num / den),
          static_cast<uint8_t>(a.g + (b.g - a.g) * num / den),
          static_cast<uint8_t>(a.b + (b.b - a.b) * num / den)};
}

// 金色(上: 明るい → 中央: 金 → 下: 濃い琥珀)の縦グラデーション
PixelColor GoldAt(int row, int rows) {
  const PixelColor top{255, 240, 160}, mid{255, 205, 50}, bottom{185, 120, 10};
  if (row * 2 < rows) return Lerp(top, mid, row * 2, rows);
  return Lerp(mid, bottom, row * 2 - rows, rows);
}

Vector2D<int> LogoOrigin() {
  const int n = sizeof(kLogo) - 1;
  const int w = n * 8 * kScale + (n - 1) * kLetterGap;
  const auto s = ScreenSize();
  return {(s.x - w) / 2, s.y / 2 - 16 * kScale / 2 - 70};
}

void DrawLogo() {
  auto& w = *screen_writer;
  const auto o = LogoOrigin();
  const int rows = 16 * kScale;
  // 影 → 本体 の順に描く
  for (int pass = 0; pass < 2; ++pass) {
    const int off = pass == 0 ? 6 : 0;
    for (int i = 0; kLogo[i]; ++i) {
      const uint8_t* g = GetFontGlyph(kLogo[i]);
      if (!g) continue;
      const int gx = o.x + i * (8 * kScale + kLetterGap);
      for (int row = 0; row < 16; ++row) {
        for (int col = 0; col < 8; ++col) {
          if (!((g[row] << col) & 0x80)) continue;
          for (int dy = 0; dy < kScale; ++dy) {
            const int y = row * kScale + dy;
            const PixelColor c = pass == 0 ? PixelColor{90, 62, 0} : GoldAt(y, rows);
            FillRectangle(w, {gx + col * kScale + off, o.y + y + off}, {kScale, 1}, c);
          }
        }
      }
    }
  }
}

Vector2D<int> BarOrigin() {
  const auto s = ScreenSize();
  return {(s.x - kBarW) / 2, s.y / 2 + 140};
}

void DrawBarFrame() {
  auto& w = *screen_writer;
  const auto o = BarOrigin();
  const PixelColor edge{190, 196, 214}, edge_dark{110, 116, 140};
  // 丸みのある外枠(角を1ピクセル欠いて丸く見せる)
  FillRectangle(w, {o.x + 2, o.y}, {kBarW - 4, kBarH}, edge);
  FillRectangle(w, {o.x, o.y + 2}, {kBarW, kBarH - 4}, edge);
  FillRectangle(w, {o.x + 1, o.y + 1}, {kBarW - 2, kBarH - 2}, edge);
  FillRectangle(w, {o.x + 2, o.y + kBarH - 2}, {kBarW - 4, 2}, edge_dark);
  FillRectangle(w, {o.x + 3, o.y + 3}, {kBarW - 6, kBarH - 6}, {0, 0, 0});
}

void DrawBlocks(int step) {
  auto& w = *screen_writer;
  const auto o = BarOrigin();
  const int in_x = o.x + 4, in_w = kBarW - 8;
  const int in_y = o.y + (kBarH - kBlockH) / 2;
  FillRectangle(w, {in_x, o.y + 3}, {in_w, kBarH - 6}, {0, 0, 0});
  const int pitch = kBlockW + kBlockGap;
  const int span = in_w + pitch * kBlocks;
  for (int b = 0; b < kBlocks; ++b) {
    const int x = in_x - pitch * kBlocks + (step * 6 + b * pitch) % span + pitch * kBlocks - pitch * kBlocks;
    const int bx = in_x + ((step * 6 + b * pitch) % span) - pitch * kBlocks;
    (void)x;
    // バーからはみ出す部分は描かない
    const int x0 = std::max(bx, in_x), x1 = std::min(bx + kBlockW, in_x + in_w);
    if (x1 <= x0) continue;
    for (int row = 0; row < kBlockH; ++row) {
      // 青のグラデーション(上が明るい)
      const PixelColor c = row < kBlockH / 2
        ? Lerp({150, 190, 255}, {60, 110, 235}, row, kBlockH / 2)
        : Lerp({60, 110, 235}, {25, 55, 175}, row - kBlockH / 2, kBlockH / 2);
      FillRectangle(w, {x0, in_y + row}, {x1 - x0, 1}, c);
    }
  }
}

}  // namespace

namespace splash {

void Show() {
  // TSC を ACPI PM タイマで較正する(10ms 測る)
  const uint64_t t0 = Rdtsc();
  acpi::WaitMilliseconds(10);
  tsc_per_ms = std::max<uint64_t>(1, (Rdtsc() - t0) / 10);
  start_tsc = Rdtsc();

  FillRectangle(*screen_writer, {0, 0}, ScreenSize(), {0, 0, 0});
  DrawLogo();
  DrawBarFrame();
  DrawBlocks(0);
}

void RunUntil(unsigned long total_ms) {
  int step = 1;
  while (ElapsedMs() < total_ms) {
    DrawBlocks(step++);
    acpi::WaitMilliseconds(45);
  }
}

}  // namespace splash
