#!/bin/sh -eu
# 実機のディスク(NVMe / SATA / USB)へ書き込めるGPT付きイメージ build/xinos-install.img を作る。
#   p1: EFIシステムパーティション (EFI/BOOT/BOOTX64.EFI, kernel.elf, fat_disk=RAMボリューム用の予備)
#   p2: 永続ボリューム (FAT32, ラベル XINOSDISK) -- アプリ・サンプル・/home など
# 使い方: scripts/mkinstall.sh [データ領域のMiB(既定192, 最大256)]
# 永続ボリュームは起動時に全体をRAMへ読み込むため、データ領域は最大256MiBです。
# 事前に build-loader.sh と build-os.sh を実行しておくこと。
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
DATA_MIB=${1:-192}
IMG=${XINOS_INSTALL_IMG:-build/xinos-install.img}
export MTOOLS_SKIP_CHECK=1
[ "$DATA_MIB" -ge 40 ] && [ "$DATA_MIB" -le 256 ] || { echo "データ領域は40〜256MiBで指定してください" >&2; exit 1; }
ESP_MIB=80
SIZE=$((1 + ESP_MIB + DATA_MIB + 2))   # 先頭1MiB + ESP + データ + 末尾2MiB(GPTバックアップ)

# ESP に入れる予備のRAMボリューム(build/fat_disk)を最新にする
"$ROOT/scripts/mkimage.sh" > /dev/null

ESP_START=2048
ESP_SECT=$((ESP_MIB * 2048))
DATA_START=$((ESP_START + ESP_SECT))
DATA_SECT=$((DATA_MIB * 2048))

rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1M count="$SIZE" status=none
sfdisk -q "$IMG" <<EOT
label: gpt
start=$ESP_START, size=$ESP_SECT, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name="XINOS-ESP"
start=$DATA_START, size=$DATA_SECT, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, name="XINOS-DATA"
EOT

ESP="$IMG@@$((ESP_START * 512))"
DATA="$IMG@@$((DATA_START * 512))"
mformat -i "$ESP" -T "$ESP_SECT" -F -v XINOSBOOT ::
mmd -i "$ESP" ::/EFI ::/EFI/BOOT
mcopy -i "$ESP" build/Loader.efi ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$ESP" kernel/kernel.elf ::/kernel.elf
mcopy -i "$ESP" build/fat_disk ::/fat_disk

mformat -i "$DATA" -T "$DATA_SECT" -F -v XINOSDISK ::
"$ROOT/scripts/populate.sh" "$DATA"
echo "built $IMG (${SIZE}MiB: ESP ${ESP_MIB}MiB + XINOSDISK ${DATA_MIB}MiB)"
