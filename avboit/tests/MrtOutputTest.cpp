// SPDX-License-Identifier: MIT
#include <donut/app/DeviceManager.h>
#include <donut/app/ApplicationBase.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <nvrhi/d3d12.h>
#include "debug/StablePowerState.h"
#include <wrl/client.h>
#include <d3d12sdklayers.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace
{
constexpr UINT Width = 2560, Height = 1440, TargetCount = 4;

void Check(HRESULT hr, const char *operation)
{
    if (FAILED(hr))
    {
        char message[256];
        snprintf(message, sizeof(message), "%s failed: 0x%08lX", operation, static_cast<unsigned long>(hr));
        throw std::runtime_error(message);
    }
}

struct Mode
{
    const char *name;
    const char *entry;
    UINT boundTargets;
    UINT writeTargets;
    UINT shaderTargets;
    UINT zeroTargets = 0;
};

constexpr Mode Modes[] = {
    {"one_bound_one_output", "one", 1, 0xf, 0x1},
    {"four_bound_one_output", "one", 4, 0xf, 0x1},
    {"four_bound_one_output_masked", "one", 4, 0x1, 0x1},
    {"four_outputs_masked_to_one", "four", 4, 0x1, 0xf},
    {"four_bound_three_outputs", "three", 4, 0xf, 0x7},
    {"four_bound_four_outputs", "four", 4, 0xf, 0xf},
    {"four_bound_zero_extras", "zeroExtra", 4, 0xf, 0xf, 0xe},
    {"four_bound_outputs_0_3", "holes", 4, 0xf, 0x9},
    {"three_bound_three_outputs", "three", 3, 0xf, 0x7},
    {"four_bound_zero_last", "zeroLast", 4, 0xf, 0xf, 0x8},
};
constexpr size_t ModeCount = sizeof(Modes) / sizeof(Modes[0]);
constexpr size_t FullOutputMode = 5;

constexpr Mode WorkModes[] = {
    {"stripped", "one", 4, 0x1, 0x1},
    {"alu64_masked", "alu64", 4, 0x1, 0x3},
    {"alu64_written", "alu64", 4, 0x3, 0x3},
    {"alu256_masked", "alu256", 4, 0x1, 0x3},
    {"alu256_written", "alu256", 4, 0x3, 0x3},
    {"texture8_masked", "texture8", 4, 0x1, 0x3},
    {"texture8_written", "texture8", 4, 0x3, 0x3},
    {"texture32_masked", "texture32", 4, 0x1, 0x3},
    {"texture32_written", "texture32", 4, 0x3, 0x3},
    {"texture128_masked", "texture128", 4, 0x1, 0x3},
    {"texture128_written", "texture128", 4, 0x3, 0x3},
};

class MrtTest
{
  public:
    MrtTest(nvrhi::IDevice *rhiDevice, DXGI_FORMAT format, UINT bytesPerPixel, bool renderPass, bool shaderWork)
        : m_device(static_cast<ID3D12Device *>(rhiDevice->getNativeObject(nvrhi::ObjectTypes::D3D12_Device))),
          m_format(format), m_bytesPerPixel(bytesPerPixel), m_renderPass(renderPass), m_shaderWork(shaderWork),
          m_modes(shaderWork ? WorkModes : Modes),
          m_modeCount(shaderWork ? sizeof(WorkModes) / sizeof(WorkModes[0]) : ModeCount)
    {
        // Native PSOs: no RHI may infer masks from the shader signature.
        if (m_renderPass)
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS18 options{};
            Check(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS18, &options, sizeof(options)),
                  "Check render pass support");
            if (!options.RenderPassesValid)
                throw std::runtime_error("--render-pass requires a runtime with valid BeginRenderPass support.");
        }
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        Check(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue)), "Create queue");
        Check(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator)),
              "Create allocator");
        Check(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&m_commands)),
              "Create command list");
        Check(m_commands->Close(), "Close initial command list");
        Check(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "Create fence");
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = TargetCount;
        Check(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvs)), "Create RTV heap");

        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues = 1;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &range;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = shaderWork ? 2 : 1;
        rootDesc.pParameters = parameters;
        rootDesc.NumStaticSamplers = shaderWork ? 1 : 0;
        rootDesc.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> rootBlob, errors;
        Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errors),
              "Serialize root signature");
        Check(m_device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                            IID_PPV_ARGS(&m_root)),
              "Create root signature");

        if (shaderWork)
        {
            m_workTexture = rhiDevice->createTexture(nvrhi::TextureDesc()
                                                         .setWidth(512)
                                                         .setHeight(512)
                                                         .setFormat(nvrhi::Format::RGBA8_UNORM)
                                                         .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                                         .setKeepInitialState(true)
                                                         .setDebugName("Write-mask test random texture"));
            std::vector<uint32_t> pixels(512 * 512);
            std::mt19937 random(20261019);
            for (auto &pixel : pixels)
                pixel = random();
            auto upload = rhiDevice->createCommandList();
            upload->open();
            upload->writeTexture(m_workTexture, 0, 0, pixels.data(), 512 * 4);
            upload->close();
            rhiDevice->executeCommandList(upload);
            rhiDevice->waitForIdle();
            D3D12_DESCRIPTOR_HEAP_DESC srvHeap{};
            srvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvHeap.NumDescriptors = 1;
            srvHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            Check(m_device->CreateDescriptorHeap(&srvHeap, IID_PPV_ARGS(&m_srvs)), "Create work texture SRV heap");
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            auto *nativeTexture =
                static_cast<ID3D12Resource *>(m_workTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource));
            m_device->CreateShaderResourceView(nativeTexture, &srv, m_srvs->GetCPUDescriptorHandleForHeapStart());
        }

        D3D12_RESOURCE_DESC texture{};
        texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = Width;
        texture.Height = Height;
        texture.DepthOrArraySize = texture.MipLevels = 1;
        texture.Format = format;
        texture.SampleDesc.Count = 1;
        texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES gpuHeap{};
        gpuHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const UINT descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        for (UINT i = 0; i < TargetCount; ++i)
        {
            Check(m_device->CreateCommittedResource(&gpuHeap, D3D12_HEAP_FLAG_NONE, &texture,
                                                    D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                    IID_PPV_ARGS(&m_targets[i])),
                  "Create render target");
            m_handles[i] = m_rtvs->GetCPUDescriptorHandleForHeapStart();
            m_handles[i].ptr += SIZE_T(i) * descriptorSize;
            m_device->CreateRenderTargetView(m_targets[i].Get(), nullptr, m_handles[i]);
        }
        UINT64 size = 0;
        m_device->GetCopyableFootprints(&texture, 0, 1, 0, &m_footprint, nullptr, nullptr, &size);
        m_targetStride = (size + 511) & ~UINT64(511);
        m_pixels = Readback(m_targetStride * TargetCount);
        m_times = Readback(2 * m_modeCount * sizeof(UINT64));
        D3D12_QUERY_HEAP_DESC queryDesc{};
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDesc.Count = 2 * UINT(m_modeCount);
        Check(m_device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&m_queries)), "Create timestamp heap");
        Check(m_queue->GetTimestampFrequency(&m_frequency), "Get timestamp frequency");

        donut::engine::ShaderFactory shaders(rhiDevice, std::make_shared<donut::vfs::NativeFileSystem>(),
                                             donut::app::GetDirectoryWithExecutable() / "shaders/avboit/dxil");
        auto vs = shaders.CreateShader("shaders/MrtOutputTest.hlsl", "fullscreen", nullptr, nvrhi::ShaderType::Vertex);
        if (!vs)
            throw std::runtime_error("Missing MRT test vertex shader");
        auto initializePs =
            shaders.CreateShader("shaders/MrtOutputTest.hlsl", "four", nullptr, nvrhi::ShaderType::Pixel);
        m_initializePipeline = Pipeline(vs, initializePs, Modes[FullOutputMode], false);
        for (auto &pipelines : m_pipelines)
            pipelines.resize(m_modeCount);
        for (size_t mode = 0; mode < m_modeCount; ++mode)
        {
            auto ps = shaders.CreateShader("shaders/MrtOutputTest.hlsl", m_modes[mode].entry, nullptr,
                                           nvrhi::ShaderType::Pixel);
            if (!ps)
                throw std::runtime_error("Missing MRT test pixel shader");
            for (unsigned additive = 0; additive < 2; ++additive)
                m_pipelines[additive][mode] = Pipeline(vs, ps, m_modes[mode], additive != 0);
        }
    }

    bool Verify()
    {
        const auto before = Capture(-1, false);
        bool success = true;
        for (unsigned additive = 0; additive < 2; ++additive)
        {
            const auto reference = Capture(m_shaderWork ? 0 : int(FullOutputMode), additive != 0);
            for (size_t mode = 0; mode < m_modeCount; ++mode)
            {
                const auto actual = Capture(int(mode), additive != 0);
                // Written heavy outputs are compared with their own live PSO;
                // masked siblings must preserve Target1, while Target0 stays cheap.
                const auto workReference = m_shaderWork && mode > 0
                                               ? Capture(int(mode % 2 ? mode + 1 : mode), additive != 0)
                                               : std::vector<uint8_t>();
                const auto &config = m_modes[mode];
                UINT64 changed[TargetCount]{}, wrong[TargetCount]{};
                for (UINT target = 0; target < TargetCount; ++target)
                    for (UINT y = 0; y < Height; ++y)
                        for (UINT x = 0; x < Width; ++x)
                        {
                            const size_t offset = size_t(target) * Width * Height * m_bytesPerPixel +
                                                  (size_t(y) * Width + x) * m_bytesPerPixel;
                            const bool written = target < config.boundTargets &&
                                                 (config.writeTargets & config.shaderTargets & (1u << target));
                            const uint8_t zero[8]{};
                            const uint8_t *expected = (written ? reference : before).data() + offset;
                            if (written && target == 1 && m_shaderWork)
                                expected = workReference.data() + offset;
                            if (config.zeroTargets & (1u << target))
                                expected = additive ? before.data() + offset : zero;
                            changed[target] +=
                                memcmp(actual.data() + offset, before.data() + offset, m_bytesPerPixel) != 0;
                            wrong[target] += memcmp(actual.data() + offset, expected, m_bytesPerPixel) != 0;
                        }
                UINT64 failures = std::accumulate(std::begin(wrong), std::end(wrong), UINT64(0));
                for (UINT target = 0; target < TargetCount; ++target)
                {
                    const bool written =
                        target < config.boundTargets && (config.writeTargets & config.shaderTargets & (1u << target));
                    const bool zero = (config.zeroTargets & (1u << target)) != 0;
                    // A completely missing draw must not pass merely because every RT stayed unchanged.
                    if (written && (!zero || !additive) && changed[target] < UINT64(Width) * Height * 9 / 10)
                        ++failures;
                }
                success &= failures == 0;
                printf("%s %s: changed=[%llu,%llu,%llu,%llu] mismatches=%llu %s\n", additive ? "add" : "replace",
                       config.name, changed[0], changed[1], changed[2], changed[3], failures,
                       failures ? "FAIL" : "PASS");
            }
        }
        return success;
    }

    void Benchmark(std::ostream &csv, const char *formatName, unsigned rounds, unsigned draws)
    {
        std::mt19937 random(20261017);
        for (unsigned additive = 0; additive < 2; ++additive)
        {
            std::vector<std::vector<double>> samples(m_modeCount);
            for (unsigned round = 0; round < rounds + 8; ++round)
            {
                std::vector<size_t> order(m_modeCount);
                std::iota(order.begin(), order.end(), 0);
                std::shuffle(order.begin(), order.end(), random);
                Reset();
                for (size_t mode : order)
                {
                    // Populate every attachment outside the timing interval. No constant-color clear.
                    BeginPass(4);
                    InitializeTargets();
                    EndPass();
                    m_commands->EndQuery(m_queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, UINT(2 * mode));
                    BeginPass(m_modes[mode].boundTargets);
                    m_commands->SetPipelineState(m_pipelines[additive][mode].Get());
                    for (unsigned draw = 0; draw < draws; ++draw)
                    {
                        m_commands->SetGraphicsRoot32BitConstant(0, 91 + draw, 0);
                        m_commands->DrawInstanced(3, 1, 0, 0);
                    }
                    EndPass();
                    m_commands->EndQuery(m_queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, UINT(2 * mode + 1));
                }
                m_commands->ResolveQueryData(m_queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, UINT(2 * m_modeCount),
                                             m_times.Get(), 0);
                Submit();
                const D3D12_RANGE readRange{0, 2 * m_modeCount * sizeof(UINT64)};
                UINT64 *ticks = nullptr;
                Check(m_times->Map(0, &readRange, reinterpret_cast<void **>(&ticks)), "Map timestamps");
                if (round >= 8)
                    for (size_t mode = 0; mode < m_modeCount; ++mode)
                    {
                        const double ms = double(ticks[2 * mode + 1] - ticks[2 * mode]) * 1000.0 / m_frequency / draws;
                        samples[mode].push_back(ms);
                        csv << formatName << ',' << (additive ? "add" : "replace") << ',' << m_modes[mode].name << ','
                            << round - 8 << ',' << draws << ',' << std::setprecision(9) << ms << '\n';
                    }
                const D3D12_RANGE noWrites{0, 0};
                m_times->Unmap(0, &noWrites);
            }
            for (size_t mode = 0; mode < m_modeCount; ++mode)
            {
                auto values = samples[mode];
                std::sort(values.begin(), values.end());
                printf("%s %s %-32s median=%.6f p10=%.6f p90=%.6f ms/draw\n", formatName, additive ? "add" : "replace",
                       m_modes[mode].name, values[values.size() / 2], values[values.size() / 10],
                       values[values.size() * 9 / 10]);
            }
        }
    }

  private:
    ComPtr<ID3D12Resource> Readback(UINT64 size)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = size;
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> resource;
        Check(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                nullptr, IID_PPV_ARGS(&resource)),
              "Create readback");
        return resource;
    }

    ComPtr<ID3D12PipelineState> Pipeline(nvrhi::IShader *vs, nvrhi::IShader *ps, const Mode &mode, bool additive)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = m_root.Get();
        vs->getBytecode(&desc.VS.pShaderBytecode, &desc.VS.BytecodeLength);
        ps->getBytecode(&desc.PS.pShaderBytecode, &desc.PS.BytecodeLength);
        desc.NumRenderTargets = mode.boundTargets;
        desc.BlendState.IndependentBlendEnable = TRUE;
        for (UINT target = 0; target < mode.boundTargets; ++target)
        {
            desc.RTVFormats[target] = m_format;
            auto &blend = desc.BlendState.RenderTarget[target];
            blend.BlendEnable = additive;
            blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlend = blend.DestBlendAlpha = additive ? D3D12_BLEND_ONE : D3D12_BLEND_ZERO;
            blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.LogicOp = D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask = (mode.writeTargets & (1u << target)) ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;
        }
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> pipeline;
        Check(m_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)), "Create MRT PSO");
        return pipeline;
    }

    void Reset()
    {
        Check(m_allocator->Reset(), "Reset allocator");
        Check(m_commands->Reset(m_allocator.Get(), nullptr), "Reset command list");
        m_commands->SetGraphicsRootSignature(m_root.Get());
        if (m_srvs)
        {
            ID3D12DescriptorHeap *heaps[] = {m_srvs.Get()};
            m_commands->SetDescriptorHeaps(1, heaps);
            m_commands->SetGraphicsRootDescriptorTable(1, m_srvs->GetGPUDescriptorHandleForHeapStart());
        }
        m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_VIEWPORT viewport{0, 0, float(Width), float(Height), 0, 1};
        const D3D12_RECT scissor{0, 0, LONG(Width), LONG(Height)};
        m_commands->RSSetViewports(1, &viewport);
        m_commands->RSSetScissorRects(1, &scissor);
    }

    void BeginPass(UINT count)
    {
        if (!m_renderPass)
        {
            m_commands->OMSetRenderTargets(count, m_handles, FALSE, nullptr);
            return;
        }
        D3D12_RENDER_PASS_RENDER_TARGET_DESC targets[TargetCount]{};
        for (UINT i = 0; i < count; ++i)
        {
            targets[i].cpuDescriptor = m_handles[i];
            targets[i].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
            targets[i].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
        }
        m_commands->BeginRenderPass(count, targets, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
    }

    void EndPass()
    {
        if (m_renderPass)
            m_commands->EndRenderPass();
    }

    void Draw(size_t mode, bool additive, UINT seed)
    {
        m_commands->SetPipelineState(m_pipelines[additive ? 1 : 0][mode].Get());
        m_commands->SetGraphicsRoot32BitConstant(0, seed, 0);
        m_commands->DrawInstanced(3, 1, 0, 0);
    }

    void InitializeTargets()
    {
        m_commands->SetPipelineState(m_initializePipeline.Get());
        m_commands->SetGraphicsRoot32BitConstant(0, 17, 0);
        m_commands->DrawInstanced(3, 1, 0, 0);
    }

    void Submit()
    {
        Check(m_commands->Close(), "Close command list");
        ID3D12CommandList *lists[] = {m_commands.Get()};
        m_queue->ExecuteCommandLists(1, lists);
        Check(m_queue->Signal(m_fence.Get(), ++m_fenceValue), "Signal fence");
        // A null event waits on the CPU; no window, presentation, or CPU wall time enters the query.
        Check(m_fence->SetEventOnCompletion(m_fenceValue, nullptr), "Wait for GPU");
        Check(m_device->GetDeviceRemovedReason(), "GPU completion");
    }

    std::vector<uint8_t> Capture(int mode, bool additive)
    {
        Reset();
        BeginPass(4);
        InitializeTargets();
        // Exercise multiple shader output signatures within the very same render pass.
        if (mode >= 0 && m_modes[mode].boundTargets == 4)
            Draw(size_t(mode), additive, 91);
        EndPass();
        if (mode >= 0 && m_modes[mode].boundTargets != 4)
        {
            BeginPass(m_modes[mode].boundTargets);
            Draw(size_t(mode), additive, 91);
            EndPass();
        }
        for (UINT target = 0; target < TargetCount; ++target)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_targets[target].Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            m_commands->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
            source.pResource = m_targets[target].Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.pResource = m_pixels.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = m_footprint;
            destination.PlacedFootprint.Offset = target * m_targetStride;
            m_commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            m_commands->ResourceBarrier(1, &barrier);
        }
        Submit();
        uint8_t *mapped = nullptr;
        const D3D12_RANGE readRange{0, SIZE_T(TargetCount * m_targetStride)};
        Check(m_pixels->Map(0, &readRange, reinterpret_cast<void **>(&mapped)), "Map RT pixels");
        std::vector<uint8_t> result(size_t(TargetCount) * Width * Height * m_bytesPerPixel);
        for (UINT target = 0; target < TargetCount; ++target)
            for (UINT y = 0; y < Height; ++y)
                memcpy(result.data() + (size_t(target) * Height + y) * Width * m_bytesPerPixel,
                       mapped + target * m_targetStride + size_t(y) * m_footprint.Footprint.RowPitch,
                       size_t(Width) * m_bytesPerPixel);
        const D3D12_RANGE noWrites{0, 0};
        m_pixels->Unmap(0, &noWrites);
        return result;
    }

    ID3D12Device *m_device;
    DXGI_FORMAT m_format;
    UINT m_bytesPerPixel;
    bool m_renderPass;
    bool m_shaderWork;
    const Mode *m_modes;
    size_t m_modeCount;
    nvrhi::TextureHandle m_workTexture;
    ComPtr<ID3D12DescriptorHeap> m_srvs;
    ComPtr<ID3D12PipelineState> m_initializePipeline;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ComPtr<ID3D12GraphicsCommandList4> m_commands;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0, m_frequency = 0, m_targetStride = 0;
    ComPtr<ID3D12DescriptorHeap> m_rtvs;
    ComPtr<ID3D12RootSignature> m_root;
    std::array<ComPtr<ID3D12Resource>, TargetCount> m_targets;
    D3D12_CPU_DESCRIPTOR_HANDLE m_handles[TargetCount]{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_footprint{};
    ComPtr<ID3D12Resource> m_pixels, m_times;
    ComPtr<ID3D12QueryHeap> m_queries;
    std::array<std::vector<ComPtr<ID3D12PipelineState>>, 2> m_pipelines;
};
} // namespace

int main(int argc, const char **argv)
{
    donut::log::ConsoleApplicationMode();
    try
    {
        bool benchmark = false, debug = false, renderPass = false, shaderWork = false;
        unsigned rounds = 80, draws = 32;
        std::string csvPath = "mrt-output.csv";
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--benchmark")
                benchmark = true;
            else if (arg == "--debug")
                debug = true;
            else if (arg == "--render-pass")
                renderPass = true;
            else if (arg == "--shader-work")
                shaderWork = true;
            else if ((arg == "--rounds" || arg == "--draws" || arg == "--csv") && i + 1 < argc)
            {
                if (arg == "--csv")
                    csvPath = argv[++i];
                else
                {
                    const int value = std::stoi(argv[++i]);
                    if (value < 1 || value > 10000)
                        throw std::runtime_error("rounds/draws must be in [1, 10000]");
                    (arg == "--rounds" ? rounds : draws) = unsigned(value);
                }
            }
            else
                throw std::runtime_error("Usage: avboit_mrt_test [--debug] [--render-pass] [--shader-work] "
                                         "[--benchmark --rounds N --draws N --csv path]");
        }
        if (debug && benchmark)
            throw std::runtime_error("Run correctness with --debug separately from performance measurements.");
        auto manager =
            std::unique_ptr<donut::app::DeviceManager>(donut::app::DeviceManager::Create(nvrhi::GraphicsAPI::D3D12));
        donut::app::DeviceCreationParameters parameters;
        parameters.enableDebugRuntime = debug;
        if (!manager->CreateHeadlessDevice(parameters))
            throw std::runtime_error("Cannot create DX12 device");
        const char *binding = renderPass ? "BeginRenderPass" : "OMSetRenderTargets";
        printf("GPU: %s; %ux%u; native %s; DXIL; debug=%d\n", manager->GetRendererString(), Width, Height, binding,
               debug);
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (debug)
        {
            auto *native =
                static_cast<ID3D12Device *>(manager->GetDevice()->getNativeObject(nvrhi::ObjectTypes::D3D12_Device));
            Check(native->QueryInterface(IID_PPV_ARGS(&infoQueue)), "Get debug info queue");
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
            infoQueue->ClearStoredMessages();
        }
        bool success = true;
        {
            std::ofstream csv;
            std::unique_ptr<avboit::StablePowerState> stable;
            if (benchmark)
            {
                stable = std::make_unique<avboit::StablePowerState>(manager->GetDevice());
                csv.open(csvPath);
                if (!csv)
                    throw std::runtime_error("Cannot open CSV");
                csv << "# GPU=" << manager->GetRendererString() << "; resolution=2560x1440; stable_power_state=1; "
                    << "randomized_mode_order=1; warmup_rounds=8; binding=" << binding
                    << "; suite=" << (shaderWork ? "write-mask-shader-work" : "output-signatures")
                    << "; timestamp_scope=bind-and-draw-batch; initialization-and-readback-excluded=1\n"
                    << "format,blend,mode,round,draws,ms_per_draw\n";
            }
            for (bool half : {false, true})
            {
                const char *name = half ? "RGBA16_FLOAT" : "R11G11B10_FLOAT";
                printf("Format: %s\n", name);
                MrtTest test(manager->GetDevice(), half ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R11G11B10_FLOAT,
                             half ? 8 : 4, renderPass, shaderWork);
                success &= test.Verify();
                if (!success)
                    break;
                if (benchmark)
                    test.Benchmark(csv, name, rounds, draws);
            }
        }
        if (infoQueue)
        {
            unsigned errors = 0, warnings = 0;
            for (UINT64 index = 0; index < infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index)
            {
                SIZE_T size = 0;
                Check(infoQueue->GetMessage(index, nullptr, &size), "Get debug message size");
                std::vector<uint8_t> storage(size);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                Check(infoQueue->GetMessage(index, message, &size), "Get debug message");
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                {
                    printf("D3D12 debug [%u]: %s\n", unsigned(message->ID), message->pDescription);
                    errors += message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR;
                    warnings += message->Severity == D3D12_MESSAGE_SEVERITY_WARNING;
                }
            }
            printf("D3D12 debug: errors=%u warnings=%u\n", errors, warnings);
            success &= errors == 0;
        }
        manager->Shutdown();
        return success ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
