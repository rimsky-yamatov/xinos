// filer: XinOS ファイルマネージャ。 使い方: filer [ディレクトリ]  (既定 /home)
// 操作: クリックで選択 / ダブルクリックまたは Enter で開く / ↑↓ で移動 / Backspace で親へ
//       Delete でゴミ箱へ / N 新しいフォルダ / F2 または R 名前変更 / ゴミ箱内では Restore・Empty ボタン
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include "../syscall.h"

namespace {

constexpr int kWinW = 560, kWinH = 420;
constexpr int kMaxEntries = 200;
constexpr int kRowH = 22;
constexpr int kListX = 6, kListY = 74, kListW = kWinW - 12 - 8;
constexpr int kRows = 13;

constexpr uint32_t kBg = 0xffffff, kBar = 0xd8dce4, kText = 0x000000,
                   kSelBg = 0x3366cc, kSelText = 0xffffff, kGray = 0x707070,
                   kBtn = 0xf2f4f8, kBtnEdge = 0x8890a0, kDim = 0xb0b4bc;

struct Entry { char name[16]; bool is_dir; unsigned size; };
struct Button { int x, y, w, h; const char* label; int id; };
enum { kUp = 1, kHome, kNew, kRename, kTrash, kRestore, kEmpty, kDelete, kRoot };

uint64_t layer = 0;
char cur_path[128] = "/home";
Entry entries[kMaxEntries];
int count = 0, sel = -1, scroll = 0;
char message[96] = "";
// 入力モード(0=なし 1=新しいフォルダ 2=名前変更)
int prompt_mode = 0;
char prompt_buf[16] = "";
unsigned long last_click_tick = 0;
int last_click_row = -1;

uint32_t icon_folder[256], icon_file[256], icon_app[256], icon_text[256], icon_code[256],
         icon_image[256], icon_trash[256];
bool have_icons = false;

const uint64_t kNoRedraw = 0x00000001ull << 32;

bool LoadXic(const char* path, uint32_t* out) {
  auto [fd, err] = SyscallOpenFile(path, 0);
  if (err) return false;
  unsigned char buf[6 + 16 * 16 * 4];
  auto [n, err2] = SyscallReadFile(fd, buf, sizeof(buf));
  if (err2 || n < sizeof(buf) || memcmp(buf, "XIC1", 4) != 0 || buf[4] != 16 || buf[5] != 16) {
    return false;
  }
  memcpy(out, buf + 6, 16 * 16 * 4);
  return true;
}

void LoadIcons() {
  have_icons = LoadXic("/icons/S_FOLDER.XIC", icon_folder) &
               LoadXic("/icons/S_FILE.XIC", icon_file) &
               LoadXic("/icons/S_APP.XIC", icon_app) &
               LoadXic("/icons/S_TEXT.XIC", icon_text) &
               LoadXic("/icons/S_CODE.XIC", icon_code) &
               LoadXic("/icons/S_IMAGE.XIC", icon_image) &
               LoadXic("/icons/S_TRASH.XIC", icon_trash);
}

bool EndsWith(const char* s, const char* ext) {
  const size_t n = strlen(s), m = strlen(ext);
  if (n < m) return false;
  for (size_t i = 0; i < m; ++i) {
    char a = s[n - m + i], b = ext[i];
    if (a >= 'a' && a <= 'z') a -= 32;
    if (b >= 'a' && b <= 'z') b -= 32;
    if (a != b) return false;
  }
  return true;
}

bool InTrash() { return strcmp(cur_path, "/trash") == 0; }

const uint32_t* IconFor(const Entry& e) {
  if (e.is_dir) return strcmp(e.name, "TRASH") == 0 && strcmp(cur_path, "/") == 0 ? icon_trash : icon_folder;
  if (EndsWith(e.name, ".XH") || EndsWith(e.name, ".XA") || EndsWith(e.name, ".C") ||
      EndsWith(e.name, ".CPP") || EndsWith(e.name, ".H")) return icon_code;
  if (EndsWith(e.name, ".TXT") || EndsWith(e.name, ".MD") || EndsWith(e.name, ".LNK")) return icon_text;
  if (EndsWith(e.name, ".JPG") || EndsWith(e.name, ".BMP")) return icon_image;
  if (strcmp(cur_path, "/apps") == 0 && !strchr(e.name, '.')) return icon_app;
  return icon_file;
}

void Join(const char* name, char* out, size_t n) {
  snprintf(out, n, "%s%s%s", cur_path, strcmp(cur_path, "/") == 0 ? "" : "/", name);
}

void Refresh() {
  count = 0;
  DirInfo info;
  for (int i = 0; i < kMaxEntries; ++i) {
    auto [ok, err] = SyscallReadDir(cur_path, i, &info);
    if (err) break;
    strncpy(entries[count].name, info.name, 15);
    entries[count].name[15] = '\0';
    entries[count].is_dir = info.is_dir;
    entries[count].size = info.size;
    ++count;
  }
  // フォルダ → ファイルの順、名前順(挿入ソート)
  for (int i = 1; i < count; ++i) {
    Entry t = entries[i];
    int j = i - 1;
    while (j >= 0 && ((entries[j].is_dir < t.is_dir) ||
                      (entries[j].is_dir == t.is_dir && strcmp(entries[j].name, t.name) > 0))) {
      entries[j + 1] = entries[j];
      --j;
    }
    entries[j + 1] = t;
  }
  if (sel >= count) sel = count - 1;
  if (count > 0 && sel < 0) sel = 0;
  if (scroll > sel) scroll = sel < 0 ? 0 : sel;
  if (sel >= scroll + kRows) scroll = sel - kRows + 1;
}

void Text(int x, int y, uint32_t color, const char* s) { SyscallWinWriteString(layer | kNoRedraw, x, y, color, s); }
void Fill(int x, int y, int w, int h, uint32_t c) { SyscallWinFillRectangle(layer | kNoRedraw, x, y, w, h, c); }

void DrawButton(const Button& b, bool enabled) {
  Fill(b.x, b.y, b.w, b.h, kBtnEdge);
  Fill(b.x + 1, b.y + 1, b.w - 2, b.h - 2, kBtn);
  const int tw = static_cast<int>(strlen(b.label)) * 8;
  Text(b.x + (b.w - tw) / 2, b.y + 4, enabled ? kText : kDim, b.label);
}

Button buttons[12];
int num_buttons = 0;

void Layout() {
  num_buttons = 0;
  int x = 6;
  auto add = [&](const char* label, int id, int w) {
    buttons[num_buttons++] = {x, 28, w, 22, label, id};
    x += w + 4;
  };
  add("Up", kUp, 36);
  add("Home", kHome, 52);
  add("/", kRoot, 28);
  if (InTrash()) {
    add("Restore", kRestore, 68);
    add("Delete", kDelete, 60);
    add("Empty", kEmpty, 56);
  } else {
    add("New", kNew, 44);
    add("Rename", kRename, 68);
    add("Trash", kTrash, 56);
  }
}

void Draw() {
  Layout();
  Fill(4, 24, kWinW - 8, kWinH - 28, kBg);
  Fill(4, 24, kWinW - 8, 56 - 4, kBar);
  for (int i = 0; i < num_buttons; ++i) DrawButton(buttons[i], true);
  // パス表示
  Fill(6, 54, kWinW - 12 - 8, 18, kBtnEdge);
  Fill(7, 55, kWinW - 14 - 8, 16, kBg);
  Text(10, 55, kText, cur_path);

  for (int r = 0; r < kRows; ++r) {
    const int idx = scroll + r;
    const int y = kListY + r * kRowH;
    if (idx >= count) break;
    const bool s = idx == sel;
    if (s) Fill(kListX, y, kListW, kRowH, kSelBg);
    if (have_icons) SyscallWinBlit(layer | kNoRedraw, kListX + 4, y + 3, 16, 16, IconFor(entries[idx]));
    Text(kListX + 28, y + 3, s ? kSelText : kText, entries[idx].name);
    if (!entries[idx].is_dir) {
      char sz[24];
      if (entries[idx].size >= 1024) snprintf(sz, sizeof(sz), "%u KB", (entries[idx].size + 1023) / 1024);
      else snprintf(sz, sizeof(sz), "%u B", entries[idx].size);
      Text(kListX + kListW - 8 * static_cast<int>(strlen(sz)) - 8, y + 3, s ? kSelText : kGray, sz);
    } else {
      Text(kListX + kListW - 8 * 5 - 8, y + 3, s ? kSelText : kGray, "<DIR>");
    }
  }
  if (count == 0) Text(kListX + 8, kListY + 6, kGray, InTrash() ? "(trash is empty)" : "(empty)");

  // ステータス行 / 入力欄
  Fill(4, kWinH - 28, kWinW - 8, 24, kBar);
  char line[128];
  if (prompt_mode) {
    snprintf(line, sizeof(line), "%s: %s_", prompt_mode == 1 ? "New folder" : "Rename to", prompt_buf);
    Text(10, kWinH - 24, 0x0000a0, line);
  } else if (message[0]) {
    Text(10, kWinH - 24, 0xa00000, message);
  } else {
    snprintf(line, sizeof(line), "%d items", count);
    Text(10, kWinH - 24, kGray, line);
  }
  SyscallWinRedraw(layer);
}

void SetMessage(const char* m) { snprintf(message, sizeof(message), "%s", m); }

void Navigate(const char* path) {
  strncpy(cur_path, path, sizeof(cur_path) - 1);
  cur_path[sizeof(cur_path) - 1] = '\0';
  sel = -1; scroll = 0; message[0] = '\0';
  Refresh();
}

void GoUp() {
  char* slash = strrchr(cur_path, '/');
  if (!slash || slash == cur_path) { Navigate("/"); return; }
  *slash = '\0';
  Navigate(cur_path);
}

void Open(const Entry& e) {
  char p[160];
  if (e.is_dir) {
    Join(e.name, p, sizeof(p));
    Navigate(p);
    return;
  }
  char cmd[64];
  int flags = 0;
  if (EndsWith(e.name, ".XH")) { snprintf(cmd, sizeof(cmd), "xhl %s", e.name); flags = LAUNCH_WITH_TERMINAL; }
  else if (EndsWith(e.name, ".XA")) { snprintf(cmd, sizeof(cmd), "xasm %s", e.name); flags = LAUNCH_WITH_TERMINAL; }
  else if (EndsWith(e.name, ".JPG") || EndsWith(e.name, ".BMP")) snprintf(cmd, sizeof(cmd), "gview %s", e.name);
  else if (strcmp(cur_path, "/apps") == 0 && !strchr(e.name, '.')) snprintf(cmd, sizeof(cmd), "%s", e.name);
  else snprintf(cmd, sizeof(cmd), "tview %s", e.name);
  SyscallLaunch(cmd, flags, cur_path);
  snprintf(message, sizeof(message), "opened %s", e.name);
}

void Op(int op, const char* a, const char* b, const char* ok_msg) {
  auto [v, err] = SyscallFsOp(op, a, b);
  if (err) snprintf(message, sizeof(message), "failed: %s", strerror(err));
  else SetMessage(ok_msg);
  Refresh();
}

void DoButton(int id) {
  message[0] = '\0';
  char p[160];
  const bool has = sel >= 0 && sel < count;
  switch (id) {
    case kUp: GoUp(); break;
    case kHome: Navigate("/home"); break;
    case kRoot: Navigate("/"); break;
    case kNew: prompt_mode = 1; prompt_buf[0] = '\0'; break;
    case kRename:
      if (has) { prompt_mode = 2; snprintf(prompt_buf, sizeof(prompt_buf), "%s", entries[sel].name); }
      else SetMessage("select an item first");
      break;
    case kTrash:
      if (has) { Join(entries[sel].name, p, sizeof(p)); Op(FSOP_TRASH, p, nullptr, "moved to trash"); }
      else SetMessage("select an item first");
      break;
    case kRestore:
      if (has) Op(FSOP_RESTORE, entries[sel].name, nullptr, "restored");
      else SetMessage("select an item first");
      break;
    case kDelete:
      if (has) { Join(entries[sel].name, p, sizeof(p)); Op(FSOP_REMOVE_ALL, p, nullptr, "deleted permanently"); }
      else SetMessage("select an item first");
      break;
    case kEmpty: Op(FSOP_EMPTY_TRASH, nullptr, nullptr, "trash emptied"); break;
  }
}

void ConfirmPrompt() {
  const int mode = prompt_mode;
  prompt_mode = 0;
  if (prompt_buf[0] == '\0') return;
  char a[160], b[160];
  if (mode == 1) {
    Join(prompt_buf, a, sizeof(a));
    Op(FSOP_MKDIR, a, nullptr, "folder created");
  } else if (sel >= 0 && sel < count) {
    Join(entries[sel].name, a, sizeof(a));
    Join(prompt_buf, b, sizeof(b));
    Op(FSOP_MOVE, a, b, "renamed");
  }
}

void MoveSel(int d) {
  if (count == 0) return;
  sel += d;
  if (sel < 0) sel = 0;
  if (sel >= count) sel = count - 1;
  if (sel < scroll) scroll = sel;
  if (sel >= scroll + kRows) scroll = sel - kRows + 1;
}

void OnKey(unsigned char keycode, char ascii, int press) {
  if (!press) return;
  if (prompt_mode) {
    if (ascii == '\n' || ascii == '\r') ConfirmPrompt();
    else if (ascii == 0x1b) prompt_mode = 0;
    else if (ascii == 0x08 || ascii == 0x7f) {
      const size_t n = strlen(prompt_buf);
      if (n) prompt_buf[n - 1] = '\0';
    } else if (ascii >= 0x20 && ascii < 0x7f) {
      const size_t n = strlen(prompt_buf);
      if (n < 12) { prompt_buf[n] = ascii; prompt_buf[n + 1] = '\0'; }
    }
    return;
  }
  message[0] = '\0';
  switch (keycode) {
    case 0x52: MoveSel(-1); return;           // ↑
    case 0x51: MoveSel(1); return;            // ↓
    case 0x4b: MoveSel(-kRows); return;       // PageUp
    case 0x4e: MoveSel(kRows); return;        // PageDown
    case 0x4a: sel = 0; scroll = 0; return;   // Home
    case 0x4d: sel = count - 1; MoveSel(0); return;  // End
    case 0x4c: DoButton(InTrash() ? kDelete : kTrash); return;  // Delete
    case 0x3b: DoButton(kRename); return;     // F2? (HID 0x3b = F2)
    case 0x3e: Refresh(); return;             // F5
  }
  if (ascii == '\n' || ascii == '\r') { if (sel >= 0 && sel < count) Open(entries[sel]); }
  else if (ascii == 0x08) GoUp();
  else if ((ascii == 'n' || ascii == 'N') && !InTrash()) DoButton(kNew);
  else if ((ascii == 'r' || ascii == 'R') && !InTrash()) DoButton(kRename);
}

void OnClick(int x, int y) {
  message[0] = '\0';
  for (int i = 0; i < num_buttons; ++i) {
    const auto& b = buttons[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) { DoButton(b.id); return; }
  }
  if (x >= kListX && x < kListX + kListW && y >= kListY && y < kListY + kRows * kRowH) {
    const int idx = scroll + (y - kListY) / kRowH;
    if (idx < count) {
      auto [tick, freq] = SyscallGetCurrentTick();
      const bool dbl = idx == last_click_row && (tick - last_click_tick) * 1000 / freq < 600;
      sel = idx;
      last_click_row = idx;
      last_click_tick = tick;
      if (dbl) { Open(entries[idx]); last_click_row = -1; }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2) { strncpy(cur_path, argv[1], sizeof(cur_path) - 1); }
  auto [layer_id, err] = SyscallOpenWindow(kWinW, kWinH, 70, 50, "Files");
  if (err) return err;
  layer = layer_id;
  LoadIcons();
  Refresh();
  Draw();

  AppEvent ev[1];
  while (true) {
    auto [n, e] = SyscallReadEvent(ev, 1);
    if (e) break;
    if (ev[0].type == AppEvent::kQuit) break;
    if (ev[0].type == AppEvent::kMouseButton) {
      const auto& m = ev[0].arg.mouse_button;
      if (m.press && m.button == 0) OnClick(m.x, m.y);
    } else if (ev[0].type == AppEvent::kKeyPush) {
      OnKey(ev[0].arg.keypush.keycode, ev[0].arg.keypush.ascii, ev[0].arg.keypush.press);
    } else {
      continue;
    }
    Draw();
  }
  SyscallCloseWindow(layer);
  return 0;
}
