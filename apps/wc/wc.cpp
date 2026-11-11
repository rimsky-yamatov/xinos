// wc: 行数・単語数・バイト数を数える。 使い方: wc [file]  (省略時は標準入力)
#include <cctype>
#include <cstdio>

int main(int argc, char** argv) {
  FILE* fp = stdin;
  if (argc >= 2) {
    fp = fopen(argv[1], "r");
    if (!fp) { printf("failed to open: %s\n", argv[1]); return 1; }
  }
  long lines = 0, words = 0, bytes = 0;
  bool in_word = false;
  int c;
  while ((c = fgetc(fp)) != EOF) {
    ++bytes;
    if (c == '\n') ++lines;
    if (isspace(c)) in_word = false;
    else if (!in_word) { in_word = true; ++words; }
  }
  printf("%ld %ld %ld\n", lines, words, bytes);
  return 0;
}
