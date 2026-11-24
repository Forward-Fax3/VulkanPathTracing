#include "Core.hpp"
#include "VulkanBuffers.hpp"
#include "VulkanCore.hpp"
#include "VulkanRendererCore.hpp"


namespace OWC::Graphics
{
	//--------------------------------------------------------
	// VulkanUniformBuffer
	//--------------------------------------------------------

	VulkanVertexBuffer::VulkanVertexBuffer(const uSize vertexSize)
		: m_VertexSize(vertexSize) {}

	VulkanVertexBuffer::VulkanVertexBuffer(const uSize size, const uSize vertexSize)
		: m_NumOfVertices(size), m_VertexSize(vertexSize)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		const auto bufferInfo = vk::BufferCreateInfo()
			.setSize(m_NumOfVertices * m_VertexSize)
			.setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eConcurrent)
			.setQueueFamilyIndices(queueIndices);

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		auto vertBuf = allocator.createBuffer(bufferInfo, allocInfo);
		if (!vertBuf)
			Log<LogLevel::Critical>("Failed to create Vulkan vertex buffer: {}", vk::to_string(vertBuf.error()));
		m_VertexBuffer = std::move(vertBuf).value();
	}

	void VulkanVertexBuffer::Resize(const uSize size)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		const auto bufferInfo = vk::BufferCreateInfo()
			.setSize(size * m_VertexSize)
			.setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eConcurrent)
			.setQueueFamilyIndices(queueIndices);

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		VulkanCore::AddVulkanEndOfFrameCleanUpFunction([oldBuffer = std::move(m_VertexBuffer)](){});

		auto vertBuf = allocator.createBuffer(bufferInfo, allocInfo);
		if (!vertBuf)
			Log<LogLevel::Critical>("Failed to create Vulkan vertex buffer: {}", vk::to_string(vertBuf.error()));
		m_VertexBuffer = std::move(vertBuf).value();
		m_NumOfVertices = size;
	}

	void VulkanVertexBuffer::ImplUpdate(std::span<const std::byte> data, const uSize offset)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const auto bufferCreateInfo = vk::BufferCreateInfo()
			.setSize(data.size())
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive);

		constexpr auto allocCreateInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo stagingBufferAllocationInfo;
		auto stagingBufferResult = allocator.createBuffer(bufferCreateInfo, allocCreateInfo, vk::Optional(stagingBufferAllocationInfo));
		if (!stagingBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan staging buffer: {}", vk::to_string(stagingBufferResult.error()));
		const auto& stagingBuffer = stagingBufferResult.value();

		std::memcpy(stagingBufferAllocationInfo.pMappedData, data.data(), data.size());

		const auto token = vkCore.GetTransferPoolToken();
		auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer(token);
		if (const auto cmdBeginResult = cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdBeginResult)
			Log<LogLevel::Critical>("Failed to begin command buffer: {}", vk::to_string(cmdBeginResult.error()));

		const auto copyBufferSize = vk::BufferCopy2()
				.setSrcOffset(0)
				.setDstOffset(offset)
				.setSize(data.size());
		const auto copyBufferInfo = vk::CopyBufferInfo2()
			.setSrcBuffer(stagingBuffer)
			.setDstBuffer(m_VertexBuffer)
			.setRegions(copyBufferSize);

		cmdTransBuf.copyBuffer2(copyBufferInfo);

		const auto transferBufferMemoryBarrier = vk::BufferMemoryBarrier2()
			.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
			.setDstAccessMask(vk::AccessFlagBits2::eNone)
			.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
			.setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)
			.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setBuffer(m_VertexBuffer)
			.setOffset(offset)
			.setSize(data.size());

		cmdTransBuf.pipelineBarrier2(vk::DependencyInfo()
			.setBufferMemoryBarriers(transferBufferMemoryBarrier)
		);
		 if (const auto cmdEndResult = cmdTransBuf.end(); !cmdEndResult)
			Log<LogLevel::Critical>("Failed to end command buffer: {}", vk::to_string(cmdEndResult.error()));

		auto semaphore = vkCore.GetSingleSemaphore();

		const auto cmdTransferSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdTransBuf);
		const auto semaphoreSubmit = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eTransfer);

		const auto transferSubmitInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdTransferSubmit)
			.setSignalSemaphoreInfos(semaphoreSubmit);

		if (const auto submitResult = vkCore.SubmitTransferCommandBuffer(transferSubmitInfo); !submitResult)
			Log<LogLevel::Critical>("Failed to submit transfer command buffer: {}", vk::to_string(submitResult.error()));

		vkCore.ReturnTransferPoolToken(token);

		auto cmdGraphicsBuffer = vkCore.GetSingleTimeGraphicsCommandBuffer();
		 if (const auto cmdBeginResult = cmdGraphicsBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdBeginResult)
			Log<LogLevel::Critical>("Failed to begin graphics command buffer: {}", vk::to_string(cmdBeginResult.error()));

		const auto GraphicsBufferMemoryBarrier = vk::BufferMemoryBarrier2()
			.setSrcAccessMask(vk::AccessFlagBits2::eNone)
			.setDstAccessMask(vk::AccessFlagBits2::eVertexAttributeRead)
			.setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
			.setDstStageMask(vk::PipelineStageFlagBits2::eVertexInput)
			.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setBuffer(m_VertexBuffer)
			.setOffset(offset)
			.setSize(data.size());

		cmdGraphicsBuffer.pipelineBarrier2(vk::DependencyInfo()
			.setBufferMemoryBarriers(GraphicsBufferMemoryBarrier)
		);

		if (const auto cmdEndResult = cmdGraphicsBuffer.end(); !cmdEndResult)
			Log<LogLevel::Critical>("Failed to end graphics command buffer: {}", vk::to_string(cmdEndResult.error()));

		const auto cmdGraphicsSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdGraphicsBuffer);

		const auto graphicsSemaphore = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eVertexInput);

		 if (const auto submitResult = vkCore.GetGraphicsQueue().submit2(vk::SubmitInfo2()
			.setCommandBufferInfos(cmdGraphicsSubmitInfo)
			.setWaitSemaphoreInfos(graphicsSemaphore)
		); !submitResult)
			Log<LogLevel::Critical>("Failed to submit graphics command buffer: {}", vk::to_string(submitResult.error()));

		VulkanCore::AddVulkanEndOfFrameCleanUpFunction([stagingBuffer = std::move(stagingBufferResult).value(), semaphore = std::move(semaphore), cmdTransBuf = std::move(cmdTransBuf), cmdGraphicsBuffer = std::move(cmdGraphicsBuffer)](){});
	}

	VulkanIndexBuffer::VulkanIndexBuffer(const uSize size)
		: m_NumOfIndices(size)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		const auto bufferInfo = vk::BufferCreateInfo()
			.setSize(m_NumOfIndices * sizeof(u32))
			.setUsage(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eConcurrent)
			.setQueueFamilyIndices(queueIndices);

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		auto indexBuf = allocator.createBuffer(bufferInfo, allocInfo);
		if (!indexBuf)
			Log<LogLevel::Critical>("Failed to create Vulkan index buffer: {}", vk::to_string(indexBuf.error()));
		m_IndexBuffer = std::move(indexBuf).value();
	}

	void VulkanIndexBuffer::Resize(const uSize size)
	{
		m_NumOfIndices = size;

		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		const auto bufferInfo = vk::BufferCreateInfo()
			.setSize(m_NumOfIndices * sizeof(u32))
			.setUsage(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst)
			.setSharingMode(vk::SharingMode::eConcurrent)
			.setQueueFamilyIndices(queueIndices);

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		auto indexBuf = allocator.createBuffer(bufferInfo, allocInfo);
		if (!indexBuf)
			Log<LogLevel::Critical>("Failed to create Vulkan index buffer: {}", vk::to_string(indexBuf.error()));
		VulkanCore::AddVulkanEndOfFrameCleanUpFunction([oldBuffer = std::move(m_IndexBuffer)](){});
		m_IndexBuffer = std::move(indexBuf).value();
	}

	void VulkanIndexBuffer::ImplUpdate(std::span<const std::byte> data, const uSize offset)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const auto bufferCreateInfo = vk::BufferCreateInfo()
			.setSize(data.size())
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive);

		constexpr auto allocCreateInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo stagingBufferAllocationInfo;
		auto stagingBufferResult = allocator.createBuffer(bufferCreateInfo, allocCreateInfo, vk::Optional(stagingBufferAllocationInfo));
		if (!stagingBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan staging buffer: {}", vk::to_string(stagingBufferResult.error()));
		const auto& stagingBuffer = stagingBufferResult.value();

		std::memcpy(stagingBufferAllocationInfo.pMappedData, data.data(), data.size());

		const auto token = vkCore.GetTransferPoolToken();
		auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer(token);
		if (const auto cmdTransBufBeginResult = cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdTransBufBeginResult)
			Log<LogLevel::Critical>("Failed to begin transfer command buffer: {}", vk::to_string(cmdTransBufBeginResult.error()));

		const auto copyBufferSize = vk::BufferCopy2()
				.setSrcOffset(0)
				.setDstOffset(offset)
				.setSize(data.size());
		const auto copyBufferInfo = vk::CopyBufferInfo2()
			.setSrcBuffer(stagingBuffer)
			.setDstBuffer(m_IndexBuffer)
			.setRegions(copyBufferSize);

		cmdTransBuf.copyBuffer2(copyBufferInfo);

		const auto transferBufferMemoryBarrier = vk::BufferMemoryBarrier2()
			.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
			.setDstAccessMask(vk::AccessFlagBits2::eNone)
			.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
			.setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)
			.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setBuffer(m_IndexBuffer)
			.setOffset(offset)
			.setSize(data.size());

		cmdTransBuf.pipelineBarrier2(vk::DependencyInfo()
			.setBufferMemoryBarriers(transferBufferMemoryBarrier)
		);
		if (const auto cmdTransBufEndResult = cmdTransBuf.end(); !cmdTransBufEndResult)
			Log<LogLevel::Critical>("Failed to end transfer command buffer: {}", vk::to_string(cmdTransBufEndResult.error()));

		auto semaphore = vkCore.GetSingleSemaphore();

		const auto cmdTransferSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdTransBuf);
		const auto semaphoreSubmit = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eTransfer);

		const auto transferSubmitInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdTransferSubmit)
			.setSignalSemaphoreInfos(semaphoreSubmit);

		if (const auto transferSubmitResult = vkCore.SubmitTransferCommandBuffer(transferSubmitInfo); !transferSubmitResult)
			Log<LogLevel::Critical>("Failed to submit transfer command buffer: {}", vk::to_string(transferSubmitResult.error()));
		vkCore.ReturnTransferPoolToken(token);

		auto cmdGraphicsBuffer = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto cmdGraphicsBufferBeginResult = cmdGraphicsBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdGraphicsBufferBeginResult)
			Log<LogLevel::Critical>("Failed to begin graphics command buffer: {}", vk::to_string(cmdGraphicsBufferBeginResult.error()));

		const auto GraphicsBufferMemoryBarrier = vk::BufferMemoryBarrier2()
			.setSrcAccessMask(vk::AccessFlagBits2::eNone)
			.setDstAccessMask(vk::AccessFlagBits2::eIndexRead)
			.setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
			.setDstStageMask(vk::PipelineStageFlagBits2::eIndexInput)
			.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
			.setBuffer(m_IndexBuffer)
			.setOffset(offset)
			.setSize(data.size());

		cmdGraphicsBuffer.pipelineBarrier2(vk::DependencyInfo()
			.setBufferMemoryBarriers(GraphicsBufferMemoryBarrier)
		);

		if (const auto cmdGraphicsBufferEndResult = cmdGraphicsBuffer.end(); !cmdGraphicsBufferEndResult)
			Log<LogLevel::Critical>("Failed to end graphics command buffer: {}", vk::to_string(cmdGraphicsBufferEndResult.error()));

		const auto cmdGraphicsSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdGraphicsBuffer);

		const auto graphicsSemaphore = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eIndexInput);

		if (const auto graphicsSubmitResult = vkCore.GetGraphicsQueue().submit2(vk::SubmitInfo2()
			.setCommandBufferInfos(cmdGraphicsSubmitInfo)
			.setWaitSemaphoreInfos(graphicsSemaphore)
		); !graphicsSubmitResult)
			Log<LogLevel::Critical>("Failed to submit graphics command buffer: {}", vk::to_string(graphicsSubmitResult.error()));

		VulkanCore::AddVulkanEndOfFrameCleanUpFunction([stagingBuffer = std::move(stagingBufferResult).value(), semaphore = std::move(semaphore), cmdTransBuf = std::move(cmdTransBuf), cmdGraphicsBuffer = std::move(cmdGraphicsBuffer)](){});
	}

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
				.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

			auto uniformBuffer = allocator.createBuffer(bufferInfo, allocInfo);
			if (!uniformBuffer)
				Log<LogLevel::Critical>("Failed to create Vulkan uniform buffer: {}", vk::to_string(uniformBuffer.error()));
			m_UniformBuffers.emplace_back(std::move(uniformBuffer).value());
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
		auto stagingBufferResult = allocator.createBuffer(bufferCreateInfo, allocCreateInfo, vk::Optional(stagingBufferAllocationInfo));
		if (!stagingBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan staging buffer: {}", vk::to_string(stagingBufferResult.error()));
		const auto& stagingBuffer = stagingBufferResult.value();
		std::memcpy(stagingBufferAllocationInfo.pMappedData, data.data(), dataSize);

		const auto token = vkCore.GetTransferPoolToken();
		auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer(token);
		if (const auto cmdTransBeginResult = cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdTransBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan single-time transfer command buffer: {}", vk::to_string(cmdTransBeginResult.error()));

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
		if (const auto cmdTransEndResult = cmdTransBuf.end(); !cmdTransEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan single-time transfer command buffer: {}", vk::to_string(cmdTransEndResult.error()));

		auto semaphore = vkCore.GetSingleSemaphore();

		const auto cmdTransferSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdTransBuf);
		const auto semaphoreSubmit = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eTransfer);

		const auto transferSubmitInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdTransferSubmit)
			.setSignalSemaphoreInfos(semaphoreSubmit);

		if (const auto submitTransResult = vkCore.SubmitTransferCommandBuffer(transferSubmitInfo); !submitTransResult)
			Log<LogLevel::Critical>("Failed to submit Vulkan single-time transfer command buffer: {}", vk::to_string(submitTransResult.error()));
		vkCore.ReturnTransferPoolToken(token);

		auto cmdGraphicsBuffer = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto cmdGraphicsBeginResult = cmdGraphicsBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdGraphicsBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan single-time graphics command buffer: {}", vk::to_string(cmdGraphicsBeginResult.error()));

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

		if (const auto cmdGraphicsEndResult = cmdGraphicsBuffer.end(); !cmdGraphicsEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan single-time graphics command buffer: {}", vk::to_string(cmdGraphicsEndResult.error()));

		const auto cmdGraphicsSubmitInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdGraphicsBuffer);

		const auto graphicsSemaphore = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader);

		if (const auto graphicsSubmitResult = vkCore.GetGraphicsQueue().submit2(vk::SubmitInfo2()
			.setCommandBufferInfos(cmdGraphicsSubmitInfo)
			.setWaitSemaphoreInfos(graphicsSemaphore)
		); !graphicsSubmitResult)
			Log<LogLevel::Critical>("Failed to submit Vulkan single-time graphics command buffer: {}", vk::to_string(graphicsSubmitResult.error()));

		VulkanCore::AddVulkanEndOfFrameCleanUpFunction([stagingBuffer = std::move(stagingBufferResult).value(), semaphore = std::move(semaphore), cmdTransBuf = std::move(cmdTransBuf), cmdGraphicsBuffer = std::move(cmdGraphicsBuffer)](){});
	}

	//--------------------------------------------------------
	// VulkanTextureBuffer
	//--------------------------------------------------------

	VulkanStorageImage::VulkanStorageImage(const ImageLoader<f32, 4, glm::aligned_highp>& image)
		: m_Width(static_cast<u32>(image.GetWidth())), m_Height(static_cast<u32>(image.GetHeight())), m_Format(vk::Format::eR32G32B32A32Sfloat)
	{
		InitializeTexture();
		VulkanStorageImage::UpdateBufferData(image.GetImageData()); // using specifically VulkanTextureBuffer to remove static analysis warning
	}

	VulkanStorageImage::VulkanStorageImage(const u32 width, const u32 height, const Format format, const AccessMask accessMask, const StageMask stageMask, const ImageLayout imageLayout)
		: m_Width(width), m_Height(height), m_Format(ConvertFormatToVulkanFormat(format)), m_CurrentAccessFlags(GetVulkanAccessMask(accessMask)), m_CurrentPipelineStageFlags(GetVulkanPipelineStageMask(stageMask)), m_CurrentLayout(GetVulkanImageLayout(imageLayout))
	{
		InitializeTexture();
	}

	void VulkanStorageImage::UpdateBufferData(const std::vector<Vec4>& data)
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
		auto stagingBufferResult = allocator.createBuffer(bufferInfo, allocCreateInfo, vk::Optional(stagingBufferAllocationInfo));
		if (!stagingBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan staging buffer: {}", vk::to_string(stagingBufferResult.error()));
		const auto& stagingBuffer = stagingBufferResult.value();

		if (stagingBufferAllocationInfo.pMappedData == nullptr || stagingBufferAllocationInfo.size != imageSize)
		{
			Log<LogLevel::Error>("Failed to map staging buffer memory in VulkanTextureBuffer::UpdateBufferData");
			return;
		}

		std::memcpy(stagingBufferAllocationInfo.pMappedData, data.data(), imageSize);

		// Copy staging buffer to texture image and transition image layout
		const auto token = vkCore.GetTransferPoolToken();
		const auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer(token);
		if (const auto cmdTransBufBeginResult = cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdTransBufBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan single-time transfer command buffer: {}", vk::to_string(cmdTransBufBeginResult.error()));

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

		if (const auto cmdTransBufEndResult = cmdTransBuf.end(); !cmdTransBufEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan single-time transfer command buffer: {}", vk::to_string(cmdTransBufEndResult.error()));

		//auto semaphore = vkCore.GetSingleSemaphore();

		const auto cmdTransferSubmitInfo = vk::CommandBufferSubmitInfo()
			.setCommandBuffer(cmdTransBuf);

		/*
		const auto signalSemaphoreTransferInfo = vk::SemaphoreSubmitInfo()
			//.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eAllTransfer);
		*/

		const auto fence = device.createFence(vk::FenceCreateInfo());
		if (!fence)
			Log<LogLevel::Critical>("Failed to create fence for transfer command buffer submission. Error: {}", vk::to_string(fence.error()));

		const auto submitTransferInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdTransferSubmitInfo);
			//.setSignalSemaphoreInfos(signalSemaphoreTransferInfo);
		if (const auto submitTransferResult = vkCore.SubmitTransferCommandBuffer(submitTransferInfo, *fence.value()); !submitTransferResult)
			Log<LogLevel::Critical>("Failed to submit transfer command buffer: {}", vk::to_string(submitTransferResult.error()));
		vkCore.ReturnTransferPoolToken(token);

		if (device.waitForFences(*fence.value(), VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
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

		//VulkanCore::GetInstance().AddVulkanEndOfFrameCleanUpFunction([cmdTransBuf = std::move(cmdTransBuf), /*cmdGraphicsBuf, semaphore,*/ stagingBuffer = std::move(stagingBuffer)]() -> void {});
	}

	void VulkanStorageImage::UpdateBufferData(const std::span<const std::byte>& data)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		// Create staging buffer
		const vk::DeviceSize imageSize = data.size();

		if (data.size() != imageSize)
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
		auto stagingBufferResult = allocator.createBuffer(bufferInfo, allocCreateInfo, vk::Optional(stagingBufferAllocationInfo));
		if (!stagingBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan staging buffer: {}", vk::to_string(stagingBufferResult.error()));
		const auto& stagingBuffer = stagingBufferResult.value();

		if (stagingBufferAllocationInfo.pMappedData == nullptr || stagingBufferAllocationInfo.size != imageSize)
		{
			Log<LogLevel::Error>("Failed to map staging buffer memory in VulkanTextureBuffer::UpdateBufferData");
			return;
		}

		std::memcpy(stagingBufferAllocationInfo.pMappedData, data.data(), imageSize);

		// Copy staging buffer to texture image and transition image layout
		const auto token = vkCore.GetTransferPoolToken();
		const auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer(token);
		if (const auto cmdTransBufBeginResult = cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdTransBufBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan single-time transfer command buffer: {}", vk::to_string(cmdTransBufBeginResult.error()));

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

		 if (const auto cmdTransBufEndResult = cmdTransBuf.end(); !cmdTransBufEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan single-time transfer command buffer: {}", vk::to_string(cmdTransBufEndResult.error()));

		//auto semaphore = vkCore.GetSingleSemaphore();

		const auto cmdTransferSubmitInfo = vk::CommandBufferSubmitInfo()
			.setCommandBuffer(cmdTransBuf);

		/*
		const auto signalSemaphoreTransferInfo = vk::SemaphoreSubmitInfo()
			//.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eAllTransfer);
		*/

		const auto fence = device.createFence(vk::FenceCreateInfo());
		if (!fence)
			Log<LogLevel::Critical>("Failed to create fence for transfer command buffer submission. Error: {}", vk::to_string(fence.error()));

		const auto submitTransferInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdTransferSubmitInfo);
			//.setSignalSemaphoreInfos(signalSemaphoreTransferInfo);
		if (const auto submitTransferResult = vkCore.SubmitTransferCommandBuffer(submitTransferInfo, *fence.value()); !submitTransferResult)
			Log<LogLevel::Critical>("Failed to submit transfer command buffer. Error: {}", vk::to_string(submitTransferResult.error()));
		vkCore.ReturnTransferPoolToken(token);

		if (device.waitForFences(*fence.value(), VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
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

		//VulkanCore::GetInstance().AddVulkanEndOfFrameCleanUpFunction([cmdTransBuf = std::move(cmdTransBuf), /*cmdGraphicsBuf, semaphore,*/ stagingBuffer = std::move(stagingBuffer)]() -> void {});
	}

	void VulkanStorageImage::InitializeTexture()
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		const auto createInfo = vk::ImageCreateInfo()
			.setImageType(vk::ImageType::e2D)
			.setFormat(m_Format)
			.setExtent(vk::Extent3D()
				.setWidth(m_Width)
				.setHeight(m_Height)
				.setDepth(1))
			.setMipLevels(1)
			.setArrayLayers(1)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setTiling(vk::ImageTiling::eOptimal)
			.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage)
			.setSharingMode(vk::SharingMode::eConcurrent)
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setQueueFamilyIndices(queueIndices);

		constexpr auto vmaCreateInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		// Create image
		auto imageResult = allocator.createImage(createInfo, vmaCreateInfo);
		if (!imageResult)
			Log<LogLevel::Critical>("Failed to create Vulkan storage image: {}", vk::to_string(imageResult.error()));
		m_TextureImage = std::move(imageResult).value();

		// Create image view
		auto imageViewResult = device.createImageView(vk::ImageViewCreateInfo()
			.setImage(m_TextureImage)
			.setViewType(vk::ImageViewType::e2D)
			.setFormat(m_Format)
			.setSubresourceRange(vk::ImageSubresourceRange()
				.setAspectMask(vk::ImageAspectFlagBits::eColor)
				.setBaseMipLevel(0)
				.setLevelCount(1)
				.setBaseArrayLayer(0)
				.setLayerCount(1)));
		if (!imageViewResult)
			Log<LogLevel::Critical>("Failed to create Vulkan storage image view: {}", vk::to_string(imageViewResult.error()));
		m_TextureImageView = std::move(imageViewResult).value();

		const f32 maxAnisotropy = vkCore.GetPhysicalDev().getProperties().limits.maxSamplerAnisotropy;

		// Create sampler
		auto samplerResult = device.createSampler(vk::SamplerCreateInfo()
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
		if (!samplerResult)
			Log<LogLevel::Critical>("Failed to create Vulkan storage image sampler: {}", vk::to_string(samplerResult.error()));
		m_TextureSampler = std::move(samplerResult).value();

		const auto& cmd = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto cmdBeginResult = cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan single-time graphics command buffer: {}", vk::to_string(cmdBeginResult.error()));


		const std::array graphicsImageBuffer = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
				.setDstStageMask(m_CurrentPipelineStageFlags)
				.setSrcAccessMask(vk::AccessFlagBits2::eNone)
				.setDstAccessMask(m_CurrentAccessFlags)
				.setOldLayout(vk::ImageLayout::eUndefined)
				.setNewLayout(m_CurrentLayout)
				.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
				.setImage(*m_TextureImage)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)
				)
		};
		cmd.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(graphicsImageBuffer)
		);

		if (const auto cmdEndResult = cmd.end(); !cmdEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan single-time graphics command buffer: {}", vk::to_string(cmdEndResult.error()));

		const auto fence = device.createFence(vk::FenceCreateInfo());
		if (!fence)
			Log<LogLevel::Critical>("Failed to create fence for transfer command buffer submission. Error: {}", vk::to_string(fence.error()));
		const auto cmdInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
		const auto submitGraphicsInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdInfo);
		if (const auto submitGraphicsResult = vkCore.GetGraphicsQueue().submit2(submitGraphicsInfo, *fence.value()); !submitGraphicsResult)
			Log<LogLevel::Critical>("Failed to submit transfer command buffer: {}", vk::to_string(submitGraphicsResult.error()));
		if (device.waitForFences(*fence.value(), VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
			Log<LogLevel::Critical>("Failed to wait for transfer finished fence");
	}

	//--------------------------------------------------------
	// VulkanDynamicTextureBuffer
	//--------------------------------------------------------

	VulkanDynamicTextureBuffer::VulkanDynamicTextureBuffer(const u32 width, const u32 height, const Format format, const AccessMask accessMask, const StageMask stageMask, const ImageLayout imageLayout)
		: m_Width(width), m_Height(height), m_Format(ConvertFormatToVulkanFormat(format))
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& device = vkCore.GetDevice();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		m_TextureImage.reserve(vkCore.GetNumberOfFramesInFlight());
		m_TextureImageView.reserve(vkCore.GetNumberOfFramesInFlight());

		const f32 maxAnisotropy = vkCore.GetPhysicalDev().getProperties().limits.maxSamplerAnisotropy;
		const auto& allQueueFamilyIndices = vkCore.GetAllUniqueQueuesIndices();

		const auto imageCreateInfo = vk::ImageCreateInfo()
				.setImageType(vk::ImageType::e2D)
				.setFormat(m_Format)
				.setExtent(vk::Extent3D()
					.setWidth(m_Width)
					.setHeight(m_Height)
					.setDepth(1))
				.setMipLevels(1)
				.setArrayLayers(1)
				.setSamples(vk::SampleCountFlagBits::e1)
				.setTiling(vk::ImageTiling::eOptimal)
				.setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage)
				.setSharingMode(vk::SharingMode::eConcurrent)
				.setInitialLayout(vk::ImageLayout::eUndefined)
				.setQueueFamilyIndices(allQueueFamilyIndices);

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		for (uSize i = 0; i < vkCore.GetNumberOfFramesInFlight(); i++)
		{
			// Create image
			auto imageResult = allocator.createImage(imageCreateInfo, allocInfo);
			if (!imageResult)
				Log<LogLevel::Critical>("Failed to create Vulkan dynamic texture image: {}", vk::to_string(imageResult.error()));
			m_TextureImage.emplace_back(std::move(imageResult).value());

			// Create image view
			auto imageViewResult = device.createImageView(vk::ImageViewCreateInfo()
				.setImage(*m_TextureImage[i])
				.setViewType(vk::ImageViewType::e2D)
				.setFormat(m_Format)
				.setSubresourceRange(vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)));
			if (!imageViewResult)
				Log<LogLevel::Critical>("Failed to create Vulkan dynamic texture image view: {}", vk::to_string(imageViewResult.error()));
			m_TextureImageView.emplace_back(std::move(imageViewResult).value());
		}

		// Create sampler
		auto samplerResult = device.createSampler(vk::SamplerCreateInfo()
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
		if (!samplerResult)
			Log<LogLevel::Critical>("Failed to create Vulkan dynamic texture sampler: {}", vk::to_string(samplerResult.error()));
		m_TextureSampler = std::move(samplerResult).value();

		const auto& cmd = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto cmdBeginResult = cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan single-time graphics command buffer: {}", vk::to_string(cmdBeginResult.error()));

		for (const auto& image : m_TextureImage)
		{
			const std::array graphicsImageBuffer = {
				vk::ImageMemoryBarrier2()
					.setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
					.setDstStageMask(GetVulkanPipelineStageMask(stageMask))
					.setSrcAccessMask(vk::AccessFlagBits2::eNone)
					.setDstAccessMask(GetVulkanAccessMask(accessMask))
					.setOldLayout(vk::ImageLayout::eUndefined)
					.setNewLayout(GetVulkanImageLayout(imageLayout))
					.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
					.setImage(*image)
					.setSubresourceRange(vk::ImageSubresourceRange()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setBaseMipLevel(0)
						.setLevelCount(1)
						.setBaseArrayLayer(0)
						.setLayerCount(1)
					)
			};
			cmd.pipelineBarrier2(vk::DependencyInfo()
				.setImageMemoryBarriers(graphicsImageBuffer)
			);
		}
		if (const auto cmdEndResult = cmd.end(); !cmdEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan single-time graphics command buffer: {}", vk::to_string(cmdEndResult.error()));

		const auto fence = device.createFence(vk::FenceCreateInfo());
		if (!fence)
			Log<LogLevel::Critical>("Failed to create fence for transfer command buffer submission. Error: {}", vk::to_string(fence.error()));
		const auto cmdInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
		const auto submitGraphicsInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdInfo);
		if (const auto submitGraphicsResult = vkCore.GetGraphicsQueue().submit2(submitGraphicsInfo, *fence.value()); !submitGraphicsResult)
			Log<LogLevel::Critical>("Failed to submit transfer command buffer: {}", vk::to_string(submitGraphicsResult.error()));
		if (device.waitForFences(*fence.value(), VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
			Log<LogLevel::Critical>("Failed to wait for transfer finished fence");

		SetCurrentAccessFlags(GetVulkanAccessMask(accessMask));
		SetCurrentImageLayout(GetVulkanImageLayout(imageLayout));
		SetCurrentPipelineStageFlags(GetVulkanPipelineStageMask(stageMask));
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
		auto bufferResult = allocator.createBuffer(bufferCreateInfo, allocationCreateInfo, vk::Optional(allocInfo));
		if (!bufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan staging buffer: {}", vk::to_string(bufferResult.error()));
		const auto& stagingBuffer = bufferResult.value();

		std::memcpy(allocInfo.pMappedData, data.data(), imageSize);

		// Copy staging buffer to texture image and transition image layout
		/*auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer();
		cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		const std::array imageMemoryBarrierBegin = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(GetCurrentPipelineStageFlags())
				.setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setSrcAccessMask(GetCurrentAccessFlags())
				.setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setOldLayout(GetCurrentLayout())
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
		vkCore.GetTransferQueue().submit2(submitTransferInfo);*/

		auto cmdGraphicsBuf = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto cmdGraphicsResult = cmdGraphicsBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdGraphicsResult)
			Log<LogLevel::Critical>("Failed to begin graphics command buffer: {}", vk::to_string(cmdGraphicsResult.error()));

		const std::array imageMemoryBarrierBegin = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(GetCurrentPipelineStageFlags())
				.setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setSrcAccessMask(GetCurrentAccessFlags())
				.setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setOldLayout(GetCurrentLayout())
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

		cmdGraphicsBuf.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierBegin)
		);

		cmdGraphicsBuf.copyBufferToImage(
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

		const std::array graphicsImageBuffer = {
			vk::ImageMemoryBarrier2()
				.setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
				.setDstStageMask(GetCurrentPipelineStageFlags())
				.setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
				.setDstAccessMask(GetCurrentAccessFlags())
				.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
				.setNewLayout(GetCurrentLayout())
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
		if (const auto cmdGraphicsResult = cmdGraphicsBuf.end(); !cmdGraphicsResult)
			Log<LogLevel::Critical>("Failed to end graphics command buffer: {}", vk::to_string(cmdGraphicsResult.error()));

		const auto cmdInfo = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdGraphicsBuf);
		/*9const auto semaphoreInfo = vk::SemaphoreSubmitInfo()
			.setSemaphore(semaphore)
			.setStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eBottomOfPipe);*/
		const auto submitGraphicsInfo = vk::SubmitInfo2()
			.setCommandBufferInfos(cmdInfo);
			//.setWaitSemaphoreInfos(semaphoreInfo);
		if (const auto submitGraphicsResult = vkCore.GetGraphicsQueue().submit2(submitGraphicsInfo); !submitGraphicsResult)
			Log<LogLevel::Critical>("Failed to submit graphics command buffer: {}", vk::to_string(submitGraphicsResult.error()));

		//VulkanCore::GetInstance().AddVulkanEndOfFrameCleanUpFunction([cmdTransBuf = std::move(cmdTransBuf), cmdGraphicsBuf = std::move(cmdGraphicsBuf), semaphore = std::move(semaphore), stagingBuffer = std::move(stagingBuffer)]() -> void {});
		VulkanCore::AddVulkanEndOfFrameCleanUpFunction([cmdGraphicsBuf = std::move(cmdGraphicsBuf), stagingBuffer = std::move(bufferResult).value()]() -> void {});
	}

	VulkanGeneralBuffer::VulkanGeneralBuffer(const uSize size, const uSize alignment /*= 0*/)
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

		const vma::AllocationCreateInfo allocInfo = vma::AllocationCreateInfo()
			.setMinAlignment(alignment)
			.setUsage(vma::MemoryUsage::eGpuOnly)
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		auto bufferResult = allocator.createBuffer(bufferInfo, allocInfo);
		if (!bufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan general buffer: {}", vk::to_string(bufferResult.error()));
		m_Buffer = std::move(bufferResult).value();

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
		auto bufferResult = allocator.createBuffer(bufferCreateInfo, allocationCreateInfo, vk::Optional(allocInfo));
		if (!bufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan staging buffer: {}", vk::to_string(bufferResult.error()));
		const auto& stagingBuffer = bufferResult.value();

		std::memcpy(allocInfo.pMappedData, data, bufferSize);

		const auto token = vkCore.GetTransferPoolToken();
		auto cmdTransBuf = vkCore.GetSingleTimeTransferCommandBuffer(token);
		if (const auto cmdTransBufBeginResult = cmdTransBuf.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdTransBufBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan staging buffer command buffer: {}", vk::to_string(cmdTransBufBeginResult.error()));

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
		if (const auto cmdTransBufEndResult = cmdTransBuf.end(); !cmdTransBufEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan staging buffer command buffer: {}", vk::to_string(cmdTransBufEndResult.error()));

		const auto fence = device.createFence(vk::FenceCreateInfo());
		const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdTransBuf);
		const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
		if (const auto submitResult = vkCore.SubmitTransferCommandBuffer(submitInfo, *fence.value()); !submitResult)
			Log<LogLevel::Critical>("Failed to submit Vulkan staging buffer command buffer: {}", vk::to_string(submitResult.error()));
		vkCore.ReturnTransferPoolToken(token);
		if (device.waitForFences(*fence.value(), vk::True, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for fence in VulkanGeneralBuffer::UpdateBufferDataImpl");
		}
	}

	VulkanHostVisibleUniformBuffer::VulkanHostVisibleUniformBuffer(const uSize size, const uSize alignment)
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		const vk::DeviceSize bufferSize = size;

		const auto& queueIndices = vkCore.GetAllUniqueQueuesIndices();

		constexpr auto bufferUsageInfo2 = vk::BufferUsageFlags2CreateInfo().setUsage(vk::BufferUsageFlagBits2::eUniformBuffer | vk::BufferUsageFlagBits2::eShaderDeviceAddress);

		const auto bufferInfo = vk::BufferCreateInfo()
			.setPNext(&bufferUsageInfo2)
			.setSize(bufferSize)
			.setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
			.setSharingMode(vk::SharingMode::eConcurrent)
			.setQueueFamilyIndices(queueIndices);

		const vma::AllocationCreateInfo allocationCreateInfo = vma::AllocationCreateInfo()
			.setMinAlignment(std::max(alignment, static_cast<uSize>(1)))
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo allocInfo;
		auto bufferResult = allocator.createBuffer(bufferInfo, allocationCreateInfo, vk::Optional(allocInfo));
		if (!bufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan Host Visible buffer: {}", vk::to_string(bufferResult.error()));
		m_Buffer = std::move(bufferResult).value();
		m_BufferHostPtr = std::span{ static_cast<std::byte*>(allocInfo.pMappedData), bufferSize };
		m_BufferDeviceAddress = vkCore.GetDevice().getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(*m_Buffer));
	}

	void VulkanHostVisibleUniformBuffer::UpdateBufferDataImpl(const std::span<const std::byte> data, uSize offset)
	{
		if (offset + data.size() > m_BufferHostPtr.size())
			Log<LogLevel::Critical>("Trying to update buffer with offset {} and size {} but buffer size is {}", offset, data.size(), m_BufferHostPtr.size());

		const auto& vkCore = VulkanCore::GetInstance();
		const auto& allocator = vkCore.GetVulkanMemoryAllocator();

		std::ranges::copy(data, m_BufferHostPtr.subspan(offset, data.size()).begin());

		if (const auto flushResult = allocator.flushAllocations(*m_Buffer.getAllocation(), offset, data.size()); !flushResult)
			Log<LogLevel::Critical>("Failed to flush buffer allocation: {}", vk::to_string(flushResult.error()));
	}

	VulkanTextureArraySampler::VulkanTextureArraySampler()
	{
		const auto& vkCore = VulkanCore::GetInstance();
		const auto& device = vkCore.GetDevice();
		auto samplerResult = device.createSampler(vk::SamplerCreateInfo()
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
		if (!samplerResult)
			Log<LogLevel::Critical>("Failed to create Vulkan texture array sampler: {}", vk::to_string(samplerResult.error()));
		m_Sampler = std::move(samplerResult).value();
	}

	VulkanTextureArraySampler::VulkanTextureArraySampler(const tg3_sampler& sampler)
	{
		constexpr auto convertToVKFilter = [](const decltype(sampler.min_filter) filter) -> vk::Filter {
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

		constexpr auto convertToVKWrap = [](const decltype(sampler.wrap_s) wrap) -> vk::SamplerAddressMode {
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

		const auto& vkCore = VulkanCore::GetInstance();
		const auto& device = vkCore.GetDevice();
		auto samplerResult = device.createSampler(vk::SamplerCreateInfo()
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
		if (!samplerResult)
			Log<LogLevel::Critical>("Failed to create Vulkan texture array sampler: {}", vk::to_string(samplerResult.error()));
		m_Sampler = std::move(samplerResult).value();
	}

	VulkanEmptyTextureArray::VulkanEmptyTextureArray()
	{
		const auto& vkCore = VulkanCore::GetInstance();
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

		constexpr auto scratchBufferInfo = vk::BufferCreateInfo()
			.setSize(1)
			.setUsage(vk::BufferUsageFlagBits::eTransferSrc)
			.setSharingMode(vk::SharingMode::eExclusive)
			.setSize(1 * 1 * sizeof(u8) * 4);

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		auto imageResult = allocator.createImage(imageCreateInfo, allocInfo);
		if (!imageResult)
			Log<LogLevel::Critical>("Failed to create Vulkan empty texture image: {}", vk::to_string(imageResult.error()));
		m_TextureImages = std::move(imageResult).value();

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		auto scratchBufferResult = allocator.createBuffer(scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));
		if (!scratchBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan empty texture scratch buffer: {}", vk::to_string(scratchBufferResult.error()));
		const auto& scratchBuffer = scratchBufferResult.value();

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

		auto imageViewResult = device.createImageView(imageViewCreateInfo);
		if (!imageViewResult)
			Log<LogLevel::Critical>("Failed to create Vulkan empty texture image view: {}", vk::to_string(imageViewResult.error()));
		m_TextureImageView = std::move(imageViewResult).value();

		Vec4 black{0.0f};
		std::memcpy(scratchBufferAllocInfo.pMappedData, &black, sizeof(Vec4));

		const auto token = vkCore.GetTransferPoolToken();
		auto cmdTrans = vkCore.GetSingleTimeTransferCommandBuffer(token);
		if (const auto cmdTransBeginResult = cmdTrans.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdTransBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan single-time transfer command buffer: {}", vk::to_string(cmdTransBeginResult.error()));

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

		cmdTrans.pipelineBarrier2(vk::DependencyInfo()
			.setImageMemoryBarriers(imageMemoryBarrierBegin)
		);

		cmdTrans.copyBufferToImage(
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

		if (const auto cmdTransEndResult = cmdTrans.end(); !cmdTransEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan single-time transfer command buffer: {}", vk::to_string(cmdTransEndResult.error()));

		const auto fence = device.createFence(vk::FenceCreateInfo());
		if (!fence)
			Log<LogLevel::Critical>("Failed to create fence for transfer command buffer submission. Error: {}", vk::to_string(fence.error()));
		const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdTrans);
		const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
		if (const auto submitTransResult = vkCore.SubmitTransferCommandBuffer(submitInfo, *fence.value()); !submitTransResult)
			Log<LogLevel::Critical>("Failed to submit Vulkan single-time transfer command buffer: {}", vk::to_string(submitTransResult.error()));
		vkCore.ReturnTransferPoolToken(token);

		if (device.waitForFences(*fence.value(), vk::True, UINT64_MAX) != vk::Result::eSuccess)
		{
			Log<LogLevel::Error>("Failed to wait for fence in VulkanColourTextureArray constructor");
		}

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto cmdFinalBeginResult = cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdFinalBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan single-time graphics command buffer: {}", vk::to_string(cmdFinalBeginResult.error()));

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

		if (const auto cmdFinalEndResult = cmdFinal.end(); !cmdFinalEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan single-time graphics command buffer: {}", vk::to_string(cmdFinalEndResult.error()));

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		if (!finalFence)
			Log<LogLevel::Critical>("Failed to create fence for final command buffer submission. Error: {}", vk::to_string(finalFence.error()));
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		if (const auto finalSubmitResult = vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, *finalFence.value()); !finalSubmitResult)
			Log<LogLevel::Critical>("Failed to submit final command buffer. Error: {}", vk::to_string(finalSubmitResult.error()));

		if (device.waitForFences(*finalFence.value(), vk::True, UINT64_MAX) != vk::Result::eSuccess)
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanColourTextureArray constructor");
	}

	VulkanColourTextureArray::VulkanColourTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		const auto& vkCore = VulkanCore::GetInstance();
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
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		auto imageResult = allocator.createImage(imageCreateInfo, allocInfo);
		if (!imageResult)
			Log<LogLevel::Critical>("Failed to create Vulkan colour texture array image: {}", vk::to_string(imageResult.error()));
		m_TextureImages = std::move(imageResult).value();

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		auto scratchBufferResult = allocator.createBuffer(scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));
		if (!scratchBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan colour texture array scratch buffer: {}", vk::to_string(scratchBufferResult.error()));
		const vma::raii::Buffer& scratchBuffer = scratchBufferResult.value();

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

		auto imageViewResult = device.createImageView(imageViewCreateInfo);
		if (!imageViewResult)
			Log<LogLevel::Critical>("Failed to create Vulkan colour texture array image view: {}", vk::to_string(imageViewResult.error()));
		m_TextureImageView = std::move(imageViewResult).value();

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

		const auto token = vkCore.GetTransferPoolToken();

		for (u32 i = 0; i < indexing.size(); i++)
		{
			if ((i + 1) % 5 == 0 || i + 1 == indexing.size())
				Log<LogLevel::Trace>("Loading texture {}/{}", i + 1, indexing.size());

			ImageLoader<u8, 4, glm::packed_highp> imageLoader = std::move(imageLoaderPreLoad);

			assert(imageCreateInfo.extent.width == imageLoader.GetWidth() && imageCreateInfo.extent.height == imageLoader.GetHeight() && "All images in the texture array must have the same dimensions");

			std::memcpy(scratchBufferAllocInfo.pMappedData, imageLoader.GetImageData().data(), scratchBufferInfo.size);

			auto cmd = vkCore.GetSingleTimeTransferCommandBuffer(token);
			if (const auto cmdResult = cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdResult)
				Log<LogLevel::Critical>("Failed to begin Vulkan command buffer: {}", vk::to_string(cmdResult.error()));

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

			if (const auto cmdEndResult = cmd.end(); !cmdEndResult)
				Log<LogLevel::Critical>("Failed to end Vulkan command buffer: {}", vk::to_string(cmdEndResult.error()));

			const auto fence = device.createFence(vk::FenceCreateInfo());
			if (!fence)
				Log<LogLevel::Critical>("Failed to create fence for command buffer submission. Error: {}", vk::to_string(fence.error()));
			const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
			const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
			if (const auto submitResult = vkCore.SubmitTransferCommandBuffer(submitInfo, *fence.value()); !submitResult)
				Log<LogLevel::Critical>("Failed to submit Vulkan command buffer: {}", vk::to_string(submitResult.error()));

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

			if (const auto waitForFenceResult = device.waitForFences(*fence.value(), vk::True, UINT64_MAX); waitForFenceResult != vk::Result::eSuccess)
				Log<LogLevel::Error>("Failed to wait for fence in VulkanColourTextureArray constructor. Error: {}", vk::to_string(waitForFenceResult));
		}

		vkCore.ReturnTransferPoolToken(token);

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto cmdFinalBeginResult = cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdFinalBeginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan command buffer: {}", vk::to_string(cmdFinalBeginResult.error()));

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

		if (const auto cmdFinalEndResult = cmdFinal.end(); !cmdFinalEndResult)
			Log<LogLevel::Critical>("Failed to end Vulkan command buffer: {}", vk::to_string(cmdFinalEndResult.error()));

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		if (!finalFence)
			Log<LogLevel::Critical>("Failed to create fence for final command buffer submission. Error: {}", vk::to_string(finalFence.error()));
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		if (const auto submitResult = vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, *finalFence.value()); !submitResult)
			Log<LogLevel::Critical>("Failed to submit final command buffer. Error: {}", vk::to_string(submitResult.error()));

		if (const auto waitForFenceResult = device.waitForFences(*finalFence.value(), vk::True, UINT64_MAX); waitForFenceResult != vk::Result::eSuccess)
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanColourTextureArray constructor. Error: {}", vk::to_string(waitForFenceResult));
	}

	/*VulkanNormalTextureArray::VulkanNormalTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		const auto& vkCore = VulkanCore::GetInstance();
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
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

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

		const auto token = vkCore.GetTransferPoolToken();

		for (u32 i = 0; i < indexing.size(); i++)
		{
			if ((i + 1) % 5 == 0 || i + 1 == indexing.size())
				Log<LogLevel::Trace>("Loading texture {}/{}", i + 1, indexing.size());

			ImageLoader<u8, 2, glm::packed_highp> imageLoader = std::move(imageLoaderPreLoad);

			assert(imageCreateInfo.extent.width == imageLoader.GetWidth() && imageCreateInfo.extent.height == imageLoader.GetHeight() && "All images in the texture array must have the same dimensions");

			std::memcpy(scratchBufferAllocInfo.pMappedData, imageLoader.GetImageData().data(), scratchBufferInfo.size);

			auto cmd = vkCore.GetSingleTimeTransferCommandBuffer(token);
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
			vkCore.SubmitTransferCommandBuffer(submitInfo, fence);

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
				Log<LogLevel::Error>("Failed to wait for fence in VulkanNormalTextureArray constructor");
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
		const auto& vkCore = VulkanCore::GetInstance();
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
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		auto imageResult = allocator.createImage(imageCreateInfo, allocInfo);
		if (!imageResult)
			Log<LogLevel::Critical>("Failed to create Vulkan normal texture array image: {}", vk::to_string(imageResult.error()));
		m_TextureImages = std::move(imageResult).value();

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		auto scratchBufferResult = allocator.createBuffer(scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));
		if (!scratchBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan normal texture array scratch buffer: {}", vk::to_string(scratchBufferResult.error()));
		const auto& scratchBuffer = scratchBufferResult.value();

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

		auto imageViewResult = device.createImageView(imageViewCreateInfo);
		if (!imageViewResult)
			Log<LogLevel::Critical>("Failed to create Vulkan normal texture array image view: {}", vk::to_string(imageViewResult.error()));
		m_TextureImageView = std::move(imageViewResult).value();

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

		const auto token = vkCore.GetTransferPoolToken();

		for (u32 i = 0; i < indexing.size(); i++)
		{
			if ((i + 1) % 5 == 0 || i + 1 == indexing.size())
				Log<LogLevel::Trace>("Loading texture {}/{}", i + 1, indexing.size());

			ImageLoader<u8, 4, glm::packed_highp> imageLoader = std::move(imageLoaderPreLoad);

			assert(imageCreateInfo.extent.width == imageLoader.GetWidth() && imageCreateInfo.extent.height == imageLoader.GetHeight() && "All images in the texture array must have the same dimensions");

			std::memcpy(scratchBufferAllocInfo.pMappedData, imageLoader.GetImageData().data(), scratchBufferInfo.size);

			auto cmd = vkCore.GetSingleTimeTransferCommandBuffer(token);
			if (const auto cmdResult = cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !cmdResult)
				Log<LogLevel::Critical>("Failed to begin Vulkan normal texture array command buffer: {}", vk::to_string(cmdResult.error()));

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

			if (const auto endResult = cmd.end(); !endResult)
				Log<LogLevel::Critical>("Failed to end Vulkan normal texture array command buffer: {}", vk::to_string(endResult.error()));

			const auto fence = device.createFence(vk::FenceCreateInfo());
			if (!fence)
				Log<LogLevel::Critical>("Failed to create fence for Vulkan normal texture array command buffer submission. Error: {}", vk::to_string(fence.error()));
			const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
			const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
			if (const auto submitResult = vkCore.SubmitTransferCommandBuffer(submitInfo, *fence.value()); !submitResult)
				Log<LogLevel::Critical>("Failed to submit Vulkan normal texture array command buffer: {}", vk::to_string(submitResult.error()));

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

			if (const auto waitForFencesResult = device.waitForFences(*fence.value(), vk::True, UINT64_MAX); waitForFencesResult != vk::Result::eSuccess)
				Log<LogLevel::Error>("Failed to wait for fence in VulkanNormalTextureArray constructor: {}", vk::to_string(waitForFencesResult));
		}

		vkCore.ReturnTransferPoolToken(token);

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto beginResult = cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !beginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan final command buffer: {}", vk::to_string(beginResult.error()));

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

		if (const auto endResult = cmdFinal.end(); !endResult)
			Log<LogLevel::Critical>("Failed to end Vulkan final command buffer: {}", vk::to_string(endResult.error()));

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		if (!finalFence)
			Log<LogLevel::Critical>("Failed to create fence for final command buffer submission. Error: {}", vk::to_string(finalFence.error()));
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		if (const auto submitResult = vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, *finalFence.value()); !submitResult)
			Log<LogLevel::Critical>("Failed to submit final Vulkan command buffer: {}", vk::to_string(submitResult.error()));

		if (const auto waitForFencesResult = device.waitForFences(*finalFence.value(), vk::True, UINT64_MAX); waitForFencesResult != vk::Result::eSuccess)
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanNormalTextureArray constructor: {}", vk::to_string(waitForFencesResult));
	}

	VulkanMetallicRoughnessTextureArray::VulkanMetallicRoughnessTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		const auto& vkCore = VulkanCore::GetInstance();
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
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		auto imageResult = allocator.createImage(imageCreateInfo, allocInfo);
		if (!imageResult)
			Log<LogLevel::Critical>("Failed to create Vulkan metallic-roughness texture array image: {}", vk::to_string(imageResult.error()));
		m_TextureImages = std::move(imageResult).value();

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		auto scratchBufferResult = allocator.createBuffer(scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));
		if (!scratchBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan metallic-roughness texture array scratch buffer: {}", vk::to_string(scratchBufferResult.error()));
		const auto& scratchBuffer = scratchBufferResult.value();

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

		auto imageViewResult = device.createImageView(imageViewCreateInfo);
		if (!imageViewResult)
			Log<LogLevel::Critical>("Failed to create Vulkan metallic-roughness texture array image view: {}", vk::to_string(imageViewResult.error()));
		m_TextureImageView = std::move(imageViewResult).value();

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

		const auto token = vkCore.GetTransferPoolToken();

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
				destPtr[pixelIndex * 2] = sourceData[pixelIndex][2];      // B -> R
				destPtr[pixelIndex * 2 + 1] = sourceData[pixelIndex][1];  // G -> G
			}

			auto cmd = vkCore.GetSingleTimeTransferCommandBuffer(token);
			if (const auto beginResult = cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !beginResult)
				Log<LogLevel::Critical>("Failed to begin Vulkan command buffer: {}", vk::to_string(beginResult.error()));

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

			if (const auto endResult = cmd.end(); !endResult)
				Log<LogLevel::Critical>("Failed to end Vulkan command buffer: {}", vk::to_string(endResult.error()));

			const auto fence = device.createFence(vk::FenceCreateInfo());
			if (!fence)
				Log<LogLevel::Critical>("Failed to create fence for Vulkan command buffer submission. Error: {}", vk::to_string(fence.error()));
			const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
			const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
			if (const auto submitResult = vkCore.SubmitTransferCommandBuffer(submitInfo, *fence.value()); !submitResult)
				Log<LogLevel::Critical>("Failed to submit Vulkan command buffer: {}", vk::to_string(submitResult.error()));

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

			if (const auto waitResult = device.waitForFences(*fence.value(), vk::True, UINT64_MAX); waitResult != vk::Result::eSuccess)
			{
				Log<LogLevel::Error>("Failed to wait for fence in VulkanMetallicRoughnessTextureArray constructor. Error: {}", vk::to_string(waitResult));
			}
		}

		vkCore.ReturnTransferPoolToken(token);

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto beginResult = cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !beginResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan command buffer: {}", vk::to_string(beginResult.error()));

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

		if (const auto endResult = cmdFinal.end(); !endResult)
			Log<LogLevel::Critical>("Failed to end Vulkan command buffer: {}", vk::to_string(endResult.error()));

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		if (!finalFence)
			Log<LogLevel::Critical>("Failed to create fence for Vulkan command buffer submission. Error: {}", vk::to_string(finalFence.error()));
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		if (const auto submitResult = vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, *finalFence.value()); !submitResult)
			Log<LogLevel::Critical>("Failed to submit Vulkan command buffer: {}", vk::to_string(submitResult.error()));

		if (const auto waitResult = device.waitForFences(*finalFence.value(), vk::True, UINT64_MAX); waitResult != vk::Result::eSuccess)
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanMetallicRoughnessTextureArray constructor. Error: {}", vk::to_string(waitResult));
	}

	VulkanEmissiveTextureArray::VulkanEmissiveTextureArray(const tg3_model& model, const std::vector<u32>& indexing, std::string_view pathToGltf)
	{
		const auto& vkCore = VulkanCore::GetInstance();
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

			Log<LogLevel::Trace>("Loading {} emissive textures  at {}x{}. This might take some time please have some patience.", indexing.size(), imageLoader.GetWidth(), imageLoader.GetHeight());
		}

		constexpr auto allocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferDevice)
			.setFlags(vma::AllocationCreateFlagBits::eCanAlias);

		auto imageResult = allocator.createImage(imageCreateInfo, allocInfo);
		if (!imageResult)
			Log<LogLevel::Critical>("Failed to create Vulkan emissive texture array image: {}", vk::to_string(imageResult.error()));
		m_TextureImages = std::move(imageResult).value();

		constexpr auto scratchBufferCreateAllocInfo = vma::AllocationCreateInfo()
			.setUsage(vma::MemoryUsage::eAutoPreferHost)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped);

		vma::AllocationInfo scratchBufferAllocInfo;
		auto scratchBufferResult = allocator.createBuffer(scratchBufferInfo, scratchBufferCreateAllocInfo, vk::Optional(scratchBufferAllocInfo));
		if (!scratchBufferResult)
			Log<LogLevel::Critical>("Failed to create Vulkan emissive texture array scratch buffer: {}", vk::to_string(scratchBufferResult.error()));
		const auto& scratchBuffer = scratchBufferResult.value();

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

		auto imageViewResult = device.createImageView(imageViewCreateInfo);
		if (!imageViewResult)
			Log<LogLevel::Critical>("Failed to create Vulkan emissive texture array image view: {}", vk::to_string(imageViewResult.error()));
		m_TextureImageView = std::move(imageViewResult).value();

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

		const auto token = vkCore.GetTransferPoolToken();

		for (u32 i = 0; i < indexing.size(); i++)
		{
			if ((i + 1) % 5 == 0 || i + 1 == indexing.size())
				Log<LogLevel::Trace>("Loading texture {}/{}", i + 1, indexing.size());

			ImageLoader<u8, 4, glm::packed_highp> imageLoader = std::move(imageLoaderPreLoad);

			assert(imageCreateInfo.extent.width == imageLoader.GetWidth() && imageCreateInfo.extent.height == imageLoader.GetHeight() && "All images in the texture array must have the same dimensions");

			std::memcpy(scratchBufferAllocInfo.pMappedData, imageLoader.GetImageData().data(), scratchBufferInfo.size);

			auto cmd = vkCore.GetSingleTimeTransferCommandBuffer(token);
			if (const auto submitResult = cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !submitResult)
				Log<LogLevel::Critical>("Failed to begin Vulkan command buffer: {}", vk::to_string(submitResult.error()));

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

			if (const auto endResult = cmd.end(); !endResult)
				Log<LogLevel::Critical>("Failed to end Vulkan command buffer: {}", vk::to_string(endResult.error()));

			auto fence = device.createFence(vk::FenceCreateInfo());
			if (!fence)
				Log<LogLevel::Critical>("Failed to create Vulkan fence: {}", vk::to_string(fence.error()));
			const auto cmdSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmd);
			const auto submitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdSubmit);
			if (const auto submitCmdResult =  vkCore.SubmitTransferCommandBuffer(submitInfo, *fence.value()); !submitCmdResult)
				Log<LogLevel::Critical>("Failed to submit Vulkan command buffer: {}", vk::to_string(submitCmdResult.error()));

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

			if (const auto waitFenceResult = device.waitForFences(*fence.value(), vk::True, UINT64_MAX); waitFenceResult != vk::Result::eSuccess)
				Log<LogLevel::Error>("Failed to wait for fence in VulkanEmissiveTextureArray constructor. Error: {}", vk::to_string(waitFenceResult));
		}

		vkCore.ReturnTransferPoolToken(token);

		// Final barrier to transition to shader read only for ray tracing shader
		auto cmdFinal = vkCore.GetSingleTimeGraphicsCommandBuffer();
		if (const auto beginCmdResult = cmdFinal.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)); !beginCmdResult)
			Log<LogLevel::Critical>("Failed to begin Vulkan command buffer: {}", vk::to_string(beginCmdResult.error()));

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

		if (const auto endCmdResult = cmdFinal.end(); !endCmdResult)
			Log<LogLevel::Critical>("Failed to end Vulkan command buffer: {}", vk::to_string(endCmdResult.error()));

		const auto finalFence = device.createFence(vk::FenceCreateInfo());
		if (!finalFence)
			Log<LogLevel::Critical>("Failed to create fence for Vulkan command buffer submission. Error: {}", vk::to_string(finalFence.error()));
		const auto cmdFinalSubmit = vk::CommandBufferSubmitInfo().setCommandBuffer(cmdFinal);
		const auto finalSubmitInfo = vk::SubmitInfo2().setCommandBufferInfos(cmdFinalSubmit);
		if (const auto submitResult = vkCore.GetGraphicsQueue().submit2(finalSubmitInfo, finalFence.value()); !submitResult)
			Log<LogLevel::Critical>("Failed to submit Vulkan command buffer: {}", vk::to_string(submitResult.error()));

		if (const auto waitFenceResult = device.waitForFences(*finalFence.value(), vk::True, UINT64_MAX); waitFenceResult != vk::Result::eSuccess)
			Log<LogLevel::Error>("Failed to wait for final fence in VulkanEmissiveTextureArray constructor. Error: {}", vk::to_string(waitFenceResult));
	}
}
