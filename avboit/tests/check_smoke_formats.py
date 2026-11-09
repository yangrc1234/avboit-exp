# SPDX-License-Identifier: MIT
"""Compare FP16/4-bit and single/dual-frame smoke at 2K with StablePowerState."""
import csv
import json
from pathlib import Path
import statistics
import subprocess
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[2]
out = root / "build/smoke-format-profile"
out.mkdir(exist_ok=True)
results = {}
for camera in (10, 11):
    for precision in ("half", "4bit"):
        for single in (False, True):
            name = f"cam{camera}-{precision}-" + ("single" if single else "dual")
            folder = out / name
            folder.mkdir(exist_ok=True)
            sequence = (
                root
                / "build/jangafx-smoke"
                / ("sequence128" if precision == "half" else "sequence128-4bit")
            )
            args = [
                str(root / "bin/avboit_viewer.exe"),
                "--sponza",
                "--stress",
                "24",
                "--smoke-sequence",
                str(sequence),
                "--smoke-frame",
                "60.5",
                "--camera-preset",
                str(camera),
                "--volume-step",
                "0.1",
                "--volume-half-res",
                "--volume-extinction",
                "6",
                "--no-smoke-mask",
                "--spot-intensity",
                "0",
                "--sun-intensity",
                "6",
                "--taa",
                "--accum-half",
                "--composition-half",
                "--render-width",
                "2560",
                "--render-height",
                "1440",
                "--benchmark",
                "60",
            ]
            if single:
                args.append("--smoke-single-frame")
            (folder / "command.json").write_text(json.dumps(args, indent=2))
            with (folder / "run.log").open("w") as log:
                subprocess.run(args, cwd=folder, stdout=log, stderr=subprocess.STDOUT, check=True)
            rows = list(
                csv.DictReader(
                    l
                    for l in (folder / "native-benchmark.csv").read_text().splitlines()
                    if not l.startswith("#")
                )
            )
            results[name] = {
                k: statistics.median(float(row[k]) for row in rows)
                for k in rows[0]
                if k != "frame_id"
            }
            im = Image.open(folder / "native-raster.ppm")
            im.save(folder / "scene.png")
            for field in ("numerator", "denominator", "total-tau"):
                with (folder / f"native-{field}.raw").open("rb") as f:
                    w, h = np.fromfile(f, "<u4", 2)
                    data = np.fromfile(f, "<f2").reshape(h, w, 4)[..., :3]
                assert np.isfinite(data).all() and data.min() >= 0, (name, field)
            print(name, round(results[name]["volume_accumulation_ms"], 3), flush=True)
            (out / "summary.json").write_text(json.dumps(results, indent=2))
# Compare quantization only; single vs dual also changes the sampled source time.
quality = {}
for camera in (10, 11):
    for mode in ("single", "dual"):
        a = out / f"cam{camera}-half-{mode}"
        b = out / f"cam{camera}-4bit-{mode}"
        with (a / "native-total-tau.raw").open("rb") as f:
            w, h = np.fromfile(f, "<u4", 2)
            mask = np.any(np.fromfile(f, "<f2").reshape(h, w, 4)[..., :3] > 0.001, axis=-1)
        delta = np.abs(
            np.asarray(Image.open(a / "scene.png")).astype(float)
            - np.asarray(Image.open(b / "scene.png")).astype(float)
        )[mask]
        quality[f"{camera}-{mode}"] = {
            "mean_255": float(delta.mean()),
            "p99_255": float(np.percentile(delta, 99)),
            "max_255": float(delta.max()),
        }
(out / "quality.json").write_text(json.dumps(quality, indent=2))
print(quality)
