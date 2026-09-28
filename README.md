# avboit-exp

[![Build and source checks](https://github.com/yangrc1234/avboit-exp/actions/workflows/ci.yml/badge.svg)](https://github.com/yangrc1234/avboit-exp/actions/workflows/ci.yml)

An independent **DirectX 12 / C++17 / HLSL** experiment in Adaptive Voxel-Based
Order-Independent Transparency, extended with frosted glass, screen-space
refraction and VFX distortion.

[Quick start](#quick-start) · [中文说明](README.zh-CN.md) · [Pipeline](docs/PIPELINE.md) ·
[Known limitations](docs/KNOWN_LIMITATIONS.md) · [MIT license](LICENSE.txt)

Based on Michal Drobot's [SIGGRAPH 2025 AVBOIT presentation](https://advances.realtimerendering.com/s2025/content/AVBOIT_SIG2025_MDROBOT-final.pdf).
This is a research implementation, not Activision source code or an official
reference implementation. [Donut](https://github.com/NVIDIA-RTX/Donut) supplies
the application/scene layer; [NVRHI](https://github.com/NVIDIA-RTX/NVRHI) submits GPU work.

| Order-independent transparency | Frosted glass |
|---|---|
| ![Intersecting RGB-transmissive panes, a transparent sphere and smoke](docs/images/oit.png) | ![Frost roughness strips filtering HDR emissive objects](docs/images/frost.png) |
| Intersecting red/cyan panes, a transparent sphere and smoke. Layers are accumulated without sorting draws. | Material-controlled Gaussian blur: varying roughness strips, HDR light spots and a cutout showing the sharp background. |

Both are actual **2560x1440 DX12 captures**, using default packed accumulation
and procedural geometry. Click either image for full size; no external models
are needed.

<details>
<summary>OIT submission-order comparison and screenshot commands</summary>

The geometry, camera and animation time are identical; only transparent draw
submission order changes. Frost and refraction are disabled in this OIT fixture
to make the overlapping layers easier to inspect.

| Original draw order | Reversed draw order |
|---|---|
| ![OIT original draw order](docs/images/oit.png) | ![OIT reversed draw order](docs/images/oit-reversed.png) |

R11G11B10 floating-point blending is not bitwise order independent. In these
captures the mean absolute displayed RGB difference is **0.030/255**, with a
maximum of **3/255**; there is no object-level sorting step.

```powershell
bin/avboit_viewer.exe --headless --stress 5 --no-frost --refraction-gain 0 --emissive-balls --time 0.7 --submission-order 0
bin/avboit_viewer.exe --headless --stress 5 --no-frost --refraction-gain 0 --emissive-balls --time 0.7 --submission-order 1
bin/avboit_viewer.exe --headless --stress 10 --emissive-balls --refraction-gain 0 --frost-mips 3
```

Each invocation writes `native-raster.ppm` and diagnostic readbacks to the current
directory; save the image before running the next command. The README PNGs are
lossless conversions of those images, with no compositing or retouching.

</details>

## What is included

- GPU bounds occupancy, adaptive Z, packed atomic extinction splatting and a
  filtered RGBA8 transmittance LUT. Default volume: 1/8 XY, 128 depth slices.
- Full-resolution four-RT accumulation with RGB transmission and a separately
  accumulated background numerator.
- One selected frost/refraction interface per pixel, a tiled B-prime cache,
  Gaussian mip chain and bilinear/bicubic reconstruction.
- Independent material-driven VFX distortion, single-sided refractive spheres,
  smoke, emissive objects and procedural stress fixtures.
- Live ImGui controls, per-pass GPU timings, occupancy overlays and RenderDoc
  markers with embedded shader debug information.
- CPU oracles, DX12 readback tests and reproducible image comparisons.

Frost width is material-controlled in reference screen pixels. The default
Gaussian chain has three levels; higher quality adds finer levels. The demo
defaults to **2560x1440 and a 120 FPS cap**. It does not implement TAA or MSAA.

## Quick start

Requirements: Windows x64, Visual Studio 2022 with C++ tools and Windows SDK,
CMake 3.18+, Python 3.9+, and a DX12 GPU.

Build and start the default procedural scene:

```powershell
git clone --recursive https://github.com/yangrc1234/avboit-exp.git
cd avboit-exp
python build_native.py
.\bin\avboit_viewer.exe
```

**Sponza is not included in the clone or downloaded by the build.** To use it,
run from the repository root:

```powershell
python tools/fetch_sponza.py
.\bin\avboit_viewer.exe --sponza
```

No rebuild is needed; you can also select **Background → Sponza** in ImGui after
downloading. Sponza's separate license is listed in [third-party notices](THIRD_PARTY_NOTICES.md).

Use **Background** and **Test case** in ImGui to change scenes. WASD/QE moves,
right mouse looks, Home resets, B changes reconstruction, G toggles frost, V
toggles VFX and Space pauses animation. Fixture controls apply immediately.
`bin/avboit_viewer.exe --help` lists the CLI, including headless captures.

## Code map

| Directory | Responsibility |
|---|---|
| `avboit/render` | Pass resources, bindings and command recording; NVRHI only |
| `avboit/shaders` | Per-pass HLSL and shared depth/material/packing contracts |
| `avboit/sample` | Viewer, Donut scene host, fixtures, material submission and UI |
| `avboit/debug` | GPU timings and StablePowerState RAII |
| `avboit/tests` | CPU oracles, GPU readbacks and Python image regressions |
| `tools` | Asset installation, formatting, source checks and runtime packaging |

Start with [TransparencyPipeline](avboit/render/TransparencyPipeline.h) and its
`Record` implementation, then the pass and shader of interest. Each actor/material
section is a separate draw. The [pipeline guide](docs/PIPELINE.md) explains the
resource formats and composition equations; [中文管线](docs/PIPELINE.zh-CN.md)
describes the same implementation.

## Validate and package

```powershell
python build_native.py --tests       # CPU + GPU; requires DX12 hardware
python build_native.py --cpu-tests   # Compile everything; run CPU checks only
python tools/package_runtime.py --output dist/avboit-exp
python tools/check_runtime_package.py --package dist/avboit-exp
```

Optional test executables and their shaders live under `build/tests/bin`, not in
the application bin. Tests run serially and write captures below `build/captures`.
CI builds the Windows app/shaders and runs CPU checks; hardware rendering tests
remain a local validation step. See [validation](docs/VALIDATION.md) and
[contributing](CONTRIBUTING.md) for formatting and extended regressions.

The runtime package contains the viewer, required shaders and licenses. It does
not contain tests, build tools or external assets. Its paths are relocatable.

## Scope and licensing

This is a readable experiment rather than a production renderer. Single-interface
selection, screen-space disocclusion, packed RGB overflow, finite-precision blend
order and Zero-T/totalTau differences are explicit
[limitations](docs/KNOWN_LIMITATIONS.md). Do not interpret passing implementation
tests as proof of physical correctness or performance on lower-end GPUs.

Project code is **MIT**. Inherited NVIDIA copyright notices are retained.
Dependencies and external assets retain their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Research references are linked,
not redistributed as project source.
