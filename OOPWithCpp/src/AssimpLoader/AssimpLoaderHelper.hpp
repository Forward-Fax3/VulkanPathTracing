#include "assimp/scene.h"
#include "assimp/material.h"
#include "assimp/GltfMaterial.h"


namespace OWC
{
    enum class AlphaMode : uint8_t
    {
        Opaque,
        Mask,
        Blend
    };

    struct MaterialAlpha {
        AlphaMode mode = AlphaMode::Opaque;
        float cutoff = 0.5f;
    };

    MaterialAlpha GetMaterialAlpha(const aiMaterial* mat);
}
