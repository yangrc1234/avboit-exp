# SPDX-License-Identifier: MIT
"""Check the opaque depth ABI using real GPU D32 and legacy FP16 alpha captures."""
import math
from pathlib import Path
import struct
import subprocess
import tempfile

viewer = Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
with tempfile.TemporaryDirectory(prefix="avboit-depth-") as directory:
    root = Path(directory)
    for camera in range(6):
        result = subprocess.run(
            [
                str(viewer),
                "--headless",
                "--legacy-scene",
                "--hardware-depth",
                "--composition-half",
                "--camera-preset",
                str(camera),
            ],
            cwd=root,
            capture_output=True,
        )
        assert result.returncode == 0, result.stderr.decode(errors="replace")
        depth = (root / "native-depth.r32f").read_bytes()
        # OM composition preserves legacy depth alpha in the shared SceneColor.
        color = (root / "native-scene-color.rgba16f").read_bytes()
        assert depth[:8] == color[:8]
        width, height = struct.unpack("<II", depth[:8])
        assert len(depth) == 8 + width * height * 4
        assert len(color) == 8 + width * height * 8
        worst = 0
        covered = 0
        for (d,), rgba in zip(
            struct.iter_unpack("<f", depth[8:]), struct.iter_unpack("<eeee", color[8:])
        ):
            assert math.isfinite(d) and 0 <= d <= 1
            z = (-4 / 79.95) / (d - 80 / 79.95)
            reference = rgba[3]
            # Legacy alpha has ten mantissa bits. One ULP includes raster depth
            # interpolation/reconstruction error as well as FP16 quantization.
            ulp = 2 ** (math.floor(math.log2(max(reference, 2**-14))) - 10)
            error = abs(z - reference) / ulp
            worst = max(worst, error)
            # Bound D32/projection arithmetic in device-depth space, then
            # propagate it through dz/dd = z^2 / abs(B). This is separate
            # from the legacy FP16 target conversion error.
            depth_roundoff = z * z / (4 / 79.95) * (2**-21)
            assert abs(z - reference) <= ulp + depth_roundoff, (camera, d, z, reference, error)
            covered += d < 1
        assert covered > 0, "Depth test ran on an empty scene"
        print(
            f"Camera {camera}: {covered} covered pixels, max depth error {worst:.4f} FP16 ULP",
            flush=True,
        )
