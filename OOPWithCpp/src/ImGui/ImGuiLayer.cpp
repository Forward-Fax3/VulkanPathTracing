#include "Core.hpp"

#include "ImGuiLayer.hpp"
#include "Application.hpp"
#include "Renderer.hpp"

#include "BaseEvent.hpp"
#include "WindowResize.hpp"
#include "WindowMinimizeEvent.hpp"
#include "WindowRestoreEvent.hpp"

#include <imgui.h>
#include <chrono>


namespace OWC
{
	ImGuiLayer::ImGuiLayer()
	{
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO& io = ImGui::GetIO();

		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		// io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		const Vec2f32 windowPixelSize(Application::GetInstance().GetPixelSize());
		io.DisplaySize = ImVec2(windowPixelSize.x, windowPixelSize.y);

		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		Application::GetInstance().GetWindow().ImGuiInit();
		m_RenderPassData = Graphics::Renderer::GetDynamicPass();
	}

	ImGuiLayer::~ImGuiLayer()
	{
		Application::GetInstance().GetWindow().ImGuiShutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::ImGuiRender()
	{
		const auto& app = Application::GetConstInstance();

		ImGui::Begin("Frame rate");
		ImGui::Text("Delta Time: %.3f ms", app.GetDeltaTime());
		ImGui::Text("FPS: %2.1f", 1000.0f / app.GetDeltaTime());
		ImGui::End();
	}
	
	void ImGuiLayer::OnEvent(BaseEvent& e)
	{
		EventDispatcher dispatcher(e);

		dispatcher.Dispatch<WindowRestore>([this](const WindowRestore&)
			{
				m_IsMinimized = false;
				return false;
			});

		if (m_IsMinimized)
			return;

		dispatcher.Dispatch<WindowMinimize>([this](const WindowMinimize&)
			{
				m_IsMinimized = true;
				return false;
			});

		dispatcher.Dispatch<WindowResize>([](const WindowResize&)
			{
				ImGuiIO& io = ImGui::GetIO();
				const Vec2f32 f32WindowPixelSized(Application::GetInstance().GetPixelSize());
				io.DisplaySize = ImVec2(f32WindowPixelSized.x, f32WindowPixelSized.y);
				return false;
			});
	}
	
	void ImGuiLayer::Begin()
	{
		Application::GetInstance().GetWindow().ImGuiNewFrame();
		ImGui::NewFrame();
		ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
	}
	
	void ImGuiLayer::End() const
	{
		using namespace Graphics;
		ImGui::Render();
		ImDrawData* drawData = ImGui::GetDrawData();

		static bool firstFrame = true;

		if (!firstFrame)
			Renderer::RestartRenderPass(m_RenderPassData);

		if (!m_IsMinimized)
		{
			Renderer::BeginDynamicPass(m_RenderPassData);
			Renderer::BeginRasterPass(m_RenderPassData);
			Renderer::DrawImGui(m_RenderPassData, drawData);
			Renderer::EndRasterPass(m_RenderPassData);
			Renderer::EndPass(m_RenderPassData);
			Renderer::SubmitRenderPass(m_RenderPassData, {}, {});
		}

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		firstFrame = false;
	}
}