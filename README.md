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
scripts/mkdisk.sh [MiB]   # 永続ディスク build/disk.img を作成(既存なら中身を消さずにシステムファイルだけ更新)
scripts/run.sh            # QEMU (UEFI) で起動(build/disk.img があれば自動で接続)
```

## 永続ディスク
`build/disk.img` を virtio-blk として接続すると、そのFAT32ボリュームがルートとして使われ、
ファイルの変更は再起動後も残ります(接続しない場合はISO内のRAMボリュームで、変更は消えます)。
- コマンドの終了ごと、および `reboot` / `poweroff` 時に変更ブロックだけをディスクへ書き戻します
- `sync`: 手動で書き戻す / `disk`: 永続ディスクの状態を表示
- 起動後のファイル更新は、ホストから `mcopy -i build/disk.img` などでも確認できます
- 実機やQEMU以外で使う場合は legacy virtio-blk(I/Oポート)対応のデバイスが必要です

## デスクトップ
- 左下の「XinOS」ボタンでスタートメニュー(Terminal / Editor / Paint / Viewer / Blocks / Cube / Eye / Stars / Reboot / Shut down)
- デスクトップ左のアイコンをダブルクリックで起動

## 標準プログラム
- シェル組み込み: `help` `ver` `uname` `uptime` `echo` `clear` `ls` `cat` `date` `memstat` `cd` `pwd` `mkdir` `rmdir` `rm` `touch` `mv` `sync` `disk` ほか
- `xasm <file.xa>`: 簡易アセンブラ+VM(命令は `apps/xasm/xvm.hpp` 参照、サンプル `hello.xa`)
- `xhl [-S] <file.xh>`: 簡易高水準言語。xasmへコンパイルして実行(`-S` で生成コードを表示)
  - 入出力はC風: `printf("x=%d %c %s\n", x, 65, "str");`(`%d` `%c` `%s`(文字列リテラル) `%%`)、
    `scanf("%d", &x);`(`%d` `%c` と空白。読めた個数を返し、EOFなら `EOF`(-1)。Ctrl+DでEOF)
  - サンプルは `/samples` にあります: `hello.xh` `demo.xh` `primes.xh` `input.xh`(`hello.xa` は xasm 用)
- `wc` `head` `tail`: ファイル/標準入力(パイプ可)を処理
- そのほかMikanOS由来: `tedit` `paint` `gview` `blocks` `cube` `grep` `sort` `more` `cp` など

## ディレクトリ
- ルート直下: `/apps`(アプリ) `/home`(ユーザー用) `/samples`(サンプル) とリソースファイル
- プロンプトにカレントディレクトリを表示(`/home>` など)。相対パス・`.`・`..` が使えます
- アプリからの `fopen` も、実行したターミナルのカレントディレクトリ基準で解決されます
- ファイル名は 8.3 形式(例: `report.txt`、ディレクトリ名は8文字まで)。`mv` は既存ディレクトリを指定するとその中へ移動します

## 今後の予定
- テキストエディタの強化、ウィンドウ付きファイルマネージャ
