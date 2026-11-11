#!/bin/sh -eu
# FATボリュームイメージ($1)にアプリとリソースを(上書きで)コピーする。既存のユーザーファイルは消さない。
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
IMG=$1
export MTOOLS_SKIP_CHECK=1
mmd -i "$IMG" ::/apps 2>/dev/null || true
for MK in apps/*/Makefile; do
  APP=$(basename "$(dirname "$MK")")
  [ -f "apps/$APP/$APP" ] && mcopy -o -i "$IMG" "apps/$APP/$APP" "::/apps/$APP"
done
for F in resource/*; do
  [ -f "$F" ] && mcopy -o -i "$IMG" "$F" "::/$(basename "$F")"
done
# 付属のサンプル(あれば)
for F in apps/*/*.xa apps/*/*.xh; do
  [ -f "$F" ] && mcopy -o -i "$IMG" "$F" "::/$(basename "$F")"
done
exit 0
