# SPDX-License-Identifier: MIT
"""Bake an R8 Chebyshev distance per brick into a copy of a smoke sequence."""
import argparse
import shutil
import struct
from pathlib import Path

import numpy as np


def distance_table(occupied):
    distance = np.full(occupied.shape, 255, dtype=np.uint8)
    distance[occupied] = 0
    reached = occupied.copy()
    if reached.any():
        for radius in range(1, 255):
            # A dilation by one brick on each axis adds the complete next shell.
            grown = reached
            for axis in range(3):
                pad = [(0, 0)] * 3
                pad[axis] = (1, 1)
                v = np.moveaxis(np.pad(grown, pad), axis, 0)
                grown = np.moveaxis(v[:-2] | v[1:-1] | v[2:], 0, axis)
            distance[grown & ~reached] = radius
            reached = grown
            if reached.all():
                break
    return distance


def bake_frame(data):
    h = list(struct.unpack_from("<8I", data))
    grid = tuple((v + 3) // 4 for v in h[3:0:-1])
    count = int(np.prod(grid))
    if h[0] not in (1, 3) or h[4] != 8 or h[5] != 8 + (count + 1) // 2:
        raise ValueError("Expected a packed VDB frame without a distance table")
    offset = h[5] * 4
    if offset > len(data):
        raise ValueError("Truncated brick table")
    indices = np.frombuffer(data, "<u2", count, 32).reshape(grid)
    distances = distance_table(indices != 0).tobytes()
    distances += bytes((-len(distances)) % 4)
    h[5] += len(distances) // 4
    return struct.pack("<8I", *h) + data[32:offset] + distances + data[offset:]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    manifest = args.source / "sequence.txt"
    count = int(manifest.read_text().split()[0])
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("Choose an empty destination")
    args.output.mkdir(parents=True, exist_ok=True)
    for i in range(1, count + 1):
        name = f"{i}.bin"
        (args.output / name).write_bytes(bake_frame((args.source / name).read_bytes()))
    shutil.copyfile(manifest, args.output / manifest.name)
