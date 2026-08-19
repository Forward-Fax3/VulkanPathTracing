#pragma once
#include "Core.hpp"
#include "UniformBuffer.hpp"
#include "VulkanCore.hpp"

#include <vulkan/vulkan_raii.hpp>


namespace OWC::Graphics
{
	class VulkanUniformBuffer : public UniformBuffer
	{
	public:
		VulkanUniformBuffer() = delete;
		explicit VulkanUniformBuffer(uSize size);
		~VulkanUniformBuffer() override = default;
		VulkanUniformBuffer(VulkanUniformBuffer&) = delete;
		VulkanUniformBuffer& operator=(VulkanUniformBuffer&) = delete;
		VulkanUniformBuffer(VulkanUniformBuffer&&) noexcept = delete;
		VulkanUniformBuffer& operator=(VulkanUniformBuffer&&) noexcept = delete;

		[[nodiscard]] vk::Buffer GetBuffer() const { return m_UniformBuffers[VulkanCore::GetConstInstance().GetCurrentFrameIndex()]; };
		[[nodiscard]] uSize GetBufferSize() const { return m_UniformBuffers[0].getMemoryRequirements().size; }

		[[nodiscard]] const std::vector<vma::raii::Buffer>& GetBuffers() const { return m_UniformBuffers; }

	private:
		void UpdateBufferDataImpl(std::span<const std::byte> data, uSize size, uSize offset) override;

	private:
		std::vector<vma::raii::Buffer> m_UniformBuffers;
	};

	class VulkanTextureBuffer : public TextureBuffer
	{
	public:
		VulkanTextureBuffer() = delete;
		explicit VulkanTextureBuffer(const ImageLoader<f32, 4, glm::aligned_highp>& image);
		explicit VulkanTextureBuffer(u32 width, u32 height);
		~VulkanTextureBuffer() override = default;
		VulkanTextureBuffer(VulkanTextureBuffer&) = delete;
		VulkanTextureBuffer& operator=(const VulkanTextureBuffer&) = delete;
		VulkanTextureBuffer(VulkanTextureBuffer&&) noexcept = delete;
		VulkanTextureBuffer& operator=(VulkanTextureBuffer&&) noexcept = delete;

		void UpdateBufferData(const std::vector<Vec4>& data) override;

		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::Image& GetImage() const { return m_TextureImage; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::ImageView& GetImageView() const { return m_TextureImageView; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::Sampler& GetSampler() const { return m_TextureSampler; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetWidth() const { return m_Width; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetHeight() const { return m_Height; }

		[[nodiscard]] OWC_FORCE_INLINE vk::AccessFlags2 GetCurrentAccessFlags() const { return m_CurrentAccessFlags; }
		[[nodiscard]] OWC_FORCE_INLINE vk::PipelineStageFlags2 GetCurrentPipelineStageFlags() const { return m_CurrentPipelineStageFlags; }
		[[nodiscard]] OWC_FORCE_INLINE vk::ImageLayout GetCurrentLayout() const { return m_CurrentLayout; }
		OWC_FORCE_INLINE void SetCurrentAccessFlags(const vk::AccessFlags2 previousAccessFlags) { m_CurrentAccessFlags = previousAccessFlags; }
		OWC_FORCE_INLINE void SetCurrentPipelineStageFlags(const vk::PipelineStageFlags2 previousPipelineStageFlags) { m_CurrentPipelineStageFlags = previousPipelineStageFlags; }
		OWC_FORCE_INLINE void SetCurrentImageLayout(const vk::ImageLayout previousLayout) { m_CurrentLayout = previousLayout; }

	private:
		void InitializeTexture();

	private:
		vma::raii::Image m_TextureImage = nullptr;
		vk::raii::ImageView m_TextureImageView = nullptr;
		vk::raii::Sampler m_TextureSampler = nullptr;
		u32 m_Width = 0;
		u32 m_Height = 0;
		vk::AccessFlags2 m_CurrentAccessFlags = vk::AccessFlags2();
		vk::PipelineStageFlags2 m_CurrentPipelineStageFlags = vk::PipelineStageFlags2();
		vk::ImageLayout m_CurrentLayout = vk::ImageLayout();
	};

	class VulkanDynamicTextureBuffer : public DynamicTextureBuffer
	{
	public:
		VulkanDynamicTextureBuffer() = delete;
		explicit VulkanDynamicTextureBuffer(u32 width, u32 height);
		~VulkanDynamicTextureBuffer() override = default;
		VulkanDynamicTextureBuffer(VulkanDynamicTextureBuffer&) = delete;
		VulkanDynamicTextureBuffer& operator=(VulkanDynamicTextureBuffer&) = delete;
		VulkanDynamicTextureBuffer(VulkanDynamicTextureBuffer&&) noexcept = delete;
		VulkanDynamicTextureBuffer& operator=(VulkanDynamicTextureBuffer&&) noexcept = delete;
		void UpdateBufferData(const std::vector<Vec4>& data) override;

		[[nodiscard]] vk::Image GetImage() const { return m_TextureImage[VulkanCore::GetConstInstance().GetCurrentFrameIndex()]; }
		[[nodiscard]] vk::ImageView GetImageView() const { return m_TextureImageView[VulkanCore::GetConstInstance().GetCurrentFrameIndex()]; }
		[[nodiscard]] vk::Sampler GetSampler() const { return m_TextureSampler[VulkanCore::GetConstInstance().GetCurrentFrameIndex()]; }

		[[nodiscard]] const std::vector<vma::raii::Image>& GetImages() const { return m_TextureImage; }
		[[nodiscard]] const std::vector<vk::raii::ImageView>& GetImageViews() const { return m_TextureImageView; }
		[[nodiscard]] const std::vector<vk::raii::Sampler>& GetSamplers() const { return m_TextureSampler; }

	private:
		std::vector<vma::raii::Image> m_TextureImage = {};
		std::vector<vk::raii::ImageView> m_TextureImageView = {};
		std::vector<vk::raii::Sampler> m_TextureSampler = {};
		u32 m_Width = 0;
		u32 m_Height = 0;
	};

	class VulkanGeneralBuffer : public GeneralBuffer
	{
		public:
		VulkanGeneralBuffer() = delete;
		explicit VulkanGeneralBuffer(uSize size);
		~VulkanGeneralBuffer() override = default;
		VulkanGeneralBuffer(VulkanGeneralBuffer&&) noexcept = delete;
		VulkanGeneralBuffer& operator=(VulkanGeneralBuffer&&) noexcept = delete;

		void UpdateBufferDataImpl(const u8* data, uSize count, uSize offset) override;

		[[nodiscard]] vk::Buffer GetBuffer() const { return m_Buffer; }
		[[nodiscard]] uSize GetBufferSize() const { return m_Buffer.getMemoryRequirements().size; }
		[[nodiscard]] vk::DeviceAddress GetBufferDeviceAddress() const { return m_BufferDeviceAddress; }

		[[nodiscard]] uSize GetDeviceBufferPtr() const override { return m_BufferDeviceAddress; }

	private:
		vma::raii::Buffer m_Buffer = nullptr;
		vk::DeviceAddress m_BufferDeviceAddress = vk::DeviceAddress();
		uSize m_BufferSize = 0;
	};

	class VulkanTextureArraySampler : public TextureArraySampler
	{
	public:
		VulkanTextureArraySampler();
		explicit VulkanTextureArraySampler(const tg3_sampler& sampler);
		~VulkanTextureArraySampler() override = default;

		[[nodiscard]] const vk::Sampler& GetSampler() const { return *m_Sampler; }

	private:
		vk::raii::Sampler m_Sampler = nullptr;
	};

	class VulkanBaseTextureArray : public TextureArray
	{
	public:
		[[nodiscard]] virtual const vk::Image& getImages() const = 0;
		[[nodiscard]] virtual  const vk::ImageView& getImageViews() const = 0;
	};

	class VulkanEmptyTextureArray : public VulkanBaseTextureArray
	{
	public:
		VulkanEmptyTextureArray();
		~VulkanEmptyTextureArray() override = default;

		[[nodiscard]] const vk::Image& getImages() const override { return *m_TextureImages; }
		[[nodiscard]] const vk::ImageView& getImageViews() const override { return *m_TextureImageView; }

	private:
		vma::raii::Image m_TextureImages = nullptr;
		vk::raii::ImageView m_TextureImageView = nullptr;
	};

	class VulkanColourTextureArray : public VulkanBaseTextureArray
	{
	public:
		VulkanColourTextureArray() = delete;
		explicit VulkanColourTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf);
		~VulkanColourTextureArray() override = default;

		[[nodiscard]] const vk::Image& getImages() const override { return *m_TextureImages; }
		[[nodiscard]] const vk::ImageView& getImageViews() const override { return *m_TextureImageView; }

	private:
		vma::raii::Image m_TextureImages = nullptr;
		vk::raii::ImageView m_TextureImageView = nullptr;
	};

	class VulkanNormalTextureArray : public VulkanBaseTextureArray
	{
	public:
		VulkanNormalTextureArray() = delete;
		explicit VulkanNormalTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf);
		~VulkanNormalTextureArray() override = default;

		[[nodiscard]] const vk::Image& getImages() const override { return *m_TextureImages; }
		[[nodiscard]] const vk::ImageView& getImageViews() const override { return *m_TextureImageView; }

	private:
		vma::raii::Image m_TextureImages = nullptr;
		vk::raii::ImageView m_TextureImageView = nullptr;
	};

	class VulkanMetallicRoughnessTextureArray : public VulkanBaseTextureArray
	{
	public:
		VulkanMetallicRoughnessTextureArray() = delete;
		explicit VulkanMetallicRoughnessTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf);
		~VulkanMetallicRoughnessTextureArray() override = default;

		[[nodiscard]] const vk::Image& getImages() const override { return *m_TextureImages; }
		[[nodiscard]] const vk::ImageView& getImageViews() const override { return *m_TextureImageView; }

	private:
		vma::raii::Image m_TextureImages = nullptr;
		vk::raii::ImageView m_TextureImageView = nullptr;
	};

	class VulkanEmissiveTextureArray : public VulkanBaseTextureArray
	{
	public:
		VulkanEmissiveTextureArray() = delete;
		explicit VulkanEmissiveTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf);
		~VulkanEmissiveTextureArray() override = default;

		[[nodiscard]] const vk::Image& getImages() const override { return *m_TextureImages; }
		[[nodiscard]] const vk::ImageView& getImageViews() const override { return *m_TextureImageView; }

	private:
		vma::raii::Image m_TextureImages = nullptr;
		vk::raii::ImageView m_TextureImageView = nullptr;
	};
}
