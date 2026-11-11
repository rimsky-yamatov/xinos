// head: 先頭の行を表示する。 使い方: head [-n N] [file]  (既定10行)
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
  long n = 10;
  const char* path = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) n = atol(argv[++i]);
    else path = argv[i];
  }
  FILE* fp = stdin;
  if (path) {
    fp = fopen(path, "r");
    if (!fp) { printf("failed to open: %s\n", path); return 1; }
  }
  char line[512];
  for (long i = 0; i < n && fgets(line, sizeof line, fp); ++i) fputs(line, stdout);
  return 0;
}
