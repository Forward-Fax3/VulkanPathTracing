//
// Created by forwardfax3 on 21/11/2026.
//

#pragma once
#include "BaseNRDCompute.hpp"

#include <vulkan/vulkan_raii.hpp>


namespace OWC::Compute
{
    class VulkanNRDCompute : public BaseNRDCompute
    {
    public:
        VulkanNRDCompute();
        ~VulkanNRDCompute() override = default;

        void NRDDenoiserPass(const std::span<const nrd::DispatchDesc>& dispatchDescs, const std::shared_ptr<const Graphics::BaseNRDShader>& shaders, const std::shared_ptr<Graphics::HostVisibleUniformBuffer>& constantUniformBuffer, const std::vector<std::shared_ptr<Graphics::TextureImage>>& textureImages, const std::vector<std::shared_ptr<Graphics::TextureImage>>& transientTextureImages, const std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>>& userImages, const std::span<const std::string_view>& waitSemaphoreNames, const std::span<const std::string_view>& startSemaphoreNames, const std::span<const std::string_view>& waitPreviousFrameSemaphoreNames) override;

    private:
        vk::raii::CommandBuffers m_CommandBuffers = nullptr;
    };
} // OWC
