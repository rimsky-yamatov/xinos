# XinOS

MikanOS をベースにした x86-64 UEFI OS。

## 目標
- シェル拡張(help / ver / uptime / uname など)
- 永続ディスク、ディレクトリ
- テキストエディタ
- 簡易アセンブリ言語 `xasm` と、その上の簡易高水準言語
- 標準プログラムの充実
- デスクトップ、スタートメニュー、アプリ配置

## ベース
MikanOS (https://github.com/uchan-nos/mikanos) を元にしています。ライセンスは LICENSE / NOTICE を参照。

## ビルド

必要なもの: `clang` `lld` `nasm` `mtools` `xorriso` `python3` `build-essential` `uuid-dev` `acpica-tools`
(起動確認には `qemu-system-x86` と `ovmf`)

1. MikanOS用のx86_64-elf環境(newlib / libc++ / freetype)を展開する
   (https://github.com/uchan-nos/mikanos-build の `x86_64-elf.tar.gz`)
2. edk2 を取得し、`make -C BaseTools` でBaseToolsをビルドする
3. 環境変数を設定してビルドする

```sh
export XINOS_ELF_DIR=$HOME/osbook/x86_64-elf   # 手順1の展開先
export XINOS_EDK2=$HOME/edk2                   # 手順2のedk2
scripts/build-loader.sh   # build/Loader.efi
scripts/build-os.sh       # kernel/kernel.elf と apps/*
scripts/mkiso.sh          # build/xinos.iso
scripts/run.sh            # QEMU (UEFI) で起動
```

## 標準プログラム
- シェル組み込み: `help` `ver` `uname` `uptime` `echo` `clear` `ls` `cat` `date` `memstat` ほか
- `xasm <file.xa>`: 簡易アセンブラ+VM(サンプル: `hello.xa`)
