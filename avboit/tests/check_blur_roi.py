# SPDX-License-Identifier: MIT
"""Compare native ROI/full-screen captures without third-party Python packages.

Run from any directory; Sponza assets must already be available to the viewer.
The headless viewer warms intermediates using a different camera before capture.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
parser.add_argument("--max-frost-sigma", type=float, default=24.0)
parser.add_argument("--sparse-blur", action="store_true")
parser.add_argument("--donut-scene", action="store_true")
parser.add_argument("--render-width", type=int, default=2560)
parser.add_argument("--render-height", type=int, default=1440)
args = parser.parse_args()
viewer = args.viewer.resolve()
with tempfile.TemporaryDirectory(prefix="avboit-blur-") as scratch:
    directory = Path(scratch)
    for camera in range(6):
        captures = []
        for full in (False, True):
            command = [
                str(viewer),
                "--headless",
                "--sponza",
                "--vfx",
                "--camera-preset",
                str(camera),
                "--max-frost-sigma",
                str(args.max_frost_sigma),
            ]
            if full:
                command.append("--full-blur")
            if args.sparse_blur:
                command.append("--sparse-blur")
            command += [
                "--render-width",
                str(args.render_width),
                "--render-height",
                str(args.render_height),
            ]
            if args.donut_scene:
                command.append("--donut-scene")
            result = subprocess.run(command, cwd=directory, capture_output=True)
            if result.returncode:
                raise RuntimeError(
                    result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace")
                )
            captures.append((directory / "native-raster.ppm").read_bytes())
        if captures[0] != captures[1]:
            raise AssertionError(f"Camera {camera}: ROI/full-screen image mismatch")
        print(f"Camera {camera}: ROI/full-screen byte-identical", flush=True)
