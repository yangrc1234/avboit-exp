# SPDX-License-Identifier: MIT
"""Generate deterministic periodic Perlin-Worley R8 noise (NumPy only).

64^3 interior plus a wrapped texel on both sides supports seamless trilinear
sampling with the renderer's existing clamp sampler. No runtime procedural noise.
"""
from pathlib import Path
import struct

import numpy as np


def perlin(p, frequency, seed):
    rng = np.random.default_rng(seed)
    gradients = rng.normal(size=(frequency, frequency, frequency, 3))
    gradients /= np.linalg.norm(gradients, axis=-1, keepdims=True)
    q = p * frequency
    cell = np.floor(q).astype(np.int32)
    f = q - cell
    fade = f * f * f * (f * (f * 6 - 15) + 10)
    result = np.zeros(p.shape[:-1])
    for z in range(2):
        for y in range(2):
            for x in range(2):
                offset = np.array([x, y, z])
                c = (cell + offset) % frequency
                g = gradients[c[..., 2], c[..., 1], c[..., 0]]
                weight = np.prod(np.where(offset, fade, 1 - fade), axis=-1)
                result += weight * np.sum(g * (f - offset), axis=-1)
    return result


def worley(p, frequency, seed):
    features = np.random.default_rng(seed).random((frequency, frequency, frequency, 3))
    q = p * frequency
    cell = np.floor(q).astype(np.int32)
    f = q - cell
    nearest = np.full(p.shape[:-1], np.inf)
    for z in range(-1, 2):
        for y in range(-1, 2):
            for x in range(-1, 2):
                offset = np.array([x, y, z])
                c = (cell + offset) % frequency
                d = offset + features[c[..., 2], c[..., 1], c[..., 0]] - f
                nearest = np.minimum(nearest, np.sum(d * d, axis=-1))
    return 1 - np.clip(np.sqrt(nearest), 0, 1)


def generate():
    n = 64
    z, y, x = np.mgrid[:n, :n, :n]
    p = (np.stack([x, y, z], axis=-1) + 0.5) / n
    # Broad Perlin structure, with smaller cellular lobes precombined offline.
    base = np.clip(
        0.5 + 1.4 * (perlin(p, 2, 41) + 0.5 * perlin(p, 4, 42) + 0.25 * perlin(p, 8, 43)) / 1.75,
        0,
        1,
    )
    cellular = 0.625 * worley(p, 4, 51) + 0.25 * worley(p, 8, 52) + 0.125 * worley(p, 16, 53)
    density = np.clip((base + 1 - cellular) / (2 - cellular), 0, 1)
    # Contrast-normalize once, not per brick, keeping interpolation continuous.
    lo, hi = np.percentile(density, [2, 98])
    density = np.clip((density - lo) / max(hi - lo, 0.001), 0, 1)
    packed = np.pad(np.rint(density * 255).astype(np.uint8), 1, mode="wrap")
    # Check periodic evaluation, including negative coordinates.
    probe = np.array([[0.123, 0.456, 0.789], [-0.03, 0.5, 1.01]])
    for fn in (perlin, worley):
        assert np.allclose(fn(probe, 4, 41), fn(probe + [1, -1, 1], 4, 41), atol=1e-10)
    side = n + 2
    header = [124, 0x80100F, side, side, side, side, 1] + [0] * 11
    header += [32, 4, int.from_bytes(b"DX10", "little"), 0, 0, 0, 0, 0]
    header += [0x1008, 0x200000, 0, 0, 0]
    path = Path(__file__).resolve().parents[1] / "assets/textures/smoke-noise.dds"
    # DXGI R8_UNORM=61; TEXTURE3D=4.
    path.write_bytes(b"DDS " + struct.pack("<31I5I", *header, 61, 4, 0, 1, 0) + packed.tobytes())
    print(f"Wrote {path}: {side}^3 R8, {packed.nbytes} bytes; periodic checks PASS")


if __name__ == "__main__":
    generate()
