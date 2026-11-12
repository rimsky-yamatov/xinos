#include "blockdev.hpp"

#include <cstring>

#include "ahci.hpp"
#include "logger.hpp"
#include "memory_manager.hpp"
#include "nvme.hpp"
#include "virtio_blk.hpp"

namespace {

std::vector<BlockDevice*> devices;
bool initialized = false;

class VirtioDevice : public BlockDevice {
 public:
  const char* Name() const override { return "virtio0"; }
  uint64_t SectorCount() const override { return virtio_blk::SectorCount(); }
  bool Read(uint64_t lba, void* buf, size_t n) override {
    return virtio_blk::Read(lba, buf, n);
  }
  bool Write(uint64_t lba, const void* buf, size_t n) override {
    return virtio_blk::Write(lba, buf, n);
  }
};

}  // namespace

const std::vector<BlockDevice*>& BlockDevices() { return devices; }

void RegisterBlockDevice(BlockDevice* dev) {
  devices.push_back(dev);
  Log(kInfo, "block device %s: %lu MiB\n", dev->Name(), dev->SectorCount() >> 11);
}

void* AllocDmaPages(size_t bytes) {
  const size_t frames = (bytes + kBytesPerFrame - 1) / kBytesPerFrame;
  auto [ frame, err ] = memory_manager->Allocate(frames);
  if (err) {
    return nullptr;
  }
  void* p = frame.Frame();
  memset(p, 0, frames * kBytesPerFrame);
  return p;
}

void InitializeBlockDevices() {
  if (initialized) {
    return;
  }
  initialized = true;
  nvme::Probe(devices);
  ahci::Probe(devices);
  if (virtio_blk::Initialize()) {
    devices.push_back(new VirtioDevice);
  }
  for (auto* d : devices) {
    Log(kInfo, "block device %s: %lu MiB\n", d->Name(), d->SectorCount() >> 11);
  }
}
