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
		~VulkanRenderPass() override = default;

		VulkanRenderPass(const VulkanRenderPass&) = delete;
		VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;
		VulkanRenderPass(VulkanRenderPass&&) = delete;
		VulkanRenderPass& operator=(VulkanRenderPass&&) = delete;

	private:
		void PushConstant(const BaseShader& shader, uSize size, const void* dataPtr) override;
		void TransitionImage(const std::shared_ptr<TextureImage>& textureBuffer, AccessMask dstAccessMask, StageMask dstStageMask, ImageLayout newLayout) override;
		void TransitionImage(const std::shared_ptr<DynamicTextureBuffer>& textureBuffer, AccessMask dstAccessMask, StageMask dstStageMask, ImageLayout newLayout) override;
		void CopyImage(const std::shared_ptr<TextureImage>& srcImage, const std::shared_ptr<DynamicTextureBuffer>& dstImage) override;
		void BeginDynamicPass() override;
		void BeginRasterPass() override;
		void AddPipeline(const BaseShader& shader) override;
		void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;
		void RayTrace(const BaseShader& shader, u32 width, u32 height, u32 depth) override;
		void EndRenderPass() override;
		void EndPass() override;
		void submitRenderPass(std::span<std::string_view> waitSemaphoreNames, std::span<std::string_view> startSemaphore, std::span<std::string_view> waitPreviousFrameSemaphoreNames) override;

		void RestartRenderPass() override;

		void DrawImGui(std::span<const std::shared_ptr<VertexBuffer>> vertexBuffer, std::span<const std::shared_ptr<IndexBuffer>> indexBuffer, ImDrawData* drawData) override;

		[[nodiscard]] uSize GetNumberOfFramesInFlight() const override;

	public:
		static void WaitTillIdle();
		static void AddToEndOfFrameCleanUp(std::move_only_function<void()>&& func);

	private:
		std::vector<vk::raii::CommandBuffer> m_CommandBuffers = {};
	};
}
