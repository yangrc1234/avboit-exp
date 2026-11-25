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

spec = importlib.util.spec_from_file_location(
    "distance_baker", Path(__file__).resolve().parents[2] / "tools/add_smoke_distance_table.py"
)
distance_baker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(distance_baker)

# An overestimate could let all four rays skip a visible brick.
occupied = np.zeros((4, 6, 7), dtype=bool)
occupied[1, 1, 2] = occupied[0, 5, 6] = True
coords = np.moveaxis(np.indices(occupied.shape), 0, -1)
nearest = np.minimum.reduce([np.abs(coords - cell).max(axis=-1) for cell in np.argwhere(occupied)])
np.testing.assert_array_equal(distance_baker.distance_table(occupied), nearest)
assert np.all(distance_baker.distance_table(np.zeros_like(occupied)) == 255)

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
    # Partial final bricks must keep the logical grid and clamp the filtering halo.
    for n in (14, 34):
        field = np.random.default_rng(n).random((n, n, n), dtype=np.float32)
        field.tofile(src)
        packer.pack(src, dst, n, precision="half")
        data = dst.read_bytes()
        h = np.frombuffer(data, "<u4", 8)
        baked = distance_baker.bake_frame(data)
        bh = np.frombuffer(baked, "<u4", 8)
        assert data[32:int(h[5])*4] == baked[32:int(h[5])*4]
        assert data[int(h[5])*4:] == baked[int(bh[5])*4:]
        g = (n + 3) // 4
        assert np.all(h[1:4] == n)
        table = np.frombuffer(data, "<u2", g**3, 32).reshape(g, g, g)
        expected = np.pad(field, ((0, g*4+1-n),)*3, mode="edge").astype("f2")
        for z, y, x in np.ndindex(g, g, g):
            offset = int(h[5])*4 + (int(table[z,y,x])-1)*256
            actual = np.frombuffer(data, "<f2", 125, offset).reshape(5,5,5)
            np.testing.assert_array_equal(actual, expected[z*4:z*4+5,y*4:y*4+5,x*4:x*4+5])
print("Brick round-trip checks passed (formats, empty bricks, borders, axes, partial bricks).")
