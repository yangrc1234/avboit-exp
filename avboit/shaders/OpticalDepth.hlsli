// SPDX-License-Identifier: MIT
#ifndef AVBOIT_OPTICAL_DEPTH
#define AVBOIT_OPTICAL_DEPTH
static const float ExtinctionTauMax = log(255.);
// Packed extinction uses round-to-nearest quantization.
uint QuantizeOpticalDepth(float tau, float tauMax, uint steps, bool nearest)
{
    return uint(saturate(tau / tauMax) * float(steps) + (nearest ? .5 : 0.));
}
float3 DecodeOpticalDepth(float3 code, float tauMax, uint steps)
{
    return code * tauMax / float(steps);
}
float3 TransmittanceToOpticalDepth(float3 t)
{
    return -log(max(t, .000001));
}
float3 OpticalDepthToTransmittance(float3 tau)
{
    return exp(-tau);
}
#endif
