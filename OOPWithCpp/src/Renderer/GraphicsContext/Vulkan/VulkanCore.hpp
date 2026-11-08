#pragma once
#include "Core.hpp"
#include "Log.hpp"
#include "Renderer.hpp"
#include "VulkanRenderPass.hpp"

#include <vulkan/vulkan_raii.hpp>
#define VMA_VULKAN_VERSION 1004000
#include "vk_mem_alloc_raii.hpp"

#include <list>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <map>
#include <functional>
#include <ranges>


namespace OWC
{
	template <typename T, typename U>
	[[nodiscard]] constexpr bool testBitMask(T value, U bitMask) requires (std::is_same_v<T, vk::Flags<U>> && !std::is_same_v<T, U>)
	{
		return (value & bitMask) == bitMask;
	}
}

namespace OWC::Graphics
{
	constexpr auto g_VulkanVersion = VK_MAKE_VERSION(1, 4, 0);

	[[nodiscard]] OWC_FORCE_INLINE bool IsExtensionAvailable(const std::vector<vk::ExtensionProperties>& extensions, std::string_view requestExtension)
	{
		const auto it = std::ranges::find_if(extensions, [&requestExtension](const vk::ExtensionProperties& ext) {
			return requestExtension == ext.extensionName;
			});

		return it != extensions.end();
	}

	[[nodiscard]] OWC_FORCE_INLINE std::pair<bool, std::string_view> IsExtensionAvailable(const std::vector<vk::ExtensionProperties>& extensions, const std::span<const char* const> requestLayer)
	{
		bool allAvailable = true;
		std::string_view missingExt;
		for (const std::string_view requestExt : requestLayer)
		{
			auto it = std::ranges::find_if(extensions, [&requestExt](const vk::ExtensionProperties& ext) {
				return ext.extensionName == requestExt;
				}
			);

			if (it == extensions.end())
			{
				allAvailable = false;
				missingExt = requestExt;
				break;
			}
		}

		return { allAvailable, missingExt };
	}

	class TransferPoolToken;

	class VulkanCore
	{
	private:
		class PRIVATE {};

		class TransferPoolRing
		{
		public:
			[[nodiscard]] explicit TransferPoolRing(const size_t poolCount = 6)
				: m_PoolCount(poolCount)
			{
				const auto& vkCore = GetInstance();
				const auto& device = vkCore.GetDevice();

				m_Pools.reserve(m_PoolCount * 2);
				m_FreeIndex.reserve(m_PoolCount);
				m_FreeIndexReusable.reserve(m_PoolCount);
				for (size_t i = 0; i < m_PoolCount; i++)
				{
					m_Pools.emplace_back(device, vk::CommandPoolCreateInfo().setQueueFamilyIndex(vkCore.m_TransferQueueFamilyIndex));
					Log<LogLevel::Trace>("Created transfer command pool {} with handle {}", i, static_cast<void*>(*m_Pools.back()));
					m_FreeIndex.emplace_back(static_cast<u32>(i));
				}

				for (size_t i = 0; i < m_PoolCount; i++)
				{
					m_Pools.emplace_back(device, vk::CommandPoolCreateInfo()
						.setQueueFamilyIndex(vkCore.m_TransferQueueFamilyIndex)
						.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer));
					Log<LogLevel::Trace>("Created reusable transfer command pool {} with handle {}", i + m_PoolCount, static_cast<void*>(*m_Pools.back()));

					m_FreeIndexReusable.emplace_back(static_cast<u32>(i) + m_PoolCount);
				}
			}

			~TransferPoolRing() = default;

			[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetPool(const TransferPoolToken& token) const noexcept;

			void ReturnPool(const TransferPoolToken& token);

			[[nodiscard]] TransferPoolToken GetPool();
			[[nodiscard]] TransferPoolToken GetReusablePool();

		private:
			std::vector<vk::raii::CommandPool> m_Pools;
			std::vector<u32> m_FreeIndex;
			std::vector<u32> m_FreeIndexReusable;
			std::mutex m_Mutex;
			std::condition_variable m_Cv;
			size_t m_PoolCount = 0;
		};

	public:
		VulkanCore() = delete;
		~VulkanCore() = default;

		explicit VulkanCore(PRIVATE) {};

		// Delete copy constructor and assignment operator to enforce singleton pattern
		// Also delete move constructor and move assignment operator
		// to prevent taking ownership of the singleton instance
		VulkanCore(const VulkanCore&) = delete;
		VulkanCore& operator=(const VulkanCore&) = delete;
		VulkanCore(VulkanCore&&) = delete;
		VulkanCore& operator=(VulkanCore&&) = delete;

		static const VulkanCore& GetInstance() { return *s_Instance; }
		static const VulkanCore& GetConstInstance() { return *s_Instance; }

		static void Init();

		static void Shutdown() { s_Instance.reset(); }

		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetGraphicsCommandBuffer() const;
		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetComputeCommandBuffer() const;
		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetTransferCommandBuffer(const TransferPoolToken& token) const;
		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetDynamicGraphicsCommandBuffer() const;
		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetDynamicComputeCommandBuffer() const;
		[[nodiscard]] OWC_FORCE_INLINE std::vector<vk::raii::CommandBuffer> GetDynamicTransferCommandBuffer(const TransferPoolToken& token) const { return GetTransferCommandBuffer(token); }
		[[nodiscard]] vk::raii::CommandBuffer GetSingleTimeGraphicsCommandBuffer() const;
		[[nodiscard]] vk::raii::CommandBuffer GetSingleTimeComputeCommandBuffer() const;
		[[nodiscard]] vk::raii::CommandBuffer GetSingleTimeTransferCommandBuffer(const TransferPoolToken& token) const;
		[[nodiscard]] vk::raii::CommandBuffer GetSingleTimeReusableGraphicsCommandBuffer() const;
		[[nodiscard]] vk::raii::CommandBuffer GetSingleTimeReusableComputeCommandBuffer() const;
		[[nodiscard]] OWC_FORCE_INLINE vk::raii::CommandBuffer GetSingleTimeReusableTransferCommandBuffer(const TransferPoolToken& token) const { return GetSingleTimeTransferCommandBuffer(token); }

		[[nodiscard]] static std::vector<vk::Semaphore> GetSemaphoresFromNames(std::span<std::string_view> semaphoreNames);
		[[nodiscard]] static  std::vector<vk::Semaphore> GetPreviousFrameSemaphoresFromNames(std::span<std::string_view> semaphoreNames);
		[[nodiscard]] vk::raii::Semaphore GetSingleSemaphore() const;

		[[nodiscard]] u32 FindMemoryType(vk::DeviceSize size, vk::MemoryPropertyFlags properties) const;

		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::Context& GetVKContext() const { return m_Context; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::Instance& GetVKInstance() const { return m_Instance; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::SurfaceKHR& GetSurface() const { return m_Surface; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::PhysicalDevice& GetPhysicalDev() const { return m_PhysicalDevice; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::Device& GetDevice() const { return m_Device; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::Queue& GetPresentQueue() const { return m_PresentQueue; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::Queue& GetGraphicsQueue() const { return m_GraphicsQueue; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::Queue& GetComputeQueue() const { return m_ComputeQueue; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetGraphicsCommandPool() const { return m_GraphicsCommandPool; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetComputeCommandPool() const { return m_ComputeCommandPool; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetDynamicGraphicsCommandPool() const { return m_DynamicGraphicsCommandPool; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetDynamicComputeCommandPool() const { return m_DynamicComputeCommandPool; }
		[[nodiscard]] TransferPoolToken GetTransferPoolToken() const;
		[[nodiscard]] TransferPoolToken GetReusableTransferPoolToken() const;
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::SwapchainKHR& GetSwapchain() const { return m_Swapchain; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::Format& GetSwapchainImageFormat() const { return m_SwapchainImageFormat; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorPool& GetImGuiDescriptorPool() const { return m_ImGuiDescriptorPool; }
		[[nodiscard]] OWC_FORCE_INLINE const std::vector<vk::Image>& GetSwapchainImages() const { return m_SwapchainImages; }
		[[nodiscard]] OWC_FORCE_INLINE const std::vector<vk::raii::ImageView>& GetSwapchainImageViews() const { return m_SwapchainImageViews; }
		[[nodiscard]] OWC_FORCE_INLINE const vma::raii::Allocator& GetVulkanMemoryAllocator() const { return m_Allocator; }
		[[nodiscard]] OWC_FORCE_INLINE uSize GetCurrentFrameIndex() const { return m_CurrentFrameIndex; }
		[[nodiscard]] OWC_FORCE_INLINE uSize GetPreviousFrameIndex() const { return m_PreviousFrameIndex; }
		[[nodiscard]] OWC_FORCE_INLINE uSize GetNumberOfFramesInFlight() const { return m_SwapchainImageViews.size(); }

		[[nodiscard]] OWC_FORCE_INLINE const vk::PhysicalDeviceProperties& GetPhysicalDeviceProperties() const { return m_PhysicalDeviceProperties; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& GetRayTracingPipelineProperties() const { return m_RayTracingPipelineProperties; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::PhysicalDeviceAccelerationStructurePropertiesKHR& GetAccelerationStructureProperties() const { return m_AccelerationStructureProperties; }

		[[nodiscard]] OWC_FORCE_INLINE u32 GetGraphicsQueueIndex() const { return m_GraphicsQueueFamilyIndex; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetComputeQueueIndex() const { return m_ComputeQueueFamilyIndex; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetTransferQueueIndex() const { return m_TransferQueueFamilyIndex; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetPresentQueueIndex() const { return m_PresentQueueFamilyIndex; }
		[[nodiscard]] OWC_FORCE_INLINE std::array<u32, 3> GetAllQueues() const { return { m_GraphicsQueueFamilyIndex, m_ComputeQueueFamilyIndex, m_TransferQueueFamilyIndex }; }
		[[nodiscard]] OWC_FORCE_INLINE const std::vector<u32>& GetAllUniqueQueuesIndices() const { return m_UniqueQueueFamilyIndices;}

		// Store end-of-frame cleanup callables as move-only type-erased wrappers (C++23)
		[[nodiscard]] static OWC_FORCE_INLINE std::vector<std::list<std::move_only_function<void()>>>& GetEndOfFrameCleanUp() { return s_Instance->m_EndOfFrameCleanUp; }

		[[nodiscard]] OWC_FORCE_INLINE bool HasSERSupport() const { return hasSERSupport; }

		OWC_FORCE_INLINE static void SetInstance(vk::raii::Instance&& instance) { s_Instance->m_Instance = std::move(instance); }
#ifndef DIST
		OWC_FORCE_INLINE static void SetDebugCallback(vk::raii::DebugUtilsMessengerEXT&& debugCallback) { s_Instance->m_DebugCallback = std::move(debugCallback); }
#endif
		OWC_FORCE_INLINE static void SetSurface(vk::raii::SurfaceKHR&& surface) { s_Instance->m_Surface = std::move(surface); }
		OWC_FORCE_INLINE static void SetPhysicalDevice(const vk::raii::PhysicalDevice& physicalDevice) {  s_Instance->m_PhysicalDevice = physicalDevice; }
		OWC_FORCE_INLINE static void SetDevice(vk::raii::Device&& device) { s_Instance->m_Device = std::move(device); }
		OWC_FORCE_INLINE static void SetPresentQueue(const vk::Queue& presentQueue) { s_Instance->m_PresentQueue = presentQueue; }
		OWC_FORCE_INLINE static void SetGraphicsQueue(const vk::Queue& graphicsQueue) { s_Instance->m_GraphicsQueue = graphicsQueue; }
		OWC_FORCE_INLINE static void SetComputeQueue(const vk::Queue& computeQueue) { s_Instance->m_ComputeQueue = computeQueue; }
		OWC_FORCE_INLINE static void SetTransferQueue(const vk::Queue& transferQueue) { s_Instance->m_TransferQueue = transferQueue; }
		OWC_FORCE_INLINE static void SetGraphicsCommandPool(vk::raii::CommandPool&& commandPool) { s_Instance->m_GraphicsCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE static void SetComputeCommandPool(vk::raii::CommandPool&& commandPool) { s_Instance->m_ComputeCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE static void SetDynamicGraphicsCommandPool(vk::raii::CommandPool&& commandPool) { s_Instance->m_DynamicGraphicsCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE static void SetDynamicComputeCommandPool(vk::raii::CommandPool&& commandPool) { s_Instance->m_DynamicComputeCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE static void SetSwapchainImageFormat(const vk::Format& format) { s_Instance->m_SwapchainImageFormat = format; }
		OWC_FORCE_INLINE static void SetImGuiDescriptorPool(vk::raii::DescriptorPool&& pool) { s_Instance->m_ImGuiDescriptorPool = std::move(pool);  }
		OWC_FORCE_INLINE static void SetSwapchain(vk::raii::SwapchainKHR&& swapchain) { s_Instance->m_Swapchain = std::move(swapchain); s_Instance->m_SwapchainImages = s_Instance->m_Swapchain.getImages(); }
		OWC_FORCE_INLINE static void SetSwapchainImageViews(std::vector<vk::raii::ImageView>&& swapchainImageViews) { s_Instance->m_SwapchainImageViews = std::move(swapchainImageViews); }
		OWC_FORCE_INLINE static void SetCurrentFrameIndex(const uSize newIndex) { s_Instance->m_CurrentFrameIndex = newIndex; }
		OWC_FORCE_INLINE static void SetPreviousFrameIndex(const uSize newIndex) { s_Instance->m_PreviousFrameIndex = newIndex; }
		OWC_FORCE_INLINE static void SetVulkanMemoryAllocator(vma::raii::Allocator&& allocator) { s_Instance->m_Allocator = std::move(allocator); }
		OWC_FORCE_INLINE static void AddVulkanEndOfFrameCleanUpFunction(std::move_only_function<void()>&& func) { s_Instance->m_EndOfFrameCleanUp[s_Instance->m_CurrentFrameIndex].emplace_back(std::move(func)); }
		OWC_FORCE_INLINE static void SetSERSupport(const bool support) { s_Instance->hasSERSupport = support; }
		OWC_FORCE_INLINE static void SetPhysicalDeviceProps(const vk::PhysicalDeviceProperties& physicalDeviceProperties) { s_Instance->m_PhysicalDeviceProperties = physicalDeviceProperties;	}

		// This clears both swapchain images and image views
		OWC_FORCE_INLINE static void ClearSwapchainImagesAndViews()
		{
			s_Instance->m_SwapchainImages.clear();
			s_Instance->m_SwapchainImageViews.clear();
		}

		OWC_FORCE_INLINE static void SetRTPhysicalDeviceProperties(const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingPipelineProperties, const vk::PhysicalDeviceAccelerationStructurePropertiesKHR& accelerationStructureProperties)
		{
			s_Instance->m_RayTracingPipelineProperties = rayTracingPipelineProperties;
			s_Instance->m_AccelerationStructureProperties = accelerationStructureProperties;
		}

		OWC_FORCE_INLINE static void SetQueueFamilyIndexes(const u32 graphicsIndex, const u32 computeIndex, const u32 transferIndex, const u32 presentIndex)
		{
			s_Instance->m_GraphicsQueueFamilyIndex = graphicsIndex;
			s_Instance->m_ComputeQueueFamilyIndex = computeIndex;
			s_Instance->m_TransferQueueFamilyIndex = transferIndex;
			s_Instance->m_PresentQueueFamilyIndex = presentIndex;

			s_Instance->m_UniqueQueueFamilyIndices = { graphicsIndex };

			if (computeIndex != graphicsIndex)
				s_Instance->m_UniqueQueueFamilyIndices.push_back(computeIndex);
			if (transferIndex != computeIndex && transferIndex != graphicsIndex)
				s_Instance->m_UniqueQueueFamilyIndices.push_back(transferIndex);
		}

		OWC_FORCE_INLINE static void SetupTransferPools()
		{
			if (s_Instance->m_TransferPoolRing)
				Log<LogLevel::Critical>("TransferPoolRing already exists. This should not happen.");

			s_Instance->m_TransferPoolRing = std::make_unique<TransferPoolRing>();
		}

		OWC_FORCE_INLINE static void SetupSemaphores()
		{
			s_Instance->m_Semaphores.reserve(s_Instance->m_SwapchainImageViews.size());
			for (uSize i = 0; i < s_Instance->m_SwapchainImageViews.size(); i++)
				s_Instance->m_Semaphores.emplace_back();
		}

		OWC_FORCE_INLINE static void DestroySemaphores()
		{
			s_Instance->m_Semaphores.clear();
		}

		OWC_FORCE_INLINE void SubmitTransferCommandBuffer(const vk::SubmitInfo2& info, const vk::Fence& fence = {}) const
		{
			std::unique_lock lock(m_TransferCommandQueueMutex);
			m_TransferQueue.submit2(info, fence);
		}

		OWC_FORCE_INLINE void ReturnTransferPoolToken(const TransferPoolToken& token) const
		{
			m_TransferPoolRing->ReturnPool(token);
		}

	private:
		vk::raii::Context m_Context = vk::raii::Context();
		vk::raii::Instance m_Instance = nullptr;
#ifndef DIST
		vk::raii::DebugUtilsMessengerEXT m_DebugCallback = nullptr;
#endif
		vk::raii::SurfaceKHR m_Surface = nullptr;
		vk::raii::PhysicalDevice m_PhysicalDevice = nullptr;
		vk::raii::Device m_Device = nullptr;
		vk::Queue m_PresentQueue = vk::Queue();
		vk::Queue m_GraphicsQueue = vk::Queue();
		vk::Queue m_ComputeQueue = vk::Queue();
		vk::Queue m_TransferQueue = vk::Queue();
		vk::raii::CommandPool m_GraphicsCommandPool = nullptr;
		vk::raii::CommandPool m_ComputeCommandPool = nullptr;
		vk::raii::CommandPool m_DynamicGraphicsCommandPool = nullptr;
		vk::raii::CommandPool m_DynamicComputeCommandPool = nullptr;
		std::unique_ptr<TransferPoolRing> m_TransferPoolRing = nullptr;
		vk::raii::SwapchainKHR m_Swapchain = nullptr;
		vk::Format m_SwapchainImageFormat = vk::Format::eUndefined;
		vk::raii::DescriptorPool m_ImGuiDescriptorPool = nullptr;
		std::vector<vk::Image> m_SwapchainImages{};
		std::vector<vk::raii::ImageView> m_SwapchainImageViews{};

		vk::PhysicalDeviceProperties m_PhysicalDeviceProperties{};

		vk::PhysicalDeviceRayTracingPipelinePropertiesKHR m_RayTracingPipelineProperties{};
		vk::PhysicalDeviceAccelerationStructurePropertiesKHR m_AccelerationStructureProperties{};

		std::vector<std::list<std::move_only_function<void()>>> m_EndOfFrameCleanUp{};

		vma::raii::Allocator m_Allocator = nullptr;

		std::vector<std::map<std::string, vk::raii::Semaphore>> m_Semaphores{};
		uSize m_CurrentFrameIndex = 0;
		uSize m_PreviousFrameIndex = 0;

		u32 m_GraphicsQueueFamilyIndex = 0;
		u32 m_ComputeQueueFamilyIndex = 0;
		u32 m_TransferQueueFamilyIndex = 0;
		u32 m_PresentQueueFamilyIndex = 0;
		std::vector<u32> m_UniqueQueueFamilyIndices{};

		bool hasSERSupport = false;

		mutable std::mutex m_TransferCommandQueueMutex;
		mutable std::mutex m_SingleUseGraphicsCommandPoolMutex;
		mutable std::mutex m_SingleUseComputeCommandPoolMutex;

		static std::unique_ptr<VulkanCore> s_Instance;

		friend class TransferPoolToken;
	};

	class TransferPoolToken
	{
	public:
		TransferPoolToken() = delete;
		OWC_FORCE_INLINE ~TransferPoolToken() { VulkanCore::GetInstance().ReturnTransferPoolToken(*this); }
		TransferPoolToken(const TransferPoolToken&) = delete;
		TransferPoolToken& operator=(const TransferPoolToken&) = delete;
		OWC_FORCE_INLINE TransferPoolToken(const TransferPoolToken&& other) noexcept : m_CommandPoolIndex(other.m_CommandPoolIndex) { other.m_CommandPoolIndex = std::numeric_limits<u32>::max(); }
		OWC_FORCE_INLINE TransferPoolToken& operator=(TransferPoolToken&& other) = delete;

	private:
		explicit OWC_FORCE_INLINE TransferPoolToken(const u32 commandPool) : m_CommandPoolIndex(commandPool) {}

		[[nodiscard]] OWC_FORCE_INLINE u32 GetPoolIndex() const noexcept { return m_CommandPoolIndex; }

	private:
		mutable u32 m_CommandPoolIndex;

		friend class VulkanCore;
		friend class VulkanCore::TransferPoolRing;
	};

	const vk::raii::CommandPool& VulkanCore::TransferPoolRing::GetPool(const TransferPoolToken& token) const noexcept
	{
		return m_Pools[token.GetPoolIndex()];
	}
}
