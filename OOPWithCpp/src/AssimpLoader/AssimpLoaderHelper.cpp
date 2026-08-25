#include "AssimpLoaderHelper.hpp"


namespace OWC
{
    MaterialAlpha GetMaterialAlpha(const aiMaterial* mat)
    {
        MaterialAlpha a;

        aiString mode;
        if (mat->Get(AI_MATKEY_GLTF_ALPHAMODE, mode) == AI_SUCCESS)
        {
            // Source was glTF - trust it directly
            if (mode == aiString("MASK"))
                a.mode = AlphaMode::Mask;
            else if (mode == aiString("BLEND"))
                a.mode = AlphaMode::Blend;
            else
                a.mode = AlphaMode::Opaque;

            float cutoff = 0.5f;
            if (mat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS)
                a.cutoff = cutoff;
        }
        else
        {
            // Non-glTF source (FBX/OBJ/etc) - infer it
            float opacity = 1.0f;
            mat->Get(AI_MATKEY_OPACITY, opacity);

            aiString texPath;
            bool hasDiffuseAlpha = mat->GetTextureCount(aiTextureType_DIFFUSE) > 0 &&
                mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS;
            // Note: you can't know from the material alone whether that texture's
            // alpha channel is actually meaningful - you may need to inspect the
            // loaded image itself (channel count) if opacity==1.0 and this bool is true.

            int blendFunc = aiBlendMode_Default;
            mat->Get(AI_MATKEY_BLEND_FUNC, blendFunc);

            if (opacity < 1.0f || blendFunc == aiBlendMode_Additive)
                a.mode = AlphaMode::Blend;
            else if (hasDiffuseAlpha)
                a.mode = AlphaMode::Mask; // heuristic guess, cutoff stays default 0.5
            else
                a.mode = AlphaMode::Opaque;
        }

        return a;
    }
}
