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
        case Format::R10G10B10A2_Uint:
            return vk::Format::eA2R10G10B10UintPack32;
        default:
            return vk::Format::eUndefined;
        }
    }

	vk::AccessFlags2 GetVulkanAccessMask(const AccessMask accessMask)
	{
		vk::AccessFlags2 accessFlags;

		if (testBitMask(accessMask, AccessMask::IndirectCommandRead))
			accessFlags |= vk::AccessFlagBits2::eIndirectCommandRead;
		if (testBitMask(accessMask, AccessMask::IndexRead))
			accessFlags |= vk::AccessFlagBits2::eIndexRead;
		if (testBitMask(accessMask, AccessMask::VertexAttributeRead))
			accessFlags |= vk::AccessFlagBits2::eVertexAttributeRead;
		if (testBitMask(accessMask, AccessMask::UniformRead))
			accessFlags |= vk::AccessFlagBits2::eUniformRead;
		if (testBitMask(accessMask, AccessMask::InputAttachmentRead))
			accessFlags |= vk::AccessFlagBits2::eInputAttachmentRead;
		if (testBitMask(accessMask, AccessMask::ShaderRead))
			accessFlags |= vk::AccessFlagBits2::eShaderRead;
		if (testBitMask(accessMask, AccessMask::ShaderWrite))
			accessFlags |= vk::AccessFlagBits2::eShaderWrite;
		if (testBitMask(accessMask, AccessMask::ColorAttachmentRead))
			accessFlags |= vk::AccessFlagBits2::eColorAttachmentRead;
		if (testBitMask(accessMask, AccessMask::ColorAttachmentWrite))
			accessFlags |= vk::AccessFlagBits2::eColorAttachmentWrite;
		if (testBitMask(accessMask, AccessMask::DepthStencilAttachmentRead))
			accessFlags |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
		if (testBitMask(accessMask, AccessMask::DepthStencilAttachmentWrite))
			accessFlags |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
		if (testBitMask(accessMask, AccessMask::TransferRead))
			accessFlags |= vk::AccessFlagBits2::eTransferRead;
		if (testBitMask(accessMask, AccessMask::TransferWrite))
			accessFlags |= vk::AccessFlagBits2::eTransferWrite;

		return accessFlags;
	}

	vk::PipelineStageFlags2 GetVulkanPipelineStageMask(const StageMask stageMask)
	{
		vk::PipelineStageFlags2 result;

		if (testBitMask(stageMask, StageMask::TopOfPipe))
			result |= vk::PipelineStageFlagBits2::eTopOfPipe;
		if (testBitMask(stageMask, StageMask::BottomOfPipe))
			result |= vk::PipelineStageFlagBits2::eBottomOfPipe;
		if (testBitMask(stageMask, StageMask::FragmentShader))
			result |= vk::PipelineStageFlagBits2::eFragmentShader;
		if (testBitMask(stageMask, StageMask::VertexShader))
			result |= vk::PipelineStageFlagBits2::eVertexShader;
		if (testBitMask(stageMask, StageMask::GeometryShader))
			result |= vk::PipelineStageFlagBits2::eGeometryShader;
		if (testBitMask(stageMask, StageMask::ComputeShader))
			result |= vk::PipelineStageFlagBits2::eComputeShader;
		if (testBitMask(stageMask, StageMask::RayTracing))
			result |= vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
		if (testBitMask(stageMask, StageMask::Transfer))
			result |= vk::PipelineStageFlagBits2::eTransfer;

		return result;
	}

	vk::ImageLayout GetVulkanImageLayout(const ImageLayout layout)
	{
		switch (layout)
		{
		case ImageLayout::Undefined:
			return vk::ImageLayout::eUndefined;
		case ImageLayout::ColorAttachmentOptimal:
			return vk::ImageLayout::eColorAttachmentOptimal;
		case ImageLayout::DepthStencilAttachmentOptimal:
			return vk::ImageLayout::eDepthStencilAttachmentOptimal;
		case ImageLayout::ShaderReadOnlyOptimal:
			return vk::ImageLayout::eShaderReadOnlyOptimal;
		case ImageLayout::TransferSrcOptimal:
			return vk::ImageLayout::eTransferSrcOptimal;
		case ImageLayout::TransferDstOptimal:
			return vk::ImageLayout::eTransferDstOptimal;
		case ImageLayout::General:
			return vk::ImageLayout::eGeneral;
		default:
			Log<LogLevel::Error>("Invalid ImageLayout specified when converting to Vulkan image layout");
			return vk::ImageLayout::eUndefined;
		}
	}
}
