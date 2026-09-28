# SPDX-License-Identifier: MIT
"""Compare all addressable LUT texels and the final image, across moving views.

The poisoned path initializes every extinction word with nonzero garbage before
sparse clear, exposing missing clear coverage and reads of inactive storage.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import struct

parser = argparse.ArgumentParser()
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
parser.add_argument("--volume-scale", type=int, choices=(2, 8), default=8)
parser.add_argument("--depth-budget", type=int, choices=(16, 32, 64, 128), default=128)
parser.add_argument("--fixed-z", action="store_true")
parser.add_argument("--donut-scene", action="store_true")
parser.add_argument("--render-width", type=int, default=2560)
parser.add_argument("--render-height", type=int, default=1440)
args = parser.parse_args()
viewer = args.viewer.resolve()
with tempfile.TemporaryDirectory(prefix="avboit-extinction-") as scratch:
    directory = Path(scratch)
    for camera in range(7):
        captures = []
        for mode in ("--dense-extinction", "--poison-extinction", ""):
            command = [
                str(viewer),
                "--headless",
                "--sponza",
                "--vfx",
                "--camera-preset",
                str(camera),
                "--volume-scale",
                str(args.volume_scale),
            ]
            if mode:
                command.append(mode)
            command += ["--depth-budget", str(args.depth_budget)]
            command += [
                "--render-width",
                str(args.render_width),
                "--render-height",
                str(args.render_height),
            ]
            if args.fixed_z:
                command.append("--fixed-z")
            if args.donut_scene:
                command.append("--donut-scene")
            result = subprocess.run(command, cwd=directory, capture_output=True)
            if result.returncode:
                raise RuntimeError(
                    result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace")
                )
            captures.append(
                (
                    (directory / "native-raster.ppm").read_bytes(),
                    (directory / "native-lut.rgba8").read_bytes(),
                )
            )
        for _, lut in captures:
            width, height, depth = struct.unpack("<III", lut[:12])
            assert (width, height) == (
                (args.render_width + args.volume_scale - 1) // args.volume_scale,
                (args.render_height + args.volume_scale - 1) // args.volume_scale,
            )
            assert 1 <= depth <= 128 and len(lut) == 12 + width * height * depth * 4
        if camera == 0 and min(captures[0][1][12:]) == 255:
            raise AssertionError("Calibration LUT contains no extinction")
        if not captures[0] == captures[1] == captures[2]:
            raise AssertionError(f"Camera {camera}: sparse/dense LUT or image mismatch")
        print(f"Camera {camera}: dense/sparse/poisoned LUT + image identical", flush=True)
