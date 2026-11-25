# SPDX-License-Identifier: MIT
"""Offline 2K accumulation ablations. Mutates no scene or material defaults."""
import csv
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
import numpy as np
from PIL import Image
from capture_utils import texture

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/accumulation-audit"
OUT.mkdir(exist_ok=True)
SEQUENCE = ROOT / "build/jangafx-smoke/sequence128-4bit"
if not (SEQUENCE / "sequence.txt").is_file():
    raise SystemExit("Prepare the JangaFX 4-bit sequence; see docs/BAKED_SMOKE.zh-CN.md")
summary = (
    json.loads((OUT / "summary.json").read_text())
    if any(x in sys.argv for x in ("--extra", "--coarse"))
    else {}
)
references = (
    {camera: summary[f"cam{camera}-baseline"]["hashes"] for camera in (10, 11)} if summary else {}
)


def capture(camera, label, audit=0, extra=()):
    folder = OUT / f"cam{camera}-{label}"
    folder.mkdir(exist_ok=True)
    args = [
        str(ROOT / "bin/avboit_viewer.exe"),
        "--sponza",
        "--stress",
        "24",
        "--smoke-sequence",
        str(SEQUENCE),
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
        "--benchmark",
        "60",
        "--no-volume-quad-t",  # Audit PSOs use the per-ray reference path.
        "--accumulation-audit",
        str(audit),
        *extra,
    ]
    (folder / "command.json").write_text(json.dumps(args, indent=2))
    with (folder / "run.log").open("w") as log:
        subprocess.run(args, cwd=folder, stdout=log, stderr=subprocess.STDOUT, check=True)
    rows = list(
        csv.DictReader(
            line
            for line in (folder / "native-benchmark.csv").read_text().splitlines()
            if not line.startswith("#")
        )
    )
    result = {
        k: statistics.median(float(row[k]) for row in rows) for k in rows[0] if k != "frame_id"
    }
    values = [float(row["volume_accumulation_ms"]) for row in rows]
    result["p10_ms"], result["p90_ms"] = np.percentile(values, [10, 90]).tolist()
    hashes = {}
    for field in ("numerator", "denominator", "total-tau", "back-numerator"):
        path = folder / f"native-{field}.raw"
        data = texture(path, "<f2", 4)[..., :3]
        assert np.isfinite(data).all() and data.min() >= 0, (label, field)
        hashes[field] = hashlib.sha256(path.read_bytes()).hexdigest()
    hashes["lut"] = hashlib.sha256((folder / "native-lut.rgba8").read_bytes()).hexdigest()
    result["hashes"] = hashes
    if label == "baseline":
        references[camera] = hashes
    else:
        ref = references[camera]
        assert hashes["lut"] == ref["lut"], "Audit must not change extinction/integration"
        if label in (
            "no-lighting",
            "no-msm",
            "no-opaque-shadow",
            "no-shadows",
            "cheap-msm-solve",
            "no-mask",
            "baseline-repeat",
            "no-spot-math",
        ):
            assert hashes["total-tau"] == ref["total-tau"], "Density must stay identical"
            assert hashes["denominator"] == ref["denominator"], "March/T must stay identical"
        if label in ("no-mask", "baseline-repeat"):
            assert hashes == ref, "All-one mask removal must be output-identical"
    if label == "coarse-no-lighting":
        coarse = summary[f"cam{camera}-coarse-baseline"]["hashes"]
        for field in ("lut", "total-tau", "denominator"):
            assert (
                hashes[field] == coarse[field]
            ), "Coarse lighting audit must preserve march results"
    if label == "no-spot-math":
        # Removing zero arithmetic may reassociate FP operations; check images
        # within rounding tolerance, while tau/denominator remain bit-identical.
        a = np.asarray(Image.open(OUT / f"cam{camera}-baseline/native-raster.ppm"), dtype=float)
        b = np.asarray(Image.open(folder / "native-raster.ppm"), dtype=float)
        result["image_mae_255"] = float(abs(a - b).mean())
        result["image_max_255"] = float(abs(a - b).max())
        assert result["image_mae_255"] < 0.001 and result["image_max_255"] <= 1
    summary[f"cam{camera}-{label}"] = result
    (OUT / "summary.json").write_text(json.dumps(summary, indent=2))
    print(camera, label, round(result["volume_accumulation_ms"], 4), flush=True)
    # Captures are large. Preserve commands, timestamps, logs, image and checksums.
    for path in folder.iterdir():
        if path.name.startswith("native-") and path.suffix not in (".csv", ".ppm", ".json"):
            path.unlink()


for camera in (10, 11):
    if "--coarse" in sys.argv:
        for label, audit in (
            ("coarse-baseline", 0),
            ("coarse-no-lighting", 1),
            ("coarse-one-density-tap", 8),
        ):
            capture(camera, label, audit, ("--volume-step", "0.5"))
        continue
    if "--extra" in sys.argv:
        for label, audit in (("no-shadows", 6), ("no-spot-math", 128), ("cheap-msm-solve", 256)):
            capture(camera, label, audit)
        continue
    capture(camera, "baseline")
    for label, audit in (
        ("no-lighting", 1),
        ("no-msm", 2),
        ("no-opaque-shadow", 4),
        ("one-density-tap", 8),
        ("no-mask", 16),
        ("constant-density-unlit", 33),
    ):
        capture(camera, label, audit)
    capture(camera, "stochastic", extra=("--smoke-stochastic-filter",))
    capture(camera, "no-brick-skip", extra=("--no-smoke-brick-skip",))
    capture(camera, "fixed-unlit", 1, ("--volume-step-scale", "1"))
    capture(camera, "fixed-unlit-no-march-t", 65, ("--volume-step-scale", "1"))
    capture(camera, "baseline-repeat")
