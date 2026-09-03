#include "Core.hpp"
#include "Log.hpp"
#include "VulkanRendererCore.hpp"


namespace OWC::Graphics
{
    [[nodiscard]] vk::Format ConvertFormatToVulkanFormat(const Format format)
    {
        switch (format)
        {
        case Format::R32G32_SFloat:
            return vk::Format::eR32G32Sfloat;
        case Format::R32G32B32A32_SFloat:
            return vk::Format::eR32G32B32A32Sfloat;
        case Format::R8G8B8A8_UNorm:
            return vk::Format::eR8G8B8A8Unorm;
        case Format::R8G8B8A8_Srgb:
            return vk::Format::eR8G8B8A8Srgb;
        default:
            return vk::Format::eUndefined;
        }
    }
}
