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
				.setImage(*m_TextureImage[i])
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
		if (device.waitForFences(*fence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for fence in VulkanGeneralBuffer::UpdateBufferDataImpl");
		}
	}

	VulkanTextureArraySampler::VulkanTextureArraySampler()
	{
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();
		m_Sampler = vk::raii::Sampler(device, vk::SamplerCreateInfo()
			.setMagFilter(vk::Filter::eLinear)
			.setMinFilter(vk::Filter::eLinear)
			.setAddressModeU(vk::SamplerAddressMode::eRepeat)
			.setAddressModeV(vk::SamplerAddressMode::eRepeat)
			.setAddressModeW(vk::SamplerAddressMode::eRepeat) // TODO: make this based on gltf data
			.setAnisotropyEnable(vk::True)
			.setMaxAnisotropy(vkCore.GetPhysicalDev().getProperties().limits.maxSamplerAnisotropy)
			.setBorderColor(vk::BorderColor::eIntOpaqueBlack)
			.setUnnormalizedCoordinates(vk::False)
			.setCompareEnable(vk::False)
			.setCompareOp(vk::CompareOp::eAlways)
			.setMipmapMode(vk::SamplerMipmapMode::eLinear)
			.setMipLodBias(0.0f)
			.setMinLod(0.0f)
			.setMaxLod(0.0f));
	}

	VulkanTextureArraySampler::VulkanTextureArraySampler(const tg3_sampler& sampler)
	{
		auto convertToVKFilter = [](const decltype(sampler.min_filter) filter) -> vk::Filter {
			switch (filter)
			{
			case TG3_TEXTURE_FILTER_NEAREST:
			case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
			case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
				return vk::Filter::eNearest;
			case -1: // tinygltf_v3 default
			case TG3_TEXTURE_FILTER_LINEAR:
			case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
			case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
				return vk::Filter::eLinear;
			default:
				Log<LogLevel::Error>("Unsupported filter in VulkanTextureArraySampler");
				return vk::Filter::eNearest;
			}
		};

		auto convertToVKWrap = [](const decltype(sampler.wrap_s) wrap) -> vk::SamplerAddressMode {
			switch (wrap)
			{
			case TG3_TEXTURE_WRAP_CLAMP_TO_EDGE:
				return vk::SamplerAddressMode::eClampToEdge;
			case TG3_TEXTURE_WRAP_REPEAT:
				return vk::SamplerAddressMode::eRepeat;
			case TG3_TEXTURE_WRAP_MIRRORED_REPEAT:
				return vk::SamplerAddressMode::eMirroredRepeat;
			default:
				Log<LogLevel::Error>("Unsupported wrap mode in VulkanTextureArraySampler");
				return vk::SamplerAddressMode::eClampToEdge;
			}
		};

		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();
		m_Sampler = vk::raii::Sampler(device, vk::SamplerCreateInfo()
			.setMagFilter(convertToVKFilter(sampler.mag_filter))
			.setMinFilter(convertToVKFilter(sampler.min_filter))
			.setAddressModeU(convertToVKWrap(sampler.wrap_s))
			.setAddressModeV(convertToVKWrap(sampler.wrap_t))
			.setAddressModeW(vk::SamplerAddressMode::eClampToEdge) // TODO: make this based on gltf data
			.setAnisotropyEnable(vk::True)
			.setMaxAnisotropy(vkCore.GetPhysicalDev().getProperties().limits.maxSamplerAnisotropy)
			.setBorderColor(vk::BorderColor::eIntOpaqueBlack)
			.setUnnormalizedCoordinates(vk::False)
			.setCompareEnable(vk::False)
			.setCompareOp(vk::CompareOp::eAlways)
			.setMipmapMode(vk::SamplerMipmapMode::eLinear)
			.setMipLodBias(0.0f)
			.setMinLod(0.0f)
			.setMaxLod(0.0f));
	}

	VulkanEmptyTextureArray::VulkanEmptyTextureArray()
	{
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		auto imageCreateInfo = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(vk::Format::eR8G8B8A8Srgb)
			.setMipLevels(1)
			.setArrayLayers(1)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setExtent(vk::Extent3D()
			.setWidth(1)
			.setHeight(1)
			.setDepth(1));

		auto scratchBufferInfo = vk::BufferCreateInfo()
			.setSize(1)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setSize(1 * 1 * sizeof(u8) * 4);

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

		m_TextureImages = vma::raii::Image(allocator, imageCreateInfo, allocInfo);

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		vma::raii::Buffer scratchBuffer(allocator, scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));

		const auto imageViewCreateInfo = vk::ImageViewCreateInfo()
				.setImage(*m_TextureImages)
				.setViewType(vk::ImageViewType::e2DArray)
				.setFormat(vk::Format::eR8G8B8A8Srgb)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1));

		m_TextureImageView = vk::raii::ImageView(device, imageViewCreateInfo);

		Vec4 black{0.0f};
		std::memcpy(scratchBufferAllocInfo.pMappedData, &black, sizeof(Vec4));

		auto cmd = vkCore.GetSingleTimeTransferCommandBuffer();
		cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

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
				.setImage(*m_TextureImages)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1))
		};

		cmd.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierBegin)
		);

		cmd.copyBufferToImage(
			scratchBuffer,
			*m_TextureImages,
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
					imageCreateInfo.extent.width,
					imageCreateInfo.extent.height,
					1
				})
		);

		cmd.end();

		const auto fence = device.createFence(vk::FenceCreateInfo());
		const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
		const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
		vkCore.GetTransferQueue().submit2(submitInfo, fence);

		if (device.waitForFences(*fence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for fence in VulkanColourTextureArray constructor");
		}

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const std::array imageMemoryBarrierEnd = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setDstStageMask(vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
				.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
				.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
				.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(*m_TextureImages)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1))
		};

		cmdFinal.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierEnd)
		);

		cmdFinal.end();

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, finalFence);

		if (device.waitForFences(*finalFence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanColourTextureArray constructor");
		}
	}

	VulkanColourTextureArray::VulkanColourTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		auto imageCreateInfo = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(vk::Format::eR8G8B8A8Srgb)
			.setMipLevels(1)
			.setArrayLayers(indexing.size())
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined);

		auto scratchBufferInfo = vk::BufferCreateInfo()
			.setSize(1)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive);

		{
			ImageLoader<u8, 4, glm::packed_highp> imageLoader;
			if (model.images->buffer_view != -1)
			{
				const tg3_buffer_view& bufferView = model.buffer_views[model.images->buffer_view];
				imageLoader = ImageLoader<u8, 4, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
			}
			else
			{
				const std::string imagePath = std::string{ pathToGltf } + "/" + std::string(model.images->uri.data, model.images->uri.len);
				imageLoader = ImageLoader<u8, 4, glm::packed_highp>(imagePath);
			}

			imageCreateInfo.setExtent(vk::Extent3D()
				.setWidth(static_cast<u32>(imageLoader.GetWidth()))
				.setHeight(static_cast<u32>(imageLoader.GetHeight()))
				.setDepth(1));

			scratchBufferInfo.setSize(imageLoader.GetWidth() * imageLoader.GetHeight() * sizeof(u8) * 4);

			Log<LogLevel::Trace>("Loading {} colour textures at {}x{}. This might take some time please have some patience.", indexing.size(), imageLoader.GetWidth(), imageLoader.GetHeight());
		}

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

		m_TextureImages = vma::raii::Image(allocator, imageCreateInfo, allocInfo);

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		vma::raii::Buffer scratchBuffer(allocator, scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));

		const auto imageViewCreateInfo = vk::ImageViewCreateInfo()
				.setImage(*m_TextureImages)
				.setViewType(vk::ImageViewType::e2DArray)
				.setFormat(vk::Format::eR8G8B8A8Srgb)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(indexing.size()));

		m_TextureImageView = vk::raii::ImageView(device, imageViewCreateInfo);

		ImageLoader<u8, 4, glm::packed_highp> imageLoaderPreLoad;
		auto nextTextureIndex = indexing[0];
		if (model.images[nextTextureIndex].buffer_view != -1)
		{
			const tg3_buffer_view& bufferView = model.buffer_views[model.images[nextTextureIndex].buffer_view];
			imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
		}
		else
		{
			const std::string imagePath = std::string(pathToGltf) + "/" + std::string(model.images[nextTextureIndex].uri.data, model.images[nextTextureIndex].uri.len);
			imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(imagePath);
		}

		for (u32 i = 0; i < indexing.size(); i++)
		{
			if ((i + 1) % 5 == 0 || i + 1 == indexing.size())
				Log<LogLevel::Trace>("Loading texture {}/{}", i + 1, indexing.size());

			ImageLoader<u8, 4, glm::packed_highp> imageLoader = std::move(imageLoaderPreLoad);

			assert(imageCreateInfo.extent.width == imageLoader.GetWidth() && imageCreateInfo.extent.height == imageLoader.GetHeight() && "All images in the texture array must have the same dimensions");

			std::memcpy(scratchBufferAllocInfo.pMappedData, imageLoader.GetImageData().data(), scratchBufferInfo.size);

			auto cmd = vkCore.GetSingleTimeTransferCommandBuffer();
			cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

			// Only transition layout on first texture
			if (i == 0)
			{
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
						.setImage(*m_TextureImages)
						.setSubresourceRange(vk::ImageSubresourceRange()
							.setAspectMask(vk::ImageAspectFlagBits::eColor)
							.setBaseMipLevel(0)
							.setLevelCount(1)
							.setBaseArrayLayer(0)
							.setLayerCount(indexing.size()))
				};

				cmd.pipelineBarrier2(vk::DependencyInfo()
					.setImageMemoryBarriers(imageMemoryBarrierBegin)
				);
			}

			cmd.copyBufferToImage(
				scratchBuffer,
				*m_TextureImages,
				vk::ImageLayout::eTransferDstOptimal,
				vk::BufferImageCopy()
					.setBufferOffset(0)
					.setBufferRowLength(0)
					.setBufferImageHeight(0)
					.setImageSubresource(vk::ImageSubresourceLayers()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setMipLevel(0)
						.setBaseArrayLayer(i)
						.setLayerCount(1))
					.setImageOffset(vk::Offset3D{ 0, 0, 0 })
					.setImageExtent(vk::Extent3D{
						imageCreateInfo.extent.width,
						imageCreateInfo.extent.height,
						1
					})
			);

			cmd.end();

			const auto fence = device.createFence(vk::FenceCreateInfo());
			const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
			const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
			vkCore.GetTransferQueue().submit2(submitInfo, fence);

			if (i + 1 < indexing.size())
			{
				nextTextureIndex = indexing[i + 1];
				if (model.images[nextTextureIndex].buffer_view != -1)
				{
					const tg3_buffer_view& bufferView = model.buffer_views[model.images[nextTextureIndex].buffer_view];
					imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
				}
				else
				{
					const std::string imagePath = std::string(pathToGltf) + "/" + std::string(model.images[nextTextureIndex].uri.data, model.images[nextTextureIndex].uri.len);
					imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(imagePath);
				}
			}

			if (device.waitForFences(*fence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
			{
				Log<LogLevel::Error>("Failed to wait for fence in VulkanColourTextureArray constructor");
			}
		}

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const std::array imageMemoryBarrierEnd = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setDstStageMask(vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
				.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
				.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
				.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(*m_TextureImages)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(indexing.size()))
		};

		cmdFinal.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierEnd)
		);

		cmdFinal.end();

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, finalFence);

		if (device.waitForFences(*finalFence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanColourTextureArray constructor");
		}
	}

	/*VulkanNormalTextureArray::VulkanNormalTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		auto imageCreateInfo = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(vk::Format::eR8G8Unorm)
			.setMipLevels(1)
			.setArrayLayers(indexing.size())
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined);

		auto scratchBufferInfo = vk::BufferCreateInfo()
			.setSize(1)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive);

		{
			ImageLoader<u8, 2, glm::packed_highp> imageLoader;
			if (model.images->buffer_view != -1)
			{
				const tg3_buffer_view& bufferView = model.buffer_views[model.images->buffer_view];
				imageLoader = ImageLoader<u8, 2, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
			}
			else
			{
				const std::string imagePath = std::string{ pathToGltf } + "/" + std::string(model.images->uri.data, model.images->uri.len);
				imageLoader = ImageLoader<u8, 2, glm::packed_highp>(imagePath);
			}

			imageCreateInfo.setExtent(vk::Extent3D()
				.setWidth(static_cast<u32>(imageLoader.GetWidth()))
				.setHeight(static_cast<u32>(imageLoader.GetHeight()))
				.setDepth(1));

			scratchBufferInfo.setSize(imageLoader.GetWidth() * imageLoader.GetHeight() * sizeof(u8) * 2);

			Log<LogLevel::Trace>("Loading {} normal textures  at {}x{}. This might take some time please have some patience.", indexing.size(), imageLoader.GetWidth(), imageLoader.GetHeight());
		}

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

		m_TextureImages = vma::raii::Image(allocator, imageCreateInfo, allocInfo);

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		vma::raii::Buffer scratchBuffer(allocator, scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));

		const auto imageViewCreateInfo = vk::ImageViewCreateInfo()
				.setImage(*m_TextureImages)
				.setViewType(vk::ImageViewType::e2DArray)
				.setFormat(vk::Format::eR8G8Unorm)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(indexing.size()));

		m_TextureImageView = vk::raii::ImageView(device, imageViewCreateInfo);

		ImageLoader<u8, 2, glm::packed_highp> imageLoaderPreLoad;
		auto nextTextureIndex = indexing[0];
		if (model.images[nextTextureIndex].buffer_view != -1)
		{
			const tg3_buffer_view& bufferView = model.buffer_views[model.images[nextTextureIndex].buffer_view];
			imageLoaderPreLoad = ImageLoader<u8, 2, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
		}
		else
		{
			const std::string imagePath = std::string(pathToGltf) + "/" + std::string(model.images[nextTextureIndex].uri.data, model.images[nextTextureIndex].uri.len);
			imageLoaderPreLoad = ImageLoader<u8, 2, glm::packed_highp>(imagePath);
		}

		for (u32 i = 0; i < indexing.size(); i++)
		{
			if ((i + 1) % 5 == 0 || i + 1 == indexing.size())
				Log<LogLevel::Trace>("Loading texture {}/{}", i + 1, indexing.size());

			ImageLoader<u8, 2, glm::packed_highp> imageLoader = std::move(imageLoaderPreLoad);

			assert(imageCreateInfo.extent.width == imageLoader.GetWidth() && imageCreateInfo.extent.height == imageLoader.GetHeight() && "All images in the texture array must have the same dimensions");

			std::memcpy(scratchBufferAllocInfo.pMappedData, imageLoader.GetImageData().data(), scratchBufferInfo.size);

			auto cmd = vkCore.GetSingleTimeTransferCommandBuffer();
			cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

			// Only transition layout on first texture
			if (i == 0)
			{
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
						.setImage(*m_TextureImages)
						.setSubresourceRange(vk::ImageSubresourceRange()
							.setAspectMask(vk::ImageAspectFlagBits::eColor)
							.setBaseMipLevel(0)
							.setLevelCount(1)
							.setBaseArrayLayer(0)
							.setLayerCount(indexing.size()))
				};

				cmd.pipelineBarrier2(vk::DependencyInfo()
					.setImageMemoryBarriers(imageMemoryBarrierBegin)
				);
			}

			cmd.copyBufferToImage(
				scratchBuffer,
				*m_TextureImages,
				vk::ImageLayout::eTransferDstOptimal,
				vk::BufferImageCopy()
					.setBufferOffset(0)
					.setBufferRowLength(0)
					.setBufferImageHeight(0)
					.setImageSubresource(vk::ImageSubresourceLayers()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setMipLevel(0)
						.setBaseArrayLayer(i)
						.setLayerCount(1))
					.setImageOffset(vk::Offset3D{ 0, 0, 0 })
					.setImageExtent(vk::Extent3D{
						imageCreateInfo.extent.width,
						imageCreateInfo.extent.height,
						1
					})
			);

			cmd.end();

			const auto fence = device.createFence(vk::FenceCreateInfo());
			const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
			const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
			vkCore.GetTransferQueue().submit2(submitInfo, fence);

			if (i + 1 < indexing.size())
			{
				// preload the next image while moving the current one to the GPU
				nextTextureIndex = indexing[i + 1];
				if (model.images[nextTextureIndex].buffer_view != -1)
				{
					const tg3_buffer_view& bufferView = model.buffer_views[model.images[nextTextureIndex].buffer_view];
					imageLoaderPreLoad = ImageLoader<u8, 2, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
				}
				else
				{
					const std::string imagePath = std::string(pathToGltf) + "/" + std::string(model.images[nextTextureIndex].uri.data, model.images[nextTextureIndex].uri.len);
					imageLoaderPreLoad = ImageLoader<u8, 2, glm::packed_highp>(imagePath);
				}
			}

			if (device.waitForFences(*fence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
			{
				Log<LogLevel::Error>("Failed to wait for fence in VulkanColourTextureArray constructor");
			}
		}

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const std::array imageMemoryBarrierEnd = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setDstStageMask(vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
				.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
				.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
				.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(*m_TextureImages)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(indexing.size()))
		};

		cmdFinal.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierEnd)
		);

		cmdFinal.end();

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, finalFence);

		if (device.waitForFences(*finalFence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanNormalTextureArray constructor");

		}
	}*/

	VulkanNormalTextureArray::VulkanNormalTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		auto imageCreateInfo = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(vk::Format::eR8G8B8A8Unorm)
			.setMipLevels(1)
			.setArrayLayers(indexing.size())
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined);

		auto scratchBufferInfo = vk::BufferCreateInfo()
			.setSize(1)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive);

		{
			ImageLoader<u8, 4, glm::packed_highp> imageLoader;
			if (model.images->buffer_view != -1)
			{
				const tg3_buffer_view& bufferView = model.buffer_views[model.images->buffer_view];
				imageLoader = ImageLoader<u8, 4, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
			}
			else
			{
				const std::string imagePath = std::string{ pathToGltf } + "/" + std::string(model.images->uri.data, model.images->uri.len);
				imageLoader = ImageLoader<u8, 4, glm::packed_highp>(imagePath);
			}

			imageCreateInfo.setExtent(vk::Extent3D()
				.setWidth(static_cast<u32>(imageLoader.GetWidth()))
				.setHeight(static_cast<u32>(imageLoader.GetHeight()))
				.setDepth(1));

			scratchBufferInfo.setSize(imageLoader.GetWidth() * imageLoader.GetHeight() * sizeof(u8) * 4);

			Log<LogLevel::Trace>("Loading {} normal textures  at {}x{}. This might take some time please have some patience.", indexing.size(), imageLoader.GetWidth(), imageLoader.GetHeight());
		}

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

		m_TextureImages = vma::raii::Image(allocator, imageCreateInfo, allocInfo);

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		vma::raii::Buffer scratchBuffer(allocator, scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));

		const auto imageViewCreateInfo = vk::ImageViewCreateInfo()
				.setImage(*m_TextureImages)
				.setViewType(vk::ImageViewType::e2DArray)
				.setFormat(vk::Format::eR8G8B8A8Unorm)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(indexing.size()));

		m_TextureImageView = vk::raii::ImageView(device, imageViewCreateInfo);

		ImageLoader<u8, 4, glm::packed_highp> imageLoaderPreLoad;
		auto nextTextureIndex = indexing[0];
		if (model.images[nextTextureIndex].buffer_view != -1)
		{
			const tg3_buffer_view& bufferView = model.buffer_views[model.images[nextTextureIndex].buffer_view];
			imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
		}
		else
		{
			const std::string imagePath = std::string(pathToGltf) + "/" + std::string(model.images[nextTextureIndex].uri.data, model.images[nextTextureIndex].uri.len);
			imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(imagePath);
		}

		for (u32 i = 0; i < indexing.size(); i++)
		{
			if ((i + 1) % 5 == 0 || i + 1 == indexing.size())
				Log<LogLevel::Trace>("Loading texture {}/{}", i + 1, indexing.size());

			ImageLoader<u8, 4, glm::packed_highp> imageLoader = std::move(imageLoaderPreLoad);

			assert(imageCreateInfo.extent.width == imageLoader.GetWidth() && imageCreateInfo.extent.height == imageLoader.GetHeight() && "All images in the texture array must have the same dimensions");

			std::memcpy(scratchBufferAllocInfo.pMappedData, imageLoader.GetImageData().data(), scratchBufferInfo.size);

			auto cmd = vkCore.GetSingleTimeTransferCommandBuffer();
			cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

			// Only transition layout on first texture
			if (i == 0)
			{
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
						.setImage(*m_TextureImages)
						.setSubresourceRange(vk::ImageSubresourceRange()
							.setAspectMask(vk::ImageAspectFlagBits::eColor)
							.setBaseMipLevel(0)
							.setLevelCount(1)
							.setBaseArrayLayer(0)
							.setLayerCount(indexing.size()))
				};

				cmd.pipelineBarrier2(vk::DependencyInfo()
					.setImageMemoryBarriers(imageMemoryBarrierBegin)
				);
			}

			cmd.copyBufferToImage(
				scratchBuffer,
				*m_TextureImages,
				vk::ImageLayout::eTransferDstOptimal,
				vk::BufferImageCopy()
					.setBufferOffset(0)
					.setBufferRowLength(0)
					.setBufferImageHeight(0)
					.setImageSubresource(vk::ImageSubresourceLayers()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setMipLevel(0)
						.setBaseArrayLayer(i)
						.setLayerCount(1))
					.setImageOffset(vk::Offset3D{ 0, 0, 0 })
					.setImageExtent(vk::Extent3D{
						imageCreateInfo.extent.width,
						imageCreateInfo.extent.height,
						1
					})
			);

			cmd.end();

			const auto fence = device.createFence(vk::FenceCreateInfo());
			const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
			const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
			vkCore.GetTransferQueue().submit2(submitInfo, fence);

			if (i + 1 < indexing.size())
			{
				// preload the next image while moving the current one to the GPU
				nextTextureIndex = indexing[i + 1];
				if (model.images[nextTextureIndex].buffer_view != -1)
				{
					const tg3_buffer_view& bufferView = model.buffer_views[model.images[nextTextureIndex].buffer_view];
					imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
				}
				else
				{
					const std::string imagePath = std::string(pathToGltf) + "/" + std::string(model.images[nextTextureIndex].uri.data, model.images[nextTextureIndex].uri.len);
					imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(imagePath);
				}
			}

			if (device.waitForFences(*fence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
			{
				Log<LogLevel::Error>("Failed to wait for fence in VulkanColourTextureArray constructor");
			}
		}

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const std::array imageMemoryBarrierEnd = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setDstStageMask(vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
				.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
				.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
				.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(*m_TextureImages)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(indexing.size()))
		};

		cmdFinal.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierEnd)
		);

		cmdFinal.end();

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, finalFence);

		if (device.waitForFences(*finalFence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanNormalTextureArray constructor");
		}
	}

	VulkanMetallicRoughnessTextureArray::VulkanMetallicRoughnessTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		auto imageCreateInfo = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(vk::Format::eR8G8Unorm)
			.setMipLevels(1)
			.setArrayLayers(indexing.size())
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setInitialLayout(vk::ImageLayout::eUndefined);

		auto scratchBufferInfo = vk::BufferCreateInfo()
			.setSize(1)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive);

		{
			ImageLoader<u8, 4, glm::packed_highp> imageLoader;
			if (model.images->buffer_view != -1)
			{
				const tg3_buffer_view& bufferView = model.buffer_views[model.images->buffer_view];
				imageLoader = ImageLoader<u8, 4, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
			}
			else
			{
				const std::string imagePath = std::string{ pathToGltf } + "/" + std::string(model.images->uri.data, model.images->uri.len);
				imageLoader = ImageLoader<u8, 4, glm::packed_highp>(imagePath);
			}

			imageCreateInfo.setExtent(vk::Extent3D()
				.setWidth(static_cast<u32>(imageLoader.GetWidth()))
				.setHeight(static_cast<u32>(imageLoader.GetHeight()))
				.setDepth(1));

			scratchBufferInfo.setSize(imageLoader.GetWidth() * imageLoader.GetHeight() * sizeof(u8) * 2);

			Log<LogLevel::Trace>("Loading {} metallic-roughness textures  at {}x{}. This might take some time please have some patience.", indexing.size(), imageLoader.GetWidth(), imageLoader.GetHeight());
		}

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eDedicatedMemory);

		m_TextureImages = vma::raii::Image(allocator, imageCreateInfo, allocInfo);

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		vma::raii::Buffer scratchBuffer(allocator, scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));

		const auto imageViewCreateInfo = vk::ImageViewCreateInfo()
				.setImage(*m_TextureImages)
				.setViewType(vk::ImageViewType::e2DArray)
				.setFormat(vk::Format::eR8G8Unorm)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(indexing.size()));

		m_TextureImageView = vk::raii::ImageView(device, imageViewCreateInfo);

		ImageLoader<u8, 4, glm::packed_highp> imageLoaderPreLoad;
		auto nextTextureIndex = indexing[0];
		if (model.images[nextTextureIndex].buffer_view != -1)
		{
			const tg3_buffer_view& bufferView = model.buffer_views[model.images[nextTextureIndex].buffer_view];
			imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
		}
		else
		{
			const std::string imagePath = std::string(pathToGltf) + "/" + std::string(model.images[nextTextureIndex].uri.data, model.images[nextTextureIndex].uri.len);
			imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(imagePath);
		}

		for (u32 i = 0; i < indexing.size(); i++)
		{
			if ((i + 1) % 5 == 0 || i + 1 == indexing.size())
				Log<LogLevel::Trace>("Loading texture {}/{}", i + 1, indexing.size());

			ImageLoader<u8, 4, glm::packed_highp> imageLoader = std::move(imageLoaderPreLoad);

			assert(imageCreateInfo.extent.width == imageLoader.GetWidth() && imageCreateInfo.extent.height == imageLoader.GetHeight() && "All images in the texture array must have the same dimensions");

			// Extract G and B channels from RGBA source and place them into R and G channels of the 2-channel target
			const auto& sourceData = imageLoader.GetImageData();
			auto* destPtr = static_cast<u8*>(scratchBufferAllocInfo.pMappedData);

			for (u32 pixelIndex = 0; pixelIndex < sourceData.size(); pixelIndex++)
			{
				destPtr[pixelIndex * 2] = sourceData[pixelIndex][1];      // G -> R
				destPtr[pixelIndex * 2 + 1] = sourceData[pixelIndex][2];  // B -> G
			}

			auto cmd = vkCore.GetSingleTimeTransferCommandBuffer();
			cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

			// Only transition layout on first texture
			if (i == 0)
			{
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
						.setImage(*m_TextureImages)
						.setSubresourceRange(vk::ImageSubresourceRange()
							.setAspectMask(vk::ImageAspectFlagBits::eColor)
							.setBaseMipLevel(0)
							.setLevelCount(1)
							.setBaseArrayLayer(0)
							.setLayerCount(indexing.size()))
				};

				cmd.pipelineBarrier2(vk::DependencyInfo()
					.setImageMemoryBarriers(imageMemoryBarrierBegin)
				);
			}

			cmd.copyBufferToImage(
				scratchBuffer,
				*m_TextureImages,
				vk::ImageLayout::eTransferDstOptimal,
				vk::BufferImageCopy()
					.setBufferOffset(0)
					.setBufferRowLength(0)
					.setBufferImageHeight(0)
					.setImageSubresource(vk::ImageSubresourceLayers()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setMipLevel(0)
						.setBaseArrayLayer(i)
						.setLayerCount(1))
					.setImageOffset(vk::Offset3D{ 0, 0, 0 })
					.setImageExtent(vk::Extent3D{
						imageCreateInfo.extent.width,
						imageCreateInfo.extent.height,
						1
					})
			);

			cmd.end();

			const auto fence = device.createFence(vk::FenceCreateInfo());
			const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
			const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
			vkCore.GetTransferQueue().submit2(submitInfo, fence);

			if (i + 1 < indexing.size())
			{
				// preload the next image while moving the current one to the GPU
				nextTextureIndex = indexing[i + 1];
				if (model.images[nextTextureIndex].buffer_view != -1)
				{
					const tg3_buffer_view& bufferView = model.buffer_views[model.images[nextTextureIndex].buffer_view];
					imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(std::span(std::bit_cast<std::byte*>(model.buffers[bufferView.buffer].data.data) + bufferView.byte_offset, bufferView.byte_length));
				}
				else
				{
					const std::string imagePath = std::string(pathToGltf) + "/" + std::string(model.images[nextTextureIndex].uri.data, model.images[nextTextureIndex].uri.len);
					imageLoaderPreLoad = ImageLoader<u8, 4, glm::packed_highp>(imagePath);
				}
			}

			if (device.waitForFences(*fence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
			{
				Log<LogLevel::Error>("Failed to wait for fence in VulkanColourTextureArray constructor");
			}
		}

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const std::array imageMemoryBarrierEnd = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setDstStageMask(vk::PipelineStageFlagBits2::eRayTracingShaderKHR)
				.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
				.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
				.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(*m_TextureImages)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(indexing.size()))
		};

		cmdFinal.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierEnd)
		);

		cmdFinal.end();

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, finalFence);

		if (device.waitForFences(*finalFence, vk::True, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanMetallicRoughnessTextureArray constructor");
		}
	}
}
