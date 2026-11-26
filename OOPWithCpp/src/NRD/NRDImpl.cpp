//
// Created by forwardfax3 on 19/11/2026.
//

#include "Core.hpp"
#include "Log.hpp"
#include "Renderer.hpp"
#include "NRDImpl.hpp"

#include "ImGuiHelpers.hpp"

#include <cmath>
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

    NRDImpl::NRDImpl(const Vec2u32 resolution, const bool enableSIGMA)
        : m_Resolution(resolution), m_EnabledSIGMA(enableSIGMA)
    {
        RecreateInstance();
    }

    void NRDImpl::Denoise(const std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>>& userImages, const std::span<const std::string_view> waitSemaphoreNames, const std::span<const std::string_view> startSemaphoreNames, const std::span<const std::string_view> waitPreviousFrameSemaphoreNames) const
    {
        const auto identifiers = m_DenoiserDescs | std::views::transform(&nrd::DenoiserDesc::identifier) | std::ranges::to<std::vector>();

        const nrd::DispatchDesc* dispatchDescsRawPtr{};
        u32 dispatchDescsNum{};
        if (const auto computeDispatchesResult = nrd::GetComputeDispatches(*m_Instance, identifiers.data(), identifiers.size(), dispatchDescsRawPtr, dispatchDescsNum);
            computeDispatchesResult != nrd::Result::SUCCESS)
            Log<LogLevel::Critical>("Failed to get NRD compute dispatches. Error: {}", nrdResultToString(computeDispatchesResult));

        m_Compute->NRDDenoiserPass(std::span{ dispatchDescsRawPtr, dispatchDescsNum },
            m_Shaders, m_UniformBuffer, m_PermanentTextures, m_TransientTextures,
            userImages, waitSemaphoreNames, startSemaphoreNames, waitPreviousFrameSemaphoreNames);
    }

    void NRDImpl::SetCommonSettings(const nrd::CommonSettings& commonSettings) const
    {
        if (const auto result = nrd::SetCommonSettings(*m_Instance, commonSettings); result != nrd::Result::SUCCESS)
            Log<LogLevel::Critical>("Failed to set NRD common settings. Error: {}", nrdResultToString(result));
    }

    void NRDImpl::SetSigma(const bool enableSIGMA)
    {
        m_EnabledSIGMA = enableSIGMA;
        RecreateInstance();
    }

    void NRDImpl::UpdateAccumulation(const f32 deltaTimeMs)
    {
        if (m_Instance == nullptr)
            return;

        if (deltaTimeMs > 0.0f)
            m_SmoothedFPS = std::lerp(m_SmoothedFPS, 1000.0f / deltaTimeMs, 0.05f); // exponential moving average

        const auto toFrames = [this](const f32 seconds)
        {
            return std::min(nrd::GetMaxAccumulatedFrameNum(seconds, m_SmoothedFPS), nrd::RELAX_MAX_HISTORY_FRAME_NUM);
        };

        const auto& [diffuse, specular, diffuseFast, specularFast] = m_RelaxAccumulationTime;
        const std::array newFrames = { toFrames(diffuse), toFrames(specular), toFrames(diffuseFast), toFrames(specularFast) };

        auto& r = m_RelaxSettings;
        if (const std::array oldFrames = { r.diffuseMaxAccumulatedFrameNum, r.specularMaxAccumulatedFrameNum, r.diffuseMaxFastAccumulatedFrameNum, r.specularMaxFastAccumulatedFrameNum };
            newFrames == oldFrames)
            return;

        std::tie(r.diffuseMaxAccumulatedFrameNum, r.specularMaxAccumulatedFrameNum, r.diffuseMaxFastAccumulatedFrameNum, r.specularMaxFastAccumulatedFrameNum)
            = std::tuple{ newFrames[0], newFrames[1], newFrames[2], newFrames[3] };
        nrd::SetDenoiserSettings(*m_Instance, static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE_SPECULAR), &m_RelaxSettings);
    }

    void NRDImpl::ImGUIRender()
    {
        [[maybe_unused]] static constexpr u32 zero = 0, one = 1, two = 2, three = 3, four = 4, five = 5,
            six = 6, seven = 7, eight = 8, nine = 9, ten = 10, sixtyFour = 64;

        static constexpr std::array diffuseSpecularNames = { "Diffuse", "Specular" };
        static constexpr std::array hitDistanceReconstructionModeNames = { "Off", "Area 3x3", "Area 5x5" };
        static_assert(hitDistanceReconstructionModeNames.size() == static_cast<size_t>(nrd::HitDistanceReconstructionMode::MAX_NUM));

        constexpr ImGuiSliderFlags clamp = ImGuiSliderFlags_ClampOnInput;
        constexpr ImGuiSliderFlags clampLog = ImGuiSliderFlags_ClampOnInput | ImGuiSliderFlags_Logarithmic;

        constexpr auto diffuseSpecularFloat = [](const char* label, float& diffuse, float& specular, const float min, const float max, const ImGuiSliderFlags flags = clamp)
        {
            std::array values = { diffuse, specular };
            const bool changed = ImGui::OWC::SliderFloat2WithAlignedText(label, diffuseSpecularNames[0], diffuseSpecularNames[1], values.data(), min, max, flags);
            std::tie(diffuse, specular) = values;
            return changed;
        };

        ImGui::Begin("NRD");

        if (ImGui::CollapsingHeader("Relax", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::PushID("Relax");
            bool relaxChanged = false;

            if (ImGui::TreeNode("Accumulation"))
            {
                // Frame counts are derived from these times in UpdateAccumulation
                auto& [diffuse, specular, diffuseFast, specularFast] = m_RelaxAccumulationTime;
                diffuseSpecularFloat("Accumulation Time (s)", diffuse, specular, 0.0f, 4.0f);
                ImGui::SetItemTooltip("Converted to frames using the smoothed FPS (max %u frames)", nrd::RELAX_MAX_HISTORY_FRAME_NUM);
                diffuseSpecularFloat("Fast Accumulation Time (s)", diffuseFast, specularFast, 0.0f, 1.0f);
                ImGui::SetItemTooltip("Usually 5x-7x shorter than Accumulation Time. Values >= Accumulation Time disable fast history");
                ImGui::Text("Smoothed FPS: %.1f", m_SmoothedFPS);
                ImGui::Text("Max Accumulated Frames: Diffuse %u, Specular %u", m_RelaxSettings.diffuseMaxAccumulatedFrameNum, m_RelaxSettings.specularMaxAccumulatedFrameNum);
                ImGui::Text("Max Fast Accumulated Frames: Diffuse %u, Specular %u", m_RelaxSettings.diffuseMaxFastAccumulatedFrameNum, m_RelaxSettings.specularMaxFastAccumulatedFrameNum);
                relaxChanged |= ImGui::SliderFloat("Fast History Clamping Sigma Scale", &m_RelaxSettings.fastHistoryClampingSigmaScale, 1.0f, 3.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderScalar("Spatial Variance Estimation History Threshold", ImGuiDataType_U32, &m_RelaxSettings.spatialVarianceEstimationHistoryThreshold, &zero, &ten, nullptr, clamp);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("History Fix"))
            {
                relaxChanged |= ImGui::SliderScalar("Frame Num", ImGuiDataType_U32, &m_RelaxSettings.historyFixFrameNum, &zero, &three, nullptr, clamp);
                relaxChanged |= ImGui::SliderScalar("Base Pixel Stride", ImGuiDataType_U32, &m_RelaxSettings.historyFixBasePixelStride, &one, &sixtyFour, nullptr, clamp);
                relaxChanged |= ImGui::SliderScalar("Alternate Pixel Stride", ImGuiDataType_U32, &m_RelaxSettings.historyFixAlternatePixelStride, &one, &sixtyFour, nullptr, clamp);
                relaxChanged |= ImGui::SliderFloat("Edge Stopping Normal Power", &m_RelaxSettings.historyFixEdgeStoppingNormalPower, 0.01f, 64.0f, "%.3f", clampLog);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Anti-Lag"))
            {
                relaxChanged |= ImGui::SliderFloat("Acceleration Amount", &m_RelaxSettings.antilagSettings.accelerationAmount, 0.0f, 1.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Spatial Sigma Scale", &m_RelaxSettings.antilagSettings.spatialSigmaScale, 0.01f, 10.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Temporal Sigma Scale", &m_RelaxSettings.antilagSettings.temporalSigmaScale, 0.01f, 10.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Reset Amount", &m_RelaxSettings.antilagSettings.resetAmount, 0.0f, 1.0f, "%.3f", clamp);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Spatial Filtering"))
            {
                relaxChanged |= diffuseSpecularFloat("Prepass Blur Radius", m_RelaxSettings.diffusePrepassBlurRadius, m_RelaxSettings.specularPrepassBlurRadius, 0.0f, 100.0f);
                relaxChanged |= ImGui::SliderScalar("A-Trous Iterations", ImGuiDataType_U32, &m_RelaxSettings.atrousIterationNum, &two, &eight, nullptr, clamp);
                relaxChanged |= diffuseSpecularFloat("Phi Luminance", m_RelaxSettings.diffusePhiLuminance, m_RelaxSettings.specularPhiLuminance, 0.0f, 10.0f);
                relaxChanged |= diffuseSpecularFloat("Min Luminance Weight", m_RelaxSettings.diffuseMinLuminanceWeight, m_RelaxSettings.specularMinLuminanceWeight, 0.0f, 1.0f);
                relaxChanged |= ImGui::SliderFloat("Min Hit Distance Weight", &m_RelaxSettings.minHitDistanceWeight, 0.001f, 0.2f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Depth Threshold", &m_RelaxSettings.depthThreshold, 0.0f, 0.1f, "%.4f", clampLog);
                relaxChanged |= ImGui::SliderFloat("Lobe Angle Fraction", &m_RelaxSettings.lobeAngleFraction, 0.0f, 1.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Roughness Fraction", &m_RelaxSettings.roughnessFraction, 0.0f, 1.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Specular Variance Boost", &m_RelaxSettings.specularVarianceBoost, 0.0f, 10.0f, "%.3f", clampLog);
                relaxChanged |= ImGui::SliderFloat("Specular Lobe Angle Slack", &m_RelaxSettings.specularLobeAngleSlack, 0.0f, 90.0f, "%.3f deg", clampLog);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Edge Stopping Relaxation"))
            {
                relaxChanged |= ImGui::SliderFloat("Luminance", &m_RelaxSettings.luminanceEdgeStoppingRelaxation, 0.0f, 1.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Normal", &m_RelaxSettings.normalEdgeStoppingRelaxation, 0.0f, 1.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Roughness", &m_RelaxSettings.roughnessEdgeStoppingRelaxation, 0.0f, 1.0f, "%.3f", clamp);
                ImGui::SeparatorText("Confidence Driven");
                relaxChanged |= ImGui::SliderFloat("Relaxation Multiplier", &m_RelaxSettings.confidenceDrivenRelaxationMultiplier, 0.0f, 10.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Luminance##Confidence", &m_RelaxSettings.confidenceDrivenLuminanceEdgeStoppingRelaxation, 0.0f, 1.0f, "%.3f", clamp);
                relaxChanged |= ImGui::SliderFloat("Normal##Confidence", &m_RelaxSettings.confidenceDrivenNormalEdgeStoppingRelaxation, 0.0f, 1.0f, "%.3f", clamp);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Misc"))
            {
                auto hitDistanceMode = static_cast<int>(m_RelaxSettings.hitDistanceReconstructionMode);
                if (ImGui::Combo("Hit Distance Reconstruction", &hitDistanceMode, hitDistanceReconstructionModeNames.data(), static_cast<int>(hitDistanceReconstructionModeNames.size())))
                {
                    m_RelaxSettings.hitDistanceReconstructionMode = static_cast<nrd::HitDistanceReconstructionMode>(hitDistanceMode);
                    relaxChanged = true;
                }
                ImGui::SetItemTooltip("Only needed with probabilistic diffuse/specular lobe selection");
                relaxChanged |= diffuseSpecularFloat("Min Material", m_RelaxSettings.minMaterialForDiffuse, m_RelaxSettings.minMaterialForSpecular, 0.0f, 4.0f);
                ImGui::TreePop();
            }

            relaxChanged |= ImGui::Checkbox("Anti-Firefly", &m_RelaxSettings.enableAntiFirefly);
            relaxChanged |= ImGui::Checkbox("Roughness Edge Stopping", &m_RelaxSettings.enableRoughnessEdgeStopping);

            if (ImGui::Button("Reset To Defaults"))
            {
                m_RelaxSettings = {};
                m_RelaxSettings.enableAntiFirefly = true;
                m_RelaxAccumulationTime = {};
                relaxChanged = true;
            }

            if (relaxChanged)
                nrd::SetDenoiserSettings(*m_Instance, static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE_SPECULAR), &m_RelaxSettings);
            ImGui::PopID();
        }

        if (m_EnabledSIGMA)
        {
            if (ImGui::CollapsingHeader("Sigma", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::PushID("Sigma");
                bool sigmaChanged = false;

                sigmaChanged |= ImGui::SliderScalar("Max Stabilized Frames", ImGuiDataType_U32, &m_SigmaSettings.maxStabilizedFrameNum, &zero, &nrd::SIGMA_MAX_HISTORY_FRAME_NUM, nullptr, clamp);
                ImGui::SetItemTooltip("0 disables the stabilization pass");
                sigmaChanged |= ImGui::SliderFloat("Plane Distance Sensitivity", &m_SigmaSettings.planeDistanceSensitivity, 0.0f, 1.0f, "%.4f", clampLog);

                ImGui::BeginDisabled();
                ImGui::InputFloat3("Light Direction", m_SigmaSettings.lightDirection, "%.3f", ImGuiInputTextFlags_ReadOnly);
                ImGui::EndDisabled();
                ImGui::SetItemTooltip("Driven by the sun direction");

                if (ImGui::Button("Reset To Defaults"))
                {
                    const auto lightDirection = std::to_array(m_SigmaSettings.lightDirection);
                    m_SigmaSettings = {};
                    std::ranges::copy(lightDirection, m_SigmaSettings.lightDirection);
                    sigmaChanged = true;
                }

                if (sigmaChanged)
                    nrd::SetDenoiserSettings(*m_Instance, static_cast<nrd::Identifier>(nrd::Denoiser::SIGMA_SHADOW), &m_SigmaSettings);
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

    void NRDImpl::RecreateInstance()
    {
        {
            m_DenoiserDescs.clear();
            m_DenoiserDescs.reserve(1ull + static_cast<uSize>(m_EnabledSIGMA));

            m_DenoiserDescs.emplace_back(
                static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE_SPECULAR),
                nrd::Denoiser::RELAX_DIFFUSE_SPECULAR
            );

            if (m_EnabledSIGMA)
            {
                m_DenoiserDescs.emplace_back(
                    static_cast<nrd::Identifier>(nrd::Denoiser::SIGMA_SHADOW),
                    nrd::Denoiser::SIGMA_SHADOW
                );
            }

            const nrd::InstanceCreationDesc desc{
                .allocationCallbacks = {},
                .denoisers = m_DenoiserDescs.data(),
                .denoisersNum = static_cast<u32>(m_DenoiserDescs.size()),
            };

            nrd::Instance* instance{};
            if (const auto result = nrd::CreateInstance(desc, instance); result != nrd::Result::SUCCESS)
                Log<LogLevel::Critical>("Failed to create NRD instance. Error: {}", nrdResultToString(result));
            m_Instance.reset(instance);
        }

        const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_Instance);
        if (m_Shaders)
            Graphics::Renderer::AddToEndOfFrameCleanUp([pShaders = std::move(m_Shaders)](){});
        m_Shaders = BaseNRDShader::CreateNRDShader(instanceDesc);
        const auto deviceLimits = VulkanCore::GetInstance().GetPhysicalDeviceProperties().limits;
        const auto alignedConstantBufferSize = alignUp(instanceDesc.constantBufferMaxDataSize, deviceLimits.minUniformBufferOffsetAlignment);
        const auto totalPerFrameBufferSize = alignedConstantBufferSize * instanceDesc.descriptorPoolDesc.setsMaxNum;
        const auto totalBufferSize = totalPerFrameBufferSize * Renderer::GetNumberOfFramesInFlight();
        if (m_UniformBuffer)
            Graphics::Renderer::AddToEndOfFrameCleanUp([pBuffer = std::move(m_UniformBuffer)](){});
        m_UniformBuffer = HostVisibleUniformBuffer::CreateHostVisibleBuffer(totalBufferSize);
        if (m_Compute)
            Graphics::Renderer::AddToEndOfFrameCleanUp([pCompute = std::move(m_Compute)](){});
        m_Compute = Compute::BaseNRDCompute::CreateNRDComputePass();
        m_Shaders->BindHostVisibleUniformBuffer(instanceDesc, m_UniformBuffer);

        if (!m_PermanentTextures.empty())
            Graphics::Renderer::AddToEndOfFrameCleanUp([pTextures = std::move(m_PermanentTextures)](){});

        m_PermanentTextures.reserve(instanceDesc.permanentPoolSize);
        for (const auto permanentTexture = std::span(instanceDesc.permanentPool, instanceDesc.permanentPoolSize);
            const auto& [format, u16DownSampleFactor] : permanentTexture)
        {
            const auto downSampleFactor = static_cast<u32>(u16DownSampleFactor);
            const auto scaledResolution = (m_Resolution + (downSampleFactor - 1)) / downSampleFactor;
            m_PermanentTextures.emplace_back(TextureImage::CreateStorageImage(scaledResolution.x, scaledResolution.y, ConvertNRDFormatToFormat(format), AccessMask::ShaderRead | AccessMask::ShaderWrite, StageMask::ComputeShader, ImageLayout::General));
        }


        if (!m_TransientTextures.empty())
            Graphics::Renderer::AddToEndOfFrameCleanUp([pTextures = std::move(m_TransientTextures)](){});

        m_TransientTextures.reserve(instanceDesc.transientPoolSize);
        for (const auto transient = std::span(instanceDesc.transientPool, instanceDesc.transientPoolSize);
            const auto& [format, u16DownSampleFactor] : transient)
        {
            const auto downSampleFactor = static_cast<u32>(u16DownSampleFactor);
            const auto scaledResolution = (m_Resolution + (downSampleFactor - 1)) / downSampleFactor;
            m_TransientTextures.emplace_back(TextureImage::CreateStorageImage(scaledResolution.x, scaledResolution.y, ConvertNRDFormatToFormat(format), AccessMask::ShaderRead | AccessMask::ShaderWrite, StageMask::ComputeShader, ImageLayout::General));
        }

        m_RelaxSettings.enableAntiFirefly = true;
        nrd::SetDenoiserSettings(*m_Instance, static_cast<nrd::Identifier>(nrd::Denoiser::RELAX_DIFFUSE_SPECULAR), &m_RelaxSettings);
        if (m_EnabledSIGMA)
            nrd::SetDenoiserSettings(*m_Instance, static_cast<nrd::Identifier>(nrd::Denoiser::SIGMA_SHADOW), &m_SigmaSettings);
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