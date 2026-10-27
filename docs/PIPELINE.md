# Rendering pipeline

This document describes the shipped implementation. It does not describe proposed
optimizations or promise exact agreement with the paper. See
[limitations](KNOWN_LIMITATIONS.md) and [中文版本](PIPELINE.zh-CN.md).

## Pass order and resources

| Pass | Reads | Writes / work |
|---|---|---|
| Opaque | Scene/materials, shadow map | HDR SceneColor, Reversed-Z D32 SceneDepth |
| Interface prepass | Special-surface geometry/materials, opaque depth | Nearest D32 interface, RGBA16F RGB transmission + sigma, RG16F offset |
| Bounds occupancy | View-space actor bounds and material flags | Virtual Z occupancy, scalar/RGB XY masks, screen tile bits, base-level frost work mask |
| Adaptive Z | Virtual occupancy | Compacted mapping from 2048 virtual log-depth intervals to 16/32/64/128 physical slices |
| Physical occupancy | Bounds masks and mapping | Physical scalar/RGB occupied-slice masks |
| Extinction clear / splat | Occupancy, transparent geometry, opaque depth | Packed integer extinction and overflow markers, atomically accumulated |
| LUT integration | Extinction, occupancy, overflow | RGBA8 3D transmittance LUT, R32_UINT first-zero slice, candidate tile IDs and indirect draw count |
| Zero-T depth | Candidates, first-zero slices and mapping | One indirect draw of conservative quads into SceneDepth; no PS or preparation pass |
| OIT accumulation | All OIT geometry, SceneDepth, LUT, selected-interface depth | Full-resolution N, A, totalTau, BackN; four R11G11B10 MRTs by default |
| B-prime resolve | Accumulations, LUT/interface and offset, opaque | RGBA16F refracted background cache, only on transparency tiles, without a pixel border |
| Gaussian | B-prime/opaque source, validity and the base work mask | RGBA16F fixed-screen mip chain |
| Main composition | Accumulations, interface, sharp B or Gaussian | Temporary HDR on transparency tiles |
| Tile copy | Temporary HDR | Copy active tiles back to SceneColor |
| VFX offset | VFX geometry/material, SceneDepth, interface and LUT | Additive full-resolution RG16F offset |
| VFX apply | SceneColor and offset | Separate HDR image; skipped/released when VFX is inactive |
| Display | Final HDR | Tone mapping, display transfer, optional occupancy overlay; ImGui afterward |

The default full-resolution HDR format is R11G11B10. `--accum-half` and
`--composition-half` are RGBA16F precision references. B and Gaussian retain alpha
for source validity; interface alpha stores sigma. There are no I/R/q cache RTs.

All scene, interface and shadow D32 targets use Reversed-Z: near is 1, far/clear
is 0. Depth tests use Greater/GreaterEqual; interface selection uses strict
Greater so equal-depth surfaces keep the first writer. The camera keeps finite 0.05/80 planes;
device depth is `A + B / viewZ`, with `A=-near/(far-near)` and
`B=near*far/(far-near)`. AVBOIT slices still integrate increasing positive view Z.

Integration appends candidate 16x16 screen tiles whose representative ray reaches
RGB zero. Each tile has one owner ray, so no deduplication pass is needed. The
depth VS still checks the entire filtered neighborhood after integration completes;
candidate selection does not change the conservative depth rule. The draw count
is reset each frame alongside extinction clear; only the appended list prefix is
read. At 2560x1440 the list capacity is 57,600 bytes, plus 16 bytes of draw arguments.
This differs from the presentation's separate quad-generation CS (slide 78):
here integration only emits candidates, and the VS computes the final depth.
The presentation specifies conservative reads but not their exact XY footprint.

## Composition algebra

First-level Gaussian source taps always require interface coverage. One interface-depth
gather rejects the whole bilinear tap if any of its four texels has no interface
(D32 clear = 0). Existing mirror repair handles the zero-RGBA tap. Refraction is
partly applied in B-prime: frost destinations also reject displaced sources whose
nearest texel has no visible interface. Sharp refraction retains foreground sampling;
later mips propagate premultiplied validity. No new RT or pass is added.
This also rejects real background outside the silhouette,
can stretch interior colors near edges, and does not distinguish adjacent
interface identities. If a narrow interface or large offset leaves no valid
samples, composition retains its existing unfiltered fallback. Stress preset 16
places an HDR foreground just outside glass.

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
D      = InterfaceOffset(p)
detail = sigma > 0 ? clampLength(D, detailRadius) : 0
Bd(p)  = SampleResolvedBackground(p + D - detail)
Result(p) = Front(p) + q(p) * Filter(Bd)(p + detail)
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

B-prime is an RGBA16F render target without UAV access. Before generation each
frame, ClearRenderTargetView resets it to zero RGBA using a matching optimized
clear value, so unwritten texels have zero validity. Diagnostic poison mode fills
garbage before the same mandatory zero clear. B and occupancy have no 1px border.

B-prime stores refracted B values at destinations on **64x64 transparency tiles
without pixel borders**, not merely on glass pixels. The producer reads
full-screen accumulation, opaque and LUT/interface data at displaced coordinates;
it never reads another position in the partially written B cache. Continuous RT
inputs use hardware bilinear sampling, followed by one resolve. Zero offset uses
exact texel loads; source tiles without transparency read opaque directly.
Source q belongs to the source texel, while final Front and q stay at the destination.
Center-based cache selection uses opaque outside the cache. Inside, bilinear
taps crossing its edge include zero premultiplied color/validity.
Sampling opaque remains safe because composition writes a temporary texture first;
only after all source reads finish are active tiles copied back to SceneColor.

Bounds occupancy marks only the first Gaussian level's 4x4 output groups directly
from actor bounds. Requests are merged atomically, with no kernel, reconstruction
or detail-offset padding. The base UAV mip is cleared to zero each frame; skipped
groups retain zero validity. Later active mips run full-screen with no work masks.
The base dispatch uses a fixed grid with uniform group rejection, not an indirect
list. Resolve/copy use tile geometry with VS rejection. Debug rectangles are
envelopes, not the set of filtered base pixels.

This deliberately reduces filter support near interface edges: masked and
full-screen base filtering need not be pixel-identical. Invalid samples retain
premultiplied validity and the final normalization/fallback still applies.
The B-prime domain does not grow with blur or refraction radius.
Frost reserves a bounded part of the same RG16F offset for the final Gaussian lookup:
`detail = D * min(1, r / max(length(D), epsilon))`. B resolves at `p + D - detail`,
while composition samples Gaussian at `p + detail`. Front, RGB q and sigma stay at p.
Sharp glass keeps its entire displacement in B. The radius defaults to 24 pixels
at 1440p, scales with render height, and is live-editable through ImGui's
`Post-blur offset (1440p px)` or `--frost-detail-radius 0..64`. Zero naturally puts
all displacement before blur; there is no separate shader variant.
The detail radius does not expand Gaussian work. Composition adds one existing
offset read, no RT/pass
or extra Gaussian taps. The full material displacement is not capped.
This is an amplitude split, not a frequency decomposition: constant offsets add
correctly, but spatially varying warps need not commute. VFX still warps the
completed HDR composition. Stress preset 18 places smooth frost beside textured
frost with equal emitters. Its interface material samples a bundled
[CC0 normal map](../assets/textures/README.md): 512x512 linear RG8 with full mips,
Wrap addressing and SampleGrad filtering. Decoded XY drives an artistic screen
offset, not a full tangent-frame refraction solve. `Frost normal tiling` /
`--frost-normal-tiling .25..64` sets whole-texture repeats across a pane (default 1)
without rebuilding resources. Sigma and
surface brightness are not textured, isolating the refraction effect.

Displaced N/A/totalTau/BackN, opaque and interface RGB T are hardware-filtered before
one resolve. Depth, interface membership and validity use the nearest source texel.
Destination Front/q stay unchanged. Filtering optical depth before
exponentiation and filtering terms before division are not equivalent to filtering
resolved color; mixed interfaces and invalid-source boundaries are another source
of error. Deferred hosts must also provide defined opaque color throughout the
bilinear footprint, even if Zero-T allowed neighboring lighting to be skipped.

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

Internal dimensions are rounded up to multiples of four. The sample uses Reversed-Z
D32 and explicit shader-readable depth. Zero-T modifies the host's SceneDepth in place, which downstream
host passes must account for. Pass markers use the common RAII scope macro.
