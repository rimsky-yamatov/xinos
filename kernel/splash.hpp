#pragma once

/**
 * @file splash.hpp
 * 起動画面。黒背景に金色の大きな「XinOS」と、Windows XP 風の進捗バー(青いブロックが流れる)を出す。
 */
namespace splash {

/** @brief 画面を黒で塗り、ロゴとバーを描く。ACPI 初期化後に呼ぶ(時間計測の較正に使う)。 */
void Show();

/** @brief Show から total_ms ミリ秒が経つまでバーをアニメーションさせる。 */
void RunUntil(unsigned long total_ms);

}  // namespace splash
