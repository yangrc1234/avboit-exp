# SPDX-License-Identifier: MIT
"""Stress ROI/occupancy with moving geometry, RGB transmission and cutout interfaces.

The dense/full reference validates work reduction, not physical ground truth.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
parser.add_argument("--sponza", action="store_true")
parser.add_argument("--capture-dir", type=Path)
parser.add_argument("--presets", nargs="+", type=int, choices=range(1, 11))
parser.add_argument("--sparse-blur", action="store_true")
args = parser.parse_args()
viewer = args.viewer.resolve()
with tempfile.TemporaryDirectory(prefix="avboit-stress-") as scratch:
    directory = Path(scratch)
    fixtures = [(i, 0.0) for i in range(1, 11)] + [(i, 2.0) for i in (3, 4, 5, 6)]
    if args.presets:
        fixtures = [(i, t) for i, t in fixtures if i in args.presets]
    for preset, time in fixtures:
        command = [
            str(viewer),
            "--headless",
            "--stress",
            str(preset),
            "--time",
            str(time),
            "--emissive-balls",
            "--vfx",
            "--rgb-glass",
            "--sphere-roughness",
            ".4",
        ]
        if args.sponza:
            command.append("--sponza")
        if args.sparse_blur:
            command.append("--sparse-blur")
        captures = []
        for flags in ([], ["--dense-extinction", "--full-blur"], ["--poison-extinction"]):
            result = subprocess.run(command + flags, cwd=directory, capture_output=True)
            if result.returncode:
                raise RuntimeError((result.stdout + result.stderr).decode(errors="replace"))
            captures.append(
                (
                    (directory / "native-raster.ppm").read_bytes(),
                    (directory / "native-lut.rgba8").read_bytes(),
                )
            )
        if captures[0] != captures[1] or captures[0] != captures[2]:
            raise AssertionError(
                f"Stress {preset}, t={time}: image/LUT differs from dense/full or poisoned reference"
            )
        if args.capture_dir:
            args.capture_dir.mkdir(parents=True, exist_ok=True)
            (args.capture_dir / f"stress-{preset}-t{time:g}.ppm").write_bytes(captures[0][0])
        print(
            f"Stress {preset}, t={time}: optimized/dense-full/poisoned image and LUT match",
            flush=True,
        )
