# SPDX-License-Identifier: MIT
"""Analyze matched 2K --volume-upsample-test HDR captures (use --composition-half)."""
from pathlib import Path
import numpy as np, json, csv

r = Path(__file__).resolve().parents[2] / "build/dither-audit"
report = {}
for name in ["smoke16", "sponza16"]:
    folder = r / name
    if not (folder / "volume-upscale-dithered-post-07.rgba16f").exists():
        continue

    def load(mode, stage):
        data = []
        for frame in range(8):
            raw = (
                folder / f"volume-upscale-{mode}-{stage}-{frame:02}.rgba16f"
            ).read_bytes()
            w, h = map(int, np.frombuffer(raw[:8], "<u4"))
            data.append(
                np.frombuffer(raw[8:], "<f2")
                .reshape(h, w, 4)[..., :3]
                .astype("float32")
            )
        return np.stack(data)

    phases = list(csv.DictReader((folder / "volume-upscale-phases.csv").open()))
    a = phases[:48]
    b = phases[48:]
    assert all(
        [x["jitter_x"], x["jitter_y"], x["noise_phase"]]
        == [y["jitter_x"], y["jitter_y"], y["noise_phase"]]
        for x, y in zip(a, b)
    )
    arrays = {
        (m, s): load(m, s) for m in ["bilateral", "dithered"] for s in ["pre", "post"]
    }
    # Common affected-pixel mask from pre-TAA A/B differences, independent of output filtering.
    diff = np.abs(arrays["bilateral", "pre"] - arrays["dithered", "pre"]).max((0, 3))
    mask = diff > 0.002
    rec = {"affected_pixels": int(mask.sum()), "matched_phases": True}
    for (m, s), data in arrays.items():
        temporal = np.std(data, axis=0).mean(2)
        delta = np.abs(np.diff(data, axis=0)).mean((0, 3))
        rec[m + "-" + s] = {
            "mean_std": float(temporal[mask].mean()),
            "mean_frame_delta": float(delta[mask].mean()),
            "phase_std": [
                float(temporal[py::2, px::2][mask[py::2, px::2]].mean())
                for py in range(2)
                for px in range(2)
            ],
        }
    report[name] = rec
print(json.dumps(report, indent=2))
(r / "report.json").write_text(json.dumps(report, indent=2))
