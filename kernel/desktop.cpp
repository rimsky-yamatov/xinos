#include "desktop.hpp"

#include <array>
#include <cstring>
#include <memory>

#include "font.hpp"
#include "graphics.hpp"
#include "layer.hpp"
#include "timer.hpp"
#include "window.hpp"

extern TimerManager* timer_manager;
void LaunchTerminalCommand(const char* command, bool show_window);

namespace {

struct Entry {
  const char* label;    // メニュー/アイコンの表示名
  const char* command;  // 実行するコマンド("" なら空の端末)
  bool show_terminal;   // true なら端末ウィンドウ付きで実行
  PixelColor color;     // アイコン色
  char glyph;           // アイコンに描く文字
};

const Entry kEntries[] = {
  {"Terminal",   "",                  true,  {40, 40, 40},    '>'},
  {"Editor",     "tedit",             false, {230, 200, 60},  'E'},
  {"Paint",      "paint",             false, {220, 90, 90},   'P'},
  {"Viewer",     "gview fujisan.jpg", false, {90, 180, 110},  'V'},
  {"Blocks",     "blocks",            false, {90, 130, 220},  'B'},
  {"Cube",       "cube",              false, {170, 100, 210}, 'C'},
  {"Eye",        "eye",               false, {240, 150, 60},  'O'},
  {"Stars",      "stars",             false, {30, 30, 90},    '*'},
};
constexpr int kNumEntries = sizeof(kEntries) / sizeof(kEntries[0]);

// デスクトップに置くアイコン(kEntries の先頭 kNumIcons 個)
constexpr int kNumIcons = 5;
constexpr int kIconSize = 48;
constexpr int kIconX = 24;
constexpr int kIconTop = 40;
constexpr int kIconPitch = 88;

// システム項目(メニュー最下部)
const char* const kSystemLabels[] = {"Reboot", "Shut down"};
const char* const kSystemCommands[] = {"reboot", "poweroff"};
constexpr int kNumSystem = 2;

constexpr int kMenuWidth = 200;
constexpr int kItemHeight = 28;
constexpr int kMenuPad = 4;
constexpr int kMenuItems = kNumEntries + kNumSystem;
constexpr int kMenuHeight = kMenuItems * kItemHeight + kMenuPad * 2 + 6;

const PixelColor kMenuBG{236, 236, 236};
const PixelColor kMenuText{0, 0, 0};

unsigned int bg_layer_id = 0, mouse_layer_id = 0, menu_layer_id = 0;
std::shared_ptr<Window> menu_window;
bool menu_visible = false;
int selected_icon = -1;
int last_click_icon = -1;
unsigned long last_click_tick = 0;

Vector2D<int> ScreenWH() {
  return ScreenSize();
}

Vector2D<int> StartButtonPos() { return {0, ScreenWH().y - 50}; }
Vector2D<int> StartButtonSize() { return {ScreenWH().x / 5, 50}; }

Vector2D<int> MenuPos() { return {0, ScreenWH().y - 50 - kMenuHeight}; }

bool Inside(Vector2D<int> p, Vector2D<int> pos, Vector2D<int> size) {
  return p.x >= pos.x && p.x < pos.x + size.x &&
         p.y >= pos.y && p.y < pos.y + size.y;
}

void DrawIcon(PixelWriter& w, int index, bool selected) {
  const auto& e = kEntries[index];
  const Vector2D<int> pos{kIconX, kIconTop + index * kIconPitch};
  if (selected) {
    FillRectangle(w, pos + Vector2D<int>{-8, -6}, {kIconSize + 16, kIconSize + 34},
                  {70, 130, 220});
    DrawRectangle(w, pos + Vector2D<int>{-8, -6}, {kIconSize + 16, kIconSize + 34},
                  {255, 255, 255});
  }
  FillRectangle(w, pos, {kIconSize, kIconSize}, e.color);
  DrawRectangle(w, pos, {kIconSize, kIconSize}, {255, 255, 255});
  WriteAscii(w, pos + Vector2D<int>{kIconSize / 2 - 4, kIconSize / 2 - 8},
             e.glyph, {255, 255, 255});
  const int len = static_cast<int>(strlen(e.label));
  WriteString(w, pos + Vector2D<int>{(kIconSize - 8 * len) / 2, kIconSize + 6},
              e.label, {255, 255, 255});
}

void DrawMenu() {
  auto& w = *menu_window->Writer();
  FillRectangle(w, {0, 0}, {kMenuWidth, kMenuHeight}, kMenuBG);
  DrawRectangle(w, {0, 0}, {kMenuWidth, kMenuHeight}, {60, 60, 60});
  // 左端に帯(ブランド)
  FillRectangle(w, {1, 1}, {6, kMenuHeight - 2}, {45, 118, 237});

  for (int i = 0; i < kNumEntries; ++i) {
    const int y = kMenuPad + i * kItemHeight;
    FillRectangle(w, {16, y + 6}, {16, 16}, kEntries[i].color);
    WriteString(w, {40, y + 6}, kEntries[i].label, kMenuText);
  }
  const int sep_y = kMenuPad + kNumEntries * kItemHeight + 2;
  FillRectangle(w, {12, sep_y}, {kMenuWidth - 24, 1}, {160, 160, 160});
  for (int i = 0; i < kNumSystem; ++i) {
    const int y = kMenuPad + (kNumEntries + i) * kItemHeight + 6;
    WriteString(w, {40, y + 6}, kSystemLabels[i], kMenuText);
  }
}

void ShowMenu(bool show) {
  if (show == menu_visible) {
    return;
  }
  menu_visible = show;
  if (show) {
    layer_manager->UpDown(menu_layer_id, layer_manager->GetHeight(mouse_layer_id));
    layer_manager->Draw(menu_layer_id);
  } else {
    layer_manager->Hide(menu_layer_id);
    layer_manager->Draw({MenuPos(), {kMenuWidth, kMenuHeight}});
  }
}

void RedrawIcon(int index) {
  // 背景レイヤーを書き換えて、アイコン領域だけ再描画する
  auto bg = layer_manager->FindLayer(bg_layer_id);
  DrawIcon(*bg->GetWindow()->Writer(), index, index == selected_icon);
  layer_manager->Draw({{kIconX - 8, kIconTop + index * kIconPitch - 6},
                       {kIconSize + 16, kIconSize + 34}});
}

void Launch(const char* command, bool show_terminal) {
  LaunchTerminalCommand(command, show_terminal);
}

int MenuItemAt(Vector2D<int> p) {
  const auto rel = p - MenuPos();
  if (rel.y < kMenuPad || rel.y >= kMenuHeight - kMenuPad) {
    return -1;
  }
  int idx = (rel.y - kMenuPad) / kItemHeight;
  if (idx >= kMenuItems) {
    return -1;
  }
  return idx;
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
  for (int i = 0; i < kNumIcons; ++i) {
    DrawIcon(writer, i, false);
  }
}

void Initialize(unsigned int bg_id, unsigned int mouse_id) {
  bg_layer_id = bg_id;
  mouse_layer_id = mouse_id;
  menu_window = std::make_shared<Window>(
      kMenuWidth, kMenuHeight, screen_config.pixel_format);
  DrawMenu();
  menu_layer_id = layer_manager->NewLayer()
    .SetWindow(menu_window)
    .Move(MenuPos())
    .ID();
  // 作成直後はスタックに積まれていないので非表示のまま
}

bool OnMousePress(Vector2D<int> pos) {
  // スタートボタン
  if (Inside(pos, StartButtonPos(), StartButtonSize())) {
    ShowMenu(!menu_visible);
    return true;
  }

  // メニュー表示中
  if (menu_visible) {
    if (Inside(pos, MenuPos(), {kMenuWidth, kMenuHeight})) {
      const int idx = MenuItemAt(pos);
      if (idx >= 0) {
        ShowMenu(false);
        if (idx < kNumEntries) {
          Launch(kEntries[idx].command, kEntries[idx].show_terminal);
        } else {
          Launch(kSystemCommands[idx - kNumEntries], false);
        }
      }
      return true;
    }
    ShowMenu(false);  // メニュー外のクリックは閉じて、そのまま通常処理へ
  }

  // デスクトップアイコン: 最前面が背景レイヤーのときだけ判定
  auto layer = layer_manager->FindLayerByPosition(pos, mouse_layer_id);
  if (layer && layer->ID() == bg_layer_id) {
    int hit = -1;
    for (int i = 0; i < kNumIcons; ++i) {
      if (Inside(pos, {kIconX - 8, kIconTop + i * kIconPitch - 6},
                 {kIconSize + 16, kIconSize + 34})) {
        hit = i;
        break;
      }
    }
    const int prev = selected_icon;
    if (hit != prev) {
      selected_icon = hit;
      if (prev >= 0) RedrawIcon(prev);
      if (hit >= 0) RedrawIcon(hit);
    }
    if (hit >= 0) {
      const auto now = timer_manager->CurrentTick();
      if (last_click_icon == hit && now - last_click_tick < kTimerFreq * 0.6) {
        Launch(kEntries[hit].command, kEntries[hit].show_terminal);
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

}  // namespace desktop
