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

	[[nodiscard]] OWC_FORCE_INLINE std::pair<bool, std::string_view> IsExtensionAvailable(const std::vector<vk::ExtensionProperties>& extensions, const std::span<const char*> requestLayer)
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

	class VulkanCore
	{
	private:
		class PRIVATE {};

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

		static VulkanCore& GetInstance() { return *s_Instance; }
		static const VulkanCore& GetConstInstance() { return *s_Instance; }

		static void Init();

		static void Shutdown() { s_Instance.reset(); }

		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetGraphicsCommandBuffer() const;
		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetComputeCommandBuffer() const;
		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetTransferCommandBuffer() const;
		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetDynamicGraphicsCommandBuffer() const;
		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetDynamicComputeCommandBuffer() const;
		[[nodiscard]] std::vector<vk::raii::CommandBuffer> GetDynamicTransferCommandBuffer() const;
		[[nodiscard]] vk::raii::CommandBuffer GetSingleTimeGraphicsCommandBuffer() const;
		[[nodiscard]] vk::raii::CommandBuffer GetSingleTimeComputeCommandBuffer() const;
		[[nodiscard]] vk::raii::CommandBuffer GetSingleTimeTransferCommandBuffer() const;

		[[nodiscard]] std::vector<vk::Semaphore> GetSemaphoresFromNames(std::span<std::string_view> semaphoreNames);
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
		[[nodiscard]] OWC_FORCE_INLINE const vk::Queue& GetTransferQueue() const { return m_TransferQueue; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetGraphicsCommandPool() const { return m_GraphicsCommandPool; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetComputeCommandPool() const { return m_ComputeCommandPool; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetTransferCommandPool() const { return m_TransferCommandPool; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetDynamicGraphicsCommandPool() const { return m_DynamicGraphicsCommandPool; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetDynamicComputeCommandPool() const { return m_DynamicComputeCommandPool; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::CommandPool& GetDynamicTransferCommandPool() const { return m_DynamicTransferCommandPool; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::SwapchainKHR& GetSwapchain() const { return m_Swapchain; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::Format& GetSwapchainImageFormat() const { return m_SwapchainImageFormat; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::raii::DescriptorPool& GetImGuiDescriptorPool() const { return m_ImGuiDescriptorPool; }
		[[nodiscard]] OWC_FORCE_INLINE const std::vector<vk::Image>& GetSwapchainImages() const { return m_SwapchainImages; }
		[[nodiscard]] OWC_FORCE_INLINE const std::vector<vk::raii::ImageView>& GetSwapchainImageViews() const { return m_SwapchainImageViews; }
		[[nodiscard]] OWC_FORCE_INLINE const vma::raii::Allocator& GetVulkanMemoryAllocator() const { return m_Allocator; }
		[[nodiscard]] OWC_FORCE_INLINE uSize GetCurrentFrameIndex() const { return m_CurrentFrameIndex; }
		[[nodiscard]] OWC_FORCE_INLINE uSize GetNumberOfFramesInFlight() const { return m_SwapchainImageViews.size(); }

		[[nodiscard]] OWC_FORCE_INLINE const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& GetRayTracingPipelineProperties() const { return m_RayTracingPipelineProperties; }
		[[nodiscard]] OWC_FORCE_INLINE const vk::PhysicalDeviceAccelerationStructurePropertiesKHR& GetAccelerationStructureProperties() const { return m_AccelerationStructureProperties; }

		[[nodiscard]] OWC_FORCE_INLINE u32 GetGraphicsQueueIndex() const { return m_GraphicsQueueFamilyIndex; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetComputeQueueIndex() const { return m_ComputeQueueFamilyIndex; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetTransferQueueIndex() const { return m_TransferQueueFamilyIndex; }
		[[nodiscard]] OWC_FORCE_INLINE u32 GetPresentQueueIndex() const { return m_PresentQueueFamilyIndex; }
		[[nodiscard]] OWC_FORCE_INLINE std::array<u32, 3> GetAllQueues() const { return { m_GraphicsQueueFamilyIndex, m_ComputeQueueFamilyIndex, m_TransferQueueFamilyIndex }; }
		[[nodiscard]] OWC_FORCE_INLINE const std::vector<u32>& GetAllUniqueQueuesIndices() const { return m_UniqueQueueFamilyIndices;}

		[[nodiscard]] OWC_FORCE_INLINE std::vector<vk::Image>& GetSwapchainImages() { return m_SwapchainImages; }
		[[nodiscard]] OWC_FORCE_INLINE std::vector<vk::raii::ImageView>& GetSwapchainImageViews() { return m_SwapchainImageViews; }

		// Store end-of-frame cleanup callables as move-only type-erased wrappers (C++23)
		[[nodiscard]] OWC_FORCE_INLINE std::vector<std::list<std::move_only_function<void()>>>& GetEndOfFrameCleanUp() { return m_EndOfFrameCleanUp; }

		[[nodiscard]] OWC_FORCE_INLINE vk::Semaphore GetLastFrameFinishedSemaphore() const { return m_LastFrameWaitSemaphore; }

		[[nodiscard]] OWC_FORCE_INLINE bool HasSERSupport() const { return hasSERSupport; }

		OWC_FORCE_INLINE void SetInstance(vk::raii::Instance&& instance) { m_Instance = std::move(instance); }
#ifndef DIST
		OWC_FORCE_INLINE void SetDebugCallback(vk::raii::DebugUtilsMessengerEXT&& debugCallback) { m_DebugCallback = std::move(debugCallback); }
#endif
		OWC_FORCE_INLINE void SetSurface(vk::raii::SurfaceKHR&& surface) { m_Surface = std::move(surface); }
		OWC_FORCE_INLINE void SetPhysicalDevice(const vk::raii::PhysicalDevice& physicalDevice) { m_PhysicalDevice = physicalDevice; }
		OWC_FORCE_INLINE void SetDevice(vk::raii::Device&& device) { m_Device = std::move(device); }
		OWC_FORCE_INLINE void SetPresentQueue(const vk::Queue& presentQueue) { m_PresentQueue = presentQueue; }
		OWC_FORCE_INLINE void SetGraphicsQueue(const vk::Queue& graphicsQueue) { m_GraphicsQueue = graphicsQueue; }
		OWC_FORCE_INLINE void SetComputeQueue(const vk::Queue& computeQueue) { m_ComputeQueue = computeQueue; }
		OWC_FORCE_INLINE void SetTransferQueue(const vk::Queue& transferQueue) { m_TransferQueue = transferQueue; }
		OWC_FORCE_INLINE void SetGraphicsCommandPool(vk::raii::CommandPool&& commandPool) { m_GraphicsCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE void SetComputeCommandPool(vk::raii::CommandPool&& commandPool) { m_ComputeCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE void SetTransferCommandPool(vk::raii::CommandPool&& commandPool) { m_TransferCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE void SetDynamicGraphicsCommandPool(vk::raii::CommandPool&& commandPool) { m_DynamicGraphicsCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE void SetDynamicComputeCommandPool(vk::raii::CommandPool&& commandPool) { m_DynamicComputeCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE void SetDynamicTransferCommandPool(vk::raii::CommandPool&& commandPool) { m_DynamicTransferCommandPool = std::move(commandPool); }
		OWC_FORCE_INLINE void SetSwapchainImageFormat(const vk::Format& format) { m_SwapchainImageFormat = format; }
		OWC_FORCE_INLINE void SetImGuiDescriptorPool(vk::raii::DescriptorPool&& pool) { m_ImGuiDescriptorPool = std::move(pool);  }
		OWC_FORCE_INLINE void SetSwapchain(vk::raii::SwapchainKHR&& swapchain) { m_Swapchain = std::move(swapchain); m_SwapchainImages = m_Swapchain.getImages(); }
		OWC_FORCE_INLINE void SetSwapchainImageViews(std::vector<vk::raii::ImageView>&& swapchainImageViews) { m_SwapchainImageViews = std::move(swapchainImageViews); }
		OWC_FORCE_INLINE void SetCurrentFrameIndex(const uSize newIndex) { m_CurrentFrameIndex = newIndex; }
		OWC_FORCE_INLINE void SetVulkanMemoryAllocator(vma::raii::Allocator&& allocator) { m_Allocator = std::move(allocator); }
		OWC_FORCE_INLINE void AddVulkanEndOfFrameCleanUpFunction(std::move_only_function<void()>&& func) { m_EndOfFrameCleanUp[m_CurrentFrameIndex].emplace_back(std::move(func)); }

		OWC_FORCE_INLINE void SetLastFrameWaitSemaphore(const vk::Semaphore semaphore) { m_LastFrameWaitSemaphore = semaphore; }

		OWC_FORCE_INLINE void SetSERSupport(const bool support) { hasSERSupport = support; }

		OWC_FORCE_INLINE void SetRTPhysicalDeviceProperties(const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& rayTracingPipelineProperties, const vk::PhysicalDeviceAccelerationStructurePropertiesKHR& accelerationStructureProperties)
		{
			m_RayTracingPipelineProperties = rayTracingPipelineProperties;
			m_AccelerationStructureProperties = accelerationStructureProperties;
		}

		OWC_FORCE_INLINE void SetQueueFamilyIndexes(const u32 graphicsIndex, const u32 computeIndex, const u32 transferIndex, const u32 presentIndex)
		{
			m_GraphicsQueueFamilyIndex = graphicsIndex;
			m_ComputeQueueFamilyIndex = computeIndex;
			m_TransferQueueFamilyIndex = transferIndex;
			m_PresentQueueFamilyIndex = presentIndex;

			m_UniqueQueueFamilyIndices = { graphicsIndex };

			if (computeIndex != graphicsIndex)
				m_UniqueQueueFamilyIndices.push_back(computeIndex);
			if (transferIndex != computeIndex && transferIndex != graphicsIndex)
				m_UniqueQueueFamilyIndices.push_back(transferIndex);
		}

		OWC_FORCE_INLINE void SetupSemaphores()
		{
			m_Semaphores.reserve(m_SwapchainImageViews.size());
			for (uSize i = 0; i < m_SwapchainImageViews.size(); i++)
				m_Semaphores.emplace_back();
		}

		OWC_FORCE_INLINE void DestroySemaphores()
		{
			m_Semaphores.clear();
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
		vk::raii::CommandPool m_TransferCommandPool = nullptr;
		vk::raii::CommandPool m_DynamicGraphicsCommandPool = nullptr;
		vk::raii::CommandPool m_DynamicComputeCommandPool = nullptr;
		vk::raii::CommandPool m_DynamicTransferCommandPool = nullptr;
		vk::raii::SwapchainKHR m_Swapchain = nullptr;
		vk::Format m_SwapchainImageFormat = vk::Format::eUndefined;
		vk::raii::DescriptorPool m_ImGuiDescriptorPool = nullptr;
		std::vector<vk::Image> m_SwapchainImages{};
		std::vector<vk::raii::ImageView> m_SwapchainImageViews{};

		vk::PhysicalDeviceRayTracingPipelinePropertiesKHR m_RayTracingPipelineProperties{};
		vk::PhysicalDeviceAccelerationStructurePropertiesKHR m_AccelerationStructureProperties{};

		std::vector<std::list<std::move_only_function<void()>>> m_EndOfFrameCleanUp{};

		vma::raii::Allocator m_Allocator = nullptr;

		std::vector<std::map<std::string, vk::raii::Semaphore>> m_Semaphores{};
		uSize m_CurrentFrameIndex = 0;

		u32 m_GraphicsQueueFamilyIndex = 0;
		u32 m_ComputeQueueFamilyIndex = 0;
		u32 m_TransferQueueFamilyIndex = 0;
		u32 m_PresentQueueFamilyIndex = 0;
		std::vector<u32> m_UniqueQueueFamilyIndices{};

		vk::Semaphore m_LastFrameWaitSemaphore = vk::Semaphore();

		bool hasSERSupport = false;

		static std::unique_ptr<VulkanCore> s_Instance;
	};
}
