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
		bool m_IsMinimized = false;
	};
}
