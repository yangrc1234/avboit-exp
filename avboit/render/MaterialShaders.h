// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>

namespace avboit
{
// Shader contract for one material. Each actor/material draw selects its own
// entry; sharing compiled shader handles never merges actor draw submissions.
// Passes retain the common vertex/resource/output ABI of this sample renderer.
struct MaterialShaders
{
    nvrhi::ShaderHandle vertex, opaquePixel, extinctionPixel, interfacePixel, accumulationPixel, distortionPixel;
    nvrhi::RasterCullMode cullMode = nvrhi::RasterCullMode::None;
};
} // namespace avboit
