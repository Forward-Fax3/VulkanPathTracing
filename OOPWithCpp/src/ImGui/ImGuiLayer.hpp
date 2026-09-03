#pragma once
#include "Layer.hpp"
#include "Renderer.hpp"

#include <chrono>


namespace OWC
{
	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer() override;
		ImGuiLayer(ImGuiLayer&) = delete;
		ImGuiLayer& operator=(ImGuiLayer&) = delete;
		ImGuiLayer(ImGuiLayer&&) = delete;
		ImGuiLayer&& operator=(ImGuiLayer&&) = delete;

		void ImGuiRender() override;
		void OnEvent(class BaseEvent& event) override;

		static void Begin();
		void End() const;

	private:
		std::shared_ptr<Graphics::RenderPassData> m_RenderPassData = nullptr;
		std::shared_ptr<Graphics::BaseShader> m_ImGuiShader = nullptr;
		std::shared_ptr<Graphics::TextureImage> m_ImGuiTexture = nullptr;
		std::shared_ptr<Graphics::VertexBuffer> m_ImGuiVertexBuffer = nullptr;
		std::shared_ptr<Graphics::IndexBuffer> m_ImGuiIndexBuffer = nullptr;
		Graphics::ImGuiShaderData m_ImGuiShaderData;
		bool m_IsMinimized = false;
	};
}
