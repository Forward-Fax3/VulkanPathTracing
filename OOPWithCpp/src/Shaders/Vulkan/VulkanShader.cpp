#include "Application.hpp"
#include "VulkanCore.hpp"
#include "VulkanShader.hpp"
#include "VulkanUniformBuffer.hpp"
#include "Log.hpp"

#include "VulkanTLAS.hpp"


namespace OWC::Graphics
{
	void VulkanBaseShader::BindUniform(u32 binding, const std::shared_ptr<UniformBuffer>& uniformBuffer)
	{
		const auto vulkanUniformBuffer = std::dynamic_pointer_cast<VulkanUniformBuffer>(uniformBuffer);

		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets{};
		writeDescriptorSets.reserve(vulkanUniformBuffer->GetBuffers().size());

		std::vector<vk::DescriptorBufferInfo> descriptorBufferInfos{}; // keep alive until updateDescriptorSets is called
		descriptorBufferInfos.reserve(vulkanUniformBuffer->GetBuffers().size());

		for (uSize i = 0; i < vulkanUniformBuffer->GetBuffers().size(); i++)
		{
			descriptorBufferInfos.emplace_back(
				vulkanUniformBuffer->GetBuffers()[i],
				0,
//				vulkanUniformBuffer->GetBufferSize()
				vk::WholeSize
			);
			Log<LogLevel::Trace>("vulkanUniformBuffer->GetBufferSize(): {}", vulkanUniformBuffer->GetBufferSize());
			writeDescriptorSets.emplace_back(
				m_DescriptorSets[i],
				binding,
				0,
				1,
				vk::DescriptorType::eUniformBuffer,
				nullptr,
				&descriptorBufferInfos.back()
			);
		}

		device.updateDescriptorSets(writeDescriptorSets, {});
	}

	void VulkanBaseShader::BindTexture(u32 binding, const std::shared_ptr<TextureBuffer>& textureBuffer)
	{
		const auto vulkanTextureBuffer = std::dynamic_pointer_cast<VulkanTextureBuffer>(textureBuffer);
		if (!vulkanTextureBuffer)
		{
			Log<LogLevel::Error>("VulkanShader::BindTexture: Invalid VulkanTextureBuffer pointer.");
			return;
		}

		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();

		const auto descriptorImageInfo = vk::DescriptorImageInfo()
			.setImageLayout(vk::ImageLayout::eGeneral)
			.setImageView(vulkanTextureBuffer->GetImageView())
			.setSampler(vulkanTextureBuffer->GetSampler());

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets{};
		writeDescriptorSets.reserve(vkCore.GetNumberOfFramesInFlight());

		for (uSize i = 0; i < vkCore.GetNumberOfFramesInFlight(); i++)
		{
			writeDescriptorSets.emplace_back(
				m_DescriptorSets[i],
				binding,
				0,
				1,
				vk::DescriptorType::eStorageImage,
				&descriptorImageInfo
			);
		}

		device.updateDescriptorSets(writeDescriptorSets, {});
	}

	void VulkanBaseShader::BindDynamicTexture(u32 binding, const std::shared_ptr<DynamicTextureBuffer>& dTextureBuffer)
	{
		const auto vulkanDynamicTextureBuffer = std::dynamic_pointer_cast<VulkanDynamicTextureBuffer>(dTextureBuffer);
		if (!vulkanDynamicTextureBuffer)
		{
			Log<LogLevel::Error>("VulkanShader::BindDynamicTexture: Invalid VulkanDynamicTextureBuffer pointer.");
			return;
		}

		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets{};
		writeDescriptorSets.reserve(vkCore.GetNumberOfFramesInFlight());

		std::vector<vk::DescriptorImageInfo> descriptorImageInfos{}; // keep alive until updateDescriptorSets is called
		descriptorImageInfos.reserve(vkCore.GetNumberOfFramesInFlight());

		for (uSize i = 0; i < vkCore.GetNumberOfFramesInFlight(); i++)
		{
			descriptorImageInfos.emplace_back(
				vulkanDynamicTextureBuffer->GetSampler(),
				vulkanDynamicTextureBuffer->GetImageViews()[i],
				vk::ImageLayout::eShaderReadOnlyOptimal
			);

			writeDescriptorSets.emplace_back(
				m_DescriptorSets[i],
				binding,
				0,
				1,
				vk::DescriptorType::eCombinedImageSampler,
				&descriptorImageInfos.back()
			);
		}
		device.updateDescriptorSets(writeDescriptorSets, {});
	}

	VulkanShaderData VulkanBaseShader::ProcessShaderData(const ShaderData& shaderData, std::map<std::vector<u32>*, vk::ShaderModuleCreateInfo>& srcToShaderModulesMap)
	{
		Log<LogLevel::Trace>("VulkanShader::ProcessShaderData: Processing shader data of type {}.", shaderData.ShaderTypeToString());

		VulkanShaderData vulkanShaderData;

		vulkanShaderData.entryPoint = shaderData.entryPoint.empty() ? "main" : shaderData.entryPoint;
		if (shaderData.language == ShaderData::ShaderLanguage::SPIRV) // will always be true for now as the check is done earlier
		{
			auto moduleFound = srcToShaderModulesMap.find(&shaderData.bytecode);

			if (moduleFound == srcToShaderModulesMap.end())
				srcToShaderModulesMap.insert(
					std::make_pair(
						&shaderData.bytecode,
						vk::ShaderModuleCreateInfo(
							vk::ShaderModuleCreateFlags(),
							shaderData.bytecode.size() * sizeof(u32),
							shaderData.bytecode.data()
						)
					)
				);

			vulkanShaderData.moduleKey = &shaderData.bytecode;
		}
		// in the future, add support for other shader languages here (e.g., GLSL to SPIR-V compilation)
		vulkanShaderData.stage = ConvertToVulkanShaderStage(shaderData.type);
		vulkanShaderData.bindingDescriptions =
			std::vector<BindingDescription>(shaderData.descriptorType.begin(), shaderData.descriptorType.end());
		return vulkanShaderData;
	}

	vk::ShaderStageFlagBits VulkanBaseShader::ConvertToVulkanShaderStage(const ShaderType type)
	{
		vk::ShaderStageFlags stage;

		if (testBitMask(type, ShaderType::Vertex))
			stage |= vk::ShaderStageFlagBits::eVertex;
		if (testBitMask(type, ShaderType::Fragment))
			stage |= vk::ShaderStageFlagBits::eFragment;
		if (testBitMask(type, ShaderType::Compute))
			stage |= vk::ShaderStageFlagBits::eCompute;

		if (testBitMask(type, ShaderType::RayGen))
			stage |= vk::ShaderStageFlagBits::eRaygenKHR;
		if (testBitMask(type, ShaderType::RayAnyHit))
			stage |= vk::ShaderStageFlagBits::eAnyHitKHR;
		if (testBitMask(type, ShaderType::RayClosestHit))
			stage |= vk::ShaderStageFlagBits::eClosestHitKHR;
		if (testBitMask(type, ShaderType::RayMiss))
			stage |= vk::ShaderStageFlagBits::eMissKHR;
		if (testBitMask(type, ShaderType::RayIntersect))
			stage |= vk::ShaderStageFlagBits::eIntersectionKHR;

		if (stage == vk::ShaderStageFlags())
			Log<LogLevel::Error>("VulkanShader::ConvertToVulkanShaderStage: Unknown shader type provided: {}!", std::to_underlying(type));

		return static_cast<vk::ShaderStageFlagBits>(static_cast<u32>(stage)); // don't ask
	}

	vk::DescriptorType VulkanBaseShader::ConvertToVulkanDescriptorType(const DescriptorType type)
	{
		switch (type)
		{
		case DescriptorType::UniformBuffer:
			return vk::DescriptorType::eUniformBuffer;
		case DescriptorType::Sampler:
			return vk::DescriptorType::eSampler;
		case DescriptorType::CombinedImageSampler:
			return vk::DescriptorType::eCombinedImageSampler;
		case DescriptorType::StorageBuffer:
			return vk::DescriptorType::eStorageBuffer;
		case DescriptorType::StorageImage:
			return vk::DescriptorType::eStorageImage;
		case DescriptorType::TLAS:
			return vk::DescriptorType::eAccelerationStructureKHR;
		default:
			Log<LogLevel::Error>("VulkanShader::ConvertToVulkanDescriptorType: Unknown descriptor type provided: {}!", std::to_underlying(type));
			return static_cast<vk::DescriptorType>(0); // to silence compiler warning
		}
	}


	VulkanShader::VulkanShader(const std::span<ShaderData>& shaderDatas)
	{
		Log<LogLevel::Trace>("VulkanShader::VulkanShader: Creating Vulkan shader with {} shader stages.", shaderDatas.size());

		if (shaderDatas.empty())
			Log<LogLevel::Error>("VulkanShader::VulkanShader: No shader data provided!");

		std::vector<VulkanShaderData> vulkanShaderDatas;
		vulkanShaderDatas.reserve(shaderDatas.size());

		std::map<std::vector<u32>*, vk::ShaderModuleCreateInfo> srcToShaderModuleMap;

		for (const auto& shaderData : shaderDatas)
		{
			if (shaderData.language != ShaderData::ShaderLanguage::SPIRV)
				Log<LogLevel::Error>("VulkanShader::VulkanShader: Unsupported shader language for Vulkan shader: {}!", std::to_underlying(shaderData.language));

			if (shaderData.type >= ShaderType::RayGen)
				Log<LogLevel::Error>("VulkanShader::VulkanShader: Unsupported shader type for Vulkan graphics pipeline shader: {}! For this shader type please use ray tracing pipeline", shaderData.ShaderTypeToString());

			Log<LogLevel::Debug>("VulkanShader::VulkanShader: Processing {} shader.", shaderData.ShaderTypeToString());

			vulkanShaderDatas.push_back(ProcessShaderData(shaderData, srcToShaderModuleMap));
		}

		CreateVulkanPipeline(vulkanShaderDatas, srcToShaderModuleMap);
	}

	void VulkanShader::CreateVulkanPipeline(const std::span<VulkanShaderData>& vulkanShaderDatas, const std::map<std::vector<u32>*, vk::ShaderModuleCreateInfo>& srcToShaderModulesMap)
	{
		const auto& app = Application::GetConstInstance();

		// Create shader modules and pipeline

		Log<LogLevel::Trace>("VulkanShader::CreateVulkanPipeline: Creating Vulkan pipeline with {} shader stages.", vulkanShaderDatas.size());
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();

		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
		shaderStages.reserve(vulkanShaderDatas.size());

		for (const auto& shaderData : vulkanShaderDatas)
			shaderStages.emplace_back(
				static_cast<vk::PipelineShaderStageCreateFlags>(0),
				shaderData.stage,
				VK_NULL_HANDLE,
				shaderData.entryPoint.c_str(),
				VK_NULL_HANDLE,
				&srcToShaderModulesMap.at(shaderData.moduleKey)
			);

		constexpr std::array dynamicStates = {
			vk::DynamicState::eViewport,
			vk::DynamicState::eScissor
		};

		const auto dynamicStateCreateInfo = vk::PipelineDynamicStateCreateInfo()
			.setDynamicStates(dynamicStates);

		constexpr vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
		constexpr auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo()
			.setTopology(vk::PrimitiveTopology::eTriangleList)
			.setPrimitiveRestartEnable(vk::False);

		const auto viewport = vk::Viewport()
			.setWidth(static_cast<f32>(app.GetPixelWidth()))
			.setHeight(static_cast<f32>(app.GetPixelHeight()))
			.setMinDepth(0.0f)
			.setMaxDepth(1.0f)
			.setX(0.0f)
			.setY(0.0f);
		
		const auto scissor = vk::Rect2D()
			.setOffset({ 0, 0 })
			.setExtent({
				app.GetPixelWidth(),
				app.GetPixelHeight()
			});

		const auto viewportState = vk::PipelineViewportStateCreateInfo()
			.setViewports(viewport)
			.setScissors(scissor);

		constexpr auto rasterizer = vk::PipelineRasterizationStateCreateInfo()
			.setPolygonMode(vk::PolygonMode::eFill)
			.setLineWidth(1.0f);

		constexpr auto multisampling = vk::PipelineMultisampleStateCreateInfo()
			.setSampleShadingEnable(vk::False)
			.setRasterizationSamples(vk::SampleCountFlagBits::e1);

		constexpr auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState()
			.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
				vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
			.setBlendEnable(vk::True)
			.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
			.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
			.setColorBlendOp(vk::BlendOp::eAdd)
			.setSrcAlphaBlendFactor(vk::BlendFactor::eOne);

		const auto colorBlending = vk::PipelineColorBlendStateCreateInfo()
			.setLogicOpEnable(vk::False)
			.setAttachments(colorBlendAttachment);

		const auto pipelineRenderingInfo = vk::PipelineRenderingCreateInfo()
			.setColorAttachmentFormats(VulkanCore::GetConstInstance().GetSwapchainImageFormat());

		std::vector<vk::DescriptorSetLayoutBinding> bindings;

		for (const auto& shaderData : vulkanShaderDatas)
			for (const auto& [descriptorCount, binding, descriptorType, stageFlags] : shaderData.bindingDescriptions)
				bindings.emplace_back(
					binding,
					ConvertToVulkanDescriptorType(descriptorType),
					descriptorCount,
					ConvertToVulkanShaderStage(stageFlags)
				);

		const auto layoutInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindings(bindings);

		SetDescriptorSetLayout(device.createDescriptorSetLayout(layoutInfo));

		const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
			.setSetLayouts(*GetDescriptorSetLayout());

		SetPipelineLayout(device.createPipelineLayout(pipelineLayoutInfo));

		const auto pipelineInfo = vk::GraphicsPipelineCreateInfo()
			.setPNext(&pipelineRenderingInfo)
			.setStages(shaderStages)
			.setPVertexInputState(&vertexInputInfo)
			.setPInputAssemblyState(&inputAssembly)
			.setPViewportState(&viewportState)
			.setPRasterizationState(&rasterizer)
			.setPMultisampleState(&multisampling)
			.setPColorBlendState(&colorBlending)
			.setPDynamicState(&dynamicStateCreateInfo)
			.setLayout(GetPipelineLayout())
			.setSubpass(0);

		SetPipeline(vk::raii::Pipeline(device, nullptr, pipelineInfo));

		// build Descriptor Pool
		// TODO: make this dynamic based on actual usage

		std::vector<vk::DescriptorPoolSize> poolSize;

		for (const auto& shaderData : vulkanShaderDatas)
		{
			for (const auto& bindingDescription : shaderData.bindingDescriptions)
			{
				vk::DescriptorType descriptorType = ConvertToVulkanDescriptorType(bindingDescription.descriptorType);

				auto it = std::ranges::find_if(poolSize, [descriptorType](const vk::DescriptorPoolSize& size)
					{
						return size.type == descriptorType;
					});

				if (it != poolSize.end())
				{
					it->descriptorCount += bindingDescription.descriptorCount * static_cast<u32>(vkCore.GetNumberOfFramesInFlight()) * 2;
				}
				else
				{
					poolSize.emplace_back(
						descriptorType,
						bindingDescription.descriptorCount * static_cast<u32>(vkCore.GetNumberOfFramesInFlight()) * 2
					);
				}
			}
		}

		const auto poolInfo = vk::DescriptorPoolCreateInfo()
			.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
			.setPoolSizes(poolSize)
			.setMaxSets(static_cast<u32>(vkCore.GetNumberOfFramesInFlight()));

		SetDescriptorPool(device.createDescriptorPool(poolInfo));
		const auto allocInfo = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(GetDescriptorPool())
			.setSetLayouts(*GetDescriptorSetLayout());

		std::vector<vk::raii::DescriptorSet> descriptorSets;
		descriptorSets.reserve(vkCore.GetNumberOfFramesInFlight());

		for (uSize i = 0; i < vkCore.GetNumberOfFramesInFlight(); i++)
			descriptorSets.push_back(std::move(device.allocateDescriptorSets(allocInfo).front()));

		SetDescriptorSets(std::move(descriptorSets));
	}

	VulkanRayTracingShader::VulkanRayTracingShader(const std::span<ShaderData>& shaderDatas)
	{
		Log<LogLevel::Trace>("VulkanShader::VulkanShader: Creating Vulkan shader with {} shader stages.", shaderDatas.size());

		if (shaderDatas.empty())
			Log<LogLevel::Error>("VulkanShader::VulkanShader: No shader data provided!");

		std::vector<VulkanShaderData> vulkanShaderDatas;
		vulkanShaderDatas.reserve(shaderDatas.size());

		std::map<std::vector<u32>*, vk::ShaderModuleCreateInfo> srcToShaderModuleMap;

		for (const auto& shaderData : shaderDatas)
		{
			if (shaderData.language != ShaderData::ShaderLanguage::SPIRV)
				Log<LogLevel::Error>("VulkanShader::VulkanShader: Unsupported shader language for Vulkan shader: {}!", std::to_underlying(shaderData.language));

			if (shaderData.type < ShaderType::RayGen)
				Log<LogLevel::Error>("VulkanRayTracingShader::VulkanRayTracingShader: Unsupported shader type for Vulkan ray tracing pipeline shader: {}! For this shader type please use graphics pipeline", shaderData.ShaderTypeToString());

			Log<LogLevel::Debug>("VulkanShader::VulkanShader: Processing {} shader.", shaderData.ShaderTypeToString());

			vulkanShaderDatas.push_back(ProcessShaderData(shaderData, srcToShaderModuleMap));
		}

		CreateVulkanRayTracingPipeline(vulkanShaderDatas, srcToShaderModuleMap);
		CreateShaderBindingTable(m_ShaderGroupCount);
	}

	void VulkanRayTracingShader::BindTLAS(u32 binding, const std::shared_ptr<BaseTLAS>& tlas)
	{
		const auto vulkanTLASBuffer = std::dynamic_pointer_cast<VulkanTLAS>(tlas);

		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();

		const auto descriptorAccelerationStructureInfo = vk::WriteDescriptorSetAccelerationStructureKHR()
			.setPAccelerationStructures(&*vulkanTLASBuffer->GetTLAS())
			.setAccelerationStructureCount(1);

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets{};
		writeDescriptorSets.reserve(vkCore.GetNumberOfFramesInFlight());

		for (uSize i = 0; i < vkCore.GetNumberOfFramesInFlight(); i++)
		{
			writeDescriptorSets.emplace_back(
				GetSpecificDescriptorSet(i),
				binding,
				0,
				1,
				vk::DescriptorType::eAccelerationStructureKHR,
				nullptr,
				nullptr,
				nullptr,
				&descriptorAccelerationStructureInfo
			);
		}

		device.updateDescriptorSets(writeDescriptorSets, {});
	}

	void VulkanRayTracingShader::CreateVulkanRayTracingPipeline(const std::span<VulkanShaderData>& vulkanShaderDatas, const std::map<std::vector<u32>*, vk::ShaderModuleCreateInfo>& srcToShaderModulesMap)
	{
		//const auto& app = Application::GetConstInstance();

		Log<LogLevel::Trace>("VulkanShader::CreateVulkanRayTracingPipeline: Creating Vulkan ray tracing pipeline with {} shader stages.", vulkanShaderDatas.size());

		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();

		std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
		shaderStages.reserve(vulkanShaderDatas.size());
		std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups;
		shaderGroups.reserve(vulkanShaderDatas.size());

		std::vector<vk::DescriptorSetLayoutBinding> bindings;

		for (u32 i = 0; i < vulkanShaderDatas.size(); i++)
		{
			const auto& [entryPoint, moduleKey, stage, bindingDescriptions] = vulkanShaderDatas[i];

			shaderStages.emplace_back(
				static_cast<vk::PipelineShaderStageCreateFlags>(0),
				stage,
				VK_NULL_HANDLE,
				entryPoint.c_str(),
				VK_NULL_HANDLE,
				&srcToShaderModulesMap.at(moduleKey)
			);

			switch (stage)
			{
			case vk::ShaderStageFlagBits::eRaygenKHR:
				shaderGroups.emplace_back(
					vk::RayTracingShaderGroupTypeKHR::eGeneral,
					i
				);
				break;
			case vk::ShaderStageFlagBits::eClosestHitKHR:
				shaderGroups.emplace_back(
					vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
					VK_SHADER_UNUSED_KHR,
					i
				);
				break;
			case vk::ShaderStageFlagBits::eMissKHR:
				shaderGroups.emplace_back(
					vk::RayTracingShaderGroupTypeKHR::eGeneral,
					i
				);
				break;
			default:
				Log<LogLevel::Error>("VulkanShader::CreateVulkanRayTracingPipeline: Unsupported shader stage for ray tracing pipeline: {}!", vk::to_string(stage));
			}

			for (const auto& [descriptorCount, binding, descriptorType, stageFlags] : bindingDescriptions)
				bindings.emplace_back(
					binding,
					ConvertToVulkanDescriptorType(descriptorType),
					descriptorCount,
					ConvertToVulkanShaderStage(stageFlags)
				);
		}

		const auto layoutInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindings(bindings);

		SetDescriptorSetLayout(device.createDescriptorSetLayout(layoutInfo));

		constexpr auto pushConstantRange = vk::PushConstantRange()
			.setOffset(0)
			.setSize(128) // TODO: make this configurable
			.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR);

		const auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
			.setSetLayouts(*GetDescriptorSetLayout())
			.setPushConstantRanges(pushConstantRange);

		SetPipelineLayout(device.createPipelineLayout(pipelineLayoutInfo));

		const auto rayTracingPipelineInfo = vk::RayTracingPipelineCreateInfoKHR()
			.setStages(shaderStages)
			.setGroups(shaderGroups)
			.setMaxPipelineRayRecursionDepth(8) // TODO: make this configurable
			.setLayout(GetPipelineLayout());

		SetPipeline(vk::raii::Pipeline(device, nullptr, nullptr, rayTracingPipelineInfo));

		m_ShaderGroupCount = static_cast<u32>(shaderGroups.size());

		std::vector<vk::DescriptorPoolSize> poolSize;

		for (const auto& shaderData : vulkanShaderDatas)
		{
			for (const auto& bindingDescription : shaderData.bindingDescriptions)
			{
				vk::DescriptorType descriptorType = ConvertToVulkanDescriptorType(bindingDescription.descriptorType);

				auto it = std::ranges::find_if(poolSize, [descriptorType](const vk::DescriptorPoolSize& size)
					{
						return size.type == descriptorType;
					});

				if (it != poolSize.end())
				{
					it->descriptorCount += bindingDescription.descriptorCount * static_cast<u32>(vkCore.GetNumberOfFramesInFlight()) * 2;
				}
				else
				{
					poolSize.emplace_back(
						descriptorType,
						bindingDescription.descriptorCount * static_cast<u32>(vkCore.GetNumberOfFramesInFlight()) * 2
					);
				}
			}
		}

		const auto poolInfo = vk::DescriptorPoolCreateInfo()
			.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
			.setPoolSizes(poolSize)
			.setMaxSets(static_cast<u32>(vkCore.GetNumberOfFramesInFlight()));

		SetDescriptorPool(device.createDescriptorPool(poolInfo));
		const auto allocInfo = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(GetDescriptorPool())
			.setSetLayouts(*GetDescriptorSetLayout());

		std::vector<vk::raii::DescriptorSet> descriptorSets;
		descriptorSets.reserve(vkCore.GetNumberOfFramesInFlight());

		for (uSize i = 0; i < vkCore.GetNumberOfFramesInFlight(); i++)
			descriptorSets.emplace_back(std::move(device.allocateDescriptorSets(allocInfo).front()));

		SetDescriptorSets(std::move(descriptorSets));
	}

	void VulkanRayTracingShader::CreateShaderBindingTable(const u32 numberOfGroups)
	{
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& RTProps = vkCore.GetRayTracingPipelineProperties();

		if (numberOfGroups < 3)
		{
			Log<LogLevel::Error>("VulkanRayTracingShader::CreateShaderBindingTable: Expected at least 3 shader groups (raygen/miss/hit), got {}.", numberOfGroups);
			return;
		}
		if (numberOfGroups > 3)
			Log<LogLevel::Warn>("VulkanRayTracingShader::CreateShaderBindingTable: Extra shader groups will be ignored. Count: {}.", numberOfGroups);

		const u32 handleSize = RTProps.shaderGroupHandleSize;
		const u32 handleAlignment = RTProps.shaderGroupHandleAlignment;
		const u32 baseAlignment = RTProps.shaderGroupBaseAlignment;

		const uSize shaderHandleSize = handleSize * numberOfGroups;
		m_ShaderHandles = GetPipeline().getRayTracingShaderGroupHandlesKHR<u8>(0, numberOfGroups, shaderHandleSize);

		// nVidia vk_raytracing_tutorial helper func
		auto alignUp = [](auto value, size_t alignment) noexcept { return ((value + alignment - 1) & ~(alignment - 1)); };
		const u32 handleSizeAligned = alignUp(handleSize, handleAlignment);
		const u32 recordStride = alignUp(handleSizeAligned, baseAlignment);
		u32 raygenSize   = recordStride;
		u32 missSize     = recordStride;
		u32 hitSize      = recordStride;
		u32 callableSize = 0; // Callable shaders not supported yet

		u32 raygenOffset   = 0;
		u32 missOffset     = raygenOffset + raygenSize;
		u32 hitOffset      = missOffset + missSize;
		u32 callableOffset = hitOffset + hitSize;

		const vk::DeviceSize bufferSize = callableOffset + callableSize;

		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		constexpr auto bufferUsageInfo2 = vk::BufferUsageFlags2CreateInfo()
			.setUsage(
				vk::BufferUsageFlagBits2::eShaderDeviceAddress |
				vk::BufferUsageFlagBits2::eShaderBindingTableKHR
			);

		const auto bufferInfo = vk::BufferCreateInfo()
			.setPNext(&bufferUsageInfo2)
			.setSize(bufferSize)
			.setSharingMode(vk::SharingMode::eConcurrent)
			.setQueueFamilyIndices(queueIndices);

		constexpr vma::AllocationCreateInfo allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessRandom);

		vma::AllocationInfo allocationInfo;
		m_SBTBuffer = vma::raii::Buffer(allocator, bufferInfo, allocInfo, vk::Optional(allocationInfo));

		const auto pData = static_cast<u8*>(allocationInfo.pMappedData);

		std::memcpy(pData + raygenOffset, m_ShaderHandles.data() + 0 * handleSize, handleSize);
		std::memcpy(pData + missOffset, m_ShaderHandles.data() + 1 * handleSize, handleSize);
		std::memcpy(pData + hitOffset, m_ShaderHandles.data() + 2 * handleSize, handleSize);

		const auto SBTBufferDeviceAddress = vkCore.GetDevice().getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(m_SBTBuffer));

		m_RayGenShaderSBTEntry
			.setDeviceAddress(SBTBufferDeviceAddress + raygenOffset)
			.setSize(raygenSize)
			.setStride(recordStride);

		m_HitGroupSBTEntry
			.setDeviceAddress(SBTBufferDeviceAddress + hitOffset)
			.setSize(hitSize)
			.setStride(recordStride);

		m_MissGroupSBTEntry
			.setDeviceAddress(SBTBufferDeviceAddress + missOffset)
			.setSize(missSize)
			.setStride(recordStride);

		m_CallableGroupSBTEntry
			.setDeviceAddress(SBTBufferDeviceAddress + callableOffset)
			.setSize(callableSize)
			.setStride(callableSize);
	}
}
