# SPDX-License-Identifier: MIT
"""Nested cubes: ordering invariance, unchanged coverage, and sparse-Z safety."""
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image, ImageDraw
from capture_utils import texture

root = Path(__file__).resolve().parents[2]
out = root / "build/flat-ordering"
out.mkdir(parents=True, exist_ok=True)
captures = {}
for name, flags in [
    ("geometric", ["--geometric-shell"]),
    ("flat", []),
    ("reverse", ["--submission-order", "1"]),
    ("poison", ["--poison-extinction"]),
    ("dense", ["--dense-extinction"]),
    ("no-slope", ["--no-slope-splat"]),
    ("budget16", ["--depth-budget", "16"]),
    ("no-min", ["--no-lut-min-filter"]),
]:
    folder = out / name
    folder.mkdir(exist_ok=True)
    run = subprocess.run(
        [
            str(root / "bin/avboit_viewer.exe"),
            "--headless",
            "--no-taa",
            "--stress",
            "32",
            "--accum-half",
            "--composition-half",
            *flags,
        ],
        cwd=folder,
        capture_output=True,
    )
    (folder / "run.log").write_bytes(run.stdout + run.stderr)
    assert run.returncode == 0, (name, run.stderr[-1500:])
    hdr = texture(folder / "native-composition.rgba16f", "<f2", 4)
    tau = texture(folder / "native-total-tau.raw", "<f2", 4)
    assert hdr.shape[:2] == (1440, 2560), hdr.shape
    assert np.isfinite(hdr).all(), name
    captures[name] = (hdr, tau)
    Image.open(folder / "native-raster.ppm").save(out / (name + ".png"))
    if name != "geometric":
        assert np.allclose(tau, captures["geometric"][1], rtol=0.002, atol=0.002), (
            name,
            "coverage/tau changed",
        )
    if name in ("reverse", "poison", "dense", "no-slope", "budget16"):
        # FP16 blend addition is not associative across triangle/actor boundaries.
        reference = captures["flat"][0]
        same = (
            np.allclose(hdr, reference, rtol=0.003, atol=0.001)
            if name == "reverse"
            else np.array_equal(hdr, reference)
        )
        assert same, (name, "flattened output changed")
    if name == "flat":
        data = (folder / "native-lut.rgba8").read_bytes()
        w, h, d = map(int, np.frombuffer(data[:12], "<u4"))
        lut = np.frombuffer(data[12:], "u1").reshape(d, h, w, 4)[..., :3].astype("int16")
        changes = np.any(
            np.diff(np.concatenate([np.full((1, h, w, 3), 255), lut]), axis=0) != 0,
            axis=(1, 2, 3),
        )
        bins = np.flatnonzero(changes)
        assert len(bins) == 2 and bins[1] == bins[0] + 1, bins
        print("Only two adjacent LUT transitions:", bins, flush=True)
    print(name, "PASS", flush=True)
canvas = Image.new("RGB", (1280, 2 * 756), "#202020")
draw = ImageDraw.Draw(canvas)
for row, (name, label) in enumerate(
    [
        ("geometric", "Geometric depth + outer camera offset (1 bin)"),
        ("flat", "Flattened ordering: inner k, outer k-1"),
    ]
):
    draw.text((12, row * 756 + 10), label, fill="white")
    canvas.paste(Image.open(out / (name + ".png")).resize((1280, 720)), (0, row * 756 + 36))
canvas.save(out / "comparison.png")
