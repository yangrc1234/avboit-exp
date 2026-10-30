// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <imgui.h>

namespace avboit
{
// Editor-only overlay. The projected handles use the unjittered display view;
// dragging intersects a fixed world-space plane, independent of render resolution.
class TranslationGizmo
{
  public:
    using Vector = std::array<float, 3>;
    struct View
    {
        Vector eye, right, up, forward;
        ImVec2 size;
        float aspect;
    };
    bool Draw(const View &view, Vector center, Vector lower, Vector upper, Vector &delta);
    bool Active() const { return active >= 0; }
    bool HitTest(ImVec2 mouse) const;
    void Reset();

  private:
    int Hit(ImVec2 mouse) const;
    int active = -1;
    bool visible = false;
    std::array<bool, 3> axisVisible{};
    ImVec2 origin, tips[3];
    Vector startCenter{}, startHit{}, planeNormal{};
    View dragView{};
};
} // namespace avboit
