#include "Core.hpp"

#include "ImGuiLayer.hpp"
#include "Application.hpp"
#include "Renderer.hpp"

#include "BaseEvent.hpp"
#include "WindowResize.hpp"
#include "WindowMinimizeEvent.hpp"
#include "WindowRestoreEvent.hpp"
#include "WindowHDRStateChanged.hpp"

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
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
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

		if (!SetupImGuiRenderData())
			return;
		SetupRenderPass();
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

		dispatcher.Dispatch<WindowRestore>([this](const WindowRestore&) -> bool {
				m_IsMinimized = false;
				return false;
			});

		if (m_IsMinimized)
			return;

		dispatcher.Dispatch<WindowMinimize>([this](const WindowMinimize&) -> bool {
				m_IsMinimized = true;
				return false;
			});

		dispatcher.Dispatch<WindowResize>([this](const WindowResize&) -> bool {
			ImGuiIO& io = ImGui::GetIO();
			const Vec2f32 f32WindowPixelSized(Application::GetInstance().GetPixelSize());
			io.DisplaySize = ImVec2(f32WindowPixelSized.x, f32WindowPixelSized.y);
			if (!SetupImGuiRenderData())
				return true; // program will shut down so there is no need for other layers to handle this now
			SetupRenderPass();
			return false;
		});

		dispatcher.Dispatch<WindowHDRStateChanged>([this](const WindowHDRStateChanged&) -> bool {
			if (!SetupImGuiRenderData())
				return true; // program will shut down so there is no need for other layers to handle this now
			SetupRenderPass();
			return false;
		});
	}
	
	void ImGuiLayer::Begin()
	{
		Application::GetInstance().GetWindow().ImGuiNewFrame();
		ImGui::NewFrame();
		ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
	}
	
	void ImGuiLayer::End()
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
			if (m_IsFontTexture)
			{
				Renderer::TransitionImage(m_RenderPassData, m_ImGuiTexture, AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
				m_IsFontTexture = false;
			}
			Renderer::BeginRasterPass(m_RenderPassData);
			Renderer::PipelineBind(m_RenderPassData, *m_ImGuiShader);
			Renderer::PushConstant(m_RenderPassData, *m_ImGuiShader, sizeof(pc), &pc);
			Renderer::DrawImGui(m_RenderPassData, m_ImGuiVertexBuffer, m_ImGuiIndexBuffer, drawData);
			Renderer::EndRasterPass(m_RenderPassData);
			Renderer::EndPass(m_RenderPassData);
			Renderer::SubmitRenderPass(m_RenderPassData);
		}

		if (testBitMask(std::bit_cast<uint32_t>(ImGui::GetIO().ConfigFlags), static_cast<uint32_t>(ImGuiConfigFlags_ViewportsEnable)))
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
	}

	bool ImGuiLayer::SetupImGuiRenderData()
	{
		using namespace Graphics;

		const ImGuiIO& io = ImGui::GetIO();

		u8* pixels = nullptr;
		int width = 0;
		int height = 0;
		int bytePerPixel = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytePerPixel);
		if (width <= 0 || height <= 0 || bytePerPixel <= 0 || pixels == nullptr)
		{
			Log<LogLevel::Error>("Failed to get ImGui font texture data, will now shut down");
			Application::GetInstance().Stop();
			return false;
		}
		m_ImGuiTexture = TextureImage::CreateStorageImage(width, height, Format::R8G8B8A8_UNorm);
		m_ImGuiTexture->UpdateBufferData(std::as_bytes(std::span{ pixels, static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(bytePerPixel) }));
		m_IsFontTexture = true;

		if (m_ImGuiVertexBuffer.size() != Renderer::GetNumberOfFramesInFlight())
		{
			m_ImGuiVertexBuffer.clear();
			m_ImGuiIndexBuffer.clear();
			m_ImGuiVertexBuffer.reserve(Renderer::GetNumberOfFramesInFlight());
			m_ImGuiIndexBuffer.reserve(Renderer::GetNumberOfFramesInFlight());
			for (uSize i = 0; i < Renderer::GetNumberOfFramesInFlight(); i++)
			{
				m_ImGuiVertexBuffer.emplace_back(VertexBuffer::CreateVertexBuffer(sizeof(ImDrawVert)));
				m_ImGuiIndexBuffer.emplace_back(IndexBuffer::CreateIndexBuffer());
			}
		}
		return true;
	}

	void ImGuiLayer::SetupRenderPass()
	{
		using namespace Graphics;

		static constexpr std::array fragmentBindingDescriptions = {
			BindingDescription {
				.descriptorCount = 1,
				.binding = 0,
				.descriptorType = DescriptorType::CombinedImageSampler,
				.stageFlags = ShaderType::Fragment,
			},
			BindingDescription {
				.descriptorCount = 1,
				.binding = 1,
				.descriptorType = DescriptorType::UniformBuffer,
				.stageFlags = ShaderType::Fragment,
			},
		};

#if defined(__has_embed) && __has_embed("../../../ShaderSrc/ImGuiShader/ImGui.spv") == __STDC_EMBED_FOUND__ && DIST
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wc23-extensions"
#endif
		constexpr unsigned char spvBytes[] = {
#embed "../../../ShaderSrc/ImGuiShader/ImGui.spv"
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
			}
		};

#else
		const auto shaderSrc = LoadFileToBytecode<u32>("ShaderSrc/ImGuiShader/ImGui.spv");

		if (!shaderSrc.has_value() || shaderSrc.value().empty())
		{
			Log<LogLevel::Error>("Failed to load ImGui shader SPIR-V bytecode, will now close back down");
			Application::GetInstance().Stop();
			return;
		}

		const std::array shaderDatas = {
			ShaderData {
				.bytecode = shaderSrc.value(),
				.type = ShaderType::Vertex,
				.language = ShaderData::ShaderLanguage::SPIRV,
				.descriptorType = {},
				.entryPoint = "vertexMain",
			},
			ShaderData {
				.bytecode = shaderSrc.value(),
				.type = ShaderType::Fragment,
				.language = ShaderData::ShaderLanguage::SPIRV,
				.descriptorType = fragmentBindingDescriptions,
				.entryPoint = "fragmentMain",
			},
		};
#endif

		static constexpr std::array vertexBindings = {
			VertexBindings{
				.stride = sizeof(ImDrawVert),
				.binding = 0, // binding
				.inputRate = VertexInputRate::Vertex,
			},
		};

		static constexpr std::array vertexAttributes = {
			VertexAttributes {
				.format = Format::R32G32_SFloat,
				.location = 0,
				.binding = 0,
				.offset = offsetof(ImDrawVert, pos),
			},
			VertexAttributes {
				.format = Format::R32G32_SFloat,
				.location = 1,
				.binding = 0,
				.offset = offsetof(ImDrawVert, uv),
			},
			VertexAttributes {
				.format = Format::R8G8B8A8_UNorm,
				.location = 2,
				.binding = 0,
				.offset = offsetof(ImDrawVert, col),
			},
		};

		m_ImGuiShader = BaseShader::CreateShader(shaderDatas, vertexBindings, vertexAttributes);
		m_ImGuiShader->BindTexture(0, m_ImGuiTexture, DescriptorType::CombinedImageSampler);
		m_ImGuiShader->BindUniform(1, Renderer::GetDynamicRageUniformBuffer());
	}
}
