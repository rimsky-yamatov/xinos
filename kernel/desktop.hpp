#pragma once

#include <cstdint>

#include "graphics.hpp"

/**
 * @file desktop.hpp
 * デスクトップアイコンとスタートメニュー。
 */
namespace desktop {

/** @brief 背景ウィンドウにデスクトップアイコンと「Start」ボタンを描画する． */
void DrawDecorations(PixelWriter& writer);

/** @brief スタートメニューのレイヤーを作る．レイヤーマネージャ初期化後に呼ぶ． */
void Initialize(unsigned int bg_layer_id, unsigned int mouse_layer_id);

/** @brief 左ボタンが押された瞬間に呼ぶ．
 * @return true なら XinOS のデスクトップ側で処理済み(通常のウィンドウ処理は不要)
 */
bool OnMousePress(Vector2D<int> pos);

/** @brief ゴミ箱の中身が変わったときに呼び、ゴミ箱アイコンを更新する。 */
void RefreshTrash();

}  // namespace desktop
