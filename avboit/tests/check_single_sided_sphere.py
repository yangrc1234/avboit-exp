# SPDX-License-Identifier: MIT
"""A lone one-sided sphere must contribute nothing to its own background ledger."""
from pathlib import Path
import subprocess
import tempfile
import numpy as np

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="avboit-single-sphere-") as directory:
    directory = Path(directory)
    for order in (0, 2):
        command = [
            str(root / "bin/avboit_viewer.exe"),
            "--headless",
            "--glass-sphere",
            "--no-smoke",
            "--calibration-board",
            "--accum-half",
            "--submission-order",
            str(order),
        ]
        result = subprocess.run(command, cwd=directory, capture_output=True)
        if result.returncode:
            raise RuntimeError(
                result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace")
            )

        def read(name, dtype, channels):
            data = (directory / name).read_bytes()
            w, h = np.frombuffer(data, dtype="<u4", count=2)
            return np.frombuffer(data, dtype=dtype, offset=8).reshape(int(h), int(w), channels)

        hit = read("native-interface.rgba32f", "<f4", 4)
        back = read("native-back-numerator.raw", "<f2", 4)[..., :3]
        tau = read("native-total-tau.raw", "<f2", 4)[..., :3].astype("f4")
        mask = hit[..., 3] > 0
        assert mask.sum() > 100000, "Sphere disappeared after culling"
        assert 3.4 < float(hit[720, 1280, 0]) < 3.6, "Kept rear rather than front hemisphere"
        assert np.isfinite(back).all() and not back.any(), "Sphere surface leaked into BackN"
        expected = -np.log(np.array([0.7728, 0.8648, 0.8832], dtype="f4"))
        assert (
            np.max(np.abs(tau[mask] - expected)) < 0.001
        ), "Accumulation counted the wrong number of sphere layers"
        data = (directory / "native-lut.rgba8").read_bytes()
        # LUT capture has width/height/valid-slices, followed by Z-major RGBA8.
        w, h, slices = map(int, np.frombuffer(data, dtype="<u4", count=3))
        lut = np.frombuffer(data, dtype="u1", offset=12).reshape(slices, h, w, 4)
        assert (
            np.max(np.abs(lut[-1, 90, 160, :3] / 255.0 - np.exp(-expected))) < 0.035
        ), "Extinction still counted sphere rear faces"
        print(f"Order {order}: front interface, one optical layer, empty BackN PASS", flush=True)
