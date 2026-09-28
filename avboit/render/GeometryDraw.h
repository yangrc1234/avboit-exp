// SPDX-License-Identifier: MIT
#pragma once

namespace avboit
{
// One actor/material section. Indices are relative to the vertex SRV bound by
// the pass. Material identity selects its shaders and raster state.
struct GeometryDraw
{
    unsigned actor = 0, material = 0, firstIndex = 0, indexCount = 0;
};
} // namespace avboit
