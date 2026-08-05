#pragma once
#include "Renderer.hpp"
#include "BaseShader.hpp"

#include "vulkan/vulkan_raii.hpp"


namespace OWC::Graphics
{
	class VulkanRenderPass : public RenderPassData
	{
	public:
		VulkanRenderPass() = delete;
		explicit VulkanRenderPass(RenderPassType type);
		~VulkanRenderPass() override;

		VulkanRenderPass(const VulkanRenderPass&) = delete;
		VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;
		VulkanRenderPass(VulkanRenderPass&&) = delete;
		VulkanRenderPass& operator=(VulkanRenderPass&&) = delete;

	private:
		void PushConstant(const BaseShader& shader, uSize size, const void* dataPtr) override;
		void TransitionImage(const std::shared_ptr<TextureBuffer>& textureBuffer, AccessMask dstAccessMask, StageMask dstStageMask, ImageLayout newLayout) override;
		void BeginDynamicPass() override;
		void BeginRasterPass() override;
		void AddPipeline(const BaseShader& shader) override;
		void BindUniform(const BaseShader& shader) override;
		void BindTexture(const BaseShader& shader, u32 binding, u32 textureID) override;
		void BindDynamicTexture(const BaseShader& shader, u32 binding, u32 textureID) override;
		void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) override;
		void RayTrace(const BaseShader& shader, u32 width, u32 height, u32 depth) override;
		void EndRenderPass() override;
		void EndPass() override;
		void submitRenderPass(std::span<std::string_view> waitSemaphoreNames, std::span<std::string_view> startSemaphore, bool waitForLastFrameToFinish) override;

		void RestartRenderPass() override;

		void DrawImGui(ImDrawData* drawData) override;

		static vk::AccessFlags2 GetVulkanAccessMask(AccessMask accessMask);
		static vk::PipelineStageFlags2 GetVulkanPipelineStageMask(StageMask stageMask);
		static vk::ImageLayout GetVulkanImageLayout(ImageLayout layout);

		[[nodiscard]] uSize GetNumberOfFramesInFlight() const override;

	public:
		static void WaitTillIdle();
		static void AddToEndOfFrameCleanUp(std::move_only_function<void()>&& func);

	private:
		std::vector<vk::raii::CommandBuffer> m_CommandBuffers = {};
	};
}
