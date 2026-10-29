# SPDX-License-Identifier: MIT
"""2K GPU checks for bin-aligned local media, partition invariance and frost split."""
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from capture_utils import texture


def main():
    root = Path(__file__).resolve().parents[2]
    output = root / "build/local-volume-check"
    report = {}

    def capture(name, preset, *flags):
        folder = output / name
        folder.mkdir(parents=True, exist_ok=True)
        run = subprocess.run(
            [
                str(root / "bin/avboit_viewer.exe"),
                "--headless",
                "--hardware-depth",
                "--accum-half",
                "--composition-half",
                "--render-width",
                "2560",
                "--render-height",
                "1440",
                "--stress",
                str(preset),
                *flags,
            ],
            cwd=folder,
            capture_output=True,
            timeout=90,
        )
        (folder / "run.log").write_bytes(run.stdout + run.stderr)
        assert run.returncode == 0, (name, run.stdout, run.stderr)
        for file in (
            "native-numerator.raw",
            "native-denominator.raw",
            "native-total-tau.raw",
            "native-back-numerator.raw",
            "native-scene-color.rgba16f",
        ):
            assert np.isfinite(texture(folder / file, "<f2", 4)).all(), (name, file)
        Image.open(folder / "native-raster.ppm").save(folder / "scene.png")
        return folder

    baseline = capture("homogeneous", 19)
    color = texture(baseline / "native-scene-color.rgba16f", "<f2", 4)[..., :3].astype("f4")
    tau = texture(baseline / "native-total-tau.raw", "<f2", 4)[720, 1280, :3]
    # The default camera looks along +Z from the origin. Central ray crosses
    # the complete 2.4-unit box; the half-pixel angular correction is < 1e-6.
    expected_tau = 0.7 * 2.4
    expected = np.array([0.4, 0.65, 1.2]) * (1 - np.exp(-expected_tau)) + 0.08 * np.exp(
        -expected_tau
    )
    report["analytic_color_max_error"] = float(abs(color[720, 1280] - expected).max())
    assert abs(tau - expected_tau).max() < 0.003
    assert report["analytic_color_max_error"] < 0.003
    for name, preset, flags in (
        ("overlap", 20, []),
        ("partition", 21, []),
        ("budget16", 19, ["--depth-budget", "16"]),
        ("budget32", 19, ["--depth-budget", "32"]),
    ):
        folder = capture(name, preset, *flags)
        other = texture(folder / "native-scene-color.rgba16f", "<f2", 4)[..., :3].astype("f4")
        error = abs(other - color)
        report[name] = {"max": float(error.max()), "rms": float(np.sqrt(np.mean(error * error)))}
        assert error.max() < 0.008, (name, report[name])
    frost = capture("frost", 22)
    n = texture(frost / "native-numerator.raw", "<f2", 4)[720, 1280, :3].astype("f4")
    back = texture(frost / "native-back-numerator.raw", "<f2", 4)[720, 1280, :3].astype("f4")
    assert (back > 0).all() and (n - back > 0).all(), (n, back)
    for name, preset, flags in (
        ("frost-poison", 22, ["--poison-resolve", "--poison-extinction"]),
        ("colored", 23, []),
        ("colored-reverse", 23, ["--submission-order", "1"]),
        ("oblique", 22, ["--camera-preset", "3"]),
    ):
        folder = capture(name, preset, *flags)
        if name == "frost-poison":
            for file in ("native-lut.rgba8", "native-scene-color.rgba16f"):
                assert (folder / file).read_bytes() == (frost / file).read_bytes(), file
        if name == "colored-reverse":
            reference = texture(output / "colored/native-scene-color.rgba16f", "<f2", 4).astype(
                "f4"
            )
            actual = texture(folder / "native-scene-color.rgba16f", "<f2", 4).astype("f4")
            assert abs(actual - reference).max() < 0.008
    imported = capture("imported-smoke", 24)
    smoke_tau = texture(imported / "native-total-tau.raw", "<f2", 4)[..., :3]
    assert smoke_tau.max() > 1 and np.count_nonzero(smoke_tau) > 1000
    imported_frost = capture("imported-frost", 25)
    poisoned = capture("imported-frost-poison", 25, "--poison-extinction", "--poison-resolve")
    for file in ("native-lut.rgba8", "native-scene-color.rgba16f"):
        assert (imported_frost / file).read_bytes() == (poisoned / file).read_bytes(), file
    (output / "report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    print("Local volume checks passed at 2560x1440")


if __name__ == "__main__":
    main()
