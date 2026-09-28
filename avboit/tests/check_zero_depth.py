# SPDX-License-Identifier: MIT
"""Validate in-place Zero-T SceneDepth, resolve closure and frost holes.

Resolve reads the filtered LUT at SceneDepth, closing RGB-zero rays without
stencil or a cutoff texture. Accumulated tau itself remains a surviving sum.
Requires NumPy. Default captures are 2560x1440, plus input-resolution alignment coverage.
"""
import argparse, math, struct, subprocess
from pathlib import Path
import numpy as np
from capture_utils import work_domain

parser = argparse.ArgumentParser()
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
parser.add_argument(
    "--output", type=Path, default=Path(__file__).resolve().parents[2] / "build/zero-depth-check"
)
a = parser.parse_args()
a.output.mkdir(parents=True, exist_ok=True)


def texture(path, dtype, channels=1):
    raw = path.read_bytes()
    w, h = struct.unpack_from("<II", raw)
    pixels = np.frombuffer(raw[8:], dtype=dtype).reshape(h, w, channels)
    assert np.isfinite(pixels).all(), path
    return pixels if channels > 1 else pixels[:, :, 0]


def capture(name, stress, *options):
    folder = a.output / name
    folder.mkdir(exist_ok=True)
    run = subprocess.run(
        [
            str(a.viewer.resolve()),
            "--headless",
            "--poison-resolve",
            "--hardware-depth",
            "--accum-half",
            "--composition-half",
            "--stress",
            str(stress),
            *options,
        ],
        cwd=folder,
        capture_output=True,
    )
    (folder / "run.log").write_bytes(run.stdout + run.stderr)
    assert run.returncode == 0, (folder, run.stdout + run.stderr)
    return folder


def check(folder, original=None, enabled=True):
    depth = texture(folder / "native-depth.r32f", "<f4")
    h, w = depth.shape
    interface = texture(folder / "native-interface-depth.r32f", "<f4")
    surface = texture(folder / "native-interface-surface.rgba16f", "<f2", 4)
    b = texture(folder / "native-background.rgba16f", "<f2", 4)
    raw = (folder / "native-lut.rgba8").read_bytes()
    vw, vh, vd = struct.unpack_from("<III", raw)
    lut = np.frombuffer(raw[12:], dtype=np.uint8).reshape(vd, vh, vw, 4)
    warp = np.fromfile(folder / "native-warp.u32", dtype="<u4")
    # The runtime no longer emits quad records. Reconstruct conservative
    # candidates on the CPU from integration metadata, then verify actual D32
    # coverage and every filtered LUT neighbor independently below.
    zero = texture(folder / "native-zero-slice.r32u", "<u4")
    count = int(warp[0])
    prefix = warp[2 : 2 + count * 2 : 2]
    quads = []
    if enabled:
        for y0 in range(0, h, 16):
            for x0 in range(0, w, 16):
                x1 = min(x0 + 16, w)
                y1 = min(y0 + 16, h)
                lx = max(0, math.floor((x0 + 0.5) * vw / w - 0.5) - 1)
                hx = min(vw, math.floor((x1 - 0.5) * vw / w - 0.5) + 3)
                ly = max(0, math.floor((y0 + 0.5) * vh / h - 0.5) - 1)
                hy = min(vh, math.floor((y1 - 0.5) * vh / h - 0.5) + 3)
                first = int(zero[ly:hy, lx:hx].max())
                if first >= vd:
                    continue
                boundary = int(np.searchsorted(prefix, first)) + 3
                if boundary >= count:
                    continue
                z = float(
                    np.float32(
                        np.exp2(np.float32(boundary / count) * np.float32(math.log2(81)))
                        - np.float32(1)
                    )
                )
                if not 0.05 < z < 80:
                    continue
                device = float(
                    np.float32(np.float32(80 / 79.95) + np.float32(-4 / 79.95) / np.float32(z))
                )
                quads.append((x0, y0, x1, y1, device))
    expected = texture(original / "native-depth.r32f", "<f4").copy() if original else None
    quad_depth = np.ones_like(depth)
    seen = set()
    for quad in quads:
        x0, y0, x1, y1 = map(int, quad[:4])
        d = float(quad[4])
        z = (-4 / 79.95) / (d - 80 / 79.95)
        assert (x0, y0) not in seen
        seen.add((x0, y0))
        assert x0 % 16 == 0 and y0 % 16 == 0 and x1 == min(x0 + 16, w) and y1 == min(y0 + 16, h)
        virtual = math.log1p(z) / math.log(81) * int(warp[0]) - 2
        vi = int(virtual)
        mapped = int(warp[2 + vi * 2]) + (virtual - vi if warp[3 + vi * 2] & 1 else 0)
        lower = math.floor(mapped)
        upper = min(lower + 1, vd - 1)
        assert 0 <= lower < vd, (quad, z, mapped)
        # Union of all bilinear footprints within the quad, at BOTH Z taps.
        lx = max(0, math.floor((x0 + 0.5) * vw / w - 0.5))
        hx = min(vw, math.floor((x1 - 0.5) * vw / w - 0.5) + 2)
        ly = max(0, math.floor((y0 + 0.5) * vh / h - 0.5))
        hy = min(vh, math.floor((y1 - 0.5) * vh / h - 0.5) + 2)
        assert not lut[lower : upper + 1, ly:hy, lx:hx, :3].any(), ("Nonzero LUT footprint", quad)
        quad_depth[y0:y1, x0:x1] = d
        if expected is not None:
            np.minimum(expected[y0:y1, x0:x1], d, out=expected[y0:y1, x0:x1])
    if expected is not None:
        assert (
            np.max(np.abs(depth - expected)) < 3e-7
        ), "SceneDepth differs from min(original, quad)"
        assert (folder / "native-lut.rgba8").read_bytes() == (
            original / "native-lut.rgba8"
        ).read_bytes(), "Zero draw changed LUT"
    assert (depth <= quad_depth + 3e-7).all()
    hidden = (interface < 1) & (interface >= depth) & (quad_depth < 1) & work_domain(folder, 10)
    visible = (interface < depth) & (quad_depth < 1)
    # Sharp refraction permits foreground sampling; frost does not.
    assert (b[hidden & (surface[:, :, 3] > 0)] == 0).all(), "Hidden frost sample still valid"
    # A winning quad guarantees RGB-zero LUT at SceneDepth. With no visible
    # interface, B and the final HDR result must be N/A: no residual opaque
    # contribution, and no old (1-exp(-truncatedTau)) normalization factor.
    closed = (
        (quad_depth < 1)
        & (np.abs(depth - quad_depth) < 3e-7)
        & ((interface >= 1) | (interface >= depth))
        & (b[:, :, 3] > 0)
        & work_domain(folder, 10)
    )
    if closed.any():
        n = texture(folder / "native-numerator.raw", "<f2", 4)[:, :, :3].astype(np.float32)
        d = texture(folder / "native-denominator.raw", "<f2", 4)[:, :, :3].astype(np.float32)
        expected_color = n[closed] / np.maximum(d[closed], 0.000001)
        np.testing.assert_allclose(b[:, :, :3][closed], expected_color, rtol=0.001, atol=2e-6)
        final = texture(folder / "native-composition.rgba16f", "<f2", 4)
        np.testing.assert_allclose(final[:, :, :3][closed], expected_color, rtol=0.001, atol=2e-6)
    print(
        folder.name,
        "quads",
        len(quads),
        "hidden",
        hidden.sum(),
        "front glass",
        visible.sum(),
        flush=True,
    )
    return len(quads), hidden.sum(), visible.sum()


front = capture("front", 11)
reference = capture("front-off", 11, "--no-zero-depth")
assert check(front, reference)[1] > 100
assert check(reference, enabled=False)[0] == 0
assert (front / "native-depth.r32f").read_bytes() != (
    reference / "native-depth.r32f"
).read_bytes(), "Main depth not modified"
tau_on = texture(front / "native-total-tau.raw", "<f2", 4)
tau_off = texture(reference / "native-total-tau.raw", "<f2", 4)
assert ((tau_on[:, :, 0] + 1) < tau_off[:, :, 0]).sum() > 100, "No hardware fragment rejection"
# The thin foreground bar lies before all extinction, but its sub-voxel
# footprint has RGB-zero LUT farther away. Sampling the last slice instead of
# SceneDepth would black it out. Its HDR color must survive unchanged.
depth = texture(front / "native-depth.r32f", "<f4")
near = depth < (80 / 79.95 - 4 / 79.95 / 1.5)
assert near.sum() > 100, "Missing foreground occluder"
final = texture(front / "native-composition.rgba16f", "<f2", 4)
original = texture(reference / "native-composition.rgba16f", "<f2", 4)
assert np.array_equal(final[near], original[near]), "Zero-T hid foreground opaque geometry"
assert (final[near, 0] > 7).all(), "Foreground HDR bar lost its radiance"
back = capture("back", 12)
assert check(back)[2] > 100
rgb = capture("rgb", 13)
assert check(rgb)[0] == 0
for budget in (16, 32, 64, 128):
    for fixed in (False, True):
        flags = [
            "--depth-budget",
            str(budget),
            *(["--fixed-z"] if fixed else []),
            "--render-width",
            "641",
            "--render-height",
            "385",
            "--volume-scale",
            "8",
        ]
        folder = capture(f"z{budget}-fixed{int(fixed)}", 11, *flags)
        original = capture(f"z{budget}-fixed{int(fixed)}-off", 11, *flags, "--no-zero-depth")
        check(folder, original)
for time in ("0", "0.7", "2"):
    folder = capture("smoke-" + time, 14, "--time", time)
    assert check(folder)[1] > 0
mirror = capture("smoke-mirror-off", 14, "--time", "0.7", "--no-mirror-blur")
assert (mirror / "native-background.rgba16f").read_bytes() == (
    a.output / "smoke-0.7/native-background.rgba16f"
).read_bytes()
assert (mirror / "native-raster.ppm").read_bytes() != (
    a.output / "smoke-0.7/native-raster.ppm"
).read_bytes(), "Mirror switch did not exercise a hole"
rgb_off = capture("rgb-off", 13, "--no-zero-depth")
assert (rgb / "native-raster.ppm").read_bytes() == (
    rgb_off / "native-raster.ppm"
).read_bytes(), "No-zero RGB scene changed"
for sparse in (False, True):
    flags = ["--sparse-blur"] if sparse else ["--dense-blur"]
    roi = capture("roi-" + str(sparse), 14, "--time", "0.7", *flags)
    full = capture("full-" + str(sparse), 14, "--time", "0.7", "--full-blur", *flags)
    assert (roi / "native-raster.ppm").read_bytes() == (
        full / "native-raster.ppm"
    ).read_bytes(), "Mirror ROI footprint mismatch"
for name, flag in [("poison", "--poison-extinction"), ("dense", "--dense-extinction")]:
    other = capture(name, 14, "--time", "0.7", flag)
    assert (other / "native-depth.r32f").read_bytes() == (
        a.output / "smoke-0.7/native-depth.r32f"
    ).read_bytes(), "Zero depth stale or dense/sparse mismatch"
print(
    "PASS: LUT zeros, in-place depth, resolve closure, tau truncation, frost, RGB, mirror, ROI, Z budgets, dense/poison"
)
