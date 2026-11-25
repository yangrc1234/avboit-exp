# SPDX-License-Identifier: MIT
"""2K same-density dense/buffer/atlas GPU comparison, with independent brick skip."""
import csv
import json
from pathlib import Path
import statistics
import subprocess
import sys
import numpy as np
from PIL import Image
from capture_utils import texture

root = Path(__file__).resolve().parents[2]
out = root / (
    "build/grenade-storage-far"
    if "--far-open" in sys.argv
    else "build/grenade-storage-coarse" if "--coarse" in sys.argv else "build/grenade-storage"
)
out.mkdir(exist_ok=True)
summary = {}
for camera in (4,) if "--far-open" in sys.argv else (0,) if "--coarse" in sys.argv else (0, 11, 4):
    reference = None
    for mode, skip in (
        (0, False),
        (1, False),
        (2, False),
        (0, True),
        (1, True),
        (2, True),
        (0, False),
    ):
        label = f"cam{camera}-mode{mode}-skip{int(skip)}"
        if label in summary:
            label += "-repeat"
        folder = out / label
        folder.mkdir(exist_ok=True)
        args = [
            str(root / "bin/avboit_viewer.exe"),
            "--sponza",
            "--stress",
            "24",
            "--gameplay-grenade",
            "--grenade-storage",
            str(mode),
            "--camera-preset",
            str(camera),
            "--volume-step",
            "0.1",
            "--volume-extinction",
            "6",
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
        if "--far-open" in sys.argv:
            args.remove("--sponza")
        if "--coarse" in sys.argv:
            args[args.index("--volume-step") + 1] = "0.5"
        if skip:
            args.append("--grenade-skip")
        (folder / "command.json").write_text(json.dumps(args, indent=2))
        with (folder / "run.log").open("w") as log:
            subprocess.run(args, cwd=folder, stdout=log, stderr=subprocess.STDOUT, check=True)
        lines = (folder / "native-benchmark.csv").read_text().splitlines()
        assert "stable_power_state=1" in lines[0]
        rows = list(csv.DictReader(line for line in lines if not line.startswith("#")))
        result = {
            k: statistics.median(float(r[k]) for r in rows) for k in rows[0] if k != "frame_id"
        }
        times = [float(r["volume_accumulation_ms"]) for r in rows]
        result["p10_ms"], result["p90_ms"] = np.percentile(times, [10, 90]).tolist()
        arrays = {}
        for name in ("numerator", "denominator", "total-tau", "back-numerator"):
            a = texture(folder / f"native-{name}.raw", "<f2", 4)[..., :3].astype(np.float32)
            assert np.isfinite(a).all() and a.min() >= 0
            arrays[name] = a
        pic = Image.open(folder / "native-raster.ppm")
        pic.save(folder / "preview.png")
        rgb = np.asarray(pic, dtype=np.float32)
        if reference is None:
            reference = (arrays, rgb)
        else:
            delta = np.abs(rgb - reference[1])
            result["image_mae_255"] = float(delta.mean())
            result["image_p99_255"] = float(np.percentile(delta, 99))
            result["image_max_255"] = float(delta.max())
            for key, a in arrays.items():
                d = np.abs(a - reference[0][key])
                result[key + "_mae"] = float(d.mean())
                result[key + "_max"] = float(d.max())
        result["gpu_storage"] = [
            line
            for line in (folder / "run.log").read_text().splitlines()
            if "Gameplay GPU mode" in line
        ]
        summary[label] = result
        (out / "summary.json").write_text(json.dumps(summary, indent=2))
        print(
            label,
            round(result["volume_accumulation_ms"], 4),
            "image MAE",
            result.get("image_mae_255", 0),
            flush=True,
        )
        for file in folder.iterdir():
            if file.name.startswith("native-") and file.suffix not in (".csv", ".ppm", ".json"):
                file.unlink()
