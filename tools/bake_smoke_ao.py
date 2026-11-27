# SPDX-License-Identifier: MIT
"""Bake an animated, low-frequency ambient visibility field alongside a smoke sequence."""
import argparse
from itertools import product
from pathlib import Path
import struct
import tempfile

import numpy as np

from downsample_smoke_sequence import area_weights


def density_grid(path):
    raw = path.read_bytes()
    kind, nx, ny, nz, table, payload, _, _ = struct.unpack_from("<8I", raw)
    if kind not in (1, 3) or table != 8:
        raise ValueError(f"Unsupported smoke brick format in {path}")
    grid = tuple((n + 3) // 4 for n in (nz, ny, nx))
    indices = np.frombuffer(raw, "<u2", int(np.prod(grid)), table * 4).reshape(grid)
    if kind == 3:
        samples = np.frombuffer(raw, "<f2", offset=payload * 4).reshape(-1, 128)[:, :125]
    else:
        records = np.frombuffer(raw, "u1", offset=payload * 4).reshape(-1, 68)
        low, high = records[:, :4].copy().view("<f2").astype("f4").T
        packed = records[:, 4:]
        codes = np.stack((packed & 15, packed >> 4), axis=-1).reshape(len(records), 128)
        samples = low[:, None] + codes[:, :125] * ((high - low) / 15)[:, None]
    blocks = np.zeros((len(samples) + 1, 4, 4, 4), dtype="f4")
    blocks[1:] = samples.reshape(-1, 5, 5, 5)[:, :4, :4, :4]
    dense = blocks[indices].transpose(0, 3, 1, 4, 2, 5).reshape(tuple(n * 4 for n in grid))
    return dense[:nz, :ny, :nx]


def resize_grid(density, size):
    coarse = density
    for axis in range(3):
        weights = area_weights(coarse.shape[axis], size)
        coarse = np.moveaxis(np.tensordot(weights, coarse, axes=(1, axis)), 0, axis)
    return coarse


def ambient_visibility(density, size, optical_scale):
    # Integrate on a finer grid before averaging visibility into the stored cells.
    coarse = resize_grid(density, max(size, min(max(density.shape), 24)))
    resolution = coarse.shape[0]
    visibility = np.zeros_like(coarse)
    # 26-point Lebedev spherical quadrature. Integrate optical depth toward the
    # open boundary, then average transmission (not optical depth) over directions.
    weights = (0., 1 / 21, 4 / 105, 9 / 280)
    for direction in product((-1, 0, 1), repeat=3):
        axes = sum(abs(v) for v in direction)
        if not axes:
            continue
        column = .5 * coarse
        for step in range(1, resolution):
            delta = [v * step for v in direction]
            dst = tuple(slice(max(0, -v), resolution - max(0, v)) for v in delta)
            src = tuple(slice(max(0, v), resolution - max(0, -v)) for v in delta)
            column[dst] += coarse[src]
        visibility += weights[axes] * np.exp(column * (-optical_scale * np.sqrt(axes) / resolution))
    return np.clip(resize_grid(visibility, size), 0., 1.)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sequence", type=Path)
    parser.add_argument("--size", type=int, choices=range(1, 65), metavar="1..64",
                        help="Default: one visibility texel per eight bricks along the longest axis")
    parser.add_argument("--optical-scale", type=float, default=20.4,
                        help="Reference extinction times the width of the normalized cubic volume")
    args = parser.parse_args()
    count, _ = (args.sequence / "sequence.txt").read_text().split()
    if not np.isfinite(args.optical_scale) or args.optical_scale <= 0:
        parser.error("optical scale must be finite and positive")
    header = np.fromfile(args.sequence / "1.bin", "<u4", count=4)
    size = args.size or (int(max(header[1:4])) + 31) // 32
    target = args.sequence / "ambient-visibility.bin"
    with tempfile.NamedTemporaryFile(dir=args.sequence, delete=False) as temp:
        temp_path = Path(temp.name)
    try:
        # A common scale across frames preserves precision without per-frame pumping.
        with tempfile.TemporaryFile() as columns, temp_path.open("wb") as out:
            max_column = 0.
            for i in range(1, int(count) + 1):
                visibility = ambient_visibility(density_grid(args.sequence / f"{i}.bin"), size, args.optical_scale)
                column = (-np.log(np.maximum(visibility, np.finfo("f4").tiny)) / args.optical_scale).astype("<f4")
                max_column = max(max_column, float(column.max()))
                columns.write(column.tobytes())
            out.write(struct.pack("<4sIIf", b"VAO2", size, int(count), max_column))
            columns.seek(0)
            for _ in range(int(count)):
                field = np.frombuffer(columns.read(size**3 * 4), "<f4")
                out.write(np.rint(field * (255. / max_column if max_column else 0.)).clip(0, 255).astype("u1").tobytes())
        temp_path.replace(target)
    finally:
        temp_path.unlink(missing_ok=True)
    print(f"{count} frames, {size} cubed, {target.stat().st_size} bytes: {target}")


if __name__ == "__main__":
    main()
