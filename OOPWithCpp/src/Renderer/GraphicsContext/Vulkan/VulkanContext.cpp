#include <limits>
#include <ranges>
#include <mutex>
#include <map>

#include "Core.hpp"

#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_to_string.hpp>
#include <SDL3/SDL_vulkan.h>
#include <backends/imgui_impl_vulkan.h>

#include "Application.hpp"
#include "VulkanContext.hpp"
#include "VulkanCore.hpp"
#include "Log.hpp"


#ifndef DIST
PFN_vkCreateDebugUtilsMessengerEXT pfnVkCreateDebugUtilsMessengerEXT;
PFN_vkDestroyDebugUtilsMessengerEXT pfnVkDestroyDebugUtilsMessengerEXT;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(
	VkInstance instance,
	const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkDebugUtilsMessengerEXT* pMessenger)
{
	return pfnVkCreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator, pMessenger);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(
	VkInstance instance,
	VkDebugUtilsMessengerEXT messenger,
	VkAllocationCallbacks const* pAllocator)
{
	return pfnVkDestroyDebugUtilsMessengerEXT(instance, messenger, pAllocator);
}

static void PrintVulkanDebugMessages(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, const std::vector<std::string>& s_MessagesLogged)
{
	using enum vk::DebugUtilsMessageSeverityFlagBitsEXT;
	using enum OWC::LogLevel;

	std::string loggedMessage;
	for (const auto& msg : s_MessagesLogged)
		loggedMessage += "\t " + msg + "\n";

	if (messageSeverity & eError)
		OWC::Log<Critical>("Vulkan Validation Layer:\n{}", loggedMessage);
	else if (messageSeverity & eWarning)
		OWC::Log<Warn>("Vulkan Validation Layer:\n{}", loggedMessage);
	else
		OWC::Log<Trace>("Vulkan Validation Layer:\n{}", loggedMessage);
}

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugMessageFunc( // TODO: add objects info logging and add auto flush after certain amount of messages or time
	vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
	vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData,
	void* /*pUserData*/)
{
	static std::mutex s_Mutex;
	static std::vector<std::string> s_MessagesLogged;
	static OWC::i32 lastMessageID = std::numeric_limits<OWC::i32>::max();
	static vk::DebugUtilsMessageSeverityFlagBitsEXT lastMessageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT();
	static bool firstMessage = true;

	if (pCallbackData == nullptr)
	{
		std::lock_guard lock(s_Mutex);

		if (s_MessagesLogged.empty())
			return vk::False;

		PrintVulkanDebugMessages(lastMessageSeverity, s_MessagesLogged);
		s_MessagesLogged.clear();
		lastMessageID = std::numeric_limits<OWC::i32>::max();
		lastMessageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT();
		firstMessage = true;
		return vk::False;
	}

	std::string str = std::format(
		"{} {}: {}",
		pCallbackData->pMessageIdName,
		vk::to_string(messageTypes),
		pCallbackData->pMessage ? pCallbackData->pMessage : "NULL"
	);

	if (pCallbackData->queueLabelCount > 0)
	{
		str += "\n\t Queue Labels:";
		for (OWC::u32 i = 0; i < pCallbackData->queueLabelCount; i++)
		{
			str += std::format("\n\t\t labelName = <{}>",
				pCallbackData->pQueueLabels[i].pLabelName ? pCallbackData->pQueueLabels[i].pLabelName : "NULL");
		}
	}
	if (pCallbackData->cmdBufLabelCount > 0)
	{
		str += "\n\t CommandBuffer Labels:";
		for (OWC::u32 i = 0; i < pCallbackData->cmdBufLabelCount; i++)
		{
			str += std::format("\n\t\t labelName = <{}>",
				pCallbackData->pCmdBufLabels[i].pLabelName ? pCallbackData->pCmdBufLabels[i].pLabelName : "NULL");
		}
	}
//	if (pCallbackData->objectCount > 0 /* && !std::string("NULL").compare(pCallbackData->pObjects[0].pObjectName) */)
//	{
//		str += "\n\t Objects:";
//		for (u32 i = 0; i < pCallbackData->objectCount; i++)
//		{
//			str += std::format(
//				"\n\t\t objectName = <{}>"
//				"\n\t\t objectType = {}",
//				pCallbackData->pObjects[i].pObjectName ? pCallbackData->pObjects[i].pObjectName : "NULL",
//				vk::to_string(pCallbackData->pObjects[i].objectType)
//			);
//		}
//	}

	// leave the lock as late as possible
	std::lock_guard lock(s_Mutex);
	if (pCallbackData->messageIdNumber == lastMessageID && messageSeverity == lastMessageSeverity)
		s_MessagesLogged.emplace_back(std::move(str));
	else if (firstMessage)
	{
		s_MessagesLogged.emplace_back(std::move(str));

		if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
		{
			PrintVulkanDebugMessages(messageSeverity, s_MessagesLogged);
			s_MessagesLogged.clear();
		}
		else
		{
			lastMessageID = pCallbackData->messageIdNumber;
			lastMessageSeverity = messageSeverity;
			firstMessage = false;
		}
	}
	else
	{
		PrintVulkanDebugMessages(lastMessageSeverity, s_MessagesLogged);
		s_MessagesLogged.clear();
		s_MessagesLogged.emplace_back(std::move(str));
		if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
		{
			// flush immediately on error messages
			PrintVulkanDebugMessages(messageSeverity, s_MessagesLogged);
			s_MessagesLogged.clear();
			lastMessageID = std::numeric_limits<OWC::i32>::max();
			lastMessageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT();
			firstMessage = true;
		}
		else
		{
			lastMessageID = pCallbackData->messageIdNumber;
			lastMessageSeverity = messageSeverity;
		}
	}

	return vk::False;
}
#endif

PFN_vkCreateRayTracingPipelinesKHR pfnVkCreateRayTracingPipelinesKHR;
PFN_vkGetAccelerationStructureBuildSizesKHR pfnVkGetAccelerationStructureBuildSizesKHR;
PFN_vkCreateAccelerationStructureKHR pfnVkCreateAccelerationStructureKHR;
PFN_vkCmdBuildAccelerationStructuresKHR pfnVkCmdBuildAccelerationStructureKHR;
PFN_vkDestroyAccelerationStructureKHR pfnVkDestroyAccelerationStructureKHR;
PFN_vkGetAccelerationStructureDeviceAddressKHR pfnVkGetAccelerationStructureDeviceAddressKHR;
PFN_vkGetRayTracingShaderGroupHandlesKHR pfnVkGetRayTracingShaderGroupHandlesKHR;
PFN_vkCmdTraceRaysKHR pfnVkCmdTraceRaysKHR;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(
	VkDevice device,
	VkDeferredOperationKHR deferredOperation,
	VkPipelineCache pipelineCache,
	uint32_t createInfoCount,
	const VkRayTracingPipelineCreateInfoKHR* pCreateInfos,
	const VkAllocationCallbacks* pAllocator,
	VkPipeline* pPipelines)
{
	return pfnVkCreateRayTracingPipelinesKHR(device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}

VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureBuildSizesKHR(
	VkDevice device,
	VkAccelerationStructureBuildTypeKHR buildType,
	const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
	const uint32_t* pMaxPrimitiveCounts,
	VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo)
{
	pfnVkGetAccelerationStructureBuildSizesKHR(device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(
	VkDevice device,
	const VkAccelerationStructureCreateInfoKHR* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkAccelerationStructureKHR* pAccelerationStructure)
{
	return pfnVkCreateAccelerationStructureKHR(device, pCreateInfo, pAllocator, pAccelerationStructure);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresKHR(
	VkCommandBuffer commandBuffer,
	uint32_t infoCount,
	const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
	const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
{
	pfnVkCmdBuildAccelerationStructureKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
}

VKAPI_CALL void VKAPI_CALL vkDestroyAccelerationStructureKHR(
	VkDevice device,
	VkAccelerationStructureKHR accelerationStructure,
	const VkAllocationCallbacks* pAllocator)
{
	pfnVkDestroyAccelerationStructureKHR(device, accelerationStructure, pAllocator);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetAccelerationStructureDeviceAddressKHR(
	VkDevice device,
	const VkAccelerationStructureDeviceAddressInfoKHR* pInfo)
{
	return pfnVkGetAccelerationStructureDeviceAddressKHR(device, pInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesKHR(
	VkDevice device,
	VkPipeline pipeline,
	uint32_t firstGroup,
	uint32_t groupCount,
	size_t dataSize,
	void* pData)
{
	return pfnVkGetRayTracingShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount, dataSize, pData);
}

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysKHR(
	VkCommandBuffer commandBuffer,
	const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable,
	const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
	const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
	const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
	uint32_t width,
	uint32_t height,
	uint32_t depth)
{
	pfnVkCmdTraceRaysKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
}

namespace OWC::Graphics
{
	static std::array g_DeviceExtensions = {
#if defined(_WIN32) || defined(_WIN64)
		vk::EXTPageableDeviceLocalMemoryExtensionName,
		vk::EXTMemoryPriorityExtensionName,
#endif
		vk::KHRSwapchainExtensionName,
		vk::KHRAccelerationStructureExtensionName,
		vk::KHRRayTracingPipelineExtensionName,
		vk::KHRBufferDeviceAddressExtensionName,
		vk::KHRDeferredHostOperationsExtensionName,
		vk::KHRShaderNonSemanticInfoExtensionName,
		vk::KHRRayQueryExtensionName,
		vk::KHRRayTracingMaintenance1ExtensionName
	};


	static std::array g_DeviceExtensionsWithSER = {
#if defined(_WIN32) || defined(_WIN64)
		vk::EXTPageableDeviceLocalMemoryExtensionName,
		vk::EXTMemoryPriorityExtensionName,
#endif
		vk::KHRSwapchainExtensionName,
		vk::KHRAccelerationStructureExtensionName,
		vk::KHRRayTracingPipelineExtensionName,
		vk::KHRBufferDeviceAddressExtensionName,
		vk::KHRDeferredHostOperationsExtensionName,
		vk::KHRShaderNonSemanticInfoExtensionName,
		vk::KHRRayQueryExtensionName,
		vk::KHRRayTracingMaintenance1ExtensionName,
		vk::EXTRayTracingInvocationReorderExtensionName
	};

	VulkanContext::VulkanContext(SDL_Window& windowHandle, const WindowProperties& properties)
		 : m_WindowProperties(properties)
	{
		VulkanCore::Init();
		
		try
		{
			StartInstance();
#ifndef DIST
			EnableVulkanDebugging();
#endif
			SurfaceInit(windowHandle);
			SelectPhysicalDevice();
			FindQueueFamilies();
			CreateLogicalDevice();
			GetAndStoreGlobalQueueFamilies();
			CreateSwapchain();
			CreateCommandPools();
			WriteCommandBuffers();
			CreateVulkanMemoryAllocator();
		}
		catch (const vk::SystemError& err)
		{
			Log<LogLevel::Critical>("Vulkan Context initialization failed: {}", err.what());
		}
		catch (const std::exception& ex)
		{
			Log<LogLevel::Critical>("Vulkan Context initialization failed: {}", ex.what());
		}

		SwapPresentImage();

#ifndef DIST 
		// flush any logged messages during initialization
		debugMessageFunc(
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral,
			nullptr,
			nullptr
		);
#endif
	}

	VulkanContext::~VulkanContext()
	{
		WaitForIdle();
		VulkanCore::GetInstance().DestroySemaphores();

#ifndef DIST
		// flush any remaining logged messages before destroying the debug messenger
		debugMessageFunc(
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral,
			nullptr,
			nullptr
		);
#endif

		m_BeginRenderCmdBuf.clear();
		m_EndRenderCmdBuf.clear();

		VulkanCore::Shutdown();
	}

	void VulkanContext::FinishRender()
	{
		auto& vkCore = VulkanCore::GetInstance();
		const auto& device = vkCore.GetDevice();

		if (!m_IsMinimized)
		{
			std::array<std::string_view, 1> semaphoreNames = { "RenderFinished" };
			auto semaphores = vkCore.GetSemaphoresFromNames(semaphoreNames);
			vkCore.SetLastFrameWaitSemaphore(semaphores[0]);

			vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eBottomOfPipe);
			const vk::SubmitInfo submitInfo = vk::SubmitInfo()
				.setSignalSemaphores(semaphores)
				.setCommandBuffers(*m_EndRenderCmdBuf[vkCore.GetCurrentFrameIndex()])
				.setWaitDstStageMask(waitDestinationStageMask)
				.setWaitSemaphores(VK_NULL_HANDLE);

			const vk::raii::Fence fence = device.createFence(vk::FenceCreateInfo());
			vkCore.GetGraphicsQueue().submit(submitInfo, fence);

			if (device.waitForFences(*fence, VK_TRUE, std::numeric_limits<uint32_t>::max()) != vk::Result::eSuccess)
				Log<LogLevel::Critical>("Failed to wait for render finished fence");

			auto& endOfFrameFuncs = vkCore.GetEndOfFrameCleanUp()[vkCore.GetCurrentFrameIndex()];
			for (auto& func : endOfFrameFuncs)
				func();
			endOfFrameFuncs.clear();

			auto indices = static_cast<u32>(vkCore.GetCurrentFrameIndex());
			const auto result = VulkanCore::GetConstInstance().GetPresentQueue().presentKHR(
				vk::PresentInfoKHR()
				.setSwapchains(*vkCore.GetSwapchain())
				.setSwapchainCount(1)
				.setImageIndices(indices)
				.setWaitSemaphores(semaphores)
			);

			(void)result;

//			uSize retryCount = 0;
//			while (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
//			{
//				if (retryCount++ >= 3)
//					Log<LogLevel::Critical>("VulkanContext::FinishRender: Failed to present image after 3 retries.");
//	
//				RecreateSwapchain();
//	
//				result = VulkanCore::GetConstInstance().GetPresentQueue().presentKHR(
//					vk::PresentInfoKHR()
//					.setSwapchains(vkCore.GetSwapchain())
//					.setSwapchainCount(1)
//					.setImageIndices(indices)
//				);
//			}
		}
	}

	void VulkanContext::SwapPresentImage()
	{
		auto& vkCore = VulkanCore::GetInstance();

		std::vector<std::string_view> imageAcquiredName = { "ImageAcquired" };
		auto imageAcquired = vkCore.GetSemaphoresFromNames(imageAcquiredName);

		auto result = vkCore.GetDevice().acquireNextImage2KHR(vk::AcquireNextImageInfoKHR()
			.setSwapchain(vkCore.GetSwapchain())
			.setSemaphore(imageAcquired[0])
//			.setTimeout(16'666)
			.setTimeout(std::numeric_limits<u32>::max())
			.setDeviceMask(1)
		);

		if (result.result == vk::Result::eErrorOutOfDateKHR)
		{
			RecreateSwapchain();
			result = vkCore.GetDevice().acquireNextImage2KHR(vk::AcquireNextImageInfoKHR()
				.setSwapchain(vkCore.GetSwapchain())
				.setSemaphore(imageAcquired[0])
//				.setTimeout(16'666)
				.setTimeout(std::numeric_limits<u32>::max())
				.setDeviceMask(1)
			);

			if (result.result == vk::Result::eErrorOutOfDateKHR)
				Log<LogLevel::Critical>("Failed to acquire next image from swapchain: {}", vk::to_string(result.result));
		}
		else if (result.result != vk::Result::eSuccess && result.result != vk::Result::eSuboptimalKHR)
			Log<LogLevel::Critical>("Failed to acquire next image from swapchain: {}", vk::to_string(result.result));

		// this should never happen since acquireNextImage2KHR should return an error if the index is out of bounds, but just in case
		if (result.value >= vkCore.GetSwapchainImageViews().size())
			Log<LogLevel::Critical>("Acquired image index {} is out of bounds for swapchain image views size {}", result.value, vkCore.GetSwapchainImageViews().size());

		vkCore.SetCurrentFrameIndex(result.value);

//		uSize retryCount = 0;
//
//		while (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
//		{
//			if (retryCount++ >= 3)
//				Log<LogLevel::Critical>("VulkanContext::SwapPresentImage: Failed to acquire next image after 3 retries.");
//
//			RecreateSwapchain();
//			result = vkCore.IncrementCurrentFrameIndex();
//		}

		std::array<std::string_view, 1> imageReadyName = { "ImageReady" };
		if (!Application::GetConstInstance().IsFirstFrame())
			imageAcquired.push_back(vkCore.GetLastFrameFinishedSemaphore());
		auto imageReady = vkCore.GetSemaphoresFromNames(imageReadyName)[0];

		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		const auto submitInfo = vk::SubmitInfo()
			.setWaitSemaphores(imageAcquired)
			.setSignalSemaphores(imageReady)
			.setCommandBuffers(*m_BeginRenderCmdBuf[vkCore.GetCurrentFrameIndex()])
			.setWaitDstStageMask(waitDestinationStageMask);

		vkCore.GetGraphicsQueue().submit(submitInfo, VK_NULL_HANDLE);
	}

	void VulkanContext::WaitForIdle()
	{
		VulkanCore::GetConstInstance().GetDevice().waitIdle();
	}

#ifndef DIST
	void VulkanContext::FlushValidationMessages()
	{
		debugMessageFunc(
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo,
			vk::DebugUtilsMessageTypeFlagsEXT(),
			nullptr,
			nullptr
		);
	}
#endif

	void VulkanContext::StartInstance()
	{
		std::vector<const char*> extensions;
		{
			u32 numberOfSDLExtensions = 0;
			const auto extensionsTemp = SDL_Vulkan_GetInstanceExtensions(&numberOfSDLExtensions);

			if constexpr (!IsDistributionMode()) // +3 for debug utils and get physical device properties 2 and surface extensions
				extensions.reserve(static_cast<uSize>(numberOfSDLExtensions) + 3);
			else // +2 for get physical device properties 2 and surface extensions
				extensions.reserve(static_cast<uSize>(numberOfSDLExtensions) + 2);

			for (uSize i = 0; i < numberOfSDLExtensions; i++)
				extensions.emplace_back(extensionsTemp[i]);
		}

		const auto instanceExtensionProperties = vk::enumerateInstanceExtensionProperties();

		extensions.emplace_back(vk::KHRGetSurfaceCapabilities2ExtensionName);

		vk::InstanceCreateInfo createInfo;
		
		std::vector<const char*> validationLayers;
#ifndef DIST
		extensions.emplace_back(vk::EXTDebugUtilsExtensionName);

		validationLayers.push_back("VK_LAYER_KHRONOS_validation");
		createInfo.setEnabledLayerCount(static_cast<u32>(validationLayers.size()));
		createInfo.setPpEnabledLayerNames(validationLayers.data());

		std::array validationFeaturesData = {
			vk::ValidationFeatureEnableEXT::eDebugPrintf
		};
		const auto validationFeatures = vk::ValidationFeaturesEXT()
			.setEnabledValidationFeatures(validationFeaturesData);
		createInfo.setPNext(&validationFeatures);
#endif

		if (const auto [extensionsAvailable, missingExtension] = IsExtensionAvailable(instanceExtensionProperties, extensions); !extensionsAvailable)
			Log<LogLevel::Critical>("Vulkan instance is missing required extension: {}", missingExtension);

		constexpr auto appInfo = vk::ApplicationInfo()
			.setPApplicationName("OOPWithCpp Application")
			.setApplicationVersion(VK_MAKE_VERSION(0, 0, 1))
			.setPEngineName("OOPWithCpp Engine")
			.setEngineVersion(VK_MAKE_VERSION(0, 0, 1))
			.setApiVersion(g_VulkanVersion);

		createInfo
			.setEnabledExtensionCount(static_cast<u32>(extensions.size()))
			.setPpEnabledExtensionNames(extensions.data())
			.setPApplicationInfo(&appInfo);

		const auto& vkCore = VulkanCore::GetConstInstance();

		VulkanCore::GetInstance().SetInstance(vk::raii::Instance(vkCore.GetVKContext(), createInfo));
		const auto& vkInstance = vkCore.GetVKInstance();

		pfnVkCreateRayTracingPipelinesKHR = std::bit_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkInstance.getProcAddr("vkCreateRayTracingPipelinesKHR"));
		pfnVkGetAccelerationStructureBuildSizesKHR = std::bit_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkInstance.getProcAddr("vkGetAccelerationStructureBuildSizesKHR"));
		pfnVkCreateAccelerationStructureKHR = std::bit_cast<PFN_vkCreateAccelerationStructureKHR>(vkInstance.getProcAddr("vkCreateAccelerationStructureKHR"));
		pfnVkCmdBuildAccelerationStructureKHR = std::bit_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkInstance.getProcAddr("vkCmdBuildAccelerationStructuresKHR"));
		pfnVkDestroyAccelerationStructureKHR = std::bit_cast<PFN_vkDestroyAccelerationStructureKHR>(vkInstance.getProcAddr("vkDestroyAccelerationStructureKHR"));
		pfnVkGetAccelerationStructureDeviceAddressKHR = std::bit_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkInstance.getProcAddr("vkGetAccelerationStructureDeviceAddressKHR"));
		pfnVkGetRayTracingShaderGroupHandlesKHR = std::bit_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkInstance.getProcAddr("vkGetRayTracingShaderGroupHandlesKHR"));
		pfnVkCmdTraceRaysKHR = std::bit_cast<PFN_vkCmdTraceRaysKHR>(vkInstance.getProcAddr("vkCmdTraceRaysKHR"));
	}

#ifndef DIST
	void VulkanContext::EnableVulkanDebugging()
	{
		const auto& vkInstance = VulkanCore::GetConstInstance().GetVKInstance();

		pfnVkCreateDebugUtilsMessengerEXT = std::bit_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkInstance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
		pfnVkDestroyDebugUtilsMessengerEXT = std::bit_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkInstance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
		if (!pfnVkCreateDebugUtilsMessengerEXT || !pfnVkDestroyDebugUtilsMessengerEXT)
			Log<LogLevel::Critical>("Failed to load Vulkan debug utils functions");

		constexpr auto debugUtilsCreateInfo = vk::DebugUtilsMessengerCreateInfoEXT()
			.setMessageSeverity(
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
			).setMessageType(
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
			vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
			vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
			).setPfnUserCallback(debugMessageFunc);

		VulkanCore::GetInstance().SetDebugCallback(vk::raii::DebugUtilsMessengerEXT(vkInstance, debugUtilsCreateInfo));
	}
#endif

	void VulkanContext::SurfaceInit(SDL_Window& windowHandle)
	{
		const auto& vkCore = VulkanCore::GetConstInstance();

		VkSurfaceKHR surface{};
		SDL_Vulkan_CreateSurface(&windowHandle, *VulkanCore::GetInstance().GetVKInstance(), nullptr, &surface);

		vk::raii::SurfaceKHR raiiSurface(vkCore.GetVKInstance(), surface);
		VulkanCore::GetInstance().SetSurface(std::move(raiiSurface));
	}

	void VulkanContext::SelectPhysicalDevice()
	{
		const auto& vkCore = VulkanCore::GetConstInstance();
		const auto& vkInstance = vkCore.GetVKInstance();

		const vk::raii::PhysicalDevices physicalDevices(vkInstance);

		if (physicalDevices.empty())
			Log<LogLevel::Critical>("Failed to find GPUs with Vulkan support");

		u32 highestScore = 0;
		for (auto& device : physicalDevices)
		{
			if (auto [isSuitable, score, supportedExtensions] = IsPhysicalDeviceSuitable(device);
				isSuitable && score > highestScore)
			{
				highestScore = score;
				VulkanCore::GetInstance().SetPhysicalDevice(device);
				const bool extensionsAvailable = IsExtensionAvailable(supportedExtensions, vk::EXTRayTracingInvocationReorderExtensionName);
				VulkanCore::GetInstance().SetSERSupport(extensionsAvailable);
			}
		}

		VulkanCore::GetInstance().SetSERSupport(false);

		if (const auto pDevice = VulkanCore::GetConstInstance().GetPhysicalDev(); pDevice == nullptr)
			Log<LogLevel::Critical>("Failed to find a suitable GPU");
		else
		{
			auto deviceProperties = pDevice.getProperties();

			Log<LogLevel::Debug>("Selected GPU: {} (ID: {}, GPU Type: {})",
				deviceProperties.deviceName.data(),
				deviceProperties.deviceID,
				vk::to_string(deviceProperties.deviceType));
			Log<LogLevel::NewLine>();

			auto chain = pDevice.getProperties2<
				vk::PhysicalDeviceProperties2,
				vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
				vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();

			VulkanCore::GetInstance().SetRTPhysicalDeviceProperties(
				chain.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>(),
				chain.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()
				);
		}
	}

	std::tuple<bool, u32, std::vector<vk::ExtensionProperties>> VulkanContext::IsPhysicalDeviceSuitable(const vk::PhysicalDevice& device)
	{
		u32 score = 0;
		auto deviceProperties = device.getProperties2();
		auto supportedExtensions = device.enumerateDeviceExtensionProperties();

		if (deviceProperties.properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
			score += 1000;

		score += deviceProperties.properties.limits.maxImageDimension2D;

		if (const auto [extensionsAvailable, missingExtension] = IsExtensionAvailable(supportedExtensions, g_DeviceExtensions); !extensionsAvailable)
		{
			Log<LogLevel::Warn>("Physical device {} is missing required extension: {}",
				deviceProperties.properties.deviceName.data(),
				missingExtension);
			return { false, 0, {} };
		}

		return { true, score, std::move(supportedExtensions) };
	}

	void VulkanContext::FindQueueFamilies()
	{
		const auto queueFamilies = VulkanCore::GetConstInstance().GetPhysicalDev().getQueueFamilyProperties();
		constexpr auto indexMax = std::numeric_limits<u32>::max();

		for (u32 i = 0; i < static_cast<u32>(queueFamilies.size()); i++) // try to find all queue families in one loop and have different queue families if possible
		{
			OWC::Log<OWC::LogLevel::Trace>(
				"Queue Family {}: {} queues, flags: {}, present supported: {}",
				i,
				queueFamilies[i].queueCount,
				vk::to_string(queueFamilies[i].queueFlags),
				static_cast<bool>(VulkanCore::GetConstInstance().GetPhysicalDev().getSurfaceSupportKHR(
					i,
					VulkanCore::GetConstInstance().GetSurface()
				))
			);

			if (m_QueueFamilyIndices.PresentFamily == indexMax &&
				VulkanCore::GetConstInstance().GetPhysicalDev().getSurfaceSupportKHR(i, VulkanCore::GetConstInstance().GetSurface()))
				m_QueueFamilyIndices.PresentFamily = i;

			if (m_QueueFamilyIndices.GraphicsFamily == indexMax && (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) == vk::QueueFlagBits::eGraphics)
				m_QueueFamilyIndices.GraphicsFamily = i;

			// prefer a dedicated compute queue family
			else if (m_QueueFamilyIndices.ComputeFamily == indexMax && (queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute) == vk::QueueFlagBits::eCompute)
				m_QueueFamilyIndices.ComputeFamily = i;

			// prefer a dedicated transfer queue family
			else if (m_QueueFamilyIndices.TransferFamily == indexMax && (queueFamilies[i].queueFlags & vk::QueueFlagBits::eTransfer) == vk::QueueFlagBits::eTransfer
				&& (queueFamilies[i].queueFlags & vk::QueueFlags(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute)) == vk::QueueFlags(0))
				m_QueueFamilyIndices.TransferFamily = i;

			// break early if all found
			if (m_QueueFamilyIndices.FoundAll())
				break;
		}

		CheckQueueFamilyValidity(queueFamilies);

		Log<LogLevel::Debug>("Vulkan Queue Families found:");
		Log<LogLevel::Debug>(" Present Queue Family Index: {}", m_QueueFamilyIndices.PresentFamily);
		Log<LogLevel::Debug>(" Graphics Queue Family Index: {}", m_QueueFamilyIndices.GraphicsFamily);
		Log<LogLevel::Debug>(" Compute Queue Family Index: {}", m_QueueFamilyIndices.ComputeFamily);
		Log<LogLevel::Debug>(" Transfer Queue Family Index: {}", m_QueueFamilyIndices.TransferFamily);
		Log<LogLevel::NewLine>();
	}

	void VulkanContext::CheckQueueFamilyValidity(const std::vector<vk::QueueFamilyProperties>& queueFamilies)
	{
		constexpr auto indexMax = std::numeric_limits<u32>::max();

		std::map<u32, u32> numberOfUsesPerQueueFamily;
		for (const auto& index : { m_QueueFamilyIndices.PresentFamily, m_QueueFamilyIndices.GraphicsFamily, m_QueueFamilyIndices.ComputeFamily, m_QueueFamilyIndices.TransferFamily })
		{
			if (index == indexMax)
				continue;

			if (!numberOfUsesPerQueueFamily.contains(index))
				numberOfUsesPerQueueFamily[index] = 1;
			// no need for else as there will only be one per family index at this point
		}

		if (m_QueueFamilyIndices.PresentFamily == indexMax)
			Log<LogLevel::Critical>("Failed to find a valid present queue family index");
		else if (m_QueueFamilyIndices.GraphicsFamily == indexMax)
			Log<LogLevel::Critical>("Failed to find a valid graphics queue family index");
		else if (m_QueueFamilyIndices.ComputeFamily == indexMax) // check if compute queue family is found, otherwise find any even if it shares with others
		{
			Log<LogLevel::Warn>("Failed to find a unique compute queue family index, any compute queue will be searched for now");

			for (u32 i = 0; i < static_cast<u32>(queueFamilies.size()); i++)
				if ((queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute) == vk::QueueFlagBits::eCompute &&
					queueFamilies[i].queueCount > numberOfUsesPerQueueFamily[i]
					)
				{
					m_QueueFamilyIndices.ComputeFamily = i;
					numberOfUsesPerQueueFamily[i]++;
					break;
				}
		}

		if (m_QueueFamilyIndices.ComputeFamily == indexMax)
			Log<LogLevel::Critical>("Failed to find a valid compute queue family index");
		else if (m_QueueFamilyIndices.TransferFamily == indexMax) // check if transfer queue family is found, otherwise find any even if it shares with others
		{
			Log<LogLevel::Trace>("Failed to find a unique transfer queue family index, any transfer queue will be searched for now");

			for (u32 i = 0; i < static_cast<u32>(queueFamilies.size()); i++)
				if ((queueFamilies[i].queueFlags & vk::QueueFlagBits::eTransfer) == vk::QueueFlagBits::eTransfer &&
					queueFamilies[i].queueCount > numberOfUsesPerQueueFamily[i]
					)
				{
					m_QueueFamilyIndices.TransferFamily = i;
					break;
				}
		}

		// if still not found, use graphics queue family
		if (m_QueueFamilyIndices.TransferFamily == indexMax)
		{
			Log<LogLevel::Trace>("Failed to find a transfer queue bit in any family index, graphics queue family will be used implicitly instead");
			m_QueueFamilyIndices.TransferFamily = m_QueueFamilyIndices.GraphicsFamily;
		}
	}

	void VulkanContext::GetAndStoreGlobalQueueFamilies() const
	{
		std::map<u32, u32> queueFamilyUsageCount;

		auto l_getQueue = [&](const u32 familyIndex) -> vk::raii::Queue {
			if (!queueFamilyUsageCount.contains(familyIndex))
				queueFamilyUsageCount[familyIndex] = 0;

			return VulkanCore::GetConstInstance().GetDevice().getQueue(familyIndex, queueFamilyUsageCount[familyIndex]++);
		};
		
		VulkanCore::GetInstance().SetGraphicsQueue(l_getQueue(m_QueueFamilyIndices.GraphicsFamily));
		VulkanCore::GetInstance().SetComputeQueue(l_getQueue(m_QueueFamilyIndices.ComputeFamily));
		VulkanCore::GetInstance().SetTransferQueue(l_getQueue(m_QueueFamilyIndices.TransferFamily));

		if (queueFamilyUsageCount.contains(m_QueueFamilyIndices.PresentFamily))
			VulkanCore::GetInstance().SetPresentQueue(
				VulkanCore::GetConstInstance().GetDevice().getQueue(m_QueueFamilyIndices.PresentFamily, 0)
			);
		else
			VulkanCore::GetInstance().SetPresentQueue(l_getQueue(m_QueueFamilyIndices.PresentFamily));

		VulkanCore::GetInstance().SetQueueFamilyIndexes(
			m_QueueFamilyIndices.GraphicsFamily,
			m_QueueFamilyIndices.ComputeFamily,
			m_QueueFamilyIndices.TransferFamily,
			m_QueueFamilyIndices.PresentFamily
			);
	}

	void VulkanContext::CreateLogicalDevice()
	{
		std::map<u32, std::pair<u32, std::vector<f32>>> uniqueQueueFamiliesMap;

		for (const auto& index : { m_QueueFamilyIndices.GraphicsFamily, m_QueueFamilyIndices.ComputeFamily, m_QueueFamilyIndices.TransferFamily })
		{
			if (!uniqueQueueFamiliesMap.contains(index))
				uniqueQueueFamiliesMap[index].first = 1;
			else
				uniqueQueueFamiliesMap[index].first++;

			uniqueQueueFamiliesMap[index].second.push_back(1.0f);
		}

		// ensure present family is also included if it is in its own queue family
		if (!uniqueQueueFamiliesMap.contains(m_QueueFamilyIndices.PresentFamily))
		{
			uniqueQueueFamiliesMap[m_QueueFamilyIndices.PresentFamily].first = 1;
			uniqueQueueFamiliesMap[m_QueueFamilyIndices.PresentFamily].second.push_back(1.0f);
		}

		std::vector<vk::DeviceQueueCreateInfo> deviceQueueCreateInfos;
		deviceQueueCreateInfos.reserve(uniqueQueueFamiliesMap.size());

		for (const auto& [familyIndex, familyData] : uniqueQueueFamiliesMap)
			deviceQueueCreateInfos.emplace_back(
				vk::DeviceQueueCreateInfo()
				.setQueueFamilyIndex(familyIndex)
				.setQueueCount(familyData.first)
				.setPQueuePriorities(familyData.second.data())
			);

		auto memoryPriorityFeature = vk::PhysicalDeviceMemoryPriorityFeaturesEXT()
			.setPNext(nullptr)
			.setMemoryPriority(vk::True);

		auto pageableDeviceLocalMemoryFeature = vk::PhysicalDevicePageableDeviceLocalMemoryFeaturesEXT()
			.setPNext(&memoryPriorityFeature)
			.setPageableDeviceLocalMemory(vk::True);

		auto shaderObjectFeature = vk::PhysicalDeviceShaderObjectFeaturesEXT()
			.setPNext(&pageableDeviceLocalMemoryFeature)
			.setShaderObject(vk::True);

		auto shaderExecutionReordering = vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT()
			.setPNext(&shaderObjectFeature)
			.setRayTracingInvocationReorder(VulkanCore::GetConstInstance().HasSERSupport() ? vk::True : vk::False);

		auto rayTracingMaintenance1 = vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR()
			.setPNext(&shaderExecutionReordering)
			.setRayTracingMaintenance1(vk::True);

		auto rayQueryFeature = vk::PhysicalDeviceRayQueryFeaturesKHR()
			.setPNext(&rayTracingMaintenance1)
			.setRayQuery(vk::True);

		auto accelerationStructureFeature = vk::PhysicalDeviceAccelerationStructureFeaturesKHR()
			.setPNext(&rayQueryFeature)
			.setAccelerationStructure(vk::True);

		auto rayTracingPipelineFeature = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR()
			.setPNext(&accelerationStructureFeature)
			.setRayTracingPipeline(vk::True);

		auto swapchainMaintenance1Feature = vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR()
			.setPNext(&rayTracingPipelineFeature)
			.setSwapchainMaintenance1(vk::True);

		auto bufferDeviceAddressFeature = vk::PhysicalDeviceBufferDeviceAddressFeatures()
			.setPNext(&swapchainMaintenance1Feature)
			.setBufferDeviceAddress(vk::True);

		auto maintenance5Feature = vk::PhysicalDeviceMaintenance5Features()
			.setPNext(&bufferDeviceAddressFeature)
			.setMaintenance5(vk::True);

		auto descriptorIndexingFeature = vk::PhysicalDeviceDescriptorIndexingFeatures()
			.setPNext(&maintenance5Feature)
			.setRuntimeDescriptorArray(vk::True)
			.setDescriptorBindingPartiallyBound(vk::True)
			.setDescriptorBindingVariableDescriptorCount(vk::True)
			.setShaderSampledImageArrayNonUniformIndexing(vk::True)
			.setShaderStorageBufferArrayNonUniformIndexing(vk::True)
			.setShaderUniformBufferArrayNonUniformIndexing(vk::True);

		auto shaderDrawParametersFeature = vk::PhysicalDeviceShaderDrawParametersFeatures()
			.setPNext(&descriptorIndexingFeature)
			.setShaderDrawParameters(vk::True);

		auto synchronization2Feature = vk::PhysicalDeviceSynchronization2Features()
			.setPNext(&shaderDrawParametersFeature)
			.setSynchronization2(vk::True);

		auto dynamicRenderingLocalReadFeature = vk::PhysicalDeviceDynamicRenderingLocalReadFeatures()
			.setPNext(&synchronization2Feature)
			.setDynamicRenderingLocalRead(vk::True);

		auto dynamicRenderingFeature = vk::PhysicalDeviceDynamicRenderingFeatures()
			.setPNext(&dynamicRenderingLocalReadFeature)
			.setDynamicRendering(vk::True);

		auto enabledFeatures = vk::PhysicalDeviceFeatures2()
			.setPNext(&dynamicRenderingFeature)
			.setFeatures(vk::PhysicalDeviceFeatures()
				.setSamplerAnisotropy(vk::True)
				.setSampleRateShading(vk::True)
				.setFillModeNonSolid(vk::True)
				.setWideLines(vk::True)
				.setPipelineStatisticsQuery(vk::True)
				.setShaderInt16(vk::True)
				.setShaderInt64(vk::True)
				.setFragmentStoresAndAtomics(vk::True)
				.setShaderImageGatherExtended(vk::True));

		auto deviceCreateInfo = vk::DeviceCreateInfo()
			.setQueueCreateInfos(deviceQueueCreateInfos)
			//.setPEnabledExtensionNames(VulkanCore::GetConstInstance().HasSERSupport() ? g_DeviceExtensionsWithSER : g_DeviceExtensions)
			.setPNext(&enabledFeatures);

		if (VulkanCore::GetConstInstance().HasSERSupport())
			deviceCreateInfo.setPEnabledExtensionNames(g_DeviceExtensionsWithSER);
		else
			deviceCreateInfo.setPEnabledExtensionNames(g_DeviceExtensions);

		VulkanCore::GetInstance().SetDevice(vk::raii::Device(VulkanCore::GetConstInstance().GetPhysicalDev(), deviceCreateInfo));

		m_QueueFamilyIndices.UniqueIndices.reserve(uniqueQueueFamiliesMap.size());
		for (const auto& familyIndex : std::views::keys(uniqueQueueFamiliesMap))
			m_QueueFamilyIndices.UniqueIndices.emplace_back(familyIndex);
	}

	void VulkanContext::CreateSwapchain() const
	{
		if (m_IsMinimized)
			return;

		auto& vkCore = VulkanCore::GetConstInstance();

		const std::vector<vk::SurfaceFormatKHR> surfaceFormats = vkCore.GetPhysicalDev().getSurfaceFormatsKHR(vkCore.GetSurface());
		if (surfaceFormats.empty())
			Log<LogLevel::Critical>("Failed to find any surface formats for the swapchain");

		VulkanCore::GetInstance().SetSwapchainImageFormat((surfaceFormats[0].format == vk::Format::eUndefined) ? vk::Format::eR8G8B8A8Unorm : surfaceFormats[0].format);
		const vk::ColorSpaceKHR colourSpace = surfaceFormats[0].colorSpace;

		Log<LogLevel::Debug>("Vulkan Swapchain Surface Format selected:");
		Log<LogLevel::Debug>(" Format: {}", vk::to_string(vkCore.GetSwapchainImageFormat()));
		Log<LogLevel::Debug>(" Color Space: {}", vk::to_string(colourSpace));

		vk::PhysicalDeviceSurfaceInfo2KHR surfaceInfo(*vkCore.GetSurface());
		vk::SurfaceCapabilities2KHR surfaceCapabilities = vkCore.GetPhysicalDev().getSurfaceCapabilities2KHR(surfaceInfo);

		if (surfaceCapabilities.surfaceCapabilities.currentExtent.width == 0 || surfaceCapabilities.surfaceCapabilities.currentExtent.height == 0)
			Log<LogLevel::Critical>("Failed to get valid surface extents for the swapchain");

		// SDL gives the actual window size, so we can use that directly
		vk::Extent2D swapchainExtent{};
		swapchainExtent.width = glm::max(glm::min(m_WindowProperties.PixelWidth, surfaceCapabilities.surfaceCapabilities.maxImageExtent.width), surfaceCapabilities.surfaceCapabilities.minImageExtent.width);
		swapchainExtent.height = glm::max(glm::min(m_WindowProperties.PixelHeight, surfaceCapabilities.surfaceCapabilities.maxImageExtent.height), surfaceCapabilities.surfaceCapabilities.minImageExtent.height);
		Log<LogLevel::Debug>("Vulkan Swapchain Surface Capabilities:");
		Log<LogLevel::Debug>(" Current Extent: {}x{}", swapchainExtent.width, swapchainExtent.height);

		const auto presentModeCompatibilities = vkCore.GetPhysicalDev().getSurfacePresentModesKHR(vkCore.GetSurface());

		if (presentModeCompatibilities.empty())
			Log<LogLevel::Critical>("Failed to find any present modes for the swapchain");

		Log<LogLevel::Debug>("Vulkan Swapchain Present Modes supported:");
		for (const auto& presentMode : presentModeCompatibilities)
			Log<LogLevel::Debug>(" {}", vk::to_string(presentMode));

		constexpr auto selectedPresentMode = vk::PresentModeKHR::eFifo; // guaranteed to be available
		// prefer mailbox present mode if available
		/*
		for (const auto& presentMode : presentModeCompatibilities)
			if (presentMode == vk::PresentModeKHR::eMailbox)
			{
				selectedPresentMode = vk::PresentModeKHR::eMailbox;
				break;
			}
		*/
		Log<LogLevel::Debug>("Vulkan Swapchain Present Mode selected: {}", vk::to_string(selectedPresentMode));

		const vk::SurfaceTransformFlagBitsKHR preTransform = (surfaceCapabilities.surfaceCapabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
			? vk::SurfaceTransformFlagBitsKHR::eIdentity
			: surfaceCapabilities.surfaceCapabilities.currentTransform;

		std::vector<u32> allUniqueQueuesIndices = vkCore.GetAllUniqueQueuesIndices();

		// Test for use of goto statement
		for (const auto& currentQueue : allUniqueQueuesIndices)
			if (currentQueue == m_QueueFamilyIndices.PresentFamily)
				goto PresentFound;

		allUniqueQueuesIndices.emplace_back(m_QueueFamilyIndices.PresentFamily);

		PresentFound:

		auto swapchainCreateInfo = vk::SwapchainCreateInfoKHR()
			.setSurface(vkCore.GetSurface())
			.setMinImageCount(std::max(surfaceCapabilities.surfaceCapabilities.minImageCount, 2u))
			.setImageFormat(vkCore.GetSwapchainImageFormat())
			.setImageColorSpace(colourSpace)
			.setImageExtent(swapchainExtent)
			.setImageArrayLayers(1)
			.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
			.setImageSharingMode(vk::SharingMode::eConcurrent)
			.setQueueFamilyIndices(allUniqueQueuesIndices)
			.setPreTransform(preTransform)
			.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
			.setPresentMode(selectedPresentMode)
			.setClipped(vk::True);

		if (m_QueueFamilyIndices.UniqueIndices.size() > 1)
			swapchainCreateInfo
				.setImageSharingMode(vk::SharingMode::eConcurrent)
				.setQueueFamilyIndexCount(static_cast<u32>(m_QueueFamilyIndices.UniqueIndices.size()))
				.setPQueueFamilyIndices(m_QueueFamilyIndices.UniqueIndices.data())
				.setOldSwapchain(*vkCore.GetSwapchain());

		VulkanCore::GetInstance().SetSwapchain(vk::raii::SwapchainKHR(vkCore.GetDevice(), swapchainCreateInfo));

		Log<LogLevel::Debug>("Vulkan Swapchain created with {} images", vkCore.GetSwapchainImages().size());

		std::vector<vk::raii::ImageView>& swapchainImageViews = VulkanCore::GetInstance().GetSwapchainImageViews();

		for (const auto& image : vkCore.GetSwapchainImages())
		{
			auto imageViewCreateInfo = vk::ImageViewCreateInfo()
				.setImage(image)
				.setViewType(vk::ImageViewType::e2D)
				.setFormat(vkCore.GetSwapchainImageFormat())
				.setComponents(vk::ComponentMapping()
					.setR(vk::ComponentSwizzle::eIdentity)
					.setG(vk::ComponentSwizzle::eIdentity)
					.setB(vk::ComponentSwizzle::eIdentity)
					.setA(vk::ComponentSwizzle::eIdentity)
				).setSubresourceRange(
					vk::ImageSubresourceRange()
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)
				);
			swapchainImageViews.emplace_back(vkCore.GetDevice(), imageViewCreateInfo);

			Log<LogLevel::Debug>(" Created image view for swapchain image with handle {}", static_cast<void*>(imageViewCreateInfo.image));
		}

		Log<LogLevel::NewLine>();
		VulkanCore::GetInstance().GetEndOfFrameCleanUp().resize(vkCore.GetSwapchainImages().size());
	}

	void VulkanContext::CreateCommandPools() const
	{
		VulkanCore::GetInstance().SetGraphicsCommandPool(
			VulkanCore::GetConstInstance().GetDevice().createCommandPool(
				vk::CommandPoolCreateInfo()
				.setQueueFamilyIndex(m_QueueFamilyIndices.GraphicsFamily)
			)
		);

		VulkanCore::GetInstance().SetComputeCommandPool(
			VulkanCore::GetConstInstance().GetDevice().createCommandPool(
				vk::CommandPoolCreateInfo()
				.setQueueFamilyIndex(m_QueueFamilyIndices.ComputeFamily)
			)
		);

		VulkanCore::GetInstance().SetTransferCommandPool(
			VulkanCore::GetConstInstance().GetDevice().createCommandPool(
				vk::CommandPoolCreateInfo()
				.setQueueFamilyIndex(m_QueueFamilyIndices.TransferFamily)
			)
		);

		VulkanCore::GetInstance().SetDynamicGraphicsCommandPool(
			VulkanCore::GetConstInstance().GetDevice().createCommandPool(
				vk::CommandPoolCreateInfo()
				.setQueueFamilyIndex(m_QueueFamilyIndices.GraphicsFamily)
				.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
			)
		);

		VulkanCore::GetInstance().SetDynamicComputeCommandPool(
			VulkanCore::GetConstInstance().GetDevice().createCommandPool(
				vk::CommandPoolCreateInfo()
				.setQueueFamilyIndex(m_QueueFamilyIndices.ComputeFamily)
				.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
			)
		);

		VulkanCore::GetInstance().SetDynamicTransferCommandPool(
			VulkanCore::GetConstInstance().GetDevice().createCommandPool(
				vk::CommandPoolCreateInfo()
				.setQueueFamilyIndex(m_QueueFamilyIndices.TransferFamily)
				.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
			)
		);

		VulkanCore::GetInstance().SetupSemaphores();
	}

	void VulkanContext::WriteCommandBuffers()
	{
		auto& vkCore = VulkanCore::GetConstInstance();
		m_BeginRenderCmdBuf = vkCore.GetGraphicsCommandBuffer();
		m_EndRenderCmdBuf = vkCore.GetGraphicsCommandBuffer();

		constexpr vk::ClearValue clearColour(vk::ClearColorValue(std::array<f32, 4>{0.0, 0.0, 0.0, 1.0}));

		const vk::Rect2D rect(
			vk::Offset2D(0, 0),
			vk::Extent2D(m_WindowProperties.PixelWidth, m_WindowProperties.PixelHeight)
		);

		for (uSize i = 0; i != vkCore.GetSwapchainImageViews().size(); i++)
		{
			Log<LogLevel::Debug>("PixelSize at record time: {}x{}",
					m_WindowProperties.PixelWidth,
					m_WindowProperties.PixelHeight
				);
			const auto& cmdBuf = m_BeginRenderCmdBuf[i];

			std::array imageMemoryBarrier = { vk::ImageMemoryBarrier2()
					.setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
					.setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
					.setSrcAccessMask(vk::AccessFlagBits2::eNone)
					.setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
					.setOldLayout(vk::ImageLayout::eUndefined)
					.setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
					.setImage(vkCore.GetSwapchainImages()[i])
					.setSubresourceRange(vk::ImageSubresourceRange()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setBaseMipLevel(0)
						.setLevelCount(1)
						.setBaseArrayLayer(0)
						.setLayerCount(1)
					) };

			cmdBuf.begin(vk::CommandBufferBeginInfo());
			cmdBuf.pipelineBarrier2(vk::DependencyInfo()
				.setImageMemoryBarriers(imageMemoryBarrier)
			);

			const std::array renderingAttachmentInfo = {
				vk::RenderingAttachmentInfo()
					.setImageView(vkCore.GetSwapchainImageViews()[i])
					.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
					.setLoadOp(vk::AttachmentLoadOp::eClear)
					.setStoreOp(vk::AttachmentStoreOp::eStore)
					.setClearValue(clearColour)
			};

			cmdBuf.beginRendering(vk::RenderingInfo()
				.setRenderArea(rect)
				.setLayerCount(1)
				.setColorAttachmentCount(1)
				.setColorAttachments(renderingAttachmentInfo)
			);

			cmdBuf.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<f32>(m_WindowProperties.PixelWidth), static_cast<f32>(m_WindowProperties.PixelHeight)));
			cmdBuf.setScissor(0, rect);
			cmdBuf.endRendering();
			cmdBuf.end();
		}

		for (uSize i = 0; i != vkCore.GetSwapchainImageViews().size(); i++)
		{
			const auto& cmdBuf = m_EndRenderCmdBuf[i];
			cmdBuf.begin(vk::CommandBufferBeginInfo());

			const std::array imageMemoryBarrier = {
				vk::ImageMemoryBarrier2()
					.setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
					.setDstStageMask(vk::PipelineStageFlagBits2::eNone)
					.setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
					.setDstAccessMask(vk::AccessFlagBits2::eNone)
					.setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
					.setNewLayout(vk::ImageLayout::ePresentSrcKHR)
					.setImage(vkCore.GetSwapchainImages()[i])
					.setSubresourceRange(vk::ImageSubresourceRange()
						.setAspectMask(vk::ImageAspectFlagBits::eColor)
						.setBaseMipLevel(0)
						.setLevelCount(1)
						.setBaseArrayLayer(0)
						.setLayerCount(1)
					)
			};

			cmdBuf.pipelineBarrier2(vk::DependencyInfo()
				.setImageMemoryBarriers(imageMemoryBarrier)
			);
			cmdBuf.end();
		}
	}

	void VulkanContext::CreateVulkanMemoryAllocator()
	{
		auto& vkCore = VulkanCore::GetInstance();

		vma::AllocatorCreateFlags allocatorFlags;
		allocatorFlags |= vma::AllocatorCreateFlagBits::eKhrMaintenance5;
		allocatorFlags |= vma::AllocatorCreateFlagBits::eKhrDedicatedAllocation;
		allocatorFlags |= vma::AllocatorCreateFlagBits::eKhrBindMemory2;
		allocatorFlags |= vma::AllocatorCreateFlagBits::eExtMemoryPriority;
		allocatorFlags |= vma::AllocatorCreateFlagBits::eExtMemoryBudget;
		allocatorFlags |= vma::AllocatorCreateFlagBits::eBufferDeviceAddress;

		const auto allocatorCreateInfo = vma::AllocatorCreateInfo()
			.setPhysicalDevice(vkCore.GetPhysicalDev())
			.setVulkanApiVersion(g_VulkanVersion)
			.setFlags(allocatorFlags)
			.setPreferredLargeHeapBlockSize(256ull * 1024 * 1024); // 256mb

		vkCore.SetVulkanMemoryAllocator(vma::raii::Allocator(vkCore.GetVKInstance(), vkCore.GetDevice(), allocatorCreateInfo));
	}

	void VulkanContext::RecreateSwapchain()
	{
		auto& vkCore = VulkanCore::GetInstance();
		WaitForIdle();
		vkCore.DestroySemaphores();
		vkCore.GetSwapchainImages().clear();
		vkCore.GetSwapchainImageViews().clear();
		CreateSwapchain();
		vkCore.SetupSemaphores();
	}

	void VulkanContext::RewriteCommandBuffers()
	{
		WaitForIdle();

		m_BeginRenderCmdBuf.clear();
		m_EndRenderCmdBuf.clear();

		WriteCommandBuffers();
	}

	void VulkanContext::Minimize()
	{
		auto& vkCore = VulkanCore::GetInstance();
		WaitForIdle();
		vkCore.GetSwapchainImages().clear();
		vkCore.GetSwapchainImageViews().clear();
		m_IsMinimized = true;
	}

	void VulkanContext::Restore()
	{
		if (!m_IsMinimized)
			return;

		m_IsMinimized = false;
		CreateSwapchain();
		RewriteCommandBuffers();
	}
	
	void VulkanContext::ImGuiInit()
	{
		const auto& vkCore = VulkanCore::GetConstInstance();

		std::array poolSizes = {
			vk::DescriptorPoolSize()
				.setType(vk::DescriptorType::eCombinedImageSampler)
				.setDescriptorCount(1000)
		};

		const auto poolInfo = vk::DescriptorPoolCreateInfo()
			.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
			.setMaxSets(1000)
			.setPoolSizes(poolSizes);

		VulkanCore::GetInstance().SetImGuiDescriptorPool(vkCore.GetDevice().createDescriptorPool(poolInfo));

		ImGui_ImplVulkan_InitInfo initInfo = {};
		initInfo.ApiVersion = g_VulkanVersion;
		initInfo.Instance = *vkCore.GetVKInstance();
		initInfo.PhysicalDevice = *vkCore.GetPhysicalDev();
		initInfo.Device = *vkCore.GetDevice();
		initInfo.QueueFamily = m_QueueFamilyIndices.GraphicsFamily;
		initInfo.Queue = vkCore.GetGraphicsQueue();
		initInfo.PipelineCache = nullptr;
		initInfo.DescriptorPool = *vkCore.GetImGuiDescriptorPool();
		initInfo.UseDynamicRendering = true;
		initInfo.MinAllocationSize = 1024 * 1024;
		initInfo.MinImageCount = static_cast<u32>(vkCore.GetSwapchainImages().size());
		initInfo.ImageCount = static_cast<u32>(vkCore.GetSwapchainImages().size());
		initInfo.Allocator = nullptr;
		initInfo.CheckVkResultFn = [](VkResult err)
		{
			if (err != VK_SUCCESS)
				Log<LogLevel::Error>("ImGui Vulkan Error: {}", vk::to_string(static_cast<vk::Result>(err)));
		};

		const auto pipelineInfo = vk::PipelineRenderingCreateInfo()
			.setColorAttachmentCount(1)
			.setColorAttachmentFormats(vkCore.GetSwapchainImageFormat())
			.setDepthAttachmentFormat(vk::Format::eUndefined)
			.setStencilAttachmentFormat(vk::Format::eUndefined);

		initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineInfo;

		ImGui_ImplVulkan_Init(&initInfo);
	}

	void VulkanContext::ImGuiShutdown()
	{
		ImGui_ImplVulkan_Shutdown();
	}

	void VulkanContext::ImGuiNewFrame()
	{
		ImGui_ImplVulkan_NewFrame();
	}
}
