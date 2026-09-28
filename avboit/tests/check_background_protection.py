# SPDX-License-Identifier: MIT
"""Validate B rejection and the float q lower bound without a q texture."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import numpy as np
from capture_utils import texture, work_domain

parser = argparse.ArgumentParser()
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="avboit-background-") as directory:
    directory = Path(directory)
    results = []
    reference_lut = None
    for options in ([], ["--protect-background", "--background-threshold", "1"], ["--no-q-bound"]):
        run = subprocess.run(
            [
                str(args.viewer.resolve()),
                "--headless",
                "--poison-resolve",
                "--no-zero-depth",
                "--stress",
                "6",
                *options,
            ],
            cwd=directory,
            capture_output=True,
        )
        if run.returncode:
            raise RuntimeError((run.stdout + run.stderr).decode(errors="replace"))
        pixels = texture(directory / "native-background.rgba16f", "<f2", 4)[
            work_domain(directory, 10)
        ]
        assert np.isfinite(pixels).all()
        results.append(pixels.copy())
        lut = (directory / "native-lut.rgba8").read_bytes()
        if reference_lut is None:
            reference_lut = lut
        assert reference_lut == lut, "B protection/q bound changed AVBOIT LUT"
    base, protected, unbounded = results
    rejected = (base[:, 3] > 0) & (protected[:, 3] == 0)
    valid = protected[:, 3] > 0
    assert (protected[rejected] == 0).all(), "Rejected B must be zero premultiplied RGBA"
    assert np.array_equal(base[valid], protected[valid]), "Valid B changed"
    assert rejected.sum() > 100, "Fixture did not exercise rejection"
    changes = np.any(base != unbounded, axis=1).sum()
    assert changes > 0, "Fixture did not exercise q bound"
    print(f"PASS: {rejected.sum()} rejected B pixels; {changes} B pixels affected by q lower bound")
