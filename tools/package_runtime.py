# SPDX-License-Identifier: MIT
"""Package only the viewer, runtime shaders and required notices; no external assets."""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, default=ROOT / "bin")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/avboit-exp")
    args = parser.parse_args()
    binaries, output = args.bin_dir.resolve(), args.output.resolve()
    if output.exists():
        raise SystemExit(f"Output already exists; choose a fresh directory: {output}")
    files = {
        "LICENSE.txt": ROOT / "LICENSE.txt",
        "THIRD_PARTY_NOTICES.md": ROOT / "THIRD_PARTY_NOTICES.md",
        "tools/fetch_sponza.py": ROOT / "tools/fetch_sponza.py",
        "assets/sponza.lock.json": ROOT / "assets/sponza.lock.json",
        "bin/avboit_viewer.exe": binaries / "avboit_viewer.exe",
        "licenses/ProggyClean.txt": ROOT / "licenses/ProggyClean.txt",
    }
    for path in (ROOT / "docs").glob("*.md"):
        files["docs/" + path.name] = path
    notices = {
        "Donut.txt": "donut/LICENSE.txt",
        "NVRHI.txt": "donut/nvrhi/LICENSE.txt",
        "ShaderMake.txt": "donut/ShaderMake/LICENSE.txt",
        "ImGui.txt": "donut/thirdparty/imgui/LICENSE.txt",
        "GLFW.txt": "donut/thirdparty/glfw/LICENSE.md",
        "cgltf.txt": "donut/thirdparty/cgltf/LICENSE",
        "stb.txt": "donut/thirdparty/stb/LICENSE",
        "JsonCpp.txt": "donut/thirdparty/jsoncpp-amalgam/LICENSE",
    }
    for name, path in notices.items():
        files["licenses/" + name] = ROOT / path
    dx_license = args.build_dir.resolve() / "_deps/directx_headers-src/LICENSE"
    files["licenses/DirectX-Headers.txt"] = dx_license
    # Manifest-driven copying excludes stale/test shader permutations from bin.
    for line in (ROOT / "avboit/shaders.cfg").read_text().splitlines():
        tokens = shlex.split(line, comments=True)
        if not tokens:
            continue
        source = Path(tokens[0])
        entry = tokens[tokens.index("-E") + 1] if "-E" in tokens else "main"
        name = source.stem + (("_" + entry) if entry != "main" else "") + ".bin"
        relative = Path("shaders/avboit/dxil") / source.parent / name
        files[(Path("bin") / relative).as_posix()] = binaries / relative
    framework = binaries / "shaders/framework/dxil"
    for path in framework.rglob("*.bin"):
        files[(Path("bin/shaders/framework/dxil") / path.relative_to(framework)).as_posix()] = path
    if not framework.is_dir():
        raise SystemExit("Missing Donut DXIL shader directory; build the viewer first")
    for destination, source in files.items():
        if not source.is_file():
            raise SystemExit(f"Missing package source: {source}")
    for destination, source in files.items():
        target = output / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    (output / "README.md").write_text(
        "# avboit-exp runtime\n\n"
        "Windows x64 / DX12. Start `bin/avboit_viewer.exe`.\n"
        "The default procedural scene needs no downloaded assets. Internal rendering\n"
        "defaults to 2560x1440 with a 120 FPS cap; ImGui changes quality and fixtures live.\n\n"
        "WASD/QE moves, right mouse looks, Home resets. B toggles reconstruction, G frost,\n"
        "V VFX, Space animation. `bin/avboit_viewer.exe --help` lists the CLI.\n\n"
        "Optional Sponza: run `python tools/fetch_sponza.py`, then choose Sponza in ImGui.\n"
        "The downloader's `--proxy http://HOST:PORT` applies to that invocation only.\n"
        "Sponza is separately licensed and is not bundled; see THIRD_PARTY_NOTICES.md.\n\n"
        "The entire folder is relocatable. Shader and asset paths are relative to the\n"
        "executable. Captures/CSVs use the current working directory. Tests and build\n"
        "tools are excluded. See docs/PIPELINE.md and docs/KNOWN_LIMITATIONS.md.\n",
        encoding="utf-8",
    )
    (output / "README.zh-CN.md").write_text(
        "# avboit-exp 运行包\n\n"
        "Windows x64 / DX12，直接运行 `bin/avboit_viewer.exe`。\n"
        "默认程序化场景不依赖外部模型。默认内部渲染分辨率 2560×1440、120 FPS 上限，\n"
        "画质和场景选项均可在 ImGui 中即时修改。\n\n"
        "WASD/QE 移动，右键转视角，Home 复位；B 切换重建过滤，G 开关磨砂，\n"
        "V 开关 VFX，空格暂停动画。命令行选项见 `bin/avboit_viewer.exe --help`。\n\n"
        "可选 Sponza：运行 `python tools/fetch_sponza.py`，然后在 ImGui 中切换场景。\n"
        "下载命令可附加 `--proxy http://HOST:PORT`。Sponza 有独立许可，未包含在运行包中；\n"
        "详见 [第三方授权](THIRD_PARTY_NOTICES.md)。\n\n"
        "可以整体移动此文件夹。Shader 与场景路径相对于 exe 查找；截图和 CSV 写入当前工作目录。\n"
        "运行包不包含构建工具和测试程序。\n\n"
        "[管线说明](docs/PIPELINE.zh-CN.md) · [已知限制](docs/KNOWN_LIMITATIONS.zh-CN.md)\n",
        encoding="utf-8",
    )
    manifest = {}
    for path in sorted(output.rglob("*")):
        if path.is_file():
            data = path.read_bytes()
            manifest[path.relative_to(output).as_posix()] = {
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
    (output / "manifest.sha256.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Runtime package: {output} ({len(manifest)} files, one executable)")


if __name__ == "__main__":
    main()
