//
// Created by forwardfax3 on 15/03/2026.
//

#include "Core.hpp"
#include "OWCRand.hpp"
#include "GPURayTracerRenderer.hpp"
#include "LoadFile.hpp"
#include "Renderer.hpp"
#include "Application.hpp"

#include "BaseEvent.hpp"
#include "WindowMinimizeEvent.hpp"
#include "WindowRestoreEvent.hpp"
#include "WindowResize.hpp"
#include "KeyEvent.hpp"
#include "SDL3/SDL_keycode.h"

#include "SponzaPalace.hpp"
#include "EXTMeshGPUInstancingExample.hpp"
#include "MeshReuseInstancingExample.hpp"

#include <array>

#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>

#include "ImGuiHelpers.hpp"
#include "glm/gtx/euler_angles.hpp"
#include "glm/gtx/norm.inl"


namespace OWC
{
	GPURayTracerRenderer::GPURayTracerRenderer()
		: m_UniformBuffer(Graphics::UniformBuffer::CreateUniformBuffer(sizeof(UniformBufferObject))),
		  m_RayTracingGPUDataBuffer(Graphics::UniformBuffer::CreateUniformBuffer(sizeof(GeneralGPUData))),
		  m_RayTracingWidth(Application::GetConstInstance().GetPixelWidth()),
		  m_RayTracingHeight(Application::GetConstInstance().GetPixelHeight()),
		  m_ScreenNeedsRefreshing(true),
		  m_Scene(std::make_shared<SponzaPalace>())
	{
		SetUpRenderImage();
		SetupPipeline();
		SetupRenderPass();
		CalculateCamera();
	}

	void GPURayTracerRenderer::OnUpdate()
	{
		using namespace OWC::Graphics;

		if (glm::length2(m_KeyPressedOnVec3) > 0.0f) // length squared instead of length to avoid unnecessary square root
		{
			const auto& app = Application::GetConstInstance();
			const float deltaTime = app.GetDeltaTime();
			const Vec3 movePos = m_KeyPressedOnVec3 * m_MoveSpeed * deltaTime;
			m_CameraNeedsRefreshing = Graphics::Renderer::GetNumberOfFramesInFlight(m_RayTracingRenderPass) - 1;
			CalculateCamera(movePos);
		}
		else if (m_CameraNeedsRefreshing != 0)
		{
			m_CameraNeedsRefreshing--;
			CalculateCamera();
		}
		else // CalculateCamera already creates a new random but camera may not be moved so do it on its own
		{
			m_StratifiedPosition = (m_StratifiedPosition + m_NumberOfSamples) % (m_StratifiedGridSize * m_StratifiedGridSize);
			std::array newRandAndStratifiedPos{ Rand::LinearFastRandValue(0u, std::numeric_limits<u32>::max()), m_StratifiedPosition };
			m_RayTracingGPUDataBuffer->UpdateBufferData(std::as_bytes(std::span<const u32>(newRandAndStratifiedPos.data(), newRandAndStratifiedPos.size())), newRandAndStratifiedPos.size(), offsetof(GeneralGPUData, randSeed));
		}

		u32 GPURefreshScreen = false;
		if (m_ScreenNeedsRefreshing || m_Scene->GetNeedScreenRefresh() || !m_AccumulationOn)
		{
			m_ScreenNeedsRefreshing = false;
			m_NumberOfAccumulatedSamples = 0;
			GPURefreshScreen = true;
		}

		m_RayTracingGPUDataBuffer->UpdateBufferData(std::as_bytes(std::span<const u32>(&GPURefreshScreen, 1)), sizeof(GeneralGPUData::GPURefreshScreen), offsetof(GeneralGPUData, GPURefreshScreen));

		m_NumberOfAccumulatedSamples = m_AccumulationOn ? m_NumberOfAccumulatedSamples + m_NumberOfSamples : m_NumberOfSamples;

		UniformBufferObject ubo{
			.divider = 1.0f / static_cast<f32>(m_NumberOfAccumulatedSamples),
			.invGammaValue = 1.0f / 2.2f
		};

		m_UniformBuffer->UpdateBufferData(std::as_bytes(std::span<const UniformBufferObject>(&ubo, 1)));

		std::array<std::string_view, 1> releaseWaitSemaphoreNames = { "ImageReady" };
		std::array<std::string_view, 1> releaseSignalSemaphoreNames = { "ImageRelease" };
		std::array<std::string_view, 1> rayTracerWaitSemaphoreNames = { "ImageRelease" };
		std::array<std::string_view, 1> rayTracerSignalSemaphoreNames = { "RayTracerDone" };
		std::array<std::string_view, 1> displayWaitSemaphoreNames = { "RayTracerDone" };

		Renderer::SubmitRenderPass(m_ImageReleaseRenderPass, releaseWaitSemaphoreNames, releaseSignalSemaphoreNames);
		Renderer::SubmitRenderPass(m_RayTracingRenderPass, rayTracerWaitSemaphoreNames, rayTracerSignalSemaphoreNames);
		Renderer::SubmitRenderPass(m_DisplayRenderPass, displayWaitSemaphoreNames);
	}

	void GPURayTracerRenderer::ImGuiRender()
	{
		constexpr std::array scenes = { "Sponza Palace", "EXT_mesh_gpu_instancing Example", "mesh Reuse Instancing Example" };

		bool cameraMoved = false;
		ImGui::Begin("GPU Renderer");
		ImGui::Text("Number of samples: %zu", m_NumberOfAccumulatedSamples);
		ImGui::Text("Ray tracer");

		if (ImGui::Combo("Scene", &m_SceneIndex, scenes.data(), scenes.size()))
		{
			Graphics::Renderer::AddToEndOfFrameCleanUp([this]()
			{
				Vec3 newPosition{ 0.0f };
				Vec3 newRotation { 0.0f };
				switch (m_SceneIndex)
				{
				case 0:
				{
					this->m_Scene = std::make_shared<SponzaPalace>();
					newPosition = Vec3(0.0f, 1.5f, 0.0f);
					break;
				}
				case 1:
					this->m_Scene = std::make_shared<EXTMeshGPUInstancingExample>();
					newPosition = Vec3(-25.0f, 15.0f, 15.0f);
					newRotation = Vec3(-20.0f, 105.0f, 0.0f);
					break;
				case 2:
					this->m_Scene = std::make_shared<MeshReuseInstancingExample>();
					newPosition = Vec3(-1.0f, 0.2f, 0.0f);
					newRotation = Vec3(0.0f, 90.0f, 0.0f);
					break;
				default:
				{
					this->m_Scene = std::make_shared<SponzaPalace>();
					newPosition = Vec3(0.0f, 1.5f, 0.0f);
					this->m_SceneIndex = 0;
					Log<LogLevel::Warn>("Invalid scene index selected, defaulting to Sponza Palace");
				}
				}
				this->m_CameraRotation = newRotation;
				this->m_CameraPosition = newPosition;
				this->SetupPipeline();
				this->SetupRenderPass();
			});
			cameraMoved = true;
		}

		ImGui::Separator();
		ImGui::Text("Use W, A, S, D, R and F to move camera.");
		ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z);
		ImGui::SliderFloat("Move Speed", &m_MoveSpeed, 0.1f, 10.0f);
		cameraMoved |= ImGui::DragFloat3("Camera Rotation", glm::value_ptr(m_CameraRotation), 0.1f);
		ImGui::Separator();
		cameraMoved |= ImGui::SliderFloat("Horizontal FOV", &m_HFOV, 1.0f, 179.0f);
		if (ImGui::SliderInt("Number of Bounces", &m_NumberOfBounces, 0, 128))
		{
			m_NumberOfBounces = glm::clamp(m_NumberOfBounces, 0, 128);
			m_ScreenNeedsRefreshing = true;
			Graphics::Renderer::AddToEndOfFrameCleanUp([this]()
			{
				this->SetupRenderPass();
			});
		}
		if (ImGui::Checkbox("Use Window Resolution", &m_UseWindowResolution))
        {
			if (m_UseWindowResolution == true)
			{
				m_RayTracingWidth = Application::GetConstInstance().GetPixelWidth();
				m_RayTracingHeight = Application::GetConstInstance().GetPixelHeight();
				m_ScreenNeedsRefreshing = true;
				Graphics::Renderer::AddToEndOfFrameCleanUp([this]()
				{
					this->SetUpRenderImage();
					this->m_RayTracingShader->BindTexture(0, m_RenderTarget);
					this->m_DisplayShader->BindTexture(1, m_RenderTarget);
					this->SetupRenderPass();
				});
			}
        }
		if (!m_UseWindowResolution)
		{
			// some static asserts to asure the position and size of ray tracing width and height will or with the short cuts ive set up
			static_assert(offsetof(GPURayTracerRenderer, m_RayTracingWidth) + sizeof(m_RayTracingWidth) == offsetof(GPURayTracerRenderer, m_RayTracingHeight));
			static_assert(sizeof(m_RayTracingWidth) == sizeof(u32));
			static_assert(sizeof(m_RayTracingHeight) == sizeof(u32));
			if (ImGui::OWC::SliderInt2WithAlignedText("Ray Tracing Resolution", "Width", "Height", &m_RayTracingWidth, 1, 8192, ImGuiSliderFlags_ClampOnInput))
			{
				m_ScreenNeedsRefreshing = true;
				Graphics::Renderer::AddToEndOfFrameCleanUp([this]()
				{
					this->SetUpRenderImage();
					this->m_RayTracingShader->BindTexture(0, m_RenderTarget);
					this->m_DisplayShader->BindTexture(1, m_RenderTarget);
					this->SetupRenderPass();
				});
			}
		}
		constexpr u32 minStratifiedGridSize = 1;
		constexpr u32 maxStratifiedGridSize = 32;
		if (ImGui::SliderScalar("Stratified Grid Size", ImGuiDataType_U32, &m_StratifiedGridSize, &minStratifiedGridSize, &maxStratifiedGridSize, "%d", ImGuiSliderFlags_ClampOnInput))
		{
			Graphics::Renderer::AddToEndOfFrameCleanUp([this]()
			{
				this->SetupRenderPass();
			});
			m_StratifiedPosition = 0;
			m_ScreenNeedsRefreshing = true;
		}
		constexpr u32 minNumberOfSamples = 1;
		constexpr u32 maxNumberOfSamples = 16;
		if (ImGui::SliderScalar("Number Of Samples", ImGuiDataType_U32, &m_NumberOfSamples, &minNumberOfSamples, &maxNumberOfSamples, "%d", ImGuiSliderFlags_ClampOnInput))
		{
			m_ScreenNeedsRefreshing = true;
			m_NumberOfAccumulatedSamples = 0;
			Graphics::Renderer::AddToEndOfFrameCleanUp([this]()
			{
				this->SetupRenderPass();
			});
		}
		if (ImGui::Checkbox("Accumulation On", &m_AccumulationOn))
		{
			m_ScreenNeedsRefreshing = true;
			m_NumberOfAccumulatedSamples = 0;
		}
		ImGui::End();

		if (cameraMoved)
			m_CameraNeedsRefreshing = Graphics::Renderer::GetNumberOfFramesInFlight(m_RayTracingRenderPass);
	}

	void GPURayTracerRenderer::OnEvent(BaseEvent& e)
	{
		EventDispatcher dispatcher(e);

		dispatcher.Dispatch<WindowRestore>([this](const WindowRestore& /*event*/) {
			this->SetUpRenderImage();
			this->SetupPipeline();
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
			if (m_UseWindowResolution == false)
			{
				this->SetupRenderPass();
				return false;
			}
			m_RayTracingWidth = Application::GetConstInstance().GetPixelWidth();
			m_RayTracingHeight = Application::GetConstInstance().GetPixelHeight();
			this->SetUpRenderImage();
			this->m_RayTracingShader->BindTexture(0, m_RenderTarget);
			this->m_DisplayShader->BindTexture(1, m_RenderTarget);
			this->SetupRenderPass();
			m_ScreenNeedsRefreshing = true;
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
		m_RenderTarget = Graphics::TextureBuffer::CreateTextureBuffer(m_RayTracingWidth, m_RayTracingHeight);
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
			u32 StratifiedGridSize;
		};

		const PushConstantData pushConstantData = {
			.megaBuffer = m_Scene->GetDeviceMegaBufferPtr(),
			.GeometryBuffer = m_Scene->GetDeviceGeometryBufferPtr(),
			.MaterialBuffer = m_Scene->GetDeviceMaterialBufferPtr(),
			.LightsBuffer = m_Scene->GetLightBufferPtr(),
			.numberOfLights = m_Scene->GetNumberOfLights(),
			.numberOfBounces = static_cast<u32>(m_NumberOfBounces),
			.numberOfSamples = m_NumberOfSamples,
			.StratifiedGridSize = m_StratifiedGridSize
		};

		using namespace OWC::Graphics;
		m_RayTracingRenderPass = Renderer::GetStaticRayTracingPass();
		Renderer::TransitionImage(m_RayTracingRenderPass, m_RenderTarget, AccessMask::ShaderWrite | AccessMask::ShaderRead, StageMask::RayTracing, ImageLayout::General);
		Renderer::PipelineBind(m_RayTracingRenderPass, *m_RayTracingShader);
		Renderer::PushConstant(m_RayTracingRenderPass, *m_RayTracingShader, sizeof(PushConstantData), &pushConstantData);
		Renderer::BindUniform(m_RayTracingRenderPass, *m_RayTracingShader);
		Renderer::BindTexture(m_RayTracingRenderPass, *m_RayTracingShader, 0, 0);
		Renderer::RayTrace(m_RayTracingRenderPass, *m_RayTracingShader, m_RayTracingWidth, m_RayTracingHeight, 1);
		Renderer::EndPass(m_RayTracingRenderPass);

		m_DisplayRenderPass = Renderer::GetStaticRenderPass();
		Renderer::TransitionImage(m_DisplayRenderPass, m_RenderTarget, AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::General);
		Renderer::BeginRasterPass(m_DisplayRenderPass);
		Renderer::PipelineBind(m_DisplayRenderPass, *m_DisplayShader);
		Renderer::BindUniform(m_DisplayRenderPass, *m_DisplayShader);
		Renderer::BindTexture(m_DisplayRenderPass, *m_DisplayShader, 1, 0);
		Renderer::Draw(m_DisplayRenderPass, 6);
		Renderer::EndRasterPass(m_DisplayRenderPass);
		Renderer::EndPass(m_DisplayRenderPass);

		// this is done last so that the internal state of m_RenderTarget is still neutral
		m_ImageReleaseRenderPass = Renderer::GetStaticRenderPass();
		Renderer::TransitionImage(m_ImageReleaseRenderPass, m_RenderTarget, AccessMask::None, StageMask::TopOfPipe, ImageLayout::General);
		Renderer::EndPass(m_ImageReleaseRenderPass);
	}

	void GPURayTracerRenderer::SetupPipeline()
	{
		using namespace OWC::Graphics;

		{
			std::vector<BindingDescription> bindingDescriptions = {
				{
					.descriptorCount = 1,
					.binding = 0,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::RayGen
				},
				{
					.descriptorCount = 1,
					.binding = 1,
					.descriptorType = DescriptorType::TLAS,
					.stageFlags = (ShaderType::RayGen | ShaderType::RayClosestHit)
				},
				{
					.descriptorCount = 1,
					.binding = 2,
					.descriptorType = DescriptorType::UniformBuffer,
					.stageFlags = ShaderType::RayGen
				}
			};

			std::vector<u32> shaderSrc;

			if (Renderer::HasShaderExecutionSupport())
				shaderSrc = LoadFileToBytecode<u32>("../ShaderSrc/GPURayTracerShaders/RayTracing_SER.spv");
			else
				shaderSrc = LoadFileToBytecode<u32>("../ShaderSrc/GPURayTracerShaders/RayTracing.spv");

			std::vector<ShaderData> shaderDatas = {
				{
					.bytecode = shaderSrc,
					.type = ShaderType::RayGen,
					.language = ShaderData::ShaderLanguage::SPIRV,
					.descriptorType = bindingDescriptions,
					.entryPoint = "RayGenShader",
				},
				{
					.bytecode = shaderSrc,
					.type = ShaderType::RayMiss,
					.language = ShaderData::ShaderLanguage::SPIRV,
					.descriptorType = {},
					.entryPoint = "MissShader",
				},
				{
					.bytecode = shaderSrc,
					.type = ShaderType::RayClosestHit,
					.language = ShaderData::ShaderLanguage::SPIRV,
					.descriptorType = {},
					.entryPoint = "ClosestHitShader",
				},
			};

			m_RayTracingShader = BaseShader::CreateRTShader(shaderDatas);
			m_RayTracingShader->BindTexture(0, m_RenderTarget);
			m_RayTracingShader->BindTLAS(1, m_Scene->GetTLAS());
			m_RayTracingShader->BindUniform(2, m_RayTracingGPUDataBuffer);
		}

		{
			std::vector<BindingDescription> fragmentBindingDescriptions = {
				{
					.descriptorCount = 1,
					.binding = 0,
					.descriptorType = DescriptorType::UniformBuffer,
					.stageFlags = ShaderType::Fragment
				},
				{
					.descriptorCount = 1,
					.binding = 1,
					.descriptorType = DescriptorType::StorageImage,
					.stageFlags = ShaderType::Fragment
				}
			};

			auto shaderSrc = LoadFileToBytecode<u32>("../ShaderSrc/GPURayTracerShaders/DisplayImage.spv");

			std::vector<ShaderData> shaderDatas = {
				{
					.bytecode = shaderSrc,
					.type = ShaderType::Vertex,
					.language = ShaderData::ShaderLanguage::SPIRV,
					.descriptorType = {},
					.entryPoint = "vertexMain"
				},
				{
					.bytecode = shaderSrc,
					.type = ShaderType::Fragment,
					.language = ShaderData::ShaderLanguage::SPIRV,
					.descriptorType = fragmentBindingDescriptions,
					.entryPoint = "fragmentMain"
				}
			};

			m_DisplayShader = BaseShader::CreateShader(shaderDatas);
			m_DisplayShader->BindUniform(0, m_UniformBuffer);
			m_DisplayShader->BindTexture(1, m_RenderTarget);
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

		const Mat4 posMat = glm::translate(Mat4(1.0f), m_CameraPosition);
		const Mat4 invView = posMat * rotMat;

		m_StratifiedPosition = m_NumberOfSamples;
		GeneralGPUData generalGPUData {
			.InvProjection = glm::inverse(projectionMatrix),
			.InvViewMatrix = glm::transpose(invView),
			.randSeed = Rand::LinearFastRandValue(0u, std::numeric_limits<u32>::max()),
			.stratifiedGridPosition = m_StratifiedPosition
		};

		constexpr uSize size = offsetof(GeneralGPUData, GPURefreshScreen); // better for to make sure that there isn't any alignment issues

		m_RayTracingGPUDataBuffer->UpdateBufferData(std::as_bytes(std::span<const GeneralGPUData>(&generalGPUData, 1)), size);
		m_ScreenNeedsRefreshing = true;
	}
}
