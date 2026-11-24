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
        explicit NRDImpl(std::span<const nrd::DenoiserDesc> denoiserDescs, Vec2u32 resolution);
        ~NRDImpl() = default;

        void Denoise(std::span<const nrd::Identifier>, const std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>>& userImages, std::span<const std::string_view> waitSemaphoreNames = {}, std::span<const std::string_view> startSemaphoreNames = {}, std::span<const std::string_view> waitPreviousFrameSemaphoreNames = {}) const;
        void Denoise(std::span<const nrd::DenoiserDesc>, const std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>>& userImages, std::span<const std::string_view> waitSemaphoreNames = {}, std::span<const std::string_view> startSemaphoreNames = {}, std::span<const std::string_view> waitPreviousFrameSemaphoreNames = {}) const;

        void SetCommonSettings(const nrd::CommonSettings& commonSettings) const;
        void SetDenoiserSettings(nrd::Identifier identifier, const void* denoiserSettings) const;

    private:
        [[nodiscard]] static Graphics::Format ConvertNRDFormatToFormat(nrd::Format format) noexcept;

    private:
        NRDInstancePtr m_Instance{};
        std::shared_ptr<Compute::BaseNRDCompute> m_Compute{};
        std::shared_ptr<Graphics::BaseNRDShader> m_Shaders{};
        std::shared_ptr<Graphics::HostVisibleUniformBuffer> m_UniformBuffer{};
        std::vector<std::shared_ptr<Graphics::TextureImage>> m_PermanentTextures{};
        std::vector<std::shared_ptr<Graphics::TextureImage>> m_TransientTextures{};
    };
} // OWC
