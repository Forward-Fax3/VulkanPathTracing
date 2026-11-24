#pragma once
#include "VulkanCore.hpp"
#include "VulkanBuffers.hpp"

#include <string>
#include <vector>
#include <span>

#include <vulkan/vulkan.hpp>

#include "BaseShader.hpp"


namespace OWC::Graphics
{
	struct VulkanShaderData
	{
		std::string entryPoint;
		const std::span<const u32>* moduleKey = nullptr;
		vk::ShaderStageFlagBits stage;
		std::vector<BindingDescription> bindingDescriptions;
	};

	class VulkanBaseShader : public BaseShader
	{
	public:
		~VulkanBaseShader() override = default;
		VulkanBaseShader(VulkanBaseShader&) = delete;
		VulkanBaseShader& operator=(VulkanBaseShader&) = delete;
		VulkanBaseShader(VulkanBaseShader&&) noexcept = delete;
		VulkanBaseShader& operator=(VulkanBaseShader&&) noexcept = delete;

		void BindUniform(u32 binding, const std::shared_ptr<UniformBuffer>& uniformBuffer) override;
		void BindTexture(u32 binding, const std::shared_ptr<TextureImage>& textureBuffer, DescriptorType descriptorType) override;
		void BindDynamicTextureImpl(u32 binding, const std::shared_ptr<DynamicTextureBuffer>& dTextureBuffer, DescriptorType descriptorType, i32 frameOffset) override;
		void BindSampler2D(u32 binding, const std::shared_ptr<TextureArraySampler>& sampler) override;
		void BindTextureArray(u32 binding, const std::shared_ptr<TextureArray>& textureArray) override;

		[[nodiscard]] virtual OWC_FORCE_INLINE const vk::raii::Pipeline& GetPipeline() const = 0;
		[[nodiscard]] virtual OWC_FORCE_INLINE const vk::raii::PipelineLayout& GetPipelineLayout() const = 0;
		[[nodiscard]] virtual OWC_FORCE_INLINE const vk::raii::DescriptorSetLayout& GetDescriptorSetLayout() const = 0;
		[[nodiscard]] virtual OWC_FORCE_INLINE const vk::raii::DescriptorSet& GetDescriptorSet() const = 0;
		[[nodiscard]] virtual OWC_FORCE_INLINE const vk::raii::DescriptorSet& GetSpecificDescriptorSet(uSize i) const = 0;

	protected:
		VulkanBaseShader() = default;

		[[nodiscard]] static VulkanShaderData ProcessShaderData(const ShaderData& shaderData, std::map<const std::span<const u32>*, vk::ShaderModuleCreateInfo>& srcToShaderModulesMap);
		[[nodiscard]] static vk::ShaderStageFlagBits ConvertToVulkanShaderStage(ShaderType type);
		[[nodiscard]] static vk::VertexInputRate ConvertToVulkanVertexInputRate(VertexInputRate rate);
		[[nodiscard]] static vk::DescriptorType ConvertToVulkanDescriptorType(DescriptorType type);
	};

	class VulkanShader final : public VulkanBaseShader
	{
	public:
		explicit VulkanShader(const std::span<const ShaderData>& shaderDatas, const std::span<const VertexBindings>& vertexBindings, const std::span<const VertexAttributes>& vertexAttributes);
		VulkanShader(VulkanShader&) = delete;
		VulkanShader& operator=(VulkanShader&) = delete;
		VulkanShader(VulkanShader&&) noexcept = delete;
		VulkanShader& operator=(VulkanShader&&) noexcept = delete;

		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::Pipeline& GetPipeline() const override { return m_Pipeline; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::PipelineLayout& GetPipelineLayout() const override { return m_PipelineLayout; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorSetLayout& GetDescriptorSetLayout() const override { return m_DescriptorSetLayout; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorSet& GetDescriptorSet() const override { return m_DescriptorSets[VulkanCore::GetInstance().GetCurrentFrameIndex()]; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorSet& GetSpecificDescriptorSet(const uSize i) const override { return m_DescriptorSets[i]; }

		void BindTLAS(u32, const std::shared_ptr<BaseTLAS>&) override { Log<LogLevel::Error>("VulkanShader::BindTLAS: TLAS can only be bound to ray tracing shaders! For this shader type please use VulkanRayTracingShader."); }

	private:
		void CreateVulkanPipeline(const std::span<VulkanShaderData>& vulkanShaderDatas, const std::map<const std::span<const u32>*, vk::ShaderModuleCreateInfo>& srcToShaderModulesMap, const std::span<const VertexBindings>& vertexBindings, const std::span<const VertexAttributes>& vertexAttributes);

	private:
		vk::raii::Pipeline m_Pipeline = nullptr;
		vk::raii::PipelineLayout m_PipelineLayout = nullptr;
		vk::raii::DescriptorSetLayout m_DescriptorSetLayout = nullptr;
		vk::raii::DescriptorPool m_DescriptorPool = nullptr;
		std::vector<vk::raii::DescriptorSet> m_DescriptorSets = {};
	};

	class VulkanRayTracingShader final : public VulkanBaseShader
	{
	public:
		explicit VulkanRayTracingShader(const std::span<const ShaderData>& shaderDatas);
		~VulkanRayTracingShader() override = default;

		VulkanRayTracingShader(VulkanRayTracingShader&) = delete;
		VulkanRayTracingShader& operator=(VulkanRayTracingShader&) = delete;
		VulkanRayTracingShader(VulkanRayTracingShader&&) noexcept = delete;
		VulkanRayTracingShader& operator=(VulkanRayTracingShader&&) noexcept = delete;

		[[nodiscard]] OWC_FORCE_INLINE const vk::StridedDeviceAddressRegionKHR& GetRayGenShaderSBTEntry() const { return m_RayGenShaderSBTEntry; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::StridedDeviceAddressRegionKHR& GetHitGroupSBTEntry() const { return m_HitGroupSBTEntry; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::StridedDeviceAddressRegionKHR& GetMissGroupSBTEntry() const { return m_MissGroupSBTEntry; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::StridedDeviceAddressRegionKHR& GetCallableGroupSBTEntry() const { return m_CallableGroupSBTEntry; }

		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::Pipeline& GetPipeline() const override { return m_Pipeline; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::PipelineLayout& GetPipelineLayout() const override { return m_PipelineLayout; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorSetLayout& GetDescriptorSetLayout() const override { return m_DescriptorSetLayout; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorSet& GetDescriptorSet() const override { return m_DescriptorSets[VulkanCore::GetInstance().GetCurrentFrameIndex()]; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorSet& GetSpecificDescriptorSet(const uSize i) const override { return m_DescriptorSets[i]; }

		void BindTLAS(u32 binding, const std::shared_ptr<BaseTLAS>& tlasBuffer) override;

	private:
		void CreateVulkanRayTracingPipeline(const std::span<VulkanShaderData>& vulkanShaderDatas, const std::map<const std::span<const u32>*, vk::ShaderModuleCreateInfo>& srcToShaderModulesMap);
		void CreateShaderBindingTable(u32 numberOfGroups);

	private:
		vk::raii::Pipeline m_Pipeline = nullptr;
		vk::raii::PipelineLayout m_PipelineLayout = nullptr;
		vk::raii::DescriptorSetLayout m_DescriptorSetLayout = nullptr;
		vk::raii::DescriptorPool m_DescriptorPool = nullptr;
		std::vector<vk::raii::DescriptorSet> m_DescriptorSets = {};
		vk::StridedDeviceAddressRegionKHR m_RayGenShaderSBTEntry = {};
		vk::StridedDeviceAddressRegionKHR m_HitGroupSBTEntry = {};
		vk::StridedDeviceAddressRegionKHR m_MissGroupSBTEntry = {};
		vk::StridedDeviceAddressRegionKHR m_CallableGroupSBTEntry = {}; // This won't be implemented yet
		vma::raii::Buffer m_SBTBuffer = nullptr;
		vma::Allocation m_SBTBufferAllocation;
		std::vector<u8> m_ShaderHandles;
		u32 m_ShaderGroupCount = 0;
	};
}
