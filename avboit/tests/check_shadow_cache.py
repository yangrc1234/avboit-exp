# SPDX-License-Identifier: MIT
"""Static shadow cache must match per-frame redraw after camera changes."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--donut-scene", action="store_true")
parser.add_argument("--legacy-scene", action="store_true")
args = parser.parse_args()

viewer = Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
with tempfile.TemporaryDirectory(prefix="avboit-shadow-") as scratch:
    directory = Path(scratch)
    for preset in range(6):
        images = []
        for redraw in (False, True):
            command = [str(viewer), "--headless", "--sponza", "--camera-preset", str(preset)]
            if args.legacy_scene:
                command.append("--legacy-scene")
            elif args.donut_scene:
                command.append("--donut-scene")
            if redraw:
                command.append("--redraw-shadows")
            result = subprocess.run(command, cwd=directory, capture_output=True)
            if result.returncode:
                raise RuntimeError((result.stdout + result.stderr).decode(errors="replace"))
            images.append((directory / "native-raster.ppm").read_bytes())
        assert images[0] == images[1], f"Shadow cache differs at camera {preset}"
        print(f"Camera {preset}: cached/redrawn shadow image identical", flush=True)
