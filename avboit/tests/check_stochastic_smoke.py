# SPDX-License-Identifier: MIT
"""2K smoke STF timing, deterministic-LUT checks and matched moving-camera captures."""
import csv
import json
from pathlib import Path
import statistics
import subprocess
import numpy as np
from PIL import Image
from capture_utils import texture

root = Path(__file__).resolve().parents[2]
out = root / "build/stochastic-smoke"
out.mkdir(exist_ok=True)
sequence = root / "build/jangafx-smoke/sequence128-4bit"
if not (sequence / "sequence.txt").exists():
    raise SystemExit("Prepare the JangaFX 4-bit sequence; see docs/BAKED_SMOKE.zh-CN.md")
summary = {}


def capture(name, camera, stochastic, temporal=False):
    folder = out / name
    folder.mkdir(exist_ok=True)
    args = [
        str(root / "bin/avboit_viewer.exe"),
        "--sponza",
        "--stress",
        "24",
        "--smoke-sequence",
        str(sequence),
        "--smoke-frame",
        "60.5",
        "--smoke-single-frame",
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
    ]
    args += ["--volume-upsample-test"] if temporal else ["--benchmark", "60"]
    if stochastic:
        args += ["--smoke-stochastic-filter"]
    (folder / "command.json").write_text(json.dumps(args, indent=2))
    with (folder / "run.log").open("w") as log:
        subprocess.run(args, cwd=folder, stdout=log, stderr=subprocess.STDOUT, check=True)
    for field in ("numerator", "denominator", "total-tau", "back-numerator"):
        data = texture(folder / f"native-{field}.raw", "<f2", 4)[..., :3]
        assert np.isfinite(data).all() and data.min() >= 0, (name, field)
    Image.open(folder / "native-raster.ppm").save(folder / "scene.png")
    if not temporal:
        rows = list(
            csv.DictReader(
                line
                for line in (folder / "native-benchmark.csv").read_text().splitlines()
                if not line.startswith("#")
            )
        )
        summary[name] = {
            k: statistics.median(float(r[k]) for r in rows) for k in rows[0] if k != "frame_id"
        }
        print(name, summary[name]["volume_accumulation_ms"], flush=True)
    return folder


def compare(a, b):
    # These stages must remain entirely deterministic and unaffected by STF.
    for pattern in ("native-lut.rgba8", "native-light-moments-*.unorm16", "native-light-tau-*.f16"):
        files = list(a.glob(pattern))
        assert files, pattern
        for path in files:
            assert path.read_bytes() == (b / path.name).read_bytes(), path.name
    mask = texture(a / "native-total-tau.raw", "<f2", 4)[..., :3].max(2) > 0.001
    ref = np.asarray(Image.open(a / "scene.png"), dtype=float)[mask]
    test = np.asarray(Image.open(b / "scene.png"), dtype=float)[mask]
    delta = test - ref
    return {
        "mean_signed_255": float(delta.mean()),
        "mae_255": float(abs(delta).mean()),
        "p99_255": float(np.percentile(abs(delta), 99)),
        "max_255": float(abs(delta).max()),
    }


for camera in (10, 11):
    a = capture(f"cam{camera}-trilinear", camera, False)
    b = capture(f"cam{camera}-stochastic", camera, True)
    summary[f"cam{camera}-image"] = compare(a, b)
    (out / "summary.json").write_text(json.dumps(summary, indent=2))
a = capture("motion-trilinear", 10, False, True)
b = capture("motion-stochastic", 10, True, True)
for phase, indices in (("static-camera", range(8)), ("moving-camera", range(8, 16))):
    pairs = []
    for frame in indices:
        name = f"volume-upscale-bilateral-{frame:02}.ppm"
        ref = np.asarray(Image.open(a / name), dtype=float)
        test = np.asarray(Image.open(b / name), dtype=float)
        pairs.append(test - ref)
    delta = np.stack(pairs)
    summary[phase] = {
        "mae_255": float(abs(delta).mean()),
        "p99_255": float(np.percentile(abs(delta), 99)),
        "mean_signed_255": float(delta.mean()),
    }
for folder in (a, b):
    for path in folder.glob("volume-upscale-bilateral-*-*.rgba16f"):
        assert np.isfinite(texture(path, "<f2", 4)).all(), path
(out / "summary.json").write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
