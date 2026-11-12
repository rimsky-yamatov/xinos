#!/bin/sh -eu
# インストール用イメージ(build/xinos-install.img)をディスクとして直接起動する(QEMU/UEFI)。
# 使い方: scripts/run-disk.sh [nvme|ahci|virtio|usb]    (既定 nvme)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${XINOS_INSTALL_IMG:-$ROOT/build/xinos-install.img}
CODE=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
VARS=$ROOT/build/OVMF_VARS_disk.fd
[ -f "$VARS" ] || cp "${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}" "$VARS"
case "${1:-nvme}" in
  nvme)   DISK="-drive file=$IMG,if=none,id=d0,format=raw -device nvme,drive=d0,serial=xinos0,bootindex=0" ;;
  ahci)   DISK="-drive file=$IMG,if=none,id=d0,format=raw -device ich9-ahci,id=ahci -device ide-hd,drive=d0,bus=ahci.0,bootindex=0" ;;
  virtio) DISK="-drive file=$IMG,if=none,id=d0,format=raw -device virtio-blk-pci,drive=d0,disable-modern=on,bootindex=0" ;;
  usb)    DISK="-drive file=$IMG,if=none,id=d0,format=raw -device usb-storage,drive=d0,bus=xhci.0,bootindex=0" ;;
  *) echo "usage: $0 [nvme|ahci|virtio|usb]" >&2; exit 1 ;;
esac
exec qemu-system-x86_64 -m 1G -cpu qemu64 \
  -drive if=pflash,format=raw,readonly=on,file="$CODE" \
  -drive if=pflash,format=raw,file="$VARS" \
  -device nec-usb-xhci,id=xhci -device usb-mouse -device usb-kbd \
  $DISK \
  ${QEMU_OPTS:-}
