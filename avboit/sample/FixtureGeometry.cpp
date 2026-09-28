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
} // namespace avboit
