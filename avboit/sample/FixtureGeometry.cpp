// SPDX-License-Identifier: MIT
#include "sample/FixtureGeometry.h"
#include <cmath>

namespace avboit
{
static void AppendSphere(std::vector<FixtureRecord> &vertices, const std::array<float, 3> &center, float radius,
                         const std::array<float, 3> &radiance, const std::array<float, 3> &transmission, unsigned kind,
                         float roughness)
{
    const unsigned LongitudeSegments = kind == 4 ? 40 : 24, LatitudeSegments = kind == 4 ? 24 : 16;
    constexpr float Pi = 3.14159265358979323846f;
    auto emit = [&](unsigned longitude, unsigned latitude)
    {
        const float phi = 2.f * Pi * float(longitude) / LongitudeSegments;
        const float theta = Pi * float(latitude) / LatitudeSegments;
        const std::array<float, 3> normal = {std::sin(theta) * std::cos(phi), std::cos(theta),
                                             std::sin(theta) * std::sin(phi)};
        vertices.push_back({center[0] + radius * normal[0], center[1] + radius * normal[1],
                            center[2] + radius * normal[2], float(kind)});
        vertices.push_back({radiance[0], radiance[1], radiance[2], 1});
        vertices.push_back({transmission[0], transmission[1], transmission[2], roughness});
        vertices.push_back({normal[0], normal[1], normal[2], radius});
    };
    // Avoid zero-area triangles at the poles. All remaining triangles are real
    // opaque geometry; depth and silhouettes come from rasterization.
    for (unsigned y = 0; y < LatitudeSegments; ++y)
        for (unsigned x = 0; x < LongitudeSegments; ++x)
        {
            if (y > 0)
            {
                emit(x, y);
                emit(x + 1, y);
                emit(x, y + 1);
            }
            if (y + 1 < LatitudeSegments)
            {
                emit(x + 1, y);
                emit(x + 1, y + 1);
                emit(x, y + 1);
            }
        }
}
void AppendEmissiveSphere(std::vector<FixtureRecord> &vertices, const std::array<float, 3> &center, float radius,
                          const std::array<float, 3> &radiance)
{
    AppendSphere(vertices, center, radius, radiance, {0, 0, 0}, 0, 0);
}
void AppendGlassSphere(std::vector<FixtureRecord> &vertices, const std::array<float, 3> &center, float radius,
                       const std::array<float, 3> &transmission, float roughness)
{
    AppendSphere(vertices, center, radius, {0, 0, 0}, transmission, 4, roughness);
}
void AppendLayeredModel(std::vector<FixtureRecord> &vertices, const std::array<float, 3> &center, float shellGap,
                        bool outer)
{
    using V3 = std::array<float, 3>;
    constexpr unsigned longitudeCount = 96, latitudeCount = 64;
    constexpr float pi = 3.14159265358979323846f;
    auto surface = [](float phi, float theta) -> V3
    {
        float s = std::sin(theta);
        float r = .72f * (1.f + .17f * std::cos(5.f * phi) * s * s + .09f * std::cos(6.f * theta));
        return {r * s * std::cos(phi), 1.35f * r * std::cos(theta), r * s * std::sin(phi)};
    };
    auto emit = [&](unsigned x, unsigned y)
    {
        float phi = 2.f * pi * (x % longitudeCount) / longitudeCount, theta = pi * y / latitudeCount;
        auto p = surface(phi, theta);
        V3 n = {0, y == 0 ? 1.f : -1.f, 0};
        if (y != 0 && y != latitudeCount)
        {
            auto a = surface(phi + .001f, theta), b = surface(phi - .001f, theta);
            auto c = surface(phi, theta + .001f), d = surface(phi, theta - .001f);
            for (unsigned k = 0; k < 3; ++k)
            {
                a[k] -= b[k];
                c[k] -= d[k];
            }
            n = {a[1] * c[2] - a[2] * c[1], a[2] * c[0] - a[0] * c[2], a[0] * c[1] - a[1] * c[0]};
            float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (float &v : n)
                v /= length;
        }
        float gap = outer ? shellGap : 0.f;
        vertices.push_back(
            {center[0] + p[0] + gap * n[0], center[1] + p[1] + gap * n[1], center[2] + p[2] + gap * n[2], 2});
        vertices.push_back(outer ? FixtureRecord{.035f, .65f, .95f, 1} : FixtureRecord{1.f, .22f, .035f, .88f});
        vertices.push_back({0, 0, 0, 0});
        vertices.push_back({n[0], n[1], n[2], -2});
    };
    for (unsigned y = 0; y < latitudeCount; ++y)
        for (unsigned x = 0; x < longitudeCount; ++x)
        {
            if (y > 0)
            {
                emit(x, y);
                emit(x + 1, y);
                emit(x, y + 1);
            }
            if (y + 1 < latitudeCount)
            {
                emit(x + 1, y);
                emit(x + 1, y + 1);
                emit(x, y + 1);
            }
        }
}
} // namespace avboit
