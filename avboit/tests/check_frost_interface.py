# SPDX-License-Identifier: MIT
"""Check the frost-only source mask against an uncovered HDR foreground edge."""

import argparse
from pathlib import Path
import subprocess

import numpy as np

from capture_utils import texture, work_domain

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
parser.add_argument(
    "--output",
    type=Path,
    default=Path(__file__).resolve().parents[2] / "build/frost-interface-check",
)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)


def capture(name, *flags):
    folder = args.output / name
    folder.mkdir(exist_ok=True)
    command = [
        str(args.viewer.resolve()),
        "--headless",
        "--hardware-depth",
        "--composition-half",
        "--poison-resolve",
        "--render-width",
        "2560",
        "--render-height",
        "1440",
        *flags,
    ]
    run = subprocess.run(command, cwd=folder, capture_output=True)
    (folder / "run.log").write_bytes(run.stdout + run.stderr)
    assert run.returncode == 0, (folder, run.stdout + run.stderr)
    return folder


for dense in (False, True):
    tag = "dense" if dense else "sparse"
    flags = ["--stress", "16", "--dense-blur" if dense else "--sparse-blur"]
    folder = capture(tag, *flags)
    depth = texture(folder / "native-interface-depth.r32f", "<f4", 1)[..., 0]
    covered = depth < 1
    y, x = np.nonzero(covered)
    assert len(x) > 1000, "Missing frost interface"
    yy, xx = np.indices(covered.shape)
    edge = covered & (xx > x.max() - 48) & (abs(yy - (y.min() + y.max()) / 2) < 100)
    hdr = texture(folder / "native-composition.rgba16f", "<f2", 4).astype(np.float32)
    assert np.isfinite(hdr).all()
    # The HDR red source must remain visible outside glass, but must not leak
    # into its grey background. Also reject black-output false positives.
    red_excess = np.maximum(hdr[..., 0] - hdr[..., 1], 0)
    assert red_excess[~covered].max() > 1, "Missing HDR foreground source"
    leakage = red_excess[edge].mean()
    assert leakage < 0.01, (tag, leakage)
    assert hdr[..., :3][edge].mean() > 0.05, "Background became black"
    base = texture(folder / "native-frost-base.rgba16f", "<f2", 4).astype(np.float32)
    valid = work_domain(folder, 1)
    assert np.isfinite(base[valid]).all()
    assert ((base[valid, 3] >= 0) & (base[valid, 3] <= 1.002)).all()
    # The filter is allowed to produce support outside the interface silhouette;
    # restricting the output mask itself would incorrectly remove that halo.
    h, w = base.shape[:2]
    iy = np.minimum(((np.arange(h) + 0.5) * depth.shape[0] / h).astype(int), depth.shape[0] - 1)
    ix = np.minimum(((np.arange(w) + 0.5) * depth.shape[1] / w).astype(int), depth.shape[1] - 1)
    outside = depth[iy[:, None], ix[None, :]] >= 1
    assert (valid & outside & (base[..., 3] > 0)).any(), "Gaussian halo was clipped"
    print(
        f"{tag}: edge red excess {leakage:.6f}; HDR source visible, Gaussian halo preserved",
        flush=True,
    )

# Filter quality only affects the Gaussian results, not upstream OIT or shared B.
sparse, dense = args.output / "sparse", args.output / "dense"
for name in ("native-lut.rgba8", "native-interface-depth.r32f", "native-depth.r32f"):
    assert (sparse / name).read_bytes() == (dense / name).read_bytes(), name
region = work_domain(sparse, 10)
b_sparse = texture(sparse / "native-background.rgba16f", "<f2", 4)[region]
b_dense = texture(dense / "native-background.rgba16f", "<f2", 4)[region]
assert np.array_equal(b_sparse, b_dense), "Filter quality changed shared B"
# Bounds scheduling still covers the complete filter footprint with holes.
roi = capture("holes-roi", "--stress", "10")
full = capture("holes-full", "--stress", "10", "--full-blur")
assert (roi / "native-raster.ppm").read_bytes() == (full / "native-raster.ppm").read_bytes()
print(
    "PASS: coverage rejection, premultiplied validity, preserved halos, shared B and ROI/full parity"
)
