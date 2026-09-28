# SPDX-License-Identifier: MIT
"""Check inversion direction and radiance scaling using rendered output."""
from pathlib import Path
import argparse, struct, subprocess, tempfile, math

parser = argparse.ArgumentParser()
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="avboit-calibration-") as directory:
    directory = Path(directory)

    def run(options):
        p = subprocess.run(
            [str(args.viewer.resolve()), "--headless", *options], cwd=directory, capture_output=True
        )
        if p.returncode:
            raise RuntimeError(
                p.stdout.decode(errors="replace") + p.stderr.decode(errors="replace")
            )

    run(["--calibration-board", "--glass-sphere", "--no-smoke"])
    header, w, h, rgb = (directory / "native-raster.ppm").read_bytes().split(b"\n", 3)
    width, height = map(int, w.split())
    assert (width, height) == (2560, 1440)

    def pixel(x, y):
        x = round(x * width / 640)
        y = round(y * height / 384)
        return tuple(rgb[(y * width + x) * 3 : (y * width + x) * 3 + 3])

    yellow, blue, green, red = [
        pixel(x, y) for x, y in [(280, 150), (360, 150), (280, 220), (360, 220)]
    ]
    assert min(yellow[:2]) > yellow[2] + 40
    assert blue[2] > max(blue[:2]) + 40
    assert green[1] > max(green[0], green[2]) + 40
    assert red[0] > max(red[1:]) + 40
    print("PASS: sphere reverses both calibration-board axes")
    captures = []
    luts = []
    for gain in (0, 1, 2):
        run(
            [
                "--emissive-balls",
                "--emissive-gain",
                str(gain),
                "--hardware-depth",
                "--composition-half",
                "--no-frost",
                "--refraction-gain",
                "0",
                "--no-smoke",
            ]
        )
        raw = (directory / "native-scene-color.rgba16f").read_bytes()
        captures.append(list(struct.iter_unpack("<4e", raw[8:])))
        luts.append((directory / "native-lut.rgba8").read_bytes())
    assert luts[0] == luts[1] == luts[2], "Radiance changed extinction"
    changed = 0
    for zero, one, two in zip(*captures):
        assert zero[3] == one[3] == two[3], "Radiance changed depth"
        assert all(math.isfinite(v) for v in two)
        if zero[:3] != one[:3]:
            changed += 1
            # Transparent shading is a constant baseline; varying opaque emission
            # must remain affine through the OM transmission blend.
            assert all(
                abs((b - z) - 2 * (a - z)) <= max(0.004, abs(b) * 0.004)
                for z, a, b in zip(zero[:3], one[:3], two[:3])
            )
    assert changed > 100
    print(f"PASS: {changed} emissive pixels scale linearly; depth and LUT unchanged")
