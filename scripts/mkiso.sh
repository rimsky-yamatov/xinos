#!/bin/sh -eu
# UEFI起動可能な ISO (build/xinos.iso) を作る。事前に build-loader.sh / build-os.sh を実行すること。
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
"$ROOT/scripts/mkimage.sh"
rm -rf build/iso
mkdir -p build/iso
cp build/esp.img build/iso/efiboot.img
xorriso -as mkisofs -R -J -V XINOS \
  -e efiboot.img -no-emul-boot \
  -isohybrid-gpt-basdat \
  -o build/xinos.iso build/iso 2>&1 | tail -3
echo "built build/xinos.iso"
