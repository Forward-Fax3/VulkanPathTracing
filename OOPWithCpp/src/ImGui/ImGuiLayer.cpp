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

#include "LoadFile.hpp"


namespace OWC
{
	ImGuiLayer::ImGuiLayer()
	{
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO& io = ImGui::GetIO();

		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

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

		using namespace Graphics;

		std::vector<BindingDescription> fragmentBindingDescriptions = {
			{
				.descriptorCount = 1,
				.binding = 0,
				.descriptorType = DescriptorType::CombinedImageSampler,
				.stageFlags = ShaderType::Fragment
			},
			{
				.descriptorCount = 1,
				.binding = 1,
				.descriptorType = DescriptorType::UniformBuffer,
				.stageFlags = ShaderType::Fragment
			}
		};

		auto shaderSrc = LoadFileToBytecode<u32>("../ShaderSrc/ImGuiShader/ImGui.spv");

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

		std::vector<VertexBindings> vertexBindings = {
			{
				.stride = sizeof(ImDrawVert),
				.binding = 0, // binding
				.inputRate = VertexInputRate::Vertex
			}
		};

		std::vector<VertexAttributes> vertexAttributes = {
			{
				.format = Format::R32G32_SFloat,
				.location = 0,
				.binding = 0,
				.offset = offsetof(ImDrawVert, pos)
			},
			{
				.format = Format::R32G32_SFloat,
				.location = 1,
				.binding = 0,
				.offset = offsetof(ImDrawVert, uv)
			},
			{
				.format = Format::R8G8B8A8_UNorm,
				.location = 2,
				.binding = 0,
				.offset = offsetof(ImDrawVert, col)
			}
		};

		u8* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		m_ImGuiTexture = TextureImage::CreateStorageImage(width, height, Format::R8G8B8A8_UNorm);
		m_ImGuiTexture->UpdateBufferData(std::as_bytes(std::span(pixels, width * height * 4)));
		m_ImGuiVertexBuffer = VertexBuffer::CreateVertexBuffer(sizeof(ImDrawVert));
		m_ImGuiIndexBuffer = IndexBuffer::CreateIndexBuffer();

		m_ImGuiShader = BaseShader::CreateShader(shaderDatas, vertexBindings, vertexAttributes);
		m_ImGuiShader->BindTexture(0, m_ImGuiTexture, DescriptorType::CombinedImageSampler);
		m_ImGuiShader->BindUniform(1, Renderer::GetDynamicRageUniformBuffer());
		m_ImGuiShaderData.shader = m_ImGuiShader;
		m_ImGuiShaderData.renderPassData = m_RenderPassData;
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
	
	void ImGuiLayer::OnEvent(BaseEvent& event)
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

		if (!m_IsMinimized)
		{
			struct { float scale[2]; float translate[2]; } pc{};
			pc.scale[0] = 2.0f / drawData->DisplaySize.x;
			pc.scale[1] = 2.0f / drawData->DisplaySize.y;
			pc.translate[0] = -1.0f - drawData->DisplayPos.x * pc.scale[0];
			pc.translate[1] = -1.0f - drawData->DisplayPos.y * pc.scale[1];

			Renderer::RestartRenderPass(m_RenderPassData);
			Renderer::BeginDynamicPass(m_RenderPassData);
			if (Application::GetConstInstance().IsFirstFrame())
				Renderer::TransitionImage(m_RenderPassData, m_ImGuiTexture, AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
			Renderer::BeginRasterPass(m_RenderPassData);
			Renderer::PipelineBind(m_RenderPassData, *m_ImGuiShader);
			Renderer::BindUniform(m_RenderPassData, *m_ImGuiShader);
			Renderer::BindTexture(m_RenderPassData, *m_ImGuiShader, 0, 0);
			Renderer::PushConstant(m_RenderPassData, *m_ImGuiShader, sizeof(pc), &pc);
			Renderer::DrawImGui(m_RenderPassData, m_ImGuiVertexBuffer, m_ImGuiIndexBuffer, drawData);
			Renderer::EndRasterPass(m_RenderPassData);
			Renderer::EndPass(m_RenderPassData);
			Renderer::SubmitRenderPass(m_RenderPassData, {}, {});
		}

		if (testBitMask(static_cast<uint32_t>(ImGui::GetIO().ConfigFlags), static_cast<uint32_t>(ImGuiConfigFlags_ViewportsEnable)))
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
	}
}
