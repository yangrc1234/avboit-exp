# SPDX-License-Identifier: MIT
"""Area-average an existing FP16 smoke sequence, preserving bounds and timing."""
import argparse
from pathlib import Path
import shutil
import tempfile
import numpy as np
from pack_smoke_bricks import pack


def decode(path):
    data = path.read_bytes()
    h = np.frombuffer(data, '<u4', 8)
    n = int(h[1])
    g = (n + 3) // 4
    if h[0] != 3 or not np.all(h[1:4] == n):
        raise ValueError('Expected cubic FP16 brick sequence')
    indices = np.frombuffer(data, '<u2', g**3, int(h[4])*4).reshape(g, g, g)
    records = np.frombuffer(data, '<f2', offset=int(h[5])*4).reshape(-1, 128)
    if indices.max() > len(records):
        raise ValueError('Invalid brick index')
    dense = np.zeros((g*4,)*3, dtype=np.float32)
    for z, y, x in np.argwhere(indices != 0):
        block = records[int(indices[z,y,x])-1, :125].reshape(5,5,5)
        dense[z*4:z*4+4, y*4:y*4+4, x*4:x*4+4] = block[:4,:4,:4]
    return dense[:n,:n,:n]


def area_weights(source_size, target_size):
    # Exact overlap of source and destination cells, not sparse point sampling.
    width = source_size / target_size
    lo = np.arange(target_size)[:,None] * width
    source = np.arange(source_size)[None,:]
    return (np.maximum(0, np.minimum(lo+width, source+1)-np.maximum(lo,source)) / width).astype('f4')


def reduce(density, n):
    w = area_weights(density.shape[0], n)
    a = np.einsum('ax,zyx->zya', w, density, optimize=True)
    a = np.einsum('by,zya->zba', w, a, optimize=True)
    a = np.einsum('cz,zba->cba', w, a, optimize=True)
    np.testing.assert_allclose(a.mean(), density.mean(), rtol=2e-5, atol=1e-8)
    return a.astype('<f4')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--size', type=int, required=True)
    args = p.parse_args()
    if args.size < 1 or (args.output.exists() and any(args.output.iterdir())):
        p.error('Use a positive size and empty output directory')
    count, fps = (args.source/'sequence.txt').read_text().split()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp)/'density.raw'
        for frame in range(1, int(count)+1):
            density = decode(args.source/f'{frame}.bin')
            if args.size > density.shape[0]:
                p.error('Target size must not exceed source size')
            reduce(density, args.size).tofile(raw)
            pack(raw, args.output/f'{frame}.bin', args.size, precision='half')
            print(f'Frame {frame}/{count}', flush=True)
    for name in ('sequence.txt','SOURCE.txt','LICENSE.txt'):
        if (args.source/name).exists():
            shutil.copyfile(args.source/name, args.output/name)
    (args.output/'RESAMPLING.txt').write_text(
        f'Area average from {args.source.resolve()} to {args.size}^3. '
        'Unchanged world bounds, density scale and frame timing. FP16.\n')


if __name__ == '__main__':
    main()
