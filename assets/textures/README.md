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
