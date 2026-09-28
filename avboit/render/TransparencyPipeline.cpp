// SPDX-License-Identifier: MIT
#include "TransparencyPipeline.h"
#include <stdexcept>
#include <string>

namespace avboit
{
void TransparencyPipeline::Create(nvrhi::IDevice *device, const TransparencyInputs &inputs, const ShaderLoader &shaders)
{
    if (!inputs.width || !inputs.height || (inputs.width & 3u) || (inputs.height & 3u))
        throw std::invalid_argument("Internal render dimensions must be positive multiples of 4");
    auto load = [&](const char *entry, nvrhi::ShaderType type, const char *file = "")
    { return shaders(entry, type, file); };
    ExtinctionInputs volumeInputs;
    m_gaussian.AllocateSchedule(device, inputs.width, inputs.height, inputs.frostMipCount);
    volumeInputs.frostFootprints = m_gaussian.Footprints();
    volumeInputs.frostWorkMask = m_gaussian.WorkMask();
    volumeInputs.frostRectangles = m_gaussian.Rectangles();
    volumeInputs.depthBudget = inputs.depthBudget;
    volumeInputs.screenWidth = inputs.width;
    volumeInputs.screenHeight = inputs.height;
    volumeInputs.width = inputs.volumeWidth;
    volumeInputs.height = inputs.volumeHeight;
    volumeInputs.objectCount = inputs.objectCount;
    volumeInputs.indices = inputs.geometryIndices;
    volumeInputs.vertices = inputs.vertices;
    volumeInputs.bounds = inputs.bounds;
    volumeInputs.camera = inputs.camera;
    volumeInputs.opaqueHardwareDepth = inputs.opaqueHardwareDepth;
    volumeInputs.materials = inputs.materials;
    volumeInputs.vertexRange = inputs.transparentRange;
    volumeInputs.opaqueColorDepth = inputs.opaqueColorDepth;
    volumeInputs.shaders.bounds = load("withFrost", nvrhi::ShaderType::Compute, "shaders/BoundsOccupancy.hlsl");
    if (inputs.depthBudget != 16 && inputs.depthBudget != 32 && inputs.depthBudget != 64 && inputs.depthBudget != 128)
        throw std::invalid_argument("Adaptive depth budget must be 16, 32, 64 or 128");
    const std::string depthEntry = inputs.depthBudget == 128 ? "main" : "budget" + std::to_string(inputs.depthBudget);
    volumeInputs.shaders.adaptiveDepth =
        load(depthEntry.c_str(), nvrhi::ShaderType::Compute,
             inputs.fixedDepth ? "shaders/FixedDepth.hlsl" : "shaders/AdaptiveDepth.hlsl");
    volumeInputs.shaders.physicalMask = load("main", nvrhi::ShaderType::Compute, "shaders/PhysicalOccupancy.hlsl");
    volumeInputs.materialShaders = inputs.materialShaders;
    volumeInputs.shaders.clear = load("clearOccupiedExtinction", nvrhi::ShaderType::Compute);
    volumeInputs.shaders.integrate = load("integrateLut", nvrhi::ShaderType::Compute);
    volumeInputs.shaders.integrateDense = load("integrateDenseLut", nvrhi::ShaderType::Compute);
    m_volume.Create(device, volumeInputs);
    InterfaceInputs interfaceInputs;
    interfaceInputs.indices = inputs.interfaceIndices;
    interfaceInputs.width = inputs.width;
    interfaceInputs.height = inputs.height;
    interfaceInputs.vertices = inputs.vertices;
    interfaceInputs.vertexRange = inputs.transparentRange;
    interfaceInputs.camera = inputs.camera;
    interfaceInputs.opaqueHardwareDepth = inputs.opaqueHardwareDepth;
    interfaceInputs.materials = inputs.materials;
    interfaceInputs.opaqueColorDepth = inputs.opaqueColorDepth;
    interfaceInputs.materialShaders = inputs.materialShaders;
    m_interface.Create(device, interfaceInputs);
    ZeroTransmittanceInputs zero;
    zero.width = inputs.width;
    zero.height = inputs.height;
    zero.opaqueDepth = inputs.opaqueHardwareDepth;
    zero.zeroSlice = m_volume.ZeroSlice();
    zero.camera = inputs.camera;
    zero.warp = m_volume.Warp();
    zero.vertex = load("quadVertex", nvrhi::ShaderType::Vertex, "shaders/ZeroTransmittance.hlsl");
    m_zero.Create(device, zero);
    OitResolveInputs oitInputs;
    oitInputs.screenTiles = m_volume.ScreenTiles();
    oitInputs.width = inputs.width;
    oitInputs.height = inputs.height;
    oitInputs.indices = inputs.geometryIndices;
    oitInputs.halfAccumulation = inputs.halfAccumulation;
    oitInputs.vertices = inputs.vertices;
    oitInputs.vertexRange = inputs.transparentRange;
    oitInputs.camera = inputs.camera;
    oitInputs.opaqueHardwareDepth = inputs.opaqueHardwareDepth;
    oitInputs.materials = inputs.materials;
    oitInputs.warp = m_volume.Warp();
    oitInputs.lut = m_volume.Transmittance();
    oitInputs.opaque = inputs.opaqueColorDepth;
    oitInputs.linear = inputs.linear;
    oitInputs.interfaceDepth = m_interface.HardwareDepth();
    oitInputs.interfaceSurface = m_interface.TransmissionSigma();
    oitInputs.materialShaders = inputs.materialShaders;
    oitInputs.backgroundVertex = load("backgroundVertex", nvrhi::ShaderType::Vertex, "shaders/ResolveTiles.hlsl");
    oitInputs.backgroundPixel = load("backgroundPixel", nvrhi::ShaderType::Pixel, "shaders/OitResolve.hlsl");
    m_resolve.Create(device, oitInputs);
    GaussianInputs gaussianInputs;
    gaussianInputs.mipCount = inputs.frostMipCount;
    gaussianInputs.camera = inputs.camera;
    gaussianInputs.width = inputs.width;
    gaussianInputs.height = inputs.height;
    gaussianInputs.background = m_resolve.Background();
    gaussianInputs.opaque = inputs.opaqueColorDepth;
    gaussianInputs.screenTiles = m_volume.ScreenTiles();
    gaussianInputs.linearSampler = inputs.linear;
    gaussianInputs.sparseShader = load("sparseBase", nvrhi::ShaderType::Compute, "shaders/GaussianBlur.hlsl");
    gaussianInputs.fusedShader = load("fused", nvrhi::ShaderType::Compute, "shaders/GaussianBlur.hlsl");
    m_gaussian.Create(device, gaussianInputs);
    FrostCompositionInputs compositionInputs;
    compositionInputs.sceneColor = inputs.opaqueColorDepth;
    compositionInputs.screenTiles = m_volume.ScreenTiles();
    compositionInputs.warp = m_volume.Warp();
    compositionInputs.lut = m_volume.Transmittance();
    compositionInputs.opaqueDepth = inputs.opaqueHardwareDepth;
    compositionInputs.numerator = m_resolve.Numerator();
    compositionInputs.denominator = m_resolve.Denominator();
    compositionInputs.totalTau = m_resolve.TotalTau();
    compositionInputs.backNumerator = m_resolve.BackNumerator();
    compositionInputs.width = inputs.width;
    compositionInputs.height = inputs.height;
    compositionInputs.interfaceDepth = m_interface.HardwareDepth();
    compositionInputs.interfaceSurface = m_interface.TransmissionSigma();
    compositionInputs.interfaceOffset = m_interface.Offset();
    compositionInputs.backgroundPyramid = m_gaussian.Output();
    compositionInputs.sharpBackground = m_resolve.Background();
    compositionInputs.camera = inputs.camera;
    compositionInputs.materials = inputs.materials;
    compositionInputs.linear = inputs.linear;
    compositionInputs.vertexShader = load("tileVertex", nvrhi::ShaderType::Vertex, "shaders/ResolveTiles.hlsl");
    compositionInputs.pixelShader = load("composePixel", nvrhi::ShaderType::Pixel, "shaders/FrostComposition.hlsl");
    compositionInputs.copyShader = load("copyPixel", nvrhi::ShaderType::Pixel, "shaders/FrostComposition.hlsl");
    m_composition.Create(device, compositionInputs);
    DistortionInputs distortionInputs;
    distortionInputs.width = inputs.fullVfx ? inputs.width : (inputs.width + 1) / 2;
    distortionInputs.height = inputs.fullVfx ? inputs.height : (inputs.height + 1) / 2;
    distortionInputs.vertices = inputs.vertices;
    distortionInputs.camera = inputs.camera;
    distortionInputs.opaqueHardwareDepth = inputs.opaqueHardwareDepth;
    distortionInputs.materials = inputs.materials;
    distortionInputs.depthWarp = m_volume.Warp();
    distortionInputs.transmittance = m_volume.Transmittance();
    distortionInputs.opaqueColorDepth = inputs.opaqueColorDepth;
    distortionInputs.interfaceDepth = m_interface.HardwareDepth();
    distortionInputs.linearSampler = inputs.linear;
    distortionInputs.materialShaders = inputs.materialShaders;
    distortionInputs.batches = inputs.distortionBatches;
    m_distortion.Create(device, distortionInputs);
    DistortionCompositeInputs apply;
    apply.enabled = inputs.applyVfx;
    apply.sceneColor = inputs.opaqueColorDepth;
    apply.offset = m_distortion.Output();
    apply.camera = inputs.camera;
    apply.linear = inputs.linear;
    apply.vertex = load("fullscreenVertex", nvrhi::ShaderType::Vertex);
    apply.pixel = load("applyDistortion", nvrhi::ShaderType::Pixel, "shaders/DistortionComposite.hlsl");
    m_distortionApply.Create(device, apply);
}
void TransparencyPipeline::Record(nvrhi::ICommandList *commands, const TransparencyFrame &frame,
                                  const StageCallback &timing) const
{
    AVBOIT_GPU_SCOPE(commands, "Transparency");
    auto route = [&](Stage stage) -> GpuTimingRoute
    {
        if (!timing)
            return {};
        return [&, stage](bool begin) { timing(stage, begin); };
    };
    auto pass = [&](Stage stage, const char *name, auto &&record)
    {
        AVBOIT_GPU_SCOPE(commands, name, route(stage));
        record();
    };
    pass(Stage::Interface, "Interface prepass", [&] { m_interface.Record(commands, frame.interfaceDraws); });
    pass(Stage::Bounds, "Bounds occupancy / frost masks",
         [&]
         {
             m_gaussian.ResetSchedule(commands, frame.cubic);
             m_volume.RecordBounds(commands);
         });
    pass(Stage::AdaptiveZ, "Adaptive Z", [&] { m_volume.RecordAdaptiveDepth(commands); });
    pass(Stage::PhysicalMask, "Physical occupancy", [&] { m_volume.RecordPhysicalMask(commands); });
    pass(Stage::ExtinctionClear, "Extinction clear",
         [&] { m_volume.RecordClear(commands, frame.denseExtinction, frame.poisonExtinction); });
    pass(Stage::ExtinctionSplat, "Extinction atomic splat",
         [&] { m_volume.RecordSplat(commands, frame.geometryDraws); });
    pass(Stage::Integration, "Transmittance LUT integration",
         [&] { m_volume.RecordIntegration(commands, frame.denseExtinction); });
    pass(Stage::ZeroDepth, "Zero-T depth draw / VS tile rejection",
         [&] { m_zero.RecordDepth(commands, frame.zeroDepth); });
    pass(Stage::Accumulation, "OIT accumulation (4 MRT)",
         [&] { m_resolve.RecordAccumulation(commands, frame.geometryDraws); });
    pass(Stage::Resolve, "B prime / transparency tiles with 1px border",
         [&] { m_resolve.RecordResolve(commands, frame.poisonResolve); });
    m_gaussian.RecordBlur(commands, frame.lastBlurMip, frame.poisonResolve, route(Stage::BlurBase), route(Stage::Blur));
    pass(Stage::Composition, "Resolve temporary color / tile copy back",
         [&] { m_composition.Record(commands, frame.poisonResolve); });
    pass(Stage::Vfx, "VFX distortion offset", [&] { m_distortion.Record(commands, frame.distortionBatches); });
    pass(Stage::VfxApply, "VFX distortion apply (HDR)", [&] { m_distortionApply.Record(commands); });
}
} // namespace avboit
