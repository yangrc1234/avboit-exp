# SPDX-License-Identifier: MIT
"""Reproduce low-resolution LUT silhouette errors against exact sorted glass.

Preset 27 has two separated parallel depth layers. The analytic reference uses
the same pixel centers, coverage and RGB transmission, without frost/refraction.
Exclude the immediate raster edge when measuring error: ordinary single-sample
geometry aliasing is not the LUT artifact this test investigates.
"""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from capture_utils import texture


def reference(width, height, time, third_layer=False):
    sx = (2 * (np.arange(width) + 0.5) / width - 1) * width / height * 0.57735027
    sy = (1 - 2 * (np.arange(height) + 0.5) / height) * 0.57735027
    x, y = np.broadcast_arrays(sx[None, :] * 3.8, sy[:, None] * 3.8)
    shift = 0.025 * np.sin(time)
    front = np.zeros((height, width), dtype=bool)
    quads = [(-1.8, (0.63, 0.16), (-0.24, 0.95), -1), (0, (0.65, 0), (0, 0.95), 0.55)]
    quads += [(1.4 + i * 0.36, (0.008 * (i + 1), 0), (0.13, 0.95), -1) for i in range(3)]
    for center, a, b, hole in quads:
        px = x - center - shift
        det = a[0] * b[1] - a[1] * b[0]
        u, v = (px * b[1] - y * b[0]) / det, (a[0] * y - a[1] * px) / det
        mask = (abs(u) <= 1) & (abs(v) <= 1)
        if hole > 0:
            mask &= u * u + v * v >= hole * hole
        front |= mask
    rear = (abs(sx[None, :] * 6) < 4.8) & (abs(sy[:, None] * 6) < 2.1)
    back_t = np.array([0.8, 0.18, 0.08])
    front_t = np.array([0.06, 0.45, 0.8])
    base = 0.12
    if third_layer:
        third_t = np.array([0.25, 0.7, 0.45])
        base = (1 - third_t) * [0.12, 0.35, 0.8] + third_t * 0.12
    behind = (1 - back_t) * [0.9, 0.35, 0.06] + back_t * base
    overlap = (1 - front_t) * [0.025, 0.04, 0.055] + front_t * behind
    expected = np.full((height, width, 3), 0.12)
    expected[rear] = behind
    expected[front & rear] = overlap
    image = Image.fromarray(front.astype("uint8") * 255)
    inner = np.array(image.filter(ImageFilter.MinFilter(3))) != 0
    interior = np.array(image.filter(ImageFilter.MinFilter(25))) != 0
    return expected, inner & ~interior & rear, interior & rear


def display(hdr):
    v = np.maximum(hdr, 0)
    return Image.fromarray(np.rint((v / (1 + v)) ** (1 / 2.2) * 255).clip(0, 255).astype("uint8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[2]
    parser.add_argument("--viewer", type=Path, default=root / "bin/avboit_viewer.exe")
    parser.add_argument("--output", type=Path, default=root / "build/glass-silhouette")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    captures, rows = {}, []
    cases = [
        ("default", 0, []),
        ("fine-xy", 0, ["--volume-scale", "2"]),
        ("no-zero-depth", 0, ["--no-zero-depth"]),
        ("no-slope", 0, ["--no-slope-splat"]),
        ("reverse", 0, ["--submission-order", "1"]),
    ]
    cases += [(f"motion-{i}", i * 0.2, []) for i in range(1, 6)]
    for name, time, flags in cases:
        folder = args.output / name
        folder.mkdir(exist_ok=True)
        run = subprocess.run(
            [
                str(args.viewer.resolve()),
                "--headless",
                "--no-taa",
                "--volume-full-res",
                "--no-lut-min-filter",
                "--stress",
                "27",
                "--render-width",
                "2560",
                "--render-height",
                "1440",
                "--composition-half",
                "--accum-half",
                "--time",
                str(time),
                *flags,
            ],
            cwd=folder,
            capture_output=True,
        )
        (folder / "run.log").write_bytes(run.stdout + run.stderr)
        if run.returncode:
            raise RuntimeError(f'{name}: see {folder / "run.log"}')
        hdr = texture(folder / "native-composition.rgba16f", "<f2", 4)[..., :3].astype("float32")
        assert np.isfinite(hdr).all(), name
        h, w = hdr.shape[:2]
        expected, edge, interior = reference(w, h, time)
        error = np.abs(hdr - expected).max(axis=2)
        assert error[interior].mean() < 0.002, "Reference disagrees away from silhouettes"
        rows.append(
            dict(
                case=name,
                edge_mean_hdr_error=float(error[edge].mean()),
                edge_p99_hdr_error=float(np.percentile(error[edge], 99)),
                interior_mean_hdr_error=float(error[interior].mean()),
            )
        )
        Image.open(folder / "native-raster.ppm").save(args.output / f"{name}.png")
        if time == 0:
            captures[name] = hdr
        if name == "default":
            display(expected).save(args.output / "exact-sorted.png")
            # Find the strongest error along the diagonal pane, away from the
            # intentionally sub-LUT-width strips. Crop is shared by all panels.
            search = edge & (np.indices(edge.shape)[1] < w * 0.42)
            py, px = np.unravel_index(np.where(search, error, -1).argmax(), error.shape)
            box = (int(px) - 80, int(py) - 80, int(px) + 80, int(py) + 80)
    panels = [
        ("AVBOIT: 1/8 XY", "default.png"),
        ("AVBOIT: 1/2 XY", "fine-xy.png"),
        ("Exact sorted over", "exact-sorted.png"),
    ]
    canvas = Image.new("RGB", (480 * 3, 516), "#202020")
    draw = ImageDraw.Draw(canvas)
    for i, (title, filename) in enumerate(panels):
        draw.text((i * 480 + 12, 10), title, fill="white")
        canvas.paste(
            Image.open(args.output / filename)
            .crop(box)
            .resize((480, 480), Image.Resampling.NEAREST),
            (i * 480, 36),
        )
    canvas.save(args.output / "comparison.png")
    frames = [
        Image.open(args.output / f"{name}.png")
        .crop(box)
        .resize((480, 480), Image.Resampling.NEAREST)
        for name in ["default"] + [f"motion-{i}" for i in range(1, 6)]
    ]
    frames[0].save(
        args.output / "edge-motion.gif",
        save_all=True,
        append_images=frames[1:] + frames[-2:0:-1],
        duration=180,
        loop=0,
    )
    report = dict(
        measurements=rows,
        crop=box,
        controls_max_hdr_difference={
            name: float(np.max(np.abs(captures[name] - captures["default"])))
            for name in ("no-zero-depth", "no-slope", "reverse")
        },
    )
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
