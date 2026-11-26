//
// Created by forwardfax3 on 15/03/2026.
//

#include "Core.hpp"
#include "Application.hpp"
#include "GPURayTracerRenderer.hpp"
#include "LoadFile.hpp"
#include "OWCRand.hpp"
#include "Renderer.hpp"

#include "BaseEvent.hpp"
#include "KeyEvent.hpp"
#include "SDL3/SDL_keycode.h"
#include "WindowHDRStateChanged.hpp"
#include "WindowMinimizeEvent.hpp"
#include "WindowResize.hpp"
#include "WindowRestoreEvent.hpp"

#include "BistroExterior.hpp"
#include "BistroInterior.hpp"
#include "EXTMeshGPUInstancingExample.hpp"
#include "MeshReuseInstancingExample.hpp"
#include "SponzaPalace.hpp"
//
// #include <array>

#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>

#include "ImGuiHelpers.hpp"
#include "glm/gtx/euler_angles.hpp"
#include "glm/gtx/norm.inl"

#include <NRD.h>

#include "NRDImpl.hpp"

#include "glm/gtc/matrix_inverse.hpp"


namespace OWC
{
	[[nodiscard]] static constexpr f32 Halton(u32 index, const u32 base) noexcept
	{
		f32 f = 1.0f, result = 0.0f;
		while (index > 0)
		{
			f /= static_cast<f32>(base);
			result += f * static_cast<f32>(index % base);
			index /= base;
		}
		return result;
	}

	[[nodiscard]] static Vec2 GetCameraJitter(const u32 frameIndex) noexcept
	{
		constexpr u32 phaseCount = 16;                 // 8–16 at native res
		const u32 i = frameIndex % phaseCount + 1;     // Halton(0) == 0, so start at 1
		return { Halton(i, 2) - 0.5f, Halton(i, 3) - 0.5f };
	}

	GPURayTracerRenderer::GPURayTracerRenderer()
		: m_UniformBuffer(Graphics::UniformBuffer::CreateUniformBuffer(sizeof(UniformBufferObject))),
		  m_RayTracingGPUDataBuffer(Graphics::UniformBuffer::CreateUniformBuffer(sizeof(GeneralGPUData))),
		  m_RayTracingResolution(Application::GetConstInstance().GetPixelWidth(), Application::GetConstInstance().GetPixelHeight()),
		  m_Scene(std::make_shared<MeshReuseInstancingExample>())
	{
		SetUpRenderImage();
		SetupPipeline();
		m_NRDImpl = std::make_unique<NRDImpl>(m_RayTracingResolution, m_Scene->GetSelectedSunLightIndex().has_value());
		SetupRenderPass();
		CalculateCamera();
	}

	void GPURayTracerRenderer::OnUpdate()
	{
		using namespace OWC::Graphics;

		m_PreViewToClip = m_ViewToClip;
		m_PreWorldToView = m_WorldToView;
		m_PreCamJit = m_CamJit;
		m_CamJit = GetCameraJitter(m_FrameIndex);
		m_FrameIndex++;

		const auto& app = Application::GetConstInstance();

		const float deltaTime = app.GetDeltaTime();
		const Vec3 movePos = m_KeyPressedOnVec3 * m_MoveSpeed * deltaTime;
		CalculateCamera(movePos);

		UniformBufferObject ubo {
			.denoisingRange = 500000.f,
			.validationEnable = m_ValidationOn ? 1u : 0u,
			.SigmaOn = m_Scene->GetSelectedSunLightIndex().has_value() ? 1u : 0u,
			.fullscreenPanel = m_FullscreenPanel,
		};

		m_UniformBuffer->UpdateBufferData(std::as_bytes(std::span{ &ubo, 1 }));

		static constexpr std::array rayTracerWaitSemaphoreNames = { std::string_view{ "ImageReady" }, };
		static constexpr std::array rayTracerSignalSemaphoreNames = { std::string_view{ "RayTracerDone" }, };
		static constexpr std::array displayWaitSemaphoreNames = { std::string_view{ "RayTracerDone" }, };

		static constexpr auto cpyMat4 = []<uSize N>(float (&dst)[N], const Mat4& src) noexcept
		{
			static_assert(N == 16, "expected a 4x4 float matrix");
#if AVX512
			_mm512_storeu_ps(dst, _mm512_loadu_ps(glm::value_ptr(src)));
#else
			std::memcpy(dst, glm::value_ptr(src), sizeof(dst));
#endif
		};

		Renderer::SubmitRenderPass(m_RayTracingRenderPass, rayTracerWaitSemaphoreNames);

		nrd::CommonSettings commonSettings {};
		Mat4 nrdViewToClip = m_ViewToClip;
		nrdViewToClip[1][1] *= -1.0f;
		Mat4 nrdViewToClipPrev = m_PreViewToClip;
		nrdViewToClipPrev[1][1] *= -1.0f;
		cpyMat4(commonSettings.viewToClipMatrix, nrdViewToClip);
		cpyMat4(commonSettings.viewToClipMatrixPrev, nrdViewToClipPrev);
		cpyMat4(commonSettings.worldToViewMatrix, m_WorldToView);
		cpyMat4(commonSettings.worldToViewMatrixPrev, m_PreWorldToView);

		commonSettings.frameIndex = m_FrameIndex;

		commonSettings.motionVectorScale[0] = 1.0f / static_cast<f32>(m_RayTracingResolution.x);
		commonSettings.motionVectorScale[1] = 1.0f / static_cast<f32>(m_RayTracingResolution.y);
		commonSettings.motionVectorScale[2] = 1.0f;

		commonSettings.resourceSizePrev[0] = m_RayTracingResolution.x;
		commonSettings.resourceSizePrev[1] = m_RayTracingResolution.y;
		commonSettings.rectSizePrev[0] = m_RayTracingResolution.x;
		commonSettings.rectSizePrev[1] = m_RayTracingResolution.y;

		commonSettings.resourceSize[0] = m_RayTracingResolution.x;
		commonSettings.resourceSize[1] = m_RayTracingResolution.y;
		commonSettings.rectSize[0] = m_RayTracingResolution.x;
		commonSettings.rectSize[1] = m_RayTracingResolution.y;

		commonSettings.cameraJitter[0] = m_CamJit.x;
		commonSettings.cameraJitter[1] = m_CamJit.y;
		commonSettings.cameraJitterPrev[0] = m_PreCamJit.x;
		commonSettings.cameraJitterPrev[1] = m_PreCamJit.y;

		commonSettings.enableValidation = m_ValidationOn;

		commonSettings.timeDeltaBetweenFrames = app.GetDeltaTime();

		if (m_ScreenNeedsRefreshing)
		{
			commonSettings.accumulationMode = nrd::AccumulationMode::CLEAR_AND_RESTART;
			m_ScreenNeedsRefreshing = false;
		}

		m_NRDImpl->SetCommonSettings(commonSettings);
		m_NRDImpl->UpdateAccumulation(app.GetDeltaTime());
		m_NRDImpl->Denoise(m_NRDInputsAndOutputs, {}, rayTracerSignalSemaphoreNames);
		Renderer::SubmitRenderPass(m_DisplayRenderPass, displayWaitSemaphoreNames);
	}

	void GPURayTracerRenderer::ImGuiRender()
	{
		static constexpr std::array scenes = { "mesh Reuse Instancing Example", "EXT_mesh_gpu_instancing Example", "Sponza Palace", "Bistro Exterior", "Bistro Interior" };

		static constexpr u32 kNoPanel = 0xFFFFFFFF;
		struct NrdPanel { const char* name; u32 id; };
		static constexpr std::array kNrdPanels {
			NrdPanel{ .name = "All (4x4 grid)", .id = kNoPanel },
			NrdPanel{ .name = "Noisy diffuse", .id = 12 },
			NrdPanel{ .name = "Denoised diffuse", .id = 13 },
			NrdPanel{ .name = "Noisy specular", .id = 15 },
			NrdPanel{ .name = "Denoised specular", .id = 14 },
			NrdPanel{ .name = "Diffuse factor", .id = 7 },
			NrdPanel{ .name = "Specular factor", .id = 11},
			NrdPanel{ .name = "World units", .id = 0 },
			NrdPanel{ .name = "Roughness", .id = 1 },
			NrdPanel{ .name = "View Z", .id = 2 },
			NrdPanel{ .name = "Motion vectors", .id = 3 },
			NrdPanel{ .name = "Normals", .id = 4 },
			NrdPanel{ .name = "History length", .id = 8 },
			NrdPanel{ .name = "SIGMA light", .id = 6 },
			NrdPanel{ .name = "Noisy shadow", .id = 9 },
			NrdPanel{ .name = "Denoised shadow", .id = 10 },
		};

		ImGui::Begin("GPU Renderer");
		ImGui::Text("Number of samples: %u", m_NumberOfSamples);
		ImGui::Text("Ray tracer");

		if (ImGui::Combo("Scene", &m_SceneIndex, scenes.data(), scenes.size()))
		{
			Graphics::Renderer::AddToEndOfFrameCleanUp([RTS = std::move(m_RayTracingShader), DTS = std::move(m_DisplayShader), RTRR = std::move(m_RayTracingRenderPass), DRR = std::move(m_DisplayRenderPass)](){});

			Vec3 newPosition{ 0.0f };
			Vec3 newRotation { 0.0f };
			switch (m_SceneIndex)
			{
			case 0:
			{
				this->m_Scene = std::make_shared<MeshReuseInstancingExample>();
				newPosition = Vec3(1.0f, 0.2f, 0.0f);
				newRotation = Vec3(0.0f, -90.0f, 0.0f);
				break;
			}
			case 1:
			{
				this->m_Scene = std::make_shared<EXTMeshGPUInstancingExample>();
				newPosition = Vec3(-18.5f, 16.5f, -27.5f);
				newRotation = Vec3(-20.0f, 50.0f, 0.0f);
				break;
			}
			case 2:
			{
				this->m_Scene = std::make_shared<SponzaPalace>();
				newPosition = Vec3(0.5f, 2.0f, 2.0f);
				newRotation = Vec3(-2.5f, 110.0f, 0.0f);
				break;
			}
			case 3:
			{
				this->m_Scene = std::make_shared<BistroExterior>();
				newPosition = Vec3(-21.0f, 3.0f, -3.0f);
				newRotation = Vec3(0.0f, 75.0f, 0.0f);
				break;
			}
			case 4:
			{
				this->m_Scene = std::make_shared<BistroInterior>();
				newPosition = Vec3(5.0f, 3.0f, 10.0f);
				newRotation = Vec3(0.0f, 150.0f, 0.0f);
				break;
			}
			default:
			{
				this->m_Scene = std::make_shared<MeshReuseInstancingExample>();
				newRotation = Vec3(0.0f, 90.0f, 0.0f);
				newPosition = Vec3(-1.0f, 0.2f, 0.0f);
				this->m_SceneIndex = 0;
				Log<LogLevel::Warn>("Invalid scene index selected, defaulting to MeshReuseInstancingExample");
			}
			}
			this->m_CameraRotation = newRotation;
			this->m_CameraPosition = newPosition;
			this->SetupPipeline();
			this->SetupRenderPass();
			this->m_NRDImpl->SetSigma(m_Scene->GetSelectedSunLightIndex().has_value());

			this->m_ScreenNeedsRefreshing = true;
		}

		ImGui::Separator();
		ImGui::Text("Use W, A, S, D, R and F to move camera.");
		ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z);
		ImGui::SliderFloat("Move Speed", &m_MoveSpeed, 0.1f, 10.0f);
		ImGui::DragFloat3("Camera Rotation", glm::value_ptr(m_CameraRotation), 0.1f);
		ImGui::Separator();
		ImGui::SliderFloat("Horizontal FOV", &m_HFOV, 1.0f, 179.0f);
		if (ImGui::SliderInt("Number of Bounces", &m_NumberOfBounces, 0, 128))
		{
			Graphics::Renderer::AddToEndOfFrameCleanUp([RTRR = std::move(m_RayTracingRenderPass), DRR = std::move(m_DisplayRenderPass)](){});
			m_NumberOfBounces = glm::clamp(m_NumberOfBounces, 0, 128);
			m_ScreenNeedsRefreshing = true;
			SetupRenderPass();
		}
		if (ImGui::Checkbox("Use Window Resolution", &m_UseWindowResolution))
        {
			if (m_UseWindowResolution == true)
			{
				Graphics::Renderer::AddToEndOfFrameCleanUp([NRDIAO = std::move(m_NRDInputsAndOutputs), DT = std::move(m_DemodulatedTextures), RTS = std::move(m_RayTracingShader), DTS = std::move(m_DisplayShader), RTRR = std::move(m_RayTracingRenderPass), DRR = std::move(m_DisplayRenderPass), NRD = std::move(m_NRDImpl)](){});
				m_RayTracingResolution.x = Application::GetConstInstance().GetPixelWidth();
				m_RayTracingResolution.y = Application::GetConstInstance().GetPixelHeight();
				m_ScreenNeedsRefreshing = true;
				SetUpRenderImage();
				SetupPipeline();
				m_NRDImpl = std::make_unique<NRDImpl>(m_RayTracingResolution, m_Scene->GetSelectedSunLightIndex().has_value());
				SetupRenderPass();
			}
        }
		if (!m_UseWindowResolution)
		{
			if (ImGui::OWC::SliderInt2WithAlignedText("Ray Tracing Resolution", "Width", "Height", glm::value_ptr(m_RayTracingResolution), 1, 8192, ImGuiSliderFlags_ClampOnInput))
			{
				Graphics::Renderer::AddToEndOfFrameCleanUp([NRDIAO = std::move(m_NRDInputsAndOutputs), DT = std::move(m_DemodulatedTextures), RTS = std::move(m_RayTracingShader), DTS = std::move(m_DisplayShader), RTRR = std::move(m_RayTracingRenderPass), DRR = std::move(m_DisplayRenderPass), NRD = std::move(m_NRDImpl)](){});
				m_ScreenNeedsRefreshing = true;
				SetUpRenderImage();
				SetupPipeline();
				m_NRDImpl = std::make_unique<NRDImpl>(m_RayTracingResolution, m_Scene->GetSelectedSunLightIndex().has_value());
				SetupRenderPass();
			}
		}
		constexpr u32 minNumberOfSamples = 1;
		constexpr u32 maxNumberOfSamples = 16;
		if (ImGui::SliderScalar("Number Of Samples", ImGuiDataType_U32, &m_NumberOfSamples, &minNumberOfSamples, &maxNumberOfSamples, "%d", ImGuiSliderFlags_ClampOnInput))
		{
			Graphics::Renderer::AddToEndOfFrameCleanUp([RTRR = std::move(m_RayTracingRenderPass), DRR = std::move(m_DisplayRenderPass)](){});
			m_ScreenNeedsRefreshing = true;
			SetupRenderPass();
		}
		ImGui::Checkbox("Debug View", &m_ValidationOn);
		ImGui::BeginDisabled(!m_ValidationOn);

		const char* preview = kNrdPanels[0].name;
		for (const auto& [name, id] : kNrdPanels)
			if (id == m_FullscreenPanel)
			{
				preview = name;
				break;
			}

		bool sigmaEnabled = m_Scene->GetSelectedSunLightIndex().has_value();
		std::span panels{ kNrdPanels.data(), kNrdPanels.size() - 3 * !sigmaEnabled };

		if (ImGui::BeginCombo("View", preview))
		{
			for (const auto& [name, id] : panels)
			{
				const bool selected = id == m_FullscreenPanel;
				if (ImGui::Selectable(name, selected))
					m_FullscreenPanel = id;
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		if (!sigmaEnabled && (m_FullscreenPanel == 6 || m_FullscreenPanel == 9 || m_FullscreenPanel == 10))
			m_FullscreenPanel = kNoPanel;

		ImGui::EndDisabled();
		ImGui::End();

		m_NRDImpl->ImGUIRender();
	}

	void GPURayTracerRenderer::OnEvent(BaseEvent& e)
	{
		EventDispatcher dispatcher(e);

		dispatcher.Dispatch<WindowRestore>([this](const WindowRestore& /*event*/) {
			this->SetUpRenderImage();
			this->SetupPipeline();
			m_NRDImpl = std::make_unique<NRDImpl>(m_RayTracingResolution, m_Scene->GetSelectedSunLightIndex().has_value());
			this->SetupRenderPass();
			this->SetActiveState(true);
			m_ScreenNeedsRefreshing = true;
			return false;
			});

		if (!IsActive())
			return;

		dispatcher.Dispatch<WindowMinimize>([this](const WindowMinimize& /*event*/) {
			this->SetActiveState(false);
			return false;
			});

		dispatcher.Dispatch<WindowResize>([this](const WindowResize& /*event*/) {
			m_FirstFrame = true;
			if (m_UseWindowResolution == false)
			{
				this->SetupRenderPass();
				return false;
			}
			m_RayTracingResolution.x = Application::GetConstInstance().GetPixelWidth();
			m_RayTracingResolution.y = Application::GetConstInstance().GetPixelHeight();
			this->SetUpRenderImage();
			this->SetupPipeline();
			m_NRDImpl = std::make_unique<NRDImpl>(m_RayTracingResolution, m_Scene->GetSelectedSunLightIndex().has_value());
			this->SetupRenderPass();
			m_ScreenNeedsRefreshing = true;
			return false;
			});

		dispatcher.Dispatch<WindowHDRStateChanged>([this](const WindowHDRStateChanged& /*event*/) {
			m_FirstFrame = true;
			this->SetupPipeline();
			this->SetupRenderPass();
			return false;
			});

		dispatcher.Dispatch<KeyPressedEvent>([this](const KeyPressedEvent& event) {
			constexpr float scale = 0.001f;

			switch (event.GetKeycode())
			{
			case SDLK_W:
				m_KeyPressedOnVec3 += Vec3(1.0f, 0.0f, 0.0f) * scale;
				break;
			case SDLK_S:
				m_KeyPressedOnVec3 -= Vec3(1.0f, 0.0f, 0.0f) * scale;
				break;
			case SDLK_D:
				m_KeyPressedOnVec3 += Vec3(0.0f, 0.0f, 1.0f) * scale;
				break;
			case SDLK_A:
				m_KeyPressedOnVec3 -= Vec3(0.0f, 0.0f, 1.0f) * scale;
				break;
			case SDLK_R:
				m_KeyPressedOnVec3 -= Vec3(0.0f, 1.0f, 0.0f) * scale;
				break;
			case SDLK_F:
				m_KeyPressedOnVec3 += Vec3(0.0f, 1.0f, 0.0f) * scale;
				break;
			default:
				return false;
			}

			if (glm::length(m_KeyPressedOnVec3) < scale)
				m_KeyPressedOnVec3 = Vec3(0.0f); // set back to 0 completely to avoid floating point precision issues

			return true;
		});

		dispatcher.Dispatch<KeyReleased>([this](const KeyReleased& event) {
			constexpr float scale = 0.001f;

			switch (event.GetKeycode())
			{
			case SDLK_W:
				m_KeyPressedOnVec3 -= Vec3(1.0f, 0.0f, 0.0f) * scale;
				break;
			case SDLK_S:
				m_KeyPressedOnVec3 += Vec3(1.0f, 0.0f, 0.0f) * scale;
				break;
			case SDLK_D:
				m_KeyPressedOnVec3 -= Vec3(0.0f, 0.0f, 1.0f) * scale;
				break;
			case SDLK_A:
				m_KeyPressedOnVec3 += Vec3(0.0f, 0.0f, 1.0f) * scale;
				break;
			case SDLK_R:
				m_KeyPressedOnVec3 += Vec3(0.0f, 1.0f, 0.0f) * scale;
				break;
			case SDLK_F:
				m_KeyPressedOnVec3 -= Vec3(0.0f, 1.0f, 0.0f) * scale;
				break;
			default:
				return false;
			}

			if (glm::length(m_KeyPressedOnVec3) < scale)
				m_KeyPressedOnVec3 = Vec3(0.0f); // set back to 0 completely to avoid floating point precision issues

			return true;
			});
	}

	void GPURayTracerRenderer::SetUpRenderImage() // used in a few places
	{
		using namespace Graphics;

		m_NRDInputsAndOutputs[nrd::ResourceType::IN_MV] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R16G16B16A16_SFloat, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		m_NRDInputsAndOutputs[nrd::ResourceType::IN_NORMAL_ROUGHNESS] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R10G10B10A2_UNorm, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		m_NRDInputsAndOutputs[nrd::ResourceType::IN_VIEWZ] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R32_SFloat, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		m_NRDInputsAndOutputs[nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R16G16B16A16_SFloat, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		m_NRDInputsAndOutputs[nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R16G16B16A16_SFloat, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		m_NRDInputsAndOutputs[nrd::ResourceType::IN_PENUMBRA] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R16_SFloat, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);

		m_NRDInputsAndOutputs[nrd::ResourceType::OUT_VALIDATION] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R16G16B16A16_SFloat, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);

		m_NRDInputsAndOutputs[nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R16G16B16A16_SFloat, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		m_NRDInputsAndOutputs[nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R16G16B16A16_SFloat, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		m_NRDInputsAndOutputs[nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY] = TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Format::R16_SFloat, AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);

		m_DemodulatedTextures.clear();
		m_DemodulatedTextures.reserve(4);
		m_DemodulatedTextures.emplace_back(Graphics::TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Graphics::Format::R16G16B16A16_SFloat));
		m_DemodulatedTextures.emplace_back(Graphics::TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Graphics::Format::R16G16B16A16_SFloat));
		m_DemodulatedTextures.emplace_back(Graphics::TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Graphics::Format::R16G16B16A16_SFloat));
		m_DemodulatedTextures.emplace_back(Graphics::TextureImage::CreateStorageImage(m_RayTracingResolution.x, m_RayTracingResolution.y, Graphics::Format::R16G16B16A16_SFloat));
	}

	void GPURayTracerRenderer::SetupRenderPass()
	{
		struct PushConstantData
		{
			uSize megaBuffer;
			uSize GeometryBuffer;
			uSize MaterialBuffer;
			uSize LightsBuffer;
			u32 numberOfLights;
			u32 numberOfBounces;
			u32 numberOfSamples;
			i32 SigmaLightIndex;
		};

		const PushConstantData pushConstantData = {
			.megaBuffer = m_Scene->GetDeviceMegaBufferPtr(),
			.GeometryBuffer = m_Scene->GetDeviceGeometryBufferPtr(),
			.MaterialBuffer = m_Scene->GetDeviceMaterialBufferPtr(),
			.LightsBuffer = m_Scene->GetLightBufferPtr(),
			.numberOfLights = m_Scene->GetNumberOfLights(),
			.numberOfBounces = static_cast<u32>(m_NumberOfBounces),
			.numberOfSamples = m_NumberOfSamples,
			.SigmaLightIndex = m_Scene->GetSelectedSunLightIndex().has_value() ? static_cast<i32>(m_Scene->GetSelectedSunLightIndex().value().first) : -1,
		};

		SetSigmaLightDirection();

		using namespace OWC::Graphics;
		m_RayTracingRenderPass = Renderer::GetStaticRayTracingPass();
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_VIEWZ), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_MV), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_NORMAL_ROUGHNESS), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_PENUMBRA), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_DemodulatedTextures.at(0), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_DemodulatedTextures.at(1), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_DemodulatedTextures.at(2), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_DemodulatedTextures.at(3), AccessMask::ShaderWrite, StageMask::RayTracing, ImageLayout::General);
		Renderer::PipelineBind(m_RayTracingRenderPass, *m_RayTracingShader);
		Renderer::PushConstant(m_RayTracingRenderPass, *m_RayTracingShader, sizeof(PushConstantData), &pushConstantData);
		Renderer::RayTrace(m_RayTracingRenderPass, *m_RayTracingShader, m_RayTracingResolution.x, m_RayTracingResolution.y, 1);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_VIEWZ), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_MV), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_NORMAL_ROUGHNESS), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_RayTracingRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_PENUMBRA), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::EndPass(m_RayTracingRenderPass);

		// NRD pass

		m_DisplayRenderPass = Renderer::GetStaticRenderPass();
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_VIEWZ), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_PENUMBRA), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_VALIDATION), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_DemodulatedTextures.at(0), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_DemodulatedTextures.at(1), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_DemodulatedTextures.at(2), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::TransitionImage(m_DisplayRenderPass, m_DemodulatedTextures.at(3), AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
		Renderer::BeginRasterPass(m_DisplayRenderPass);
		Renderer::PipelineBind(m_DisplayRenderPass, *m_DisplayShader);
		Renderer::Draw(m_DisplayRenderPass, 6);
		Renderer::EndRasterPass(m_DisplayRenderPass);
		Renderer::TransitionImage(m_DisplayRenderPass, m_DemodulatedTextures.at(0), AccessMask::None, StageMask::TopOfPipe, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_DemodulatedTextures.at(1), AccessMask::None, StageMask::TopOfPipe, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_DemodulatedTextures.at(2), AccessMask::None, StageMask::TopOfPipe, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_DemodulatedTextures.at(3), AccessMask::None, StageMask::TopOfPipe, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_VIEWZ), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_PENUMBRA), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_VALIDATION), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::TransitionImage(m_DisplayRenderPass, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY), AccessMask::ShaderRead, StageMask::ComputeShader, ImageLayout::General);
		Renderer::EndPass(m_DisplayRenderPass);
	}

	void GPURayTracerRenderer::SetupPipeline()
	{
		using namespace OWC::Graphics;

		{
			static constexpr std::array bindingDescriptions = {
				BindingDescription {
					.descriptorCount = 1,
					.binding = 0,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayClosestHit | ShaderType::RayMiss,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 1,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayClosestHit | ShaderType::RayMiss,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 2,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayClosestHit | ShaderType::RayMiss,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 3,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayGen,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 4,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayGen,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 5,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayClosestHit,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 6,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayClosestHit | ShaderType::RayMiss,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 7,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayClosestHit | ShaderType::RayMiss,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 8,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayClosestHit | ShaderType::RayMiss,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 9,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayClosestHit,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 10,
					.descriptorType = DescriptorType::TLAS,
					.stageFlags = ShaderType::RayGen | ShaderType::RayClosestHit | ShaderType::RayAnyHit,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 11,
					.descriptorType = DescriptorType::UniformBuffer,
					.stageFlags = ShaderType::RayGen | ShaderType::RayAnyHit | ShaderType::RayClosestHit | ShaderType::RayMiss,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 12,
					.descriptorType = DescriptorType::Sampler,
					.stageFlags = ShaderType::RayClosestHit | ShaderType::RayAnyHit,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 13,
					.descriptorType = DescriptorType::SampledImage,
					.stageFlags = ShaderType::RayClosestHit | ShaderType::RayAnyHit,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 14,
					.descriptorType = DescriptorType::SampledImage,
					.stageFlags = ShaderType::RayClosestHit,
				},
				 BindingDescription{
					.descriptorCount = 1,
					.binding = 15,
					.descriptorType = DescriptorType::SampledImage,
					.stageFlags = ShaderType::RayClosestHit,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 16,
					.descriptorType = DescriptorType::SampledImage,
					.stageFlags = ShaderType::RayClosestHit,
				},
			};

			static constexpr auto makeDatas = [][[nodiscard]](const std::span<const u32> code) constexpr noexcept -> auto {
				return std::array {
					ShaderData {
						.bytecode = code,
						.type = ShaderType::RayGen,
						.language = ShaderData::ShaderLanguage::SPIRV,
						.descriptorType = bindingDescriptions,
						.entryPoint = "RayGenShader",
					},
					ShaderData {
						.bytecode = code,
						.type = ShaderType::RayMiss,
						.language = ShaderData::ShaderLanguage::SPIRV,
						.descriptorType = {},
						.entryPoint = "MissShader",
					},
					ShaderData {
						.bytecode = code,
						.type = ShaderType::RayClosestHit,
						.language = ShaderData::ShaderLanguage::SPIRV,
						.descriptorType = {},
						.entryPoint = "ClosestHitShader",
					},
					ShaderData {
						.bytecode = code,
						.type = ShaderType::RayAnyHit,
						.language = ShaderData::ShaderLanguage::SPIRV,
						.descriptorType = {},
						.entryPoint = "AnyHitShader",
					},
				};
			};

#if defined(__has_embed) && __has_embed("../../../ShaderSrc/GPURayTracerShaders/RayTracing.spv") == __STDC_EMBED_FOUND__ && __has_embed("../../../ShaderSrc/GPURayTracerShaders/RayTracing_SER.spv") == __STDC_EMBED_FOUND__ && DIST
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wc23-extensions"
#endif
			static constexpr unsigned char spvBytesSER[] = {
#embed "../../../ShaderSrc/GPURayTracerShaders/RayTracing_SER.spv"
			};
			static constexpr unsigned char spvBytes[] = {
#embed "../../../ShaderSrc/GPURayTracerShaders/RayTracing.spv"
			};
#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

			static constexpr auto shaderSrcSER = spvBytesToDWords(spvBytesSER);
			static constexpr auto shaderSrc    = spvBytesToDWords(spvBytes);
			static_assert(shaderSrcSER[0] == 0x07230203 && shaderSrc[0] == 0x07230203, "bad SPIR-V magic");

			static constexpr auto shaderDatasSER = makeDatas(shaderSrcSER);
			static constexpr auto shaderDatas    = makeDatas(shaderSrc);

			m_RayTracingShader = BaseShader::CreateRTShader(Renderer::HasShaderExecutionReorderingSupport() ? shaderDatasSER : shaderDatas);
#else
			static constexpr std::string_view shaderPathSER = "ShaderSrc/GPURayTracerShaders/RayTracing_SER.spv";
			static constexpr std::string_view shaderPath = "ShaderSrc/GPURayTracerShaders/RayTracing.spv";

			const auto shaderSrc = LoadFileToBytecode<u32>(std::string{ Renderer::HasShaderExecutionReorderingSupport() ? shaderPathSER : shaderPath });
			if (!shaderSrc.has_value())
				Log<LogLevel::Critical>("Failed to load ray tracing shader SPIR-V bytecode"); // TODO: make this fail gracefully an boot the user back to the launch screen
			const auto shaderDatas = makeDatas(shaderSrc.value());
			m_RayTracingShader = BaseShader::CreateRTShader(shaderDatas);
#endif

			m_RayTracingShader->BindTexture(0, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_VIEWZ), DescriptorType::StorageImage);
			m_RayTracingShader->BindTexture(1, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_MV), DescriptorType::StorageImage);
			m_RayTracingShader->BindTexture(2, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_NORMAL_ROUGHNESS), DescriptorType::StorageImage);
			m_RayTracingShader->BindTexture(3, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST), DescriptorType::StorageImage);
			m_RayTracingShader->BindTexture(4, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST), DescriptorType::StorageImage);
			m_RayTracingShader->BindTexture(5, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_PENUMBRA), DescriptorType::StorageImage);
			m_RayTracingShader->BindTexture(6, m_DemodulatedTextures.at(0), DescriptorType::StorageImage);
			m_RayTracingShader->BindTexture(7, m_DemodulatedTextures.at(1), DescriptorType::StorageImage);
			m_RayTracingShader->BindTexture(8, m_DemodulatedTextures.at(2), DescriptorType::StorageImage);
			m_RayTracingShader->BindTexture(9, m_DemodulatedTextures.at(3), DescriptorType::StorageImage);
			m_RayTracingShader->BindTLAS(10, m_Scene->GetTLAS());
			m_RayTracingShader->BindUniform(11, m_RayTracingGPUDataBuffer);
			m_RayTracingShader->BindSampler2D(12, m_Scene->GetTextureArraySampler());
			m_RayTracingShader->BindTextureArray(13, m_Scene->GetColourTextureArray());
			m_RayTracingShader->BindTextureArray(14, m_Scene->GetNormalTextureArray());
			m_RayTracingShader->BindTextureArray(15, m_Scene->GetMetallicRoughnessTextureArray());
			m_RayTracingShader->BindTextureArray(16, m_Scene->GetEmissiveTextureArray());
		}

		{
			static constexpr std::array fragmentBindingDescriptions = {
				BindingDescription {
					.descriptorCount = 1,
					.binding = 0,
					.descriptorType = DescriptorType::UniformBuffer,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 1,
					.descriptorType = DescriptorType::UniformBuffer,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 2,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 3,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 4,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 5,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 6,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 7,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 8,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 9,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 10,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 11,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 12,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
				BindingDescription {
					.descriptorCount = 1,
					.binding = 13,
					.descriptorType = DescriptorType::CombinedImageSampler,
					.stageFlags = ShaderType::Fragment,
				},
			};

#if defined(__has_embed) && __has_embed("../../../ShaderSrc/GPURayTracerShaders/DisplayImage.spv") == __STDC_EMBED_FOUND__ && DIST
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wc23-extensions"
#endif
			static constexpr unsigned char spvBytes[] = {
#embed "../../../ShaderSrc/GPURayTracerShaders/DisplayImage.spv"
			};
#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

			static constexpr auto shaderSrc = spvBytesToDWords(spvBytes);
			static_assert(shaderSrc[0] == 0x07230203, "bad SPIR-V magic");

			static constexpr std::array shaderDatas = {
				ShaderData {
					.bytecode = shaderSrc,
					.type = ShaderType::Vertex,
					.language = ShaderData::ShaderLanguage::SPIRV,
					.descriptorType = {},
					.entryPoint = "vertexMain",
				},
				ShaderData {
					.bytecode = shaderSrc,
					.type = ShaderType::Fragment,
					.language = ShaderData::ShaderLanguage::SPIRV,
					.descriptorType = fragmentBindingDescriptions,
					.entryPoint = "fragmentMain",
				},
			};
#else
			auto shaderSrc = LoadFileToBytecode<u32>("ShaderSrc/GPURayTracerShaders/DisplayImage.spv");

			if (!shaderSrc.has_value())
				Log<LogLevel::Critical>("Failed to load ray tracing display shader SPIR-V bytecode"); // TODO: make this fail gracefully and boot the user back to the launch screen

			const std::array shaderDatas = {
				ShaderData {
					.bytecode = shaderSrc.value(),
					.type = ShaderType::Vertex,
					.language = ShaderData::ShaderLanguage::SPIRV,
					.descriptorType = {},
					.entryPoint = "vertexMain"
				},
				ShaderData {
					.bytecode = shaderSrc.value(),
					.type = ShaderType::Fragment,
					.language = ShaderData::ShaderLanguage::SPIRV,
					.descriptorType = fragmentBindingDescriptions,
					.entryPoint = "fragmentMain"
				}
			};
#endif

			m_DisplayShader = BaseShader::CreateShader(shaderDatas);
			m_DisplayShader->BindUniform(0, m_UniformBuffer);
			m_DisplayShader->BindUniform(1, Renderer::GetDynamicRageUniformBuffer());
			m_DisplayShader->BindTexture(2, m_DemodulatedTextures.at(0), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(3, m_DemodulatedTextures.at(1), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(4, m_DemodulatedTextures.at(2), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(5, m_DemodulatedTextures.at(3), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(6, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_VIEWZ), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(7, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(8, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(9, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(10, m_NRDInputsAndOutputs.at(nrd::ResourceType::OUT_VALIDATION), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(11, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(12, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST), DescriptorType::CombinedImageSampler);
			m_DisplayShader->BindTexture(13, m_NRDInputsAndOutputs.at(nrd::ResourceType::IN_PENUMBRA), DescriptorType::CombinedImageSampler);
		}
	}

	void GPURayTracerRenderer::SetSigmaLightDirection() const
	{
		if (m_Scene->GetSelectedSunLightIndex().has_value())
		{
			const Vec3p toSun = glm::normalize(m_Scene->GetSelectedSunLightIndex().value().second);
			m_NRDImpl->SetSigmaLightDirection(toSun);
		}
	}

	void GPURayTracerRenderer::CalculateCamera(const Vec3& movement /*= Vec3(0.0f)*/)
	{
		const auto& app = Application::GetConstInstance();
		const auto aspect = static_cast<f32>(app.GetPixelWidth()) / static_cast<f32>(app.GetPixelHeight());
		const float VFOV = 2.0f * glm::atan(glm::tan(glm::radians(m_HFOV) / 2.0f) / aspect);
		const auto projectionMatrix = glm::perspective(VFOV, aspect, 0.1f, 100.0f);

		const Vec3 rotation = Vec3(-1.0, 1.0, 1.0) * glm::radians(m_CameraRotation) + Vec3(glm::pi<f32>(), 0.0f, 0.0f);
		const Mat4 rotMat = glm::eulerAngleYXZ(rotation.y, rotation.x, rotation.z);

		const auto forward = glm::normalize(Vec3(rotMat * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));
		const auto right = glm::normalize(Vec3(rotMat * Vec4(1.0f, 0.0f, 0.0f, 0.0f)));
		const auto up = glm::normalize(Vec3(rotMat * Vec4(0.0f, 1.0f, 0.0f, 0.0f)));

		m_CameraPosition += forward * movement.x;
		m_CameraPosition += up * movement.y;
		m_CameraPosition += right * movement.z;

		Mat4 mat{ 1.0f };
		mat[2][2] = -1;
		const Mat4 posMat = glm::translate(mat, m_CameraPosition);
		const Mat4 invView = posMat * rotMat;

		m_WorldToView = glm::inverse(invView);
		m_ViewToClip = projectionMatrix;
		m_StratifiedPosition += m_NumberOfSamples;
		const f32 sunAngularRadius = glm::radians(0.2665f);
		const GeneralGPUData generalGPUData {
			.InvProjection = glm::inverseTranspose(projectionMatrix),
			.InvViewMatrix = glm::transpose(invView),
			.WorldToClip = glm::transpose(m_ViewToClip * m_WorldToView),
			.PreWorldToClip = glm::transpose(m_PreViewToClip * m_PreWorldToView),
			.WorldToView = glm::transpose(m_WorldToView),
			.PreWorldToView = glm::transpose(m_PreWorldToView),
			.randSeed = Rand::LinearFastRandValue(0u, std::numeric_limits<u32>::max()),
			.frameIndex = m_FrameIndex,
			.camJit = m_CamJit,
			.tanSunAngularRadius = std::tan(sunAngularRadius),
			.cosSunAngularRadius = std::cos(sunAngularRadius),
		};

		m_RayTracingGPUDataBuffer->UpdateBufferData(std::as_bytes(std::span(&generalGPUData, 1)), sizeof(GeneralGPUData));
	}
}
