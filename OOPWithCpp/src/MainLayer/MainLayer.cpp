#include "Application.hpp"
#include "MainLayer.hpp"
#include "CPURayTracerRenderer.hpp"
#include "CPURayTracer.hpp"
#include "InterLayerData.hpp"
#include "WindowMinimizeEvent.hpp"
#include "WindowRestoreEvent.hpp"

#include <array>
#include <string_view>
#include <memory>


namespace OWC
{
	static u32 framesNeedingUpdatesForRangeInfo = 0;

	MainLayer::MainLayer()
		: m_DynamicRangeInfo({
			.exposure = 1.0f,
			.saturation = 1.0f,
			.HDROn = Graphics::Renderer::GetHDR(),
			.toneMappingSelector = 1, // Force ACES as AGX is broken
			.paperWhiteNits = 203.0f,
			.maxNits = 1000.0f,
			.maxUINits = 203.0f,
			.invGamma = 1.0f / 2.2f
		})
	{
		m_EmptyRenderPass = Graphics::Renderer::GetStaticRenderPass();
		Graphics::Renderer::EndPass(m_EmptyRenderPass);
		framesNeedingUpdatesForRangeInfo = Graphics::Renderer::GetNumberOfFramesInFlight();
	}

	void MainLayer::OnUpdate()
	{
		if (m_ActiveLayer == 0 && !m_IsMinimized)
		{
			std::array<std::string_view, 1> waitSemaphores = { "ImageReady" };
			Graphics::Renderer::SubmitRenderPass(m_EmptyRenderPass, waitSemaphores);
		}
	}

	void MainLayer::ImGuiRender()
	{
		Application& app = Application::GetInstance();

		bool CPURayTracerActive = m_ActiveLayer == 1;
		bool GPURayTracer = m_ActiveLayer == 2;

		ImGui::Begin("mainLayer");

		if (!GPURayTracer)
		{
			if (ImGui::Checkbox("CPU Ray Tracer", &CPURayTracerActive))
			{
				if (CPURayTracerActive)
				{
					auto ILD = std::make_shared<InterLayerData>();
					m_TestLayer = std::make_shared<CPURayTracerRenderer>(ILD);
					m_CPURayTracerLayer = std::make_shared<CPURayTracer>(ILD);

					app.PushLayer(m_TestLayer);
					app.PushLayer(m_CPURayTracerLayer);
					m_ActiveLayer = 1;
				}
				else
				{
					app.PopLayer(m_TestLayer);
					app.PopLayer(m_CPURayTracerLayer);
					m_ActiveLayer = 0;
				}
			}
		}
		if (!CPURayTracerActive)
		{
			if (ImGui::Checkbox("GPU Ray Tracer", &GPURayTracer))
			{
				if (GPURayTracer)
				{
					m_GPURayTracerLayer = std::make_shared<GPURayTracerRenderer>();
					app.PushLayer(m_GPURayTracerLayer);
					m_ActiveLayer = 2;
				}
				else
				{
					app.PopLayer(m_GPURayTracerLayer);
					m_ActiveLayer = 0;
				}
			}
		}
		bool rangeInfoChanged = ImGui::DragFloat("Exposure", &m_DynamicRangeInfo.exposure, m_DynamicRangeInfo.exposure / 100.0f, 0.0f, 10.0f);
		rangeInfoChanged |= ImGui::SliderFloat("Saturation", &m_DynamicRangeInfo.saturation, 0.0f, 2.0f);
		if (m_DynamicRangeInfo.HDROn)
		{
			//ImGui::Checkbox(m_ToneMappingSelector ? "Current: ACES" : "Current: AGX", std::bit_cast<bool*>(&m_ToneMappingSelector));
			rangeInfoChanged |= ImGui::SliderFloat("Paper White (nits)", &m_DynamicRangeInfo.paperWhiteNits, 1.0f, 1000.0f);
			rangeInfoChanged |= ImGui::SliderFloat("Peak Brightness (nits)", &m_DynamicRangeInfo.maxNits, 1.0f, 10000.0f);
			rangeInfoChanged |= ImGui::SliderFloat("UI Brightness (nits)", &m_DynamicRangeInfo.maxUINits, 1.0f, 500.0f);
		}
		else
		{
			f32 gamma = 1.0f / m_DynamicRangeInfo.invGamma;
			rangeInfoChanged |= ImGui::DragFloat("Gamma", &gamma, 0.01f, 0.1f, 5.0f);
			m_DynamicRangeInfo.invGamma = 1.0f / gamma;
		}
		ImGui::End();
		if (m_DynamicRangeInfo.HDROn != Graphics::Renderer::GetHDR())
		{
			m_DynamicRangeInfo.HDROn = Graphics::Renderer::GetHDR();
			rangeInfoChanged = true;
		}
		if (rangeInfoChanged)
			framesNeedingUpdatesForRangeInfo = Graphics::Renderer::GetNumberOfFramesInFlight();
		if (framesNeedingUpdatesForRangeInfo > 0)
		{
			framesNeedingUpdatesForRangeInfo--;
			Graphics::Renderer::GetDynamicRageUniformBuffer()->UpdateBufferData(std::as_bytes(std::span(&m_DynamicRangeInfo, 1)));
		}
	}

	void MainLayer::OnEvent(BaseEvent& event)
	{
		EventDispatcher dispatcher(event);

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
	}


}
