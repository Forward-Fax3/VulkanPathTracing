#pragma once
#include "Core.hpp"

namespace OWC::Graphics
{
    enum class Format : u32
    {
        Undefined = 0,

        R8_UNorm,
        R8_SNorm,
        R8_Uint,
        R8_Sint,

        R8G8_UNorm,
        R8G8_SNorm,
        R8G8_Uint,
        R8G8_Sint,

        R8G8B8A8_UNorm,
        R8G8B8A8_SNorm,
        R8G8B8A8_Uint,
        R8G8B8A8_Sint,
        R8G8B8A8_Srgb,

        R16_UNorm,
        R16_SNorm,
        R16_Uint,
        R16_Sint,
        R16_SFloat,

        R16G16_UNorm,
        R16G16_SNorm,
        R16G16_Uint,
        R16G16_Sint,
        R16G16_SFloat,

        R16G16B16A16_UNorm,
        R16G16B16A16_SNorm,
        R16G16B16A16_Uint,
        R16G16B16A16_Sint,
        R16G16B16A16_SFloat,

        R32_Uint,
        R32_Sint,
        R32_SFloat,

        R32G32_Uint,
        R32G32_Sint,
        R32G32_SFloat,

        R32G32B32_Uint,
        R32G32B32_Sint,
        R32G32B32_SFloat,

        R32G32B32A32_Uint,
        R32G32B32A32_Sint,
        R32G32B32A32_SFloat,

        R10G10B10A2_UNorm,
        R10G10B10A2_Uint,
        R11G11B10_UFloat,
        R9G9B9E5_UFloat,
    };

    enum class ImageLayout : u8
    {
        Undefined = 0,
        ColorAttachmentOptimal = 1,
        DepthStencilAttachmentOptimal = 2,
        ShaderReadOnlyOptimal = 3,
        TransferSrcOptimal = 4,
        TransferDstOptimal = 5,
        General = 6,
    };

    enum class AccessMask : u16
    {
        None = 0,
        IndirectCommandRead = 1 << 0,
        IndexRead = 1 << 1,
        VertexAttributeRead = 1 << 2,
        UniformRead = 1 << 3,
        InputAttachmentRead = 1 << 4,
        ShaderRead = 1 << 5,
        ShaderWrite = 1 << 6,
        ColorAttachmentRead = 1 << 7,
        ColorAttachmentWrite = 1 << 8,
        DepthStencilAttachmentRead = 1 << 9,
        DepthStencilAttachmentWrite = 1 << 10,
        TransferRead = 1 << 11,
        TransferWrite = 1 << 12,
    };

    OWC_FORCE_INLINE AccessMask operator|(const AccessMask lhs, const AccessMask rhs) noexcept
    {
        return static_cast<AccessMask>(std::to_underlying(lhs) | std::to_underlying(rhs));
    }

    OWC_FORCE_INLINE AccessMask operator&(const AccessMask lhs, const AccessMask rhs) noexcept
    {
        return static_cast<AccessMask>(std::to_underlying(lhs) & std::to_underlying(rhs));
    }

    enum class StageMask : u32
    {
        None = 0,
        VertexShader = 1 << 0,
        TessellationControlShader = 1 << 1,
        TessellationEvaluationShader = 1 << 2,
        GeometryShader = 1 << 3,
        FragmentShader = 1 << 4,
        ComputeShader = 1 << 5,
        RayTracing = 1 << 6,
        TopOfPipe = 1 << 7,
        BottomOfPipe = 1 << 8,
        Transfer = 1 << 9,
    };

    OWC_FORCE_INLINE StageMask operator|(const StageMask lhs, const StageMask rhs) noexcept
    {
        return static_cast<StageMask>(std::to_underlying(lhs) | std::to_underlying(rhs));
    }

    OWC_FORCE_INLINE StageMask operator&(const StageMask lhs, const StageMask rhs) noexcept
    {
        return static_cast<StageMask>(std::to_underlying(lhs) & std::to_underlying(rhs));
    }
}
