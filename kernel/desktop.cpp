#include "desktop.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <strings.h>
#include <vector>

#include "console.hpp"
#include "fat.hpp"
#include "font.hpp"
#include "fsops.hpp"
#include "graphics.hpp"
#include "icon.hpp"
#include "layer.hpp"
#include "terminal.hpp"
#include "timer.hpp"
#include "window.hpp"

extern TimerManager* timer_manager;
extern Console* console;

namespace {

struct Shortcut {
  char label[24]{};
  char command[96]{};
  char cwd[64]{};
  char icon_path[64]{};
  bool terminal{false};
  bool trash{false};
  Icon icon, icon_full;
};

constexpr int kIconSize = 48;
constexpr int kIconX = 24;
constexpr int kIconTop = 40;
constexpr int kIconPitch = 88;
constexpr int kColumnPitch = 100;

constexpr int kMenuWidth = 220;
constexpr int kItemHeight = 28;
constexpr int kMenuPad = 4;
constexpr int kMenuIconSize = 20;

const char* const kSystemLabels[] = {"Reboot", "Shut down"};
const char* const kSystemCommands[] = {"reboot", "poweroff"};
constexpr int kNumSystem = 2;

const PixelColor kMenuBG{236, 236, 236};
const PixelColor kMenuText{0, 0, 0};
const PixelColor kHighlight{70, 130, 220};

std::vector<Shortcut> icons;   // デスクトップ
std::vector<Shortcut> menu;    // スタートメニュー
bool trash_full = false;

unsigned int bg_layer_id = 0, mouse_layer_id = 0, menu_layer_id = 0;
std::shared_ptr<Window> menu_window;
bool menu_visible = false;
bool initialized = false;
int selected_icon = -1;
int last_click_icon = -1;
unsigned long last_click_tick = 0;

Vector2D<int> ScreenWH() { return ScreenSize(); }
Vector2D<int> StartButtonPos() { return {0, ScreenWH().y - 50}; }
Vector2D<int> StartButtonSize() { return {ScreenWH().x / 5, 50}; }

int MenuItemCount() { return static_cast<int>(menu.size()) + kNumSystem; }
int MenuHeight() { return MenuItemCount() * kItemHeight + kMenuPad * 2 + 6; }
Vector2D<int> MenuPos() { return {0, ScreenWH().y - 50 - MenuHeight()}; }

bool Inside(Vector2D<int> p, Vector2D<int> pos, Vector2D<int> size) {
  return p.x >= pos.x && p.x < pos.x + size.x && p.y >= pos.y && p.y < pos.y + size.y;
}

// ---- .LNK の読み込み ----
bool ReadSmallFile(const char* path, char* buf, size_t cap) {
  auto [ e, post_slash ] = fat::FindFile(path);
  if (e == nullptr || e->attr == fat::Attribute::kDirectory) return false;
  size_t len = e->file_size < cap - 1 ? e->file_size : cap - 1;
  len = fat::LoadFile(buf, len, *e);
  buf[len] = '\0';
  return true;
}

void CopyStr(char* dst, size_t cap, const char* src, size_t n) {
  n = std::min(n, cap - 1);
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool ParseLnk(char* text, Shortcut* s) {
  for (char* line = text; *line;) {
    char* nl = strchr(line, '\n');
    size_t len = nl ? static_cast<size_t>(nl - line) : strlen(line);
    size_t trimmed = len;
    while (trimmed > 0 && (line[trimmed - 1] == '\r' || line[trimmed - 1] == ' ')) --trimmed;
    char* eq = static_cast<char*>(memchr(line, '=', trimmed));
    if (eq) {
      const size_t klen = eq - line;
      const char* v = eq + 1;
      const size_t vlen = trimmed - klen - 1;
      if (klen == 4 && strncasecmp(line, "name", 4) == 0) CopyStr(s->label, sizeof(s->label), v, vlen);
      else if (klen == 4 && strncasecmp(line, "exec", 4) == 0) CopyStr(s->command, sizeof(s->command), v, vlen);
      else if (klen == 4 && strncasecmp(line, "icon", 4) == 0) CopyStr(s->icon_path, sizeof(s->icon_path), v, vlen);
      else if (klen == 3 && strncasecmp(line, "cwd", 3) == 0) CopyStr(s->cwd, sizeof(s->cwd), v, vlen);
      else if (klen == 8 && strncasecmp(line, "terminal", 8) == 0) s->terminal = vlen > 0 && v[0] == '1';
      else if (klen == 7 && strncasecmp(line, "special", 7) == 0) s->trash = vlen >= 5 && strncasecmp(v, "trash", 5) == 0;
    }
    line += len + (nl ? 1 : 0);
    if (!nl) break;
  }
  return s->label[0] != '\0';
}

void LoadIcons(Shortcut* s) {
  if (s->icon_path[0]) {
    LoadIcon(s->icon_path, &s->icon);
    if (s->trash) {  // 満杯のゴミ箱は TRASH.XIC → TRASHF.XIC
      char full[80];
      const char* dot = strrchr(s->icon_path, '.');
      if (dot) {
        snprintf(full, sizeof(full), "%.*sF%s", static_cast<int>(dot - s->icon_path), s->icon_path, dot);
        LoadIcon(full, &s->icon_full);
      }
    }
  }
}

void LoadDir(const char* dir, std::vector<Shortcut>* out) {
  out->clear();
  std::vector<std::string> names;
  fsops::DirInfo info;
  for (int i = 0; fsops::ReadDir(dir, i, &info) && i < 64; ++i) {
    const size_t n = strlen(info.name);
    if (!info.is_dir && n > 4 && strcasecmp(info.name + n - 4, ".LNK") == 0) {
      names.push_back(info.name);
    }
  }
  std::sort(names.begin(), names.end());
  for (const auto& name : names) {
    char path[64], text[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name.c_str());
    if (!ReadSmallFile(path, text, sizeof(text))) continue;
    Shortcut s;
    if (ParseLnk(text, &s)) {
      LoadIcons(&s);
      out->push_back(std::move(s));
    }
  }
}

// ショートカットが無い環境(古いボリューム)用の既定
void AddFallback(std::vector<Shortcut>* out, const char* label, const char* command, bool term) {
  Shortcut s;
  CopyStr(s.label, sizeof(s.label), label, strlen(label));
  CopyStr(s.command, sizeof(s.command), command, strlen(command));
  s.terminal = term;
  out->push_back(std::move(s));
}

void LoadAll() {
  LoadDir("/desktop", &icons);
  LoadDir("/menu", &menu);
  if (icons.empty()) {
    AddFallback(&icons, "Terminal", "", true);
    AddFallback(&icons, "Editor", "tedit", false);
    AddFallback(&icons, "Paint", "paint", false);
  }
  if (menu.empty()) {
    for (const auto& s : icons) menu.push_back(s);
  }
  trash_full = !fsops::TrashIsEmpty();
}

// ---- 描画 ----
Vector2D<int> IconPos(int i) {
  const int per_col = std::max(1, (ScreenWH().y - 50 - kIconTop - 40) / kIconPitch);
  return {kIconX + (i / per_col) * kColumnPitch, kIconTop + (i % per_col) * kIconPitch};
}

void DrawFallbackTile(PixelWriter& w, Vector2D<int> pos, int size, const char* label) {
  FillRectangle(w, pos, {size, size}, {70, 130, 220});
  DrawRectangle(w, pos, {size, size}, {255, 255, 255});
  char ch = label[0] ? label[0] : '?';
  WriteAscii(w, pos + Vector2D<int>{size / 2 - 4, size / 2 - 8}, ch, {255, 255, 255});
}

void DrawOneIcon(PixelWriter& w, int index, bool selected) {
  const auto& s = icons[index];
  const auto pos = IconPos(index);
  const PixelColor bg = selected ? kHighlight : kDesktopBGColor;
  if (selected) {
    FillRectangle(w, pos + Vector2D<int>{-8, -6}, {kIconSize + 16, kIconSize + 34}, kHighlight);
    DrawRectangle(w, pos + Vector2D<int>{-8, -6}, {kIconSize + 16, kIconSize + 34}, {255, 255, 255});
  } else {
    FillRectangle(w, pos + Vector2D<int>{-8, -6}, {kIconSize + 16, kIconSize + 34}, kDesktopBGColor);
  }
  const Icon& ic = (s.trash && trash_full && s.icon_full.Valid()) ? s.icon_full : s.icon;
  if (ic.Valid()) {
    DrawIcon(w, pos, ic, kIconSize, bg);
  } else {
    DrawFallbackTile(w, pos, kIconSize, s.label);
  }
  const int len = static_cast<int>(strlen(s.label));
  const int text_w = std::min(len * 8, kIconSize + 16);
  WriteString(w, pos + Vector2D<int>{(kIconSize - text_w) / 2, kIconSize + 6}, s.label, {255, 255, 255});
}

void DrawAllIcons(PixelWriter& w) {
  for (int i = 0; i < static_cast<int>(icons.size()); ++i) DrawOneIcon(w, i, i == selected_icon);
}

void DrawMenu() {
  auto& w = *menu_window->Writer();
  const int mh = MenuHeight();
  FillRectangle(w, {0, 0}, {kMenuWidth, mh}, kMenuBG);
  DrawRectangle(w, {0, 0}, {kMenuWidth, mh}, {60, 60, 60});
  FillRectangle(w, {1, 1}, {6, mh - 2}, {45, 118, 237});

  const int n = static_cast<int>(menu.size());
  for (int i = 0; i < n; ++i) {
    const int y = kMenuPad + i * kItemHeight;
    const auto& s = menu[i];
    const Icon& ic = (s.trash && trash_full && s.icon_full.Valid()) ? s.icon_full : s.icon;
    if (ic.Valid()) {
      DrawIcon(w, {14, y + 4}, ic, kMenuIconSize, kMenuBG);
    } else {
      FillRectangle(w, {14, y + 4}, {kMenuIconSize, kMenuIconSize}, {70, 130, 220});
    }
    WriteString(w, {44, y + 6}, s.label, kMenuText);
  }
  const int sep_y = kMenuPad + n * kItemHeight + 2;
  FillRectangle(w, {12, sep_y}, {kMenuWidth - 24, 1}, {160, 160, 160});
  for (int i = 0; i < kNumSystem; ++i) {
    const int y = kMenuPad + (n + i) * kItemHeight + 6;
    WriteString(w, {44, y + 6}, kSystemLabels[i], kMenuText);
  }
}

void ShowMenu(bool show) {
  if (show == menu_visible) return;
  menu_visible = show;
  if (show) {
    layer_manager->UpDown(menu_layer_id, layer_manager->GetHeight(mouse_layer_id));
    layer_manager->Draw(menu_layer_id);
  } else {
    layer_manager->Hide(menu_layer_id);
    layer_manager->Draw({MenuPos(), {kMenuWidth, MenuHeight()}});
  }
}

void RebuildMenuWindow() {
  const bool was_visible = menu_visible;
  if (menu_visible) ShowMenu(false);
  menu_window = std::make_shared<Window>(kMenuWidth, MenuHeight(), screen_config.pixel_format);
  DrawMenu();
  auto layer = layer_manager->FindLayer(menu_layer_id);
  layer->SetWindow(menu_window);
  layer->Move(MenuPos());
  (void)was_visible;
}

void RedrawIconArea(int index) {
  auto bg = layer_manager->FindLayer(bg_layer_id);
  DrawOneIcon(*bg->GetWindow()->Writer(), index, index == selected_icon);
  layer_manager->Draw({IconPos(index) + Vector2D<int>{-8, -6}, {kIconSize + 16, kIconSize + 34}});
}

void Launch(const Shortcut& s) {
  LaunchTerminalCommand(s.command, s.terminal, s.cwd[0] ? s.cwd : nullptr);
}

int MenuItemAt(Vector2D<int> p) {
  const auto rel = p - MenuPos();
  if (rel.y < kMenuPad || rel.y >= MenuHeight() - kMenuPad) return -1;
  const int idx = (rel.y - kMenuPad) / kItemHeight;
  return idx < MenuItemCount() ? idx : -1;
}

// 表示名から 8.3 のファイル名(拡張子なし)を作る
void LnkBaseName(const char* label, char* out) {
  int n = 0;
  for (const char* p = label; *p && n < 8; ++p) {
    if (isalnum(static_cast<unsigned char>(*p))) out[n++] = toupper(static_cast<unsigned char>(*p));
  }
  if (n == 0) out[n++] = 'X';
  out[n] = '\0';
}

}  // namespace

namespace desktop {

void DrawDecorations(PixelWriter& writer) {
  const auto size = StartButtonSize();
  const auto pos = StartButtonPos();
  FillRectangle(writer, pos, size, {80, 80, 80});
  DrawRectangle(writer, {10, pos.y + 10}, {30, 30}, {160, 160, 160});
  FillRectangle(writer, {16, pos.y + 16}, {18, 18}, {45, 118, 237});
  WriteString(writer, {52, pos.y + 17}, "XinOS", {255, 255, 255});
}

void Reload() {
  if (!initialized) return;
  if (menu_visible) ShowMenu(false);
  selected_icon = -1;
  LoadAll();
  auto bg = layer_manager->FindLayer(bg_layer_id);
  DrawDesktop(*bg->GetWindow()->Writer());
  DrawAllIcons(*bg->GetWindow()->Writer());
  RebuildMenuWindow();
  layer_manager->Draw(bg_layer_id);
}

void Initialize(unsigned int bg_id, unsigned int mouse_id) {
  bg_layer_id = bg_id;
  mouse_layer_id = mouse_id;
  LoadAll();
  menu_window = std::make_shared<Window>(kMenuWidth, MenuHeight(), screen_config.pixel_format);
  DrawMenu();
  menu_layer_id = layer_manager->NewLayer().SetWindow(menu_window).Move(MenuPos()).ID();
  initialized = true;
  auto bg = layer_manager->FindLayer(bg_layer_id);
  DrawAllIcons(*bg->GetWindow()->Writer());
  layer_manager->Draw(bg_layer_id);
}

void RefreshTrash() {
  if (!initialized) return;
  const bool full = !fsops::TrashIsEmpty();
  if (full == trash_full) return;
  trash_full = full;
  for (int i = 0; i < static_cast<int>(icons.size()); ++i) {
    if (icons[i].trash) RedrawIconArea(i);
  }
  DrawMenu();
  if (menu_visible) layer_manager->Draw(menu_layer_id);
}

bool OnMousePress(Vector2D<int> pos) {
  if (!initialized) return false;
  if (Inside(pos, StartButtonPos(), StartButtonSize())) {
    ShowMenu(!menu_visible);
    return true;
  }
  if (menu_visible) {
    if (Inside(pos, MenuPos(), {kMenuWidth, MenuHeight()})) {
      const int idx = MenuItemAt(pos);
      if (idx >= 0) {
        ShowMenu(false);
        if (idx < static_cast<int>(menu.size())) {
          Launch(menu[idx]);
        } else {
          LaunchTerminalCommand(kSystemCommands[idx - menu.size()], false);
        }
      }
      return true;
    }
    ShowMenu(false);
  }

  auto layer = layer_manager->FindLayerByPosition(pos, mouse_layer_id);
  if (layer && (layer->ID() == bg_layer_id || layer->ID() == console->LayerID())) {
    int hit = -1;
    for (int i = 0; i < static_cast<int>(icons.size()); ++i) {
      if (Inside(pos, IconPos(i) + Vector2D<int>{-8, -6}, {kIconSize + 16, kIconSize + 34})) {
        hit = i;
        break;
      }
    }
    const int prev = selected_icon;
    if (hit != prev) {
      selected_icon = hit;
      if (prev >= 0) RedrawIconArea(prev);
      if (hit >= 0) RedrawIconArea(hit);
    }
    if (hit >= 0) {
      const auto now = timer_manager->CurrentTick();
      if (last_click_icon == hit && now - last_click_tick < kTimerFreq * 0.6) {
        Launch(icons[hit]);
        last_click_icon = -1;
      } else {
        last_click_icon = hit;
        last_click_tick = now;
      }
      return true;
    }
  }
  return false;
}

int AddShortcut(const char* label, const char* command, const char* icon_path,
                bool terminal, bool start_menu) {
  const char* dir = start_menu ? "/menu" : "/desktop";
  if (fat::FindFile(dir).first == nullptr && fat::CreateDirectory(dir).error) return 1;
  RemoveShortcut(label, start_menu);
  char base[16], path[64], text[400];
  LnkBaseName(label, base);
  snprintf(path, sizeof(path), "%s/%s.LNK", dir, base);
  const int n = snprintf(text, sizeof(text), "name=%s\nexec=%s\nicon=%s\nterminal=%d\ncwd=/home\n",
                         label, command, icon_path, terminal ? 1 : 0);
  auto [ e, err ] = fat::CreateFile(path);
  if (err) return 1;
  fat::FileDescriptor fd{*e};
  fd.Write(text, n);
  Reload();
  return 0;
}

int RemoveShortcut(const char* label, bool start_menu) {
  const char* dir = start_menu ? "/menu" : "/desktop";
  fsops::DirInfo info;
  for (int i = 0; fsops::ReadDir(dir, i, &info); ++i) {
    char path[64], text[512];
    snprintf(path, sizeof(path), "%s/%s", dir, info.name);
    if (!ReadSmallFile(path, text, sizeof(text))) continue;
    Shortcut s;
    if (ParseLnk(text, &s) && strcasecmp(s.label, label) == 0) {
      const int rc = fat::RemoveEntry(path) ? 1 : 0;
      if (!rc) Reload();
      return rc;
    }
  }
  return 1;
}

}  // namespace desktop
