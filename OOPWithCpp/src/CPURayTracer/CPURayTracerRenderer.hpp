#pragma once
#include "Layer.hpp"
#include "BaseShader.hpp"
#include "Renderer.hpp"
#include "GPUBuffers.hpp"
#include "InterLayerData.hpp"

#include <memory>


namespace OWC
{
	class CPURayTracerRenderer : public Layer
	{
	private:
		struct UniformBufferObject
		{
			f32 divider = 0.0f;
		};

	public:
		CPURayTracerRenderer() = delete;
		explicit CPURayTracerRenderer(const std::shared_ptr<InterLayerData>& ILD);
		~CPURayTracerRenderer() override = default;
		CPURayTracerRenderer(const CPURayTracerRenderer&) = delete;
		CPURayTracerRenderer& operator=(const CPURayTracerRenderer&) = delete;
		CPURayTracerRenderer(CPURayTracerRenderer&&) = delete;
		CPURayTracerRenderer& operator=(CPURayTracerRenderer&&) = delete;

		void OnUpdate() override;
		void OnEvent(class BaseEvent& event) override;

	private: // methods
		void SetupRenderPass();
		void SetupPipeline();

	private: // attributes
		std::unique_ptr<Graphics::BaseShader> m_Shader = nullptr;
		std::shared_ptr<Graphics::RenderPassData> m_renderPass = nullptr;
		std::shared_ptr<Graphics::UniformBuffer> m_UniformBuffer = nullptr;
		std::shared_ptr<Graphics::DynamicTextureBuffer> m_Image = nullptr;
		std::shared_ptr<InterLayerData> m_ILD = nullptr;
		uSize framesUpdated = 0;
	};
}
