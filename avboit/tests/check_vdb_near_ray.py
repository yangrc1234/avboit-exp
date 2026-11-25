# SPDX-License-Identifier: MIT
"""Compare wave ray LOD against independent rays and check in-ray transitions."""
import csv
import statistics
import numpy as np
from PIL import Image
from capture_utils import texture
from check_vdb_compute import capture

def run(scene, mode, flags):
    options = {"baseline": [], "near": ["--vdb-near-ray-lod"], "quarter": ["--vdb-quarter-ray-reference"]}
    folder = capture("near-ray-" + scene + "-" + mode, [
        "--render-width", "1280", "--render-height", "720", "--vdb-compute", "--benchmark", "180",
        *options[mode], *flags,
    ], sponza=scene != "far")
    with (folder / "native-benchmark.csv").open() as stream:
        rows = csv.DictReader(line for line in stream if not line.startswith("#"))
        ms = statistics.median(float(row["volume_accumulation_ms"]) for row in rows)
    return folder, ms

def compare(scene, flags, quarter=False):
    reference, base_time = run(scene, "baseline", flags)
    print(f"{scene:8} baseline {base_time:.3f} ms", flush=True)
    def read(folder, name):
        return texture(folder / ("native-" + name + ".raw"), "<f2", 4)[..., :3].astype(np.float32)
    baseline = np.asarray(Image.open(reference / "native-raster.ppm")).astype(np.int16)
    ref_tau = read(reference, "total-tau")
    assert np.isfinite(ref_tau).all() and ref_tau.max() > 0
    for mode in (["near", "quarter"] if quarter else ["near"]):
        folder, ms = run(scene, mode, flags)
        rgb = np.asarray(Image.open(folder / "native-raster.ppm")).astype(np.int16)
        tau = read(folder, "total-tau")
        assert np.isfinite(tau).all() and tau.max() > 0
        mask = (tau.max(axis=2) + ref_tau.max(axis=2)) > .02
        error = np.abs(rgb - baseline)[mask]
        print(f"{scene:8} {mode:8} {ms:.3f} ms; active RGB error mean/p99/max "
              f"{error.mean():.3f}/{np.percentile(error,99):.1f}/{error.max()}", flush=True)
        if mode == "near":
            assert error.mean() < 1 and np.percentile(error, 99) <= 6, (scene, error.max())
        if scene == "far" and mode == "near":
            assert np.array_equal(rgb, baseline) and np.array_equal(tau, ref_tau)
        if scene == "glass" and mode == "near":
            # Glass lies inside the volume. Inspect pixels covered by its interface, away from
            # the silhouette where the bilateral upsampler may include neighboring shared rays.
            interface = texture(folder / "native-interface-depth.r32f", "<f4", 1)[...,0] > 0
            inner = interface.copy()
            for dy in range(-4,5):
                for dx in range(-4,5):
                    inner &= np.roll(interface, (dy, dx), axis=(0,1))
            ref_back = read(reference,"back-numerator")
            active = inner & (ref_back.max(axis=2) > .01)
            assert active.sum() > 100
            diff = np.abs(read(folder,"back-numerator")[active] - ref_back[active])
            print(f"Glass interior BackN ({active.sum()} pixels) mean/max {diff.mean():.6g}/{diff.max():.6g}",flush=True)
            assert diff.mean() < .002


def check_depth_transitions():
    # Use a footprint that crosses both thresholds inside this actor. Disable
    # early-out so dense rays can reach the independent tail as well.
    folder = capture("wave-lod-transitions", [
        "--render-width", "896", "--render-height", "504", "--vdb-compute",
        "--vdb-near-ray-lod", "--volume-step-view", "--no-volume-early-out",
    ])
    stats = texture(folder / "native-march-counters.rgba32f", "<f4", 4)
    assert np.isfinite(stats).all() and (stats[..., 0] >= stats[..., 2]).all()
    shaded = stats[..., 1] > 0
    masks = stats[..., 3][shaded]
    assert np.all(np.isin(masks, np.arange(1, 8)))
    transitions = np.count_nonzero(masks == 7)
    print(f"Traversed quad, diagonal and pixel modes on {transitions}/{masks.size} shaded rays", flush=True)
    assert transitions > 100


def check_diagonal_pairs():
    folder = capture("wave-lod-diagonals", [
        "--render-width", "1280", "--render-height", "720", "--vdb-compute",
        "--vdb-near-ray-lod", "--volume-step-view",
    ])
    stats = texture(folder / "native-march-counters.rgba32f", "<f4", 4)
    tl, tr = stats[::2, ::2], stats[::2, 1::2]
    bl, br = stats[1::2, ::2], stats[1::2, 1::2]
    # Mask 6 means these rays finished before switching to independent rays.
    active = np.logical_and.reduce([(p[..., 3] == 6) & (p[..., 1] > 0) for p in (tl, tr, bl, br)])
    assert np.array_equal(tl[active], br[active]) and np.array_equal(tr[active], bl[active])
    different = np.count_nonzero(np.any(tl[active] != tr[active], axis=1))
    print(f"Distinct diagonal counts on {different}/{active.sum()} shared quads", flush=True)
    assert different > 100


if __name__ == "__main__":
    compare("close", [], True)
    compare("dense", ["--volume-extinction","32"])
    compare("inside", ["--camera-preset","11"])
    compare("far", ["--camera-preset","4"])
    compare("glass", ["--stress","25"], True)
    check_depth_transitions()
    check_diagonal_pairs()
