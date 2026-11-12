#include "path.hpp"

#include <cstring>

namespace path {

bool Resolve(const char* cwd, const char* path, char* out, size_t out_size) {
  constexpr int kMaxElems = 32;
  constexpr size_t kElemMax = 16;
  char elems[kMaxElems][kElemMax];
  int n = 0;

  // cwd(path が相対のとき) → path の順に区切りながら処理する
  const char* parts[2] = {path[0] == '/' ? "" : cwd, path};
  for (const char* part : parts) {
    const char* p = part;
    while (*p) {
      while (*p == '/') ++p;
      if (!*p) break;
      const char* end = p;
      while (*end && *end != '/') ++end;
      const size_t len = end - p;
      if (len == 1 && p[0] == '.') {
        // 何もしない
      } else if (len == 2 && p[0] == '.' && p[1] == '.') {
        if (n > 0) --n;
      } else {
        if (n >= kMaxElems || len >= kElemMax) return false;
        memcpy(elems[n], p, len);
        elems[n][len] = '\0';
        ++n;
      }
      p = end;
    }
  }

  size_t pos = 0;
  if (out_size < 2) return false;
  out[pos++] = '/';
  for (int i = 0; i < n; ++i) {
    const size_t len = strlen(elems[i]);
    if (pos + len + 2 > out_size) return false;
    memcpy(&out[pos], elems[i], len);
    pos += len;
    if (i + 1 < n) out[pos++] = '/';
  }
  out[pos] = '\0';
  return true;
}

}  // namespace path
