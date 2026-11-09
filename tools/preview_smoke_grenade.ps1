# SPDX-License-Identifier: MIT
# Quality-first local preview. Bake/import instructions: docs/BAKED_SMOKE.zh-CN.md.
$projectRoot = Split-Path -Parent $PSScriptRoot
$sequencePath = Join-Path $projectRoot 'build/smoke-grenade/sequence-half'
$viewerPath = Join-Path $projectRoot 'bin/avboit_viewer.exe'
if (!(Test-Path -LiteralPath (Join-Path $sequencePath 'sequence.txt'))) {
    throw 'Bake and import the smoke sequence first; see docs/BAKED_SMOKE.zh-CN.md.'
}
& $viewerPath --sponza --stress 24 --smoke-sequence $sequencePath --smoke-frame 79 --smoke-paused `
    --volume-step 0.025 --volume-step-scale 1 --volume-full-res --volume-reference-sampling `
    --volume-extinction 16 --no-smoke-mask --spot-intensity 0 --sun-intensity 6 `
    --taa --accum-half --composition-half --render-width 2560 --render-height 1440 --fps-limit 60
