# SPDX-License-Identifier: MIT
"""Verify sparse brick encoding, positive borders and Blender axis conversion."""
import importlib.util
from pathlib import Path
import tempfile
import numpy as np

spec = importlib.util.spec_from_file_location(
    "packer", Path(__file__).resolve().parents[2] / "tools/pack_smoke_bricks.py"
)
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)

with tempfile.TemporaryDirectory() as tmp:
    src, dst = Path(tmp) / "density.raw", Path(tmp) / "bricks.bin"
    z, y, x = np.indices((8, 8, 8))
    ramp = ((x + 8 * y + 64 * z) / 512).astype("<f4")
    for field in (ramp, np.zeros_like(ramp), (ramp > 0.85).astype("<f4")):
        field.tofile(src)
        for swap in (False, True):
            expected = field.swapaxes(0, 1) if swap else field
            padded = np.pad(expected, ((0, 1),) * 3, mode="edge")
            for precision in ("4bit", "half"):
                packer.pack(src, dst, 8, swap, precision)
                data = dst.read_bytes()
                header = np.frombuffer(data, "<u4", 8)
                assert header[0] == (3 if precision == "half" else 1)
                table = np.frombuffer(data, "<u2", 8, 32)
                for bz, by, bx in np.ndindex(2, 2, 2):
                    reference = padded[
                        bz * 4 : bz * 4 + 5, by * 4 : by * 4 + 5, bx * 4 : bx * 4 + 5
                    ].ravel()
                    index = int(table[(bz * 2 + by) * 2 + bx])
                    if not index:
                        assert not reference.any()
                        continue
                    start = int(header[5]) * 4 + (index - 1) * (256 if precision == "half" else 68)
                    if precision == "half":
                        decoded = np.frombuffer(data, "<f2", 125, start).astype("f4")
                        np.testing.assert_array_equal(decoded, reference.astype("f2").astype("f4"))
                    else:
                        lo, hi = np.frombuffer(data, "<f2", 2, start).astype("f4")
                        words = np.frombuffer(data, "<u4", 16, start + 4)
                        codes = np.array(
                            [(int(words[j // 8]) >> (4 * (j % 8))) & 15 for j in range(125)]
                        )
                        decoded = lo + codes * (hi - lo) / 15
                        assert np.max(np.abs(decoded - reference)) <= (hi - lo) / 30 + 1e-6
print("Brick round-trip checks passed (formats, empty bricks, borders, axes).")
