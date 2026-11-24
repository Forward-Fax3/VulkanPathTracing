//
// Created by forwardfax3 on 19/11/2026.
//

#pragma once
#include "Core.hpp"

#include "../BaseNRDShader.hpp"

#include <expected>
#include <vector>

#include <vulkan/vulkan_raii.hpp>


namespace OWC::Graphics
{
    class VulkanNRDShader final : public BaseNRDShader
    {
    public:
        VulkanNRDShader() = delete;
        explicit VulkanNRDShader(const nrd::InstanceDesc& instanceDesc);
        ~VulkanNRDShader() override = default;

        void BindHostVisibleUniformBuffer(const nrd::InstanceDesc& instanceDesc, const std::shared_ptr<const Graphics::HostVisibleUniformBuffer>& constantBuffer) override;

        [[nodiscard]] OWC_FORCE_INLINE vk::Pipeline GetPipeline(const nrd::DispatchDesc& dispatchDesc) const { return *m_Pipelines[dispatchDesc.pipelineIndex]; }
        [[nodiscard]] OWC_FORCE_INLINE vk::PipelineLayout GetPipelineLayout() const { return *m_PipelineLayout; }
        [[nodiscard]] OWC_FORCE_INLINE vk::DescriptorSet GetDescriptorSet() const { return *m_ConstantBufferSet; }
        [[nodiscard]] OWC_FORCE_INLINE u32 GetResourcesSetIndex() const { return m_ResourcesSetIndex; }
        [[nodiscard]] OWC_FORCE_INLINE u32 GetConstBufferSetIndex() const { return m_ConstBufferSetIndex; }

        [[nodiscard]] OWC_FORCE_INLINE u32 GetTexBase() const { return m_TexBase; }
        [[nodiscard]] OWC_FORCE_INLINE u32 GetUavBase() const { return m_UavBase; }
        [[nodiscard]] OWC_FORCE_INLINE u32 GetConstBufferStride() const { return m_ConstBufferStride; }
        [[nodiscard]] OWC_FORCE_INLINE u32 GetNumOfConstBuffersPerFrame() const { return m_NumOfConstBuffersPerFrame; }

    private:
        void CreateSamplers(const nrd::InstanceDesc& instanceDesc);
        void CreateConstantBufferSet(const nrd::InstanceDesc& instanceDesc);

    private:
        std::vector<vk::raii::Pipeline> m_Pipelines;
        vk::raii::PipelineLayout m_PipelineLayout = nullptr;
        std::array<vk::raii::DescriptorSetLayout, 2> m_DescriptorSetLayouts = { nullptr, nullptr };
        std::vector<vk::raii::Sampler> m_Samplers;
        vk::raii::DescriptorPool m_DescriptorPool = nullptr;
        vk::raii::DescriptorSet m_ConstantBufferSet = nullptr;
        u32 m_ResourcesSetIndex = 0;
        u32 m_ConstBufferSetIndex = 0;

        u32 m_TexBase = 0; // TODO: put this somewhere better
        u32 m_UavBase = 0; // TODO: put this somewhere better
        u32 m_ConstBufferStride = 0; // TODO: put this somewhere better
        u32 m_NumOfConstBuffersPerFrame = 0; // TODO: put this somewhere better
    };
} // OWC
