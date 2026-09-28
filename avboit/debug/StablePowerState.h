// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>

struct ID3D12Device;
namespace avboit
{
// Benchmark-only scope. Never silently export unlocked timings as stable data.
class StablePowerState
{
  public:
    explicit StablePowerState(nvrhi::IDevice *device);
    ~StablePowerState();
    StablePowerState(const StablePowerState &) = delete;
    StablePowerState &operator=(const StablePowerState &) = delete;

  private:
    ID3D12Device *m_device = nullptr;
};
} // namespace avboit
