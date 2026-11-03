# SPDX-License-Identifier: MIT
"""2K exact-T skip comparison: HDR, opacity ledger, and instrumented work counts."""
from pathlib import Path
import subprocess, json
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[2]
out = root / "build/volume-lut-skip"
out.mkdir(parents=True, exist_ok=True)
report = {}
for case, flags in [
    ("thin", ["--stress", "24", "--volume-extinction", ".1"]),
    ("smoke3", ["--stress", "24", "--volume-extinction", "3"]),
    ("smoke16", ["--stress", "24", "--volume-extinction", "16"]),
    ("smoke128", ["--stress", "24", "--volume-extinction", "128"]),
    ("frost8", ["--stress", "25", "--volume-extinction", "8"]),
    ("half16", ["--stress", "24", "--volume-extinction", "16", "--volume-half-res"]),
]:
    data = {}
    for mode, extra in [
        ("base", []),
        ("skip", ["--volume-lut-skip"]),
        ("base-stats", ["--volume-march-stats"]),
        ("skip-stats", ["--volume-lut-skip", "--volume-march-stats"]),
    ]:
        folder = out / (case + "-" + mode)
        folder.mkdir(exist_ok=True)
        run = subprocess.run(
            [
                str(root / "bin/avboit_viewer.exe"),
                "--headless",
                "--no-taa",
                "--volume-full-res",
                "--no-lut-min-filter",
                "--accum-half",
                "--composition-half",
                *flags,
                *extra,
            ],
            cwd=folder,
            capture_output=True,
        )
        (folder / "run.log").write_bytes(run.stdout + run.stderr)
        assert run.returncode == 0, folder

        def read(name):
            return (
                np.frombuffer((folder / name).read_bytes()[8:], dtype="<f2")
                .reshape(-1, 4)[:, :3]
                .astype(float)
            )

        if "stats" in mode:
            a = read("native-numerator.raw")
            m = a[:, 0] > 0
            data[mode] = a[m].mean(0).tolist()
        else:
            Image.open(folder / "native-raster.ppm").save(
                out / (case + "-" + mode + ".png")
            )
            a, tau = read("native-composition.rgba16f"), read("native-total-tau.raw")
            assert np.isfinite(a).all() and np.isfinite(tau).all()
            if mode == "base":
                reference, tau_reference = a, tau
            else:
                error = np.abs(a - reference)
                data[mode] = dict(
                    mae=float(error.mean()),
                    max=float(error.max()),
                    p99=float(np.quantile(error.max(1), 0.99)),
                    tau_max=float(np.abs(tau - tau_reference).max()),
                    opacity_max=float(
                        np.abs(np.exp(-tau) - np.exp(-tau_reference)).max()
                    ),
                )
        print(case, mode, data.get(mode, "captured"), flush=True)
    report[case] = data
(out / "report.json").write_text(json.dumps(report, indent=2))
