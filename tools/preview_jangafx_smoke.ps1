# SPDX-License-Identifier: MIT
# JangaFX CC0 Smoke_Plume_01; see docs/BAKED_SMOKE.zh-CN.md.
param(
    [ValidateSet('half', '4bit')][string]$Precision = 'half',
    [switch]$SingleFrame
)
$projectRoot = Split-Path -Parent $PSScriptRoot
$sequenceFolder = if ($Precision -eq '4bit') { 'sequence128-4bit' } else { 'sequence128' }
$sequencePath = Join-Path $projectRoot "build/jangafx-smoke/$sequenceFolder"
$samplingArgs = if ($SingleFrame) { @('--smoke-single-frame') } else { @() }
$viewerPath = Join-Path $projectRoot 'bin/avboit_viewer.exe'
if (!(Test-Path -LiteralPath (Join-Path $sequencePath 'sequence.txt'))) {
    throw 'Import the JangaFX sequence first; see docs/BAKED_SMOKE.zh-CN.md.'
}
& $viewerPath --sponza --stress 24 --smoke-sequence $sequencePath --smoke-frame 0 --smoke-paused `
    --volume-step 0.025 --volume-step-scale 1 --volume-full-res --volume-reference-sampling `
    --volume-extinction 6 --no-smoke-mask --spot-intensity 0 --sun-intensity 6 `
    --taa --accum-half --composition-half --render-width 2560 --render-height 1440 --fps-limit 60 @samplingArgs
