# SPDX-License-Identifier: MIT
"""Read renderer captures and reconstruct the domains actually written this frame."""
import numpy as np
from PIL import Image


def texture(path, dtype, channels):
    with path.open("rb") as f:
        w, h = np.fromfile(f, "<u4", 2)
        return np.fromfile(f, dtype).reshape(int(h), int(w), channels)


def work_domain(folder, stage):
    info = np.fromfile(folder / "native-work-layout.u32", "<u4").reshape(11, 4)
    w, h, base, capacity = map(int, info[stage])
    if stage == 10:
        tiles = texture(folder / "native-screen-tiles.r32u", "<u4", 1)[..., 0]
        domain = np.repeat(np.repeat((tiles & 4) != 0, 64, axis=0), 64, axis=1)[:h, :w]
        padded = np.pad(domain, 1)
        return np.logical_or.reduce(
            [padded[y : y + h, x : x + w] for y in range(3) for x in range(3)]
        )
    if capacity == 0:
        return np.zeros((h, w), dtype=bool)
    bits = np.fromfile(folder / "native-work-masks.u32", "<u4")[base + 1 : base + 1 + capacity]
    bits = bits.reshape((h + 3) // 4, (w + 3) // 4)
    return np.repeat(np.repeat(bits != 0, 4, axis=0), 4, axis=1)[:h, :w]


def image(path):
    return np.asarray(Image.open(path / "native-raster.ppm")).astype(np.float32)


def difference(a, b):
    d = np.abs(a - b)
    return dict(
        max=float(d.max()), rms=float(np.sqrt(np.mean(d * d))), p99=float(np.percentile(d, 99))
    )
