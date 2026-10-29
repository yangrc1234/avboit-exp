# Frost normal map

`frost-normal.dds` derives from **Plaster 001** by ambientCG / Lennart Demes:

- Source: <https://ambientcg.com/view?id=Plaster001>
- Download: <https://ambientcg.com/get?file=Plaster001_1K-PNG.zip>
- License: [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/),
  confirmed by [ambientCG's asset license](https://docs.ambientcg.com/license/).
- Original file: `Plaster001_1K-PNG_NormalDX.png` (1024x1024).
- Conversion: 512x512, linear RG8 XY, DirectX normal convention, complete
  box-filtered mip chain. Z is unused by the demo offset shader.

To reproduce, download the source archive and run
`python tools/prepare_frost_normal.py PATH/TO/Plaster001_1K-PNG.zip`.
The DDS is bundled in the repository and runtime; no download is needed to run.

ImGui's `Frost normal tiling` changes texture repeats across a pane (default 1).
The sample decodes normal XY to a screen-space offset, scaled by the material's
displacement and refraction gain. This is an artistic approximation, not a full
tangent-frame refraction solve. Sphere and sharp-glass shading are unchanged.

## OpenVDB smoke volume

`smoke-volume.dds` is converted from the OpenVDB project's official `smoke.vdb`
sample, distributed by the Academy Software Foundation / OpenVDB contributors.

- Source: https://www.openvdb.org/download/
- Original: https://media.githubusercontent.com/media/AcademySoftwareFoundation/openvdb-website/master/download/models/smoke.vdb
- Repository license: https://github.com/AcademySoftwareFoundation/openvdb-website/blob/master/LICENSE.txt
- Asset license: **CC BY 4.0**, https://creativecommons.org/licenses/by/4.0/ .
  The derived DDS retains this license; it is not MIT.
- Source SHA-256: `735c22eecf8b498aa0c0c7a4098ea4f2c4ad1f63bf601c278094020301093ac9`.
- Changes: crop to the sample ROI, area-resample to 64x128x64, store original
  density in R and a computed +Y density integral in G, linear RG16F, one mip.
  Runtime scales the integral by world height and extinction for a fixed top light.

The 2 MiB DDS is bundled; the VDB and JavaScript decoder are not runtime dependencies.
To reproduce: obtain the VDB and the MIT `openvdb@0.5.1` JavaScript reader, then run
`python tools/prepare_smoke_volume.py smoke.vdb --reader PATH/TO/openvdb/index.js`.
Requires Node.js, NumPy and Pillow. The original ROI, not the enormous inactive
file bounding box, defines the converted volume.
