#pragma once
#include "Core.hpp"

namespace OWC::Graphics
{
    enum class Format : u32
    { // TODO actually add some more formats not just what ImGui needs
        Undefined = 0,
        R32G32_SFloat,
        R32G32B32A32_SFloat,
        R8G8B8A8_UNorm,
        R8G8B8A8_Srgb
    };
}
