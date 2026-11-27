# SPDX-License-Identifier: MIT
"""Splitting a uniform medium into more physical Q4 bricks must preserve optical depth."""
from pathlib import Path
import struct

import numpy as np

from capture_utils import image, texture
from check_vdb_compute import capture, root


def uniform_frame(folder: Path, dimensions, empty=False):
    folder.mkdir(parents=True, exist_ok=True)
    count = int(np.prod([(n + 3) // 4 for n in dimensions]))
    table = np.zeros(2 * ((count + 1) // 2), dtype="<u2")
    if not empty:
        # Two different record addresses and affine encodings of density 0.5.
        table[:count] = 1 + np.arange(count) % 2
    header = struct.pack("<8I", 1, *dimensions, 8, 8 + len(table) // 2, 0, 0)
    records = struct.pack("<ee", .25, 1.) + bytes([0x55]) * 64
    records += struct.pack("<ee", .5, .5) + bytes(64)
    (folder / "1.bin").write_bytes(header + table.tobytes() + records)
    (folder / "sequence.txt").write_text("1 60\n")


if __name__ == "__main__":
    samples = []
    for label, size, empty in (("coarse", (9, 11, 13), False), ("fine", (18, 22, 26), False),
                               ("empty", (9, 11, 13), True)):
        folder = root / "build" / "uniform-vdb-brick" / label
        uniform_frame(folder, size, empty)
        flags = ["--vdb-brick-march", "--smoke-sequence", str(folder), "--smoke-frame", "0",
                 "--volume-step", ".0125", "--volume-extinction", "1", "--no-volume-early-out", "--no-zero-depth"]
        result = capture("single-brick-uniform-" + label, flags, sponza=False)
        samples.append(texture(result / "native-total-tau.raw", "<f2", 4)[..., 0].astype(np.float32))
    coarse, fine, empty = samples
    assert np.isfinite(coarse).all() and coarse.max() > .1 and not empty.any()
    assert np.array_equal(coarse > 0, fine > 0)
    assert np.allclose(coarse, fine, atol=.003, rtol=.003)
    # The cheaper shading rule changes radiance; the density and ownership must match.
    uniform = root / "build" / "uniform-vdb-brick" / "fine"
    flags = ["--smoke-sequence", str(uniform), "--smoke-frame", "0", "--stress", "25",
             "--volume-step", ".0125", "--volume-extinction", "1", "--no-volume-early-out", "--no-zero-depth"]
    modes = {"step": ["--vdb-brick-march"], "brick": ["--vdb-brick-coarse-light"],
             "vertex": ["--vdb-brick-vertex-light"],
             "vertex-unindexed": ["--vdb-brick-vertex-light", "--no-vdb-brick-indices"]}
    paths = [capture("single-brick-glass-" + name, flags + options, sponza=False)
             for name, options in modes.items()]
    tau = [texture(path / "native-total-tau.raw", "<f2", 4)[..., :3] for path in paths]
    assert all(np.array_equal(tau[0], other) for other in tau[1:])
    for target in ("numerator", "denominator", "back-numerator"):
        name = "native-" + target + ".raw"
        assert (paths[2] / name).read_bytes() == (paths[3] / name).read_bytes(), name
    # Per-step T weights can round on opposite sides of a half-float boundary.
    step, vertex = [texture(path / "native-denominator.raw", "<f2", 4)[..., :3] for path in (paths[0], paths[2])]
    assert all(np.isfinite(x).all() and (x >= 0).all() for x in (step, vertex))
    assert np.abs(step.view("<u2").astype(np.int32) - vertex.view("<u2").astype(np.int32)).max() <= 1
    for path in paths:
        back = texture(path / "native-back-numerator.raw", "<f2", 4)[..., :3]
        assert np.isfinite(back).all() and back.max() > 0
    # Exercise full-rate physical bricks, including corner routing on both triangles.
    full = capture("full-brick-glass-vertex", flags + ["--volume-full-res", "--vdb-brick-vertex-light"], sponza=False)
    assert "Physical Q4 bricks: full resolution" in (full / "run.log").read_text()
    assert texture(full / "native-back-numerator.raw", "<f2", 4)[..., :3].max() > 0
    cached = capture("full-brick-glass-cache", flags + ["--volume-full-res", "--vdb-brick-cache"], sponza=False)
    assert "cached corner lighting" in (cached / "run.log").read_text()
    for name in ("native-denominator.raw", "native-total-tau.raw"):
        assert (full / name).read_bytes() == (cached / name).read_bytes(), name
    assert np.abs(image(full) - image(cached)).max() <= 2
    print("Uniform density, partial bricks, Q4 decoding, empty frame and glass-split shading passed.")
