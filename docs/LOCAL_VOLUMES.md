# Local volumes: first AVBOIT prototype

Select **Test case 19–25** in ImGui, or run `bin/avboit_viewer.exe --stress 22`.
The material editor exposes RGB extinction per world unit and source radiance.

| Case | Purpose |
| --- | --- |
| 19 | One homogeneous box |
| 20 | Same medium, two overlapping half-density actors |
| 21 | Same medium, two adjacent depth halves |
| 22 | RGB frosted glass intersects the medium |
| 23 | Different colored media overlap |
| 24 | Imported OpenVDB smoke density |
| 25 | Imported smoke intersecting frosted glass |

Each actor owns a separate proxy draw and can select independent extinction and
accumulation vertex/pixel programs. Ordinary surfaces retain their original
programs. Camera-space volume integration adds no render target or global pass.
Light-space moment shadows have separate resources, described below.

## Data flow

1. Existing bounds occupancy includes each volume's world-space AABB, then builds
   adaptive Z and physical occupancy as usual.
2. The volume extinction shader rasterizes its projected box rectangle at LUT XY
   resolution. It intersects each ray with the AABB and opaque depth, walks the
   occupied adaptive virtual intervals, and injects `sigma * physicalLength`
   directly into their physical RGB bins. There is no surface splat push.
3. Existing integration combines surface and volume extinction in one LUT.
4. A full- or half-resolution proxy draw repeats the intersections and marches
   with an independent world-space step. It accumulates segment contributions in
   registers and writes N/A/totalTau/BackN once per covered pixel.
5. Existing B generation, Gaussian and final composition consume those outputs.

Camera-space Z intervals are divided by the ray's forward component to obtain
physical distance. Near-plane intersections use a conservative full-screen proxy;
the pixel shader still rejects rays missing the box. Extinction is rounded using
differences of cumulative optical-depth endpoints, avoiding systematic loss from
rounding many tiny segments separately. Different actors still quantize separately.

## Accumulation convention

This prototype uses differential AVBOIT weights `w = deltaTau * T(samplePosition)`:
`A += w`, `N += source * w`, `totalTau += deltaTau`. The existing resolve multiplies
N by `(1-exp(-totalTau))/A`. This makes a constant source function reproduce the
homogeneous analytic result, while keeping contributions additive under actor
partitioning. It is an AVBOIT approximation, not independent Beer integration of
each actor followed by another normalization.

The selected interface splits each segment into front/back pieces, evaluated at
jittered positions within each piece using the same pixel phase. Back pieces also contribute to BackN. No running
front-to-back color integral is retained by the ray marcher.

## World-space accumulation steps

Accumulation uses a .5 world-unit base step, independent of adaptive Z. At each
step start, max(R,G,B) of the global T-LUT controls a smooth increase below T=.1,
up to 4x at T=0. A 256-step budget coarsens long intervals to cover the complete
ray, rather than dropping the remaining optical depth. Interface depth still
splits steps into front/back pieces. Density and lighting use four stratified depth phases per 2x2 pixel quad,
with a shared 32x32 blue-noise offset inside each stratum. Half-resolution draws
form quads in low-resolution coordinates; integration bounds and segment lengths are unchanged.
Spatial dithering is stable by default. The existing TAA-oriented stochastic
option also animates the march phase; this demo has no temporal accumulation.

ImGui exposes all four parameters without rebuilding resources. CLI equivalents:
`--volume-step`, `--volume-low-t`, `--volume-step-scale`, `--volume-max-steps`.
Set the multiplier to 1 for fixed steps throughout. Extinction injection remains
bin-aligned; light-space MSM retains its independent 96/32-step integration.

## Current limits

- Cases 19-23 use homogeneous boxes. Cases 24-25 sample a 64x128x64 imported
  density texture. Sponza uses dynamic scene lighting, ordinary depth shadows and
  separate volumetric moment shadows. The standalone analytic fixtures keep a
  constant source (or the imported texture's fixed +Y lighting reference).
  There is no compressed brick traversal, fluid simulation or global height fog
  yet. The texture is loaded only for the
  imported-smoke fixtures and released when switching away.
- Pure emission at zero extinction is not supported by this normalized 4-MRT
  path. Zero extinction produces no contribution. Artistic additive god rays need
  a deliberate emission treatment rather than dividing by an almost-zero sigma.
- Overlapping heterogeneous sources and surface/media mixing remain approximate.
  LUT XY filtering, finite bin size, existing surface visibility bias, packed
  quantization and common normalization can alter their relative contributions.
- The LUT lookup interpolates cumulative transmittance linearly. Large steps
  can miss thin density/lighting features; low-T coarsening also approximates tau.
- Lighting work scales with projected coverage times march steps times overlapping
  actors. There is no hierarchy, temporal reconstruction or early ray termination.
  This baseline is not a low-end performance claim.

## Dynamic lighting and volumetric shadows

Run `bin/avboit_viewer.exe --sponza --stress 24` (or 25 for frosted glass).
ImGui's **Dynamic lighting / volume shadows** section edits sun direction and
irradiance, spotlight position/target/intensity, movement and MSM enablement live.
The main animation control drives light time too. `--time`, `--sun-intensity`,
`--spot-intensity` and `--no-volume-shadows` support repeatable captures.

Each light has two independent shadow representations:

- Ordinary Reversed-Z depth for opaque/masked Sponza geometry. The sun and spot
  share a two-layer 2048px depth array. The spot is redrawn dynamically; the sun
  is cached until its direction changes or forced redraw is requested.
- Optical-depth moments for local media: two camera-centered, texel-snapped sun
  projections (12m and 40m XY width, 80m light-depth coverage) and one spotlight
  projection, each 256px. The sun blends at the fine cascade boundary. Spot
  moments use linear light depth, independent of perspective device depth.

A compute pass raymarches intersecting volume bounds, adding `deltaTau` and
`deltaTau * (z,z^2,z^3,z^4)` in FP32. At the final store, moments are normalized
by total tau and transformed into RGBA16_UNORM; total tau uses R16_FLOAT.
Lookup uses one hardware bilinear sample for each texture, decodes the moments,
reconstructs the optical-depth CDF and returns
`exp(-tau * CDF)`. Ordinary shadow visibility and this transmission multiply
the incident light. Smoke uses isotropic single scattering plus a constant
ambient term; its source RGB acts as an artistic scattering tint/gain.
Sponza opaque/masked direct lighting also receives the volume shadows.

World-space bounds select active light projections, including off-camera
casters. Projections with no overlapping media or zero light intensity skip
integration and sampling. Resources are pooled as three slices; scenes with no
local volumes use tiny dummy bindings instead of allocating the 256px arrays.
Inactive slices need no clear because consumers test their active flag first.

This is the four-power-moment approach described by
[MBOIT](https://momentsingraphics.de/I3D2018.html), also used for volumetric shadows
in [Smolder](https://www.realtimerendering.com/advances/s2026/index.html).
This implementation uses the MBOIT authors' four-moment quantization transform:
RGBA16_UNORM + R16_FLOAT (80 bits/texel, 1.875 MiB for three 256px slices), a small
moment bias, and midpoint interpolation of the CDF bounds. This matches Smolder's
reported storage size, but is not a claim to reproduce its exact encoding or full
renderer. No manual four-tap reconstruction or separate moment blur pass is needed.
Normalized moments and tau are filtered independently: near sharp changes in
total optical depth, this approximates rather than exactly reproduces filtering
unnormalized moments. The three 2K smoke/moving-spot/frost captures differed from
the former FP32 baseline by at most 1/255 after tone mapping. This is fixture
coverage, not a guarantee for every density field.

Remaining limits: moment reconstruction and finite shadow resolution can leak or
soften shadows; the MSM uses luminance-weighted scalar extinction, so colored
volume shadow transmission is not preserved. Spot lights are implemented; point
light cubemaps are not. Sample-only opaque fixtures do not yet cast into the
Sponza shadow maps, and transparent glass does not shadow lights. Volumetric
shadows currently rebuild each frame rather than caching static volumes.

## Validation

Run `python avboit/tests/check_local_volumes.py` with NumPy and Pillow installed.
It captures 2560x1440 images with half-precision accumulation to isolate algorithm
behavior: analytic homogeneous color/tau, overlapping and adjacent actor partitions,
16/32/128 depth budgets, front/back contributions at glass, poisoned storage,
submission order and an oblique view. Captures and numerical results go under
`build/local-volume-check`. Separate native CTests exercise default packed RTs.

`python avboit/tests/check_volume_lighting.py` checks 2K Sponza captures with MSM
on/off, moving spot illumination, frost, and no volumes. It verifies finite,
bounded moment data, source attenuation and the inactive projection flags.


### Demo TAA

Enable `--taa` or **Resolve pipeline → TAA** in ImGui. This uses the same Donut
TAA pass as the neighboring ADS demo, after transparency/VFX distortion and before
tonemapping. Halton projection jitter is shared by opaque and transparent geometry,
occupancy projection and volume rays. TAA also enables temporal quad phase rotation
and stochastic half-resolution reconstruction. Disabling it releases its history,
motion and output textures. It defaults off to preserve non-temporal comparisons.

Motion is reconstructed from scene depth and camera transforms. There are no
transparent-object motion vectors or volume representative depths yet, so moving
transparency, volumes and refraction can ghost. Camera cuts, resource recreation
and actor material edits reset history. The GPU profiler reports a separate `taa`
stage. Headless `--taa` also checks toggling, motion, cuts, resizing and finite HDR
output before capturing a converged frame.
