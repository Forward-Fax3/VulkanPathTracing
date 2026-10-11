#include "VulkanCore.hpp"

#include <mutex>


namespace OWC::Graphics
{
	std::unique_ptr<VulkanCore> VulkanCore::s_Instance = nullptr;

	void VulkanCore::TransferPoolRing::ReturnPool(const TransferPoolToken& token)
	{
		if (token.GetPoolIndex() == std::numeric_limits<u32>::max())
			return;

		std::unique_lock lock(m_Mutex);
		if (token.GetPoolIndex() >= m_PoolCount)
			m_FreeIndexReusable.emplace_back(token.GetPoolIndex());
		else
			m_FreeIndex.emplace_back(token.GetPoolIndex());

		lock.unlock();
		m_Cv.notify_one();

		if (m_FreeIndex.size() > m_PoolCount || m_FreeIndexReusable.size() > m_PoolCount)
			Log<LogLevel::Error>("VulkanCore::TransferPoolRing::ReturnPool: More pools returned than allocated. This indicates a logic error in the pool management.");

		token.m_CommandPoolIndex = std::numeric_limits<u32>::max();
	}

	TransferPoolToken VulkanCore::TransferPoolRing::GetPool()
	{
		std::unique_lock lock(m_Mutex);
		m_Cv.wait(lock, [this]() { return !m_FreeIndex.empty(); });
		const u32 pool = m_FreeIndex.back();
		m_FreeIndex.pop_back();
		return TransferPoolToken{ pool };
	}

	TransferPoolToken VulkanCore::TransferPoolRing::GetReusablePool()
	{
		std::unique_lock lock(m_Mutex);
		m_Cv.wait(lock, [this]() { return !m_FreeIndexReusable.empty(); });
		const u32 index = m_FreeIndexReusable.back();
		m_FreeIndexReusable.pop_back();
		return TransferPoolToken{ index };
	}

	void VulkanCore::Init()
	{
		{
			if (!s_Instance)
				s_Instance = std::make_unique<VulkanCore>(PRIVATE());
			else
			{
				Log<LogLevel::Warn>("VulkanCore instance already exists!");
				return;
			}

			if (const auto runtimeVersion = vk::enumerateInstanceVersion(); runtimeVersion < g_VulkanVersion)
				Log<LogLevel::Critical>("Vulkan runtime version {}.{}.{} is lower than the required version {}.{}.{}",
					VK_VERSION_MAJOR(runtimeVersion), VK_VERSION_MINOR(runtimeVersion), VK_VERSION_PATCH(runtimeVersion),
					VK_VERSION_MAJOR(g_VulkanVersion), VK_VERSION_MINOR(g_VulkanVersion), VK_VERSION_PATCH(g_VulkanVersion));
			else
				Log<LogLevel::Trace>("Vulkan runtime version {}.{}.{} detected",
					VK_VERSION_MAJOR(runtimeVersion), VK_VERSION_MINOR(runtimeVersion), VK_VERSION_PATCH(runtimeVersion));
		}
	}

	std::vector<vk::raii::CommandBuffer> VulkanCore::GetGraphicsCommandBuffer() const
	{
		auto commandBuffers = m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
			.setCommandPool(*m_GraphicsCommandPool)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount(static_cast<u32>(m_SwapchainImageViews.size()))
		);

		for (const auto& cmdBuf : commandBuffers)
			Log<LogLevel::Debug>("Created graphics command buffer: ID: 0x{:x}", std::bit_cast<const uint64_t>(std::bit_cast<const VkCommandBuffer>(*cmdBuf)));

		return commandBuffers;
	}

	std::vector<vk::raii::CommandBuffer> VulkanCore::GetComputeCommandBuffer() const
	{
		auto commandBuffers = m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
				.setCommandPool(*m_ComputeCommandPool)
				.setLevel(vk::CommandBufferLevel::ePrimary)
				.setCommandBufferCount(static_cast<u32>(m_SwapchainImageViews.size()))
		);
		return commandBuffers;
	}

	std::vector<vk::raii::CommandBuffer> VulkanCore::GetTransferCommandBuffer(const TransferPoolToken& token) const
	{
		if (token.GetPoolIndex() == std::numeric_limits<u32>::max())
		{
			Log<LogLevel::Error>("VulkanCore::GetSingleTimeTransferCommandBuffer: Invalid TransferPoolToken provided. The token has already been returned to the pool.");
			return {};
		}

		auto commandBuffers = m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
				.setCommandPool(*m_TransferPoolRing->GetPool(token))
				.setLevel(vk::CommandBufferLevel::ePrimary)
				.setCommandBufferCount(static_cast<u32>(m_SwapchainImageViews.size()))
		);
		return commandBuffers;
	}

	std::vector<vk::raii::CommandBuffer> VulkanCore::GetDynamicGraphicsCommandBuffer() const
	{
		auto commandBuffers = m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
			.setCommandPool(*m_DynamicGraphicsCommandPool)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount(static_cast<u32>(m_SwapchainImageViews.size()))
		);

		for (const auto& cmdbuf : commandBuffers)
			Log<LogLevel::Debug>("Allocated dynamic graphics command buffer: ID: 0x{:x}", std::bit_cast<const uint64_t>(std::bit_cast<const VkCommandBuffer>(*cmdbuf)));

		return commandBuffers;
	}

	std::vector<vk::raii::CommandBuffer> VulkanCore::GetDynamicComputeCommandBuffer() const
	{
		auto commandBuffers = m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
			.setCommandPool(*m_DynamicComputeCommandPool)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount(static_cast<u32>(m_SwapchainImageViews.size()))
		);
		return commandBuffers;
	}

	vk::raii::CommandBuffer VulkanCore::GetSingleTimeGraphicsCommandBuffer() const
	{
		std::unique_lock lock(m_SingleUseGraphicsCommandPoolMutex);

		return std::move(m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
			.setCommandPool(*m_GraphicsCommandPool)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount(1)
		)[0]);
	}

	vk::raii::CommandBuffer VulkanCore::GetSingleTimeComputeCommandBuffer() const
	{
		std::unique_lock lock(m_SingleUseComputeCommandPoolMutex);

		return std::move(m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
			.setCommandPool(*m_ComputeCommandPool)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount(1)
		)[0]);
	}

	vk::raii::CommandBuffer VulkanCore::GetSingleTimeTransferCommandBuffer(const TransferPoolToken& token) const
	{
		if (token.GetPoolIndex() == std::numeric_limits<u32>::max())
		{
			Log<LogLevel::Error>("VulkanCore::GetSingleTimeTransferCommandBuffer: Invalid TransferPoolToken provided. The token has already been returned to the pool.");
			return nullptr;
		}

		return std::move(m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
			.setCommandPool(m_TransferPoolRing->GetPool(token))
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount(1)
		)[0]);
	}

	vk::raii::CommandBuffer VulkanCore::GetSingleTimeReusableGraphicsCommandBuffer() const
	{
		return std::move(m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
			.setCommandPool(*m_DynamicGraphicsCommandPool)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount(1)
		)[0]);
	}

	vk::raii::CommandBuffer VulkanCore::GetSingleTimeReusableComputeCommandBuffer() const
	{
		return std::move(m_Device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo()
			.setCommandPool(*m_DynamicComputeCommandPool)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount(1)
		)[0]);
	}

	std::vector<vk::Semaphore> VulkanCore::GetSemaphoresFromNames(const std::span<std::string_view> semaphoreNames)
	{
		if (semaphoreNames.empty())
			return {};

		std::vector<vk::Semaphore> semaphores;
		semaphores.reserve(semaphoreNames.size());

		for (const auto& name : semaphoreNames)
		{
			auto it = m_Semaphores[m_CurrentFrameIndex].find(std::string(name));

			if (it != m_Semaphores[m_CurrentFrameIndex].end())
				semaphores.emplace_back(*it->second);

			else
			{
				for (uSize i = 0; i < m_SwapchainImageViews.size(); ++i)
				{
					constexpr auto semaphoreCreateInfo = vk::SemaphoreCreateInfo().setFlags(vk::SemaphoreCreateFlags());
					auto semaphore = m_Device.createSemaphore(semaphoreCreateInfo);
					Log<LogLevel::Debug>("Created semaphore: name: {}, ID: 0x{:x}", name, std::bit_cast<uint64_t>(static_cast<VkSemaphore>(*semaphore)));
					m_Semaphores[i].insert_or_assign(std::string(name), std::move(semaphore));
				}
				semaphores.emplace_back(*m_Semaphores[m_CurrentFrameIndex].at(std::string(name)));
			}
		}

		return semaphores;
	}

	std::vector<vk::Semaphore> VulkanCore::GetPreviousFrameSemaphoresFromNames(const std::span<std::string_view> semaphoreNames)
	{
		if (semaphoreNames.empty())
			return {};

		std::vector<vk::Semaphore> semaphores;
		semaphores.reserve(semaphoreNames.size());

		for (const auto& name : semaphoreNames)
		{
			auto it = m_Semaphores[m_PreviousFrameIndex].find(std::string(name));

			if (it != m_Semaphores[m_PreviousFrameIndex].end())
				semaphores.emplace_back(*it->second);

			else
			{
				for (uSize i = 0; i < m_SwapchainImageViews.size(); ++i)
				{
					constexpr auto semaphoreCreateInfo = vk::SemaphoreCreateInfo().setFlags(vk::SemaphoreCreateFlags());
					auto semaphore = m_Device.createSemaphore(semaphoreCreateInfo);
					Log<LogLevel::Debug>("Created semaphore: name: {}, ID: 0x{:x}", name, std::bit_cast<uint64_t>(static_cast<VkSemaphore>(*semaphore)));
					m_Semaphores[i].insert_or_assign(std::string(name), std::move(semaphore));
				}
				semaphores.emplace_back(*m_Semaphores[m_PreviousFrameIndex].at(std::string(name)));
			}
		}

		return semaphores;
	}

	vk::raii::Semaphore VulkanCore::GetSingleSemaphore() const
	{
		constexpr auto semaphoreCreateInfo = vk::SemaphoreCreateInfo()
			.setFlags(vk::SemaphoreCreateFlags());
		return vk::raii::Semaphore{ m_Device, semaphoreCreateInfo };
	}

	u32 VulkanCore::FindMemoryType(vk::DeviceSize size, vk::MemoryPropertyFlags properties) const
	{
		(void)size; // Unused parameter
		vk::PhysicalDeviceMemoryProperties memoryProperties = m_PhysicalDevice.getMemoryProperties();
		for (u32 i = 0; i < memoryProperties.memoryTypeCount; ++i)
			if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
				return i;

		Log<LogLevel::Critical>("Failed to find suitable memory type!");
		std::unreachable();
	}

	TransferPoolToken VulkanCore::GetTransferPoolToken() const
	{
		return m_TransferPoolRing->GetPool();
	}

	TransferPoolToken VulkanCore::GetReusableTransferPoolToken() const
	{
		return m_TransferPoolRing->GetReusablePool();
	}
}
