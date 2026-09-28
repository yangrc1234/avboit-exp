# SPDX-License-Identifier: MIT
"""2K tile culling parity and disconnected B/Gaussian source domains."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image
from capture_utils import difference, texture, work_domain


def main():
    native = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=native / "build/tile-pipeline-check")
    args = parser.parse_args()
    results = {}
    fixtures = [
        ("separated", ["--sponza", "--stress", "15", "--emissive-balls"]),
        ("smoke", ["--sponza", "--stress", "14", "--time", ".7"]),
        ("rgb", ["--stress", "13"]),
        (
            "aligned-sphere",
            ["--glass-sphere", "--rgb-glass", "--render-width", "2561", "--render-height", "1441"],
        ),
    ]
    for name, flags in fixtures:
        images = {}
        for variant, extra in [
            ("tile", []),
            ("unculled", ["--no-tile-resolve"]),
            ("full", ["--full-blur"]),
        ]:
            folder = args.output / name / variant
            folder.mkdir(parents=True, exist_ok=True)
            with (folder / "run.log").open("w") as log:
                subprocess.run(
                    [
                        str(native / "bin/avboit_viewer.exe"),
                        "--headless",
                        "--poison-resolve",
                        *flags,
                        *extra,
                    ],
                    cwd=folder,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    check=True,
                )
            images[variant] = np.asarray(Image.open(folder / "native-raster.ppm")).astype("f4")
        metrics = {key: difference(images["tile"], images[key]) for key in ("unculled", "full")}
        assert all(m["max"] == 0 for m in metrics.values()), (name, metrics)
        folder = args.output / name / "tile"
        tiles = texture(folder / "native-screen-tiles.r32u", "<u4", 1)[..., 0]
        metrics["active_resolve_tile_fraction"] = float(((tiles & 4) != 0).mean())
        rects = np.fromfile(folder / "native-regions.u32", "<u4").reshape(11, 4)
        for stage in (1, 3, 5, 10):
            mask = work_domain(folder, stage)
            x0, y0, x1, y1 = map(int, rects[stage])
            metrics[f"stage{stage}"] = dict(
                pixels=int(mask.sum()), envelope_pixels=(x1 - x0) * (y1 - y0), full_pixels=mask.size
            )
            # Mask preview is useful for reviewing the empty gap between panes.
            if name == "separated":
                Image.fromarray((mask * 255).astype("uint8")).save(
                    folder / f"work-stage-{stage}.png"
                )
        if name == "separated":
            assert metrics["stage10"]["pixels"] < metrics["stage10"]["envelope_pixels"], metrics
            assert metrics["active_resolve_tile_fraction"] < 1, metrics
        results[name] = metrics
        print(name, json.dumps(metrics), flush=True)
    (args.output / "results.json").write_text(json.dumps(results, indent=2))
    print("PASS: tile/unculled/full image parity; disconnected source work")


if __name__ == "__main__":
    main()
