# SPDX-License-Identifier: MIT
"""Check relocated runtime output against the source build from unrelated cwd."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "dist/avboit-exp")
    parser.add_argument("--reference", type=Path, default=ROOT / "bin/avboit_viewer.exe")
    parser.add_argument(
        "--sponza", action="store_true", help="Also test separately installed package assets"
    )
    args = parser.parse_args()
    viewers = [args.reference.resolve(), args.package.resolve() / "bin/avboit_viewer.exe"]
    fixtures = [
        [],
        ["--sparse-blur", "--emissive-balls", "--no-smoke"],
        ["--fixed-z", "--depth-budget", "16", "--render-width", "641", "--render-height", "385"],
        ["--stress", "6", "--protect-background", "--background-threshold", ".1"],
        ["--calibration-board", "--glass-sphere", "--no-smoke"],
    ]
    if args.sponza:
        fixtures.append(
            ["--donut-scene", "--glass-sphere", "--rgb-glass", "--sphere-roughness", ".4"]
        )
    with tempfile.TemporaryDirectory(prefix="avboit-relocated-") as scratch:
        directory = Path(scratch)
        for fixture in fixtures:
            results = []
            for viewer in viewers:
                result = subprocess.run(
                    [str(viewer), "--headless", *fixture], cwd=directory, capture_output=True
                )
                if result.returncode:
                    raise RuntimeError((result.stdout + result.stderr).decode(errors="replace"))
                results.append(
                    (
                        (directory / "native-raster.ppm").read_bytes(),
                        (directory / "native-lut.rgba8").read_bytes(),
                    )
                )
            if results[0] != results[1]:
                raise AssertionError(f"Relocated image/LUT mismatch: {fixture}")
            print(f'Relocated image and LUT match: {fixture or "calibration"}', flush=True)
        result = subprocess.run(
            [str(viewers[1]), "--headless", "--sponza-path", str(directory / "missing.gltf")],
            cwd=directory,
            capture_output=True,
        )
        if result.returncode == 0 or b"Sponza not found:" not in result.stdout + result.stderr:
            raise AssertionError("Missing explicit asset path must fail with a useful message")
        print("Missing-asset diagnostic passed")


if __name__ == "__main__":
    main()
