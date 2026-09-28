# SPDX-License-Identifier: MIT
"""Check the zero-T shading shortcut preserves all observable four-RT terms."""
from pathlib import Path
import argparse, struct, subprocess, tempfile

parser = argparse.ArgumentParser()
parser.add_argument(
    "--viewer", type=Path, default=Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="avboit-zero-t-") as scratch:
    scratch = Path(scratch)
    for half in (False, True):
        for stress in (1, 6, 11):
            results = []
            for reference in (False, True):
                command = [
                    str(args.viewer.resolve()),
                    "--headless",
                    "--poison-resolve",
                    "--no-zero-depth",
                    "--stress",
                    str(stress),
                ]
                if half:
                    command += ["--accum-half"]
                command += ["--no-zero-t-shortcut" if reference else "--zero-t-shortcut"]
                run = subprocess.run(command, cwd=scratch, capture_output=True)
                if run.returncode:
                    raise RuntimeError((run.stdout + run.stderr).decode(errors="replace"))
                results.append(
                    [
                        (scratch / name).read_bytes()
                        for name in (
                            "native-raster.ppm",
                            "native-lut.rgba8",
                            "native-background.rgba16f",
                            "native-total-tau.raw",
                        )
                    ]
                )
            assert (
                results[0] == results[1]
            ), f"Shortcut changed outputs: half={half}, stress={stress}"
            if stress == 11:
                lut = results[0][1]
                w, h, d = struct.unpack("<III", lut[:12])
                center = 12 + ((d - 1) * w * h + (h // 2) * w + w // 2) * 4
                assert lut[center : center + 3] == bytes(
                    3
                ), "Fixture did not saturate all RGB channels"
                if half:
                    tau = results[0][3]
                    tw, th = struct.unpack("<II", tau[:8])
                    rgb = struct.unpack_from("<4e", tau, 8 + ((th // 2) * tw + tw // 2) * 8)[:3]
                    assert min(rgb) > 25, "Hidden fragments stopped contributing to totalTau"
            print(f"PASS: stress={stress}, half={half}: image/LUT/B/totalTau exact", flush=True)
