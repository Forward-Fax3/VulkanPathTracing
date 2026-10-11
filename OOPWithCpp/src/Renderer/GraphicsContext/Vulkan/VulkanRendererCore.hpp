#pragma once
#include "RendererCore.hpp"
#include <vulkan/vulkan_raii.hpp>


namespace OWC::Graphics
{
    [[nodiscard]] vk::Format ConvertFormatToVulkanFormat(Format format);

    [[nodiscard]] vk::AccessFlags2 GetVulkanAccessMask(AccessMask accessMask);
    [[nodiscard]] vk::PipelineStageFlags2 GetVulkanPipelineStageMask(StageMask stageMask);
    [[nodiscard]] vk::ImageLayout GetVulkanImageLayout(ImageLayout layout);
}
