# SPDX-License-Identifier: MIT
"""Pack XYZ cubic float32 density into sparse 4-bit or FP16 bricks (X fastest)."""
import argparse
import struct
from pathlib import Path
import numpy as np


def pack(source, output, size, blender_z_up=False, precision="4bit"):
    a = np.fromfile(source, dtype="<f4").reshape(size, size, size)
    if size % 4 or not np.isfinite(a).all() or a.min() < 0:
        raise ValueError("Expected finite positive density, size divisible by four")
    if blender_z_up:
        a = a.swapaxes(0, 1)
    padded = np.pad(a, ((0, 1),) * 3, mode="edge")
    blocks = np.lib.stride_tricks.sliding_window_view(padded, (5, 5, 5))[::4, ::4, ::4]
    blocks = blocks.reshape(-1, 125)
    occupied = blocks.max(axis=1) > 0
    b = blocks[occupied]
    count = len(b)
    if count > 65535:
        raise ValueError("16-bit brick index capacity exceeded")
    indices = np.zeros(len(blocks), dtype="<u2")
    indices[occupied] = np.arange(1, count + 1)
    if precision == "half":
        # Unquantized half density reference: 125 values + 3 padding halves.
        records = np.zeros((count, 128), dtype="<f2")
        records[:, :125] = b
        max_error = float(np.max(np.abs(records[:, :125].astype(np.float32) - b))) if count else 0.0
        format_id = 3
    else:
        lo, hi = b.min(axis=1).astype(np.float16), b.max(axis=1).astype(np.float16)
        hi = np.where(
            hi.astype(np.float32) < b.max(axis=1),
            np.nextafter(hi, np.float16(np.inf)),
            hi,
        )
        lo = np.where(
            lo.astype(np.float32) > b.min(axis=1),
            np.nextafter(lo, np.float16(-np.inf)),
            lo,
        )
        span = hi.astype(np.float32) - lo.astype(np.float32)
        q = (
            np.rint((b - lo[:, None]) / np.where(span > 0, span, 1)[:, None] * 15)
            .clip(0, 15)
            .astype(np.uint32)
        )
        max_error = float(np.abs(lo[:, None] + q / 15 * span[:, None] - b).max()) if count else 0.0
        records = np.zeros((count, 17), dtype="<u4")
        records[:, 0] = lo.view(np.uint16).astype(np.uint32) | (
            hi.view(np.uint16).astype(np.uint32) << 16
        )
        for j in range(125):
            records[:, 1 + j // 8] |= q[:, j] << ((j % 8) * 4)
        format_id = 1
    table = indices.tobytes()
    table += bytes((-len(table)) % 4)
    header = struct.pack("<8I", format_id, size, size, size, 8, 8 + len(table) // 4, 0, 0)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + table + records.tobytes())
    print(
        f"{count}/{len(blocks)} bricks; {output.stat().st_size} bytes; max density error {max_error:.6f}"
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--size", type=int, required=True)
    p.add_argument("--blender-z-up", action="store_true")
    p.add_argument("--precision", choices=("4bit", "half"), default="4bit")
    args = p.parse_args()
    pack(args.source, args.output, args.size, args.blender_z_up, args.precision)
