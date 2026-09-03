#pragma once
#include "RendererCore.hpp"
#include <vulkan/vulkan_raii.hpp>


namespace OWC::Graphics
{
    [[nodiscard]] vk::Format ConvertFormatToVulkanFormat(Format format);
}
