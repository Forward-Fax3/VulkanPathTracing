#include "VulkanRenderPass.hpp"
#include "VulkanRendererCore.hpp"
#include "Application.hpp"

#include "VulkanCore.hpp"
#include "VulkanShader.hpp"
#include "VulkanBuffers.hpp"

#include <imgui.h>


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

	void VulkanRenderPass::PushConstant(const BaseShader& shader, const uSize size, const void* dataPtr)
	{
		auto info = vk::PushConstantsInfo()
			.setLayout(dynamic_cast<const VulkanBaseShader&>(shader).GetPipelineLayout())
			.setOffset(0)
			.setSize(size)
			.setPValues(dataPtr);

		if (testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit))
			info.setStageFlags(vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eMissKHR);
		else
			info.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);

		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].pushConstants2(info);
		else
			for (const auto& cmdBuf : m_CommandBuffers)
				cmdBuf.pushConstants2(info);
	}

	void VulkanRenderPass::TransitionImage(const std::shared_ptr<TextureImage>& textureBuffer, const AccessMask dstAccessMask, const StageMask dstStageMask, const ImageLayout newLayout)
	{
		const auto vulkanTextureBuffer = std::dynamic_pointer_cast<VulkanStorageImage>(textureBuffer);

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

	void VulkanRenderPass::TransitionImage(const std::shared_ptr<DynamicTextureBuffer>& textureBuffer, const AccessMask dstAccessMask, const  StageMask dstStageMask, const ImageLayout newLayout)
	{
		const auto vulkanTextureBuffer = std::dynamic_pointer_cast<VulkanDynamicTextureBuffer>(textureBuffer);

		auto barrier = vk::ImageMemoryBarrier2()
			.setSrcAccessMask(vulkanTextureBuffer->GetCurrentAccessFlags())
			.setDstAccessMask(GetVulkanAccessMask(dstAccessMask))
			.setOldLayout(vulkanTextureBuffer->GetCurrentLayout())
			.setNewLayout(GetVulkanImageLayout(newLayout))
			.setSrcStageMask(vulkanTextureBuffer->GetCurrentPipelineStageFlags())
			.setDstStageMask(GetVulkanPipelineStageMask(dstStageMask))
			.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
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
		{
			barrier.setImage(vulkanTextureBuffer->GetImage());

			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].pipelineBarrier2(dependencyInfo);
		}
		else
			for (uSize i = 0; i < VulkanCore::GetConstInstance().GetNumberOfFramesInFlight(); i++)
			{
				const auto& cmdBuf = m_CommandBuffers[i];

				barrier.setImage(*vulkanTextureBuffer->GetImages()[i]);

				cmdBuf.pipelineBarrier2(dependencyInfo);
			}
	}

	void VulkanRenderPass::CopyImage(const std::shared_ptr<TextureImage>& srcImage, const std::shared_ptr<DynamicTextureBuffer>& dstImage)
	{
		// vk::CopyImageInfo2
		const auto vkSrcImage = std::dynamic_pointer_cast<VulkanStorageImage>(srcImage);
		const auto vkDstImage = std::dynamic_pointer_cast<VulkanDynamicTextureBuffer>(dstImage);

		if (vkSrcImage->GetWidth() != vkDstImage->GetWidth() || vkSrcImage->GetHeight() != vkDstImage->GetHeight())
		{
			Log<LogLevel::Error>("Attempted to copy image from source to destination with different dimensions. This is not allowed.");
			return;
		}

		const auto imageCopy = vk::ImageCopy2()
			.setSrcSubresource(vk::ImageSubresourceLayers()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setMipLevel(0)
				.setBaseArrayLayer(0)
				.setLayerCount(1))
			.setSrcOffset(vk::Offset3D(0, 0, 0))
			.setDstSubresource(vk::ImageSubresourceLayers()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setMipLevel(0)
				.setBaseArrayLayer(0)
				.setLayerCount(1))
			.setDstOffset(vk::Offset3D(0, 0, 0))
			.setExtent(vk::Extent3D(
				vkSrcImage->GetWidth(),
				vkSrcImage->GetHeight(),
				1));

		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
		{
			const auto copyInfo = vk::CopyImageInfo2()
				.setSrcImage(vkSrcImage->GetImage())
				.setDstImage(vkDstImage->GetImage())
				.setSrcImageLayout(vkSrcImage->GetCurrentLayout())
				.setDstImageLayout(vkDstImage->GetCurrentLayout())
				.setRegions(imageCopy);

			m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()].copyImage2(copyInfo);
		}
		else
			for (uSize i = 0; i < m_CommandBuffers.size(); i++)
			{
				const auto copyInfo = vk::CopyImageInfo2()
					.setSrcImage(vkSrcImage->GetImage())
					.setDstImage(vkDstImage->GetImages()[i])
					.setSrcImageLayout(vkSrcImage->GetCurrentLayout())
					.setDstImageLayout(vkDstImage->GetCurrentLayout())
					.setRegions(imageCopy);

				const auto& cmdBuf = m_CommandBuffers[i];
				cmdBuf.copyImage2(copyInfo);
			}
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
		const auto& vulkanShader = dynamic_cast<const VulkanBaseShader&>(shader);
		const auto bindPoint = testBitMask(GetRenderPassType(), RenderPassType::RayTracingBit) ? vk::PipelineBindPoint::eRayTracingKHR : vk::PipelineBindPoint::eGraphics;
		const auto& vkCore = VulkanCore::GetConstInstance();

		if (testBitMask(GetRenderPassType(), RenderPassType::Dynamic))
		{
			const auto& cmdBuf = m_CommandBuffers[vkCore.GetCurrentFrameIndex()];
			cmdBuf.bindPipeline(bindPoint, vulkanShader.GetPipeline());

			cmdBuf.bindDescriptorSets(
				bindPoint,
				vulkanShader.GetPipelineLayout(),
				0,
				*vulkanShader.GetDescriptorSet(),
				{}
			);
		}
		else
			for (uSize i = 0; i < vkCore.GetNumberOfFramesInFlight(); i++)
			{
				auto& cmdBuf = m_CommandBuffers[i];
				cmdBuf.bindPipeline(bindPoint, vulkanShader.GetPipeline());

				cmdBuf.bindDescriptorSets(
					bindPoint,
					vulkanShader.GetPipelineLayout(),
					0,
					*vulkanShader.GetSpecificDescriptorSet(i),
					{}
				);
			}
	}

	void VulkanRenderPass::Draw(const u32 vertexCount, const u32 instanceCount, const u32 firstVertex, const u32 firstInstance)
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

	void VulkanRenderPass::submitRenderPass(const std::span<std::string_view> waitSemaphoreNames, const std::span<std::string_view> startSemaphore, const std::span<std::string_view> waitPreviousFrameSemaphoreNames)
	{
		VulkanCore& vkCore = VulkanCore::GetInstance();

		const std::vector<vk::Semaphore> waitSemaphores = vkCore.GetSemaphoresFromNames(waitSemaphoreNames);
		const std::vector<vk::Semaphore> signalSemaphores = vkCore.GetSemaphoresFromNames(startSemaphore);
		const std::vector<vk::Semaphore> waitPreviousFrameSemaphores = vkCore.GetPreviousFrameSemaphoresFromNames(waitPreviousFrameSemaphoreNames);

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

	void VulkanRenderPass::DrawImGui(const std::span<const std::shared_ptr<VertexBuffer>> vertexBuffer, const std::span<const std::shared_ptr<IndexBuffer>> indexBuffer, ImDrawData* drawData)
	{
		if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
			return; // window minimized

		if (drawData->TotalVtxCount == 0 || drawData->TotalIdxCount == 0)
			return; // nothing to draw

		const auto& vkCore = VulkanCore::GetConstInstance();

		const vk::CommandBuffer& cmd = m_CommandBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()];
		const auto vulkanVertexBuffer = std::dynamic_pointer_cast<VulkanVertexBuffer>(vertexBuffer[vkCore.GetCurrentFrameIndex()]);
		const auto vulkanIndexBuffer = std::dynamic_pointer_cast<VulkanIndexBuffer>(indexBuffer[vkCore.GetCurrentFrameIndex()]);

    	if (drawData->TotalVtxCount > 0)
		{
    		if (vulkanVertexBuffer->GetNumOfVertices() < drawData->TotalVtxCount)
			{
    			vulkanVertexBuffer->Resize(drawData->TotalVtxCount);
			}
    		if (vulkanIndexBuffer->GetNumberOfIndices() < drawData->TotalIdxCount)
			{
    			vulkanIndexBuffer->Resize(drawData->TotalIdxCount);
			}

    		uSize vtxOffset = 0;
    		uSize idxOffset = 0;
		    for (int n = 0; n < drawData->CmdListsCount; n++)
		    {
		        const ImDrawList* cmdList = drawData->CmdLists[n];
		    	const std::span<const ImDrawVert> vtxData(cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size);
		    	const std::span<const ImDrawIdx> idxData(cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size);
		    	vulkanVertexBuffer->Update(std::as_bytes(vtxData), vtxOffset);
		    	vulkanIndexBuffer->Update(std::as_bytes(idxData), idxOffset);
		        vtxOffset += cmdList->VtxBuffer.Size * sizeof(ImDrawVert);
		        idxOffset += cmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
		    }
		}

    	cmd.bindVertexBuffers(0, vulkanVertexBuffer->GetBuffer(), { 0 });
    	cmd.bindIndexBuffer(vulkanIndexBuffer->GetBuffer(), 0, vk::IndexType::eUint32);

    	vk::Viewport viewport{};
    	viewport.x = 0.0f;
    	viewport.y = 0.0f;
    	viewport.width = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    	viewport.height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    	viewport.minDepth = 0.0f;
    	viewport.maxDepth = 1.0f;
    	cmd.setViewport(0, viewport);

    	const ImVec2 clipOff = drawData->DisplayPos;
    	const ImVec2 clipScale = drawData->FramebufferScale;

    	int globalVtxOffset = 0;
    	int globalIdxOffset = 0;

    	for (int n = 0; n < drawData->CmdListsCount; n++)
    	{
    	    const ImDrawList* cmdList = drawData->CmdLists[n];

    	    for (int i = 0; i < cmdList->CmdBuffer.Size; i++)
    	    {
    	        const ImDrawCmd& drawCmd = cmdList->CmdBuffer[i];

    	        /*if (drawCmd.UserCallback != nullptr)
    	        {
    	            // Support AddCallback (e.g. ImDrawCallback_ResetRenderState, or your own)
    	            if (drawCmd.UserCallback == ImDrawCallback_ResetRenderState)
    	            {
    	                // re-bind your default pipeline/state here
    	                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_ImGuiPipeline);
    	            }
    	            else
    	            {
    	                drawCmd.UserCallback(cmdList, &drawCmd);
    	            }
    	            continue;
    	        }*/

    	        // Clip rect -> scissor
    	        const ImVec2 clipMin((drawCmd.ClipRect.x - clipOff.x) * clipScale.x,
    	                        (drawCmd.ClipRect.y - clipOff.y) * clipScale.y);
    	        const ImVec2 clipMax((drawCmd.ClipRect.z - clipOff.x) * clipScale.x,
    	                        (drawCmd.ClipRect.w - clipOff.y) * clipScale.y);

    	        if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
    	            continue; // zero-area scissor, skip draw

    	        vk::Rect2D scissor{};
    	        scissor.offset.x = static_cast<int32_t>(clipMin.x);
    	        scissor.offset.y = static_cast<int32_t>(clipMin.y);
    	        scissor.extent.width  = static_cast<uint32_t>(clipMax.x - clipMin.x);
    	        scissor.extent.height = static_cast<uint32_t>(clipMax.y - clipMin.y);
    	        cmd.setScissor(0, scissor);

    	    	cmd.drawIndexed(
    	            drawCmd.ElemCount, 1,
    	            drawCmd.IdxOffset + globalIdxOffset,
    	            drawCmd.VtxOffset + globalVtxOffset,
    	            0);
    	    }

    	    globalIdxOffset += cmdList->IdxBuffer.Size;
    	    globalVtxOffset += cmdList->VtxBuffer.Size;
    	}
	}

	void VulkanRenderPass::AddToEndOfFrameCleanUp(std::move_only_function<void()>&& func)
	{
		VulkanCore::GetInstance().AddVulkanEndOfFrameCleanUpFunction(std::move(func));
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
