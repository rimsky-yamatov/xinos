// tail: 末尾の行を表示する。 使い方: tail [-n N] [file]  (既定10行、最大64行)
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int kMaxKeep = 64, kLineLen = 256;
char ring[kMaxKeep][kLineLen];
}

int main(int argc, char** argv) {
  long n = 10;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) n = atol(argv[++i]);
    else path = argv[i];
  }
  if (n < 1) n = 1;
  if (n > kMaxKeep) n = kMaxKeep;
  FILE* fp = stdin;
  if (path) {
    fp = fopen(path, "r");
    if (!fp) { printf("failed to open: %s\n", path); return 1; }
  }
  long count = 0;
  while (fgets(ring[count % n], kLineLen, fp)) ++count;
  long start = count > n ? count - n : 0;
  for (long i = start; i < count; ++i) fputs(ring[i % n], stdout);
  return 0;
}
