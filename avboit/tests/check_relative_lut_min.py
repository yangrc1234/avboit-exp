# SPDX-License-Identifier: MIT
"""Compare relative min limits against sorted references and sparse/zero invariants."""
from pathlib import Path
import subprocess, json
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from capture_utils import texture
from check_glass_silhouette import reference, display

root = Path(__file__).resolve().parents[2]
out = root / "build/relative-lut-min"
out.mkdir(parents=True, exist_ok=True)
report = []


def three_layer_reference(w, h):
    x = (2 * (np.arange(w) + 0.5) / w - 1) * w / h * 0.57735027 * 4.5
    y = (1 - 2 * (np.arange(h) + 0.5) / h) * 0.57735027 * 4.5
    x, y = np.broadcast_arrays(x[None, :], y[:, None])
    mask = np.zeros((h, w), bool)
    for center, a, b, hole in [
        (-0.95, (0.65, 0.18), (-0.22, 1.05), -1),
        (1, (0.65, 0), (0, 1.05), 0.55),
    ]:
        px = x - center
        det = a[0] * b[1] - a[1] * b[0]
        u = (px * b[1] - y * b[0]) / det
        v = (a[0] * y - a[1] * px) / det
        coverage = (abs(u) <= 1) & (abs(v) <= 1)
        if hole > 0:
            coverage &= u * u + v * v >= hole * hole
        mask |= coverage
    image = Image.fromarray(mask.astype("uint8") * 255)
    halo = (np.asarray(image.filter(ImageFilter.MaxFilter(33))) != 0) & (
        np.asarray(image.filter(ImageFilter.MaxFilter(3))) == 0
    )
    inside = np.asarray(image.filter(ImageFilter.MinFilter(25))) != 0
    tf = np.array([0.6, 0.7, 0.8])
    tm = np.array([0.04, 0.05, 0.06])
    rear = np.array([1.2, 1.2, 1.2]) * 0.9 + 0.12 * 0.1
    expected = np.broadcast_to((1 - tf) * [0.025, 0.035, 0.045] + tf * rear, (h, w, 3)).copy()
    expected[mask] = (1 - tf) * [0.025, 0.035, 0.045] + tf * (
        (1 - tm) * [0.035, 0.045, 0.055] + tm * rear
    )
    return expected, halo, inside


for scene in (27, 33, 31):
    modes = [
        ("off", 0, ["--no-lut-min-filter"]),
        ("old", 0, []),
        ("retain25", 0.25, []),
        ("retain50", 0.5, []),
        ("retain75", 0.75, []),
        ("retain100", 1, []),
    ]
    if scene == 31:
        modes = [modes[0], modes[3]]
    if scene == 33:
        modes += [
            ("poison", 0.5, ["--poison-extinction"]),
            ("dense", 0.5, ["--dense-extinction"]),
        ]
    luts = {}
    images = {}
    for name, retain, flags in modes:
        folder = out / f"{scene}-{name}"
        folder.mkdir(exist_ok=True)
        run = subprocess.run(
            [
                str(root / "bin/avboit_viewer.exe"),
                "--headless",
                "--no-taa",
                "--stress",
                str(scene),
                "--render-width",
                "2560",
                "--render-height",
                "1440",
                "--accum-half",
                "--composition-half",
                "--lut-min-retain",
                str(retain),
                "--no-zero-depth",
                "--no-zero-t-shortcut",
                *flags,
            ],
            cwd=folder,
            capture_output=True,
        )
        (folder / "run.log").write_bytes(run.stdout + run.stderr)
        assert run.returncode == 0, (scene, name)
        hdr = texture(folder / "native-composition.rgba16f", "<f2", 4)[..., :3].astype("float32")
        assert np.isfinite(hdr).all()
        images[name] = hdr
        raw = (folder / "native-lut.rgba8").read_bytes()
        w, h, d = map(int, np.frombuffer(raw[:12], "<u4"))
        lut = np.frombuffer(raw[12:], "u1").reshape(d, h, w, 4)[..., :3]
        luts[name] = lut
        assert np.all(np.diff(lut.astype("int16"), axis=0) <= 0), (
            scene,
            name,
            "Z monotonicity",
        )
        if name != "off":
            src = luts["off"]
            if name != "dense":
                assert np.array_equal(lut == 0, src == 0), (scene, name, "new zero")
            if name not in ("dense", "poison"):
                pad = np.pad(src, ((0, 0), (1, 1), (1, 1), (0, 0)), mode="edge")
                minimum = np.minimum.reduce(
                    [pad[:, y : y + h, x : x + w] for y in range(3) for x in range(3)]
                )
                expectedLut = np.minimum(
                    src,
                    np.maximum(minimum, np.maximum(16, src.astype("float32") * retain)),
                )
                # Integration bounds pre-UNORM T; this reference sees rounded input.
                assert np.max(abs(lut.astype("float32") - expectedLut)) <= 1.01, (
                    scene,
                    name,
                    "filter bound",
                )
        if name in ("poison", "dense"):
            assert np.array_equal(hdr, images["retain50"]), (
                name,
                "HDR sparse mismatch",
            )
            assert np.array_equal(lut[: len(luts["retain50"])], luts["retain50"]), (
                name,
                "LUT sparse mismatch",
            )
        display(hdr).save(out / f"{scene}-{name}.png")
        row = dict(scene=scene, mode=name)
        if scene != 31:
            expected, edge, inside = (
                three_layer_reference(2560, 1440) if scene == 33 else reference(2560, 1440, 0)
            )
            err = abs(hdr - expected).max(axis=2)
            row.update(
                edge_mean=float(err[edge].mean()),
                edge_p99=float(np.percentile(err[edge], 99)),
                interior_mean=float(err[inside].mean()),
            )
            if name == "off":
                display(expected).save(out / f"{scene}-reference.png")
        if name == "retain100":
            assert np.array_equal(hdr, images["off"]), (
                scene,
                "retain=1 differs from unfiltered",
            )
        report.append(row)
        print(row, flush=True)
    if scene != 31:
        box = (1005, 324, 1145, 464) if scene == 33 else (460, 375, 660, 575)
        panels = [
            ("old", "Previous min"),
            ("retain25", "Retain 0.25"),
            ("retain50", "Retain 0.50"),
            ("off", "No min"),
            ("reference", "Sorted reference"),
        ]
        canvas = Image.new("RGB", (5 * 360, 396), "#202020")
        draw = ImageDraw.Draw(canvas)
        for i, (name, label) in enumerate(panels):
            draw.text((i * 360 + 10, 10), label, fill="white")
            canvas.paste(
                Image.open(out / f"{scene}-{name}.png")
                .crop(box)
                .resize((360, 360), Image.Resampling.NEAREST),
                (i * 360, 36),
            )
        canvas.save(out / f"{scene}-comparison.png")
(out / "report.json").write_text(json.dumps(report, indent=2))
print("Relative bounds, no new zeros, Z monotonicity, sparse equivalence PASS")
