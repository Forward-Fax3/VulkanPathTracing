#include "GPUBuffers.hpp"
#include "VulkanBuffers.hpp"


namespace OWC::Graphics
{
	std::shared_ptr<VertexBuffer> VertexBuffer::CreateVertexBuffer(const uSize vertexSize)
	{
		return std::make_shared<VulkanVertexBuffer>(vertexSize);
	}

	std::shared_ptr<VertexBuffer> VertexBuffer::CreateVertexBuffer(uSize size, uSize vertexSize)
	{
		return std::make_shared<VulkanVertexBuffer>(size, vertexSize);
	}

	std::shared_ptr<IndexBuffer> IndexBuffer::CreateIndexBuffer()
	{
		return std::make_shared<VulkanIndexBuffer>();
	}

	std::shared_ptr<IndexBuffer> IndexBuffer::CreateIndexBuffer(uSize numOfIndices)
	{
		return std::make_shared<VulkanIndexBuffer>(numOfIndices);
	}

	std::shared_ptr<UniformBuffer> UniformBuffer::CreateUniformBuffer(uSize size)
	{
		// For now, only Vulkan is supported
		return std::make_shared<VulkanUniformBuffer>(size);
	}

	std::shared_ptr<TextureImage> TextureImage::CreateStorageImage(const ImageLoader<f32, 4, glm::aligned_highp>& image)
	{
		// For now, only Vulkan is supported
		return std::make_shared<VulkanStorageImage>(image);
	}

	std::shared_ptr<TextureImage> TextureImage::CreateStorageImage(u32 width, u32 height, Format format)
	{
		// For now, only Vulkan is supported
		return std::make_shared<VulkanStorageImage>(width, height, format);
	}

	std::shared_ptr<DynamicTextureBuffer> DynamicTextureBuffer::CreateDynamicTextureBuffer(const u32 width, const u32 height, const Format format, const AccessMask accessMask, const StageMask stageMask, const ImageLayout imageLayout)
	{
		// For now, only Vulkan is supported
		return std::make_shared<VulkanDynamicTextureBuffer>(width, height, format, accessMask, stageMask, imageLayout);
	}

	std::shared_ptr<GeneralBuffer> GeneralBuffer::CreateGeneralBuffer(uSize size, uSize alignment /*= 0*/)
	{
		// For Now, only Vulkan is supported
		return std::make_shared<VulkanGeneralBuffer>(size, alignment);
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
