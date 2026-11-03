# SPDX-License-Identifier: MIT
"""Validate integrated XY minimum, sparse reads, and glass image error."""
import json, subprocess
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from capture_utils import texture
from check_glass_silhouette import reference, display
from check_crossing_glass import geometry, exact_image

root = Path(__file__).resolve().parents[2]
out = root / "build/lut-min-filter"
out.mkdir(parents=True, exist_ok=True)
report = []
for scene in (27, 28, 17, 11, 13):
    images = {}
    luts = {}
    for name, flags in [
        ("ordinary", []),
        ("min", ["--lut-min-filter"]),
        ("poison", ["--lut-min-filter", "--poison-extinction"]),
        ("dense", ["--lut-min-filter", "--dense-extinction"]),
    ]:
        folder = out / f"{scene}-{name}"
        folder.mkdir(exist_ok=True)
        run = subprocess.run(
            [
                str(root / "bin/avboit_viewer.exe"),
                "--headless",
                "--lut-min-retain",
                "0",
                "--no-taa",
                "--volume-full-res",
                "--no-lut-min-filter",
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
            raise RuntimeError(str(folder))
        hdr = texture(folder / "native-composition.rgba16f", "<f2", 4)[..., :3].astype(
            "float32"
        )
        assert np.isfinite(hdr).all()
        images[name] = hdr
        display(hdr).save(folder / "image.png")
        raw = (folder / "native-lut.rgba8").read_bytes()
        w, h, d = map(int, np.frombuffer(raw[:12], "<u4"))
        luts[name] = np.frombuffer(raw[12:], "u1").reshape(d, h, w, 4)
        if name in ("ordinary", "min") and scene in (27, 28, 17):
            h, w = hdr.shape[:2]
            if scene == 17:
                _, masks, inside, near = geometry(0, w, h)
                expected = exact_image(masks, near)
                edge = inside
            else:
                expected, edge, inside = reference(w, h, 0, third_layer=scene == 28)
            err = abs(hdr - expected).max(axis=2)
            report.append(
                dict(
                    scene=scene,
                    mode=name,
                    edge_mean=float(err[edge].mean()),
                    interior_mean=float(err[inside].mean()),
                    interior_p99=float(np.percentile(err[inside], 99)),
                )
            )
    for mode in ("poison", "dense"):
        diff = float(abs(images["min"] - images[mode]).max())
        assert diff == 0, (scene, mode, diff)
        assert np.array_equal(luts["min"], luts[mode][: len(luts["min"])]), (
            scene,
            mode,
            "LUT",
        )
    assert np.all(np.diff(luts["min"][..., :3].astype("int16"), axis=0) <= 0), scene
    if scene in (27, 28):
        src = luts["ordinary"]
        pad = np.pad(src, ((0, 0), (1, 1), (1, 1), (0, 0)), mode="edge")
        neighbour_min = np.minimum.reduce(
            [
                pad[:, y : y + src.shape[1], x : x + src.shape[2]]
                for y in range(3)
                for x in range(3)
            ]
        )
        expected = np.minimum(src, np.maximum(neighbour_min, 16))
        assert np.array_equal(expected, luts["min"]), ("not bounded XY minimum", scene)
        canvas = Image.new("RGB", (1440, 516), "#202020")
        draw = ImageDraw.Draw(canvas)
        for i, (name, label) in enumerate(
            [("ordinary", "Ordinary"), ("min", "Integrated T: XY min")]
        ):
            draw.text((480 * i + 12, 10), label, fill="white")
            canvas.paste(
                display(images[name])
                .crop((460, 375, 660, 575))
                .resize((480, 480), Image.Resampling.NEAREST),
                (i * 480, 36),
            )
        expected, _, _ = reference(2560, 1440, 0, third_layer=scene == 28)
        draw.text((972, 10), "Exact sorted reference", fill="white")
        canvas.paste(
            display(expected)
            .crop((460, 375, 660, 575))
            .resize((480, 480), Image.Resampling.NEAREST),
            (960, 36),
        )
        canvas.save(out / f"{scene}-comparison.png")
    print("validated scene", scene, flush=True)
(out / "report.json").write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
