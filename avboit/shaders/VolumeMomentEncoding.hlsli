// SPDX-License-Identifier: MIT
// Four-moment change of basis from the authors' CC0 MBOIT reference:
// https://momentsingraphics.de/Media/I3D2018/Muenstermann2018-MBOITCode.zip
// Encode normalized moments into RGBA16_UNORM; odd components get a +0.5
// storage offset. The affine transform commutes with hardware bilinear filtering.
// Total optical depth is stored separately in R16_FLOAT.
#ifndef AVBOIT_VOLUME_MOMENT_ENCODING
#define AVBOIT_VOLUME_MOMENT_ENCODING
float4 EncodeVolumeMoments(float4 m)
{
    return float4(.5 + 1.5 * m.x - 2. * m.z, 4. * (m.y - m.w), .5 + .866025403784 * m.x - .38490017946 * m.z,
                  .5 * (m.y + m.w));
}
float4 DecodeVolumeMoments(float4 e)
{
    e.xz -= .5;
    return float4(-e.x / 3. + 1.732050807569 * e.z, .125 * e.y + e.w, -.75 * e.x + 1.299038105677 * e.z,
                  -.125 * e.y + e.w);
}
#endif
