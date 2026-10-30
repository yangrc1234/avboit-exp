# SPDX-License-Identifier: MIT
"""2K regression for dense smoke, Zero-T cutoffs and half-resolution TAA."""
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from capture_utils import texture


def main():
    root = Path(__file__).resolve().parents[2]
    output = root / "build/dense-volume-check"
    report = {}

    def capture(name, extinction, *flags):
        folder = output / name
        folder.mkdir(parents=True, exist_ok=True)
        run = subprocess.run(
            [
                str(root / "bin/avboit_viewer.exe"),
                "--headless",
                "--sponza",
                "--stress",
                "24",
                "--render-width",
                "2560",
                "--render-height",
                "1440",
                "--volume-extinction",
                str(extinction),
                *flags,
            ],
            cwd=folder,
            capture_output=True,
            timeout=180,
        )
        (folder / "run.log").write_bytes(run.stdout + run.stderr)
        assert run.returncode == 0, (name, run.stdout, run.stderr)
        image = Image.open(folder / "native-raster.ppm")
        image.save(folder / "scene.png")
        return folder, np.asarray(image).astype(np.int16)

    for extinction in (64, 128):
        folder, enabled = capture(f"zero-{extinction}", extinction)
        _, disabled = capture(f"nozero-{extinction}", extinction, "--no-zero-depth")
        error = np.abs(enabled - disabled)
        report[f"zero_delta_{extinction}"] = {
            "max_255": int(error.max()),
            "mean_255": float(error.mean()),
            "pixels_over_8": int(np.count_nonzero(error.max(axis=2) > 8)),
        }
        # Zero-T may discard sub-quantization contributions, but must not shift
        # visible density/lighting samples or create holes in the visible skin.
        # Do not require identical extrema: quantized T and sparse density can
        # still change a few isolated pixels. Reject large or widespread holes.
        assert error.mean() < 0.01, report
        assert report[f"zero_delta_{extinction}"]["pixels_over_8"] < 32, report
        shape = (1440, 2560)
        a = np.fromfile(folder / "native-denominator.raw", dtype="<u4", offset=8).reshape(shape)
        tau = np.fromfile(folder / "native-total-tau.raw", dtype="<u4", offset=8).reshape(shape)
        # Bottom of the imported smoke: the old random-point visibility left
        # thousands of black pixels here with nonzero optical depth but no source.
        roi = np.s_[800:1200, 1300:1630]
        holes = int(np.count_nonzero((a[roi] == 0) & (tau[roi] != 0)))
        report[f"bottom_zero_weight_pixels_{extinction}"] = holes
        assert holes < 32, report

    folder, _ = capture("half-taa", 64, "--volume-half-res", "--taa")
    hdr = texture(folder / "native-taa.rgba16f", "<f2", 4)
    assert np.isfinite(hdr).all()
    # The native --taa path also exercises camera motion, cuts and history resize.
    # This is not a numerical guarantee of temporal quality for arbitrary motion.
    report["half_taa_finite"] = True
    (output / "report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
