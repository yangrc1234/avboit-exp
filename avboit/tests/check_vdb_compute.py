# SPDX-License-Identifier: MIT
"""Compare the JangaFX Q4 compute path and the raster reference at 1440p."""
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from capture_utils import texture

root = Path(__file__).resolve().parents[2]
viewer = root / "bin/avboit_viewer.exe"
sequence = root / "build/jangafx-smoke/sequence128-4bit"
scene = root / "assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf"
out = root / "build/vdb-compute-regression"


def capture(name, flags, *, sponza=True, packed=False):
    folder = out / name
    folder.mkdir(parents=True, exist_ok=True)
    args = [
        str(viewer), "--headless", *(["--sponza-path", str(scene), "--sponza"] if sponza else []), "--stress", "24",
        "--smoke-sequence", str(sequence), "--smoke-frame", "80", "--smoke-paused",
        "--render-width", "2560", "--render-height", "1440", "--volume-step", "0.025",
        "--volume-step-scale", "1", "--volume-extinction", "6", "--no-smoke-mask",
        "--camera-preset", "10", *([] if packed else ["--accum-half", "--composition-half"]), "--volume-quad-t",
        "--volume-half-res", *flags,
    ]
    with (folder / "run.log").open("w") as log:
        subprocess.run(args, cwd=folder, stdout=log, stderr=subprocess.STDOUT, check=True)
    return folder


if __name__ == "__main__":
    if not (sequence / "sequence.txt").exists():
        raise SystemExit("Prepare the Q4 JangaFX sequence as described in docs/BAKED_SMOKE.zh-CN.md")
    reference = capture("ps", [])
    masked = capture("ps-proxy-stencil", ["--vdb-proxy-stencil"])
    for name in ("native-numerator.raw", "native-denominator.raw", "native-total-tau.raw",
                 "native-back-numerator.raw", "native-raster.ppm"):
        assert (masked / name).read_bytes() == (reference / name).read_bytes(), name
    hull = capture("ps-proxy-hull", ["--vdb-proxy-stencil", "--vdb-ray-hull"])
    for name in ("native-numerator.raw", "native-denominator.raw", "native-total-tau.raw", "native-back-numerator.raw"):
        a, b = (texture(folder / name, "<f2", 4)[..., :3].astype(np.float32) for folder in (masked, hull))
        assert np.allclose(a, b, rtol=.002, atol=.002), name
    a, b = (np.asarray(Image.open(folder / "native-raster.ppm"), dtype=np.int16) for folder in (masked, hull))
    assert np.abs(a - b).max() <= 1
    for name, flags in (("cs", []), ("cs-shared", ["--vdb-shared-lighting"])):
        result = capture(name, ["--vdb-compute", *flags])
        tau = texture(reference / "native-total-tau.raw", "<f2", 4)[..., :3]
        actual = texture(result / "native-total-tau.raw", "<f2", 4)[..., :3]
        assert tau.max() > 1 and np.allclose(tau, actual, rtol=.002, atol=.002)
        for smoke_tau in (tau, actual):
            assert np.array_equal(smoke_tau[..., 0], smoke_tau[..., 1])
            assert np.array_equal(smoke_tau[..., 0], smoke_tau[..., 2])
        a, b = (np.asarray(Image.open(f / "native-raster.ppm"), dtype=np.int16) for f in (reference, result))
        assert np.abs(a - b).mean() < .1 and np.abs(a - b).max() <= 5
    heat = capture("steps", ["--volume-step-view", "--vdb-compute"])
    stats = texture(heat / "native-march-counters.rgba32f", "<f4", 4)
    assert stats.shape == (720, 1280, 4)
    steps = stats[..., 0] - stats[..., 2]
    assert np.isfinite(steps).all() and (steps >= 0).all() and (steps == np.floor(steps)).all()
    assert steps.min() == 0 and steps.max() > 16
    skipped = capture("skipped", ["--volume-skip-view", "--vdb-compute"])
    skip_stats = texture(skipped / "native-march-counters.rgba32f", "<f4", 4)
    assert np.array_equal(stats, skip_stats) and skip_stats[..., 2].max() > 0
    assert not np.array_equal(Image.open(heat / "native-raster.ppm"), Image.open(skipped / "native-raster.ppm"))
    disabled = capture("skip-off", ["--volume-skip-view", "--vdb-compute", "--no-smoke-brick-skip"])
    assert not texture(disabled / "native-march-counters.rgba32f", "<f4", 4)[..., 2].any()
    counts = []
    for label, flags in (("on", []), ("off", ["--no-volume-early-out"])):
        folder = capture("dense-tail-" + label, ["--volume-step-view", "--vdb-compute", "--volume-extinction", "32", *flags])
        dense = texture(folder / "native-march-counters.rgba32f", "<f4", 4)
        counts.append(dense[..., 0] - dense[..., 2])
    assert np.all(counts[0] <= counts[1]) and np.any(counts[0] < counts[1])
    print("Q4 integration, shared lighting, step counters and dense-tail termination passed.")
