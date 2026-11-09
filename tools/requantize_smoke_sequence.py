# SPDX-License-Identifier: MIT
"""Requantize an FP16 brick sequence without changing its voxel grid or timing."""
import argparse
from pathlib import Path
import shutil
import tempfile
import numpy as np
from pack_smoke_bricks import pack


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    args = p.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        p.error("Use an empty output directory")
    count, fps = (args.source / "sequence.txt").read_text().split()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "density.raw"
        for frame in range(1, int(count) + 1):
            data = (args.source / f"{frame}.bin").read_bytes()
            h = np.frombuffer(data, "<u4", 8)
            n = int(h[1])
            g = n // 4
            if h[0] != 3 or n % 4 or not np.all(h[1:4] == n) or h[4] != 8:
                raise ValueError("Expected cubic FP16 brick data")
            indices = np.frombuffer(data, "<u2", g**3, 32).reshape(g, g, g)
            records = np.frombuffer(data, "<f2", offset=int(h[5]) * 4).reshape(-1, 128)
            if indices.max() > len(records):
                raise ValueError("Bad brick index")
            density = np.zeros((n, n, n), "<f4")
            for z, y, x in np.argwhere(indices != 0):
                block = records[int(indices[z, y, x]) - 1, :125].reshape(5, 5, 5)
                density[z * 4 : z * 4 + 4, y * 4 : y * 4 + 4, x * 4 : x * 4 + 4] = block[:4, :4, :4]
            # Verify that rebuilding borders preserves every original FP16 voxel.
            padded = np.pad(density, ((0, 1),) * 3, mode="edge")
            for z, y, x in np.argwhere(indices != 0):
                np.testing.assert_array_equal(
                    padded[z * 4 : z * 4 + 5, y * 4 : y * 4 + 5, x * 4 : x * 4 + 5].ravel(),
                    records[int(indices[z, y, x]) - 1, :125],
                )
            density.tofile(raw)
            pack(raw, args.output / f"{frame}.bin", n, precision="4bit")
            print(f"Frame {frame}/{count}", flush=True)
    for name in ("sequence.txt", "LICENSE.txt", "SOURCE.txt"):
        if (args.source / name).exists():
            shutil.copyfile(args.source / name, args.output / name)


if __name__ == "__main__":
    main()
