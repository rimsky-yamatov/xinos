#!/bin/sh -eu
# QEMU (UEFI/OVMF) で ISO を起動する。 使い方: scripts/run.sh [iso]
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/xinos.iso}
CODE=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
VARS=$ROOT/build/OVMF_VARS.fd
[ -f "$VARS" ] || cp "${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}" "$VARS"
# 永続ディスク(build/disk.img)があれば virtio-blk として接続する
DISK=${XINOS_DISK:-$ROOT/build/disk.img}
DISK_OPTS=""
# XINOS_DISK_BUS: virtio(既定) / nvme / ahci(SATA) / usb(USBメモリ)
if [ -f "$DISK" ]; then
  case "${XINOS_DISK_BUS:-virtio}" in
    nvme)
      DISK_OPTS="-drive file=$DISK,if=none,id=xinosdisk,format=raw -device nvme,drive=xinosdisk,serial=xinos0" ;;
    usb)
      DISK_OPTS="-drive file=$DISK,if=none,id=xinosdisk,format=raw -device usb-storage,drive=xinosdisk,bus=xhci.0" ;;
    ahci)
      DISK_OPTS="-drive file=$DISK,if=none,id=xinosdisk,format=raw -device ich9-ahci,id=ahci -device ide-hd,drive=xinosdisk,bus=ahci.0" ;;
    *)
      DISK_OPTS="-drive file=$DISK,if=none,id=xinosdisk,format=raw -device virtio-blk-pci,drive=xinosdisk,disable-modern=on" ;;
  esac
fi
exec qemu-system-x86_64 -m 1G -cpu qemu64 \
  -drive if=pflash,format=raw,readonly=on,file="$CODE" \
  -drive if=pflash,format=raw,file="$VARS" \
  -drive format=raw,media=cdrom,readonly=on,file="$ISO" \
  -device nec-usb-xhci,id=xhci -device usb-mouse -device usb-kbd \
  $DISK_OPTS ${QEMU_OPTS:-}
