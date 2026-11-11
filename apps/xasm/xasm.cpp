// xasm: XinOS 簡易アセンブラ + VM
// 使い方: xasm <file.xa>   (命令一覧は xvm.hpp を参照)
#include <cstdio>

#include "xvm.hpp"

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: xasm <file.xa>\n"); return 1; }
  FILE* fp = fopen(argv[1], "r");
  if (!fp) { printf("failed to open: %s\n", argv[1]); return 1; }
  xvm::Reset();
  bool ok = xvm::AssembleFile(fp);
  fclose(fp);
  return ok ? xvm::Run() : 1;
}
