#pragma once

#include <cstdint>

#include "graphics.hpp"

/**
 * @file desktop.hpp
 * デスクトップアイコン(/desktop/*.LNK)・スタートメニュー(/menu/*.LNK)・ゴミ箱アイコン。
 *
 * ショートカット(.LNK)は "key=value" の行からなるテキスト:
 *   name=表示名 / exec=実行するコマンド / icon=XICの絶対パス /
 *   terminal=0|1(端末ウィンドウを出すか) / cwd=作業ディレクトリ / special=trash
 */
namespace desktop {

/** @brief タスクバーと Start ボタンを描画する． */
void DrawDecorations(PixelWriter& writer);

/** @brief スタートメニューのレイヤーを作り、ショートカットを読み込む． */
void Initialize(unsigned int bg_layer_id, unsigned int mouse_layer_id);

/** @brief ショートカットとアイコンを読み込み直して再描画する(ボリュームの入れ替え後や link 後)． */
void Reload();

/** @brief 左ボタンが押された瞬間に呼ぶ．処理済みなら true． */
bool OnMousePress(Vector2D<int> pos);

/** @brief ゴミ箱の中身が変わったときに呼び、ゴミ箱アイコンを更新する． */
void RefreshTrash();

/** @brief ショートカットを作る(同じ表示名があれば置き換える)。0 で成功。 */
int AddShortcut(const char* label, const char* command, const char* icon_path,
                bool terminal, bool start_menu);

/** @brief 表示名が一致するショートカットを消す。0 で成功。 */
int RemoveShortcut(const char* label, bool start_menu);

}  // namespace desktop
