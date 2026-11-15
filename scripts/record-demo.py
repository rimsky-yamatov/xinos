#!/usr/bin/env python3
"""XinOS のデモ動画を作る: 電源オン(UEFI起動) → デスクトップ → スタートメニュー → アプリ起動。

QEMU(UEFI)を起動し、モニタの screendump で画面を連続キャプチャしながら、
マウス操作を自動で行って mp4 にまとめる。
必要なもの: qemu-system-x86, ovmf, ffmpeg, build/xinos.iso (scripts/mkiso.sh)
使い方: scripts/record-demo.py [出力mp4] [起動するメニュー項目の番号(0始まり、既定6=Cube)]
"""
import os, shutil, socket, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "xinos-demo.mp4")
ITEM = int(sys.argv[2]) if len(sys.argv) > 2 else 6
LABELS = ["Terminal", "Files", "Editor", "Paint", "Viewer", "Blocks", "Cube", "Eye", "Stars", "Trash"]
FONT = os.path.join(ROOT, "resource", "nihongo.ttf")
FRAMES = "/tmp/xinos-frames"
MON = "/tmp/xinos-demo-mon"
PROBE = "/tmp/xinos-demo-probe.ppm"
FRAME_INTERVAL = 0.1   # キャプチャ間隔(秒) = 10fps
W, H = 1280, 800

# 画面レイアウト(kernel/desktop.cpp と対応)
START_BTN = (20, H - 25)
MENU_ITEMS = 10                       # /menu のショートカット数(+ Reboot / Shut down)
MENU_TOP = H - 50 - ((MENU_ITEMS + 2) * 28 + 4 * 2 + 6)
def menu_item_pos(i):
    return (80, MENU_TOP + 4 + i * 28 + 14)


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
        self.s.settimeout(10)
        self._read()

    def _read(self):
        buf = b""
        while not buf.endswith(b"(qemu) "):
            buf += self.s.recv(65536)
        return buf

    def cmd(self, line):
        self.s.send((line + "\n").encode())
        return self._read()


def pixel(path, x, y):
    with open(path, "rb") as f:
        head = f.read(32)
        # "P6\nW H\n255\n"
        parts = head.split(b"\n", 3)
        off = len(parts[0]) + len(parts[1]) + len(parts[2]) + 3
        f.seek(off + (y * W + x) * 3)
        return tuple(f.read(3))


def near(a, b, tol=12):
    return all(abs(p - q) <= tol for p, q in zip(a, b))


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

    frames = []          # (経過秒, ファイル)
    events = []          # (経過秒, コマンド) ... デスクトップ表示後に組み立てる
    marks = {}           # 場面の切り替え時刻
    ready_at = None
    cursor = [200, 200]  # カーネル起動直後のマウス位置

    def glide(t_start, target, duration, steps=30):
        sx, sy = cursor
        tx, ty = target
        px, py = sx, sy
        for i in range(1, steps + 1):
            nx = round(sx + (tx - sx) * i / steps)
            ny = round(sy + (ty - sy) * i / steps)
            events.append((t_start + duration * i / steps, f"mouse_move {nx - px} {ny - py}"))
            px, py = nx, ny
        cursor[0], cursor[1] = tx, ty

    def click(t):
        events.append((t, "mouse_button 1"))
        events.append((t + 0.12, "mouse_button 0"))

    end_at = None
    n = 0
    while True:
        now = time.monotonic() - t0
        while events and events[0][0] <= now:
            mon.cmd(events.pop(0)[1])
        path = f"{FRAMES}/f{n:05d}.png"
        mon.cmd(f"screendump {path} -f png")
        now = time.monotonic() - t0
        frames.append((now, path))
        n += 1

        if ready_at is None:
          if n % 5 == 0 and now > 5:
            # アイコン(Terminal=暗灰, Editor=黄)が描画されたらデスクトップ表示完了
            mon.cmd(f"screendump {PROBE}")
            # タスクバーの Start ボタン(青い四角)が描かれたらデスクトップ表示完了
            if near(pixel(PROBE, 25, 775), (45, 118, 237)):
                ready_at = now
                marks["desktop"] = now
                t = now + 2.5
                glide(t, START_BTN, 1.4)
                t += 1.4 + 0.7
                marks["start_click"] = t
                click(t)
                t += 2.2
                glide(t, menu_item_pos(ITEM), 1.1)
                t += 1.1 + 0.7
                marks["app_click"] = t
                click(t)
                end_at = t + 9.0
        elif end_at and now > end_at:
            break
        spare = FRAME_INTERVAL - ((time.monotonic() - t0) - now)
        if spare > 0:
            time.sleep(spare)
        if now > 150:
            print("タイムアウト: デスクトップが表示されませんでした", file=sys.stderr)
            break

    qemu.terminate()
    try:
        qemu.wait(timeout=10)
    except subprocess.TimeoutExpired:
        qemu.kill()

    # ffmpeg 用の concat リスト(各フレームの表示時間 = 次のフレームまでの間隔)
    lst = os.path.join(FRAMES, "list.txt")
    with open(lst, "w") as f:
        for i, (t, p) in enumerate(frames):
            d = (frames[i + 1][0] - t) if i + 1 < len(frames) else 1.5
            f.write(f"file '{p}'\nduration {d:.3f}\n")
        f.write(f"file '{frames[-1][1]}'\n")

    def cap(text, a, b):
        return (f"drawtext=fontfile={FONT}:text='{text}':enable='between(t,{a:.2f},{b:.2f})':"
                f"x=(w-text_w)/2:y=24:fontsize=30:fontcolor=white:box=1:"
                f"boxcolor=black@0.65:boxborderw=14")

    d, sc, ac = marks.get("desktop", 20), marks.get("start_click", 30), marks.get("app_click", 40)
    total = frames[-1][0] + 1.5
    filters = ",".join([
        # UEFI初期画面は解像度が違うので、全フレームを W x H に揃える(黒帯つき)
        f"scale={W}:{H}:force_original_aspect_ratio=decrease:flags=neighbor",
        f"pad={W}:{H}:(ow-iw)/2:(oh-ih)/2:black",
        cap("電源オン → UEFI → XinOS が起動", 0, d),
        cap("デスクトップが表示された", d, sc - 0.4),
        cap("スタートボタンをクリック", sc - 0.4, sc + 2.0),
        cap(f"メニューから「{LABELS[ITEM]}」を選んで起動", sc + 2.0, total),
        "fps=20", "format=yuv420p",
    ])
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0",
                    "-i", lst, "-vf", filters, "-c:v", "libx264", "-crf", "23",
                    "-preset", "medium", "-movflags", "+faststart", OUT], check=True)
    shutil.rmtree(FRAMES, ignore_errors=True)
    print(f"frames={len(frames)} desktop={d:.1f}s start_click={sc:.1f}s app_click={ac:.1f}s total={total:.1f}s")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
