# SPDX-License-Identifier: MIT
"""GPU checks for compact targets, interface filtering and half-resolution VFX."""
from pathlib import Path
import argparse, math, struct, subprocess

parser = argparse.ArgumentParser()
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
parser.add_argument(
    "--output", type=Path, default=Path(__file__).resolve().parents[2] / "build/compact-check"
)
a = parser.parse_args()
a.output.mkdir(parents=True, exist_ok=True)


def capture(name, flags):
    folder = a.output / name
    folder.mkdir(exist_ok=True)
    exe = a.viewer
    run = subprocess.run(
        [str(exe.resolve()), "--headless", "--poison-resolve", *flags],
        cwd=folder,
        capture_output=True,
    )
    (folder / "run.log").write_bytes(run.stdout + run.stderr)
    assert run.returncode == 0, (name, run.stdout + run.stderr)
    return folder


cases = [
    ("ordinary", ["--stress", "8"]),
    ("smoke", ["--stress", "14", "--time", ".7"]),
    ("curves", ["--stress", "4", "--time", ".7"]),
    ("sphere", ["--stress", "6"]),
    ("cutout", ["--stress", "10"]),
    ("off", ["--stress", "4", "--no-frost", "--refraction-gain", "0"]),
    (
        "shuffled",
        ["--stress", "10", "--submission-order", "2", "--order-seed", "17", "--time", "2"],
    ),
    ("aligned", ["--stress", "14", "--render-width", "641", "--render-height", "385"]),
]
for name, flags in cases:
    selected = capture(name, flags)
    all_draws = capture(name + "-all", flags + ["--all-interface"])
    for file in (
        "native-raster.ppm",
        "native-interface.rgba32f",
        "native-background.rgba16f",
        "native-lut.rgba8",
    ):
        assert (selected / file).read_bytes() == (all_draws / file).read_bytes(), (
            name,
            "Interface filtering changed",
            file,
        )
    print(name, ": selective/all geometry exact", flush=True)


def field(folder):
    raw = (folder / "native-vfx.rg16f").read_bytes()
    w, h = struct.unpack_from("<II", raw)
    values = [v[0] for v in struct.iter_unpack("<e", raw[8:])]
    assert len(values) == w * h * 2 and all(math.isfinite(v) for v in values)
    return w, h, values


for width, height in ((640, 384), (641, 385)):
    for mode in (1, 2, 3):
        flags = [
            "--stress",
            "8",
            "--vfx-mode",
            str(mode),
            "--time",
            ".15",
            "--render-width",
            str(width),
            "--render-height",
            str(height),
        ]
        half = capture(f"vfx-{width}-{mode}-half", flags + ["--vfx-half"])
        full = capture(f"vfx-{width}-{mode}-full", flags + ["--vfx-full"])
        w, h, lo = field(half)
        fw, fh, hi = field(full)
        aligned_w, aligned_h = (width + 3) & ~3, (height + 3) & ~3
        assert (w, h) == (aligned_w // 2, aligned_h // 2) and (fw, fh) == (aligned_w, aligned_h)
        assert min(lo) < 0 and max(lo) > 0, "Signed offset lost"
        # Offset units stay in full-resolution pixels; only the sampling grid changes.
        energyLo = sum(abs(v) for v in lo) / len(lo)
        energyHi = sum(abs(v) for v in hi) / len(hi)
        ratio = energyLo / energyHi
        assert 0.9 < ratio < 1.1, ("Half-res strength or coverage changed excessively", ratio)
        for file in ("native-background.rgba16f", "native-lut.rgba8"):
            assert (half / file).read_bytes() == (
                full / file
            ).read_bytes(), "VFX resolution changed frost/OIT"
        print("VFX", width, height, mode, "field mean magnitude ratio", round(ratio, 5), flush=True)
zero = capture("vfx-zero", ["--vfx-mode", "1", "--vfx-strength", "0"])
assert not any(field(zero)[2])
print("PASS: filtered interface draws, half-res VFX dimensions/units/independence")
