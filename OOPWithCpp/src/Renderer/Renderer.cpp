#include "Application.hpp"
#include "Renderer.hpp"

#include "VulkanRenderPass.hpp"


namespace OWC::Graphics
{
	std::shared_ptr<GraphicsContext> Renderer::s_GC = nullptr;

	RendererAPI Renderer::s_API = RendererAPI::Vulkan;

	void Renderer::Init()
	{
		s_GC = Application::GetInstance().GetWindow().GetGraphicsContextPtr().lock();
	}

	void Renderer::Shutdown()
	{
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

	void Renderer::PushConstant(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader, uSize size,
	                            const void* dataPtr)
	{
		data->PushConstant(shader, size, dataPtr);
	}

	void Renderer::TransitionImage(const std::shared_ptr<RenderPassData>& data,
		const std::shared_ptr<TextureBuffer>& textureBuffer, AccessMask dstAccessMask, StageMask dstStageMask, ImageLayout newLayout)
	{
		data->TransitionImage(textureBuffer, dstAccessMask, dstStageMask, newLayout);
	}

	void Renderer::BeginDynamicPass(const std::shared_ptr<RenderPassData>& data)
	{
		data->BeginDynamicPass();
	}

	void Renderer::BindUniform(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader)
	{
		data->BindUniform(shader);
	}

	void Renderer::Draw(const std::shared_ptr<RenderPassData>& data, u32 vertexCount, u32 instanceCount /*= 1*/, u32 firstVertex /*= 0*/, u32 firstInstance /*= 0*/)
	{
		data->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void Renderer::RayTrace(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader, u32 width, u32 height, u32 depth)
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

	void Renderer::SubmitRenderPass(const std::shared_ptr<RenderPassData>& data,
		std::span<std::string_view> waitSemaphoreNames, std::span<std::string_view> startSemaphoreNames,
		bool waitForLastFrameToFinish)
	{
		data->submitRenderPass(waitSemaphoreNames, startSemaphoreNames, waitForLastFrameToFinish);
	}

	void Renderer::RestartRenderPass(const std::shared_ptr<RenderPassData>& data)
	{
		data->RestartRenderPass();
	}

	void Renderer::DrawImGui(const std::shared_ptr<RenderPassData>& data, ImDrawData* drawData)
	{
		data->DrawImGui(drawData);
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

	void Renderer::BindTexture(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader, u32 binding, u32 textureID)
	{
		data->BindTexture(shader, binding, textureID);
	}

	void Renderer::BindDynamicTexture(const std::shared_ptr<RenderPassData>& data, const BaseShader& shader, u32 binding, u32 textureID)
	{
		data->BindDynamicTexture(shader, binding, textureID);
	}

	OWC::uSize Renderer::GetNumberOfFramesInFlight(const std::shared_ptr<RenderPassData>& data)
	{
		return data->GetNumberOfFramesInFlight();
	}
};