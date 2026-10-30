// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <string>

namespace avboit
{
using ActorVector = std::array<float, 3>;
using ActorBasis = std::array<ActorVector, 3>;
// Column basis, right-handed Euler degrees: Rz * Ry * Rx. Geometry is always
// regenerated from its rest pose, so repeated edits do not accumulate drift.
inline ActorBasis RotationBasis(ActorVector degrees)
{
    constexpr float radians = 3.14159265359f / 180.f;
    float x = degrees[0] * radians, y = degrees[1] * radians, z = degrees[2] * radians;
    float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
    return {{{cz * cy, sz * cy, -sy},
             {cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx},
             {cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx}}};
}
inline ActorVector Rotate(const ActorBasis &basis, ActorVector v)
{
    ActorVector r{};
    for (unsigned a = 0; a < 3; ++a)
        for (unsigned b = 0; b < 3; ++b)
            r[a] += basis[b][a] * v[b];
    return r;
}
inline ActorVector DirectionRotation(ActorVector v)
{
    constexpr float degrees = 180.f / 3.14159265359f;
    return {-std::atan2(v[1], std::hypot(v[0], v[2])) * degrees, std::atan2(v[0], v[2]) * degrees, 0};
}
inline ActorVector RotateWorld(ActorVector rotation, unsigned axis, float degrees)
{
    ActorVector delta{};
    delta[axis] = degrees;
    auto world = RotationBasis(delta), b = RotationBasis(rotation);
    for (auto &column : b)
        column = Rotate(world, column);
    constexpr float toDegrees = 180.f / 3.14159265359f;
    float y = std::asin(std::clamp(-b[0][2], -1.f, 1.f));
    if (std::abs(std::cos(y)) > 1e-5f)
        return {std::atan2(b[1][2], b[2][2]) * toDegrees, y * toDegrees, std::atan2(b[0][1], b[0][0]) * toDegrees};
    return {std::atan2(-b[2][1], b[1][1]) * toDegrees, y * toDegrees, 0};
}
enum class ActorType
{
    Transparent,
    Opaque,
    Distortion,
    Sun,
    Spotlight
};
struct SceneActor
{
    ActorType type = ActorType::Transparent;
    std::string name;
    unsigned firstRecord = 0, endRecord = 0, renderIndex = 0, material = 0;
    ActorVector position{}, rotation{}, homePosition{}, homeRotation{}, lower{}, upper{};
    bool enabled = true, animate = false;
    float lightIntensity = 0;
};
} // namespace avboit
