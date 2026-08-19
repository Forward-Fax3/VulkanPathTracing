#pragma once
#include "VulkanCore.hpp"
#include "VulkanUniformBuffer.hpp"

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
		std::vector<u32>* moduleKey = nullptr;
		vk::ShaderStageFlagBits stage;
		std::vector<BindingDescription> bindingDescriptions;
	};

	class VulkanBaseShader : public BaseShader
	{
	public:
		VulkanBaseShader() = default;
		~VulkanBaseShader() override = default;
		VulkanBaseShader(VulkanBaseShader&) = delete;
		VulkanBaseShader& operator=(VulkanBaseShader&) = delete;
		VulkanBaseShader(VulkanBaseShader&&) noexcept = delete;
		VulkanBaseShader& operator=(VulkanBaseShader&&) noexcept = delete;

		void BindUniform(u32 binding, const std::shared_ptr<UniformBuffer>& uniformBuffer) override;
		void BindTexture(u32 binding, const std::shared_ptr<TextureBuffer>& textureBuffer) override;
		void BindDynamicTexture(u32 binding, const std::shared_ptr<DynamicTextureBuffer>& dTextureBuffer) override;
		void BindSampler2D(u32 binding, const std::shared_ptr<TextureArraySampler>& sampler) override;
		void BindTextureArray(u32 binding, const std::shared_ptr<TextureArray>& textureArray) override;

		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::Pipeline& GetPipeline() const { return m_Pipeline; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::PipelineLayout& GetPipelineLayout() const { return m_PipelineLayout; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorSetLayout& GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorSet& GetDescriptorSet() const { return m_DescriptorSets[VulkanCore::GetConstInstance().GetCurrentFrameIndex()]; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorSet& GetSpecificDescriptorSet(const uSize i) const { return m_DescriptorSets[i]; }

	protected:
		[[nodiscard]] OWC_FORCE_INLINE vk::DescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }

		void OWC_FORCE_INLINE SetPipeline(vk::raii::Pipeline&& pipeline) { m_Pipeline = std::move(pipeline); }
		void OWC_FORCE_INLINE SetPipelineLayout(vk::raii::PipelineLayout&& pipelineLayout) { m_PipelineLayout = std::move(pipelineLayout); }
		void OWC_FORCE_INLINE SetDescriptorSetLayout(vk::raii::DescriptorSetLayout&& descriptorSetLayout) { m_DescriptorSetLayout = std::move(descriptorSetLayout); }
		void OWC_FORCE_INLINE SetDescriptorPool(vk::raii::DescriptorPool&& descriptorPool) { m_DescriptorPool = std::move(descriptorPool); }
		void OWC_FORCE_INLINE SetDescriptorSets(std::vector<vk::raii::DescriptorSet>&& descriptorSet) { m_DescriptorSets = std::move(descriptorSet); }

		[[nodiscard]] static VulkanShaderData ProcessShaderData(const ShaderData& shaderData, std::map<std::vector<u32>*, vk::ShaderModuleCreateInfo>& srcToShaderModulesMap);
		[[nodiscard]] static vk::ShaderStageFlagBits ConvertToVulkanShaderStage(ShaderType type);
		[[nodiscard]] static vk::DescriptorType ConvertToVulkanDescriptorType(DescriptorType type);

	private:
		vk::raii::Pipeline m_Pipeline = nullptr;
		vk::raii::PipelineLayout m_PipelineLayout = nullptr;
		vk::raii::DescriptorSetLayout m_DescriptorSetLayout = nullptr;
		vk::raii::DescriptorPool m_DescriptorPool = nullptr;
		std::vector<vk::raii::DescriptorSet> m_DescriptorSets = {};
	};

	class VulkanShader : public VulkanBaseShader
	{
	public:
		explicit VulkanShader(const std::span<ShaderData>& shaderDatas);
		VulkanShader(VulkanShader&) = delete;
		VulkanShader& operator=(VulkanShader&) = delete;
		VulkanShader(VulkanShader&&) noexcept = delete;
		VulkanShader& operator=(VulkanShader&&) noexcept = delete;

		void BindTLAS(u32 binding, const std::shared_ptr<BaseTLAS>& tlasBuffer) override { Log<LogLevel::Error>("VulkanShader::BindTLAS: TLAS can only be bound to ray tracing shaders! For this shader type please use VulkanRayTracingShader."); }

	private:
		void CreateVulkanPipeline(const std::span<VulkanShaderData>& vulkanShaderDatas, const std::map<std::vector<u32>*, vk::ShaderModuleCreateInfo>& srcToShaderModulesMap);
	};

	class VulkanRayTracingShader : public VulkanBaseShader
	{
	public:
		explicit VulkanRayTracingShader(const std::span<ShaderData>& shaderDatas);
		~VulkanRayTracingShader() override = default;

		VulkanRayTracingShader(VulkanRayTracingShader&) = delete;
		VulkanRayTracingShader& operator=(VulkanRayTracingShader&) = delete;
		VulkanRayTracingShader(VulkanRayTracingShader&&) noexcept = delete;
		VulkanRayTracingShader& operator=(VulkanRayTracingShader&&) noexcept = delete;

		[[nodiscard]] OWC_FORCE_INLINE const vk::StridedDeviceAddressRegionKHR& GetRayGenShaderSBTEntry() const { return m_RayGenShaderSBTEntry; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::StridedDeviceAddressRegionKHR& GetHitGroupSBTEntry() const { return m_HitGroupSBTEntry; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::StridedDeviceAddressRegionKHR& GetMissGroupSBTEntry() const { return m_MissGroupSBTEntry; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::StridedDeviceAddressRegionKHR& GetCallableGroupSBTEntry() const { return m_CallableGroupSBTEntry; }

		void BindTLAS(u32 binding, const std::shared_ptr<BaseTLAS>& tlasBuffer) override;

	private:
		void CreateVulkanRayTracingPipeline(const std::span<VulkanShaderData>& vulkanShaderDatas, const std::map<std::vector<u32>*, vk::ShaderModuleCreateInfo>& srcToShaderModulesMap);
		void CreateShaderBindingTable(u32 numberOfGroups);

	private:
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
