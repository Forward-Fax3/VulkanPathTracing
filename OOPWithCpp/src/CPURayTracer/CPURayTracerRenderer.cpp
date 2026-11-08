#include "Core.hpp"
#include "CPURayTracerRenderer.hpp"
#include "LoadFile.hpp"
#include "Renderer.hpp"

#include "BaseEvent.hpp"
#include "WindowMinimizeEvent.hpp"
#include "WindowRestoreEvent.hpp"
#include "WindowResize.hpp"

#include <array>

#include <glm/gtc/type_ptr.hpp>


namespace OWC
{
	static bool operator>(const Vec2u& lhs, const u32 rhs)
	{
		return (lhs.x > rhs) && (lhs.y > rhs);
	}
	
	CPURayTracerRenderer::CPURayTracerRenderer(const std::shared_ptr<InterLayerData>& ILD)
		: m_ILD(ILD)
	{
		m_UniformBuffer = Graphics::UniformBuffer::CreateUniformBuffer(sizeof(UniformBufferObject));
		m_Image = Graphics::DynamicTextureBuffer::CreateDynamicTextureBuffer(1, 1, Graphics::Format::R32G32B32A32_SFloat, Graphics::AccessMask::ShaderRead, Graphics::StageMask::FragmentShader, Graphics::ImageLayout::ShaderReadOnlyOptimal);
		const std::vector emptyImageData = { Vec4(0.0f) };
		m_Image->UpdateBufferData(emptyImageData);
		SetupPipeline();
		SetupRenderPass();
	}

	void CPURayTracerRenderer::OnUpdate()
	{
		using namespace OWC::Graphics;

		std::array<std::string_view, 1> waitSemaphoreNames = { "ImageReady" };

		UniformBufferObject ubo{
			.divider = 1.0f / static_cast<f32>((m_ILD->numberOfSamples - 1 == 0) ? 1 : m_ILD->numberOfSamples - 1), // avoid division by zero
		};

		m_UniformBuffer->UpdateBufferData(std::as_bytes(std::span<const UniformBufferObject>(&ubo, 1)));

		if (m_ILD->ImageUpdates.any())
		{
			if (m_ILD->ImageUpdates[1] && m_ILD->imageScreenSize > 0u) // image resize
			{
				Renderer::AddToEndOfFrameCleanUp([image = std::move(m_Image)] -> void {});
				m_Image = DynamicTextureBuffer::CreateDynamicTextureBuffer(m_ILD->imageScreenSize.x, m_ILD->imageScreenSize.y, Format::R32G32B32A32_SFloat, AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
				m_Image->UpdateBufferData(m_ILD->imageData);
				SetupPipeline();
				SetupRenderPass();
			}
			else if (m_ILD->imageScreenSize.x == 0 || m_ILD->imageScreenSize.y == 0) // clear image
			{
				Renderer::AddToEndOfFrameCleanUp([image = std::move(m_Image)] -> void {});
				m_Image = DynamicTextureBuffer::CreateDynamicTextureBuffer(1, 1, Format::R32G32B32A32_SFloat, AccessMask::ShaderRead, StageMask::FragmentShader, ImageLayout::ShaderReadOnlyOptimal);
				const std::vector emptyImageData = { Vec4(0.0f) };
				m_Image->UpdateBufferData(emptyImageData);
				SetupPipeline();
				SetupRenderPass();
			}
			else if (m_ILD->ImageUpdates[0]) // update image
			{
				framesUpdated = 0;

				m_Image->UpdateBufferData(m_ILD->imageData);
				framesUpdated++;
			}

			m_ILD->ImageUpdates.reset();
		}
		else if (framesUpdated != Renderer::GetNumberOfFramesInFlight() && !m_ILD->imageData.empty()) // keep updating image for all frames in flight
		{
			m_Image->UpdateBufferData(m_ILD->imageData);
			framesUpdated++;
		}

		Renderer::SubmitRenderPass(m_renderPass, waitSemaphoreNames);
	}

	void CPURayTracerRenderer::OnEvent(BaseEvent& e)
	{
		EventDispatcher dispatcher(e);

		dispatcher.Dispatch<WindowRestore>([this](const WindowRestore& /*event*/) -> bool {
			this->SetupRenderPass();
			this->SetActiveState(true);
			return false;
			});

		if (!IsActive())			
			return;

		dispatcher.Dispatch<WindowMinimize>([this](const WindowMinimize& /*event*/) -> bool {
			this->SetActiveState(false);
			return false;
			});

		dispatcher.Dispatch<WindowResize>([this](const WindowResize& /*event*/) -> bool {
			this->SetupPipeline();
			this->SetupRenderPass();
			return false;
			});
	}

	void CPURayTracerRenderer::SetupRenderPass()
	{
		using namespace OWC::Graphics;
		constexpr u32 numberOfVertices = 6;

		Renderer::AddToEndOfFrameCleanUp([renderPass = std::move(m_renderPass)] -> void {});
		m_renderPass = Renderer::GetStaticRenderPass();
		Renderer::BeginRasterPass(m_renderPass);
		Renderer::PipelineBind(m_renderPass, *m_Shader);
		Renderer::Draw(m_renderPass, numberOfVertices);
		Renderer::EndRasterPass(m_renderPass);
		Renderer::EndPass(m_renderPass);
	}

	void CPURayTracerRenderer::SetupPipeline()
	{
		using namespace OWC::Graphics;

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
			}
		};

#if defined(__has_embed) && __has_embed("../../../ShaderSrc/CPURayTracerShaders/CPUShader.spv") == __STDC_EMBED_FOUND__ && DIST
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wc23-extensions"
#endif
		static constexpr unsigned char spvBytes[] = {
#embed "../../../ShaderSrc/CPURayTracerShaders/CPUShader.spv"
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
		const auto shaderSrc = LoadFileToBytecode<u32>("ShaderSrc/CPURayTracerShaders/CPUShader.spv");

		if (!shaderSrc.has_value() || shaderSrc.value().empty())
			Log<LogLevel::Critical>("Failed to load CPU shader SPIR-V bytecode"); // TODO: make this fail gracefully and boot the user back to the launch screen

		std::array shaderDatas = {
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

		Renderer::AddToEndOfFrameCleanUp([shader = std::move(m_Shader)] -> void {});
		m_Shader = BaseShader::CreateShader(shaderDatas);
		m_Shader->BindUniform(0, m_UniformBuffer);
		m_Shader->BindUniform(1, Renderer::GetDynamicRageUniformBuffer());
		m_Shader->BindDynamicTexture(2, m_Image, DescriptorType::CombinedImageSampler);
	}
} // namespace OWC
