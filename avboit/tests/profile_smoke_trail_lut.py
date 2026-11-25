# SPDX-License-Identifier: MIT
"""Compare the capsule candidate index to exhaustive analytic evaluation at 2K."""
import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess

import numpy as np
from PIL import Image


def run(exe, folder, shots, radius, reference=False, rebuild=False, camera=0):
    folder.mkdir(parents=True, exist_ok=True)
    command = [str(exe), "--benchmark", "60", "--sponza", "--stress", "24", "--gameplay-grenade",
               "--volume-step", ".5", "--volume-extinction", "6", "--sun-intensity", "6",
               "--spot-intensity", "0", "--taa", "--render-width", "2560", "--render-height", "1440",
               "--time", "4", "--smoke-shots", str(shots), "--smoke-shot-radius", str(radius),
               "--camera-preset", str(camera)]
    if reference:
        command.append("--smoke-trail-reference")
    if rebuild:
        command.append("--smoke-trail-rebuild")
    with (folder / "capture.log").open("w") as log:
        subprocess.run(command, cwd=folder, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    with (folder / "native-benchmark.csv").open() as stream:
        rows = list(csv.DictReader(line for line in stream if not line.startswith("#")))
    timing = {key: statistics.median(float(row[key]) for row in rows) for key in
              ["frame_ms", "shadow_ms", "extinction_splat_ms", "volume_accumulation_ms", "smoke_trail_index_ms"]}
    Image.open(folder / "native-raster.ppm").save(folder / "preview.png")
    print(folder.name, timing, flush=True)
    return timing


def compare(reference, indexed):
    a = np.asarray(Image.open(reference / "native-raster.ppm")).astype(np.float32)
    b = np.asarray(Image.open(indexed / "native-raster.ppm")).astype(np.float32)
    result = {"image_max_difference": float(abs(a - b).max())}
    # No density representation changed: the same analytic result is expected.
    for name in ["native-total-tau.raw", "native-denominator.raw", "native-numerator.raw"]:
        result[name] = (reference / name).read_bytes() == (indexed / name).read_bytes()
    for slice_index in range(3):
        name = f"native-light-tau-{slice_index}.f16"
        if (reference / name).exists():
            result[name] = (reference / name).read_bytes() == (indexed / name).read_bytes()
    print("comparison", result, flush=True)
    if result["image_max_difference"] > 1 or not all(v for k, v in result.items() if k != "image_max_difference"):
        raise AssertionError(f"Capsule index changed the analytic result: {result}")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[2]
    parser.add_argument("--exe", type=Path, default=root / "bin/avboit_viewer.exe")
    parser.add_argument("--output", type=Path, default=root / "build/smoke-trail-lut")
    args = parser.parse_args()
    result = {}
    for name, count, radius, camera in [("fifteen", 15, .45, 0), ("one", 1, .45, 0),
                                        ("none", 0, .45, 0), ("thin", 15, .01, 0),
                                        ("near", 15, .45, 11)]:
        ref, lut = args.output / (name + "-reference"), args.output / (name + "-lut")
        result[name] = {"reference": run(args.exe, ref, count, radius, True, camera=camera),
                        "lut": run(args.exe, lut, count, radius, camera=camera)}
        result[name]["comparison"] = compare(ref, lut)
        (args.output / "summary.json").write_text(json.dumps(result, indent=2))
    result["rebuild"] = run(args.exe, args.output / "fifteen-rebuild", 15, .45, rebuild=True)
    compare(args.output / "fifteen-reference", args.output / "fifteen-rebuild")
    (args.output / "summary.json").write_text(json.dumps(result, indent=2))
