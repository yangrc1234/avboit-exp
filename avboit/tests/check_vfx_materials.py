# SPDX-License-Identifier: MIT
"""Validate signed VFX fields, independent layers and special-interface rejection."""
import argparse
import math
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
parser.add_argument(
    "--output", type=Path, default=Path(__file__).resolve().parents[2] / "build/vfx-materials"
)
args = parser.parse_args()
exe = args.viewer.resolve()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
WIDTH, HEIGHT = 2560, 1440


def capture(name, *options):
    path = root / name
    path.mkdir(exist_ok=True)
    result = subprocess.run(
        [
            str(exe),
            "--headless",
            "--render-width",
            str(WIDTH),
            "--render-height",
            str(HEIGHT),
            "--vfx-full",
            "--no-smoke",
            *options,
        ],
        cwd=path,
        capture_output=True,
    )
    (path / "run.log").write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"{name}: see run.log")
    data = (path / "native-vfx.rg16f").read_bytes()
    width, height = struct.unpack("<II", data[:8])
    assert (width, height) == (WIDTH, HEIGHT) and len(data) == 8 + width * height * 4
    field = [v[0] for v in struct.iter_unpack("<e", data[8:])]
    assert all(math.isfinite(v) for v in field), name
    return field, (path / "native-lut.rgba8").read_bytes()


heat0, _ = capture("heat-0", "--vfx-mode", "1", "--time", "0")
heat1, _ = capture("heat-1", "--vfx-mode", "1", "--time", ".7")
assert max(map(abs, heat0)) > 1 and heat0 != heat1
assert min(heat0) < 0 and max(heat0) > 0
shock0, _ = capture("shock-0", "--vfx-mode", "2", "--time", "0")
shock1, _ = capture("shock-1", "--vfx-mode", "2", "--time", ".7")
assert max(map(abs, shock0)) > 1 and shock0 != shock1
zero, _ = capture("strength-zero", "--vfx-mode", "1", "--vfx-strength", "0")
assert not any(zero)

primary, lut = capture(
    "dual-primary", "--stress", "8", "--vfx-mode", "3", "--vfx-layers", "1", "--time", ".15"
)
secondary, lut2 = capture(
    "dual-secondary", "--stress", "8", "--vfx-mode", "3", "--vfx-layers", "2", "--time", ".15"
)
both, lut3 = capture("dual-both", "--stress", "8", "--vfx-mode", "3", "--time", ".15")
assert lut == lut2 == lut3, "VFX must not enter extinction accumulation"
assert max(map(abs, primary)) > 1 and max(map(abs, secondary)) > 1
assert sum(a != 0 and b != 0 for a, b in zip(primary, secondary)) > 0, "Layers did not overlap"
worst = 0
for a, b, c in zip(primary, secondary, both):
    # Each isolated layer was already rounded to FP16. Combined blending rounds
    # after adding the shader's second contribution, so allow two input ULPs.
    magnitude = max(abs(a), abs(b), abs(c), 2**-14)
    tolerance = 2 ** (math.floor(math.log2(magnitude)) - 9)
    error = abs(a + b - c)
    worst = max(worst, error)
    assert error <= tolerance, (a, b, c, error, tolerance)

ordinary, _ = capture("rear-heat-ordinary", "--stress", "8", "--vfx-mode", "1", "--time", ".4")
special, _ = capture(
    "rear-heat-special", "--stress", "7", "--vfx-mode", "1", "--vfx-behind", "--time", ".4"
)
center = ((HEIGHT // 2) * WIDTH + WIDTH // 2) * 2
assert (
    max(abs(v) for v in ordinary[center : center + 2]) > 1
), "Ordinary glass incorrectly suppressed rear heat"
assert special[center : center + 2] == [0, 0], "Special interface did not reject rear heat"
print(
    f"Heat/shock animation, signed finite values, zero strength, two-layer blend, LUT exclusion and interface rejection PASS; max blend difference {worst:g} px"
)
