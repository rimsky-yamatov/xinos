#!/bin/sh -eu
# ブート用ESPイメージ(build/esp.img)とOSボリューム(build/fat_disk)を作る。root権限は不要(mtools使用)。
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p build
FAT=build/fat_disk
ESP=build/esp.img
export MTOOLS_SKIP_CHECK=1

# --- OSボリューム: アプリとリソースを格納 ---
rm -f "$FAT"
dd if=/dev/zero of="$FAT" bs=1M count=32 status=none
mformat -i "$FAT" -F -v XINOS ::
"$ROOT/scripts/populate.sh" "$FAT"

# --- ESP: ローダ + カーネル + OSボリューム ---
rm -f "$ESP"
dd if=/dev/zero of="$ESP" bs=1M count=80 status=none
mformat -i "$ESP" -F -v XINOSBOOT ::
mmd -i "$ESP" ::/EFI ::/EFI/BOOT
mcopy -i "$ESP" build/Loader.efi ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$ESP" kernel/kernel.elf ::/kernel.elf
mcopy -i "$ESP" "$FAT" ::/fat_disk
echo "built $ESP"
