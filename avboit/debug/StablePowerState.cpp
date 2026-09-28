// SPDX-License-Identifier: MIT
#include "StablePowerState.h"
#include <nvrhi/d3d12.h>
#include <cstdio>
#include <stdexcept>

namespace avboit
{
StablePowerState::StablePowerState(nvrhi::IDevice *device)
{
    // D3D12 documents device removal when called outside Developer Mode.
    // Read the setting; enabling it is a user/system configuration decision.
    DWORD enabled = 0, size = sizeof(enabled);
    const auto status =
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock",
                     L"AllowDevelopmentWithoutDevLicense", RRF_RT_REG_DWORD, nullptr, &enabled, &size);
    if (status != ERROR_SUCCESS || enabled == 0)
        throw std::runtime_error(
            "StablePowerState benchmark requires Windows Developer Mode; no timing data exported.");
    auto *native = static_cast<ID3D12Device *>(device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device));
    if (!native)
        throw std::runtime_error("StablePowerState requires a native D3D12 device.");
    const HRESULT result = native->SetStablePowerState(TRUE);
    if (FAILED(result))
    {
        char error[192];
        snprintf(error, sizeof(error), "SetStablePowerState(TRUE) failed (0x%08lX); benchmark cancelled.",
                 static_cast<unsigned long>(result));
        throw std::runtime_error(error);
    }
    m_device = native;
    printf("StablePowerState enabled for benchmark\n");
}
StablePowerState::~StablePowerState()
{
    const HRESULT result = m_device->SetStablePowerState(FALSE);
    if (FAILED(result))
        fprintf(stderr, "SetStablePowerState(FALSE) failed (0x%08lX)\n", static_cast<unsigned long>(result));
    else
        printf("StablePowerState restored after benchmark\n");
}
} // namespace avboit
