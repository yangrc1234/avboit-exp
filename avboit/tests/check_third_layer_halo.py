# SPDX-License-Identifier: MIT
"""Reproduce third-layer dark halos against exact per-pixel sorted over.

The middle layer has no zero transmission and every surface is front parallel.
Measure outside its true coverage, excluding the nearest one-pixel raster edge.
"""
from pathlib import Path
import json
import subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from capture_utils import texture
from check_glass_silhouette import display

root = Path(__file__).resolve().parents[2]
out = root / "build/third-layer-halo"
out.mkdir(parents=True, exist_ok=True)
images = {}
report = []
for name, flags in [
    ("min", []),
    ("no-min", ["--no-lut-min-filter"]),
    ("no-zero", ["--no-zero-depth", "--no-zero-t-shortcut"]),
    ("fp16", ["--accum-half"]),
    ("fine-xy", ["--volume-scale", "2"]),
    ("reverse", ["--submission-order", "1"]),
]:
    folder = out / name
    folder.mkdir(exist_ok=True)
    run = subprocess.run(
        [
            str(root / "bin/avboit_viewer.exe"),
            "--headless",
            "--lut-min-retain",
            "0",
            "--no-taa",
            "--stress",
            "33",
            "--render-width",
            "2560",
            "--render-height",
            "1440",
            "--composition-half",
            *flags,
        ],
        cwd=folder,
        capture_output=True,
    )
    (folder / "run.log").write_bytes(run.stdout + run.stderr)
    assert run.returncode == 0, (name, run.stderr[-1500:])
    hdr = texture(folder / "native-composition.rgba16f", "<f2", 4)[..., :3].astype(
        "float32"
    )
    assert np.isfinite(hdr).all(), name
    images[name] = hdr
    display(hdr).save(out / (name + ".png"))
    if name == "min":
        h, w = hdr.shape[:2]
        assert (w, h) == (2560, 1440)
        x = (2 * (np.arange(w) + 0.5) / w - 1) * w / h * 0.57735027 * 4.5
        y = (1 - 2 * (np.arange(h) + 0.5) / h) * 0.57735027 * 4.5
        x, y = np.broadcast_arrays(x[None, :], y[:, None])
        mask = np.zeros((h, w), dtype=bool)
        for center, a, b, hole in [
            (-0.95, (0.65, 0.18), (-0.22, 1.05), -1),
            (1, (0.65, 0), (0, 1.05), 0.55),
        ]:
            px = x - center
            det = a[0] * b[1] - a[1] * b[0]
            u = (px * b[1] - y * b[0]) / det
            v = (a[0] * y - a[1] * px) / det
            coverage = (abs(u) <= 1) & (abs(v) <= 1)
            if hole > 0:
                coverage &= (u * u + v * v) >= hole * hole
            mask |= coverage
        image = Image.fromarray(mask.astype("uint8") * 255)
        dilated = np.array(image.filter(ImageFilter.MaxFilter(33))) != 0
        edge = np.array(image.filter(ImageFilter.MaxFilter(3))) != 0
        halo = dilated & ~edge
        tf = np.array([0.6, 0.7, 0.8])
        tm = np.array([0.04, 0.05, 0.06])
        rear = np.array([1.2, 1.2, 1.2]) * 0.9 + 0.12 * 0.1
        middle = (1 - tm) * [0.035, 0.045, 0.055] + tm * rear
        expected = np.broadcast_to(
            (1 - tf) * [0.025, 0.035, 0.045] + tf * rear, (h, w, 3)
        ).copy()
        expected[mask] = (1 - tf) * [0.025, 0.035, 0.045] + tf * middle
        display(expected).save(out / "reference.png")
        darkness = np.maximum(expected - hdr, 0).max(axis=2)
        py, px = np.unravel_index(np.where(halo, darkness, -1).argmax(), halo.shape)
        box = (int(px) - 70, int(py) - 70, int(px) + 70, int(py) + 70)
    # Away from the silhouette, sorted over must match the same uniform layers.
    assert np.max(abs(hdr[~dilated] - expected[~dilated])) < 0.03, name
    darkness = np.maximum(expected - hdr, 0).max(axis=2)
    report.append(
        dict(
            case=name,
            halo_mean_darkening=float(darkness[halo].mean()),
            halo_p99_darkening=float(np.percentile(darkness[halo], 99)),
            halo_max_darkening=float(darkness[halo].max()),
            halo_pixels_over_point1=int(np.count_nonzero(halo & (darkness > 0.1))),
        )
    )
    print(report[-1], flush=True)
for name in ("no-zero", "fp16", "reverse"):
    report.append(
        dict(
            control=name,
            max_hdr_difference=float(abs(images[name] - images["min"]).max()),
        )
    )
canvas = Image.new("RGB", (4 * 420, 456), "#202020")
draw = ImageDraw.Draw(canvas)
for i, (name, label) in enumerate(
    [
        ("min", "1/8 LUT: XY min"),
        ("no-min", "1/8 LUT: no min"),
        ("fine-xy", "1/2 LUT: XY min"),
        ("reference", "Exact sorted over"),
    ]
):
    draw.text((420 * i + 10, 10), label, fill="white")
    canvas.paste(
        Image.open(out / (name + ".png"))
        .crop(box)
        .resize((420, 420), Image.Resampling.NEAREST),
        (420 * i, 36),
    )
canvas.save(out / "comparison.png")
(out / "report.json").write_text(
    json.dumps(dict(crop=box, measurements=report), indent=2)
)
print("Artifacts:", out)
