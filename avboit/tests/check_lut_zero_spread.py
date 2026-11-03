# SPDX-License-Identifier: MIT
"""Verify neighbour-floor protection without hardware/shading Zero-T shortcuts."""
from pathlib import Path
import subprocess
import numpy as np
from capture_utils import texture

root = Path(__file__).resolve().parents[2]
out = root / "build/lut-zero-regression"
out.mkdir(parents=True, exist_ok=True)
luts = {}
hdrs = {}
for name, flags in [
    ("off", []),
    ("min", ["--lut-min-filter"]),
    ("poison", ["--lut-min-filter", "--poison-extinction"]),
    ("dense", ["--lut-min-filter", "--dense-extinction"]),
]:
    folder = out / name
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
            "31",
            "--no-zero-depth",
            "--no-zero-t-shortcut",
            "--accum-half",
            "--composition-half",
            *flags,
        ],
        cwd=folder,
        capture_output=True,
    )
    (folder / "run.log").write_bytes(run.stdout + run.stderr)
    assert run.returncode == 0, folder
    den = texture(folder / "native-denominator.raw", "<f2", 4)[..., :3]
    tau = texture(folder / "native-total-tau.raw", "<f2", 4)[..., :3]
    assert not np.any((tau > 0.01) & (den == 0)), name
    hdrs[name] = texture(folder / "native-composition.rgba16f", "<f2", 4)
    assert np.isfinite(hdrs[name]).all()
    raw = (folder / "native-lut.rgba8").read_bytes()
    w, h, d = map(int, np.frombuffer(raw[:12], "<u4"))
    luts[name] = np.frombuffer(raw[12:], "u1").reshape(d, h, w, 4)
    assert np.all(np.diff(luts[name][..., :3].astype("int16"), axis=0) <= 0)
    print(name, "PASS", flush=True)
src = luts["off"]
padded = np.pad(src, ((0, 0), (1, 1), (1, 1), (0, 0)), mode="edge")
minimum = np.minimum.reduce(
    [
        padded[:, y : y + src.shape[1], x : x + src.shape[2]]
        for y in range(3)
        for x in range(3)
    ]
)
expected = np.minimum(src, np.maximum(minimum, 16))
assert np.array_equal(luts["min"], expected)
assert np.array_equal(luts["min"][..., :3] == 0, src[..., :3] == 0)
for mode in ["poison", "dense"]:
    assert np.array_equal(luts["min"], luts[mode][: len(src)])
    assert np.array_equal(hdrs["min"], hdrs[mode])
print("bounded min, centre zeros, sparse/dense, nonzero accumulation PASS")
