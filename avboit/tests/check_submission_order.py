# SPDX-License-Identifier: MIT
"""Measure finite-precision order sensitivity; require exact integer-path LUTs.

Image equality is deliberately not required: additive floating-point MRT blending
is not associative. Report the error rather than hide it behind a loose threshold.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
parser.add_argument(
    "--output", type=Path, default=Path(__file__).resolve().parents[2] / "build/submission-order"
)
args = parser.parse_args()
exe = args.viewer.resolve()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
report = []
for fixture in (0, 2, 6, 10):
    for accum_half, composition_half in (
        (False, False),
        (True, False),
        (False, True),
        (True, True),
    ):
        label = f"stress-{fixture}-accum-{int(accum_half)}-composition-{int(composition_half)}"
        reference = None
        for order in range(3):
            path = root / f"{label}-order-{order}"
            path.mkdir(exist_ok=True)
            command = [
                str(exe),
                "--headless",
                "--stress",
                str(fixture),
                "--time",
                "2",
                "--hardware-depth",
                "--emissive-balls",
                "--submission-order",
                str(order),
                "--order-seed",
                "42",
            ]
            if accum_half:
                command += ["--accum-half"]
            if composition_half:
                command += ["--composition-half"]
            run = subprocess.run(command, cwd=path, capture_output=True)
            (path / "run.log").write_bytes(run.stdout + run.stderr)
            if run.returncode:
                raise RuntimeError(f"{label}/{order}: see run.log")
            image = (path / "native-raster.ppm").read_bytes().split(b"\n", 3)[3]
            lut = (path / "native-lut.rgba8").read_bytes()
            if order == 0:
                reference = (image, lut)
                continue
            if lut != reference[1]:
                raise AssertionError(f"{label}/{order}: integer extinction LUT changed with order")
            differences = [abs(a - b) for a, b in zip(image, reference[0])]
            row = dict(
                fixture=fixture,
                accum_half=accum_half,
                composition_half=composition_half,
                order=order,
                max_display_byte_error=max(differences),
                rms_display_byte_error=math.sqrt(
                    sum(d * d for d in differences) / len(differences)
                ),
                channels_changed=sum(d != 0 for d in differences),
                lut_identical=True,
            )
            report.append(row)
            print(row, flush=True)
(root / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
