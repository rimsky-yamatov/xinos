#!/usr/bin/env python3
"""XinOS のアイコン(.XIC)とショートカット(.LNK)を生成する。

使い方: tools/mkicons.py <出力ディレクトリ> [--sheet sheet.png]
  <出力>/icons/*.XIC       アイコン
  <出力>/desktop/*.LNK     デスクトップのショートカット
  <出力>/startmenu/*.LNK   スタートメニューの項目

XIC 形式: "XIC1" + u8 幅 + u8 高さ + (幅*高さ) 個の 32bit ARGB(リトルエンディアン, A=0 は透過)
LNK 形式: "key=value" の行。name / exec / icon / terminal(0|1) / cwd / special(trash)
"""
import math, os, struct, sys, zlib

SS = 4  # スーパーサンプリング倍率


class Canvas:
    """単位正方形 [0,1]^2 上に図形を描いて、n x n の ARGB に縮小する。"""

    def __init__(self, n):
        self.n = n
        self.m = n * SS
        self.px = [(0, 0, 0, 0)] * (self.m * self.m)

    def _paint(self, inside, color):
        r, g, b, a = color
        m = self.m
        for y in range(m):
            fy = (y + 0.5) / m
            row = y * m
            for x in range(m):
                if inside((x + 0.5) / m, fy):
                    if a >= 255:
                        self.px[row + x] = (r, g, b, 255)
                    else:
                        pr, pg, pb, pa = self.px[row + x]
                        na = a + pa * (255 - a) / 255
                        if na > 0:
                            self.px[row + x] = (
                                (r * a + pr * pa * (255 - a) / 255) / na,
                                (g * a + pg * pa * (255 - a) / 255) / na,
                                (b * a + pb * pa * (255 - a) / 255) / na, na)

    def rrect(self, x0, y0, x1, y1, rad, color):
        def inside(x, y):
            if x < x0 or x > x1 or y < y0 or y > y1:
                return False
            cx = min(max(x, x0 + rad), x1 - rad)
            cy = min(max(y, y0 + rad), y1 - rad)
            return (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad
        self._paint(inside, color)

    def ellipse(self, cx, cy, rx, ry, color):
        self._paint(lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1, color)

    def circle(self, cx, cy, r, color):
        self.ellipse(cx, cy, r, r, color)

    def poly(self, pts, color):
        def inside(x, y):
            c = False
            j = len(pts) - 1
            for i in range(len(pts)):
                xi, yi = pts[i]
                xj, yj = pts[j]
                if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
                    c = not c
                j = i
            return c
        self._paint(inside, color)

    def line(self, p0, p1, w, color):
        (x0, y0), (x1, y1) = p0, p1
        dx, dy = x1 - x0, y1 - y0
        l2 = dx * dx + dy * dy

        def inside(x, y):
            t = 0 if l2 == 0 else max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / l2))
            return (x - (x0 + t * dx)) ** 2 + (y - (y0 + t * dy)) ** 2 <= (w / 2) ** 2
        self._paint(inside, color)

    def star(self, cx, cy, r, color):
        pts = []
        for i in range(10):
            ang = -math.pi / 2 + i * math.pi / 5
            rad = r if i % 2 == 0 else r * 0.42
            pts.append((cx + rad * math.cos(ang), cy + rad * math.sin(ang)))
        self.poly(pts, color)

    def argb(self):
        n, m = self.n, self.m
        out = []
        for y in range(n):
            for x in range(n):
                ra = ga = ba = aa = 0.0
                for sy in range(SS):
                    for sx in range(SS):
                        r, g, b, a = self.px[(y * SS + sy) * m + x * SS + sx]
                        ra += r * a; ga += g * a; ba += b * a; aa += a
                if aa == 0:
                    out.append(0)
                else:
                    cnt = SS * SS
                    a8 = int(round(aa / cnt))
                    out.append((a8 << 24) | (int(ra / aa) << 16) | (int(ga / aa) << 8) | int(ba / aa))
        return out


def C(r, g, b, a=255):
    return (r, g, b, a)


WHITE, BLACK = C(255, 255, 255), C(20, 20, 24)


def folder(c):
    c.poly([(0.08, 0.22), (0.38, 0.22), (0.46, 0.32), (0.92, 0.32), (0.92, 0.84), (0.08, 0.84)], C(224, 160, 40))
    c.rrect(0.08, 0.36, 0.92, 0.84, 0.05, C(250, 200, 80))
    c.rrect(0.08, 0.36, 0.92, 0.44, 0.03, C(255, 224, 140))


def file_icon(c, accent=None, lines=True):
    c.poly([(0.2, 0.08), (0.62, 0.08), (0.82, 0.28), (0.82, 0.92), (0.2, 0.92)], C(245, 247, 250))
    c.poly([(0.62, 0.08), (0.62, 0.28), (0.82, 0.28)], C(190, 198, 212))
    if lines:
        col = accent or C(150, 160, 178)
        for i, y in enumerate((0.45, 0.58, 0.71)):
            c.rrect(0.3, y, 0.72 if i < 2 else 0.55, y + 0.05, 0.02, col)


def terminal(c):
    c.rrect(0.07, 0.12, 0.93, 0.88, 0.09, C(34, 37, 48))
    c.rrect(0.07, 0.12, 0.93, 0.28, 0.09, C(70, 75, 95))
    c.rrect(0.07, 0.2, 0.93, 0.28, 0.0, C(70, 75, 95))
    for i, col in enumerate((C(240, 90, 90), C(250, 200, 70), C(90, 200, 110))):
        c.circle(0.17 + i * 0.08, 0.2, 0.027, col)
    c.line((0.2, 0.42), (0.35, 0.53), 0.06, C(90, 230, 130))
    c.line((0.35, 0.53), (0.2, 0.64), 0.06, C(90, 230, 130))
    c.rrect(0.42, 0.65, 0.62, 0.7, 0.01, C(220, 225, 235))


def editor(c):
    c.rrect(0.2, 0.08, 0.8, 0.92, 0.05, C(250, 250, 252))
    for i, y in enumerate((0.2, 0.33, 0.46, 0.59, 0.72)):
        c.rrect(0.29, y, 0.68 if i != 4 else 0.5, y + 0.05, 0.02, C(150, 160, 178))
    c.poly([(0.52, 0.86), (0.86, 0.52), (0.94, 0.6), (0.6, 0.94)], C(250, 196, 40))
    c.poly([(0.52, 0.86), (0.6, 0.94), (0.44, 0.96)], C(240, 220, 190))
    c.poly([(0.86, 0.52), (0.92, 0.46), (0.99, 0.53), (0.94, 0.6)], C(235, 110, 130))


def paint(c):
    c.ellipse(0.5, 0.54, 0.43, 0.37, C(236, 214, 170))
    c.circle(0.63, 0.72, 0.065, C(0, 0, 0, 0)) if False else None
    c.circle(0.3, 0.5, 0.075, C(230, 70, 70))
    c.circle(0.42, 0.33, 0.075, C(250, 200, 50))
    c.circle(0.62, 0.3, 0.075, C(70, 190, 100))
    c.circle(0.76, 0.46, 0.075, C(70, 130, 230))
    c.circle(0.6, 0.74, 0.07, C(250, 250, 250))
    c.line((0.58, 0.62), (0.9, 0.12), 0.06, C(150, 100, 60))
    c.poly([(0.9, 0.12), (0.95, 0.05), (0.97, 0.15)], C(60, 60, 70))


def viewer(c):
    c.rrect(0.07, 0.16, 0.93, 0.84, 0.06, C(250, 250, 252))
    c.rrect(0.13, 0.22, 0.87, 0.78, 0.02, C(120, 190, 255))
    c.circle(0.68, 0.36, 0.08, C(255, 215, 70))
    c.poly([(0.13, 0.78), (0.38, 0.45), (0.55, 0.66), (0.68, 0.52), (0.87, 0.78)], C(60, 160, 90))


def blocks(c):
    cells = [((1, 0), C(80, 150, 240)), ((2, 0), C(80, 150, 240)), ((3, 0), C(80, 150, 240)), ((2, 1), C(80, 150, 240)),
             ((0, 2), C(240, 150, 60)), ((0, 3), C(240, 150, 60)), ((1, 3), C(240, 150, 60)), ((2, 3), C(240, 150, 60)),
             ((3, 2), C(230, 80, 90)), ((3, 3), C(230, 80, 90))]
    for (cx, cy), col in cells:
        x, y = 0.1 + cx * 0.2, 0.1 + cy * 0.2
        c.rrect(x, y, x + 0.18, y + 0.18, 0.03, col)


def cube(c):
    c.poly([(0.5, 0.08), (0.9, 0.29), (0.5, 0.5), (0.1, 0.29)], C(190, 150, 240))
    c.poly([(0.1, 0.29), (0.5, 0.5), (0.5, 0.93), (0.1, 0.72)], C(130, 90, 200))
    c.poly([(0.5, 0.5), (0.9, 0.29), (0.9, 0.72), (0.5, 0.93)], C(90, 55, 150))


def eye(c):
    c.ellipse(0.5, 0.5, 0.45, 0.28, C(250, 250, 252))
    c.circle(0.5, 0.5, 0.21, C(70, 130, 220))
    c.circle(0.5, 0.5, 0.1, C(15, 20, 40))
    c.circle(0.56, 0.44, 0.04, WHITE)


def stars(c):
    c.rrect(0.06, 0.06, 0.94, 0.94, 0.1, C(22, 26, 70))
    for (x, y, r) in ((0.3, 0.32, 0.17), (0.68, 0.55, 0.2), (0.28, 0.72, 0.1), (0.76, 0.22, 0.08)):
        c.star(x, y, r, C(255, 220, 80))


def filer(c):
    folder(c)
    c.circle(0.68, 0.64, 0.12, C(255, 255, 255, 235))
    c.circle(0.68, 0.64, 0.075, C(90, 140, 230))
    c.line((0.77, 0.73), (0.88, 0.84), 0.05, C(70, 70, 80))


def trash(c, full=False):
    if full:
        c.poly([(0.28, 0.3), (0.38, 0.1), (0.55, 0.3)], C(250, 250, 250))
        c.poly([(0.48, 0.3), (0.62, 0.06), (0.74, 0.3)], C(255, 236, 150))
        c.poly([(0.36, 0.3), (0.3, 0.14), (0.46, 0.3)], C(210, 225, 250))
    c.poly([(0.2, 0.3), (0.8, 0.3), (0.72, 0.92), (0.28, 0.92)], C(150, 162, 180))
    for x in (0.38, 0.5, 0.62):
        c.rrect(x - 0.018, 0.4, x + 0.018, 0.82, 0.015, C(105, 116, 135))
    c.rrect(0.14, 0.2, 0.86, 0.3, 0.03, C(112, 124, 144))
    c.rrect(0.4, 0.11, 0.6, 0.21, 0.03, C(112, 124, 144))


def app(c):
    c.rrect(0.08, 0.08, 0.92, 0.92, 0.14, C(70, 130, 220))
    c.rrect(0.22, 0.28, 0.78, 0.72, 0.04, WHITE)
    c.rrect(0.22, 0.28, 0.78, 0.38, 0.04, C(160, 190, 240))
    c.rrect(0.22, 0.34, 0.78, 0.38, 0.0, C(160, 190, 240))


ICONS48 = {
    "TERM": terminal, "EDITOR": editor, "PAINT": paint, "VIEWER": viewer, "BLOCKS": blocks,
    "CUBE": cube, "EYE": eye, "STARS": stars, "FILER": filer, "TRASH": trash,
    "TRASHF": lambda c: trash(c, True), "FOLDER": folder, "FILE": file_icon, "APP": app,
}
ICONS16 = {
    "S_FOLDER": folder, "S_FILE": file_icon, "S_APP": app,
    "S_TEXT": lambda c: file_icon(c, C(80, 140, 230)),
    "S_CODE": lambda c: file_icon(c, C(60, 170, 100)),
    "S_IMAGE": viewer, "S_TRASH": trash,
}

# (ファイル名, 表示名, コマンド, 端末表示, アイコン, cwd, special)
SHORTCUTS = [
    ("TERM", "Terminal", "", 1, "/icons/TERM.XIC", "/home", ""),
    ("FILER", "Files", "filer /home", 0, "/icons/FILER.XIC", "/", ""),
    ("EDITOR", "Editor", "tedit", 0, "/icons/EDITOR.XIC", "/home", ""),
    ("PAINT", "Paint", "paint", 0, "/icons/PAINT.XIC", "/home", ""),
    ("VIEWER", "Viewer", "gview /fujisan.jpg", 0, "/icons/VIEWER.XIC", "/", ""),
    ("BLOCKS", "Blocks", "blocks", 0, "/icons/BLOCKS.XIC", "/", ""),
    ("CUBE", "Cube", "cube", 0, "/icons/CUBE.XIC", "/", ""),
    ("EYE", "Eye", "eye", 0, "/icons/EYE.XIC", "/", ""),
    ("STARS", "Stars", "stars", 0, "/icons/STARS.XIC", "/", ""),
    ("TRASH", "Trash", "filer /trash", 0, "/icons/TRASH.XIC", "/", "trash"),
]
DESKTOP = ["TERM", "FILER", "EDITOR", "PAINT", "VIEWER", "BLOCKS", "TRASH"]
STARTMENU = ["TERM", "FILER", "EDITOR", "PAINT", "VIEWER", "BLOCKS", "CUBE", "EYE", "STARS", "TRASH"]


def write_xic(path, n, argb):
    with open(path, "wb") as f:
        f.write(b"XIC1" + bytes([n, n]))
        f.write(struct.pack("<%dI" % len(argb), *argb))


def write_lnk(path, label, cmd, term, icon, cwd, special):
    lines = [f"name={label}", f"exec={cmd}", f"icon={icon}", f"terminal={term}", f"cwd={cwd}"]
    if special:
        lines.append(f"special={special}")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def make_sheet(path, items, cell=64):
    cols = 8
    rows = (len(items) + cols - 1) // cols
    w, h = cols * cell, rows * cell
    img = [(240, 240, 244)] * (w * h)
    for idx, (n, argb) in enumerate(items):
        ox, oy = (idx % cols) * cell + (cell - n) // 2, (idx // cols) * cell + (cell - n) // 2
        for y in range(n):
            for x in range(n):
                p = argb[y * n + x]
                a = p >> 24
                if a == 0:
                    continue
                bg = img[(oy + y) * w + ox + x]
                img[(oy + y) * w + ox + x] = tuple(
                    int(((p >> s) & 255) * a / 255 + bg[i] * (255 - a) / 255) for i, s in enumerate((16, 8, 0)))
    raw = b"".join(b"\0" + bytes(sum(img[y * w:(y + 1) * w], ())) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    out = sys.argv[1]
    for d in ("icons", "desktop", "startmenu"):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    sheet_items = []
    for sizes, table in ((48, ICONS48), (16, ICONS16)):
        for name, fn in table.items():
            c = Canvas(sizes)
            fn(c)
            argb = c.argb()
            write_xic(os.path.join(out, "icons", name + ".XIC"), sizes, argb)
            sheet_items.append((sizes, argb))
    table = {s[0]: s for s in SHORTCUTS}
    for i, key in enumerate(DESKTOP):
        s = table[key]
        write_lnk(os.path.join(out, "desktop", f"{i + 1:02d}{key}.LNK"), *s[1:])
    for i, key in enumerate(STARTMENU):
        s = table[key]
        write_lnk(os.path.join(out, "startmenu", f"{i + 1:02d}{key}.LNK"), *s[1:])
    if "--sheet" in sys.argv:
        make_sheet(sys.argv[sys.argv.index("--sheet") + 1], sheet_items)
    print(f"wrote {len(ICONS48) + len(ICONS16)} icons, {len(DESKTOP)} desktop and {len(STARTMENU)} start-menu shortcuts to {out}")


if __name__ == "__main__":
    sys.exit(main())
