#include "BaseShader.hpp"
#include "VulkanShader.hpp"


namespace OWC::Graphics
{
	std::unique_ptr<BaseShader> BaseShader::CreateShader(const std::span<ShaderData>& shaderDatas, const std::span<VertexBindings>& vertexBindings, const std::span<VertexAttributes>& vertexAttributes)
	{
		// setup so that in future other graphics APIs can be supported
		return std::make_unique<VulkanShader>(shaderDatas, vertexBindings, vertexAttributes);
	}

	std::unique_ptr<BaseShader> BaseShader::CreateRTShader(const std::span<ShaderData>& shaderDatas)
	{
		return std::make_unique<VulkanRayTracingShader>(shaderDatas);
	}
}
