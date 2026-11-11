#!/bin/sh -eu
# カーネルと全アプリをビルドする。
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/scripts/env.sh"
cd "$ROOT"
make ${MAKE_OPTS:--j4} -C kernel kernel.elf
for MK in apps/*/Makefile; do
  APP_DIR=$(dirname "$MK")
  APP=$(basename "$APP_DIR")
  make ${MAKE_OPTS:--j4} -C "$APP_DIR" "$APP"
done
