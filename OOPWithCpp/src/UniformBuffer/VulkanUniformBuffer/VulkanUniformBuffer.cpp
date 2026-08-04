#include "VulkanUniformBuffer.hpp"
#include "VulkanCore.hpp"


namespace OWC::Graphics
{
	//--------------------------------------------------------
	// VulkanUniformBuffer
	//--------------------------------------------------------

	VulkanUniformBuffer::VulkanUniformBuffer(const uSize size)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		// Create uniform buffers for each frame in flight and allocate memory
		const vk::DeviceSize bufferSize = size;

		m_UniformBuffers.reserve(vkCore.GetNumberOfFramesInFlight());

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		for (uSize i = 0; i < vkCore.GetNumberOfFramesInFlight(); i++)
		{
			const auto bufferInfo = vk::BufferCreateInfo()
				.setSize(bufferSize)
				.setUsage(vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst)
				.setSharingMode(vk::SharingMode::eConcurrent)
				.setQueueFamilyIndices(queueIndices);

			constexpr auto allocInfo = vma::AllocationCreateInfo()
				.setUsage(vma::MemoryUsage::eAutoPreferDevice)
				.setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

			m_UniformBuffers.emplace_back(allocator, bufferInfo, allocInfo);
		}
	}

	void VulkanUniformBuffer::UpdateBufferDataImpl(std::span<const std::byte> data, uSize size, uSize offset)
	{
		vk::DeviceSize dataSize = size == 0ull ? data.size() : size;

		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();
		const uSize currentFrame = vkCore.GetCurrentFrameIndex();

		const auto bufferCreateInfo = vk::BufferCreateInfo()
			.setSize(dataSize)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive);

		constexpr auto allocCreateInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo stagingBufferAllocationInfo;
		vma::raii::Buffer stagingBuffer(allocator, bufferCreateInfo, allocCreateInfo, vk::Optional(stagingBufferAllocationInfo));

		std::memcpy(stagingBufferAllocationInfo.pMappedData, data.data(), dataSize);

		auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer();
		cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const auto copyBufferSize = vk::BufferCopy2()
				.setSrcOffset(0)
				.setDstOffset(offset)
				.setSize(dataSize);
		const auto copyBufferInfo = vk::CopyBufferInfo2()
			.setSrcBuffer(stagingBuffer)
			.setDstBuffer(m_UniformBuffers[currentFrame])
			.setRegions(copyBufferSize);

		cmdTransBuf.copyBuffer2(copyBufferInfo);

		const auto transferBufferMemoryBarrier = vk::BufferMemoryBarrier2()
			.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
			.setDstAccessMask(vk::AccessFlagBits2::eNone)
			.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
			.setDstStageMask(vk::PipelineStageFlagBits2::eNone)
			.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setBuffer(m_UniformBuffers[currentFrame])
			.setOffset(offset)
			.setSize(dataSize);

		cmdTransBuf.pipelineBarrier2(vk::DependencyInfo()
			.setBufferMemoryBarriers(transferBufferMemoryBarrier)
		);
		cmdTransBuf.end();

		auto semaphore = vkCore.GetSingleSemaphore();

		const auto cmdTransferSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdTransBuf);
		const auto semaphoreSubmit = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eTransfer);

		const auto transferSubmitInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdTransferSubmit)
			.setSignalSemaphoreInfos(semaphoreSubmit);

		vkCore.GetTransferQueue().submit2(transferSubmitInfo);

		auto cmdGraphicsBuffer = vkCore.GetSingleTimeGraphicsCommandBuffer();
		cmdGraphicsBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const auto GraphicsBufferMemoryBarrier = vk::BufferMemoryBarrier2()
			.setSrcAccessMask(vk::AccessFlagBits2::eNone)
			.setDstAccessMask(vk::AccessFlagBits2::eUniformRead)
			.setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
			.setDstStageMask(vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader) // assuming the uniform buffer is used in both vertex and fragment shaders, adjust as necessary
			.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setBuffer(m_UniformBuffers[currentFrame])
			.setOffset(offset)
			.setSize(dataSize);

		cmdGraphicsBuffer.pipelineBarrier2(vk::DependencyInfo()
			.setBufferMemoryBarriers(GraphicsBufferMemoryBarrier)
		);

		cmdGraphicsBuffer.end();

		const auto cmdGraphicsSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdGraphicsBuffer);

		const auto graphicsSemaphore = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader);

		vkCore.GetGraphicsQueue().submit2(vk::SubmitInfo2()
			.setCommandBufferInfos(cmdGraphicsSubmitInfo)
			.setWaitSemaphoreInfos(graphicsSemaphore)
		);

		VulkanCore::GetInstance().AddVulkanEndOfFrameCleanUpFunction([stagingBuffer = std::move(stagingBuffer), semaphore = std::move(semaphore), cmdTransBuf = std::move(cmdTransBuf), cmdGraphicsBuffer = std::move(cmdGraphicsBuffer)](){});
	}

	//--------------------------------------------------------
	// VulkanTextureBuffer
	//--------------------------------------------------------

	VulkanTextureBuffer::VulkanTextureBuffer(const ImageLoader<f32, 4, glm::aligned_highp>& image)
		: m_Width(static_cast<u32>(image.GetWidth())), m_Height(static_cast<u32>(image.GetHeight()))
	{
		InitializeTexture();
		VulkanTextureBuffer::UpdateBufferData(image.GetImageData()); // using specifically VulkanTextureBuffer to remove static analysis warning
	}

	VulkanTextureBuffer::VulkanTextureBuffer(const u32 width, const u32 height)
		: m_Width(width), m_Height(height)
	{
		InitializeTexture();
	}

	void VulkanTextureBuffer::UpdateBufferData(const std::vector<Vec4>& data)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		// Create staging buffer
		const vk::DeviceSize imageSize = static_cast<uSize>(m_Width) * static_cast<uSize>(m_Height) * sizeof(Vec4);

		if (data.size() * sizeof(Vec4) != imageSize)
		{
			Log<LogLevel::Error>("Data size does not match texture size in VulkanTextureBuffer::UpdateBufferData");
			return;
		}

		const auto bufferInfo = vk::BufferCreateInfo()
			.setSize(imageSize)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive);

		constexpr auto allocCreateInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo stagingBufferAllocationInfo;
		vma::raii::Buffer stagingBuffer(allocator, bufferInfo, allocCreateInfo, vk::Optional(stagingBufferAllocationInfo));

		if (stagingBufferAllocationInfo.pMappedData == nullptr || stagingBufferAllocationInfo.size != imageSize)
		{
			Log<LogLevel::Error>("Failed to map staging buffer memory in VulkanTextureBuffer::UpdateBufferData");
			return;
		}

		std::memcpy(stagingBufferAllocationInfo.pMappedData, data.data(), imageSize);

		// Copy staging buffer to texture image and transition image layout
		auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer();
		cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const std::array imageMemoryBarrierBegin = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(GetCurrentPipelineStageFlags())
				.setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setSrcAccessMask(GetCurrentAccessFlags())
				.setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setOldLayout(GetCurrentLayout())
				.setNewLayout(vk::ImageLayout::eGeneral)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(m_TextureImage)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)
				)
		};

		cmdTransBuf.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierBegin)
		);

		cmdTransBuf.copyBufferToImage(
			stagingBuffer,
			m_TextureImage,
			vk::ImageLayout::eGeneral,
			vk::BufferImageCopy()
				.setBufferOffset(0)
				.setBufferRowLength(0)
				.setBufferImageHeight(0)
				.setImageSubresource(vk::ImageSubresourceLayers()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setMipLevel(0)
					.setBaseArrayLayer(0)
					.setLayerCount(1))
				.setImageOffset(vk::Offset3D{ 0, 0, 0 })
				.setImageExtent(vk::Extent3D{
					m_Width,
					m_Height,
					1
				})
			);

		const std::array imageMemoryBarrierEnd = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setDstStageMask(vk::PipelineStageFlagBits2::eTopOfPipe)
				.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setDstAccessMask(vk::AccessFlagBits2::eNone)
				.setOldLayout(vk::ImageLayout::eGeneral)
				.setNewLayout(vk::ImageLayout::eGeneral)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(m_TextureImage)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)
				)
		};

		cmdTransBuf.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierEnd)
		);

		SetCurrentAccessFlags(vk::AccessFlagBits2::eTransferWrite);
		SetCurrentImageLayout(vk::ImageLayout::eGeneral);
		SetCurrentPipelineStageFlags(vk::PipelineStageFlagBits2::eTransfer);

		cmdTransBuf.end();

		//auto semaphore = vkCore.GetSingleSemaphore();

		const auto cmdTransferSubmitInfo = vk::CommandBufferSubmitInfo()
			.setCommandBuffer(cmdTransBuf);

		/*
		const auto signalSemaphoreTransferInfo = vk::SemaphoreSubmitInfo()
			//.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eAllTransfer);
		*/

		const auto fence = device.createFence(vk::FenceCreateInfo());

		const auto submitTransferInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdTransferSubmitInfo);
			//.setSignalSemaphoreInfos(signalSemaphoreTransferInfo);
		vkCore.GetTransferQueue().submit2(submitTransferInfo, *fence);

		if (device.waitForFences(*fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
			Log<LogLevel::Critical>("Failed to wait for transfer finished fence");

		/*
		auto cmdGraphicsBuf = vkCore.GetSingleTimeGraphicsCommandBuffer();
		cmdGraphicsBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		const std::array graphicsImageBuffer = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
				.setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eBottomOfPipe)
				.setSrcAccessMask(vk::AccessFlagBits2::eNone)
				.setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
				.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
				.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(m_TextureImage)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)
				)
		};
		cmdGraphicsBuf.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(graphicsImageBuffer)
		);
		cmdGraphicsBuf.end();

		const auto cmdInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdGraphicsBuf);
		const auto semaphoreInfo = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eBottomOfPipe);
		const auto submitGraphicsInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdInfo)
			.setWaitSemaphoreInfos(semaphoreInfo);
		vkCore.GetGraphicsQueue().submit2(submitGraphicsInfo);
		*/

		VulkanCore::GetInstance().AddVulkanEndOfFrameCleanUpFunction([cmdTransBuf = std::move(cmdTransBuf), /*cmdGraphicsBuf, semaphore,*/ stagingBuffer = std::move(stagingBuffer)]() -> void {});
	}

	void VulkanTextureBuffer::InitializeTexture()
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		const auto createInfo = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(vk::Format::eR32G32B32A32Sfloat)
			.setExtent(vk::Extent3D()
				.setWidth(m_Width)
				.setHeight(m_Height)
				.setDepth(1))
			.setMipLevels(1)
			.setArrayLayers(1)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage)
			.setSharingMode(vk::SharingMode::eConcurrent)
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setQueueFamilyIndices(queueIndices);

		constexpr auto vmaCreateInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

		// Create image
		m_TextureImage = vma::raii::Image(allocator, createInfo, vmaCreateInfo);

		// Create image view
		m_TextureImageView = vk::raii::ImageView(device, vk::ImageViewCreateInfo()
			.setImage(m_TextureImage)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(vk::Format::eR32G32B32A32Sfloat)
			.setSubresourceRange(vk::ImageSubresourceRange()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1)));

		const f32 maxAnisotropy = vkCore.GetPhysicalDev().getProperties().limits.maxSamplerAnisotropy;

		// Create sampler
		m_TextureSampler = vk::raii::Sampler(device, vk::SamplerCreateInfo()
			.setMagFilter(vk::Filter::eLinear)
			.setMinFilter(vk::Filter::eLinear)
			.setAddressModeU(vk::SamplerAddressMode::eRepeat)
			.setAddressModeV(vk::SamplerAddressMode::eRepeat)
			.setAddressModeW(vk::SamplerAddressMode::eRepeat)
			.setAnisotropyEnable(vk::True)
			.setMaxAnisotropy(maxAnisotropy)
			.setBorderColor(vk::BorderColor::eIntOpaqueBlack)
			.setUnnormalizedCoordinates(vk::False)
			.setCompareEnable(vk::False)
			.setCompareOp(vk::CompareOp::eAlways)
			.setMipmapMode(vk::SamplerMipmapMode::eLinear)
			.setMipLodBias(0.0f)
			.setMinLod(0.0f)
			.setMaxLod(0.0f));

		SetCurrentAccessFlags(vk::AccessFlagBits2::eNone);
		SetCurrentImageLayout(vk::ImageLayout::eUndefined);
		SetCurrentPipelineStageFlags(vk::PipelineStageFlagBits2::eTopOfPipe);
	}

	//--------------------------------------------------------
	// VulkanDynamicTextureBuffer
	//--------------------------------------------------------

	VulkanDynamicTextureBuffer::VulkanDynamicTextureBuffer(const u32 width, const u32 height)
		: m_Width(width), m_Height(height)
	{
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		m_TextureImage.reserve(vkCore.GetNumberOfFramesInFlight());
		m_TextureImageView.reserve(vkCore.GetNumberOfFramesInFlight());
		m_TextureSampler.reserve(vkCore.GetNumberOfFramesInFlight());

		const f32 maxAnisotropy = vkCore.GetPhysicalDev().getProperties().limits.maxSamplerAnisotropy;
		const auto& allQueueFamilyIndices = vkCore.GetAllUniqueQueuesIndices();

		const auto imageCreateInfo = vk::ImageCreateInfo()
				.setImageType(vk::ImageType::e2D)
				.setFormat(vk::Format::eR32G32B32A32Sfloat)
				.setExtent(vk::Extent3D()
					.setWidth(m_Width)
					.setHeight(m_Height)
					.setDepth(1))
				.setMipLevels(1)
				.setArrayLayers(1)
				.setSamples(vk::SampleCountFlagBits::e1)
				.setTiling(vk::ImageTiling::eOptimal)
				.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
				.setSharingMode(vk::SharingMode::eConcurrent)
				.setInitialLayout(vk::ImageLayout::eUndefined)
				.setQueueFamilyIndices(allQueueFamilyIndices);

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

		for (uSize i = 0; i < vkCore.GetNumberOfFramesInFlight(); i++)
		{
			// Create image
			m_TextureImage.emplace_back(allocator, imageCreateInfo, allocInfo);

			// Create image view
			m_TextureImageView.emplace_back(device, vk::ImageViewCreateInfo()
				.setImage(m_TextureImage[i])
				.setViewType(vk::ImageViewType::e2D)
				.setFormat(vk::Format::eR32G32B32A32Sfloat)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)));

			// Create sampler
			m_TextureSampler.emplace_back(device,vk::SamplerCreateInfo()
				.setMagFilter(vk::Filter::eLinear)
				.setMinFilter(vk::Filter::eLinear)
				.setAddressModeU(vk::SamplerAddressMode::eRepeat)
				.setAddressModeV(vk::SamplerAddressMode::eRepeat)
				.setAddressModeW(vk::SamplerAddressMode::eRepeat)
				.setAnisotropyEnable(vk::True)
				.setMaxAnisotropy(maxAnisotropy)
				.setBorderColor(vk::BorderColor::eIntOpaqueBlack)
				.setUnnormalizedCoordinates(vk::False)
				.setCompareEnable(vk::False)
				.setCompareOp(vk::CompareOp::eAlways)
				.setMipmapMode(vk::SamplerMipmapMode::eLinear)
				.setMipLodBias(0.0f)
				.setMinLod(0.0f)
				.setMaxLod(0.0f));
		}
	}

	void VulkanDynamicTextureBuffer::UpdateBufferData(const std::vector<Vec4>& data)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();
		const uSize currentFrame = vkCore.GetCurrentFrameIndex();

		// Create staging buffer
		const vk::DeviceSize imageSize = static_cast<uSize>(m_Width) * static_cast<uSize>(m_Height) * sizeof(Vec4);

		const auto bufferCreateInfo = vk::BufferCreateInfo()
													   .setSize(imageSize)
													   .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
													   .setSharingMode(vk::SharingMode::eExclusive);

		constexpr auto allocationCreateInfo = vma::AllocationCreateInfo()
		.setUsage(vma::MemoryUsage::eAuto)
		.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo allocInfo;
		vma::raii::Buffer stagingBuffer(allocator, bufferCreateInfo, allocationCreateInfo, vk::Optional(allocInfo));

		std::memcpy(allocInfo.pMappedData, data.data(), imageSize);

		// Copy staging buffer to texture image and transition image layout
		auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer();
		cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const std::array imageMemoryBarrierBegin = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eTopOfPipe)
				.setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setSrcAccessMask(vk::AccessFlagBits2::eNone)
				.setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setOldLayout(vk::ImageLayout::eUndefined)
				.setNewLayout(vk::ImageLayout::eTransferDstOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(m_TextureImage[currentFrame])
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)
				)
		};

		cmdTransBuf.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierBegin)
		);

		cmdTransBuf.copyBufferToImage(
			stagingBuffer,
			m_TextureImage[currentFrame],
			vk::ImageLayout::eTransferDstOptimal,
			vk::BufferImageCopy()
				.setBufferOffset(0)
				.setBufferRowLength(0)
				.setBufferImageHeight(0)
				.setImageSubresource(vk::ImageSubresourceLayers()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setMipLevel(0)
					.setBaseArrayLayer(0)
					.setLayerCount(1))
				.setImageOffset(vk::Offset3D{ 0, 0, 0 })
				.setImageExtent(vk::Extent3D{
					m_Width,
					m_Height,
					1
				}
			)
		);

		const std::array imageMemoryBarrierEnd = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setDstStageMask(vk::PipelineStageFlagBits2::eNone)
				.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setDstAccessMask(vk::AccessFlagBits2::eNone)
				.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
				.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(m_TextureImage[currentFrame])
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)
				)
		};

		cmdTransBuf.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierEnd)
		);

		cmdTransBuf.end();

		auto semaphore = vkCore.GetSingleSemaphore();

		const auto cmdTransferSubmitInfo = vk::CommandBufferSubmitInfo()
			.setCommandBuffer(cmdTransBuf);
		const auto semaphoreTransferInfo = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eAllTransfer);

		const auto submitTransferInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdTransferSubmitInfo)
			.setSignalSemaphoreInfos(semaphoreTransferInfo);
		vkCore.GetTransferQueue().submit2(submitTransferInfo);

		auto cmdGraphicsBuf = vkCore.GetSingleTimeGraphicsCommandBuffer();
		cmdGraphicsBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		const std::array graphicsImageBuffer = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
				.setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eBottomOfPipe)
				.setSrcAccessMask(vk::AccessFlagBits2::eNone)
				.setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
				.setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
				.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(m_TextureImage[currentFrame])
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)
				)
		};
		cmdGraphicsBuf.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(graphicsImageBuffer)
		);
		cmdGraphicsBuf.end();

		const auto cmdInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdGraphicsBuf);
		const auto semaphoreInfo = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eBottomOfPipe);
		const auto submitGraphicsInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdInfo)
			.setWaitSemaphoreInfos(semaphoreInfo);
		vkCore.GetGraphicsQueue().submit2(submitGraphicsInfo);

		VulkanCore::GetInstance().AddVulkanEndOfFrameCleanUpFunction([cmdTransBuf = std::move(cmdTransBuf), cmdGraphicsBuf = std::move(cmdGraphicsBuf), semaphore = std::move(semaphore), stagingBuffer = std::move(stagingBuffer)]() -> void {});
	}

	VulkanGeneralBuffer::VulkanGeneralBuffer(const uSize size)
		: m_BufferSize(size)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		// Create uniform buffers for each frame in flight and allocate memory
		const vk::DeviceSize bufferSize = size;

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		constexpr auto bufferUsageInfo2 = vk::BufferUsageFlags2CreateInfo()
			.setUsage(
				vk::BufferUsageFlagBits2::eTransferDst |
				vk::BufferUsageFlagBits2::eUniformBuffer |
				vk::BufferUsageFlagBits2::eVertexBuffer |
				vk::BufferUsageFlagBits2::eIndexBuffer |
				vk::BufferUsageFlagBits2::eUniformBuffer |
				vk::BufferUsageFlagBits2::eTransferSrc |
				vk::BufferUsageFlagBits2::eStorageBuffer |
				vk::BufferUsageFlagBits2::eShaderDeviceAddress |
				vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR |
				vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR |
				vk::BufferUsageFlagBits2::eShaderBindingTableKHR
			);

		const auto bufferInfo = vk::BufferCreateInfo()
			.setPNext(&bufferUsageInfo2)
			.setSize(bufferSize)
			.setSharingMode(vk::SharingMode::eConcurrent)
			.setQueueFamilyIndices(queueIndices);

		constexpr vma::AllocationCreateInfo allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eGpuOnly)
			.setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

		m_Buffer = vma::raii::Buffer(allocator, bufferInfo, allocInfo);

		m_BufferDeviceAddress = vkCore.GetDevice().getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(*m_Buffer));
	}

	void VulkanGeneralBuffer::UpdateBufferDataImpl(const u8* data, uSize count, uSize offset)
	{
		if (data == nullptr)
		{
			Log<LogLevel::Error>("Data pointer is null in VulkanGeneralBuffer::UpdateBufferDataImpl");
			return;
		}

		if (count == 0)
			count = m_BufferSize;

		const auto& vkCore = VulkanCore::GetInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		// Create staging buffer
		const vk::DeviceSize bufferSize = count;

		const auto bufferCreateInfo = vk::BufferCreateInfo()
			.setSize(bufferSize)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive);

		constexpr auto allocationCreateInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAuto)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo allocInfo;
		vma::raii::Buffer stagingBuffer(allocator, bufferCreateInfo, allocationCreateInfo, vk::Optional(allocInfo));

		std::memcpy(allocInfo.pMappedData, data, bufferSize);

		auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer();
		cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const auto copyBufferSize = vk::BufferCopy2()
				.setSrcOffset(0)
				.setDstOffset(offset)
				.setSize(bufferSize);

		const auto copyBufferInfo = vk::CopyBufferInfo2()
			.setSrcBuffer(stagingBuffer)
			.setDstBuffer(m_Buffer)
			.setRegions(copyBufferSize);

		cmdTransBuf.copyBuffer2(copyBufferInfo);

		const auto transferBufferMemoryBarrier = vk::BufferMemoryBarrier2()
			.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
			.setDstAccessMask(vk::AccessFlagBits2::eNone)
			.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
			.setDstStageMask(vk::PipelineStageFlagBits2::eNone)
			.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setBuffer(m_Buffer)
			.setOffset(0)
			.setSize(bufferSize);

		cmdTransBuf.pipelineBarrier2(vk::DependencyInfo()
			.setBufferMemoryBarriers(transferBufferMemoryBarrier)
		);
		cmdTransBuf.end();

		const auto fence = device.createFence(vk::FenceCreateInfo());
		const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdTransBuf);
		const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
		vkCore.GetTransferQueue().submit2(submitInfo, fence);
		if (device.waitForFences(*fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for fence in VulkanGeneralBuffer::UpdateBufferDataImpl");
		}
	}
}
