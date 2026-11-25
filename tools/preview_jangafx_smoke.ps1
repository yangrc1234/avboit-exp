# SPDX-License-Identifier: MIT
# JangaFX CC0 Smoke_Plume_01; see docs/BAKED_SMOKE.zh-CN.md.
param(
    [ValidateSet('half', '4bit')][string]$Precision,
    [switch]$ReferenceSampling,
    [switch]$SingleFrame, # Compatibility with earlier preview commands.
    [ValidateSet(0, 10, 25)][int]$VoxelCm = 0,
    [ValidateRange(0, 165)][int]$Frame = 0
)
$projectRoot = Split-Path -Parent $PSScriptRoot
$grid = switch ($VoxelCm) { 10 { 34 }; 25 { 14 }; default { 128 } }
if (!$PSBoundParameters.ContainsKey('Precision')) {
    $Precision = if (Test-Path -LiteralPath (Join-Path $projectRoot "build/jangafx-smoke/sequence$grid-4bit/sequence.txt")) { '4bit' } else { 'half' }
}
$sequenceFolder = "sequence$grid" + $(if ($Precision -eq '4bit') { '-4bit' } else { '' })
$sequencePath = Join-Path $projectRoot "build/jangafx-smoke/$sequenceFolder"
$viewerPath = Join-Path $projectRoot 'bin/avboit_viewer.exe'
if (!(Test-Path -LiteralPath (Join-Path $sequencePath 'sequence.txt'))) {
    throw 'Import the JangaFX sequence first; see docs/BAKED_SMOKE.zh-CN.md.'
}
Write-Host ("Density grid {0}^3: {1:F2} cm voxels in the default 3.4 m bounds." -f $grid, (340.0 / $grid))
$samplingArgs = if ($ReferenceSampling) { @('--volume-reference-sampling') } else { @() }
& $viewerPath --sponza --stress 24 --smoke-sequence $sequencePath --smoke-frame $Frame --smoke-paused `
    --volume-step 0.025 --volume-step-scale 1 --volume-full-res @samplingArgs `
    --volume-extinction 6 --no-smoke-mask --spot-intensity 0 --sun-intensity 6 `
    --taa --accum-half --composition-half --render-width 2560 --render-height 1440 --fps-limit 60
