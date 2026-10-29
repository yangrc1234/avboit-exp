# SPDX-License-Identifier: MIT
"""Convert the official OpenVDB smoke sample to a small density/light-integral DDS.

Requires NumPy, Pillow, Node.js and a local openvdb@0.5.1 JavaScript reader.
The reader is an offline conversion dependency, not shipped with the viewer.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vdb", type=Path)
    parser.add_argument("--reader", type=Path, required=True, help="openvdb@0.5.1 index.js")
    parser.add_argument("--node", default="node")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "assets/textures/smoke-volume.dds",
    )
    args = parser.parse_args()
    digest = hashlib.sha256(args.vdb.read_bytes()).hexdigest()
    if digest != "735c22eecf8b498aa0c0c7a4098ea4f2c4ad1f63bf601c278094020301093ac9":
        raise ValueError("Expected the pinned official smoke.vdb sample")
    with tempfile.TemporaryDirectory() as temp:
        raw = Path(temp) / "density.f32"
        script = f"""
import {{OpenVDBReader, Vector3}} from {json.dumps(args.reader.resolve().as_uri())};
import fs from 'node:fs';
const reader = new OpenVDBReader();
await reader.read(new Uint8Array(fs.readFileSync({json.dumps(str(args.vdb.resolve()))})));
const grid = reader.grids.density;
// The pinned sample's useful ROI is [0,112] x [0,224] x [0,113].
// Its file_bbox includes distant inactive tiles; do not normalize that huge box.
const data = new Float32Array(113 * 225 * 114), p = new Vector3();
let index = 0;
for (let z = 0; z < 114; ++z)
    for (let y = 0; y < 225; ++y)
        for (let x = 0; x < 113; ++x)
            data[index++] = Math.max(0, grid.getValue(p.set(x, y, z)));
fs.writeFileSync({json.dumps(str(raw))}, Buffer.from(data.buffer));
"""
        subprocess.run([args.node, "--input-type=module", "-"], input=script, text=True, check=True)
        source = np.fromfile(raw, dtype="<f4").reshape(114, 225, 113)
    if not np.isfinite(source).all() or source.max() <= 0:
        raise ValueError("Invalid density data")
    xy = np.stack(
        [np.asarray(Image.fromarray(v).resize((64, 128), Image.Resampling.BOX)) for v in source]
    )
    density = np.stack(
        [
            np.asarray(Image.fromarray(xy[:, y, :]).resize((64, 64), Image.Resampling.BOX))
            for y in range(128)
        ],
        axis=1,
    )
    # Integral from each voxel center toward +Y, expressed in normalized Y units.
    # Runtime multiplies by world-space height and the current extinction coefficient.
    light = (np.cumsum(density[:, ::-1, :], axis=1)[:, ::-1, :] - 0.5 * density) / 128
    rg = np.stack([density, light], axis=-1).astype("<f2")
    header = [124, 0x80100F, 128, 64, 64 * 4, 64, 1] + [0] * 11
    header += [32, 4, int.from_bytes(b"DX10", "little"), 0, 0, 0, 0, 0]
    header += [0x1008, 0x200000, 0, 0, 0]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # DXGI R16G16_FLOAT=34, D3D10_RESOURCE_DIMENSION_TEXTURE3D=4.
    args.output.write_bytes(b"DDS " + struct.pack("<31I5I", *header, 34, 4, 0, 1, 0) + rg.tobytes())
    print(f"Wrote {args.output}: 64x128x64 RG16F, {args.output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
