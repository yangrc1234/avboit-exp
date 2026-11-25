// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace avboit
{
// Gameplay submits finite world-space segments, already clipped to its bullet hit.
// This visual overlay never changes the replicated binary smoke coverage.
class SmokeTrails
{
  public:
    static constexpr unsigned Capacity = 15;
    static constexpr unsigned LutSize = 16;
    static constexpr unsigned LutOrigin = 1 + Capacity * 2;
    using Point = std::array<float, 3>;
    // Header: count; then start.xyz/radius and end.xyz/clear strength per capsule.
    // Two trailing float4s hold the world-space LUT origin and cells-per-metre.
    using GpuData = std::array<std::array<float, 4>, LutOrigin + 2>;
    struct Capsule
    {
        Point start, end;
        float radius, lifetime, age = 0;
    };
    void Add(Point start, Point end, float radius, float lifetime)
    {
        if (!std::isfinite(radius) || !std::isfinite(lifetime) || radius <= 0 || lifetime <= 0)
            return;
        for (unsigned i = 0; i < 3; ++i)
            if (!std::isfinite(start[i]) || !std::isfinite(end[i]))
                return;
        if (m_capsules.size() == Capacity)
            m_capsules.erase(m_capsules.begin());
        m_capsules.push_back({start, end, radius, lifetime});
    }
    void Tick(float dt)
    {
        for (auto &capsule : m_capsules)
            capsule.age += std::max(dt, 0.f);
        m_capsules.erase(
            std::remove_if(m_capsules.begin(), m_capsules.end(), [](const Capsule &c) { return c.age >= c.lifetime; }),
            m_capsules.end());
    }
    void Clear() { m_capsules.clear(); }
    unsigned Count() const { return unsigned(m_capsules.size()); }
    GpuData Pack() const
    {
        GpuData result{};
        result[0][0] = float(Count());
        for (unsigned i = 0; i < Count(); ++i)
        {
            const auto &c = m_capsules[i];
            float t = std::clamp((c.lifetime - c.age) / (c.lifetime * .6f), 0.f, 1.f);
            result[1 + 2 * i] = {c.start[0], c.start[1], c.start[2], c.radius};
            result[2 + 2 * i] = {c.end[0], c.end[1], c.end[2], t * t * (3.f - 2.f * t)};
        }
        return result;
    }

  private:
    std::vector<Capsule> m_capsules;
};
} // namespace avboit
