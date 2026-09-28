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
adaptive/fixed mapping, packed atomics, occupancy, filter footprints and the
interactive viewer's live resource/material controls.

## Extended readback checks

Install `requirements-dev.txt` into a virtual environment. Useful targeted checks:

```powershell
.venv/Scripts/python avboit/tests/check_single_sided_sphere.py
.venv/Scripts/python avboit/tests/check_tile_pipeline.py
.venv/Scripts/python avboit/tests/check_frost_chain.py
.venv/Scripts/python avboit/tests/check_extinction_roi.py
.venv/Scripts/python avboit/tests/check_blur_roi.py
.venv/Scripts/python avboit/tests/check_background_protection.py
.venv/Scripts/python avboit/tests/check_vfx_materials.py
```

Most scripts accept `--viewer` for a non-default executable; some fixed fixtures
use `bin/avboit_viewer.exe`. Check the script before selecting a fixture or output directory. Dense, poisoned and ROI references
are useful for detecting unwritten-source reads, not just visible artifacts.

`check_submission_order.py` reports finite-precision order differences rather than
claiming bitwise order independence. The packed RGB overflow stress in the GPU
oracle intentionally documents the carry limitation. See
[known limitations](KNOWN_LIMITATIONS.md) before interpreting test results.

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
