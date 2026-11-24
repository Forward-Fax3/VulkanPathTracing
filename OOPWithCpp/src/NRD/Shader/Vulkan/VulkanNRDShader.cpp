//
// Created by forwardfax3 on 19/11/2026.
//

#include "VulkanNRDShader.hpp"
#include "VulkanBuffers.hpp"

#include "VulkanCore.hpp"

namespace OWC::Graphics
{
    VulkanNRDShader::VulkanNRDShader(const nrd::InstanceDesc& instanceDesc)
        : m_ConstBufferSetIndex(instanceDesc.constantBufferAndSamplersSpaceIndex)
    {
        if (instanceDesc.resourcesSpaceIndex >= 2 || instanceDesc.constantBufferAndSamplersSpaceIndex >= 2 || instanceDesc.resourcesSpaceIndex == instanceDesc.constantBufferAndSamplersSpaceIndex)
            Log<LogLevel::Critical>("Invalid resources space index for VulkanNRDShader.");

        CreateSamplers(instanceDesc);

        const auto& vkCore = VulkanCore::GetInstance();
        const auto& device = vkCore.GetDevice();
        const auto& nrdLibDesc = *nrd::GetLibraryDesc();

        m_ConstBufferStride = alignUp(instanceDesc.constantBufferMaxDataSize, static_cast<u32>(vkCore.GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment));
        m_NumOfConstBuffersPerFrame = instanceDesc.descriptorPoolDesc.setsMaxNum;

        m_TexBase = nrdLibDesc.spirvBindingOffsets.textureOffset + instanceDesc.resourcesBaseRegisterIndex;
        m_UavBase = nrdLibDesc.spirvBindingOffsets.storageTextureAndBufferOffset + instanceDesc.resourcesBaseRegisterIndex;

        const u32 samplerBase = nrdLibDesc.spirvBindingOffsets.samplerOffset + instanceDesc.samplersBaseRegisterIndex;
        const u32 cbBinding = nrdLibDesc.spirvBindingOffsets.constantBufferOffset + instanceDesc.constantBufferRegisterIndex;
        const auto& pool = instanceDesc.descriptorPoolDesc;

        std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;
        layoutBindings.reserve(pool.perSetTexturesMaxNum + pool.perSetStorageTexturesMaxNum);

        for (decltype(pool.perSetTexturesMaxNum) i = 0; i < pool.perSetTexturesMaxNum; i++)
            layoutBindings.emplace_back(m_TexBase + i, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eCompute);
        for (decltype(pool.perSetStorageTexturesMaxNum) i = 0; i < pool.perSetStorageTexturesMaxNum; i++)
            layoutBindings.emplace_back(m_UavBase + i, vk::DescriptorType::eStorageImage, 1u, vk::ShaderStageFlagBits::eCompute);

        std::vector<vk::DescriptorBindingFlags> bindingFlags(layoutBindings.size(), vk::DescriptorBindingFlagBits::ePartiallyBound);
        const auto bindingFlagsInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo{}.setBindingFlags(bindingFlags);

        auto bindingLayoutResult = device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo{}
            .setBindings(layoutBindings)
            .setFlags(vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor)
            .setPNext(&bindingFlagsInfo));
        if (!bindingLayoutResult)
            Log<LogLevel::Critical>("Failed to create descriptor set layout. Error: {}", vk::to_string(bindingLayoutResult.error()));

        m_DescriptorSetLayouts[instanceDesc.resourcesSpaceIndex] = std::move(bindingLayoutResult).value();

        std::vector<vk::DescriptorSetLayoutBinding> rootBindings;
        rootBindings.reserve(instanceDesc.samplersNum + 1); // samplers + constant buffer

        for (u32 i = 0; i < instanceDesc.samplersNum; i++)
            rootBindings.emplace_back(samplerBase + i, vk::DescriptorType::eSampler, 1u, vk::ShaderStageFlagBits::eCompute, &*m_Samplers[i]);

        rootBindings.emplace_back(cbBinding, vk::DescriptorType::eUniformBufferDynamic, 1u, vk::ShaderStageFlagBits::eCompute);

        auto rootLayoutResult = device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{}.setBindings(rootBindings));
        if (!rootLayoutResult)
            Log<LogLevel::Critical>("Failed to create root descriptor set layout. Error: {}", vk::to_string(rootLayoutResult.error()));

        m_DescriptorSetLayouts[instanceDesc.constantBufferAndSamplersSpaceIndex] = std::move(rootLayoutResult).value();

        std::array<vk::DescriptorSetLayout, 2> setLayouts{};
        setLayouts[0] = *m_DescriptorSetLayouts[0];
        setLayouts[1] = *m_DescriptorSetLayouts[1];

        auto pipelineLayoutResult = device.createPipelineLayout(vk::PipelineLayoutCreateInfo{}.setSetLayouts(setLayouts));
        if (!pipelineLayoutResult)
            Log<LogLevel::Critical>("Failed to create pipeline layout. Error: {}", vk::to_string(pipelineLayoutResult.error()));

        m_PipelineLayout = std::move(pipelineLayoutResult).value();

        std::vector<vk::ShaderModuleCreateInfo> shaderModuleCreateInfos;
        shaderModuleCreateInfos.reserve(instanceDesc.pipelinesNum);

        std::vector<vk::ComputePipelineCreateInfo> computePipelineCreateInfos;
        computePipelineCreateInfos.reserve(instanceDesc.pipelinesNum);

        for (decltype(instanceDesc.pipelinesNum) i = 0; i < instanceDesc.pipelinesNum; i++)
        {
            const auto& pipeline = instanceDesc.pipelines[i];

            if (!pipeline.computeShaderSPIRV.bytecode || pipeline.computeShaderSPIRV.size == 0 || pipeline.computeShaderSPIRV.size % 4 != 0)
            {
                Log<LogLevel::Error>("NRD pipeline {} has no/invalid SPIR-V", i);
                continue;
            }

            using spirvPtrType = decltype(vk::ShaderModuleCreateInfo::pCode);
            using spirvPtrSizeType = decltype(vk::ShaderModuleCreateInfo::codeSize);
            shaderModuleCreateInfos.emplace_back(vk::ShaderModuleCreateFlags{}, static_cast<spirvPtrSizeType>(pipeline.computeShaderSPIRV.size), static_cast<spirvPtrType>(pipeline.computeShaderSPIRV.bytecode));

            const auto shaderStageCreateInfo = vk::PipelineShaderStageCreateInfo{}
                .setPName(instanceDesc.shaderEntryPoint)
                .setPNext(&shaderModuleCreateInfos.back())
                .setStage(vk::ShaderStageFlagBits::eCompute);

            computePipelineCreateInfos.emplace_back(vk::PipelineCreateFlags{}, shaderStageCreateInfo, *m_PipelineLayout);
        }

        auto pipelinesResult = device.createComputePipelines(nullptr, computePipelineCreateInfos);
        if (!pipelinesResult)
            Log<LogLevel::Critical>("Failed to create Vulkan NRD pipelines. Error: {}", vk::to_string(pipelinesResult.error()));
        else
            Log<LogLevel::Trace>("Successfully created Vulkan NRD pipelines.");

        m_Pipelines = std::move(pipelinesResult).value();

        CreateConstantBufferSet(instanceDesc);
    }

    void VulkanNRDShader::BindHostVisibleUniformBuffer(const nrd::InstanceDesc& instanceDesc, const std::shared_ptr<const Graphics::HostVisibleUniformBuffer>& constantBuffer)
    {
        const auto& vulkanConstantBuffer = std::dynamic_pointer_cast<const Graphics::VulkanHostVisibleUniformBuffer>(constantBuffer);

        const auto& vkCore = VulkanCore::GetInstance();
        const auto& device = vkCore.GetDevice();

        // Point it at the ring buffer once. The range is one slot; the dynamic offset picks which slot.
        const auto bufferInfo = vk::DescriptorBufferInfo{}
        .setBuffer(vulkanConstantBuffer->GetBuffer()) // your vk::Buffer getter
        .setOffset(0)
        .setRange(instanceDesc.constantBufferMaxDataSize);

        device.updateDescriptorSets(vk::WriteDescriptorSet{}
            .setDstSet(*m_ConstantBufferSet)
            .setDstBinding(instanceDesc.constantBufferRegisterIndex + nrd::GetLibraryDesc()->spirvBindingOffsets.constantBufferOffset)
            .setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)
            .setBufferInfo(bufferInfo), {});
    }

    void VulkanNRDShader::CreateSamplers(const nrd::InstanceDesc& instanceDesc)
    {
        if (instanceDesc.samplersNum == 0)
        {
            Log<LogLevel::Trace>("Skipping creation of Vulkan NRD samplers.");
            return;
        }
        if (instanceDesc.samplersNum > 2)
        {
            Log<LogLevel::Critical>("Vulkan NRD supports up to 2 samplers.");
            return;
        }

        const auto& device = VulkanCore::GetInstance().GetDevice();

        m_Samplers.reserve(instanceDesc.samplersNum);

        static constexpr auto convertToVKFilter = [](const nrd::Sampler sampler)
        {
            return sampler == nrd::Sampler::NEAREST_CLAMP ? vk::Filter::eNearest : vk::Filter::eLinear;
        };

        [[assume(instanceDesc.samplersNum <= 2)]];

        for (decltype(instanceDesc.samplersNum) i = 0; i < instanceDesc.samplersNum; i++)
        {
            const auto& sampler = instanceDesc.samplers[i];

            auto samplerResult = device.createSampler(vk::SamplerCreateInfo()
                .setMagFilter(convertToVKFilter(sampler))
                .setMinFilter(convertToVKFilter(sampler))
                .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                .setAnisotropyEnable(vk::False)
                .setMaxAnisotropy(1.0f)
                .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
                .setUnnormalizedCoordinates(vk::False)
                .setCompareEnable(vk::False)
                .setCompareOp(vk::CompareOp::eAlways)
                .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                .setMipLodBias(0.0f)
                .setMinLod(0.0f)
                .setMaxLod(0.0f));

            if (!samplerResult)
                Log<LogLevel::Critical>("Failed to create sampler. Error: {}", vk::to_string(samplerResult.error()));

            m_Samplers.emplace_back(std::move(samplerResult).value());
        }
    }

    void VulkanNRDShader::CreateConstantBufferSet(const nrd::InstanceDesc& instanceDesc)
    {
        const auto& vkCore = VulkanCore::GetInstance();
        const auto& device = vkCore.GetDevice();

        std::vector<vk::DescriptorPoolSize> poolSizes{ { vk::DescriptorType::eUniformBufferDynamic, 1 } };
        if (instanceDesc.samplersNum > 0)
            poolSizes.emplace_back(vk::DescriptorType::eSampler, instanceDesc.samplersNum);

        auto poolResult = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{}
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
            .setMaxSets(1)
            .setPoolSizes(poolSizes));
        if (!poolResult)
            Log<LogLevel::Critical>("Failed to create NRD descriptor pool. Error: {}", vk::to_string(poolResult.error()));
        m_DescriptorPool = std::move(poolResult).value();

        const vk::DescriptorSetLayout cbLayout = *m_DescriptorSetLayouts[m_ConstBufferSetIndex];
        auto setsResult = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{}
            .setDescriptorPool(*m_DescriptorPool)
            .setSetLayouts(cbLayout));
        if (!setsResult)
            Log<LogLevel::Critical>("Failed to allocate NRD constant buffer set. Error: {}", vk::to_string(setsResult.error()));
        m_ConstantBufferSet = std::move(setsResult.value().front());
    }
} // namespace OWC::Graphics