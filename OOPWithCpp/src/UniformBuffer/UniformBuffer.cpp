#include "UniformBuffer.hpp"
#include "VulkanUniformBuffer.hpp"


namespace OWC::Graphics
{
	std::shared_ptr<UniformBuffer> UniformBuffer::CreateUniformBuffer(uSize size)
	{
		// For now, only Vulkan is supported
		return std::make_shared<VulkanUniformBuffer>(size);
	}

	std::shared_ptr<TextureBuffer> TextureBuffer::CreateTextureBuffer(const ImageLoader<f32, 4, glm::aligned_highp>& image)
	{
		// For now, only Vulkan is supported
		return std::make_shared<VulkanTextureBuffer>(image);
	}

	std::shared_ptr<TextureBuffer> TextureBuffer::CreateTextureBuffer(u32 width, u32 height)
	{
		// For now, only Vulkan is supported
		return std::make_shared<VulkanTextureBuffer>(width, height);
	}

	std::shared_ptr<DynamicTextureBuffer> DynamicTextureBuffer::CreateDynamicTextureBuffer(u32 width, u32 height)
	{
		// For now, only Vulkan is supported
		return std::make_shared<VulkanDynamicTextureBuffer>(width, height);
	}

	std::shared_ptr<GeneralBuffer> GeneralBuffer::CreateGeneralBuffer(uSize size)
	{
		// For Now, only Vulkan is supported
		return std::make_shared<VulkanGeneralBuffer>(size);
	}

	std::shared_ptr<TextureArraySampler> TextureArraySampler::CreateEmptyTextureArraySampler()
	{
		return std::make_shared<VulkanTextureArraySampler>();
	}

	std::shared_ptr<TextureArraySampler> TextureArraySampler::CreateTextureArraySampler(const tg3_sampler& sampler)
	{
		return std::make_shared<VulkanTextureArraySampler>(sampler);
	}

	std::shared_ptr<TextureArray> TextureArray::CreateEmptyTextureArray()
	{
	return std::make_shared<VulkanEmptyTextureArray>();
	}

	std::shared_ptr<TextureArray> TextureArray::CreateColourTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		return std::make_shared<VulkanColourTextureArray>(model, indexing, pathToGltf);
	}

	std::shared_ptr<TextureArray> TextureArray::CreateNormalTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		return std::make_shared<VulkanNormalTextureArray>(model, indexing, pathToGltf);
	}

	std::shared_ptr<TextureArray> TextureArray::CreateMetallicRoughnessTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		return std::make_shared<VulkanMetallicRoughnessTextureArray>(model, indexing, pathToGltf);
	}

	std::shared_ptr<TextureArray> TextureArray::CreateEmissiveTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		return std::make_shared<VulkanEmissiveTextureArray>(model, indexing, pathToGltf);
	}
}
