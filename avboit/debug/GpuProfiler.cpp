// SPDX-License-Identifier: MIT
#include "debug/GpuProfiler.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <vector>
#include <cstdio>

namespace avboit
{
static const char *Names[GpuProfiler::Count] = {
    "frame",       "shadow",        "opaque",           "interface",        "bounds",
    "adaptive_z",  "physical_mask", "extinction_clear", "extinction_splat", "integration",
    "zero_depth",  "accumulation",  "resolve",          "blur_base",        "blur",
    "composition", "vfx",           "vfx_apply",        "display"};

const char *GpuProfiler::StageName(unsigned stage)
{
    return Names[stage];
}
const char *GpuProfiler::DisplayName(unsigned stage)
{
    // Keep CSV column names stable for existing benchmark comparisons.
    if (stage == Resolve)
        return "B_prime_cache";
    if (stage == Composition)
        return "resolve_and_tile_copy";
    return Names[stage];
}
std::vector<float> GpuProfiler::History(Stage stage)
{
    Collect();
    std::vector<Sample> ordered(samples.begin(), samples.end());
    std::sort(ordered.begin(), ordered.end(), [](const Sample &a, const Sample &b) { return a.frame < b.frame; });
    std::vector<float> values;
    for (const auto &sample : ordered)
        values.push_back(sample.ms[stage]);
    return values;
}
void GpuProfiler::Initialize(nvrhi::IDevice *d, bool detailed, bool enabled)
{
    device = d;
    stageCount = enabled ? (detailed ? Count : 1) : 0;
    if (!enabled)
        return;
    for (auto &slot : ring)
        for (auto &query : slot.queries)
        {
            query = device->createTimerQuery();
            if (!query)
                throw std::runtime_error("GPU timer allocation failed");
        }
}
void GpuProfiler::Collect()
{
    for (auto &slot : ring)
        if (slot.pending && device->pollTimerQuery(slot.queries[Frame]))
        {
            Sample sample{};
            sample.frame = slot.frame;
            for (unsigned stage = 0; stage < stageCount; ++stage)
            {
                sample.ms[stage] = 1000.f * device->getTimerQueryTime(slot.queries[stage]);
                if (!std::isfinite(sample.ms[stage]) || sample.ms[stage] < 0)
                    throw std::runtime_error("Invalid GPU timestamp interval");
            }
            samples.push_back(sample);
            if (samples.size() > 600)
                samples.pop_front();
            slot.pending = false;
        }
}
void GpuProfiler::BeginFrame(nvrhi::ICommandList *commands)
{
    if (stageCount == 0)
        return;
    Collect();
    active = nullptr;
    for (auto &slot : ring)
        if (!slot.pending)
        {
            active = &slot;
            break;
        }
    ++frameNumber;
    if (!active)
    {
        ++skipped;
        return;
    }
    active->frame = frameNumber;
    for (auto &query : active->queries)
        device->resetTimerQuery(query);
    commands->beginTimerQuery(active->queries[Frame]);
}
GpuTimingRoute GpuProfiler::Route(nvrhi::ICommandList *commands, Stage stage)
{
    return [this, commands, stage](bool begin) { Event(commands, stage, begin); };
}
void GpuProfiler::Event(nvrhi::ICommandList *commands, Stage stage, bool begin)
{
    if (stage == Frame)
    {
        if (begin)
            BeginFrame(commands);
        else
            EndFrame(commands);
        return;
    }
    if (!active || stageCount <= 1)
        return;
    if (begin)
        commands->beginTimerQuery(active->queries[stage]);
    else
        commands->endTimerQuery(active->queries[stage]);
}
void GpuProfiler::EndFrame(nvrhi::ICommandList *commands)
{
    if (!active)
        return;
    commands->endTimerQuery(active->queries[Frame]);
    active->pending = true;
    active = nullptr;
}
void GpuProfiler::ResetSamples()
{
    Collect();
    samples.clear();
    skipped = 0;
}
void GpuProfiler::ExportCsv(const std::string &path, const std::string &metadata)
{
    Collect();
    if (samples.empty())
        throw std::runtime_error("No GPU timing samples");
    std::ofstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open benchmark CSV");
    file << "# " << metadata << "\n# skipped_frames=" << skipped << "\nframe_id";
    for (unsigned stage = 0; stage < stageCount; ++stage)
        file << "," << Names[stage] << "_ms";
    file << "\n" << std::setprecision(7);
    std::vector<Sample> ordered(samples.begin(), samples.end());
    std::sort(ordered.begin(), ordered.end(), [](const Sample &a, const Sample &b) { return a.frame < b.frame; });
    for (const auto &sample : ordered)
    {
        file << sample.frame;
        for (unsigned stage = 0; stage < stageCount; ++stage)
            file << "," << sample.ms[stage];
        file << "\n";
    }
    printf("GPU timings: %zu frames, %u skipped (milliseconds)\n", samples.size(), skipped);
    for (unsigned stage = 0; stage < stageCount; ++stage)
    {
        std::vector<float> values;
        for (const auto &sample : samples)
            values.push_back(sample.ms[stage]);
        std::sort(values.begin(), values.end());
        printf("  %-20s median %.4f  p95 %.4f\n", Names[stage], values[values.size() / 2],
               values[std::min(values.size() - 1, size_t(std::ceil(values.size() * .95) - 1))]);
    }
}
} // namespace avboit
