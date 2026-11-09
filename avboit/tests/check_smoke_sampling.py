# SPDX-License-Identifier: MIT
"""2K reference comparison for voxel-aware density taps and low-T lighting."""
from pathlib import Path
import argparse
import json
import subprocess
import numpy as np
from PIL import Image
from capture_utils import texture

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--reuse", action="store_true", help="Inspect existing captures")
options = parser.parse_args()
root = Path(__file__).resolve().parents[2]
output = root / "build/smoke-sampling"
report = {}
for name, frame, step, extinction, scene in (
    ("dense", 15.5, 0.1, 8, 24),
    ("high-ext", 15.5, 0.1, 64, 24),
    ("coarse", 15.5, 0.5, 8, 24),
    ("frost", 3.5, 0.1, 8, 25),
):
    images, taus, luts = [], [], []
    for reference in (True, False):
        folder = output / f"{name}-{int(reference)}"
        folder.mkdir(parents=True, exist_ok=True)
        args = [
            str(root / "bin/avboit_viewer.exe"),
            "--headless",
            "--sponza",
            "--stress",
            str(scene),
            "--baked-smoke",
            "--smoke-frame",
            str(frame),
            "--volume-step",
            str(step),
            "--volume-extinction",
            str(extinction),
            "--no-taa",
            "--volume-half-res",
            "--accum-half",
            "--render-width",
            "2560",
            "--render-height",
            "1440",
        ]
        if reference:
            args.append("--volume-reference-sampling")
        if not options.reuse:
            run = subprocess.run(args, cwd=folder, capture_output=True, timeout=180)
            (folder / "run.log").write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, (name, reference)
        image = Image.open(folder / "native-raster.ppm")
        image.save(folder / "scene.png")
        images.append(np.asarray(image).astype(np.float32))
        for field in ("numerator", "denominator", "total-tau", "back-numerator"):
            data = texture(folder / f"native-{field}.raw", "<f2", 4)[..., :3]
            assert np.isfinite(data).all() and data.min() >= 0, (name, field)
            if field == "total-tau":
                taus.append(data.astype(np.float32))
        luts.append((folder / "native-lut.rgba8").read_bytes())
    assert luts[0] == luts[1], "Accumulation optimization changed extinction/LUT"
    # Evaluate covered pixels rather than diluting error with untouched opaque sky.
    mask = np.any(taus[0] > 0, axis=-1) | np.any(taus[1] > 0, axis=-1)
    difference = np.abs(images[0] - images[1])[mask]
    p99 = float(np.percentile(difference, 99))
    assert p99 <= 4, (name, "Covered-pixel p99 exceeds 4/255", p99)
    report[name] = dict(
        covered_fraction=float(mask.mean()),
        mean_255=float(difference.mean()),
        p99_255=p99,
        max_255=float(difference.max()),
    )
(output / "quality.json").write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
