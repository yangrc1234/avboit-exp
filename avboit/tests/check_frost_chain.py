# SPDX-License-Identifier: MIT
"""Fixed-screen frost: cross-resolution scale, quality and source-region coverage.

Main reference is 2560x1440. 1080p/2160p explicitly test resolution invariance.
Requires numpy and Pillow; output images and per-case logs remain reviewable.
"""
import argparse, json, subprocess
from pathlib import Path
import numpy as np
from PIL import Image
from capture_utils import texture, difference, work_domain

p = argparse.ArgumentParser()
p.add_argument(
    "--output", type=Path, default=Path(__file__).resolve().parents[2] / "build/frost-chain-check"
)
a = p.parse_args()
native = Path(__file__).resolve().parents[2]
results = {}


def run(name, width=2560, height=1440, count=3, flags=()):
    folder = a.output / name
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / "run.log").open("w") as log:
        subprocess.run(
            [
                str(native / "bin/avboit_viewer.exe"),
                "--headless",
                "--render-width",
                str(width),
                "--render-height",
                str(height),
                "--stress",
                "10",
                "--emissive-balls",
                "--refraction-gain",
                "0",
                "--frost-mips",
                str(count),
                "--poison-resolve",
                *flags,
            ],
            cwd=folder,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )
    base = texture(folder / "native-frost-base.rgba16f", "<f2", 4).astype(np.float32)
    assert base.shape == (45 * 2 ** (count - 1), 80 * 2 ** (count - 1), 4), (name, base.shape)
    rect = np.fromfile(folder / "native-regions.u32", "<u4").reshape(11, 4)
    valid = base[work_domain(folder, 1)]
    assert valid.size and np.isfinite(valid).all() and valid[..., 3].max() <= 1.001, (
        name,
        "invalid first mip",
    )
    img = np.asarray(Image.open(folder / "native-raster.ppm")).astype(np.float32)
    results[name] = {
        "base_size": [base.shape[1], base.shape[0]],
        "source_pixels": int(work_domain(folder, 10).sum()),
    }
    print(name, results[name], flush=True)
    return img, base, rect


for count in (3, 4, 5):
    images = {}
    for w, h in ((1920, 1080), (2560, 1440), (3840, 2160)):
        images[h] = run(f"mips{count}-{h}", w, h, count)
    reference, base, rect = images[1440]
    full, _, _ = run(f"mips{count}-full", count=count, flags=["--full-blur"])
    metric = difference(reference, full)
    assert metric["max"] == 0, metric
    results[f"mips{count}-roi-vs-full"] = metric
    assert (rect[10, 2:] > rect[10, :2]).all(), rect[10]
    for h in (1080, 2160):
        small = Image.fromarray(images[h][0].astype("uint8")).resize(
            (1280, 720), Image.Resampling.BOX
        )
        ref = Image.fromarray(reference.astype("uint8")).resize((1280, 720), Image.Resampling.BOX)
        results[f"mips{count}-{h}-screen-diff"] = difference(
            np.asarray(small).astype("f4"), np.asarray(ref).astype("f4")
        )

for name, flags in [
    ("dense", ["--dense-blur"]),
    ("smoke", ["--stress", "14", "--time", ".7"]),
    ("rgb", ["--stress", "13"]),
    ("moving", ["--camera-preset", "2"]),
]:
    reference, _, _ = run(name, flags=flags)
    full, _, _ = run(name + "-full", flags=[*flags, "--full-blur"])
    metric = difference(reference, full)
    assert metric["max"] == 0, (name, metric)
    results[name + "-vs-full"] = metric
(a.output / "results.json").write_text(json.dumps(results, indent=2))
print("PASS: fixed-screen sizes, cached source B, ROI and dense reference")
