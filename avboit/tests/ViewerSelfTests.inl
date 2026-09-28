// SPDX-License-Identifier: MIT
// UI/resource integration tests; deliberately separate from frame rendering.
// Included after the NativeViewer definition in sample/NativeViewer.cpp.
void NativeViewer::TestUiInteractions(avboit::ViewerUi &ui, nvrhi::IFramebuffer *framebuffer)
{
    auto pump = [&]()
    {
        Render(framebuffer);
        GetDevice()->waitForIdle();
        ui.Animate(1.f / 60.f);
        ui.Render(framebuffer);
    };
    auto click = [&](const ImVec2 &position)
    {
        // Controls can move below the viewport as quality options grow.
        if (position.y > ImGui::GetIO().DisplaySize.y - 70 || position.y < 65)
        {
            uiScrollRequest = position.y - ImGui::GetIO().DisplaySize.y * .5f;
            pump();
            pump();
        }
        auto &io = ImGui::GetIO();
        io.AddMousePosEvent(position.x, position.y);
        pump();
        io.AddMouseButtonEvent(0, true);
        pump();
        io.AddMouseButtonEvent(0, false);
        pump();
        pump();
    };
    auto checkCompactResources = [&]()
    {
        if (transparency.Zero().Depth() != depth.Get())
            throw std::runtime_error("Zero-T allocated separate scene depth");
        const auto &apply = transparency.DistortionApply();
        if (apply.Enabled() != HasVfxDraws() ||
            (!HasVfxDraws() && apply.Output() != transparency.Composition().Output()) ||
            apply.Output()->getDesc().format != transparency.Composition().Output()->getDesc().format)
            throw std::runtime_error("VFX HDR composition allocation mismatch");
        const auto chain = avboit::FrostChain::Make(Width, Height, frostMipCount);
        const auto &pyramid = transparency.Gaussian().Output()->getDesc();
        if (transparency.Resolve().Background() == transparency.Gaussian().Output() ||
            transparency.Resolve().Background()->getDesc().mipLevels != 1 || pyramid.mipLevels != frostMipCount ||
            pyramid.width != chain.width || pyramid.height != chain.height)
            throw std::runtime_error("Fixed-screen frost/sharp source allocation mismatch");
        const auto &vfxDesc = transparency.Distortion().Output()->getDesc();
        if (vfxDesc.width != (fullVfx ? Width : (Width + 1) / 2) ||
            vfxDesc.height != (fullVfx ? Height : (Height + 1) / 2))
            throw std::runtime_error("VFX resolution mismatch");
        if (transparency.Composition().Output()->getDesc().format !=
            ((halfComposition || !hardwareDepth) ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::R11G11B10_FLOAT))
            throw std::runtime_error("Composition precision mismatch");
    };
    bool initialCubic = cubic, initialFrost = frost, initialSmoke = showSmoke;
    unsigned initialObjects = objectCount;
    unsigned initialScale = volumeScale;
    Render(framebuffer);
    Capture(framebuffer->getDesc().colorAttachments[0].texture, "quality-before.ppm");
    const unsigned initialWidth = Width, initialHeight = Height;
    const nvrhi::BufferHandle initialVertices = vertexBuffer, initialMaterials = materialBuffer,
                              initialBounds = boundsBuffer, initialCamera = cameraBuffer;
    const auto initialMaterialEntries = materials.entries.size();
    const bool initialFollow = followWindow;
    const unsigned originalWindowWidth = windowWidth, originalWindowHeight = windowHeight;
    followWindow = true;
    for (const auto size : std::array<std::array<unsigned, 2>, 3>{{{777, 433}, {319, 257}, {1001, 563}}})
    {
        BackBufferResizing();
        BackBufferResized(size[0], size[1], 1);
        pump();
        checkCompactResources();
        const auto &target = transparency.Composition().Output()->getDesc();
        if (target.width != avboit::AlignRenderDimension(size[0]) ||
            target.height != avboit::AlignRenderDimension(size[1]))
            throw std::runtime_error("Window-follow resource size mismatch");
    }
    BackBufferResized(0, 0, 1);
    pump();
    if (Width != 1004 || Height != 564)
        throw std::runtime_error("Minimize changed render size");
    BackBufferResized(4096, 2304, 1);
    pump();
    if (Width != 2048 || Height != 1152)
        throw std::runtime_error("Window-follow cap changed aspect ratio");
    followWindow = false;
    BackBufferResized(701, 401, 1);
    pump();
    if (Width != 2048 || Height != 1152)
        throw std::runtime_error("Fixed resolution followed window");
    windowWidth = originalWindowWidth;
    windowHeight = originalWindowHeight;
    followWindow = initialFollow;
    for (const auto size :
         std::array<std::array<unsigned, 2>, 3>{{{513, 321}, {959, 575}, {initialWidth, initialHeight}}})
    {
        SetRenderSize(size[0], size[1]);
        rebuildRequested = true;
        preserveMaterialsOnRebuild = true;
        pump();
        const auto &image = transparency.Composition().Output()->getDesc();
        const auto &pyramid = transparency.Gaussian().Output()->getDesc();
        unsigned tiles =
            ((Width + volumeScale * 8 - 1) / (volumeScale * 8)) * ((Height + volumeScale * 8 - 1) / (volumeScale * 8));
        if (image.width != Width || image.height != Height ||
            pyramid.width != avboit::FrostChain::Make(Width, Height, frostMipCount).width ||
            pyramid.height != avboit::FrostChain::Make(Width, Height, frostMipCount).height ||
            masks->getDesc().byteSize != (2048 + tiles * 128) * 4)
            throw std::runtime_error("Live internal resolution rebuild failed");
    }

    const bool originalFullVfx = fullVfx, originalHalfComposition = halfComposition;
    const bool originalVfx = vfx;
    const unsigned originalLayers = vfxLayers;
    for (bool enabled : {false, true})
        for (unsigned layers : {0u, 1u, 2u, 3u})
        {
            vfx = enabled;
            vfxLayers = layers;
            pump();
            checkCompactResources();
        }
    vfx = originalVfx;
    vfxLayers = originalLayers;
    pump();
    checkCompactResources();
    for (bool value : {true, false})
    {
        fullVfx = value;
        halfComposition = value;
        rebuildRequested = true;
        preserveMaterialsOnRebuild = true;
        pump();
        checkCompactResources();
    }
    fullVfx = originalFullVfx;
    halfComposition = originalHalfComposition;
    rebuildRequested = true;
    preserveMaterialsOnRebuild = true;
    pump();
    checkCompactResources();
    const unsigned originalMipCount = frostMipCount;
    const float originalSigma = maxFrostSigma;
    for (float sigma : {0.f, 6.f, 12.f, 24.f, originalSigma})
    {
        maxFrostSigma = sigma;
        pump();
        const auto chain = avboit::FrostChain::Make(Width, Height, frostMipCount);
        if (maxBlurMip >= chain.mipCount || camera[4][0] > sigma || (sigma == 0 && camera[4][0] != 0))
            throw std::runtime_error("Live frost sigma/mip selection mismatch");
    }
    maxFrostSigma = originalSigma;
    pump();
    click(uiMipHigh);
    if (frostMipCount != 5)
        throw std::runtime_error("ImGui mip slider high endpoint failed");
    click(uiMipLow);
    if (frostMipCount != 3)
        throw std::runtime_error("ImGui mip slider low endpoint failed");
    for (unsigned count : {5u, 4u, 3u, originalMipCount})
    {
        frostMipCount = count;
        rebuildRequested = true;
        preserveMaterialsOnRebuild = true;
        pump();
        checkCompactResources();
    }
    auto stableTargets = [&]()
    {
        return std::vector<nvrhi::TextureHandle>{transparency.Interface().TransmissionSigma(),
                                                 transparency.Interface().Offset(),
                                                 transparency.Interface().HardwareDepth(),
                                                 transparency.Gaussian().Output(),
                                                 transparency.Composition().Output(),
                                                 transparency.Composition().Temporary(),
                                                 transparency.Distortion().Output()};
    };
    const auto targetsBeforeVolumeChange = stableTargets();
    const bool originalFixed = fixedDepth;
    for (bool fixed : {true, false, originalFixed})
    {
        fixedDepth = fixed;
        rebuildRequested = true;
        preserveMaterialsOnRebuild = true;
        pump();
    }
    const unsigned originalBudget = depthBudget;
    for (unsigned budget : {16u, 32u, 64u, 128u, originalBudget})
    {
        depthBudget = budget;
        rebuildRequested = true;
        preserveMaterialsOnRebuild = true;
        pump();
        const auto &volume = transparency.Volume();
        if (volume.Transmittance()->getDesc().depth != budget ||
            volume.PackedExtinction()->getDesc().byteSize !=
                uint64_t(volumeWidth) * volumeHeight * (budget / 4 + budget) * 4)
            throw std::runtime_error("Depth budget resource allocation mismatch");
    }
    for (unsigned scale : {2u, 8u, initialScale})
    {
        click(uiVolume);
        click(uiVolumeChoices[scale == 2 ? 0 : 1]);
        if (volumeScale != scale || lut->getDesc().width != (Width + scale - 1) / scale ||
            physicalMasks->getDesc().byteSize !=
                ((Width + scale * 8 - 1) / (scale * 8)) * ((Height + scale * 8 - 1) / (scale * 8)) * 8 * 4)
            throw std::runtime_error("ImGui extinction resolution rebuild failed");
    }
    const auto targetsAfterVolumeChange = stableTargets();
    for (size_t i = 0; i < targetsBeforeVolumeChange.size(); ++i)
        if (targetsBeforeVolumeChange[i].Get() != targetsAfterVolumeChange[i].Get())
            throw std::runtime_error("Volume quality reallocated unrelated full-resolution targets");
    if (vertexBuffer.Get() != initialVertices.Get() || materialBuffer.Get() != initialMaterials.Get() ||
        boundsBuffer.Get() != initialBounds.Get() || cameraBuffer.Get() != initialCamera.Get() ||
        materials.entries.size() != initialMaterialEntries)
        throw std::runtime_error("Quality changes rebuilt scene buffers");
    Render(framebuffer);
    Capture(framebuffer->getDesc().colorAttachments[0].texture, "quality-after.ppm");
    {
        std::ifstream before("quality-before.ppm", std::ios::binary), after("quality-after.ppm", std::ios::binary);
        const std::string a((std::istreambuf_iterator<char>(before)), std::istreambuf_iterator<char>());
        const std::string b((std::istreambuf_iterator<char>(after)), std::istreambuf_iterator<char>());
        if (a.empty() || a != b)
            throw std::runtime_error("Quality round trip changed the restored image");
    }
    click(uiCubic);
    if (cubic == initialCubic)
        throw std::runtime_error("ImGui cubic toggle failed");
    bool initialSparse = sparseBlur;
    click(uiSparse);
    if (sparseBlur == initialSparse)
        throw std::runtime_error("ImGui sparse blur toggle failed");
    click(uiSparse);
    if (sparseBlur != initialSparse)
        throw std::runtime_error("ImGui dense blur restore failed");
    click(uiFrost);
    if (frost == initialFrost)
        throw std::runtime_error("ImGui frost toggle failed");
    click(uiSmoke);
    if (showSmoke == initialSmoke || int(objectCount) != int(initialObjects) + (initialSmoke ? -8 : 8))
        throw std::runtime_error("ImGui fixture change did not apply immediately");
    click(uiProfile);
    if (profiler.ActiveStages() != 0)
        throw std::runtime_error("ImGui profiling disable failed");
    click(uiProfile);
    if (profiler.ActiveStages() == 0)
        throw std::runtime_error("ImGui profiling enable failed");
    click(uiFrost);
    if (frost != initialFrost)
        throw std::runtime_error("ImGui frost restore failed");
    const int originalOverlay = occupancyOverlay, originalSlice = occupancySlice;
    const auto overlayTargets = stableTargets();
    uiScrollRequest = -100000;
    pump();
    pump();
    for (int mode : {1, 2, 3, 4, 5, 6, 7, 0, originalOverlay})
    {
        click(uiOccupancy);
        click(uiOccupancyChoices[mode]);
        if (occupancyOverlay != mode || rebuildRequested)
            throw std::runtime_error("Live occupancy overlay switch failed");
        const auto currentTargets = stableTargets();
        for (size_t i = 0; i < overlayTargets.size(); ++i)
            if (overlayTargets[i].Get() != currentTargets[i].Get())
                throw std::runtime_error("Debug overlay recreated rendering targets");
        if (mode == 7)
        {
            occupancySlice = int(depthBudget) - 1;
            pump();
            occupancySlice = originalSlice;
        }
    }
    TestMaterialEdits(framebuffer);
    materialEditor = true;
    pump();
    materialEditor = false;
    bool originalAccum = halfAccumulation;
    auto materialIndex = objectMaterials.front();
    auto originalMaterial = materials.entries[materialIndex];
    materials.entries[materialIndex].gain = .37f;
    materials.entries[materialIndex].inheritOptics = false;
    const auto targetsBeforePrecision = stableTargets();
    const nvrhi::TextureHandle lutBeforePrecision = transparency.Volume().Transmittance();
    const nvrhi::BufferHandle extinctionBeforePrecision = transparency.Volume().PackedExtinction();
    for (bool half : {true, false})
    {
        halfAccumulation = half;
        rebuildRequested = true;
        preserveMaterialsOnRebuild = true;
        pump();
        const auto targetsAfterPrecision = stableTargets();
        for (size_t i = 0; i < targetsBeforePrecision.size(); ++i)
            if (targetsBeforePrecision[i].Get() != targetsAfterPrecision[i].Get())
                throw std::runtime_error("RT format change reallocated unrelated targets");
        if (lutBeforePrecision.Get() != transparency.Volume().Transmittance() ||
            extinctionBeforePrecision.Get() != transparency.Volume().PackedExtinction())
            throw std::runtime_error("RT format change reallocated extinction volume");
        auto format = half ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::R11G11B10_FLOAT;
        if (numerator->getDesc().format != format || backNumerator->getDesc().format != format)
            throw std::runtime_error("Precision reference rebuild selected incorrect RT formats");
        if (materials.entries[materialIndex].gain != .37f || materials.entries[materialIndex].inheritOptics)
            throw std::runtime_error("Precision reference rebuild lost material edits");
    }
    materials.entries[materialIndex] = originalMaterial;
    halfAccumulation = originalAccum;
    rebuildRequested = true;
    preserveMaterialsOnRebuild = true;
    pump();
    // Exercise newly exposed runtime controls without restarting the device.
    const unsigned savedPreset = cameraPreset;
    for (unsigned preset = 0; preset < 7; ++preset)
    {
        ApplyCameraPreset(preset);
        pump();
    }
    ApplyCameraPreset(savedPreset);
    const bool savedDepth = hardwareDepth;
    if (!useDonutScene)
        for (bool value : {true, false, savedDepth})
        {
            hardwareDepth = value;
            rebuildRequested = true;
            preserveMaterialsOnRebuild = true;
            pump();
            if (bool(camera[4][3]) != value)
                throw std::runtime_error("Live depth source mismatch");
        }
    // Scene changes intentionally rebuild geometry; test them after the resource
    // identity and image round-trip checks for quality-only changes.
    const auto savedStress = stressPreset;
    const bool savedSphere = glassSphere, savedRgb = rgbGlass, savedBalls = emissiveBalls, savedSmoke = showSmoke;
    const bool savedBoard = calibrationBoard, savedPanes = sceneGlassPanes, savedVfx = vfx, savedAnimate = animate,
               savedBehind = vfxBehind, savedFront = emissiveFront;
    const auto savedVfxMode = vfxMode, savedVfxLayers = vfxLayers;
    const float savedRough = sphereRoughness, savedTime = time;
    uiScrollRequest = -100000;
    pump();
    pump();
    for (unsigned choice : {15u, 16u, 17u, 18u})
    {
        click(uiDemoCase);
        click(uiDemoChoices[choice]);
        if (CurrentDemoCase() != choice || rebuildRequested)
            throw std::runtime_error("ImGui scene preset did not apply immediately");
    }
    stressPreset = savedStress;
    glassSphere = savedSphere;
    rgbGlass = savedRgb;
    emissiveBalls = savedBalls;
    showSmoke = savedSmoke;
    calibrationBoard = savedBoard;
    sceneGlassPanes = savedPanes;
    vfx = savedVfx;
    animate = savedAnimate;
    vfxBehind = savedBehind;
    emissiveFront = savedFront;
    vfxMode = savedVfxMode;
    vfxLayers = savedVfxLayers;
    sphereRoughness = savedRough;
    time = savedTime;
    ApplyCameraPreset(savedPreset);
    rebuildRequested = true;
    preserveMaterialsOnRebuild = false;
    pump();
    const auto savedPath = scenePathUi;
    const int savedSource = useSponza ? (useDonutScene ? 1 : 2) : 0;
    pendingSceneMode = 1;
    snprintf(scenePathUi.data(), scenePathUi.size(), "missing-scene-for-runtime-test.gltf");
    sceneSourceRequested = true;
    pump();
    if (runtimeError.empty() || int(useSponza ? (useDonutScene ? 1 : 2) : 0) != savedSource)
        throw std::runtime_error("Missing scene replaced active scene");
    scenePathUi = savedPath;
    runtimeError.clear();
    auto defaultScene =
        app::GetDirectoryWithExecutable().parent_path() / "assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf";
    if (std::filesystem::is_regular_file(defaultScene))
    {
        scenePathUi.fill(0);
        for (int mode : {1, 2, 0})
        {
            click(uiBackground);
            click(uiBackgroundChoices[mode]);
            if (!runtimeError.empty() || useSponza != (mode != 0) || bool(donutScene) != (mode == 1))
                throw std::runtime_error("Runtime scene source switch failed: " + runtimeError);
        }
    }
    scenePathUi = savedPath;
    pendingSceneMode = savedSource;
    sceneSourceRequested = true;
    pump();
    if (!runtimeError.empty())
        throw std::runtime_error(runtimeError);
    const auto savedCsv = csvPathUi;
    const bool savedAnimation = animate, savedOrderAnimation = animateOrder;
    snprintf(csvPathUi.data(), csvPathUi.size(), "live-benchmark-test.csv");
    interactiveBenchmarkFrames = 2;
    benchmarkRequested = true;
    pump();
    if (benchmarkRequested || animate != savedAnimation || animateOrder != savedOrderAnimation ||
        !std::filesystem::is_regular_file("live-benchmark-test.csv"))
        throw std::runtime_error("Interactive benchmark failed");
    csvPathUi = savedCsv;
    interactiveBenchmarkFrames = 180;
    printf("ImGui input/rebuild/profiling interaction tests PASS\n");
}
void NativeViewer::TestMaterialEdits(nvrhi::IFramebuffer *framebuffer)
{
    auto saved = materials.entries;
    auto readFile = [](const char *path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(file), {});
    };
    Render(framebuffer);
    Capture(framebuffer->getDesc().colorAttachments[0].texture, "material-before.ppm");
    auto before = readFile("material-before.ppm");
    for (auto &material : materials.entries)
    {
        if (material.kind == 1)
            material.transmission = {.1f, .45f, .8f, 0};
        if (material.kind == 2 || material.kind == 4)
        {
            material.transmission = {.22f, .58f, .91f, .7f};
            material.inheritOptics = false;
            material.ior = 1.7f;
            material.gain = .5f;
        }
    }
    bool oldDense = denseExtinction, oldPoison = poisonExtinction, oldFull = fullBlur;
    std::vector<char> referenceImage, referenceLut;
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        denseExtinction = mode == 0;
        fullBlur = mode == 0;
        poisonExtinction = mode == 2;
        Render(framebuffer);
        Capture(framebuffer->getDesc().colorAttachments[0].texture, "material-after.ppm");
        auto image = readFile("material-after.ppm"), lutBytes = readFile("native-lut.rgba8");
        if (mode == 0)
        {
            referenceImage = image;
            referenceLut = lutBytes;
        }
        else if (image != referenceImage || lutBytes != referenceLut)
            throw std::runtime_error("Edited material sparse/full reference mismatch");
    }
    if (before == referenceImage)
        throw std::runtime_error("Material edits did not change the rendered image");
    materials.entries = std::move(saved);
    denseExtinction = oldDense;
    poisonExtinction = oldPoison;
    fullBlur = oldFull;
    Render(framebuffer);
    printf("Live material edits, colored smoke and conservative sparse work checks PASS\n");
}
