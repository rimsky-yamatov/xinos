#!/bin/sh -eu
# QEMU (UEFI/OVMF) で ISO を起動する。 使い方: scripts/run.sh [iso]
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ISO=${1:-$ROOT/build/xinos.iso}
CODE=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
VARS=$ROOT/build/OVMF_VARS.fd
[ -f "$VARS" ] || cp "${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}" "$VARS"
exec qemu-system-x86_64 -m 1G -cpu qemu64 \
  -drive if=pflash,format=raw,readonly=on,file="$CODE" \
  -drive if=pflash,format=raw,file="$VARS" \
  -drive format=raw,media=cdrom,readonly=on,file="$ISO" \
  -device nec-usb-xhci,id=xhci -device usb-mouse -device usb-kbd \
  ${QEMU_OPTS:-}
