# Rendering pipeline

This document describes the shipped implementation. It does not describe proposed
optimizations or promise exact agreement with the paper. See
[limitations](KNOWN_LIMITATIONS.md) and [中文版本](PIPELINE.zh-CN.md).

## Pass order and resources

| Pass | Reads | Writes / work |
|---|---|---|
| Opaque | Scene/materials, shadow map | HDR SceneColor, forward D32 SceneDepth |
| Interface prepass | Special-surface geometry/materials, opaque depth | Nearest D32 interface, RGBA16F RGB transmission + sigma, RG16F offset |
| Bounds occupancy | View-space actor bounds and effective material footprint | Virtual Z occupancy, scalar/RGB XY masks, screen tile bits, per-mip frost work masks |
| Adaptive Z | Virtual occupancy | Compacted mapping from 2048 virtual log-depth intervals to 16/32/64/128 physical slices |
| Physical occupancy | Bounds masks and mapping | Physical scalar/RGB occupied-slice masks |
| Extinction clear / splat | Occupancy, transparent geometry, opaque depth | Packed integer extinction and overflow markers, atomically accumulated |
| LUT integration | Extinction, occupancy, overflow | RGBA8 3D transmittance LUT and R32_UINT first-zero slice |
| Zero-T depth | First-zero slices and mapping | Conservative quads into the existing SceneDepth; no PS or preparation pass |
| OIT accumulation | All OIT geometry, SceneDepth, LUT, selected-interface depth | Full-resolution N, A, totalTau, BackN; four R11G11B10 MRTs by default |
| B-prime resolve | Accumulations, LUT/interface, opaque | RGBA16F background cache, only on transparency tiles plus a one-pixel border |
| Gaussian | B-prime/opaque source, validity and frost work masks | RGBA16F fixed-screen mip chain |
| Main composition | Accumulations, interface, sharp B or Gaussian | Temporary HDR on transparency tiles |
| Tile copy | Temporary HDR | Copy active tiles back to SceneColor |
| VFX offset | VFX geometry/material, SceneDepth, interface and LUT | Additive full-resolution RG16F offset |
| VFX apply | SceneColor and offset | Separate HDR image; skipped/released when VFX is inactive |
| Display | Final HDR | Tone mapping, display transfer, optional occupancy overlay; ImGui afterward |

The default full-resolution HDR format is R11G11B10. `--accum-half` and
`--composition-half` are RGBA16F precision references. B and Gaussian retain alpha
for source validity; interface alpha stores sigma. There are no I/R/q cache RTs.

## Composition algebra

For each surviving fragment, RGB alpha is `1 - transmission` after material
coverage. The LUT estimates transmission in front of its depth. Accumulation
builds weighted numerator `N`, denominator `A`, exact-fragment optical depth
`totalTau`, and the subset `BackN` geometrically behind the selected interface.
Glass surface lighting is a separate fixture shading contribution.

Linear splatting splits optical depth between adjacent Z slices. XY comes from
the low-resolution rasterizer; LUT reads filter in XYZ. This follows the
[presentation's linear-splat description, slides 64–65](https://advances.realtimerendering.com/s2025/content/AVBOIT_SIG2025_MDROBOT-final.pdf#page=64),
which does not prescribe an additional four-neighbor XY scatter.

```text
totalT = exp(-totalTau)
if zeroDepthEnabled && all(T_LUT(SceneDepth) == 0): totalT = 0
k      = (1 - totalT) / max(A, epsilon)
I      = N * k + Opaque * totalT
q      = max(totalT, saturate(T_LUT(interfaceDepth) * interfaceTransmission))
B      = (BackN * k + Opaque * totalT) / max(q, epsilon)
Front  = (N - BackN) * k
Result = Front + q * Filter(B)
```

All operations are componentwise for RGB. Keeping `BackN` avoids recovering a
small background numerator by subtracting two nearly equal accumulated values.
The lower bound `q >= totalT` prevents filtered low-resolution LUT estimates from
amplifying the opaque contribution beyond its intended range. `q` is recomputed
in registers by the passes that need it.

With Zero-T depth enabled, resolve samples the existing filtered LUT at the
**current SceneDepth**, using the same adaptive mapping and two-virtual-slice
bias as accumulation. An RGB-zero result closes the ray (`totalT = 0`), including
the normalization above. It does not modify the stored `totalTau` or blindly zero
`q` for an interface in front of the closure. B preparation and final composition
share this code; closed rays skip reading opaque color, allowing a deferred host
to leave hidden lighting unevaluated. No stencil, cutoff RT or new pass is needed.

This is a per-pixel LUT rule, not a record of which quad won the depth test: it
can also close rays outside the more conservative quad footprint. Reading the LUT
at its far endpoint would incorrectly ignore a nearer opaque surface. The rule
retains the LUT's spatial/quantization approximation and is disabled together
with `--no-zero-depth` for the unculled full-resolution-tau reference.

When there is no special interface, B is the ordinary resolved image `I`.
When the interface is behind SceneDepth, composition uses ordinary OIT. Such a
source is invalid for frost; sharp refraction still accepts the documented
foreground-sampling approximation. An optional low-q rejection is a diagnostic
quality tradeoff, disabled by default.

## B-prime and tile work

B-prime stores complete B values on **64x64 transparency tiles plus a one-pixel
bilinear guard**, not merely on glass pixels. Source positions outside these tiles
have no transparent contribution and read opaque SceneColor instead. Center-based
source selection and the guard preserve bilinear filtering across tile boundaries.
Sampling opaque remains safe because composition writes a temporary texture first;
only after all source reads finish are active tiles copied back to SceneColor.

Bounds occupancy also marks frost requests. Per-level work masks include the
material displacement, reconstruction footprint and reverse-propagated Gaussian
support. Overlapping requests are merged atomically. Gaussian currently dispatches
a **fixed grid with uniform per-group early rejection**; it does not build an
indirect compact work list. Resolve/copy use tile geometry with VS rejection.
Debug rectangles are envelopes, not the actual set of processed tiles.

The B-prime domain does not grow with blur or refraction radius. Gaussian masks
still must cover the source dependencies of every requested output. There is no
additional hard radius cap introduced to make B scheduling work.

## Frost filtering

Roughness and IOR produce a screen-space sigma in 1440p reference units. The user
can cap the material sigma. Background depth does not change the width.
For a 16:9 image the three-level chain is 320x180, 160x90, 80x45 regardless of
render resolution. Four/five levels add 640x360 and 1280x720, respectively.
Changing aspect ratio changes chain width; equal aspect ratios preserve scale.

The first pass directly filters B/opaque into the first level (16 sparse taps or
64 dense reference taps). Further levels fuse separable horizontal and vertical
Gaussian reductions using groupshared storage; no horizontal RT is allocated.
All filtering stays in linear HDR, with premultiplied RGB/validity. Invalid taps
can use the opposite offset; both invalid means no contribution.

Positive frost starts at the first blurred level, with no sharp-to-base lerp.
Variance selects fractional LOD. Bilinear uses one hardware mip-filtered sample;
cubic B-spline reconstruction uses four bilinear samples per level and blends two
levels. Validity is normalized only when consuming the filtered result. If the
footprint is wholly invalid, the pixel retains ordinary OIT.

## Host integration

`TransparencyPipeline` owns pass resources, but not the window, scene, command
submission or presentation. `Create` reconnects resources after configuration
changes; the host must retire old GPU work first. `Record` only records into the
caller's open command list. Shader handles and material/geometry tables come from
the host. Actors and material sections stay separate draws; see
[material submission](MATERIAL_DRAWS_ZH.md).

Internal dimensions are rounded up to multiples of four. The sample uses forward
D32 and explicit shader-readable depth; reverse-Z requires adapting comparisons
and projection. Zero-T modifies the host's SceneDepth in place, which downstream
host passes must account for. Pass markers use the common RAII scope macro.
