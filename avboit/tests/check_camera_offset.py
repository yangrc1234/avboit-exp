# SPDX-License-Identifier: MIT
"""Camera-offset captures; verify unchanged coverage/tau and sparse safety."""
from pathlib import Path
import subprocess, json
import numpy as np
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[2]
out = root / "build/camera-offset-bins"
out.mkdir(parents=True, exist_ok=True)
rows = []
for name, bins, flags in [
    ("zero", 0, []),
    ("back", -2, []),
    ("one", 1, []),
    ("two", 2, []),
    ("three", 3, []),
    ("four", 4, []),
    ("poison", 2, ["--poison-extinction"]),
    ("dense", 2, ["--dense-extinction"]),
    ("reverse", 2, ["--submission-order", "1"]),
]:
    folder = out / name
    folder.mkdir(exist_ok=True)
    run = subprocess.run(
        [
            str(root / "bin/avboit_viewer.exe"),
            "--headless",
            "--no-taa",
            "--volume-full-res",
            "--no-lut-min-filter",
            "--stress",
            "26",
            "--shell-offset-bins",
            str(bins),
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
    Image.open(folder / "native-raster.ppm").save(out / f"{name}.png")
    tau = (folder / "native-total-tau.raw").read_bytes()
    if name == "zero":
        original_tau = tau
    if name != "reverse":
        assert tau == original_tau, (name, "physical coverage/tau changed")
    else:
        a = np.asarray(Image.open(out / "two.png")).astype("int16")
        b = np.asarray(Image.open(out / "reverse.png")).astype("int16")
        print("reverse display max difference", abs(a - b).max())
    if name in ("poison", "dense"):
        assert (folder / "native-composition.rgba16f").read_bytes() == (
            out / "two/native-composition.rgba16f"
        ).read_bytes(), name
        a = (folder / "native-lut.rgba8").read_bytes()
        b = (out / "two/native-lut.rgba8").read_bytes()
        assert a[12 : 12 + len(b) - 12] == b[12:], name
    print(name, "passed", flush=True)
canvas = Image.new("RGB", (1280, 3 * 396), "#202020")
d = ImageDraw.Draw(canvas)
for i, (name, label) in enumerate(
    [
        ("zero", "0 bins"),
        ("back", "-2 bins (away)"),
        ("one", "+1 bin"),
        ("two", "+2 bins"),
        ("three", "+3 bins"),
        ("four", "+4 bins"),
    ]
):
    x = (i % 2) * 640
    y = (i // 2) * 396
    d.text((x + 8, y + 8), label, fill="white")
    canvas.paste(Image.open(out / f"{name}.png").resize((640, 360)), (x, y + 36))
canvas.save(out / "comparison.png")
