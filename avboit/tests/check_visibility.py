# SPDX-License-Identifier: MIT
"""2K sky culling and foreground emissive rejection before frost filtering."""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image
from capture_utils import difference, texture, work_domain


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=root / "build/visibility-after")
    args = parser.parse_args()
    results = {}

    def run(name, flags):
        folder = args.output / name
        folder.mkdir(parents=True, exist_ok=True)
        with (folder / "run.log").open("w") as log:
            subprocess.run(
                [str(root / "bin/avboit_viewer.exe"), "--headless", *flags],
                cwd=folder,
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
            )
        return folder, np.asarray(Image.open(folder / "native-raster.ppm")).astype("f4")

    sky, image = run(
        "sky", ["--sponza", "--stress", "10", "--camera-preset", "6", "--benchmark", "90"]
    )
    mask = texture(sky / "native-screen-tiles.r32u", "<u4", 1)
    assert not mask.any(), "Off-frustum bounds still request full-screen work"
    for stage in (1, 3, 5, 10):
        assert not work_domain(sky, stage).any(), ("Empty sky still dispatches stage", stage)
    _, unculled = run(
        "sky-unculled", ["--sponza", "--stress", "10", "--camera-preset", "6", "--no-tile-resolve"]
    )
    results["sky_vs_unculled"] = difference(image, unculled)
    assert results["sky_vs_unculled"]["max"] == 0, results

    for mode in ("packed", "half-composition"):
        flags = ["--stress", "9", "--hardware-depth", "--emissive-front", "--poison-resolve"]
        if mode == "half-composition":
            flags += ["--composition-half"]
        off, dark = run(mode + "-front0", [*flags, "--emissive-gain", "0"])
        on, bright = run(mode + "-front1", [*flags, "--emissive-gain", "1"])
        hit = texture(on / "native-interface.rgba32f", "<f4", 4)
        device = texture(on / "native-depth.r32f", "<f4", 1)[..., 0]
        opaque_z = (-4.0 / 79.95) / (device - 80.0 / 79.95)
        covered = hit[..., 3] > 0
        visible = covered & (hit[..., 0] < opaque_z - 0.01)
        hidden = covered & (hit[..., 0] > opaque_z + 0.01)
        assert visible.any() and hidden.any()
        diff = np.abs(bright - dark)
        assert diff.max() > 50, "Foreground emission was accidentally disabled"
        # Every bright sphere lies inside this frost pane's footprint.
        # Changing only foreground radiance must not change visible frost.
        assert diff[visible].max() == 0, (mode, float(diff[visible].max()))
        background = texture(on / "native-background.rgba16f", "<f2", 4)
        valid = work_domain(on, 10)
        assert not background[hidden & valid].any(), "Occluded frost source must be RGBA zero"
        results[mode] = dict(
            visible_glass_pixels=int(visible.sum()),
            rejected_source_pixels=int((hidden & valid).sum()),
            max_glass_difference=float(diff[visible].max()),
        )
        print(mode, results[mode], flush=True)

    # Counterexample: actual background emission must remain visible through frost.
    _, dark = run(
        "behind0", ["--stress", "9", "--hardware-depth", "--emissive-balls", "--emissive-gain", "0"]
    )
    path, bright = run(
        "behind1", ["--stress", "9", "--hardware-depth", "--emissive-balls", "--emissive-gain", "1"]
    )
    hit = texture(path / "native-interface.rgba32f", "<f4", 4)
    assert (
        np.abs(bright - dark)[hit[..., 3] > 0].max() > 30
    ), "Background emission incorrectly rejected"
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "results.json").write_text(json.dumps(results, indent=2))
    print("PASS: empty sky has no B/blur work; foreground radiance does not contaminate frost")


if __name__ == "__main__":
    main()
