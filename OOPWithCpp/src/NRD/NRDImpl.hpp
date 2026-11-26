//
// Created by forwardfax3 on 19/11/2026.
//

#pragma once
#include "Core.hpp"

#include "BaseNRDShader.hpp"
#include "BaseNRDCompute.hpp"
#include "GPUBuffers.hpp"
#include "RendererCore.hpp"

#include "NRD.h"


namespace OWC
{


    class NRDImpl
    {
        struct NRDInstanceDeleter
        {
            void operator()(nrd::Instance* i) const { nrd::DestroyInstance(*i); }
        };

        using NRDInstancePtr = std::unique_ptr<nrd::Instance, NRDInstanceDeleter>;

    public:
        NRDImpl() = delete;
        explicit NRDImpl(Vec2u32 resolution, bool enableSIGMA);
        ~NRDImpl() = default;

        void Denoise(const std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>>& userImages, std::span<const std::string_view> waitSemaphoreNames = {}, std::span<const std::string_view> startSemaphoreNames = {}, std::span<const std::string_view> waitPreviousFrameSemaphoreNames = {}) const;

        void SetCommonSettings(const nrd::CommonSettings& commonSettings) const;
        OWC_FORCE_INLINE void SetSigmaLightDirection(const Vec3p& sigmaLightDirection)
        {
            if (m_EnabledSIGMA)
            {
                std::memcpy(&m_SigmaSettings.lightDirection, &sigmaLightDirection, std::min(sizeof(Vec3p), sizeof(m_SigmaSettings.lightDirection)));
                nrd::SetDenoiserSettings(*m_Instance, static_cast<nrd::Identifier>(nrd::Denoiser::SIGMA_SHADOW), &m_SigmaSettings);
            }
            else
                Log<LogLevel::Warn>("SIGMA is disabled, cannot set light direction");
        }

        void SetSigma(bool enableSIGMA);

        // Converts accumulation times (seconds) into frame counts using a smoothed FPS; call once per frame
        void UpdateAccumulation(f32 deltaTimeMs);

        void ImGUIRender();

    private:
        void RecreateInstance();

    private:
        // NRD recommends accumulating in "seconds" not "frames" (see nrd::GetMaxAccumulatedFrameNum)
        struct RelaxAccumulationTime
        {
            f32 diffuse = 0.5f;
            f32 specular = 0.5f;
            f32 diffuseFast = 0.1f;
            f32 specularFast = 0.1f;
        };

        [[nodiscard]] static Graphics::Format ConvertNRDFormatToFormat(nrd::Format format) noexcept;

    private:
        NRDInstancePtr m_Instance{};
        std::shared_ptr<Compute::BaseNRDCompute> m_Compute{};
        std::shared_ptr<Graphics::BaseNRDShader> m_Shaders{};
        std::shared_ptr<Graphics::HostVisibleUniformBuffer> m_UniformBuffer{};
        std::vector<std::shared_ptr<Graphics::TextureImage>> m_PermanentTextures{};
        std::vector<std::shared_ptr<Graphics::TextureImage>> m_TransientTextures{};

        std::vector<nrd::DenoiserDesc> m_DenoiserDescs{};

        nrd::RelaxSettings m_RelaxSettings{};
        nrd::SigmaSettings m_SigmaSettings{};

        f32 m_SmoothedFPS = 60.0f;
        RelaxAccumulationTime m_RelaxAccumulationTime{};
        Vec2u32 m_Resolution{};

        bool m_EnabledSIGMA = false;
    };
} // OWC
