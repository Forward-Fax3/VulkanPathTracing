#include "Application.hpp"
#include "Renderer.hpp"

#include "VulkanCore.hpp"
#include "VulkanRenderPass.hpp"


namespace OWC::Graphics
{
	std::shared_ptr<GraphicsContext> Renderer::s_GC = nullptr;

	RendererAPI Renderer::s_API = RendererAPI::Vulkan;

	std::shared_ptr<UniformBuffer> Renderer::s_DynamicRangeUniformBuffer = nullptr;

	bool Renderer::s_HasHDR = false;

	void Renderer::Init()
	{
		s_GC = Application::GetInstance().GetWindow().GetGraphicsContextPtr().lock();
		s_DynamicRangeUniformBuffer = UniformBuffer::CreateUniformBuffer(sizeof(DynamicRangeInfo));
	}

	void Renderer::Shutdown()
	{
		s_DynamicRangeUniformBuffer.reset();
		s_GC.reset();
	}

	void Renderer::FinishRender()
	{
		s_GC->FinishRender();
	}

	std::shared_ptr<OWC::Graphics::RenderPassData> Renderer::GetDynamicPass()
	{
		switch (s_API) // Switch statement for future APIs
		{
		case OWC::Graphics::RendererAPI::Vulkan:
			return std::make_shared<VulkanRenderPass>(RenderPassType::Dynamic);
		default:
			return nullptr;
		}
	}

	std::shared_ptr<RenderPassData> Renderer::GetDynamicRayTracingPass()
	{
		switch (s_API) // Switch statement for future APIs
		{
		case OWC::Graphics::RendererAPI::Vulkan:
			return std::make_shared<VulkanRenderPass>(RenderPassType::DynamicRayTracing);
		default:
			return nullptr;
		}
	}

	std::shared_ptr<RenderPassData> Renderer::GetStaticRayTracingPass()
	{
		switch (s_API) // Switch statement for future APIs
		{
		case OWC::Graphics::RendererAPI::Vulkan:
			return std::make_shared<VulkanRenderPass>(RenderPassType::StaticRayTracing);
		default:
			return nullptr;
		}
	}

	std::shared_ptr<RenderPassData> Renderer::GetStaticRenderPass()
	{
		switch (s_API) // Switch statement for future APIs
		{
		case OWC::Graphics::RendererAPI::Vulkan:
			return std::make_shared<VulkanRenderPass>(RenderPassType::Static);
		default:
			return nullptr;
		}
	}

	void Renderer::BeginRasterPass(const std::shared_ptr<RenderPassData>& data)
	{
		data->BeginRasterPass();
	}

	void Renderer::PushConstant(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader, const uSize size, const void* dataPtr)
	{
		data->PushConstant(shader, size, dataPtr);
	}

	void Renderer::TransitionImage(const std::shared_ptr<RenderPassData>& data, const std::shared_ptr<TextureImage>& textureBuffer, const AccessMask dstAccessMask, const StageMask dstStageMask, const ImageLayout newLayout)
	{
		data->TransitionImage(textureBuffer, dstAccessMask, dstStageMask, newLayout);
	}

	void Renderer::TransitionImage(const std::shared_ptr<RenderPassData>& data, const std::shared_ptr<DynamicTextureBuffer>& textureBuffer, const AccessMask dstAccessMask, const StageMask dstStageMask, const ImageLayout newLayout)
	{
		data->TransitionImage(textureBuffer, dstAccessMask, dstStageMask, newLayout);
	}

	void Renderer::CopyImage(const std::shared_ptr<RenderPassData>& data, const std::shared_ptr<TextureImage>& srcImage, const std::shared_ptr<DynamicTextureBuffer>& dstImage)
	{
		data->CopyImage(srcImage, dstImage);
	}

	void Renderer::BeginDynamicPass(const std::shared_ptr<RenderPassData>& data)
	{
		data->BeginDynamicPass();
	}

	void Renderer::Draw(const std::shared_ptr<RenderPassData>& data, const u32 vertexCount, const u32 instanceCount /*= 1*/, const u32 firstVertex /*= 0*/, const u32 firstInstance /*= 0*/)
	{
		data->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void Renderer::RayTrace(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader, const u32 width, const u32 height, const u32 depth)
	{
		data->RayTrace(shader, width, height, depth);
	}

	void Renderer::PipelineBind(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader)
	{
		data->AddPipeline(shader);
	}

	void Renderer::EndRasterPass(const std::shared_ptr<RenderPassData>& data)
	{
		data->EndRenderPass();
	}

	void Renderer::EndPass(const std::shared_ptr<RenderPassData>& data)
	{
		data->EndPass();
	}

	void Renderer::SubmitRenderPass(const std::shared_ptr<RenderPassData>& data, const std::span<std::string_view> waitSemaphoreNames, const std::span<std::string_view> startSemaphoreNames, const std::span<std::string_view> waitPreviousFrameSemaphoreNames)
	{
		data->submitRenderPass(waitSemaphoreNames, startSemaphoreNames, waitPreviousFrameSemaphoreNames);
	}

	void Renderer::RestartRenderPass(const std::shared_ptr<RenderPassData>& data)
	{
		data->RestartRenderPass();
	}

	void Renderer::DrawImGui(const std::shared_ptr<RenderPassData>& data, const std::span<const std::shared_ptr<VertexBuffer>> vertexBuffer, const std::span<const std::shared_ptr<IndexBuffer>> indexBuffer, ImDrawData* drawData)
	{
		data->DrawImGui(vertexBuffer, indexBuffer, drawData);
	}

	void Renderer::WaitTillIdle()
	{
		switch (s_API)
		{
		case OWC::Graphics::RendererAPI::Vulkan:
			VulkanRenderPass::WaitTillIdle();
			break;
		default:
			std::unreachable();
		}
	}

	void Renderer::AddToEndOfFrameCleanUp(std::move_only_function<void()>&& func)
	{
		switch (s_API)
		{
		case OWC::Graphics::RendererAPI::Vulkan:
			VulkanRenderPass::AddToEndOfFrameCleanUp(std::move(func));
			break;
		default:
			std::unreachable();
		}
	}

	uSize Renderer::GetNumberOfFramesInFlight()
	{
		switch (s_API)
		{
		case OWC::Graphics::RendererAPI::Vulkan:
			return VulkanCore::GetConstInstance().GetNumberOfFramesInFlight();
		default:
			std::unreachable();
		}
	}

	OWC::uSize Renderer::GetNumberOfFramesInFlight(const std::shared_ptr<RenderPassData>& data)
	{
		return data->GetNumberOfFramesInFlight();
	}

	bool Renderer::HasShaderExecutionReorderingSupport()
	{
		switch (s_API) // Switch statement for future APIs
		{
		case OWC::Graphics::RendererAPI::Vulkan:
			return VulkanCore::GetConstInstance().HasSERSupport();
		default:
			return false;
		}
	}

	void Renderer::WaitForGPUIdle()
	{
		s_GC->WaitForIdle();
	}
};