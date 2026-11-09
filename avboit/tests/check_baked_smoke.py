# SPDX-License-Identifier: MIT
"""2K single-volume coverage and packed-density integration regression."""
from pathlib import Path
import json
import subprocess
import numpy as np
from PIL import Image
from capture_utils import texture

root = Path(__file__).resolve().parents[2]
report = {}
previous = None
for name, flags in [
    ("unmasked", ["--no-smoke-mask"]),
    ("door-open", []),
    ("door-closed", ["--smoke-door-closed"]),
]:
    folder = root / "build/baked-smoke" / name
    folder.mkdir(parents=True, exist_ok=True)
    run = subprocess.run(
        [
            str(root / "bin/avboit_viewer.exe"),
            "--headless",
            "--sponza",
            "--stress",
            "24",
            "--baked-smoke",
            "--no-taa",
            "--volume-full-res",
            "--no-zero-depth",
            "--accum-half",
            "--render-width",
            "2560",
            "--render-height",
            "1440",
            "--volume-step",
            "0.1",
            "--volume-step-scale",
            "1",
            "--volume-extinction",
            "8",
            *flags,
        ],
        cwd=folder,
        capture_output=True,
        timeout=180,
    )
    (folder / "run.log").write_bytes(run.stdout + run.stderr)
    assert run.returncode == 0, name
    tau = texture(folder / "native-total-tau.raw", "<f2", 4)[..., :3].astype(np.float32)
    numerator = texture(folder / "native-numerator.raw", "<f2", 4)[..., :3]
    assert np.isfinite(tau).all() and np.isfinite(numerator).all()
    assert tau.max() > 0 and tau.min() >= 0
    if previous is not None:
        assert tau.sum() < previous.sum() * 0.99, "Coverage did not reduce optical depth"
        assert np.max(tau - previous) < 0.02, "Mask unexpectedly added density"
    previous = tau
    report[name] = dict(mean_tau=float(tau.mean()), max_tau=float(tau.max()))
    Image.open(folder / "native-raster.ppm").save(folder / "scene.png")
(root / "build/baked-smoke/report.json").write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
