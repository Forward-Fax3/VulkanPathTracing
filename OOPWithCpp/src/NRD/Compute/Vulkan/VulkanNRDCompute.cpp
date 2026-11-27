//
// Created by forwardfax3 on 21/11/2026.
//

#include "Core.hpp"
#include "Log.hpp"
#include "VulkanCore.hpp"
#include "VulkanBuffers.hpp"
#include "VulkanNRDShader.hpp"
#include "VulkanNRDCompute.hpp"

#include <utility>


namespace OWC::Compute
{
    using namespace Graphics;
    VulkanNRDCompute::VulkanNRDCompute()
        : m_CommandBuffers(VulkanCore::GetInstance().GetDynamicComputeCommandBuffer())
    {}

    void VulkanNRDCompute::NRDDenoiserPass(const std::span<const nrd::DispatchDesc>& dispatchDescs, const std::shared_ptr<const Graphics::BaseNRDShader>& shaders, const std::shared_ptr<Graphics::HostVisibleUniformBuffer>& constantUniformBuffer, const std::vector<std::shared_ptr<Graphics::TextureImage>>& textureImages, const std::vector<std::shared_ptr<Graphics::TextureImage>>& transientTextureImages, const std::unordered_map<nrd::ResourceType, std::shared_ptr<Graphics::TextureImage>>& userImages, const std::span<const std::string_view>& waitSemaphoreNames, const std::span<const std::string_view>& startSemaphoreNames, const std::span<const std::string_view>& waitPreviousFrameSemaphoreNames)
    {
        auto resolveImageView = [&textureImages, &transientTextureImages, &userImages][[nodiscard]](const nrd::ResourceDesc& rd) noexcept -> vk::ImageView
        {
            auto poolView = [](const auto& pool, uint16_t index) noexcept -> vk::ImageView
            {
                assert(index < pool.size());
                const auto tex = std::dynamic_pointer_cast<const Graphics::VulkanStorageImage>(pool[index]);
                assert(tex && "NRD pool texture is not a VulkanStorageImage");
                return *tex->GetImageView();
            };

            switch (rd.type)
            {
            case nrd::ResourceType::PERMANENT_POOL:
                return poolView(textureImages, rd.indexInPool);
            case nrd::ResourceType::TRANSIENT_POOL:
                return poolView(transientTextureImages, rd.indexInPool);
            default:
            {
                const auto& view = std::dynamic_pointer_cast<Graphics::VulkanStorageImage>(userImages.at(rd.type));
            	if (!view)
                	Log<LogLevel::Critical>("NRD asked for a user resource you didn't provide or image is not a vulkan image");
                return *view->GetImageView();
            }
            }
        };

        const auto& vkCore = VulkanCore::GetInstance();
        const auto& vulkanShader = std::dynamic_pointer_cast<const VulkanNRDShader>(shaders);

        const uSize cbStride = vulkanShader->GetConstBufferStride();
        const uSize frameBase = vkCore.GetCurrentFrameIndex() * cbStride * vulkanShader->GetNumOfConstBuffersPerFrame();

        std::vector<vk::DescriptorImageInfo> imageInfos;
        std::vector<vk::WriteDescriptorSet> writes;

        u32 nextOffset = frameBase;
        u32 boundOffset = frameBase;

        const auto& cmdBuf = m_CommandBuffers[vkCore.GetCurrentFrameIndex()];
        if (const auto restResult = cmdBuf.reset(); !restResult)
            Log<LogLevel::Error>("Failed to reset command buffer. Error code: {}", vk::to_string(restResult.error()));

        if (const auto beginResult = cmdBuf.begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !beginResult)
            Log<LogLevel::Error>("Failed to begin command buffer. Error code: {}", vk::to_string(beginResult.error()));

        constexpr auto computeToComputeBarrier = vk::MemoryBarrier2{}
            .setSrcStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setSrcAccessMask(vk::AccessFlagBits2::eShaderStorageWrite)
            .setDstStageMask(vk::PipelineStageFlagBits2::eComputeShader)
            .setDstAccessMask(vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eShaderStorageWrite);
        const auto barrierDependency = vk::DependencyInfo().setMemoryBarriers(computeToComputeBarrier);

        bool firstDispatch = true;
        for (const auto& dispatch : dispatchDescs)
        {
            if (!std::exchange(firstDispatch, false))
                cmdBuf.pipelineBarrier2(barrierDependency);

            const auto pipeline = vulkanShader->GetPipeline(dispatch);

            imageInfos.resize(dispatch.resourcesNum);
            writes.resize(dispatch.resourcesNum);

            u32 texIdx = 0, uavIdx = 0;
            for (u32 i = 0; i < dispatch.resourcesNum; i++)
            {
                const nrd::ResourceDesc& rd = dispatch.resources[i];
                const bool isStorage = rd.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;

                imageInfos[i] = vk::DescriptorImageInfo{}
                .setImageView(resolveImageView(rd))
                .setImageLayout(vk::ImageLayout::eGeneral);

                writes[i] = vk::WriteDescriptorSet{}
                .setDstBinding(isStorage ? vulkanShader->GetUavBase() + uavIdx++
                                         : vulkanShader->GetTexBase() + texIdx++)
                .setDescriptorType(isStorage ? vk::DescriptorType::eStorageImage
                                             : vk::DescriptorType::eSampledImage)
                .setImageInfo(imageInfos[i]);
            }

            cmdBuf.pushDescriptorSet(vk::PipelineBindPoint::eCompute, vulkanShader->GetPipelineLayout(), vulkanShader->GetResourcesSetIndex(), writes);

            if (dispatch.constantBufferDataSize && !dispatch.constantBufferDataMatchesPreviousDispatch)
            {
                boundOffset = nextOffset;
                constantUniformBuffer->UpdateBufferData(dispatch.constantBufferData, dispatch.constantBufferDataSize, boundOffset);
                nextOffset += cbStride;
            }

            cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, vulkanShader->GetPipelineLayout(), vulkanShader->GetConstBufferSetIndex(), vulkanShader->GetDescriptorSet(), boundOffset);
            cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
            cmdBuf.dispatch(dispatch.gridWidth, dispatch.gridHeight, 1);
        }

        if (const auto cmdEndResult = cmdBuf.end(); !cmdEndResult)
            Log<LogLevel::Critical>("NRD compute command buffer end failed. Error: {}", vk::to_string(cmdEndResult.error()));

        const std::vector<vk::Semaphore> waitSemaphores = VulkanCore::GetSemaphoresFromNames(waitSemaphoreNames);
		const std::vector<vk::Semaphore> signalSemaphores = VulkanCore::GetSemaphoresFromNames(startSemaphoreNames);
		const std::vector<vk::Semaphore> waitPreviousFrameSemaphores = VulkanCore::GetPreviousFrameSemaphoresFromNames(waitPreviousFrameSemaphoreNames, false);

		constexpr vk::PipelineStageFlags2 waitDestinationStageMask(vk::PipelineStageFlagBits2::eAllCommands);

		const auto cmdBufSubmitInfo = vk::CommandBufferSubmitInfo()
			.setCommandBuffer(*m_CommandBuffers[vkCore.GetCurrentFrameIndex()]);

		auto submitInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdBufSubmitInfo);

		std::vector<vk::SemaphoreSubmitInfo> waitSemaphoreSubmitInfos;
		waitSemaphoreSubmitInfos.reserve(waitSemaphores.size() + waitPreviousFrameSemaphores.size());
		if (!waitSemaphores.empty() || !waitPreviousFrameSemaphores.empty())
		{
			for (const auto& semaphore : waitSemaphores)
			{
				waitSemaphoreSubmitInfos.emplace_back(
					vk::SemaphoreSubmitInfo()
						.setSemaphore(semaphore)
						.setStageMask(waitDestinationStageMask)
						.setValue(0)
						.setDeviceIndex(0)
				);
			}
			for (const auto& semaphore : waitPreviousFrameSemaphores)
			{
				waitSemaphoreSubmitInfos.emplace_back(
					vk::SemaphoreSubmitInfo()
						.setSemaphore(semaphore)
						.setStageMask(waitDestinationStageMask)
						.setValue(0)
						.setDeviceIndex(0)
				);
			}

			submitInfo.setWaitSemaphoreInfos(waitSemaphoreSubmitInfos);
		}

		std::vector<vk::SemaphoreSubmitInfo> signalSemaphoreSubmitInfo;
		signalSemaphoreSubmitInfo.reserve(signalSemaphores.size());
		if (!signalSemaphores.empty())
		{
			for (const auto& semaphore : signalSemaphores)
			{
				signalSemaphoreSubmitInfo.emplace_back(
					vk::SemaphoreSubmitInfo()
						.setSemaphore(semaphore)
						.setStageMask(waitDestinationStageMask)
						.setValue(0)
						.setDeviceIndex(0)
				);
			}

			submitInfo.setSignalSemaphoreInfos(signalSemaphoreSubmitInfo);
		}

        if (const auto result = vkCore.GetComputeQueue().submit2(submitInfo); !result)
			Log<LogLevel::Critical>("Failed to submit command buffer: {}", vk::to_string(result.error()));
    }
} // OWC::Graphics