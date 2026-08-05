#include "Core.hpp"
#include "Application.hpp"
#include "CPURayTracer.hpp"
#include "ImGuiHelpers.hpp"

#include "BaseEvent.hpp"
#include "WindowResize.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <utility>


namespace OWC
{
	CPURayTracer::CPURayTracer(const std::shared_ptr<InterLayerData>& interLayerData)
		: m_InterLayerData(interLayerData)
	{
		m_Camera = std::make_unique<RTCamera>(m_InterLayerData->imageData);
		m_Camera->GetSettings().ScreenSize = Vec2(Application::GetConstInstance().GetPixelSize());
		m_RayTracingStateUpdated = true;
		m_Scene = BaseScene::CreateScene(Scene::Basic);
		m_InterLayerData->numberOfSamples = 1; // Start at 1 to avoid division by zero
	}

	void CPURayTracer::OnUpdate()
	{
		if (m_RayTracingStateUpdated)
		{
			m_RayTracingStateUpdated = false;
			m_Scene->SetBaseCameraSettings(m_Camera->GetSettings());
			m_CameraSettingsUpdated = true;

			m_InterLayerData->imageData.clear();
			m_InterLayerData->imageScreenSize = Application::GetConstInstance().GetPixelSize();
			m_InterLayerData->imageData.resize(m_InterLayerData->GetNumberOfPixels<uSize>());
			m_InterLayerData->numberOfSamples = 0;
			m_InterLayerData->ImageUpdates |= 0b10;
		}

		if (m_CameraSettingsUpdated)
		{
			m_Camera->UpdateCameraSettings();

			for (auto& pixel : m_InterLayerData->imageData)
				pixel = Vec4(0.0f);

			m_InterLayerData->numberOfSamples = 0;
			m_CameraSettingsUpdated = false;
		}

		if (RenderFrame())
		{
			m_LastFrameTime = std::chrono::duration<f32, std::milli>(std::chrono::high_resolution_clock::now() - m_LastTimePoint).count();
			m_LastTimePoint = std::chrono::high_resolution_clock::now();
			m_InterLayerData->numberOfSamples++;
			m_InterLayerData->ImageUpdates |= 0b01;
		}
	}

	void CPURayTracer::ImGuiRender()
	{
		constexpr std::array<const char*, 7> gammaCorrectionNames = {
			"None",
			"Gamma 1.9",
			"Gamma 2.0",
			"Gamma 2.2",
			"Gamma 2.4",
			"BT. 1886",
			"Custom"
		};
		constexpr std::array<const char*, 6> sceneNames = {
			"Basic",
//			"RandTest",
			"DuelGreySpheres",
			"DielectricTest",
			"MetalTest",
			"EarthScene",
			"Book1FinalRender"
		};

		ImGui::Begin("CPU Ray Tracer");
		ImGui::Text("CPU Ray Tracer Layer");
		ImGui::Text(
			"RayTracing time %.3f ms/frame (%.1f FPS)\nnumber of samples %s",
			m_LastFrameTime,
			1000.0f / m_LastFrameTime,
			std::format("{}", m_InterLayerData->numberOfSamples).c_str()
		);

		if (ImGui::Combo("Scene", &m_CurrentSceneIndex, sceneNames.data(), static_cast<i32>(sceneNames.size())))
		{
			auto selectedScene = static_cast<Scene>(m_CurrentSceneIndex);
			m_Scene = BaseScene::CreateScene(selectedScene);
			m_Scene->SetBaseCameraSettings(m_Camera->GetSettings());
			m_CameraSettingsUpdated = true;

			m_InterLayerData->numberOfSamples = 0;
			m_InterLayerData->ImageUpdates |= 0b01;

			for (auto& pixel : m_InterLayerData->imageData)
				pixel = Vec4(0.0f);
		}

		if (ImGui::Combo("Gamma Correction", &m_CurrentGammaIndex, gammaCorrectionNames.data(), static_cast<i32>(gammaCorrectionNames.size())))
		{
			auto gamma = static_cast<GammaCorrection>(m_CurrentGammaIndex);
			if (gamma != GammaCorrection::custom)
				UpdateGammaValue(gamma);
			else
				m_InterLayerData->invGammaValue = 1.0f / m_CustomGammaValue;
		}
		if (static_cast<GammaCorrection>(m_CurrentGammaIndex) == GammaCorrection::custom && ImGui::InputFloat("Custom Gamma Value", &m_CustomGammaValue))
			m_InterLayerData->invGammaValue = 1.0f / m_CustomGammaValue;

		if (ImGui::Button("Reset Camera Position"))
		{
			m_Scene->SetBaseCameraSettings(m_Camera->GetSettings());
			m_CameraSettingsUpdated = true;
		}

		bool useCustomResolutionUpdated = ImGui::Checkbox("Use Window Resolution", &m_UseWindowResolution);

		if (!m_UseWindowResolution)
		{
			Vec2i customResolution(m_InterLayerData->imageScreenSize);
			useCustomResolutionUpdated |= ImGui::OWC::SliderInt2WithAlignedText("Custom Resolution", "Width", "Height", glm::value_ptr(customResolution), 1, 8192, ImGuiSliderFlags_ClampOnInput);

			if (useCustomResolutionUpdated)
			{
				m_InterLayerData->imageScreenSize = customResolution;
				m_Camera->GetSettings().ScreenSize = Vec2(customResolution);

				m_InterLayerData->numberOfSamples = 0;
				m_InterLayerData->ImageUpdates |= 0b10;

				m_CameraSettingsUpdated = true;

				m_InterLayerData->imageData.clear();
				m_InterLayerData->imageData.resize(m_InterLayerData->GetNumberOfPixels<uSize>());
			}
		}
		else if (useCustomResolutionUpdated)
		{
			// scope ordered to reduce number of instructions
			m_InterLayerData->imageScreenSize = Application::GetConstInstance().GetPixelSize();
			m_Camera->GetSettings().ScreenSize = Vec2(m_InterLayerData->imageScreenSize);

			m_InterLayerData->numberOfSamples = 0;
			m_InterLayerData->ImageUpdates |= 0b10;
			m_CameraSettingsUpdated = true;

			m_InterLayerData->imageData.clear();
			m_InterLayerData->imageData.resize(m_InterLayerData->GetNumberOfPixels<uSize>());
		}

		bool isMultiThreadedUpdated = ImGui::Checkbox("Multi-Threaded Rendering", &m_IsMultiThreaded);
		if (isMultiThreadedUpdated)
			m_CameraSettingsUpdated = true;
		ImGui::End();

		// Render Scene Specific ImGui

		CameraRenderSettings& cameraSettings = m_Camera->GetSettings();

		i32 minBouncesBeforeClamp = 0;
		i32 maxBouncesBeforeClamp = 8192;

		ImGui::Begin("Camera Settings");
		m_CameraSettingsUpdated |= ImGui::DragFloat3("Position", glm::value_ptr(cameraSettings.Position), 0.1f);
		m_CameraSettingsUpdated |= ImGui::DragFloat3("Rotation", glm::value_ptr(cameraSettings.Rotation), 0.1f);
		m_CameraSettingsUpdated |= ImGui::DragFloat("FOV", &cameraSettings.FOV, 0.1f, 1.0f, 89.0f);
		m_CameraSettingsUpdated |= ImGui::DragFloat("Focal Length", &cameraSettings.FocalLength, 0.1f, 0.1f, 8192.0f);
		m_CameraSettingsUpdated |= ImGui::DragInt("Max Bounces", &cameraSettings.MaxBounces, 1.0f, minBouncesBeforeClamp, maxBouncesBeforeClamp);
		ImGui::End();

		cameraSettings.MaxBounces = _mm_cvtsi128_si32(_mm_max_epi32(
			_mm_min_epi32(
				_mm_set1_epi32(cameraSettings.MaxBounces),
				_mm_set1_epi32(maxBouncesBeforeClamp)
			),
			_mm_set1_epi32(minBouncesBeforeClamp)
		));
	}

	void CPURayTracer::OnEvent(BaseEvent& e)
	{
		EventDispatcher dispatcher(e);

		dispatcher.Dispatch<WindowResize>([this](const WindowResize&) {
			if (!m_UseWindowResolution)
				return false;

			std::vector<Vec4>& pixelArray = m_InterLayerData->imageData;
			pixelArray.clear();
			m_InterLayerData->imageScreenSize = Application::GetConstInstance().GetPixelSize();
			pixelArray.resize(m_InterLayerData->GetNumberOfPixels<uSize>());
			m_InterLayerData->numberOfSamples = 0;
			m_InterLayerData->ImageUpdates |= 0b10;
			m_Camera->GetSettings().ScreenSize = Vec2(m_InterLayerData->imageScreenSize);
			m_CameraSettingsUpdated = true;
			return false;
		});
	}

	OWC::RenderPassReturnData CPURayTracer::RenderFrame()
	{
		if (m_IsMultiThreaded)
			return m_Camera->MultiThreadedRenderPass(m_Scene->GetHitable());
		else
			return m_Camera->SingleThreadedRenderPass(m_Scene->GetHitable());
	}

	void CPURayTracer::UpdateGammaValue(GammaCorrection gammaCorrection) const
	{
		switch (gammaCorrection)
		{
		case GammaCorrection::None:
			m_InterLayerData->invGammaValue = 1.0f;
			break;
		case GammaCorrection::Gamma19:
			m_InterLayerData->invGammaValue = 1.0f / 1.9f;
			break;
		case GammaCorrection::Gamma20:
			m_InterLayerData->invGammaValue = 1.0f / 2.0f;
			break;
		case GammaCorrection::Gamma22:
			m_InterLayerData->invGammaValue = 1.0f / 2.2f;
			break;
		case GammaCorrection::Gamma24:
		case GammaCorrection::GammaBT1886:
			m_InterLayerData->invGammaValue = 1.0f / 2.4f;
			break;
		case GammaCorrection::custom:
		default:
			std::unreachable();
		}
	}
}