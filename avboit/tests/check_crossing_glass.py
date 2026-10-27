# SPDX-License-Identifier: MIT
"""Measure oblique-glass LUT self-shadowing against exact two-plane compositing.

Compare slope-aware splatting with the original splat and exact sorted RGB over.
--require-clean fails on visible interior stripes in the enabled path. Query-bias
sweeps use the actual captured LUT offline; they do not modify the renderer.
"""

import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw

from capture_utils import texture

COLORS = np.array([[0.15, 0.85, 0.95], [0.95, 0.25, 0.1]])
TRANSMISSION = np.array([[0.2, 0.75, 0.9], [0.9, 0.45, 0.15]])
BACKGROUND = 0.12


def geometry(camera, width, height):
    """Analytic intersections with x=z-4.5 and x=4.5-z; no GPU depth input."""
    angle = np.deg2rad({0: 0, 7: 30, 8: 40, 9: -30}[camera])
    sn, cs = np.sin(angle), np.cos(angle)
    eye = np.array([-4.5 * sn, 0, 4.5 * (1 - cs)])
    sx = (2 * (np.arange(width) + 0.5) / width - 1) * width / height * 0.57735027
    sy = (1 - 2 * (np.arange(height) + 0.5) / height) * 0.57735027
    dx, dz = sn + cs * sx, cs - sn * sx
    depths, masks, interiors = [], [], []
    for nx in (-1, 1):
        with np.errstate(divide="ignore", invalid="ignore"):
            z = (4.5 - nx * eye[0] - eye[2]) / (nx * dx + dz)
        hit_x, hit_y = eye[0] + dx * z, sy[:, None] * z
        valid = (z > 0.05) & (z < 80)
        masks.append(valid & (abs(hit_x) <= 1.5) & (abs(hit_y) <= 1.15))
        interiors.append(valid & (abs(hit_x) < 1.35) & (abs(hit_y) < 0.9))
        depths.append(z)
    depths, masks = np.array(depths), np.array(masks)
    near_a = depths[0] < depths[1]
    # Exclude silhouettes and the genuine change of layer order at the crossing.
    interior = np.all(interiors, axis=0) & (abs(depths[0] - depths[1]) > 0.3)
    return depths, masks, interior, near_a


def exact_image(masks, near_a):
    """Per-pixel sorted RGB over, including each plane exactly once."""
    h, w = masks.shape[1:]
    result = np.full((h, w, 3), BACKGROUND)
    for a_is_layer in (~near_a, near_a):  # far, then near
        for plane in (0, 1):
            selected = masks[plane] & (a_is_layer if plane == 0 else ~a_is_layer)
            result[selected] = (1 - TRANSMISSION[plane]) * COLORS[plane] + TRANSMISSION[
                plane
            ] * result[selected]
    return result


def sample_lut(folder, depths, width, height, bias):
    """Trilinear LUT queries along the middle scanline, with captured adaptive Z."""
    raw = (folder / "native-lut.rgba8").read_bytes()
    vw, vh, vd = np.frombuffer(raw[:12], "<u4")
    lut = np.frombuffer(raw[12:], "u1").reshape(vd, vh, vw, 4)[..., :3] / 255.0
    warp = np.fromfile(folder / "native-warp.u32", "<u4")
    count = int(warp[0])
    virtual = np.log1p(np.clip(depths, 0, 80)) / np.log(81) * count - bias
    v = np.clip(virtual, 0, count - 0.0001)
    vi = v.astype(int)
    mapped = warp[2 + 2 * vi] + (v - vi) * ((warp[3 + 2 * vi] & 1) != 0)
    mapped = np.clip(mapped, 0, vd - 1)
    z0 = mapped.astype(int)
    xs = (np.arange(width) + 0.5) * vw / width - 0.5
    ys = (height // 2 + 0.5) * vh / height - 0.5
    x0, y0 = np.floor(xs).astype(int), int(np.floor(ys))
    result = np.zeros((*depths.shape, 3))
    for oz in (0, 1):
        for oy in (0, 1):
            for ox in (0, 1):
                wz = mapped - z0 if oz else 1 - (mapped - z0)
                wy = ys - y0 if oy else 1 - (ys - y0)
                wx = xs - x0 if ox else 1 - (xs - x0)
                tap = lut[
                    np.minimum(z0 + oz, vd - 1),
                    np.clip(y0 + oy, 0, vh - 1),
                    np.clip(x0 + ox, 0, vw - 1),
                ]
                result += tap * (wz * wy * wx)[..., None]
    result[virtual < 0] = 1
    return result, count, int(vw)


def display(linear):
    v = np.maximum(linear, 0)
    return np.rint((v / (1 + v)) ** (1 / 2.2) * 255).clip(0, 255).astype("u1")


def analyze(folder, camera, frost=False):
    hdr = texture(folder / "native-composition.rgba16f", "<f2", 4)[..., :3].astype("f4")
    assert np.isfinite(hdr).all(), "Non-finite HDR output"
    height, width = hdr.shape[:2]
    depths, masks, interior, near_a = geometry(camera, width, height)
    assert interior.sum() > 1000, "Missing overlapping interior"
    reference = exact_image(masks, near_a)
    error = hdr - reference
    crossing = masks.all(axis=0) & (abs(depths[0] - depths[1]) <= 0.3)
    # Reference is constant within each layer-order region. Adjacent steps here
    # cannot be explained by geometry, material texture or physical lighting.
    pairs = interior[:, :-1] & interior[:, 1:] & (near_a[:-1] == near_a[1:])
    steps = abs(error[:, 1:] - error[:, :-1])[pairs]
    row = height // 2
    active = interior[row]
    alpha = 1 - TRANSMISSION
    total_t = TRANSMISSION.prod(axis=0)
    sweeps = []
    for bias in (0, 2, 4, 8, 16):
        t, virtual_count, vw = sample_lut(folder, depths, width, height, bias)
        near_t = np.where(near_a[:, None], t[0], t[1])
        n = (COLORS[:, None] * alpha[:, None] * t).sum(axis=0)
        d = (alpha[:, None] * t).sum(axis=0)
        predicted = n / np.maximum(d, 1e-6) * (1 - total_t) + total_t * BACKGROUND
        if bias == 2:
            # Validate the offline LUT lookup against actual GPU accumulators.
            gpu_n = texture(folder / "native-numerator.raw", "<f2", 4)[row, :, :3]
            gpu_d = texture(folder / "native-denominator.raw", "<f2", 4)[row, :, :3]
            np.testing.assert_allclose(n[active], gpu_n[active], atol=0.002, rtol=0.004)
            np.testing.assert_allclose(d[active], gpu_d[active], atol=0.002, rtol=0.004)
        # Virtual-depth variation over one low-resolution cell is angle dependent.
        v = np.log1p(np.clip(depths, 0, 80)) / np.log(81) * virtual_count
        cell_span = abs(np.gradient(v, axis=1)) * width / vw
        sweeps.append(
            dict(
                bias=bias,
                minimum_near_T=float(near_t[active].min()),
                rms_reference_error=float(
                    np.sqrt(np.mean((predicted[active] - reference[row, active]) ** 2))
                ),
            )
        )
    max_step = float(steps.max())
    report = dict(
        camera=camera,
        width=width,
        height=height,
        virtual_slices=virtual_count,
        occupied_physical_slices=int(np.fromfile(folder / "native-warp.u32", "<u4")[1]),
        max_virtual_slice_span_per_xy_cell=float(cell_span[:, active].max()),
        interior_pixels=int(interior.sum()),
        rms_vs_sharp_reference=float(np.sqrt(np.mean(error[interior] ** 2))),
        max_interior_adjacent_hdr_step=max_step,
        crossing_rms_vs_sharp_reference=(
            None if frost else float(np.sqrt(np.mean(error[crossing] ** 2)))
        ),
        # Sharp sorted over is not a multilayer frost ground truth. Only gate
        # sharp captures; frost captures show the downstream visual effect.
        visible_stripes=None if frost else max_step > 0.002,
        offline_bias_sweep=sweeps,
    )
    Image.fromarray(display(reference)).save(folder / "exact-sorted.png")
    Image.open(folder / "native-raster.ppm").save(folder / "result.png")
    return report, masks


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer", type=Path, default=root / "bin/avboit_viewer.exe")
    parser.add_argument("--output", type=Path, default=root / "build/crossing-glass")
    parser.add_argument("--reuse", action="store_true", help="Analyze existing captures only")
    parser.add_argument(
        "--require-clean", action="store_true", help="Fail if interior stripes remain"
    )
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    def capture(name, camera, *flags):
        folder = args.output / name
        folder.mkdir(exist_ok=True)
        if not args.reuse:
            command = [
                str(args.viewer.resolve()),
                "--headless",
                "--stress",
                "17",
                "--camera-preset",
                str(camera),
                "--hardware-depth",
                "--accum-half",
                "--composition-half",
                "--render-width",
                "2560",
                "--render-height",
                "1440",
                *flags,
            ]
            run = subprocess.run(command, cwd=folder, capture_output=True)
            (folder / "run.log").write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0, (folder, run.stderr)
        return folder

    report = []
    for camera in (0, 7, 8, 9):
        for frost in (False, True):
            name = f'camera-{camera}-{"frost" if frost else "sharp"}'
            baseline = capture(
                name + "-no-slope", camera, "--no-slope-splat", *([] if frost else ["--no-frost"])
            )
            baseline_row, _ = analyze(baseline, camera, frost)
            baseline_row.update(case=name + "-no-slope", frost=frost, slope_splat=False)
            report.append(baseline_row)
            folder = capture(name, camera, *([] if frost else ["--no-frost"]))
            row, masks = analyze(folder, camera, frost)
            row.update(case=name, frost=frost, slope_splat=True)
            report.append(row)
            print(
                name,
                "max HDR step",
                row["max_interior_adjacent_hdr_step"],
                "minimum near T",
                row["offline_bias_sweep"][1]["minimum_near_T"],
                flush=True,
            )
            if camera == 8 and frost:
                _, xs = np.nonzero(masks.all(axis=0))
                box = (max(0, int(xs.min()) - 24), 560, min(2560, int(xs.max()) + 25), 880)
                panels = [
                    (
                        "Original splat (no frost)",
                        args.output / "camera-8-sharp-no-slope/result.png",
                    ),
                    ("Slope-aware splat (no frost)", args.output / "camera-8-sharp/result.png"),
                    (
                        "Exact sorted RGB over (no frost)",
                        args.output / "camera-8-sharp/exact-sorted.png",
                    ),
                    ("Current AVBOIT + frost", folder / "result.png"),
                ]
                size = ((box[2] - box[0]) * 3, (box[3] - box[1]) * 3)
                canvas = Image.new("RGB", (size[0] * len(panels), size[1] + 36), "#202020")
                draw = ImageDraw.Draw(canvas)
                for index, (title, path) in enumerate(panels):
                    draw.text((index * size[0] + 8, 10), title, fill="white")
                    canvas.paste(
                        Image.open(path).crop(box).resize(size, Image.Resampling.NEAREST),
                        (index * size[0], 36),
                    )
                canvas.save(args.output / "comparison.png")
    for name, flags in (
        ("fine-xy", ["--volume-scale", "2"]),
        ("reversed-order", ["--submission-order", "1"]),
        ("poisoned", ["--poison-extinction"]),
        ("dense", ["--dense-extinction"]),
    ):
        folder = capture(name, 8, "--no-frost", *flags)
        row, _ = analyze(folder, 8)
        row.update(case=name, frost=False, slope_splat=True)
        report.append(row)
        if name != "fine-xy":
            base = args.output / "camera-8-sharp"
            assert (folder / "native-lut.rgba8").read_bytes() == (
                base / "native-lut.rgba8"
            ).read_bytes()
            assert (folder / "native-raster.ppm").read_bytes() == (
                base / "native-raster.ppm"
            ).read_bytes()
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    failed = [r["case"] for r in report if r["slope_splat"] and r["visible_stripes"]]
    print("Quality result:", "STRIPES REPRODUCED: " + ", ".join(failed) if failed else "CLEAN")
    if args.require_clean and failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
