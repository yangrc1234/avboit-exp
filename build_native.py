# SPDX-License-Identifier: MIT
"""Configure and build the viewer; regression executables are opt-in."""

import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dxc", type=Path, help="Use installed DXC; otherwise ShaderMake downloads it"
    )
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=8)
    tests = parser.add_mutually_exclusive_group()
    tests.add_argument(
        "--tests", action="store_true", help="Run CPU and DX12 tests (requires a GPU)"
    )
    tests.add_argument(
        "--cpu-tests", action="store_true", help="Build tests, run only CPU tests (CI)"
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    root = Path(__file__).resolve().parent
    build = args.build_dir.resolve() if args.build_dir else root / "build"
    environment = {key.upper(): value for key, value in os.environ.items()}
    enabled = args.tests or args.cpu_tests
    configure = [
        "cmake",
        "-S",
        str(root),
        "-B",
        str(build),
        "-G",
        "Visual Studio 17 2022",
        "-A",
        "x64",
        "-DBUILD_TESTING=" + ("ON" if enabled else "OFF"),
    ]
    if args.dxc:
        configure += [
            "-DSHADERMAKE_FIND_COMPILERS=OFF",
            "-DSHADERMAKE_DXC_PATH=" + str(args.dxc.resolve()),
        ]
    if args.output_dir:
        configure += ["-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" + str(args.output_dir.resolve())]
    subprocess.run(configure, env=environment, check=True)
    command = ["cmake", "--build", str(build), "--config", "Release", "--parallel", str(args.jobs)]
    if not enabled:
        command += ["--target", "avboit_viewer"]
    subprocess.run(command, env=environment, check=True)
    if enabled:
        command = [
            "ctest",
            "--test-dir",
            str(build),
            "-C",
            "Release",
            "--output-on-failure",
            "-j",
            "1",
        ]
        if args.cpu_tests:
            command += ["-L", "cpu"]
        subprocess.run(command, env=environment, check=True)


if __name__ == "__main__":
    main()
