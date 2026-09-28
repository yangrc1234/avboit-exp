# Known limitations

[中文说明](KNOWN_LIMITATIONS.zh-CN.md)

The sample prioritizes inspectable code and practical screen-space approximations.
Passing the tests below does not establish physical correctness or paper parity.

- **One special interface per pixel.** A nearer refracting surface can replace a
  deeper frost interface. Frost and refraction on the same selected surface work
  together. Ordinary RGB transparency still participates in OIT.
- **Screen-space refraction.** Hidden geometry and off-screen information are not
  reconstructed. A displaced sample can include objects in front of the receiving
  glass. B is relative to the source pixel's selected interface. Solid-sphere
  inversion uses an analytic sphere and a screen-depth approximation, not ray tracing.
- **No temporal or multisample antialiasing.** Silhouettes and narrow specular or
  Fresnel features can alias. The fixture's glass lighting is deliberately simple
  and is not an energy-conserving coupled reflection/transmission BSDF.
- **Low-resolution extinction.** Default XY is 1/8 raster coverage, with linear Z
  splatting and filtered XYZ LUT queries. Nearby layers can share bins, and sharp boundaries can
  differ from full-resolution sorted transparency.
- **Packed RGB overflow.** Packed integer addition has finite guard capacity;
  adversarial overdraw can carry between channel fields. The GPU oracle reports
  this explicitly. The experiment does not promise correct arbitrary RGB overdraw.
- **Floating-point blending order.** R11G11B10 additive targets are not bitwise
  order independent. RGBA16F CLI references help distinguish precision effects
  from ordering/classification errors.
- **Zero-T approximation.** Zero quads update SceneDepth using the RGBA8 LUT.
  Resolve still uses exp(-totalTau) from surviving full-resolution fragments.
  The values can differ; saturated regions may retain some HDR background.
- **Sparse frost validity.** Mirror taps repair invalid source samples using
  nearby visible color. This does not reconstruct hidden geometry. Entirely
  invalid filter footprints fall back to the ordinary OIT result.
- **Sample host ABI.** The camera/vertex buffers are a compact research interface,
  not a UE material compiler. The implementation uses forward D32 depth. Reverse-Z,
  instancing, skinning and host material permutations require explicit integration.
- **Performance scope.** Local validation uses an RTX 3070. No lower-end GPU or
  separate-machine validation is claimed. Detailed timestamp instrumentation adds
  overhead; prefer frame-only timings for overall comparisons.

The tests intentionally retain dense/half-precision/fixed-Z comparisons to expose
these tradeoffs. See [validation](VALIDATION.md).
