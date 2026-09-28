# GPU profiling implementation notes

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

Zero-T now generates/rejects tile quads directly in the depth VS. Only zero_depth remains; zero_prepare and the depth copy have been removed. VFX offset and HDR apply are separate stages. The current detailed ring has 20 stages x 6 frames = 120 timer queries.

Both command-line and ImGui benchmarks use a StablePowerState RAII scope before warmup and restore normal state afterward. They check Developer Mode before calling the D3D12 API and cancel on failure rather than label unlocked measurements as stable. CSV metadata records stable_power_state=1. Normal interactive rendering does not set stable power. See [Microsoft's profiling contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-setstablepowerstate): stable clocks may be lower than normal boost clocks, so this is an A/B comparison mode rather than a prediction of normal gameplay performance.
