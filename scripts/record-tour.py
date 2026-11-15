#!/usr/bin/env python3
"""XinOS の約5分のツアー動画を録画する(QEMU を自動操作して mp4 にまとめる)。

流れ: 起動画面 → デスクトップ → スタートメニュー → ファイルマネージャ(作成/名前変更/ゴミ箱/復元)
      → ターミナル(xhl・ディレクトリ・link でアイコン追加) → 再起動しても残る(永続ディスク)
必要なもの: qemu-system-x86, ovmf, ffmpeg, build/xinos.iso, build/disk.img(scripts/mkdisk.sh で新規作成)
使い方: scripts/record-tour.py [出力mp4]
"""
import os, shutil, socket, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "xinos-tour.mp4")
FONT = os.path.join(ROOT, "resource", "nihongo.ttf")
FRAMES = "/tmp/xinos-tour-frames"
MON = "/tmp/xinos-tour-mon"
PROBE = "/tmp/xinos-tour-probe.ppm"
W, H = 1280, 800
INTERVAL = 0.1


class Monitor:
    def __init__(self, path):
        for _ in range(100):
            try:
                self.s = socket.socket(socket.AF_UNIX)
                self.s.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError("QEMU monitor に接続できません")
        self.s.settimeout(15)
        self._read()

    def _read(self):
        buf = b""
        while not buf.endswith(b"(qemu) "):
            buf += self.s.recv(65536)

    def cmd(self, line):
        self.s.send((line + "\n").encode())
        self._read()


def pixel(path, x, y):
    with open(path, "rb") as f:
        head = f.read(32)
        parts = head.split(b"\n", 3)
        off = len(parts[0]) + len(parts[1]) + len(parts[2]) + 3
        f.seek(off + (y * W + x) * 3)
        return tuple(f.read(3))


def near(a, b, tol=14):
    return all(abs(p - q) <= tol for p, q in zip(a, b))


KEYS = {"!": "shift-1", " ": "spc", ".": "dot", "/": "slash", "-": "minus", "_": "shift-minus",
        ">": "shift-dot", ",": "comma", "\n": "ret", ":": "shift-semicolon"}


def keyname(c):
    if c in KEYS:
        return KEYS[c]
    if c.isupper():
        return "shift-" + c.lower()
    return c


class Ctl:
    """台本(ジェネレータ)から使う操作の集まり。yield した数値は待ち時間(秒)。"""

    def __init__(self, mon):
        self.mon = mon
        self.cursor = [200, 200]
        self.t = 0.0
        self.caps = []          # (開始時刻, 文字列 or None)
        self.desktop_ready = False

    def cap(self, text):
        self.caps.append((self.t, text))

    def glide(self, x, y, dur=1.2):
        steps = max(1, int(dur / 0.1))
        sx, sy = self.cursor
        px, py = sx, sy
        for i in range(1, steps + 1):
            nx = round(sx + (x - sx) * i / steps)
            ny = round(sy + (y - sy) * i / steps)
            self.mon.cmd(f"mouse_move {nx - px} {ny - py}")
            px, py = nx, ny
            yield 0.1
        self.cursor = [x, y]

    def click(self):
        self.mon.cmd("mouse_button 1")
        yield 0.12
        self.mon.cmd("mouse_button 0")
        yield 0.35

    def dbl(self):
        yield from self.click()
        yield from self.click()

    def at(self, x, y, dur=1.0):
        yield from self.glide(x, y, dur)

    def click_at(self, x, y, dur=0.9):
        yield from self.glide(x, y, dur)
        yield from self.click()

    def dbl_at(self, x, y, dur=0.9):
        yield from self.glide(x, y, dur)
        yield from self.dbl()

    def type(self, text, delay=0.11):
        for c in text:
            self.mon.cmd("sendkey " + keyname(c))
            yield delay

    def key(self, name, wait=0.3):
        self.mon.cmd("sendkey " + name)
        yield wait

    def cmdline(self, text, wait=1.6):
        yield from self.type(text + "\n")
        yield wait


def script(c):
    def open_from_menu(i):
        """スタートボタン → メニューの i 番目をクリックして起動する。"""
        yield from c.at(20, 775, 1.4)
        yield from c.click()
        yield 1.2
        yield from c.at(110, 418 + 28 * i, 1.0)
        yield 0.4
        yield from c.click()

    # ---- 1. 起動 ----
    c.cap("電源オン → UEFI → 起動画面 → デスクトップ")
    yield from c.wait_desktop()
    yield 2.5

    # ---- 2. デスクトップ ----
    c.cap("デスクトップ・設定できるアイコン")
    for y in (64, 152, 240, 328, 416, 504, 592):
        yield from c.at(48, y, 0.9)
        yield 0.8
    c.cap("スタートメニューからアプリを起動")
    yield from c.at(20, 775, 1.6)
    yield from c.click()
    yield 2.0
    for i in range(10):
        yield from c.at(110, 418 + 28 * i, 0.5)
        yield 0.35
    yield from c.at(110, 418 + 28 * 6, 0.8)   # Cube
    yield 0.6
    yield from c.click()
    c.cap("Cube を起動(3D描画アプリ)")
    yield 9.0
    yield from c.click_at(164, 21, 1.2)      # Cube のウィンドウを閉じる(アイコンに重なるため)
    yield 1.0

    # ---- 他のアプリ ----
    c.cap("Stars(星空のアニメーション)")
    yield from open_from_menu(8)
    yield 7.0
    yield from c.click_at(104, 21, 1.2)
    yield 1.0

    c.cap("Paint でお絵かき(マウスで描く)")
    yield from open_from_menu(3)
    yield 2.5
    yield from c.at(60, 70, 1.0)
    c.mon.cmd("mouse_button 1")
    for (x, y) in ((110, 112), (70, 132), (150, 62), (192, 122), (100, 60)):
        yield from c.glide(x, y, 0.9)
    c.mon.cmd("mouse_button 0")
    yield 3.5
    yield from c.click_at(204, 21, 1.2)
    yield 1.0

    c.cap("Editor でテキストを入力")
    yield from open_from_menu(2)
    yield 2.5
    yield from c.type("Hello from XinOS!\n", 0.14)
    yield from c.type("This editor runs on my own OS.", 0.14)
    yield 4.0
    yield from c.click_at(648, 22, 1.4)
    yield 1.5
    yield from c.click_at(334, 208, 1.0)     # 「変更を破棄しますか?」の Discard
    yield 1.5

    # ---- 3. ファイルマネージャ ----
    c.cap("ファイルマネージャ(Files)")
    yield from c.dbl_at(48, 152, 1.6)
    yield 3.0
    yield from c.click_at(186, 89)           # "/"
    yield 2.0
    yield from c.dbl_at(170, 245)            # SAMPLES(APPS DESKTOP HOME ICONS MENU SAMPLES の6行目)
    yield 2.5
    c.cap("ファイルをダブルクリックで実行(xhl)")
    yield from c.dbl_at(170, 223)            # PRIMES.XH
    yield 6.0
    yield from c.click_at(300, 60)           # Files を前面へ
    yield 1.0
    c.cap("新しいフォルダを作る")
    yield from c.click_at(142, 89)           # Home
    yield 1.0
    yield from c.click_at(226, 89)           # New
    yield 0.8
    yield from c.type("docs\n")
    yield 2.0
    yield from c.click_at(170, 135)          # 行0を選択
    yield 1.0
    c.cap("名前を変更する")
    yield from c.click_at(286, 89)           # Rename
    yield 0.8
    for _ in range(4):
        yield from c.key("backspace", 0.15)
    yield from c.type("notes\n")
    yield 2.0
    c.cap("ゴミ箱へ移動 → ゴミ箱アイコンが変わる")
    yield from c.click_at(352, 89)           # Trash
    yield 4.0
    yield from c.dbl_at(48, 598, 1.8)        # デスクトップのゴミ箱
    yield 3.0
    yield from c.click_at(170, 135)
    yield 1.0
    c.cap("元の場所へ復元")
    yield from c.click_at(238, 89)           # Restore
    yield 3.0
    yield from c.click_at(617, 62)           # ゴミ箱の Files を閉じる
    yield 1.0
    yield from c.click_at(617, 62)           # 最初の Files を閉じる
    yield 1.5

    # ---- 4. ターミナル ----
    c.cap("ターミナル・コマンドとプログラミング言語 xhl")
    yield from c.click_at(350, 212, 1.5)     # 前面のターミナルを選ぶ
    yield 0.8
    yield from c.cmdline("pwd")
    yield from c.cmdline("ver")
    yield from c.cmdline("xhl demo.xh", 4.0)
    yield from c.cmdline("xhl input.xh", 1.0)
    yield from c.type("10 20 30\n")
    yield 0.8
    yield from c.key("ctrl-d", 2.5)
    c.cap("ディレクトリとファイル操作")
    yield from c.cmdline("cd /home")
    yield from c.cmdline("echo hello XinOS > memo.txt")
    yield from c.cmdline("cat memo.txt")
    yield from c.cmdline("mkdir work")
    yield from c.cmdline("cd work")
    yield from c.cmdline("pwd")
    yield from c.cmdline("cd ..")
    yield from c.cmdline("ls", 2.0)
    c.cap("link コマンドでアプリにアイコンを設定")
    yield from c.cmdline("link MyEdit /icons/EDITOR.XIC tedit", 3.0)
    yield from c.at(150, 66, 1.5)             # 追加された MyEdit アイコン(2列目の先頭)
    yield 3.0
    c.cap("永続ディスクにつながっている")
    yield from c.click_at(350, 212, 1.5)
    yield from c.cmdline("disk", 3.0)

    # ---- 5. 再起動して確認 ----
    c.cap("再起動しても、作ったファイルとアイコンは残る")
    yield from c.cmdline("reboot", 1.0)
    yield from c.wait_reboot()
    c.cursor = [200, 200]
    yield 2.0
    yield from c.at(150, 66, 1.5)
    yield 2.5
    yield from c.click_at(350, 212, 1.5)
    yield from c.cmdline("cat /home/memo.txt", 2.0)
    yield from c.cmdline("ls /home", 2.5)
    yield from c.cmdline("ls /desktop", 3.0)
    c.cap("XinOS・MikanOS ベースの x86-64 UEFI OS")
    yield 10.0


def main():
    shutil.rmtree(FRAMES, ignore_errors=True)
    os.makedirs(FRAMES)
    if os.path.exists(MON):
        os.remove(MON)
    env = dict(os.environ)
    env["QEMU_OPTS"] = f"-display none -monitor unix:{MON},server,nowait -vga std"
    env.setdefault("XINOS_DISK_BUS", "virtio")
    qemu = subprocess.Popen([os.path.join(ROOT, "scripts", "run.sh")], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.monotonic()
    mon = Monitor(MON)
    c = Ctl(mon)
    frames = []
    state = {"n": 0}

    def probe_ready():
        mon.cmd(f"screendump {PROBE}")
        return near(pixel(PROBE, 48, 64), (40, 40, 40)) and near(pixel(PROBE, 48, 152), (230, 200, 60), 30)

    def capture():
        path = f"{FRAMES}/f{state['n']:05d}.png"
        mon.cmd(f"screendump {path} -f png")
        c.t = time.monotonic() - t0
        frames.append((c.t, path))
        state["n"] += 1

    def wait_desktop():
        while True:
            capture()
            if c.t > 4 and state["n"] % 4 == 0 and probe_ready():
                return
            time.sleep(INTERVAL)
            yield 0

    def wait_reboot():
        # いったん画面が変わる(デスクトップが消える)のを待ってから、再びデスクトップが出るのを待つ
        gone = False
        for _ in range(400):
            capture()
            if state["n"] % 3 == 0:
                r = probe_ready()
                if not r:
                    gone = True
                if gone and r:
                    return
            time.sleep(INTERVAL)
            yield 0

    c.wait_desktop = wait_desktop
    c.wait_reboot = wait_reboot
    gen = script(c)
    wake = 0.0
    done = False
    while not done:
        capture()
        if c.t >= wake:
            try:
                while True:
                    v = next(gen)
                    if v > 0:
                        wake = c.t + v
                        break
            except StopIteration:
                done = True
        spare = INTERVAL - ((time.monotonic() - t0) - c.t)
        if spare > 0:
            time.sleep(spare)
        if c.t > 700:
            print("timeout", file=sys.stderr)
            break
    for _ in range(10):
        capture()
        time.sleep(INTERVAL)

    qemu.terminate()
    try:
        qemu.wait(timeout=10)
    except subprocess.TimeoutExpired:
        qemu.kill()

    lst = os.path.join(FRAMES, "list.txt")
    with open(lst, "w") as f:
        for i, (t, p) in enumerate(frames):
            d = (frames[i + 1][0] - t) if i + 1 < len(frames) else 1.5
            f.write(f"file '{p}'\nduration {max(d, 0.02):.3f}\n")
        f.write(f"file '{frames[-1][1]}'\n")
    total = frames[-1][0] + 1.5

    parts = [f"scale={W}:{H}:force_original_aspect_ratio=decrease:flags=neighbor",
             f"pad={W}:{H}:(ow-iw)/2:(oh-ih)/2:black"]
    for i, (st, text) in enumerate(c.caps):
        en = c.caps[i + 1][0] if i + 1 < len(c.caps) else total
        if text:
            parts.append(f"drawtext=fontfile={FONT}:text='{text}':enable='between(t,{st:.2f},{en:.2f})':"
                         f"x=(w-text_w)/2:y=24:fontsize=30:fontcolor=white:box=1:"
                         f"boxcolor=black@0.65:boxborderw=14")
    parts += ["fps=20", "format=yuv420p"]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst,
                    "-vf", ",".join(parts), "-c:v", "libx264", "-crf", "24", "-preset", "medium",
                    "-movflags", "+faststart", OUT], check=True)
    shutil.rmtree(FRAMES, ignore_errors=True)
    print(f"frames={len(frames)} total={total:.1f}s wrote {OUT}")


if __name__ == "__main__":
    main()
