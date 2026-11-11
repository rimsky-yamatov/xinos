#!/bin/sh -eu
# MikanLoaderPkg (UEFIブートローダ) を edk2 でビルドし、Loader.efi を build/ に置く。
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/scripts/env.sh"
cd "$XINOS_EDK2"
export WORKSPACE=$PWD EDK_TOOLS_PATH=$PWD/BaseTools PACKAGES_PATH=$ROOT PYTHON_COMMAND=python3
export PATH=$PWD/BaseTools/BinWrappers/PosixLike:$PATH
[ -d MdePkg/Library/MipiSysTLib/mipisyst/library/include ] || \
  mkdir -p MdePkg/Library/MipiSysTLib/mipisyst/library/include
build -p MikanLoaderPkg/MikanLoaderPkg.dsc -b DEBUG -a X64 -t CLANGDWARF > /dev/null
mkdir -p "$ROOT/build"
cp Build/MikanLoaderX64/DEBUG_CLANGDWARF/X64/Loader.efi "$ROOT/build/Loader.efi"
echo "built build/Loader.efi"
