# SPDX-License-Identifier: MIT
"""Convert ambientCG Plaster001_1K-PNG.zip NormalDX to the bundled linear RG8 DDS."""

import argparse
from io import BytesIO
from pathlib import Path
import struct
from zipfile import ZipFile

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "assets/textures/frost-normal.dds",
    )
    args = parser.parse_args()
    with ZipFile(args.archive) as archive:
        normal = Image.open(BytesIO(archive.read("Plaster001_1K-PNG_NormalDX.png")))
        # Average signed XY in linear space, retaining cancellation in coarse mips.
        xy = normal.convert("RGB").resize((512, 512), Image.Resampling.BOX)
    levels = []
    while True:
        r, g, _ = xy.split()
        levels.append(Image.merge("LA", (r, g)).tobytes())
        if xy.width == 1:
            break
        xy = xy.resize((xy.width // 2, xy.height // 2), Image.Resampling.BOX)
    # DDS_HEADER + DX10 extension. DXGI_FORMAT_R8G8_UNORM=49; TEXTURE2D=3.
    header = [124, 0x2100F, 512, 512, 1024, 0, len(levels)] + [0] * 11
    header += [32, 4, int.from_bytes(b"DX10", "little"), 0, 0, 0, 0, 0]
    header += [0x401008, 0, 0, 0, 0]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(
        b"DDS " + struct.pack("<31I5I", *header, 49, 3, 0, 1, 0) + b"".join(levels)
    )
    print(f"Wrote {args.output}: 512x512 RG8, {len(levels)} mips")


if __name__ == "__main__":
    main()
