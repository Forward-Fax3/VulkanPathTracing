#pragma once
#include "GraphicsContext.hpp"
#include "Renderer.hpp"

#include <vulkan/vulkan_raii.hpp>


namespace OWC::Graphics
{
	class VulkanContext final : public GraphicsContext
	{
	private:
		struct QueueFamilyIndices
		{
			u32 PresentFamily = std::numeric_limits<u32>::max();
			u32 GraphicsFamily = std::numeric_limits<u32>::max();
			u32 ComputeFamily = std::numeric_limits<u32>::max();
			u32 TransferFamily = std::numeric_limits<u32>::max();
			
			std::vector<u32> UniqueIndices = {};

			// helper function to check if all families are found
			[[nodiscard]] inline bool FoundAll() const
			{
				return (PresentFamily | GraphicsFamily |
						ComputeFamily | TransferFamily)
						!= std::numeric_limits<u32>::max();
			}
		};

	public:
		VulkanContext() = delete;
		explicit VulkanContext(SDL_Window& windowHandle, const WindowProperties& properties);
		~VulkanContext() override;
		VulkanContext(const VulkanContext&) = delete;
		VulkanContext& operator=(const VulkanContext&) = delete;
		VulkanContext(VulkanContext&&) = delete;
		VulkanContext& operator=(VulkanContext&&) = delete;

		void FinishRender() override;
		void SwapPresentImage() override;
		void WaitForIdle() override;
#ifndef DIST
		void FlushValidationMessages() override;
#endif

		void Minimize() override;
		void Restore() override;
		void Resize() override { RecreateSwapchain(); RewriteCommandBuffers(); }

		void ImGuiInit() override;
		void ImGuiShutdown() override;
		void ImGuiNewFrame() override;

	private:
		static void StartInstance();
#ifndef DIST
		void EnableVulkanDebugging();
#endif
		static void SurfaceInit(SDL_Window& windowHandle);
		static void SelectPhysicalDevice();
		static std::tuple<bool, u32, std::vector<vk::ExtensionProperties>> IsPhysicalDeviceSuitable(const vk::PhysicalDevice& device);
		void FindQueueFamilies();
		void CheckQueueFamilyValidity(const std::vector<vk::QueueFamilyProperties>& queueFamilies);
		void GetAndStoreGlobalQueueFamilies() const;
		void CreateLogicalDevice();
		void CreateSwapchain() const;
		void CreateCommandPools() const;
		void WriteCommandBuffers();
		static void CreateVulkanMemoryAllocator();

		void RecreateSwapchain();
		void RewriteCommandBuffers();

	private:
		QueueFamilyIndices m_QueueFamilyIndices{};

		std::vector<vk::raii::CommandBuffer> m_BeginRenderCmdBuf{};
		std::vector<vk::raii::CommandBuffer> m_EndRenderCmdBuf{};

		const WindowProperties& m_WindowProperties;

		bool m_IsMinimized = false;
	};
}
