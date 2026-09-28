# SPDX-License-Identifier: MIT
"""Check live-material footprint variants against dense/full work references."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
parser.add_argument("--capture-dir", type=Path)
args = parser.parse_args()
viewer = args.viewer.resolve()
cases = []
for normal in (False, True):
    for ior, gain in ((1, 1), (1.1, 4), (1.5, 0), (2.5, 4)):
        cases.append(
            (
                f"sphere-{normal}-{ior}-{gain}",
                [
                    "--glass-sphere",
                    "--sphere-roughness",
                    ".6",
                    "--sphere-ior",
                    str(ior),
                    "--refraction-gain",
                    str(gain),
                ]
                + (["--normal-sphere"] if normal else []),
            )
        )
cases += [
    (
        "inside-sphere",
        [
            "--glass-sphere",
            "--sphere-roughness",
            ".6",
            "--sphere-ior",
            "2.5",
            "--camera-preset",
            "3",
        ],
    ),
    ("planar-min", ["--planar-ior", "1", "--refraction-gain", "0"]),
    ("planar-max", ["--planar-ior", "2.5", "--refraction-gain", "4"]),
    (
        "sparse-normal",
        [
            "--glass-sphere",
            "--sphere-roughness",
            ".6",
            "--normal-sphere",
            "--refraction-gain",
            "4",
            "--sparse-blur",
        ],
    ),
]
with tempfile.TemporaryDirectory(prefix="avboit-materials-") as scratch:
    directory = Path(scratch)
    outputs = {}

    def capture(flags):
        result = subprocess.run(
            [str(viewer), "--headless", "--emissive-balls", "--no-smoke", *flags],
            cwd=directory,
            capture_output=True,
        )
        if result.returncode:
            raise RuntimeError((result.stdout + result.stderr).decode(errors="replace"))
        return (
            (directory / "native-raster.ppm").read_bytes(),
            (directory / "native-lut.rgba8").read_bytes(),
        )

    for name, flags in cases:
        output = capture(flags)
        if output != capture(flags + ["--full-blur", "--dense-extinction"]):
            raise AssertionError(f"ROI/occupancy mismatch: {name}")
        outputs[name] = output
        if args.capture_dir:
            args.capture_dir.mkdir(parents=True, exist_ok=True)
            (args.capture_dir / f"{name}.ppm").write_bytes(output[0])
        print(f"{name}: optimized/full reference match", flush=True)
    # An IOR of one removes physical deflection; surface lighting/absorption remain.
    straight = capture(
        [
            "--glass-sphere",
            "--sphere-roughness",
            ".6",
            "--sphere-ior",
            "1",
            "--refraction-gain",
            "0",
        ]
    )
    assert straight == outputs["sphere-False-1-1"], "IOR=1 must ignore physical refraction gain"
    assert (
        outputs["sphere-False-1.5-0"] == outputs["sphere-True-1.5-0"]
    ), "Zero gain must remove mode dependence"
    assert (
        outputs["sphere-False-1-1"][0] != outputs["sphere-True-1-1"][0]
    ), "Artistic normal offset should remain distinct"
    print("IOR=1 and zero-gain invariants passed")
