// SPDX-License-Identifier: MIT
#include "sample/ViewerUi.h"
#include "debug/GpuProfiler.h"
#include "sample/MaterialTable.h"
#include <donut/app/ApplicationBase.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/ShaderFactory.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace avboit
{
namespace
{
class ScopedItemWidth
{
  public:
    explicit ScopedItemWidth(float width) { ImGui::PushItemWidth(width); }
    ~ScopedItemWidth() { ImGui::PopItemWidth(); }
    ScopedItemWidth(const ScopedItemWidth &) = delete;
    ScopedItemWidth &operator=(const ScopedItemWidth &) = delete;
};
} // namespace
ViewerUi::ViewerUi(donut::app::DeviceManager *manager, std::function<void()> controls)
    : ImGui_Renderer(manager), drawControls(std::move(controls))
{
    auto fs = std::make_shared<donut::vfs::RootFileSystem>();
    fs->mount("/shaders/donut", donut::app::GetDirectoryWithExecutable() / "shaders/framework/dxil");
    auto factory = std::make_shared<donut::engine::ShaderFactory>(GetDevice(), fs, "/shaders");
    if (!Init(factory))
        throw std::runtime_error("ImGui renderer initialization failed");
    ImGui::GetIO().IniFilename = nullptr;
    // Recovery can display a warning and keep rendering. Make such errors visible
    // to headless UI checks instead of accepting a successful screenshot alone.
    auto *context = ImGui::GetCurrentContext();
    context->ErrorCallbackUserData = this;
    context->ErrorCallback = [](ImGuiContext *, void *user, const char *message)
    {
        ++static_cast<ViewerUi *>(user)->errorCount;
        std::fprintf(stderr, "ImGui error: %s\n", message);
    };
}
void ViewerUi::buildUI()
{
    drawControls();
}

void DrawGpuProfiler(GpuProfiler &profiler)
{
    // Refresh statistics at 4 Hz while rendering the UI every frame.
    static std::array<std::vector<float>, GpuProfiler::Count> history;
    static double lastUpdate = -1;
    static unsigned previousStages = 0, selected = GpuProfiler::Frame;
    unsigned count = profiler.ActiveStages();
    if (count == 0)
    {
        ImGui::TextUnformatted("GPU profiling disabled");
        return;
    }
    if (count != previousStages || ImGui::GetTime() - lastUpdate > .25)
    {
        for (unsigned stage = 0; stage < count; ++stage)
            history[stage] = profiler.History(GpuProfiler::Stage(stage));
        previousStages = count;
        lastUpdate = ImGui::GetTime();
    }
    if (history[0].empty())
    {
        ImGui::TextUnformatted("Waiting for GPU samples...");
        return;
    }
    selected = std::min(selected, count - 1);
    auto recentMean = [](const std::vector<float> &samples)
    {
        const auto count = std::min(size_t(60), samples.size());
        return std::accumulate(samples.end() - count, samples.end(), 0.f) / float(count);
    };
    float frameMean = recentMean(history[0]);
    ImGui::Text("Render: %.3f ms | mean of last %zu frames", frameMean, std::min(size_t(60), history[0].size()));
    ImGui::TextDisabled("UI/present excluded. Detailed queries add overhead.");
    ImGui::TextUnformatted("Pass cost distribution (not a timestamp timeline)");
    if (ImGui::BeginTable("pass-costs", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Mean ms", ImGuiTableColumnFlags_WidthFixed, 65);
        ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableHeadersRow();
        for (unsigned stage = 0; stage < count; ++stage)
        {
            if (history[stage].empty())
                continue;
            float mean = recentMean(history[stage]);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(GpuProfiler::DisplayName(stage), selected == stage))
                selected = stage;
            if (ImGui::IsItemHovered() && stage == GpuProfiler::Resolve)
                ImGui::SetTooltip("B occupancy tile draw; reference modes also write I/R/q caches.");
            if (ImGui::IsItemHovered() && stage == GpuProfiler::Composition)
                ImGui::SetTooltip("Main resolve + frost: active tile quads blend into SceneColor.");
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", mean);
            ImGui::TableNextColumn();
            ImGui::ProgressBar(std::min(1.f, mean / std::max(frameMean, .00001f)), ImVec2(-1, 0), "");
        }
        ImGui::EndTable();
    }
    auto &values = history[selected];
    if (!values.empty())
    {
        auto range = std::minmax_element(values.begin(), values.end());
        ImGui::Text("%s: history min %.4f / max %.4f ms", GpuProfiler::DisplayName(selected), *range.first,
                    *range.second);
        ImGui::PlotLines("##history", values.data(), int(values.size()), 0, "GPU ms", 0.f,
                         std::max(.01f, *range.second * 1.1f), ImVec2(-1, 90));
    }
}
} // namespace avboit

namespace avboit
{
void DrawMaterialEditor(MaterialTable &materials, const MaterialDefaults &defaults, int selectedMaterial)
{
    if (selectedMaterial < 0 || size_t(selectedMaterial) >= materials.entries.size())
        return;
    const ScopedItemWidth itemWidth(160);
    auto &material = materials.entries[selectedMaterial];
    ImGui::TextDisabled("Material edits apply live.");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Each actor owns its material. Changing the scene preset resets edits.");
    if (material.kind == 0)
    {
        ImGui::ColorEdit3("Radiance", material.color.data(), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::DragFloat3("Radiance RGB", material.color.data(), .1f, 0.f, 1000.f, "%.2f",
                          ImGuiSliderFlags_AlwaysClamp);
        return;
    }
    if (material.kind >= 5)
    {
        ImGui::ColorEdit3("Source radiance", material.color.data(),
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        float extinction = (material.transmission[0] + material.transmission[1] + material.transmission[2]) / 3.f;
        if (ImGui::SliderFloat("Extinction", &extinction, 0.f, 128.f, "%.3f / world unit",
                               ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
            material.transmission[0] = material.transmission[1] = material.transmission[2] = extinction;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Higher extinction makes smoke denser. This slider sets all RGB channels together.");
        if (ImGui::TreeNode("RGB extinction"))
        {
            ImGui::DragFloat3("Extinction RGB", material.transmission.data(), .05f, 0.f, 128.f, "%.3f",
                              ImGuiSliderFlags_AlwaysClamp);
            ImGui::TreePop();
        }
    }
    else if (material.kind != 3)
    {
        if (material.kind != 4)
            ImGui::ColorEdit3("Radiance", material.color.data(), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::SliderFloat("Coverage", &material.color[3], 0, 1);
        ImGui::SliderFloat3("RGB transmission", material.transmission.data(), 0, 1);
    }
    if (material.kind == 3)
    {
        int pattern = int(material.vfxPattern);
        if (ImGui::Combo("Pattern", &pattern, "Oscillating ring\0Heat wave\0Expanding shock\0"))
            material.vfxPattern = unsigned(pattern);
        ImGui::SliderFloat("Phase (cycles)", &material.vfxPhase, 0, 1);
        ImGui::SliderFloat("Strength (px)", &material.gain, 0, 64);
        return;
    }
    if (material.kind == 2 || material.kind == 4)
    {
        ImGui::SliderFloat("Roughness", &material.transmission[3], 0, 1);
        if (material.kind == 2)
            ImGui::SliderFloat("Displacement (px)", &material.displacement, -64, 64);
    }
    if (material.kind < 5)
    {
        auto effective = materials.Resolve(unsigned(selectedMaterial), defaults);
        if (ImGui::Checkbox("Inherit scene optics", &material.inheritOptics) && !material.inheritOptics)
        {
            material.ior = effective.value.ior;
            material.gain = effective.value.gain;
            material.normalSphere = effective.value.normalSphere;
        }
        ImGui::BeginDisabled(material.inheritOptics);
        ImGui::SliderFloat("IOR", &material.ior, 1, 2.5f);
        ImGui::SliderFloat("Displacement gain", &material.gain, 0, 4);
        if (material.kind == 4)
            ImGui::Checkbox("Normal-driven refraction", &material.normalSphere);
        ImGui::EndDisabled();
    }
}

} // namespace avboit
