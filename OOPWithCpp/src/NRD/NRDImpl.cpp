//
// Created by forwardfax3 on 19/11/2026.
//

#include "Core.hpp"
#include "Log.hpp"
#include "Renderer.hpp"
#include "NRDImpl.hpp"

#include <ranges>

#include "VulkanCore.hpp"


namespace OWC
{
    using namespace Graphics;

    static std::string_view nrdResultToString(const nrd::Result r) noexcept
    {
        switch (r)
        {
        case nrd::Result::SUCCESS:
            return "SUCCESS";
        case nrd::Result::FAILURE:
            return "FAILURE";
        case nrd::Result::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case nrd::Result::UNSUPPORTED:
            return "UNSUPPORTED";
        case nrd::Result::NON_UNIQUE_IDENTIFIER:
            return "NON_UNIQUE_IDENTIFIER";
        case nrd::Result::MAX_NUM:
        default:
            return "UNKNOWN";
        }
    }

    NRDImpl::NRDImpl(const std::span<const nrd::DenoiserDesc> denoiserDescs, const Vec2u32 resolution)
    {
        {
            const nrd::InstanceCreationDesc desc{
                .allocationCallbacks = {},
                .denoisers = denoiserDescs.data(),
                .denoisersNum = static_cast<u32>(denoiserDescs.size()),
            };

            nrd::Instance* instance{};
            if (const auto result = nrd::CreateInstance(desc, instance); result != nrd::Result::SUCCESS)
                Log<LogLevel::Critical>("Failed to create NRD instance. Error: {}", nrdResultToString(result));
            m_Instance.reset(instance);
        }

        const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_Instance);
        m_Shaders = BaseNRDShader::CreateNRDShader(instanceDesc);
        const auto deviceLimits = VulkanCore::GetInstance().GetPhysicalDeviceProperties().limits;
        const auto alignedConstantBufferSize = alignUp(instanceDesc.constantBufferMaxDataSize, deviceLimits.minUniformBufferOffsetAlignment);
        const auto totalPerFrameBufferSize = alignedConstantBufferSize * instanceDesc.descriptorPoolDesc.setsMaxNum;
        const auto totalBufferSize = totalPerFrameBufferSize * Renderer::GetNumberOfFramesInFlight();
        m_UniformBuffer = HostVisibleUniformBuffer::CreateHostVisibleBuffer(totalBufferSize);
        m_Compute = Compute::BaseNRDCompute::CreateNRDComputePass();
        m_Shaders->BindHostVisibleUniformBuffer(instanceDesc, m_UniformBuffer);

        m_PermanentTextures.reserve(instanceDesc.permanentPoolSize);
        for (const auto permanentTexture = std::span(instanceDesc.permanentPool, instanceDesc.permanentPoolSize);
            const auto& [format, u16DownSampleFactor] : permanentTexture)
        {
            const auto downSampleFactor = static_cast<u32>(u16DownSampleFactor);
            const auto scaledResolution = (resolution + (downSampleFactor - 1)) / downSampleFactor;
            m_PermanentTextures.emplace_back(TextureImage::CreateStorageImage(scaledResolution.x, scaledResolution.y, ConvertNRDFormatToFormat(format), AccessMask::ShaderRead | AccessMask::ShaderWrite, StageMask::ComputeShader, ImageLayout::General));
        }

        for (const auto transient = std::span(instanceDesc.transientPool, instanceDesc.transientPoolSize);
            const auto& [format, u16DownSampleFactor] : transient)
        {
            const auto downSampleFactor = static_cast<u32>(u16DownSampleFactor);
            const auto scaledResolution = (resolution + (downSampleFactor - 1)) / downSampleFactor;
            m_TransientTextures.emplace_back(TextureImage::CreateStorageImage(scaledResolution.x, scaledResolution.y, ConvertNRDFormatToFormat(format), AccessMask::ShaderRead | AccessMask::ShaderWrite, StageMask::ComputeShader, ImageLayout::General));
        }
    }

    void NRDImpl::Denoise(const std::span<const nrd::Identifier> identifiers, const std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>>& userImages, const std::span<const std::string_view> waitSemaphoreNames, const std::span<const std::string_view> startSemaphoreNames, const std::span<const std::string_view> waitPreviousFrameSemaphoreNames) const
    {
        if (m_Instance == nullptr || m_Shaders == nullptr || identifiers.empty())
            return;

        const nrd::DispatchDesc* dispatchDescsRawPtr{};
        u32 dispatchDescsNum{};
        if (const auto computeDispatchesResult = nrd::GetComputeDispatches(*m_Instance, identifiers.data(), identifiers.size(), dispatchDescsRawPtr, dispatchDescsNum); computeDispatchesResult != nrd::Result::SUCCESS)
            Log<LogLevel::Critical>("Failed to get NRD compute dispatches. Error: {}", nrdResultToString(computeDispatchesResult));

        m_Compute->NRDDenoiserPass(std::span{ dispatchDescsRawPtr, dispatchDescsNum },
            m_Shaders, m_UniformBuffer, m_PermanentTextures, m_TransientTextures,
            userImages, waitSemaphoreNames, startSemaphoreNames, waitPreviousFrameSemaphoreNames);
    }

    void NRDImpl::Denoise(const std::span<const nrd::DenoiserDesc> denoiserDescs, const std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>>& userImages, const std::span<const std::string_view> waitSemaphoreNames, const std::span<const std::string_view> startSemaphoreNames, const std::span<const std::string_view> waitPreviousFrameSemaphoreNames) const
    {
        const auto identifiers = denoiserDescs | std::views::transform(&nrd::DenoiserDesc::identifier) | std::ranges::to<std::vector>();
        Denoise(identifiers, userImages, waitSemaphoreNames, startSemaphoreNames, waitPreviousFrameSemaphoreNames);
    }

    void NRDImpl::SetCommonSettings(const nrd::CommonSettings& commonSettings) const
    {
        if (const auto result = nrd::SetCommonSettings(*m_Instance, commonSettings); result != nrd::Result::SUCCESS)
            Log<LogLevel::Critical>("Failed to set NRD common settings. Error: {}", nrdResultToString(result));
    }

    void NRDImpl::SetDenoiserSettings(const nrd::Identifier identifier, const void* denoiserSettings) const
    {
        if (const auto result = nrd::SetDenoiserSettings(*m_Instance, identifier, denoiserSettings); result != nrd::Result::SUCCESS)
            Log<LogLevel::Critical>("Failed to set NRD Denoiser settings. Error: {}", nrdResultToString(result));
    }

    Format NRDImpl::ConvertNRDFormatToFormat(const nrd::Format format) noexcept
    {
        switch (format)
        {
        case nrd::Format::R8_UNORM:
            return Format::R8_UNorm;
        case nrd::Format::R8_SNORM:
            return Format::R8_SNorm;
        case nrd::Format::R8_UINT:
            return Format::R8_Uint;
        case nrd::Format::R8_SINT:
            return Format::R8_Sint;
        case nrd::Format::RG8_UNORM:
            return Format::R8G8_UNorm;
        case nrd::Format::RG8_SNORM:
            return Format::R8G8_SNorm;
        case nrd::Format::RG8_UINT:
            return Format::R8G8_Uint;
        case nrd::Format::RG8_SINT:
            return Format::R8G8_Sint;
        case nrd::Format::RGBA8_UNORM:
            return Format::R8G8B8A8_UNorm;
        case nrd::Format::RGBA8_SNORM:
            return Format::R8G8B8A8_SNorm;
        case nrd::Format::RGBA8_UINT:
            return Format::R8G8B8A8_Uint;
        case nrd::Format::RGBA8_SINT:
            return Format::R8G8B8A8_Sint;
        case nrd::Format::RGBA8_SRGB:
            return Format::R8G8B8A8_Srgb;
        case nrd::Format::R16_UNORM:
            return Format::R16_UNorm;
        case nrd::Format::R16_SNORM:
            return Format::R16_SNorm;
        case nrd::Format::R16_UINT:
            return Format::R16_Uint;
        case nrd::Format::R16_SINT:
            return Format::R16_Sint;
        case nrd::Format::R16_SFLOAT:
            return Format::R16_SFloat;
        case nrd::Format::RG16_UNORM:
            return Format::R16G16_UNorm;
        case nrd::Format::RG16_SNORM:
            return Format::R16G16_SNorm;
        case nrd::Format::RG16_UINT:
            return Format::R16G16_Uint;
        case nrd::Format::RG16_SINT:
            return Format::R16G16_Sint;
        case nrd::Format::RG16_SFLOAT:
            return Format::R16G16_SFloat;
        case nrd::Format::RGBA16_UNORM:
            return Format::R16G16B16A16_UNorm;
        case nrd::Format::RGBA16_SNORM:
            return Format::R16G16B16A16_SNorm;
        case nrd::Format::RGBA16_UINT:
            return Format::R16G16B16A16_Uint;
        case nrd::Format::RGBA16_SINT:
            return Format::R16G16B16A16_Sint;
        case nrd::Format::RGBA16_SFLOAT:
            return Format::R16G16B16A16_SFloat;
        case nrd::Format::R32_UINT:
            return Format::R32_Uint;
        case nrd::Format::R32_SINT:
            return Format::R32_Sint;
        case nrd::Format::R32_SFLOAT:
            return Format::R32_SFloat;
        case nrd::Format::RG32_UINT:
            return Format::R32G32_Uint;
        case nrd::Format::RG32_SINT:
            return Format::R32G32_Sint;
        case nrd::Format::RG32_SFLOAT:
            return Format::R32G32_SFloat;
        case nrd::Format::RGB32_UINT:
            return Format::R32G32B32_Uint;
        case nrd::Format::RGB32_SINT:
            return Format::R32G32B32_Sint;
        case nrd::Format::RGB32_SFLOAT:
            return Format::R32G32B32_SFloat;
        case nrd::Format::RGBA32_UINT:
            return Format::R32G32B32A32_Uint;
        case nrd::Format::RGBA32_SINT:
            return Format::R32G32B32A32_Sint;
        case nrd::Format::RGBA32_SFLOAT:
            return Format::R32G32B32A32_SFloat;
        case nrd::Format::R10_G10_B10_A2_UNORM:
            return Format::R10G10B10A2_UNorm;
        case nrd::Format::R10_G10_B10_A2_UINT:
            return Format::R10G10B10A2_Uint;
        case nrd::Format::R11_G11_B10_UFLOAT:
            return Format::R11G11B10_UFloat;
        case nrd::Format::R9_G9_B9_E5_UFLOAT:
            return Format::R9G9B9E5_UFloat;
        case nrd::Format::MAX_NUM:
        default:
            return Format::Undefined;
        }
    }
} // OWC