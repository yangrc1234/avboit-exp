# Third-party notices and asset policy

Project additions are MIT licensed; see [LICENSE.txt](LICENSE.txt).
The host started from [NVIDIA Donut Samples](https://github.com/NVIDIA-RTX/Donut-Samples).
Its MIT copyright and permission text are retained in the root license and
inherited source headers. The AVBOIT implementation is based on public research,
not Activision or Call of Duty source. No additional CLA applies to this project.

## Dependencies

Donut is pinned by the root Git submodule. Its nested submodules pin NVRHI,
ShaderMake, ImGui, GLFW, cgltf and stb. Embedded JsonCpp retains its upstream
license. Do not remove notices when distributing a modified build.

| Component | Upstream | License in the checkout |
|---|---|---|
| Donut | [NVIDIA-RTX/Donut](https://github.com/NVIDIA-RTX/Donut) | `donut/LICENSE.txt` — MIT |
| NVRHI | [NVIDIA-RTX/NVRHI](https://github.com/NVIDIA-RTX/NVRHI) | `donut/nvrhi/LICENSE.txt` — MIT |
| ShaderMake | [NVIDIA-RTX/ShaderMake](https://github.com/NVIDIA-RTX/ShaderMake) | `donut/ShaderMake/LICENSE.txt` — MIT |
| Dear ImGui | [ocornut/imgui](https://github.com/ocornut/imgui) | `donut/thirdparty/imgui/LICENSE.txt` — MIT |
| GLFW | [glfw/glfw](https://github.com/glfw/glfw) | `donut/thirdparty/glfw/LICENSE.md` — zlib/libpng |
| cgltf | [jkuhlmann/cgltf](https://github.com/jkuhlmann/cgltf) | `donut/thirdparty/cgltf/LICENSE` — MIT |
| stb | [nothings/stb](https://github.com/nothings/stb) | `donut/thirdparty/stb/LICENSE` — public-domain/MIT choice |
| JsonCpp | [open-source-parsers/jsoncpp](https://github.com/open-source-parsers/jsoncpp) | `donut/thirdparty/jsoncpp-amalgam/LICENSE` — MIT/public domain |
| DirectX-Headers | [microsoft/DirectX-Headers](https://github.com/microsoft/DirectX-Headers) | fetched dependency `LICENSE` — MIT |
| ProggyClean (ImGui default font) | Dear ImGui's embedded font; Tristan Grimmer | `licenses/ProggyClean.txt` — MIT |

`tools/package_runtime.py` includes these license texts. DXC and the Windows SDK
are build prerequisites, not bundled runtime binaries. Python and formatting
packages in `requirements-dev.txt` are development dependencies, not vendored code.

## Optional Sponza

Sponza is **not MIT licensed by this project** and is not committed or bundled.
The optional downloader retrieves the pinned Khronos sample asset and its original
README, metadata and license references. The upstream metadata identifies Crytek
and the Cryengine Limited License Agreement; metadocumentation uses CC-BY-4.0.
Consult the upstream grant before redistributing the model or derived media:

- [Sponza model and attribution](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza)
- [Sponza license references](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/Sponza/LICENSE.md)

`assets/sponza.lock.json` records the source revision and each file's Git blob hash
and size. The downloader preserves the files and directory relationships. A license
reference is not a new grant from this repository's maintainer.

All bundled scene fixtures are generated procedurally from the MIT source. No
external weapon model is distributed.

## Research references

- Michal Drobot, [Adaptive Voxel-Based Order-Independent Transparency, SIGGRAPH 2025](https://advances.realtimerendering.com/s2025/content/AVBOIT_SIG2025_MDROBOT-final.pdf).
- [XeGTAO / Vanilla](https://github.com/GameTechDev/XeGTAO) was studied for profiling
  organization; its profiler code is not vendored. See [profiling](docs/PROFILING.md).

The papers and third-party repositories are references, not relicensed copies.
