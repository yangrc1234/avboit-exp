# SPDX-License-Identifier: MIT
"""Convert the fixed-domain Blender probe's VDB cache to a runtime sequence.

Requires Node and the offline openvdb@0.5.1 reader used by prepare_smoke_volume.py.
This converter assumes a cubic domain at index origin zero, without adaptive bounds.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import numpy as np
from pack_smoke_bricks import pack

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("cache", type=Path)
p.add_argument("--reader", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--size", type=int, default=48)
p.add_argument("--fps", type=float, default=24)
p.add_argument("--precision", choices=("4bit", "half"), default="4bit")
p.add_argument(
    "--pattern",
    default="fluid_data_*.vdb",
    help="Use fluid_noise_*.vdb for the high-resolution bake",
)
args = p.parse_args()
files = sorted(args.cache.glob(args.pattern))
if not files or not 0 < args.fps <= 240 or args.size <= 0 or args.size % 4:
    p.error("Expected VDB frames and a positive frame rate")
if args.output.exists() and any(args.output.iterdir()):
    p.error("Use an empty output directory")
has_density = False
with tempfile.TemporaryDirectory() as temp:
    for frame, source in enumerate(files, 1):
        raw = Path(temp) / "density.f32"
        script = f"""
import {{OpenVDBReader,Vector3}} from {json.dumps(args.reader.resolve().as_uri())};
import fs from 'node:fs';
const r=new OpenVDBReader();
await r.read(new Uint8Array(fs.readFileSync({json.dumps(str(source.resolve()))})));
const g=r.grids.density_noise || r.grids.density, p=new Vector3(), size={args.size};
if(!g) throw new Error('Missing density');
const res=g.metadata.file_base_resolution?.value;
if(res && (res.x!==size || res.y!==size || res.z!==size)) throw new Error('Grid resolution does not match --size');
const a=new Float32Array(size**3);let k=0;
for(let z=0;z<size;z++)for(let y=0;y<size;y++)for(let x=0;x<size;x++)
 a[k++]=Math.max(0,g.getValue(p.set(x,y,z)));
fs.writeFileSync({json.dumps(str(raw))},Buffer.from(a.buffer));
"""
        subprocess.run(["node", "--input-type=module", "-"], input=script, text=True, check=True)
        pack(raw, args.output / f"{frame}.bin", args.size, True, args.precision)
        has_density |= bool(np.fromfile(raw, dtype="<f4").max() > 0)
if not has_density:
    raise ValueError("All source frames are empty; refusing to publish a sequence manifest")
(args.output / "sequence.txt").write_text(f"{len(files)} {args.fps}\n")
