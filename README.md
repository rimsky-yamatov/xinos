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

## デスクトップ
- 左下の「XinOS」ボタンでスタートメニュー(Terminal / Editor / Paint / Viewer / Blocks / Cube / Eye / Stars / Reboot / Shut down)
- デスクトップ左のアイコンをダブルクリックで起動

## 標準プログラム
- シェル組み込み: `help` `ver` `uname` `uptime` `echo` `clear` `ls` `cat` `date` `memstat` ほか
- `xasm <file.xa>`: 簡易アセンブラ+VM(命令は `apps/xasm/xvm.hpp` 参照、サンプル `hello.xa`)
- `xhl [-S] <file.xh>`: 簡易高水準言語。xasmへコンパイルして実行(`-S` で生成コードを表示)
  - 入出力はC風: `printf("x=%d %c %s\n", x, 65, "str");`(`%d` `%c` `%s`(文字列リテラル) `%%`)、
    `scanf("%d", &x);`(`%d` `%c` と空白。読めた個数を返し、EOFなら `EOF`(-1)。Ctrl+DでEOF)
  - サンプル: `hello.xh` `demo.xh` `primes.xh` `input.xh`
- `wc` `head` `tail`: ファイル/標準入力(パイプ可)を処理
- そのほかMikanOS由来: `tedit` `paint` `gview` `blocks` `cube` `grep` `sort` `more` `cp` など

## 今後の予定
- 永続ディスク(virtio-blk)とディレクトリ作成(`mkdir` / `cd`)
- テキストエディタの強化、ウィンドウ付きファイルマネージャ
