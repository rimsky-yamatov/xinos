#include "icon.hpp"

#include <cstring>

#include "fat.hpp"

bool LoadIcon(const char* abs_path, Icon* icon) {
  auto [ entry, post_slash ] = fat::FindFile(abs_path);
  if (entry == nullptr || entry->attr == fat::Attribute::kDirectory) {
    return false;
  }
  const size_t size = entry->file_size;
  if (size < 6 || size > 6 + 256 * 256 * 4) {
    return false;
  }
  std::vector<uint8_t> buf(size);
  if (fat::LoadFile(buf.data(), size, *entry) != size) {
    return false;
  }
  if (memcmp(buf.data(), "XIC1", 4) != 0) {
    return false;
  }
  const int w = buf[4], h = buf[5];
  if (w == 0 || h == 0 || size < 6 + static_cast<size_t>(w) * h * 4) {
    return false;
  }
  icon->w = w;
  icon->h = h;
  icon->px.resize(static_cast<size_t>(w) * h);
  memcpy(icon->px.data(), buf.data() + 6, static_cast<size_t>(w) * h * 4);
  return true;
}

void DrawIcon(PixelWriter& writer, Vector2D<int> pos, const Icon& icon, int size,
              const PixelColor& bg) {
  if (!icon.Valid()) {
    return;
  }
  for (int y = 0; y < size; ++y) {
    const int sy = y * icon.h / size;
    for (int x = 0; x < size; ++x) {
      const int sx = x * icon.w / size;
      const uint32_t p = icon.px[sy * icon.w + sx];
      const unsigned a = p >> 24;
      if (a == 0) {
        continue;
      }
      PixelColor c{static_cast<uint8_t>(p >> 16), static_cast<uint8_t>(p >> 8),
                   static_cast<uint8_t>(p)};
      if (a < 255) {
        c.r = (c.r * a + bg.r * (255 - a)) / 255;
        c.g = (c.g * a + bg.g * (255 - a)) / 255;
        c.b = (c.b * a + bg.b * (255 - a)) / 255;
      }
      writer.Write(pos + Vector2D<int>{x, y}, c);
    }
  }
}
