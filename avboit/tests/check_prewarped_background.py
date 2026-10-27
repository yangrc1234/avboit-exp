# SPDX-License-Identifier: MIT
"""2K checks for refraction resolved into B before Gaussian filtering.

Poison parity detects stale cache or mip reads. Large offsets must
work outside the B cache: sharp glass reads opaque there, frost rejects sources
without interface coverage. No old shader variant is required as an oracle.
"""

import argparse
from pathlib import Path
import subprocess

import numpy as np

from capture_utils import texture, work_domain


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=root / "build/prewarped-background-test")
    args = parser.parse_args()

    def capture(name, flags):
        folder = args.output / name
        folder.mkdir(parents=True, exist_ok=True)
        run = subprocess.run(
            [
                str(root / "bin/avboit_viewer.exe"),
                "--headless",
                "--hardware-depth",
                "--composition-half",
                "--render-width",
                "2560",
                "--render-height",
                "1440",
                *flags,
            ],
            cwd=folder,
            capture_output=True,
        )
        (folder / "run.log").write_bytes(run.stdout + run.stderr)
        assert run.returncode == 0, (folder, run.stdout + run.stderr)
        return folder

    sphere = [
        "--glass-sphere",
        "--rgb-glass",
        "--calibration-board",
        "--refraction-gain",
        "4",
    ]
    cases = {
        "sharp-smoke": ["--stress", "6", "--rgb-glass", "--refraction-gain", "4"],
        "frost": [*sphere, "--sphere-roughness", "0.65", "--emissive-balls"],
        "frost-dense": [
            *sphere,
            "--sphere-roughness",
            "0.65",
            "--dense-blur",
            "--frost-mips",
            "5",
        ],
        "holes-vfx": ["--stress", "10", "--vfx-mode", "3", "--time", ".15"],
        "textured-frost": ["--stress", "18", "--emissive-balls"],
        "textured-frost-dense": [
            "--stress",
            "18",
            "--emissive-balls",
            "--dense-blur",
            "--frost-mips",
            "5",
            "--frost-detail-radius",
            "64",
        ],
        "textured-frost-linear": [
            "--stress",
            "18",
            "--emissive-balls",
            "--linear",
            "--frost-detail-radius",
            "64",
            "--camera-preset",
            "7",
        ],
    }
    for name, flags in cases.items():
        folder = capture(name, flags)
        reference = capture(name + "-poison", flags + ["--poison-resolve"])
        for file in (
            "native-raster.ppm",
            "native-composition.rgba16f",
            "native-lut.rgba8",
        ):
            assert (folder / file).read_bytes() == (reference / file).read_bytes(), (
                name,
                file,
            )
        b = texture(folder / "native-background.rgba16f", "<f2", 4).astype("f4")
        mask = work_domain(folder, 10)
        poisoned = texture(reference / "native-background.rgba16f", "<f2", 4)
        assert np.array_equal(b[mask], poisoned[mask]), "B sampled its own unwritten cache"
        assert (b[~mask] == 0).all(), "Unwritten B lost zero validity"
        assert np.isfinite(b[mask]).all()
        assert ((b[mask, 3] >= 0) & (b[mask, 3] <= 1)).all()
        base = texture(folder / "native-frost-base.rgba16f", "<f2", 4)
        base_mask = work_domain(folder, 1)
        assert np.isfinite(base).all()
        assert (base[~base_mask] == 0).all(), "Unfiltered base lost zero validity"

        if name in ("sharp-smoke", "frost"):
            depth = texture(folder / "native-interface-depth.r32f", "<f4", 1)[..., 0]
            opaque_depth = texture(folder / "native-depth.r32f", "<f4", 1)[..., 0]
            offset = texture(folder / "native-interface-offset.rg16f", "<f2", 2).astype("f4")
            sigma = texture(folder / "native-interface-surface.rgba16f", "<f2", 4)[..., 3]
            radius = 24 * depth.shape[0] / 1440
            detail = (
                offset
                * np.minimum(1, radius / np.maximum(np.linalg.norm(offset, axis=2), 1e-5))[
                    ..., None
                ]
            )
            offset -= np.where((sigma > 0)[..., None], detail, 0)
            y, x = np.nonzero((depth > opaque_depth) & (np.abs(offset).max(axis=2) > 1))
            h, w = depth.shape
            sx = np.clip(x + offset[y, x, 0], 0, w - 1)
            sy = np.clip(y + offset[y, x, 1], 0, h - 1)
            x0, y0 = sx.astype(int), sy.astype(int)
            x1, y1 = np.minimum(x0 + 1, w - 1), np.minimum(y0 + 1, h - 1)
            corners = [(y0, x0), (y0, x1), (y1, x0), (y1, x1)]
            if name == "sharp-smoke":
                # With no transparency at any source texel, the final captured
                # SceneColor is still opaque. It is an independent source oracle.
                tiles = texture(folder / "native-screen-tiles.r32u", "<u4", 1)[..., 0]
                outside = np.logical_and.reduce(
                    [(tiles[cy // 64, cx // 64] & 4) == 0 for cy, cx in corners]
                )
                assert outside.sum() > 100, "Fixture did not leave the B cache"
                hdr = texture(folder / "native-composition.rgba16f", "<f2", 4).astype("f4")
                taps = [hdr[cy[outside], cx[outside], :3] for cy, cx in corners]
                fx, fy = (sx - x0)[outside, None], (sy - y0)[outside, None]
                expected = (taps[0] * (1 - fx) + taps[1] * fx) * (1 - fy)
                expected += (taps[2] * (1 - fx) + taps[3] * fx) * fy
                actual = b[y[outside], x[outside]]
                np.testing.assert_allclose(actual[:, :3], expected, rtol=0.002, atol=0.001)
                assert (actual[:, 3] == 1).all()
            else:
                outside = np.logical_and.reduce([depth[cy, cx] == 0 for cy, cx in corners])
                assert outside.sum() > 100, "Fixture did not leave interface coverage"
                assert (
                    b[y[outside], x[outside]] == 0
                ).all(), "Frost accepted uncovered source color"
            print(
                f"{name}: {outside.sum()} displaced pixels outside source coverage checked",
                flush=True,
            )
        print(
            f"{name}: poison parity, finite premultiplied B and Gaussian PASS",
            flush=True,
        )


if __name__ == "__main__":
    main()
