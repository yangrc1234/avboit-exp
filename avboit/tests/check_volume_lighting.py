# SPDX-License-Identifier: MIT
"""2K Sponza checks: moving light, MSM attenuation, finite moments and empty scenes."""
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from capture_utils import texture


def main():
    root = Path(__file__).resolve().parents[2]
    output = root / "build/volume-lighting"
    report = {}

    def capture(name, preset=24, *flags):
        folder = output / name
        folder.mkdir(parents=True, exist_ok=True)
        run = subprocess.run(
            [
                str(root / "bin/avboit_viewer.exe"),
                "--headless",
                "--sponza",
                "--hardware-depth",
                "--render-width",
                "2560",
                "--render-height",
                "1440",
                "--accum-half",
                "--composition-half",
                "--stress",
                str(preset),
                *flags,
            ],
            cwd=folder,
            capture_output=True,
            timeout=90,
        )
        (folder / "run.log").write_bytes(run.stdout + run.stderr)
        assert run.returncode == 0, (name, run.stderr)
        assert b"ERROR" not in run.stderr, (name, run.stderr)
        Image.open(folder / "native-raster.ppm").save(folder / "scene.png")
        color = texture(folder / "native-scene-color.rgba16f", "<f2", 4)[..., :3].astype("f4")
        numerator = texture(folder / "native-numerator.raw", "<f2", 4)[..., :3].astype("f4")
        assert np.isfinite(color).all() and np.isfinite(numerator).all()
        assert (color >= 0).all() and (numerator >= 0).all()
        data = np.fromfile(folder / "native-volume-lights.f32", "<f4").reshape(39, 4)
        assert np.isfinite(data).all()
        return folder, color, numerator, data

    folder, on, n_on, data = capture("msm-on")
    assert (data[37, :3] == 1).all(), data[37]
    # A rotated scene root must not rotate the world-space light a second time.
    # Reversed-Z gradients point opposite to the direction of light travel.
    for base in (5, 13, 17):
        gradient = data[base : base + 3, 2]
        alignment = float(np.dot(-gradient / np.linalg.norm(gradient), data[0, :3]))
        assert alignment > 0.9999, (base, alignment)
    sun_folder, sun, _, _ = capture("sun-only", 24, "--spot-intensity", "0")
    _, sun_off, _, _ = capture("sun-no-msm", 24, "--spot-intensity", "0", "--no-volume-shadows")
    # This ceiling patch lies outside the spot cone. A full/half angle mismatch
    # lit it despite having no matching shadow-map coverage (bright seams).
    assert np.max(abs(on[220:410, 700:850] - sun[220:410, 700:850])) < 0.002
    # Exclude all camera-ray transparent/volume coverage: the remaining delta
    # must be smoke casting a shadow on opaque geometry, not self-shadowing.
    tau = texture(sun_folder / "native-total-tau.raw", "<f2", 4)[..., :3]
    surface = np.max(tau, axis=-1) == 0
    shadow_delta = (sun_off - sun)[surface]
    report["sun_surface_shadow_max_attenuation"] = float(shadow_delta.max())
    report["sun_surface_shadow_changed_pixels"] = int(
        np.count_nonzero(np.max(shadow_delta, axis=-1) > 0.001)
    )
    assert report["sun_surface_shadow_changed_pixels"] > 100
    assert shadow_delta.min() > -0.02
    for index in range(3):
        tau = texture(folder / f"native-light-tau-{index}.f16", "<f2", 1)[..., 0]
        moments = texture(folder / f"native-light-moments-{index}.unorm16", "<u2", 4)
        assert np.isfinite(tau).all() and np.isfinite(moments).all()
        assert tau.min() >= 0 and tau.max() > 0.01
        e = moments.astype("f4") / 65535.0 - np.array([0.5, 0, 0.5, 0])
        decoded = np.stack(
            [
                -e[..., 0] / 3 + np.sqrt(3) * e[..., 2],
                0.125 * e[..., 1] + e[..., 3],
                -0.75 * e[..., 0] + 0.75 * np.sqrt(3) * e[..., 2],
                -0.125 * e[..., 1] + e[..., 3],
            ],
            axis=-1,
        )
        assert np.all(abs(decoded) <= 1.0001)
        report[f"cascade_{index}_max_tau"] = float(tau.max())
    _, off, n_off, _ = capture("msm-off", 24, "--no-volume-shadows")
    # Same extinction and density; shadowing can only attenuate source radiance.
    assert n_on.sum() < n_off.sum() * 0.98
    assert np.max(n_on - n_off) < 0.02
    report["volume_radiance_ratio_on_off"] = float(n_on.sum() / n_off.sum())
    report["surface_and_volume_image_change"] = float(abs(on - off).mean())
    assert report["surface_and_volume_image_change"] > 0.0001
    _, moved, _, _ = capture("moving-spot", 24, "--time", "3")
    report["moving_light_mean_change"] = float(abs(moved - on).mean())
    assert report["moving_light_mean_change"] > 0.0001
    capture("frost", 25)
    _, _, _, empty = capture("no-volume", 0, "--no-smoke")
    assert (empty[37, :3] == 0).all()
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
