# SPDX-License-Identifier: MIT
"""2K A/B sweep; differences are not ground-truth errors. Writes an HTML gallery."""
import argparse, json, re, subprocess, tempfile, html
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from capture_utils import texture


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=root / "build/lut-min-scenes")
    parser.add_argument("--scenes", type=int, nargs="+", default=list(range(31)))
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    names = re.findall(
        r'"([^"\n]+)"',
        (root / "avboit/sample/StressFixtures.cpp")
        .read_text()
        .split("static const char *names[] =", 1)[1]
        .split("};", 1)[0],
    )
    rows = []
    sections = []
    thumbs = []
    for scene in args.scenes:
        images = []
        hdrs = []
        zero_checks = {}
        for mode in ("off", "on"):
            # Only these newly created temporary captures are removed on exit.
            with tempfile.TemporaryDirectory(prefix=f"capture-{scene}-{mode}-", dir=out) as temp:
                folder = Path(temp)
                assert folder.resolve().is_relative_to(out)
                cmd = [
                    str(root / "bin/avboit_viewer.exe"),
                    "--headless",
                    "--no-taa",
                    "--volume-full-res",
                    "--no-lut-min-filter",
                    "--stress",
                    str(scene),
                    "--render-width",
                    "2560",
                    "--render-height",
                    "1440",
                ]
                if mode == "on":
                    cmd += ["--lut-min-filter"]
                run = subprocess.run(cmd, cwd=folder, capture_output=True, timeout=180)
                (out / f"{scene:02}-{mode}.log").write_bytes(run.stdout + run.stderr)
                if run.returncode:
                    raise RuntimeError(f"scene {scene} {mode}, see log")
                im = Image.open(folder / "native-raster.ppm").convert("RGB")
                im.save(out / f"{scene:02}-{mode}.png")
                images.append(im)
                hdr = texture(folder / "native-composition.rgba16f", "<f2", 4)[..., :3].astype(
                    "float32"
                )
                assert np.isfinite(hdr).all()
                hdrs.append(hdr)
                raw = (folder / "native-lut.rgba8").read_bytes()
                w, h, d = map(int, np.frombuffer(raw[:12], "<u4"))
                lut = np.frombuffer(raw[12:], "u1").reshape(d, h, w, 4)[..., :3]
                assert np.all(np.diff(lut.astype("int16"), axis=0) <= 0), scene
                zero = texture(folder / "native-zero-slice.r32u", "<u4", 1)[..., 0]
                closed = np.all(lut == 0, axis=3)
                first = closed.argmax(axis=0)
                active = closed.any(axis=0)
                mismatch = active & (zero != first)
                zero_checks[mode] = dict(
                    mismatch_columns=int(mismatch.sum()),
                    max_slice_delta=(
                        int(abs(zero[active].astype("int64") - first[active]).max())
                        if active.any()
                        else 0
                    ),
                    invalid_sentinel=int((zero[~active] < d).sum()),
                )
        a, b = [np.asarray(i).astype("float32") for i in images]
        diff = abs(a - b).max(axis=2)
        hdrdiff = abs(hdrs[0] - hdrs[1]).max(axis=2)
        # Find a region with large average visible change, rather than one outlier.
        smooth = np.asarray(Image.fromarray(diff.astype("uint8")).filter(ImageFilter.BoxBlur(40)))
        y, x = np.unravel_index(smooth.argmax(), smooth.shape)
        x = int(np.clip(x, 160, 2400))
        y = int(np.clip(y, 160, 1280))
        box = (x - 160, y - 160, x + 160, y + 160)
        row = dict(
            scene=scene,
            name=names[scene],
            zero_checks=zero_checks,
            display_mean=float(diff.mean()),
            changed_percent=float((diff > 2).mean() * 100),
            display_p99=float(np.percentile(diff, 99)),
            hdr_mean=float(hdrdiff.mean()),
            crop=box,
        )
        rows.append(row)
        panel = Image.new("RGB", (960, 356), "#202020")
        draw = ImageDraw.Draw(panel)
        for i, label in enumerate(("OFF", "ON", "Absolute display difference x4")):
            draw.text((320 * i + 8, 10), label, fill="white")
            im = (
                images[i]
                if i < 2
                else Image.fromarray(np.minimum(abs(a - b) * 4, 255).astype("uint8"))
            )
            panel.paste(im.crop(box), (320 * i, 36))
        panel.save(out / f"{scene:02}-detail.png")
        tile = Image.new("RGB", (640, 210), "#202020")
        draw = ImageDraw.Draw(tile)
        draw.text((8, 5), f"{scene}: {names[scene]}", fill="white")
        for i, im in enumerate(images):
            tile.paste(im.resize((320, 180)), (320 * i, 30))
        thumbs.append(tile)
        sections.append(
            f'<section><h2>{scene}: {html.escape(names[scene])}</h2><p>OFF / ON. Changed pixels (&gt;2/255): {row["changed_percent"]:.2f}%. Difference is not error.</p><div><a href="{scene:02}-off.png"><img src="{scene:02}-off.png"></a><a href="{scene:02}-on.png"><img src="{scene:02}-on.png"></a></div><img class="detail" src="{scene:02}-detail.png"></section>'
        )
        (out / "report.json").write_text(json.dumps(rows, indent=2))
        print(
            f'{scene}: {row["changed_percent"]:.2f}% changed, mean {row["display_mean"]:.3f}',
            flush=True,
        )
    for start in range(0, len(thumbs), 10):
        subset = thumbs[start : start + 10]
        sheet = Image.new("RGB", (1280, 210 * ((len(subset) + 1) // 2)), "#202020")
        for i, tile in enumerate(subset):
            sheet.paste(tile, ((i % 2) * 640, (i // 2) * 210))
        sheet.save(out / f"overview-{start//10}.png")
    (out / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>T-LUT min-filter comparison</title><style>body{background:#181818;color:#ddd;font:16px sans-serif;margin:24px}section{margin-bottom:48px}div{display:flex}div img{width:100%}div a{width:50%}.detail{max-width:100%}</style><h1>T-LUT XY min-filter: all presets, 2560x1440</h1><p>Default packed accumulation, ordinary splat, linear T-LUT. Static frames without TAA. Each pair: OFF / ON. Click full images for 1:1 inspection.</p>'
        + "".join(sections),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
