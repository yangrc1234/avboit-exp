// SPDX-License-Identifier: MIT
#include "sample/TranslationGizmo.h"
#include <algorithm>
#include <cmath>

namespace avboit
{
namespace
{
using V = TranslationGizmo::Vector;
using View = TranslationGizmo::View;
V Sub(V a, V b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
float Dot(V a, V b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
bool Project(const View &v, V point, ImVec2 &pixel)
{
    auto d = Sub(point, v.eye);
    float z = Dot(d, v.forward);
    if (z < .05f)
        return false;
    pixel = {(Dot(d, v.right) / (z * v.aspect * .57735027f) * .5f + .5f) * v.size.x,
             (.5f - Dot(d, v.up) / (z * .57735027f) * .5f) * v.size.y};
    return true;
}
bool PlaneHit(const View &v, ImVec2 mouse, V point, V normal, V &hit)
{
    float x = (mouse.x / v.size.x * 2.f - 1.f) * v.aspect * .57735027f;
    float y = (1.f - mouse.y / v.size.y * 2.f) * .57735027f;
    V ray;
    for (unsigned a = 0; a < 3; ++a)
        ray[a] = v.forward[a] + x * v.right[a] + y * v.up[a];
    float denominator = Dot(ray, normal);
    if (std::abs(denominator) < 1e-5f)
        return false;
    float t = Dot(Sub(point, v.eye), normal) / denominator;
    if (t <= 0)
        return false;
    for (unsigned a = 0; a < 3; ++a)
        hit[a] = v.eye[a] + ray[a] * t;
    return true;
}
float Distance(ImVec2 a, ImVec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}
float SegmentDistance(ImVec2 p, ImVec2 a, ImVec2 b)
{
    float x = b.x - a.x, y = b.y - a.y;
    float t = std::clamp(((p.x - a.x) * x + (p.y - a.y) * y) / std::max(x * x + y * y, 1e-5f), 0.f, 1.f);
    return Distance(p, {a.x + x * t, a.y + y * t});
}
} // namespace
void TranslationGizmo::Reset()
{
    active = -1;
    visible = false;
}
int TranslationGizmo::Hit(ImVec2 mouse) const
{
    if (!visible)
        return -1;
    if (Distance(mouse, origin) < 11.f)
        return 3;
    int best = -1;
    float distance = 8.f;
    for (int a = 0; a < 3; ++a)
        if (axisVisible[a])
        {
            float d = SegmentDistance(mouse, origin, tips[a]);
            if (d < distance)
            {
                distance = d;
                best = a;
            }
        }
    return best;
}
bool TranslationGizmo::HitTest(ImVec2 mouse) const
{
    return Active() || Hit(mouse) >= 0;
}
bool TranslationGizmo::Draw(const View &view, Vector center, Vector lower, Vector upper, Vector &delta)
{
    auto &io = ImGui::GetIO();
    delta = {};
    visible = view.size.x > 0 && view.size.y > 0 && Project(view, center, origin);
    if (!visible)
    {
        active = -1;
        return false;
    }
    float length = Dot(Sub(center, view.eye), view.forward) * (2.f * .57735027f) * 90.f / view.size.y;
    for (int a = 0; a < 3; ++a)
    {
        auto tip = center;
        tip[a] += length;
        axisVisible[a] = Project(view, tip, tips[a]) && Distance(tips[a], origin) > 18.f;
    }
    // Background draw list keeps handles behind ImGui panels. UI windows win input.
    int hovered =
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && !ImGui::IsAnyItemActive() ? Hit(io.MousePos) : -1;
    if (active < 0 && hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        planeNormal = view.forward;
        if (hovered < 3)
            planeNormal[hovered] = 0;
        if (PlaneHit(view, io.MousePos, center, planeNormal, startHit))
        {
            active = hovered;
            dragView = view;
            startCenter = center;
        }
    }
    bool changed = false;
    if (Active())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            delta = Sub(startCenter, center);
            active = -1;
            changed = true;
        }
        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            active = -1;
        else
        {
            V hit;
            if (PlaneHit(dragView, io.MousePos, startCenter, planeNormal, hit))
            {
                auto movement = Sub(hit, startHit);
                for (int a = 0; a < 3; ++a)
                    delta[a] = startCenter[a] + ((active == 3 || active == a) ? movement[a] : 0.f) - center[a];
                changed = Dot(delta, delta) > 1e-12f;
            }
        }
    }
    if (Active() || hovered >= 0)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    auto *draw = ImGui::GetBackgroundDrawList();
    for (unsigned corner = 0; corner < 8; ++corner)
        for (unsigned axis = 0; axis < 3; ++axis)
            if ((corner & (1u << axis)) == 0)
            {
                V a, b;
                for (unsigned c = 0; c < 3; ++c)
                    a[c] = b[c] = (corner & (1u << c)) ? upper[c] : lower[c];
                b[axis] = upper[axis];
                ImVec2 pa, pb;
                if (Project(view, a, pa) && Project(view, b, pb))
                    draw->AddLine(pa, pb, IM_COL32(255, 210, 70, 130));
            }
    const ImU32 colors[] = {IM_COL32(255, 80, 80, 255), IM_COL32(90, 235, 110, 255), IM_COL32(85, 150, 255, 255)};
    const char *labels[] = {"X", "Y", "Z"};
    for (int a = 0; a < 3; ++a)
        if (axisVisible[a])
        {
            auto color = (active == a || hovered == a) ? IM_COL32(255, 240, 100, 255) : colors[a];
            draw->AddLine(origin, tips[a], IM_COL32(0, 0, 0, 200), 6.f);
            draw->AddLine(origin, tips[a], color, 3.f);
            draw->AddCircleFilled(tips[a], 5.f, color);
            draw->AddText({tips[a].x + 7, tips[a].y - 7}, color, labels[a]);
        }
    draw->AddCircleFilled(origin, 8.f,
                          active == 3 || hovered == 3 ? IM_COL32(255, 230, 100, 255) : IM_COL32(240, 240, 240, 255));
    return changed;
}
} // namespace avboit
