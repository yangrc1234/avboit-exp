# Contributing

This is a Windows/DX12 rendering experiment. Small, reviewable changes and clear
image-quality tradeoffs are preferred over a general engine framework.

## Build and checks

```powershell
git submodule update --init --recursive
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements-dev.txt
.venv/Scripts/python tools/format_sources.py
python build_native.py --tests
.venv/Scripts/python tools/check_repository.py
```

`--tests` requires a DX12 GPU; `--cpu-tests` builds the same targets but runs only
the CPU tests. GPU tests run serially. Use 2560x1440 for image-quality checks;
small and non-aligned sizes in dimension tests validate resource handling only.
See [validation](docs/VALIDATION.md) for extended readback regressions.

## Code conventions

- C++17; four-space indentation; Allman braces; 120-column C++/HLSL limit.
  Run the pinned formatter instead of manually reflowing code. Includes retain
  their order because HLSL includes and test fragments can depend on it.
- Python 3.9+ with Black, 100 columns. Format only first-party sources.
- `avboit/render` owns pass resources and records commands. It must not depend
  on ImGui, scene loading, window management or the sample's fixtures.
- A pass `Record` must not submit, present or wait for GPU completion. The host
  owns synchronization around resource replacement and CPU readback.
- Keep shader resource bindings, depth conventions, premultiplied validity and
  filter footprints explicit. Update CPU oracles when changing a data contract.
- Use `AVBOIT_GPU_SCOPE` for markers and timing. Actors/material sections remain
  separate draws, even when they share compiled shaders.
- Avoid unrelated render changes during cleanup. Keep numerical reference paths
  when they detect a real failure mode; do not add redundant production paths.

For a rendering change, include the fixture/command, resolution, GPU, screenshots
or readback comparison, and relevant tests. Performance comparisons require
StablePowerState; timings from one GPU are not claims about other hardware.

## Licenses and provenance

Contributions are provided under the repository's MIT license. Keep existing
copyright notices. Add `SPDX-License-Identifier: MIT` to new source files. A
separate CLA is not required. Do not copy a third party's implementation without
recording its source and preserving its applicable license.

Keep downloaded assets and build outputs out of Git. Sponza is optional and
separately licensed. See [third-party notices](THIRD_PARTY_NOTICES.md).
