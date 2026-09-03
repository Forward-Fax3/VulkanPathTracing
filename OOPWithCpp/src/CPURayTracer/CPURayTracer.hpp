#pragma once
#include "Core.hpp"
#include "Layer.hpp"
#include "InterLayerData.hpp"
#include "ImageLoader.hpp"
#include "Scene.hpp"
#include "Camera.hpp"

#include <memory>
#include <bitset>


namespace OWC
{
	enum class GammaCorrection : u8
	{
		None = 0,
		Gamma19,
		Gamma20,
		Gamma22,
		Gamma24,
		GammaBT1886,
		custom
	};

	class CPURayTracer : public Layer
	{
	public:
		CPURayTracer() = delete;
		explicit CPURayTracer(const std::shared_ptr<InterLayerData>& interLayerData);
		~CPURayTracer() override = default;

		CPURayTracer(const CPURayTracer&) = delete;
		CPURayTracer& operator=(const CPURayTracer&) = delete;
		CPURayTracer(CPURayTracer&&) = delete;
		CPURayTracer&& operator=(CPURayTracer&&) = delete;

		void OnUpdate() override;
		void ImGuiRender() override;
		void OnEvent(BaseEvent& event) override;

	private:
		RenderPassReturnData RenderFrame();

		void UpdateGammaValue(GammaCorrection gammaCorrection) const;

	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> m_LastTimePoint{};
		std::shared_ptr<InterLayerData> m_InterLayerData = nullptr;
		bool m_RayTracingStateUpdated = false;
		bool m_UseWindowResolution = true;
		bool m_CameraSettingsUpdated = true;

		i32	m_CurrentSceneIndex = 0;
		//i32 m_CurrentGammaIndex = 3; // Default to Gamma 2.2
		//f32 m_CustomGammaValue = 2.2f;
		f32 m_LastFrameTime = 0.0f;

		bool m_IsMultiThreaded = false;

		std::unique_ptr<BaseScene> m_Scene = nullptr;
		std::unique_ptr<RTCamera> m_Camera = nullptr;
	};
}
