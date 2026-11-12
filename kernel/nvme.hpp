#pragma once

#include <vector>

#include "blockdev.hpp"

/**
 * @file nvme.hpp
 * NVM Express (PCIe SSD) のポーリング式ドライバ。名前空間1のみ、I/Oキュー1本。
 */
namespace nvme {

/** @brief PCI 上の NVMe コントローラを探して初期化し、見つけたデバイスを out に追加する。 */
void Probe(std::vector<BlockDevice*>& out);

}  // namespace nvme
