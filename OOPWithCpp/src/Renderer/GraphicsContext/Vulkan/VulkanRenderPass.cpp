#include "VulkanRenderPass.hpp"
#include "Application.hpp"

#include "VulkanCore.hpp"
#include "VulkanShader.hpp"
#include "VulkanUniformBuffer.hpp"

#include <backends/imgui_impl_vulkan.h>


namespace OWC::Graphics
{
	VulkanRenderPass::VulkanRenderPass(const RenderPassType type)
		: RenderPassData(type)
	{
		const VulkanCore& vkCore = VulkanCore::GetConstInstance();

		const vk::Rect2D rect(
			vk::Offset2D(0, 0),
			{
				Application::GetConstInstance().GetPixelWidth(),
				Application::GetConstInstance().GetPixelHeight()
			});

		if (type == RenderPassType::Static)
		{
			m_CommandBuffers = vkCore.GetGraphicsCommandBuffer();
			for (auto& cmdBuf : m_CommandBuffers)
				cmdBuf.begin(vk::CommandBufferBeginInfo());
		}
		else if (type == RenderPassType::Dynamic)
			m_CommandBuffers = vkCore.GetDynamicGraphicsCommandBuffer();
		else if (type == RenderPassType::StaticRayTracing)
		{
			m_CommandBuffers = vkCore.GetComputeCommandBuffer();
			for (auto& cmdBuf : m_CommandBuffers)
				cmdBuf.begin(vk::CommandBufferBeginInfo());
		}
		else if (type == RenderPassType::DynamicRayTracing)
			m_CommandBuffers = vkCore.GetDynamicComputeCommandBuffer();
		else
			Log<LogLevel::Critical>("Invalid RenderPassType specified when creating VulkanRenderPass");
	}

	VulkanRenderPass::~VulkanRenderPass() = default; /*
	{
		const VulkanCore& vkCore = VulkanCore::GetConstInstance();
		const vk::Device& device = vkCore.GetDevice();


		if (GetRenderPassType() == RenderPassType::Static)
			for (auto& cmdBuf : m_CommandBuffers)
				device.freeCommandBuffers(vkCore.GetGraphicsCommandPool(), *cmdBuf);
		else if (GetRenderPassType() == RenderPassType::Dynamic)
			for (auto& cmdBuf : m_CommandBuffers)
				device.freeCommandBuffers(vkCore.GetDynamicGraphicsCommandPool(), *cmdBuf);
		else if (GetRenderPassType() == RenderPassType::StaticRayTracing)
			for (auto& cmdBuf : m_CommandBuffers)
				device.freeCommandBuffers(vkCore.GetComputeCommandPool(), *cmdBuf);
		else if (GetRenderPassType() == RenderPassType::DynamicRayTracing)
			for (auto& cmdBuf : m_CommandBuffers)
				device.freeCommandBuffers(vkCore.GetDynamicComputeCommandPool(), *cmdBuf);
		else
			std::unreachable();
	}*/

	void VulkanRenderPass::PushConstant(const BaseShader& shader, uSize size, const void* dataPtr)
	{
		const auto info = vk::PushConstantsInfo()
			.setLayout(dynamic_cast<const VulkanBaseShader&>(shader).GetPipelineLayout())
			.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eMissKHR)
			.setOffset(0)
			.setSize(size)
			.setPValues(dataPtr);

		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].pushConstants2(info);
		else
			for (const auto& cmdBuf : m_CommandBuffers)
				cmdBuf.pushConstants2(info);
	}

	void VulkanRenderPass::TransitionImage(const std::shared_ptr<TextureBuffer>& textureBuffer, const AccessMask dstAccessMask, StageMask dstStageMask, const ImageLayout newLayout)
	{
		const auto vulkanTextureBuffer = std::dynamic_pointer_cast<VulkanTextureBuffer>(textureBuffer);

		const auto barrier = vk::ImageMemoryBarrier2()
			.setSrcAccessMask(vulkanTextureBuffer->GetCurrentAccessFlags())
			.setDstAccessMask(GetVulkanAccessMask(dstAccessMask))
			.setOldLayout(vulkanTextureBuffer->GetCurrentLayout())
			.setNewLayout(GetVulkanImageLayout(newLayout))
			.setSrcStageMask(vulkanTextureBuffer->GetCurrentPipelineStageFlags())
			.setDstStageMask(GetVulkanPipelineStageMask(dstStageMask))
			.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setImage(vulkanTextureBuffer->GetImage())
			.setSubresourceRange(vk::ImageSubresourceRange()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1)
			);

		vulkanTextureBuffer->SetCurrentAccessFlags(GetVulkanAccessMask(dstAccessMask));
		vulkanTextureBuffer->SetCurrentImageLayout(GetVulkanImageLayout(newLayout));
		vulkanTextureBuffer->SetCurrentPipelineStageFlags(GetVulkanPipelineStageMask(dstStageMask));

		const auto dependencyInfo = vk::DependencyInfo().setImageMemoryBarriers(barrier);

		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].pipelineBarrier2(dependencyInfo);
		else
			for (const auto& cmdBuf : m_CommandBuffers)
				cmdBuf.pipelineBarrier2(dependencyInfo);
	}

	void VulkanRenderPass::BeginDynamicPass()
	{
		if (testBitMask(GetRenderPassType(), RenderPassType::Static))
		{
			Log<LogLevel::Error>("Attempted to begin dynamic pass on a static render pass. This is not allowed.");
			return;
		}

		const auto& cmdBuf = m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()];
		cmdBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	}

	void VulkanRenderPass::BeginRasterPass()
	{
		if (testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit))
		{
			Log<LogLevel::Error>("Attempted to begin raster pass on a ray tracing render pass. This is not allowed.");
			return;
		}

		VulkanCore& vkCore = VulkanCore::GetInstance();


		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
		{
			const uSize frameIndex = vkCore.GetCurrentFrameIndex();
			const vk::CommandBuffer& cmdBuf = m_CommandBuffers[frameIndex];

			const std::array renderingAttachmentInfo = {
				vk::RenderingAttachmentInfo()
					.setImageView(vkCore.GetSwapchainImageViews()[frameIndex])
					.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
					.setLoadOp(vk::AttachmentLoadOp::eLoad)
					.setStoreOp(vk::AttachmentStoreOp::eStore)
			};

			const vk::Rect2D rect(
				vk::Offset2D(0, 0),
				{
					Application::GetConstInstance().GetPixelWidth(),
					Application::GetConstInstance().GetPixelHeight()
				});

			cmdBuf.beginRendering(vk::RenderingInfo()
				.setRenderArea(rect)
				.setLayerCount(1)
				.setColorAttachmentCount(1)
				.setColorAttachments(renderingAttachmentInfo)
			);

			cmdBuf.setViewport(0, vk::Viewport(0.0f, 0.0f,
				static_cast<f32>(Application::GetConstInstance().GetPixelWidth()),
				static_cast<f32>(Application::GetConstInstance().GetPixelHeight()),
				0.0f, 1.0f));
			cmdBuf.setScissor(0, rect);
		}
		else
		{
			const uSize numOfFrames = vkCore.GetNumberOfFramesInFlight();

			for (uSize i = 0; i < numOfFrames; i++)
			{
				auto& cmdBuf = m_CommandBuffers[i];

				const std::array renderingAttachmentInfo = {
					vk::RenderingAttachmentInfo()
						.setImageView(vkCore.GetSwapchainImageViews()[i])
						.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
						.setLoadOp(vk::AttachmentLoadOp::eLoad)
						.setStoreOp(vk::AttachmentStoreOp::eStore)
				};

				const vk::Rect2D rect(
					vk::Offset2D(0, 0),
					{
						Application::GetConstInstance().GetPixelWidth(),
						Application::GetConstInstance().GetPixelHeight()
					});

				cmdBuf.beginRendering(vk::RenderingInfo()
					.setRenderArea(rect)
					.setLayerCount(1)
					.setColorAttachmentCount(1)
					.setColorAttachments(renderingAttachmentInfo)
				);

				cmdBuf.setViewport(0, vk::Viewport(0.0f, 0.0f,
					static_cast<f32>(Application::GetConstInstance().GetPixelWidth()),
					static_cast<f32>(Application::GetConstInstance().GetPixelHeight()),
					0.0f, 1.0f));
				cmdBuf.setScissor(0, rect);
			}
		}
	}

	void VulkanRenderPass::AddPipeline(const BaseShader& shader)
	{
		const auto bindPoint = testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit) ? vk::PipelineBindPoint::eRayTracingKHR : vk::PipelineBindPoint::eGraphics;

		const auto& vulkanShader = dynamic_cast<const VulkanBaseShader&>(shader);
		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
		{
			const uSize frameIndex = VulkanCore::GetConstInstance().GetCurrentFrameIndex();
			m_CommandBuffers[frameIndex].bindPipeline(bindPoint, vulkanShader.GetPipeline());
		}
		else
			for (auto& cmdBuf : m_CommandBuffers)
				cmdBuf.bindPipeline(bindPoint, vulkanShader.GetPipeline());
	}

	void VulkanRenderPass::BindUniform(const BaseShader& shader)
	{
		const auto bindPoint = testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit) ? vk::PipelineBindPoint::eRayTracingKHR : vk::PipelineBindPoint::eGraphics;

		const auto& vulkanShader = dynamic_cast<const VulkanBaseShader&>(shader);
		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].bindDescriptorSets(
				bindPoint,
				vulkanShader.GetPipelineLayout(),
				0,
				*vulkanShader.GetDescriptorSet(),
				{}
			);
		else
			for (const auto& cmdBuf : m_CommandBuffers)
				cmdBuf.bindDescriptorSets(
					bindPoint,
					vulkanShader.GetPipelineLayout(),
					0,
					*vulkanShader.GetDescriptorSet(),
					{}
				);
	}

	void VulkanRenderPass::BindTexture(const BaseShader& shader, u32 binding, u32 textureID)
	{
		const auto bindPoint = testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit) ? vk::PipelineBindPoint::eRayTracingKHR : vk::PipelineBindPoint::eGraphics;

		(void)binding;
		(void)textureID;

		const auto& vulkanShader = dynamic_cast<const VulkanBaseShader&>(shader);

		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].bindDescriptorSets(
				bindPoint,
				vulkanShader.GetPipelineLayout(),
				0,
				*vulkanShader.GetDescriptorSet(),
				{}
			);
		else
			for (const auto& cmdBuf : m_CommandBuffers)
				cmdBuf.bindDescriptorSets(
					bindPoint,
					vulkanShader.GetPipelineLayout(),
					0,
					*vulkanShader.GetDescriptorSet(),
					{}
				);
	}

	void VulkanRenderPass::Draw(u32 vertexCount, u32 instanceCount /*= 1*/, u32 firstVertex /*= 0*/, u32 firstInstance /*= 0*/)
	{
		if (testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit))
		{
			Log<LogLevel::Error>("Draw called on a ray tracing render pass. This is not allowed.");
			return;
		}

		if (GetRenderPassType() == RenderPassType::Dynamic)
		{
			const uSize frameIndex = VulkanCore::GetConstInstance().GetCurrentFrameIndex();
			m_CommandBuffers[frameIndex].draw(vertexCount, instanceCount, firstVertex, firstInstance);
		}
		else
			for (auto& cmdBuf : m_CommandBuffers)
				cmdBuf.draw(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void VulkanRenderPass::RayTrace(const BaseShader& shader, const u32 width, const u32 height, const u32 depth)
	{
		if (!testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit))
		{
			Log<LogLevel::Error>("RayTrace called on a non ray tracing render pass. This is not allowed.");
			return;
		}

		const auto& vulkanShader = dynamic_cast<const VulkanRayTracingShader&>(shader);

		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].traceRaysKHR(
				vulkanShader.GetRayGenShaderSBTEntry(),
				vulkanShader.GetMissGroupSBTEntry(),
				vulkanShader.GetHitGroupSBTEntry(),
				vulkanShader.GetCallableGroupSBTEntry(),
				width,
				height,
				depth
			);
		else
			for (auto& cmdBuf : m_CommandBuffers)
				cmdBuf.traceRaysKHR(
					vulkanShader.GetRayGenShaderSBTEntry(),
					vulkanShader.GetMissGroupSBTEntry(),
					vulkanShader.GetHitGroupSBTEntry(),
					vulkanShader.GetCallableGroupSBTEntry(),
					width,
					height,
					depth
				);
	}

	void VulkanRenderPass::EndRenderPass()
	{
		if (testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit))
		{
			Log<LogLevel::Error>("EndRenderPass called on a ray tracing render pass. This is not allowed.");
			return;
		}

		if (testBitMask(GetRenderPassType(), RenderPassType::Static))
			for (uSize i = 0; i != VulkanCore::GetConstInstance().GetSwapchainImages().size(); i++)
			{
				vk::raii::CommandBuffer& cmdBuf = m_CommandBuffers[i];
				cmdBuf.endRendering();
			}
		else
		{
			const vk::CommandBuffer& cmdBuf = m_CommandBuffers[VulkanCore::GetInstance().GetCurrentFrameIndex()];
			cmdBuf.endRendering();
		}
	}

	void VulkanRenderPass::EndPass()
	{
		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].end();
		else
			for (auto& cmdBuf : m_CommandBuffers)
				cmdBuf.end();
	}

	void VulkanRenderPass::submitRenderPass(std::span<std::string_view> waitSemaphoreNames, std::span<std::string_view> startSemaphore, bool waitForLastFrameToFinish)
	{
		VulkanCore& vkCore = VulkanCore::GetInstance();

		std::vector<vk::Semaphore> waitSemaphores = vkCore.GetSemaphoresFromNames(waitSemaphoreNames);
		std::vector<vk::Semaphore> signalSemaphores = vkCore.GetSemaphoresFromNames(startSemaphore);

		if (waitForLastFrameToFinish && !Application::GetConstInstance().IsFirstFrame())
			waitSemaphores.push_back(vkCore.GetLastFrameFinishedSemaphore());

		vk::PipelineStageFlags2 waitDestinationStageMask(vk::PipelineStageFlagBits2::eAllCommands);

		const auto cmdBufSubmitInfo = vk::CommandBufferSubmitInfo()
			.setCommandBuffer(*m_CommandBuffers[vkCore.GetCurrentFrameIndex()]);

		auto submitInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdBufSubmitInfo);

		vk::SemaphoreSubmitInfo waitSemaphoreSubmitInfo;
		if (!waitSemaphores.empty())
		{
			waitSemaphoreSubmitInfo = vk::SemaphoreSubmitInfo()
				.setSemaphore(waitSemaphores.back())
				.setStageMask(waitDestinationStageMask)
				.setValue(0)
				.setDeviceIndex(0);

			submitInfo.setWaitSemaphoreInfos(waitSemaphoreSubmitInfo);
		}

		vk::SemaphoreSubmitInfo signalSemaphoreSubmitInfo;
		if (!signalSemaphores.empty())
		{
			signalSemaphoreSubmitInfo = vk::SemaphoreSubmitInfo()
				.setSemaphore(signalSemaphores.back())
				.setStageMask(waitDestinationStageMask)
				.setValue(0)
				.setDeviceIndex(0);

			submitInfo.setSignalSemaphoreInfos(signalSemaphoreSubmitInfo);
		}

		if (testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit))
			vkCore.GetComputeQueue().submit2(submitInfo);
		else
			vkCore.GetGraphicsQueue().submit2(submitInfo);
	}

	void VulkanRenderPass::RestartRenderPass()
	{
		const VulkanCore& vkCore = VulkanCore::GetConstInstance();
		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
			m_CommandBuffers[vkCore.GetCurrentFrameIndex()].reset(vk::CommandBufferResetFlags());
		else
			Log<LogLevel::Critical>("RestartRenderPass can not be call on static render pass. This is not allowed.");
	}

	void VulkanRenderPass::DrawImGui(ImDrawData* drawData)
	{
		const vk::CommandBuffer& cmdBuf = m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()];
		ImGui_ImplVulkan_RenderDrawData(drawData, cmdBuf);
	}

	void VulkanRenderPass::AddToEndOfFrameCleanUp(std::move_only_function<void()>&& func)
	{
		VulkanCore::GetInstance().AddVulkanEndOfFrameCleanUpFunction(std::move(func));
	}

	vk::AccessFlags2 VulkanRenderPass::GetVulkanAccessMask(const AccessMask accessMask)
	{
		vk::AccessFlags2 accessFlags;

		if (testBitMask(accessMask, AccessMask::IndirectCommandRead))
			accessFlags |= vk::AccessFlagBits2::eIndirectCommandRead;
		if (testBitMask(accessMask, AccessMask::IndexRead))
			accessFlags |= vk::AccessFlagBits2::eIndexRead;
		if (testBitMask(accessMask, AccessMask::VertexAttributeRead))
			accessFlags |= vk::AccessFlagBits2::eVertexAttributeRead;
		if (testBitMask(accessMask, AccessMask::UniformRead))
			accessFlags |= vk::AccessFlagBits2::eUniformRead;
		if (testBitMask(accessMask, AccessMask::InputAttachmentRead))
			accessFlags |= vk::AccessFlagBits2::eInputAttachmentRead;
		if (testBitMask(accessMask, AccessMask::ShaderRead))
			accessFlags |= vk::AccessFlagBits2::eShaderRead;
		if (testBitMask(accessMask, AccessMask::ShaderWrite))
			accessFlags |= vk::AccessFlagBits2::eShaderWrite;
		if (testBitMask(accessMask, AccessMask::ColorAttachmentRead))
			accessFlags |= vk::AccessFlagBits2::eColorAttachmentRead;
		if (testBitMask(accessMask, AccessMask::ColorAttachmentWrite))
			accessFlags |= vk::AccessFlagBits2::eColorAttachmentWrite;
		if (testBitMask(accessMask, AccessMask::DepthStencilAttachmentRead))
			accessFlags |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
		if (testBitMask(accessMask, AccessMask::DepthStencilAttachmentWrite))
			accessFlags |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
		if (testBitMask(accessMask, AccessMask::TransferRead))
			accessFlags |= vk::AccessFlagBits2::eTransferRead;
		if (testBitMask(accessMask, AccessMask::TransferWrite))
			accessFlags |= vk::AccessFlagBits2::eTransferWrite;

		return accessFlags;
	}

	vk::PipelineStageFlags2 VulkanRenderPass::GetVulkanPipelineStageMask(const StageMask stageMask)
	{
		vk::PipelineStageFlags2 result;

		if (testBitMask(stageMask, StageMask::TopOfPipe))
			result |= vk::PipelineStageFlagBits2::eTopOfPipe;
		if (testBitMask(stageMask, StageMask::BottomOfPipe))
			result |= vk::PipelineStageFlagBits2::eBottomOfPipe;
		if (testBitMask(stageMask, StageMask::FragmentShader))
			result |= vk::PipelineStageFlagBits2::eFragmentShader;
		if (testBitMask(stageMask, StageMask::VertexShader))
			result |= vk::PipelineStageFlagBits2::eVertexShader;
		if (testBitMask(stageMask, StageMask::GeometryShader))
			result |= vk::PipelineStageFlagBits2::eGeometryShader;
		if (testBitMask(stageMask, StageMask::ComputeShader))
			result |= vk::PipelineStageFlagBits2::eComputeShader;
		if (testBitMask(stageMask, StageMask::RayTracing))
			result |= vk::PipelineStageFlagBits2::eRayTracingShaderKHR;

		return result;
	}

	vk::ImageLayout VulkanRenderPass::GetVulkanImageLayout(const ImageLayout layout)
	{
		switch (layout)
		{
		case ImageLayout::Undefined:
			return vk::ImageLayout::eUndefined;
		case ImageLayout::ColorAttachmentOptimal:
			return vk::ImageLayout::eColorAttachmentOptimal;
		case ImageLayout::DepthStencilAttachmentOptimal:
			return vk::ImageLayout::eDepthStencilAttachmentOptimal;
		case ImageLayout::ShaderReadOnlyOptimal:
			return vk::ImageLayout::eShaderReadOnlyOptimal;
		case ImageLayout::TransferSrcOptimal:
			return vk::ImageLayout::eTransferSrcOptimal;
		case ImageLayout::TransferDstOptimal:
			return vk::ImageLayout::eTransferDstOptimal;
		case ImageLayout::General:
			return vk::ImageLayout::eGeneral;
		default:
			Log<LogLevel::Error>("Invalid ImageLayout specified when converting to Vulkan image layout");
			return vk::ImageLayout::eUndefined;
		}
	}

	void VulkanRenderPass::BindDynamicTexture(const BaseShader& shader, u32 binding, u32 textureID)
	{
		const auto bindPoint = testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit) ? vk::PipelineBindPoint::eRayTracingKHR : vk::PipelineBindPoint::eGraphics;

		(void)binding;
		(void)textureID;
		const auto& vulkanShader = dynamic_cast<const VulkanBaseShader&>(shader);
		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].bindDescriptorSets(
				bindPoint,
				vulkanShader.GetPipelineLayout(),
				0,
				*vulkanShader.GetDescriptorSet(),
				{}
			);
		else
			for (const auto& cmdBuf : m_CommandBuffers)
				cmdBuf.bindDescriptorSets(
					bindPoint,
					vulkanShader.GetPipelineLayout(),
					0,
					*vulkanShader.GetDescriptorSet(),
					{}
				);
	}

	uSize VulkanRenderPass::GetNumberOfFramesInFlight() const
	{
		return VulkanCore::GetConstInstance().GetNumberOfFramesInFlight();
	}

	void VulkanRenderPass::WaitTillIdle()
	{
		VulkanCore::GetConstInstance().GetDevice().waitIdle();
	}
}
