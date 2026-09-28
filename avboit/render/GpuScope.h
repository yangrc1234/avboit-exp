// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include <functional>
#include <utility>

namespace avboit
{
// Optional timestamp routing supplied by the host. Algorithm modules only
// depend on this callback, never on the sample's profiler/query implementation.
using GpuTimingRoute = std::function<void(bool begin)>;

class GpuScope
{
  public:
    GpuScope(nvrhi::ICommandList *commands, const char *name, GpuTimingRoute timing = {})
        : m_commands(commands), m_timing(std::move(timing))
    {
        // Unconditional GPU event: capture tools still see the hierarchy when
        // timestamps are disabled, frame-only, or their query ring is full.
        m_commands->beginMarker(name);
        if (m_timing)
            m_timing(true);
    }
    ~GpuScope()
    {
        if (m_timing)
            m_timing(false);
        m_commands->endMarker();
    }
    GpuScope(const GpuScope &) = delete;
    GpuScope &operator=(const GpuScope &) = delete;

  private:
    nvrhi::ICommandList *m_commands;
    GpuTimingRoute m_timing;
};
} // namespace avboit

#define AVBOIT_GPU_JOIN_IMPL(a, b) a##b
#define AVBOIT_GPU_JOIN(a, b) AVBOIT_GPU_JOIN_IMPL(a, b)
// AVBOIT_GPU_SCOPE(commands, name [, timestampRoute]); scope ends at block exit.
#define AVBOIT_GPU_SCOPE(...) ::avboit::GpuScope AVBOIT_GPU_JOIN(avboitGpuScope_, __COUNTER__)(__VA_ARGS__)
