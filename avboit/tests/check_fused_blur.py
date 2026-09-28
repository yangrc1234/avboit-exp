# SPDX-License-Identifier: MIT
"""2K fused Gaussian ROI/poison regression; optional pre-cleanup image baseline."""
import argparse, json, math, subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
p.add_argument(
    "--output", type=Path, default=Path(__file__).resolve().parents[2] / "build/fused-regression"
)
p.add_argument("--baseline", type=Path, help="Prior capture root with CASE-fused/native-raster.ppm")
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)


def capture(name, flags):
    folder = a.output / name
    folder.mkdir(exist_ok=True)
    with (folder / "run.log").open("w") as log:
        subprocess.run(
            [
                str(a.viewer.resolve()),
                "--headless",
                "--render-width",
                "2560",
                "--render-height",
                "1440",
                *flags,
            ],
            cwd=folder,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )
    return (folder / "native-raster.ppm").read_bytes().split(b"\n255\n", 1)[1]


def compare(x, y):
    assert len(x) == len(y)
    delta = [abs(i - j) for i, j in zip(x, y)]
    result = {"max": max(delta), "rms": math.sqrt(sum(v * v for v in delta) / len(delta))}
    assert result["max"] <= 3 and result["rms"] < 0.1, result
    return result


results = []
for name, flags in [
    ("smoke", ["--stress", "14"]),
    ("emissive", ["--stress", "10", "--emissive-balls"]),
    ("sparse", ["--stress", "10", "--sparse-blur"]),
    ("no-mirror", ["--stress", "14", "--no-mirror-blur"]),
    ("odd", ["--stress", "14", "--render-width", "2561", "--render-height", "1441"]),
]:
    fused = capture(name + "-fused", flags)
    # Full-chain/full-screen reference catches undersized ROI dependencies.
    full = capture(name + "-full", flags + ["--full-blur"])
    roi = compare(fused, full)
    poison = compare(fused, capture(name + "-poison", flags + ["--poison-resolve"]))
    result = {"case": name, "roi_vs_full": roi, "poison": poison}
    if a.baseline:
        reference = (
            (a.baseline / (name + "-fused") / "native-raster.ppm")
            .read_bytes()
            .split(b"\n255\n", 1)[1]
        )
        result["baseline"] = compare(reference, fused)
        assert result["baseline"]["max"] == 0, result
    print(result, flush=True)
    results.append(result)
(a.output / "results.json").write_text(json.dumps(results, indent=2))
print("PASS: 2K fused Gaussian ROI/poison parity, mirror, sparse, odd dimensions")
