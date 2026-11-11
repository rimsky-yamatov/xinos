# XinOS ビルド環境。`. scripts/env.sh` で読み込む。
# 必要なもの: clang lld nasm mtools xorriso (+ qemu-system-x86 ovmf)
#   XINOS_ELF_DIR : MikanOS用 x86_64-elf (newlib/libc++) の展開先
#   XINOS_EDK2    : edk2 のチェックアウト(BaseTools ビルド済み)
XINOS_ELF_DIR=${XINOS_ELF_DIR:-$HOME/osbook/x86_64-elf}
XINOS_EDK2=${XINOS_EDK2:-$HOME/edk2}
CLANG_INC=$(clang -print-resource-dir)/include

export CPPFLAGS="-I$XINOS_ELF_DIR/include/c++/v1 -I$XINOS_ELF_DIR/include \
-I$XINOS_ELF_DIR/include/freetype2 -I$CLANG_INC \
-I$XINOS_EDK2/MdePkg/Include -I$XINOS_EDK2/MdePkg/Include/X64 \
-nostdlibinc -D__ELF__ -D_LDBL_EQ_DBL -D_GNU_SOURCE -D_POSIX_TIMERS \
-DEFIAPI='__attribute__((ms_abi))'"
export LDFLAGS="-L$XINOS_ELF_DIR/lib"
