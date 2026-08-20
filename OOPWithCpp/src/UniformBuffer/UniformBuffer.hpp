#pragma once
#include "ImageLoader.hpp"

#include <cstddef>
#include <span>
#include <memory>
#include <vector>
#include <map>
#include <glm/vec4.hpp>

#include "tiny_gltf_v3.h"


namespace OWC::Graphics
{
	class UniformBuffer
	{
	public:
		UniformBuffer() = default;
		virtual ~UniformBuffer() = default;
		UniformBuffer(UniformBuffer&) = default;
		UniformBuffer& operator=(const UniformBuffer&) = default;
		UniformBuffer(UniformBuffer&&) noexcept = default;
		UniformBuffer& operator=(UniformBuffer&&) noexcept = default;

		void UpdateBufferData(const std::span<const std::byte> data, const uSize size = 0, const uSize offset = 0)
		{
			UpdateBufferDataImpl(data, size, offset);
		}

		static std::shared_ptr<UniformBuffer> CreateUniformBuffer(uSize size);

	private:
		virtual void UpdateBufferDataImpl(std::span<const std::byte> data, uSize size, uSize offset) = 0;
	};

	class TextureBuffer
	{
	public:
		TextureBuffer() = default;
		virtual ~TextureBuffer() = default;
		TextureBuffer(TextureBuffer&) = default;
		TextureBuffer& operator=(const TextureBuffer&) = default;
		TextureBuffer(TextureBuffer&&) noexcept = default;
		TextureBuffer& operator=(TextureBuffer&&) noexcept = default;

		virtual void UpdateBufferData(const std::vector<Vec4>& data) = 0;

		static std::shared_ptr<TextureBuffer> CreateTextureBuffer(const ImageLoader<f32, 4, glm::aligned_highp>& image);
		static std::shared_ptr<TextureBuffer> CreateTextureBuffer(u32 width, u32 height);
	};

	class DynamicTextureBuffer // a texture that can be updated every frame
	{
		public:
		DynamicTextureBuffer() = default;
		virtual ~DynamicTextureBuffer() = default;
		DynamicTextureBuffer(DynamicTextureBuffer&) = default;
		DynamicTextureBuffer& operator=(const DynamicTextureBuffer&) = default;
		DynamicTextureBuffer(DynamicTextureBuffer&&) noexcept = default;
		DynamicTextureBuffer& operator=(DynamicTextureBuffer&&) noexcept = default;
		virtual void UpdateBufferData(const std::vector<Vec4>& data) = 0;

		static std::shared_ptr<DynamicTextureBuffer> CreateDynamicTextureBuffer(u32 width, u32 height);
	};

	class GeneralBuffer
	{
	public:
		GeneralBuffer() = default;
		virtual ~GeneralBuffer() = default;
		GeneralBuffer(GeneralBuffer&) = default;
		GeneralBuffer& operator=(const GeneralBuffer&) = default;
		GeneralBuffer(GeneralBuffer&&) noexcept = default;
		GeneralBuffer& operator=(GeneralBuffer&&) noexcept = default;

		virtual void UpdateBufferDataImpl(const u8* data, uSize count, uSize offset) = 0;

		OWC_FORCE_INLINE void UpdateBufferData(const u8* data, const uSize count = 0, const uSize offset = 0)
		{
			UpdateBufferDataImpl(data, count, offset);
		}

		virtual uSize GetDeviceBufferPtr() const = 0;

		static std::shared_ptr<GeneralBuffer> CreateGeneralBuffer(uSize size);
	};

	class TextureArraySampler
	{
	public:
		TextureArraySampler() = default;
		virtual ~TextureArraySampler() = default;
		TextureArraySampler(TextureArraySampler&) = default;
		TextureArraySampler& operator=(const TextureArraySampler&) = default;
		TextureArraySampler(TextureArraySampler&&) noexcept = default;
		TextureArraySampler& operator=(TextureArraySampler&&) noexcept = default;

		static std::shared_ptr<TextureArraySampler> CreateEmptyTextureArraySampler();
		static std::shared_ptr<TextureArraySampler> CreateTextureArraySampler(const tg3_sampler& sampler);
	};

	class TextureArray
	{
	public:
		TextureArray() = default;
		virtual ~TextureArray() = default;
		TextureArray(TextureArray&) = default;
		TextureArray& operator=(const TextureArray&) = default;
		TextureArray(TextureArray&&) noexcept = default;
		TextureArray& operator=(TextureArray&&) noexcept = default;

		static std::shared_ptr<TextureArray> CreateEmptyTextureArray();
		static std::shared_ptr<TextureArray> CreateColourTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf);
		static std::shared_ptr<TextureArray> CreateNormalTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf);
		static std::shared_ptr<TextureArray> CreateMetallicRoughnessTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf);
		static std::shared_ptr<TextureArray> CreateEmissiveTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf);
	};
}
