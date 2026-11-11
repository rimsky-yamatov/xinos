#!/bin/sh -eu
# 永続ディスク(build/disk.img)を作る。 使い方: scripts/mkdisk.sh [サイズMiB(既定64)]
# 既にある場合は中身を消さずに、アプリとリソースだけを最新のビルドで更新する。
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
DISK=${XINOS_DISK:-build/disk.img}
SIZE=${1:-64}
export MTOOLS_SKIP_CHECK=1
mkdir -p "$(dirname "$DISK")"
if [ ! -f "$DISK" ]; then
  dd if=/dev/zero of="$DISK" bs=1M count="$SIZE" status=none
  mformat -i "$DISK" -F -v XINOSDISK ::
  echo "created $DISK (${SIZE}MiB)"
else
  echo "updating system files in $DISK"
fi
"$ROOT/scripts/populate.sh" "$DISK"
echo "ready: $DISK"
