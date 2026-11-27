# SPDX-License-Identifier: MIT
"""Compare classified proxy coverage and glass-split accumulation against the unclassified rectangle."""
import numpy as np

from capture_utils import texture
from check_vdb_brick import uniform_frame
from check_vdb_compute import capture, root


def render(flags):
    # Reuse one capture directory. Only the relevant channels are kept in RAM.
    folder = capture("classified-brick-scratch", flags, sponza=False)
    return [texture(folder / f"native-{name}.raw", "<f2", 4)[..., :3].astype(np.float32)
            for name in ("total-tau", "denominator", "back-numerator")]


def check():
    sequence = root / "build" / "uniform-vdb-brick" / "classifier"
    uniform_frame(sequence, (18, 22, 26))
    flags = ["--stress", "25", "--smoke-sequence", str(sequence), "--smoke-frame", "0",
             "--volume-step", ".0125", "--volume-extinction", "1", "--no-zero-depth",
             "--no-volume-early-out", "--no-taa"]
    for resolution in ("--volume-half-res", "--volume-full-res"):
        reference = render(flags + [resolution, "--vdb-brick-march"])
        assert reference[0].max() > .1 and reference[2].max() > 0
        for preset in ("balanced", "performance", "low"):
            rect = render(flags + [resolution, "--vdb-brick-lod", preset])
            cube = render(flags + [resolution, "--vdb-brick-classify-cube", "--vdb-brick-lod", preset])
            for actual in (rect, cube):
                assert all(np.isfinite(target).all() and (target >= 0).all() for target in actual)
                # At constant density, midpoint length and the reference jittered lattice
                # differ by at most one step per contiguous one-step run, plus FP16 blending.
                one_step_tau = .5 * .0125
                diff = np.abs(actual[0] - reference[0])
                assert diff.mean() < one_step_tau and diff.max() < 4 * one_step_tau, (
                    resolution, preset, diff.mean(), diff.max())
                assert np.all(actual[0][reference[0] > 4 * one_step_tau] > 0)
                assert np.any(reference[2] > .05) and np.all(actual[2][reference[2] > .05] > 0)
            # Geometry choice must preserve optical depth, T weighting, and glass-split radiance.
            for name, a, b in zip(("tau", "denominator", "back-numerator"), rect, cube):
                diff = np.abs(a - b)
                assert diff.mean() < .0005 and diff.max() < .035, (resolution, preset, name, diff.max())
    print("All brick LOD presets preserve uniform density and glass-split coverage on both proxies.")


if __name__ == "__main__":
    check()
