#pragma once
#define RENDERER_HPP
#ifndef GRAPHICSCONTEXT_HPP
#include "GraphicsContext.hpp"
#endif
#include "BaseShader.hpp"
#include "GPUBuffers.hpp"

#include <imgui.h>
#include <span>


namespace OWC::Graphics
{
	class GraphicsContext;

	struct DynamicRangeInfo {
		f32 exposure = 1.0f;
		f32 saturation = 1.0f;
		u32 HDROn = 0;
		u32 toneMappingSelector = 0;
		f32 paperWhiteNits = 0.0f;
		f32 maxNits = 0.0f;
		f32 maxUINits = 0.0f;
		f32 invGamma = 1.0f / 2.2f;
	};

	class RenderPassData;

	struct ImGuiShaderData
	{
		std::shared_ptr<RenderPassData> renderPassData = nullptr;
		std::shared_ptr<BaseShader> shader = nullptr;
	};

	enum class RendererAPI : u8
	{
//		None = 0,
		Vulkan = 1,
	}; // only Vulkan is supported for now

	enum class RenderPassType : u8
	{
		Static = 0x01,
		Dynamic = 0x02,
		RayTracingBit = 0x04,
		StaticRayTracing = RayTracingBit | Static,
		DynamicRayTracing = RayTracingBit | Dynamic,
	};

	OWC_FORCE_INLINE RenderPassType operator&(RenderPassType lhs, RenderPassType rhs)
	{
		return static_cast<RenderPassType>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
	}

	class Renderer;

	class RenderPassData
	{
		friend class Renderer;

	public:
		virtual ~RenderPassData() = default;
		RenderPassData(const RenderPassData&) = delete;
		RenderPassData& operator=(const RenderPassData&) = delete;
		RenderPassData(RenderPassData&&) = delete;
		RenderPassData& operator=(RenderPassData&&) = delete;

	private:
		void virtual PushConstant(const BaseShader& shader, uSize size, const void* dataPtr) = 0;
		void virtual TransitionImage(const std::shared_ptr<TextureImage>& textureBuffer, AccessMask dstAccessMask, StageMask dstStageMask, ImageLayout newLayout) = 0;
		void virtual TransitionImage(const std::shared_ptr<DynamicTextureBuffer>& textureBuffer, AccessMask dstAccessMask, StageMask dstStageMask, ImageLayout newLayout) = 0;
		void virtual CopyImage(const std::shared_ptr<TextureImage>& srcImage, const std::shared_ptr<DynamicTextureBuffer>& dstImage) = 0;
		void virtual BeginDynamicPass() = 0;
		void virtual BeginRasterPass() = 0;
		void virtual AddPipeline(const BaseShader& shader) = 0;
		void virtual Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) = 0;
		void virtual RayTrace(const BaseShader& shader, u32 width, u32 height, u32 depth) = 0;
		void virtual EndRenderPass() = 0;
		void virtual EndPass() = 0;
		void virtual submitRenderPass(std::span<std::string_view> waitSemaphoreNames, std::span<std::string_view> startSemaphore, std::span<std::string_view> waitPreviousFrameSemaphoreNames) = 0;

		void virtual RestartRenderPass() = 0;

		void virtual DrawImGui(std::span<const std::shared_ptr<VertexBuffer>> vertexBuffer, std::span<const std::shared_ptr<IndexBuffer>> indexBuffer, ImDrawData* drawData) = 0;

		[[nodiscard]] uSize virtual GetNumberOfFramesInFlight() const = 0;

	protected:
		explicit RenderPassData(const RenderPassType type) : type(type) {}

		[[nodiscard]] RenderPassType GetRenderPassType() const { return type; }

	private:
		RenderPassType type;
	};

	class Renderer
	{
	public:
		static void Init();
		static void Shutdown();
		static void FinishRender();

		[[nodiscard]] static std::shared_ptr<RenderPassData> GetDynamicPass();
		[[nodiscard]] static std::shared_ptr<RenderPassData> GetDynamicRayTracingPass();
		[[nodiscard]] static std::shared_ptr<RenderPassData> GetStaticRayTracingPass();
		[[nodiscard]] static std::shared_ptr<RenderPassData> GetStaticRenderPass();

		static void BeginDynamicPass(const std::shared_ptr<RenderPassData>& data);
		static void BeginRasterPass(const std::shared_ptr<RenderPassData>& data);

		static void PushConstant(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader, uSize size, const void* dataPtr);
		static void TransitionImage(const std::shared_ptr<RenderPassData>& data, const std::shared_ptr<TextureImage>& textureBuffer, AccessMask dstAccessMask, StageMask dstStageMask, ImageLayout newLayout);
		static void TransitionImage(const std::shared_ptr<RenderPassData>& data, const std::shared_ptr<DynamicTextureBuffer>& textureBuffer, AccessMask dstAccessMask, StageMask dstStageMask, ImageLayout newLayout);
		//static void CopyImage(const std::shared_ptr<RenderPassData>& data, const std::shared_ptr<TextureImage>& srcImage, const std::shared_ptr<TextureImage>& dstImage);
		//static void CopyImage(const std::shared_ptr<RenderPassData>& data, const std::shared_ptr<DynamicTextureBuffer>& srcImage, const std::shared_ptr<TextureImage>& dstImage);
		static void CopyImage(const std::shared_ptr<RenderPassData>& data, const std::shared_ptr<TextureImage>& srcImage, const std::shared_ptr<DynamicTextureBuffer>& dstImage);
		//static void CopyImage(const std::shared_ptr<RenderPassData>& data, const std::shared_ptr<DynamicRangeInfo>& srcImage, const std::shared_ptr<DynamicTextureBuffer>& dstImage);
		static void PipelineBind(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader);
		static void Draw(const std::shared_ptr<RenderPassData>& data, u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
		static void RayTrace(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader, u32 width, u32 height, u32 depth);
		static void EndRasterPass(const std::shared_ptr<RenderPassData>& data);
		static void EndPass(const std::shared_ptr<RenderPassData>& data);
		static void SubmitRenderPass(const std::shared_ptr<RenderPassData>& data, std::span<std::string_view> waitSemaphoreNames = {}, std::span<std::string_view> startSemaphoreNames = {}, std::span<std::string_view> waitPreviousFrameSemaphoreNames = {});

		static void RestartRenderPass(const std::shared_ptr<RenderPassData>& data);

		static void DrawImGui(const std::shared_ptr<RenderPassData>& data, std::span<const std::shared_ptr<VertexBuffer>> vertexBuffer, std::span<const std::shared_ptr<IndexBuffer>> indexBuffer, ImDrawData* drawData);

		static void WaitTillIdle();
		static void AddToEndOfFrameCleanUp(std::move_only_function<void()>&& func);

		[[nodiscard]] static uSize GetNumberOfFramesInFlight();
		[[nodiscard]] [[deprecated]] static uSize GetNumberOfFramesInFlight(const std::shared_ptr<RenderPassData>& data);

		[[nodiscard]] static OWC_FORCE_INLINE const std::shared_ptr<UniformBuffer>& GetDynamicRageUniformBuffer() { return s_DynamicRangeUniformBuffer; }

		[[nodiscard]] static bool HasShaderExecutionReorderingSupport();

		static RendererAPI GetAPI() { return s_API; }

		static OWC_FORCE_INLINE void SetHDR(const bool hasHDR) { s_HasHDR = hasHDR; }
		[[nodiscard]] static OWC_FORCE_INLINE bool GetHDR() { return s_HasHDR; }

		static void WaitForGPUIdle();

	private:
		static std::shared_ptr<GraphicsContext> s_GC;
		static RendererAPI s_API;
		static std::shared_ptr<UniformBuffer> s_DynamicRangeUniformBuffer;
		static bool s_HasHDR;
	};
}
