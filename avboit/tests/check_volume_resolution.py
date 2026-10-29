# SPDX-License-Identifier: MIT
"""2K checks for low-resolution volume statistics and full-resolution interfaces."""
from pathlib import Path
import json
import subprocess
import numpy as np
from PIL import Image
from capture_utils import texture

root = Path(__file__).resolve().parents[2]
out = root / "build/volume-resolution"
report = {}
for preset in (0, 19, 22, 25):
    captures = []
    for mode, flags in (("full", []), ("half", ["--volume-half-res"])):
        folder = out / f"{preset}-{mode}"
        folder.mkdir(parents=True, exist_ok=True)
        args = [
            str(root / "bin/avboit_viewer.exe"),
            "--headless",
            "--hardware-depth",
            "--stress",
            str(preset),
            "--accum-half",
            "--composition-half",
            *flags,
        ]
        if preset == 25:
            args += ["--sponza"]
        run = subprocess.run(args, cwd=folder, capture_output=True, timeout=90)
        (folder / "run.log").write_bytes(run.stdout + run.stderr)
        assert run.returncode == 0, run.stderr
        assert b"ERROR" not in run.stdout + run.stderr
        image = np.asarray(Image.open(folder / "native-raster.ppm"), dtype=float)
        Image.open(folder / "native-raster.ppm").save(folder / "scene.png")
        for name in ("numerator", "denominator", "total-tau", "back-numerator"):
            data = texture(folder / f"native-{name}.raw", "<f2", 4)[..., :3]
            assert np.isfinite(data).all() and (data >= 0).all(), (preset, mode, name)
        captures.append((folder, image))
    a, b = captures
    for name in ("native-interface-depth.r32f", "native-lut.rgba8"):
        assert (a[0] / name).read_bytes() == (b[0] / name).read_bytes(), name
    error = abs(a[1] - b[1])
    if preset == 0:
        assert error.max() == 0, "No eligible volumes must leave the image unchanged"
    report[str(preset)] = {
        "mean_8bit_error": float(error.mean()),
        "p99_8bit_error": float(np.percentile(error, 99)),
    }
    assert error.mean() < 3, report[str(preset)]
(out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
