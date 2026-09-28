// SPDX-License-Identifier: MIT
#pragma once
#include <donut/app/imgui_renderer.h>
#include <functional>
namespace avboit
{
class GpuProfiler;
class MaterialTable;
struct MaterialDefaults;
class ViewerUi : public donut::app::ImGui_Renderer
{
  public:
    ViewerUi(donut::app::DeviceManager *manager, std::function<void()> controls);

  protected:
    void buildUI() override;

  private:
    std::function<void()> drawControls;
};
void DrawGpuProfiler(GpuProfiler &profiler);
void DrawMaterialEditor(MaterialTable &materials, const MaterialDefaults &defaults, bool &visible, int &selected);
} // namespace avboit
