// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include <array>
#include <deque>
#include <string>
#include <vector>
#include "render/GpuScope.h"

namespace avboit
{
// Timer reads are nonblocking during interactive rendering. Benchmark code may
// drain the GPU between batches, outside all measured command-list intervals.
class GpuProfiler
{
  public:
    enum Stage
    {
        Frame,
        Shadow,
        Opaque,
        Interface,
        Bounds,
        AdaptiveZ,
        PhysicalMask,
        ExtinctionClear,
        ExtinctionSplat,
        Integration,
        ZeroDepth,
        Accumulation,
        Resolve,
        BlurBase,
        Blur,
        Composition,
        Vfx,
        VfxApply,
        Display,
        Count
    };
    static constexpr unsigned RingSize = 6;
    void Initialize(nvrhi::IDevice *device, bool detailed, bool enabled);
    GpuTimingRoute Route(nvrhi::ICommandList *commands, Stage stage);
    void Event(nvrhi::ICommandList *commands, Stage stage, bool begin);
    void Collect();
    std::vector<float> History(Stage stage);
    static const char *StageName(unsigned stage);
    static const char *DisplayName(unsigned stage);
    unsigned ActiveStages() const { return stageCount; }
    unsigned SkippedFrames() const { return skipped; }
    void ResetSamples();
    void ExportCsv(const std::string &path, const std::string &metadata);

  private:
    void BeginFrame(nvrhi::ICommandList *commands);
    void EndFrame(nvrhi::ICommandList *commands);
    struct Slot
    {
        std::array<nvrhi::TimerQueryHandle, Count> queries;
        bool pending = false;
        unsigned frame = 0;
    };
    struct Sample
    {
        unsigned frame;
        std::array<float, Count> ms;
    };
    nvrhi::IDevice *device = nullptr;
    std::array<Slot, RingSize> ring;
    std::deque<Sample> samples;
    Slot *active = nullptr;
    unsigned frameNumber = 0, skipped = 0, stageCount = Count;
};
} // namespace avboit
