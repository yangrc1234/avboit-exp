# Validation

## Reproduce

```powershell
python build_native.py --tests
```

This builds the viewer, optional test executables and both shader manifests, then
runs CTest serially. The default test rendering resolution is 2560x1440. Smaller
and rounded dimension cases exercise allocation/viewport handling, not quality.
No stable-clock performance claim is made by an ordinary correctness run.

`--cpu-tests` runs the CPU-labeled tests only. The Windows CI builds C++ and all
HLSL and runs this subset; GitHub-hosted runners do not validate the DX12 images.
The optional Sponza tests are registered only if the asset is installed at configure time.

CPU checks cover CLI validation, material tables, camera transforms, submission
order, interface selection and the hash-checked asset installer. GPU checks cover
adaptive/fixed mapping, packed atomics, occupancy, base-mask bounds and the
interactive viewer's live resource/material controls.

`mrt_output_gpu` verifies undeclared pixel-shader MRT outputs with native DX12
PSOs, all write masks enabled, and bitwise readback at 2560x1440. It covers packed
RGB and RGBA16F, replacement and additive blending. See the
[MRT output test](MRT_OUTPUT_TEST.zh-CN.md) for optional stable-clock timing.

`mrt_write_mask_gpu` exercises the same native harness with independent ALU loops
and dependent texture samples used only by Target1. Masked/live PSOs share shader
bytecode; the stripped reference declares only Target0. It verifies live output
and bitwise preservation of disabled targets. Performance remains an explicit
`--shader-work --benchmark` run, not a timing assertion in CTest.

## Extended readback checks

Install `requirements-dev.txt` into a virtual environment. Useful targeted checks:

```powershell
.venv/Scripts/python avboit/tests/check_single_sided_sphere.py
.venv/Scripts/python avboit/tests/check_tile_pipeline.py
.venv/Scripts/python avboit/tests/check_frost_chain.py
.venv/Scripts/python avboit/tests/check_extinction_roi.py
.venv/Scripts/python avboit/tests/check_blur_roi.py
.venv/Scripts/python avboit/tests/check_background_protection.py
.venv/Scripts/python avboit/tests/check_frost_interface.py
.venv/Scripts/python avboit/tests/check_prewarped_background.py
.venv/Scripts/python avboit/tests/check_vfx_materials.py
.venv/Scripts/python avboit/tests/check_zero_depth.py
.venv/Scripts/python avboit/tests/check_hardware_depth.py
.venv/Scripts/python avboit/tests/check_visibility.py
```

Most scripts accept `--viewer` for a non-default executable; some fixed fixtures
use `bin/avboit_viewer.exe`. Check the script before selecting a fixture or output directory. Dense, poisoned and ROI references
are useful for detecting unwritten-source reads, not just visible artifacts.

`check_prewarped_background.py` checks refraction applied during B generation at
2K: RGB glass with foreground smoke, large offsets, frost, dense five-level
Gaussian, cutouts, VFX and textured frost at the maximum post-blur radius with
both cubic and bilinear reconstruction. Poisoned/full-domain captures must match the scheduled
result exactly. Source positions outside B coverage are checked against opaque
color for sharp refraction, and against zero validity for uncovered frost sources.

`check_zero_depth.py` verifies conservative depth writes and RGB-zero filtered
footprints, then checks that closed ordinary rays resolve to `N/A` in both B and
the final HDR output. A thin bright foreground bar exercises the difference
between sampling the LUT at SceneDepth and incorrectly reading its far endpoint.
It also covers RGB nonzero channels, frost validity/mirror repair, adaptive/fixed
Z budgets and dense/poisoned references. Indirect candidate IDs/counts are checked
for missing or duplicate tiles, partial screen/volume edges, and stale work after
switching from an occupied view to an empty frame.

`check_hardware_depth.py` compares Reversed-Z D32 reconstruction against an
independent linear-depth output over six Sponza views. The frost interface check
also validates its known plane depth and zero clear value; visibility checks
cover empty-sky scheduling and rejection of bright foreground sources.

`check_submission_order.py` reports finite-precision order differences rather than
claiming bitwise order independence. The packed RGB overflow stress in the GPU
oracle intentionally documents the carry limitation. See
[known limitations](KNOWN_LIMITATIONS.md) before interpreting test results.

## Crossing-glass quality diagnostic

Choose **90-degree crossing glass / LUT self-shadow** in ImGui, or run
`bin/avboit_viewer.exe --stress 17 --camera-preset 8`. Both panes have constant
material values, orthogonal tangents and separate actor draws. Camera presets
0/7/8/9 orbit the intersection at 0/+30/+40/-30 degrees.

```powershell
.venv/Scripts/python avboit/tests/check_crossing_glass.py
.venv/Scripts/python avboit/tests/check_crossing_glass.py --reuse --require-clean
```

The first command records 2K HDR captures with slope splatting on/off, an exact
sorted RGB-over reference, `build/crossing-glass/comparison.png` and a JSON report.
It checks the actual LUT against GPU N/A, then sweeps query bias offline.
Silhouettes and the true crossing are excluded from stripe measurements; crossing
error is reported separately. The third comparison panel is the sharp reference,
not a multilayer frost oracle. Reversed draw order, dense integration and poisoned
extinction must match; finer XY coverage is a separate quality control.

**Current quality result: improved, not exact.** At the +40-degree camera, one pane
is 85 degrees from face-on. A LUT cell spans up to about 10.4 virtual slices.
Slope-aware splatting restores the nearest pane's minimum T from 0.538 to 1.0.
With RGBA16F accumulation and no frost, interior HDR RMS falls from 0.01236 to
0.00231 and the maximum adjacent step falls from 0.07056 to 0.00745. However,
crossing-band RMS increases from 0.02268 to 0.02805: delaying extinction also delays
real foreground attenuation. The +/-30-degree views pass the interior step gate;
the most grazing view still fails. `--require-clean` intentionally exits with
status 1 until that remaining error is resolved. The CTest scene is a runtime
smoke test and does not certify its visual quality.

The bounded-push version retains the previous slope-corrected quality metrics.
Across camera presets 0/7/8/9, the reserve changes occupied physical slices from
72/98/101/98 to 88/114/117/114. Accepted virtual resolution stays at 512 in these
captures; more crowded scenes can still force a coarser mapping. These are work
counts, not GPU timing measurements.

Splat and bounds occupancy share `SlopeSplatGuardSlices` in `DepthMapping.hlsli`:
64 cells of the original 2048-cell log-Z grid, or 1/32 of normalized log depth.
The splat caps its slope-derived shift in that coordinate before adaptive mapping.
Bounds occupancy extends the upper Z endpoint by the same 64 cells, for both the
global warp input and per-tile scalar/RGB masks. The physical mask remaps the
expanded interval normally. There is no per-draw depth constant, extra RT/pass or
query-side slope reconstruction. The comparison toggle disables both push and
reserve. This guard is separate from the existing interpolation/query guards.

The bounds GPU oracle checks both toggle states, far-plane clamping, off-screen
rejection and empty frames, including the resulting adaptive warp and physical
masks. End-to-end dense/poisoned comparisons check that shifted writes are cleared
and integrated. The JSON quality report records accepted virtual resolution and
occupied physical slices so any precision loss from the reserve remains visible.

## Source and runtime checks

```powershell
.venv/Scripts/python tools/format_sources.py --check
python tools/check_repository.py
python tools/package_runtime.py --output dist/avboit-exp
python tools/check_runtime_package.py --package dist/avboit-exp
```

Packaging copies only the viewer, current runtime shader entries, required Donut
shaders and license/documentation files. It writes a SHA-256 manifest. The relocation
check runs the package from an unrelated working directory and compares its images
and LUTs against the source build. Sponza is excluded by default.

## Hardware coverage

The development machine is Windows x64 with an RTX 3070. A fresh publication build
and the checks actually run for it are recorded in [release checks](RELEASE_CHECKS.md).
Neither a same-machine clean build nor CPU-only CI establishes separate-machine,
low-end GPU or driver coverage.

For timing comparisons, use the built-in benchmark, which requires Windows
Developer Mode and a successful StablePowerState call. It restores the state when
the benchmark scope ends. Details and instrumentation overhead:
[profiling](PROFILING.md).

## Borderless B and base-only Gaussian scheduling

B is cleared with a matching optimized RT clear value and produced only in the
exact occupied tiles. XY occupancy and diagnostic frost/sharp bits have no pixel
or displacement padding. Z guards are unchanged. The base Gaussian UAV mip is
cleared, filtered only in bounds-marked groups, and later active mips run full-screen.
The GPU bounds oracle permits only 1/1000-pixel CPU/GPU projection rounding at
integer boundaries; production adds no such padding. Physical remapping is checked
independently from the GPU's validated virtual mask.

`check_prewarped_background.py` compares normal and poisoned initialization with
the same schedule, verifies zero color/validity outside B/base producer domains,
and exercises sparse/dense filtering, cubic/bilinear and maximum post offset.
The older ROI/full-blur equality checks assume expanded producer halos and are
no longer an equivalence contract: full base filtering is a quality reference.
