# Publication checks

Initial publication preparation, 2026-09-28. Local machine: Windows x64,
RTX 3070, Visual Studio 2022 and Windows SDK DXC 10.0.26100.0.
These are correctness checks, not StablePowerState performance measurements.

## Build and source

- A fresh build directory produced the default viewer and shaders. The application
  `bin` contains only `avboit_viewer.exe`; ShaderMake lives under `build/tools`.
- The optional test build passed **38/38 CTest cases**, including installed Sponza:
  8 CPU-labeled and 30 GPU-labeled checks, run serially.
- The asset installer's 5 unit tests passed. Sponza was installed from a local
  cache after validating all 76 manifest entries; no new download was needed.
- clang-format 19.1.7 and Black 25.1.0 checks passed for first-party sources.
- The repository audit checked license identifiers, local documentation links,
  generated-file exclusions and basic credential patterns. This is not a complete
  security audit.
- Donut and its nested dependencies are pinned Git submodules. No external model,
  test executable, capture dump or build output is committed.

## Rendering

The following extended readback scripts passed:

- `check_single_sided_sphere.py`
- `check_tile_pipeline.py`
- `check_frost_chain.py`
- `check_extinction_roi.py`
- `check_blur_roi.py`
- `check_background_protection.py`
- `check_vfx_materials.py`
- `check_compact_pipeline.py`
- `check_calibration.py`

The main reference is 2560x1440. Cross-resolution and allocation checks also use
explicit smaller, larger and rounded dimensions.

Five 2560x1440 fixtures were compared with the pre-publication development build:
default, frosted/emissive, RGB sphere with smoke, layered VFX and Sponza. Both
the final image and captured transmittance LUT were **byte-identical** in all five.

## Runtime package

The asset-free package contains one executable, shaders, documentation and license
notices. Five relocated fixtures matched the source build's image and LUT exactly
when launched from an unrelated working directory. An explicitly missing Sponza
path failed with the expected diagnostic. The package includes a SHA-256 manifest.

## Remaining coverage

GitHub Actions is configured to check sources and build/run CPU tests on Windows;
the initial remote CI result is not included in this local record. DXC's automatic
download path was not exercised in the local build, which used the SDK compiler.
No separate-machine, lower-end GPU or alternate-driver verification is claimed.
The [known limitations](KNOWN_LIMITATIONS.md) remain applicable.
