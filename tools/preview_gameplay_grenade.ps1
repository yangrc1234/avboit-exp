# SPDX-License-Identifier: MIT
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $projectRoot 'bin/avboit_viewer.exe') --sponza --stress 24 --gameplay-grenade `
    --volume-step 0.1 --volume-extinction 6 --sun-intensity 6 --spot-intensity 0 `
    --render-width 2560 --render-height 1440 --fps-limit 120
