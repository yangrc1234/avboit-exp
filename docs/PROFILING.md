# GPU profiling implementation notes

## Background refraction filtering comparison

Refraction resolves directly into the destination B cache before Gaussian using
bilinear RT inputs followed by one resolve. The measurements below record the
selection experiment; the four-resolve variant and comparison switch were removed.

RTX 3070, 2560x1440, packed accumulation/composition, StablePowerState, detailed
timers, two alternating 120-frame runs per variant:

| Fixture | Four resolves B / frame ms | Filtered inputs B / frame ms |
|---|---:|---:|
| RGB sphere with dense front smoke | 0.138 / 0.907 | 0.070 / 0.835 |
| Frosted RGB sphere and HDR emitters | 0.165 / 1.015 | 0.080 / 0.927 |
| Low RGB transmission layered panes | 0.708 / 5.567 | 0.275 / 5.234 |

Eight static 2K image fixtures used half-float composition for the quality comparison.
Frosted sphere (front/oblique) and low-transmission panes differed by at most 1/255
after tone mapping; separated Sponza glass differed by at most 2/255. Sharp sphere,
front smoke, RGB intersections and cutouts had rare boundary outliers up to 71/255:
only 0.026%-0.129% of interface pixels differed by more than 1/255. These are local
fixture measurements, not a guarantee for arbitrary material/depth discontinuities.

For the standalone DX12 omitted-MRT-output correctness and timing experiment, see
[MRT output test (Chinese)](MRT_OUTPUT_TEST.zh-CN.md).

## XeGTAO / Vanilla reference

Reviewed the public GameTechDev/XeGTAO source (2026-09-26). Its profiler belongs to the Vanilla sample framework, not the GTAO shader itself:

- [vaProfiler.h / scope and trace data](https://github.com/GameTechDev/XeGTAO/blob/master/Source/Core/vaProfiler.h)
- [vaProfiler.cpp / aggregation and ImGui UI](https://github.com/GameTechDev/XeGTAO/blob/master/Source/Core/vaProfiler.cpp)
- [vaGPUTimerDX12.cpp / DX12 timestamp backend](https://github.com/GameTechDev/XeGTAO/blob/master/Source/Rendering/DirectX/vaGPUTimerDX12.cpp)
- [vaGPUTimerDX12.h / buffered storage](https://github.com/GameTechDev/XeGTAO/blob/master/Source/Rendering/DirectX/vaGPUTimerDX12.h)

Named scopes record beginning/end timestamps and recursion depth. GPU scopes use a timestamp query heap. EndFrame resolves the used query range in one ResolveQueryData call, rotates through backbuffer-count-plus-one readback sets, and converts an older set into trace entries. Query frequency converts ticks to seconds; clock calibration aligns GPU and CPU time axes. The framework's frame buffering underpins the delayed readback.

vaTracerView groups entries into a scope tree, accumulating inclusive duration, instance counts and min/max; self time subtracts child contributions. The built-in ImGui view displays the tree's average milliseconds per frame, with double-click expansion. Collection/display views swap at a 1.5-second update cadence. Full time-positioned events can be exported to Chrome tracing. It is not simply a graph of CPU submission durations.

## This prototype

GpuProfiler currently uses NVRHI timer queries and six asynchronous query slots. The ImGui front end shows per-pass mean durations, proportional bars and a selected-pass history (up to 600 samples), refreshing statistics at 4 Hz. These bars show cost distribution, not absolute GPU timeline positions. The render interval excludes the UI draw, presentation and command-list-close restoration. The viewer has only a flat pass list today; hierarchical CPU/GPU tracing is not claimed.

NVRHI's current DX12 endTimerQuery immediately resolves each timer, unlike XeGTAO's frame-batched resolve. Detailed profiling therefore adds visible overhead. Frame-only mode reduces instrumentation for overall A/B comparisons. A future custom DX12 query heap could defer all resolves until frame end and expose absolute timestamps for a proper trace. That backend change is not silently included in these measurements.

Only public source behavior was studied; XeGTAO profiler code has not been vendored. Donut supplies the existing ImGui integration. Build-directory reference downloads are excluded from source packaging.

Zero-T candidate tile IDs and the indirect instance count are emitted during integration. The depth VS retains the full neighborhood/depth test; one indirect draw replaces the full-screen instance grid. Count reset is included in extinction_clear, candidate emission in integration, and indirect execution in zero_depth, so compare all three together. There is no zero_prepare pass or depth copy. VFX offset and HDR apply are separate stages. The current detailed ring has 19 stages x 6 frames = 114 timer queries.

Both command-line and ImGui benchmarks use a StablePowerState RAII scope before warmup and restore normal state afterward. They check Developer Mode before calling the D3D12 API and cancel on failure rather than label unlocked measurements as stable. CSV metadata records stable_power_state=1. Normal interactive rendering does not set stable power. See [Microsoft's profiling contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-setstablepowerstate): stable clocks may be lower than normal boost clocks, so this is an A/B comparison mode rather than a prediction of normal gameplay performance.

## Frost reconstruction comparison

The default cubic reconstruction filters each adjacent Gaussian mip with four bilinear taps, then interpolates their results (eight taps). `--cubic-trilinear`, or **4-tap cubic + trilinear (approximate)** below the ImGui cubic checkbox, shares the finer mip's cubic coordinates and weights across four hardware trilinear taps. `--linear` selects the one-instruction trilinear baseline.

The four-tap mode is exact at integer LOD, but its coarser-mip contribution is approximate at fractional LOD. Check HDR highlights and transitions across integer LOD before adopting it. The existing cubic producer padding is retained; no RT or pass is added. Four trilinear instructions still access two mip levels each, so neither texture traffic nor frame time is expected to halve. The exact two-level approach is also described in [GPU Gems 2, section 20.3](https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-20-fast-third-order-texture-filtering).

2026-10-12 comparison on RTX 3070, 2560x1440, sparse first filter, three Gaussian mips, StablePowerState enabled. Each mode used two runs of 30 warmup + 600 measured frames in ABBA order; values are the mean of the two run medians. Frame-only queries measure overall cost; separate detailed runs locate the change.

| Scene | Interface frost coverage | Exact 8-tap frame | Approximate 4-tap frame | Frame saving |
| --- | ---: | ---: | ---: | ---: |
| Sponza, `--stress 10` | 8.4% | 1.726 ms | 1.722 ms | 0.004 ms / 0.24% |
| Near pane, `--stress 16 --camera-preset 3` | 81.5% | 2.694 ms | 2.668 ms | 0.026 ms / 0.98% |

The composition/copy stage changed from 0.083 to 0.077 ms and from 0.472 to 0.449 ms respectively. The small Sponza frame delta is comparable to inter-run variation. These are prototype-scene measurements, not a low-end GPU prediction.

The approximate path reintroduces a grid around HDR highlights. Varying the material sigma cap by +/-0.016 reference pixels around the mip-1 boundary produced a maximum display-channel change of 16/255 with four taps versus 1/255 with eight taps (same pixels and scene). A poison/clean comparison was identical, confirming this observed difference was reconstruction error rather than an unwritten producer halo. Exact eight-tap reconstruction remains the default.
