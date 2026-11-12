#pragma once

#include <vector>

#include "blockdev.hpp"

/**
 * @file ahci.hpp
 * AHCI(SATA)コントローラのポーリング式ドライバ。接続された SATA ディスクごとに1デバイス。
 */
namespace ahci {

void Probe(std::vector<BlockDevice*>& out);

}  // namespace ahci
