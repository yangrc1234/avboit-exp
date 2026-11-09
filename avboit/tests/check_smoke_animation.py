# SPDX-License-Identifier: MIT
"""Compare brick skipping against the same animation with traversal disabled."""
from pathlib import Path
import subprocess
import json
import numpy as np
from PIL import Image
from capture_utils import texture

root = Path(__file__).resolve().parents[2]
out = root / "build/smoke-animation"
report = {}
for frame in (0.0, 3.5, 15.0, 30.5, 31.5):
    images = []
    taus = []
    shadows = []
    for skip in (False, True):
        folder = out / f"{frame}-{int(skip)}"
        folder.mkdir(parents=True, exist_ok=True)
        args = [
            str(root / "bin/avboit_viewer.exe"),
            "--headless",
            "--sponza",
            "--stress",
            "24",
            "--baked-smoke",
            "--smoke-frame",
            str(frame),
            "--no-taa",
            "--no-zero-depth",
            "--volume-full-res",
            "--accum-half",
            "--volume-step",
            "0.1",
            "--volume-extinction",
            "8",
        ]
        if not skip:
            args.append("--no-smoke-brick-skip")
        p = subprocess.run(args, cwd=folder, capture_output=True, timeout=180)
        (folder / "run.log").write_bytes(p.stdout + p.stderr)
        assert p.returncode == 0, (frame, skip, p.stderr)
        image = Image.open(folder / "native-raster.ppm")
        image.save(folder / "scene.png")
        images.append(np.asarray(image).astype(np.int16))
        taus.append(texture(folder / "native-total-tau.raw", "<f2", 4))
        shadows.append([texture(folder / f"native-light-tau-{i}.f16", "<f2", 1) for i in range(3)])
    assert all(np.isfinite(t).all() for t in taus)
    assert np.array_equal(taus[0], taus[1]), ("tau mismatch", frame)
    assert np.array_equal(images[0], images[1]), ("image mismatch", frame)
    for a, b in zip(*shadows):
        assert np.array_equal(a, b), ("shadow mismatch", frame)
    report[str(frame)] = dict(
        mean_tau=float(taus[0][..., :3].astype(np.float32).mean()), identical=True
    )
assert report["0.0"]["mean_tau"] != report["15.0"]["mean_tau"]
(out / "report.json").write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
