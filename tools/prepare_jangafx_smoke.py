# SPDX-License-Identifier: MIT
"""Import JangaFX Smoke_Plume_01 into a fixed, Y-up cubic runtime domain.

Requires the extracted CC0 archive and local openvdb@0.5.1 JS reader.
The original 288x512x320 domain is padded to 512 cubed, never stretched.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import tempfile
import numpy as np
from pack_smoke_bricks import pack


def convert(item):
    frame, source, args = item
    with tempfile.TemporaryDirectory() as temp:
        raw = Path(temp) / "density.raw"
        script = f"""
import {{OpenVDBReader,Vector3}} from {json.dumps(args.reader.resolve().as_uri())};
import fs from 'node:fs';
const r=new OpenVDBReader();
await r.read(new Uint8Array(fs.readFileSync({json.dumps(str(source.resolve()))})));
const g=r.grids.density;
if(!g) throw Error('Missing density grid');
const lo=g.metadata.file_bbox_min.value, hi=g.metadata.file_bbox_max.value;
const tm=g.transform.transformMap;
if(lo.x<0||lo.y<0||lo.z<0||hi.x>=288||hi.y>=512||hi.z>=320)
 throw Error('Unexpected source bounds');
if(tm.scale.x!==1||tm.scale.y!==1||tm.scale.z!==1||tm.translation.x!==-143.5||tm.translation.y!==-255.5||tm.translation.z!==-159.5)
 throw Error('Unexpected grid transform');
const time=Buffer.from(g.metadata.simulation_time.value,'latin1').readFloatLE();
if(Math.abs(time-(4.75+{frame}/60))>1e-4) throw Error('Unexpected frame time');
const n={args.size}, step=512/n, a=new Float32Array(n**3), p=new Vector3();
let k=0;
for(let z=0;z<n;z++)for(let y=0;y<n;y++)for(let x=0;x<n;x++,k++){{
 const bx=-112+x*step-.5, by=y*step-.5, bz=-96+z*step-.5;
 if(bx>hi.x||by>hi.y||bz>hi.z||bx+step<lo.x||by+step<lo.y||bz+step<lo.z) continue;
 let sum=0;
 for(const oz of [.25,.75])for(const oy of [.25,.75])for(const ox of [.25,.75])
   sum+=Math.max(0,g.getValue(p.set(Math.round(bx+ox*step),Math.round(by+oy*step),Math.round(bz+oz*step))));
 a[k]=sum/8;
}}
fs.writeFileSync({json.dumps(str(raw))},Buffer.from(a.buffer));
"""
        subprocess.run(["node", "--input-type=module", "-"], input=script, text=True, check=True)
        a = np.fromfile(raw, "<f4")
        if not np.isfinite(a).all() or a.max() <= 0:
            raise ValueError(f"Invalid density in frame {frame}")
        pack(raw, args.output / f"{frame+1}.bin", args.size, False, args.precision)
    return frame


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("cache", type=Path)
    p.add_argument("--reader", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--size", type=int, default=128)
    p.add_argument("--precision", choices=["half", "4bit"], default="half")
    args = p.parse_args()
    if args.size < 16 or args.size % 4:
        p.error("Size must be a positive multiple of four, at least 16")
    files = sorted(
        args.cache.glob("embergen_smoke_plume_a_*.vdb"),
        key=lambda x: int(x.stem.rsplit("_", 1)[1]),
    )
    if [int(x.stem.rsplit("_", 1)[1]) for x in files] != list(range(166)):
        p.error("Expected the complete 166-frame Smoke_Plume_01 sequence")
    if args.output.exists() and any(args.output.iterdir()):
        p.error("Use an empty output directory")
    args.output.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=2) as workers:
        for frame in workers.map(convert, ((i, f, args) for i, f in enumerate(files))):
            print(f"Converted {frame+1}/166", flush=True)
    (args.output / "sequence.txt").write_text("166 60\n")
    (args.output / "SOURCE.txt").write_text(
        "JangaFX Smoke_Plume_01, CC0. https://jangafx.com/software/embergen/download/free-vdb-animations\nFixed index domain [-112,0,-96] to [400,512,416], Y-up; 8 samples per output voxel.\n"
    )
    license_path = args.cache.parent / "LICENSE.txt"
    if license_path.exists():
        (args.output / "LICENSE.txt").write_bytes(license_path.read_bytes())


if __name__ == "__main__":
    main()
