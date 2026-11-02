# SPDX-License-Identifier: MIT
"""Compare conservative surface splats, including seams and sparse-write safety.

This is a quality experiment: report errors against sorted layers, rather than
asserting that conservative coverage improves every region.
"""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from capture_utils import texture
from check_glass_silhouette import display, reference


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", type=Path, default=root / "build/conservative-splat"
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    captures = {}
    for scene in (27, 28, 17):
        for mode, flags in (
            ("ordinary", []),
            ("conservative", ["--conservative-splat"]),
            ("poison", ["--conservative-splat", "--poison-extinction"]),
            ("dense", ["--conservative-splat", "--dense-extinction"]),
        ):
            folder = args.output / f"{scene}-{mode}"
            folder.mkdir(exist_ok=True)
            run = subprocess.run(
                [
                    str(root / "bin/avboit_viewer.exe"),
                    "--headless",
                    "--stress",
                    str(scene),
                    "--render-width",
                    "2560",
                    "--render-height",
                    "1440",
                    "--accum-half",
                    "--composition-half",
                    *flags,
                ],
                cwd=folder,
                capture_output=True,
            )
            (folder / "run.log").write_bytes(run.stdout + run.stderr)
            if run.returncode:
                raise RuntimeError(f"Capture failed: {folder}")
            hdr = texture(folder / "native-composition.rgba16f", "<f2", 4)[
                ..., :3
            ].astype("float32")
            assert np.isfinite(hdr).all(), str(folder)
            captures[scene, mode] = hdr
            display(hdr).save(folder / "image.png")
            if scene not in (27, 28):
                continue
            h, w = hdr.shape[:2]
            expected, edge, interior = reference(w, h, 0, third_layer=scene == 28)
            error = abs(hdr - expected).max(axis=2)
            # Inspect actual overlap, including internal triangle diagonals.
            ys, xs = np.indices((h, w))
            regions = {
                "diagonal_edge": edge & (xs < w * 0.42),
                "cutout_edge": edge & (xs >= w * 0.42) & (xs < w * 0.65),
                "thin_strips": edge & (xs >= w * 0.65),
                "interior": interior,
            }
            # Outside the front silhouette: two remaining layers in scene 28.
            # Bound the measured area well inside the rear pane's own silhouette.
            sx = (2 * (xs + 0.5) / w - 1) * w / h * 0.57735027
            sy = (1 - 2 * (ys + 0.5) / h) * 0.57735027
            rear = (abs(sx * 6) < 4.6) & (abs(sy * 6) < 1.9)
            # Reference colors have distinct values inside and outside front glass.
            outside = rear & (
                abs(expected[..., 0] - expected[h // 2, w // 2, 0]) < 1e-6
            )
            outside = (
                np.array(
                    Image.fromarray(outside.astype("uint8") * 255).filter(
                        ImageFilter.MinFilter(3)
                    )
                )
                != 0
            )
            regions["outside_front"] = outside
            rows.append(
                dict(
                    scene=scene,
                    mode=mode,
                    regions={
                        name: dict(
                            mean=float(error[mask].mean()),
                            p99=float(np.percentile(error[mask], 99)),
                            maximum=float(error[mask].max()),
                        )
                        for name, mask in regions.items()
                        if mask.any()
                    },
                )
            )
            if mode == "ordinary":
                display(expected).save(args.output / f"{scene}-reference.png")
        for mode in ("poison", "dense"):
            difference = float(
                abs(captures[scene, mode] - captures[scene, "conservative"]).max()
            )
            rows.append(dict(scene=scene, control=mode, max_hdr_difference=difference))
            assert (
                difference == 0
            ), f"Sparse coverage disagrees: scene {scene}, {mode}: {difference}"
        if scene in (27, 28):
            # Show a diagonal-pane corner and the internal diagonal separately.
            for title, box in (
                ("edge", (460, 375, 660, 575)),
                ("seam", (400, 640, 700, 940)),
            ):
                canvas = Image.new("RGB", (1440, 516), "#202020")
                label = ImageDraw.Draw(canvas)
                for column, (name, path) in enumerate(
                    (
                        ("Ordinary splat", args.output / f"{scene}-ordinary/image.png"),
                        (
                            "Conservative splat",
                            args.output / f"{scene}-conservative/image.png",
                        ),
                        (
                            "Exact sorted reference",
                            args.output / f"{scene}-reference.png",
                        ),
                    )
                ):
                    label.text((column * 480 + 12, 10), name, fill="white")
                    canvas.paste(
                        Image.open(path)
                        .crop(box)
                        .resize((480, 480), Image.Resampling.NEAREST),
                        (column * 480, 36),
                    )
                canvas.save(args.output / f"{scene}-{title}.png")
    (args.output / "report.json").write_text(
        json.dumps(rows, indent=2), encoding="utf-8"
    )
    print(json.dumps(rows, indent=2))


if __name__ == "__main__":
    main()
