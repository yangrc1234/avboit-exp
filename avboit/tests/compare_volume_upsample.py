# SPDX-License-Identifier: MIT
"""Compare --volume-upsample-test captures for kernels 0, 1 and 2.

Use <scene>-<kernel> directories; each run needs --composition-half. The
'dithered' file prefix denotes the selected test kernel, including the
nonrandom matched reference. No GPU work runs in this analysis script.
"""
from pathlib import Path
import numpy as np, json, csv
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--root", type=Path, default=Path(__file__).resolve().parents[2] / "build/dither-kernels"
)
parser.add_argument(
    "--reference-root",
    type=Path,
    default=Path(__file__).resolve().parents[2] / "build/dither-audit",
)
args = parser.parse_args()
root = args.root
# Independent quadrature checks the matched spatial kernel, not the nonlinear
# bilateral normalization or TAA. Those are compared using the GPU captures.
u = (np.arange(256) + 0.5) / 256 - 0.5
for phase, expected in [(0.0, np.array([0.125, 0.75, 0.125])), (0.5, np.array([0.5, 0.5, 0.0]))]:
    x = np.arange(3) - 1 + np.floor(phase + 0.5)
    weights = np.maximum(0.0, 1.0 - np.abs(x[:, None] - phase - u)).mean(1)
    np.testing.assert_allclose(weights, expected, atol=1e-12)
    assert np.all(weights >= 0.0) and abs(weights.sum() - 1.0) < 1e-12


def load(folder, mode, stage):
    a = []
    for f in range(8):
        raw = (folder / f"volume-upscale-{mode}-{stage}-{f:02}.rgba16f").read_bytes()
        w, h = np.frombuffer(raw[:8], "<u4")
        a.append(np.frombuffer(raw[8:], "<f2").reshape(h, w, 4)[..., :3].astype(np.float32))
    return np.stack(a)


report = {}
for scene in ["smoke16", "sponza16", "frost16"]:
    if not (root / f"{scene}-2/volume-upscale-dithered-post-07.rgba16f").exists():
        continue
    folders = [root / f"{scene}-{i}" for i in range(3)]
    arrays = {("fixed", s): load(folders[0], "bilateral", s) for s in ["pre", "post"]}
    for name, folder in zip(["legacy", "coherent", "matched"], folders):
        rows = list(csv.DictReader((folder / "volume-upscale-phases.csv").open()))
        assert len(rows) == 96
        assert all(
            tuple(a.values())[1:] == tuple(b.values())[1:] for a, b in zip(rows[:48], rows[48:])
        )
        for stage in ["pre", "post"]:
            arrays[name, stage] = load(folder, "dithered", stage)
        assert np.array_equal(load(folder, "bilateral", "post"), arrays["fixed", "post"]), folder
    mask = (
        np.maximum.reduce(
            [
                np.abs(arrays[n, "pre"] - arrays["fixed", "pre"]).max((0, 3))
                for n in ["legacy", "coherent", "matched"]
            ]
        )
        > 0.002
    )
    rec = {"pixels": int(mask.sum())}
    for (name, stage), a in arrays.items():
        assert np.isfinite(a).all()
        temporal = a.std(0).mean(2)
        delta = np.abs(np.diff(a, axis=0)).mean((0, 3))
        rec[name + "-" + stage] = {
            "std": float(temporal[mask].mean()),
            "delta": float(delta[mask].mean()),
            "phase_std": [
                float(temporal[y::2, x::2][mask[y::2, x::2]].mean())
                for y in range(2)
                for x in range(2)
            ],
        }
    # Same pre-existing fine full-resolution stable TAA reference for every candidate.
    refpath = args.reference_root / (scene + "-reference") / "native-taa.rgba16f"
    if refpath.exists():
        raw = refpath.read_bytes()
        w, h = map(int, np.frombuffer(raw[:8], "<u4"))
        ref = (
            np.frombuffer(raw[8:], "<f2")
            .reshape(h, w, 4)[h // 4 : h // 4 + h // 2, w // 4 : w // 4 + w // 2, :3]
            .astype(np.float32)
        )
        for n in ["fixed", "legacy", "coherent", "matched"]:
            err = np.abs(arrays[n, "post"].mean(0) - ref).mean(2)
            rec[n + "-post"]["reference_mae"] = float(err[mask].mean())
    report[scene] = rec
assert report, "No complete capture sets found"
print(json.dumps(report, indent=2))
(root / "report.json").write_text(json.dumps(report, indent=2))
