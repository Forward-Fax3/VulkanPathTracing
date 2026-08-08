//
// Created by forwardfax3 on 15/04/2026.
//

#pragma once
#include "Core.hpp"
#include "UniformBuffer.hpp"

#include <tiny_gltf_v3.h>
#include <memory>


namespace OWC
{
    struct GPUGLTFData
    {
        u64 positionsOffset = 0;
        u64 indicesOffset = 0;
        u64 normalsOffset = 0;
        u64 coloursOffset = 0;
        u32 materialIndex = 0 ;
        u32 is16BitIndices = 0; // bool
        u32 _[2] = { 0, 0 }; // pad to 48 bytes
    };

    struct GPUMaterialData
    {
        Vec3p baseColourFactor = Vec3p(1.0f);
        f32 metallicFactor = 1.0f;
        f32 roughnessFactor = 1.0f;
        u32 baseColourTextureIndex = ~0u;
        u32 metallicRoughnessTextureIndex = ~0u;
        u32 normalTextureIndex = ~0u;
        u32 occlusionTextureIndex = ~0u;
        u32 emissiveTextureIndex = ~0u;
        f32 alphaCutoff = 0.5f;
        u32 _[1] = { 0 }; // pad to 48 bytes
    };

    struct GPULightData
    {
        OWC_FORCE_INLINE GPULightData() = default;
        OWC_FORCE_INLINE GPULightData(const Vec3p& pos, const u32 t, const Vec3p& dir, const f32 inten, const Vec3p& col, const f32 r, const f32 innerConeAngle, const f32 outerConeAngle)
            : position(pos), type(t), direction(dir), intensity(inten), Colour(col), range(r), innerConeAngle(innerConeAngle), outerConeAngle(outerConeAngle) {}
        OWC_FORCE_INLINE GPULightData(const Vec3p& pos, const u32 t, const Vec3p& dir, const f32 inten, const Vec3p& col, const f32 r, const float rad)
            : position(pos), type(t), direction(dir), intensity(inten), Colour(col), range(r), radius(rad) {}
        OWC_FORCE_INLINE GPULightData(const Vec3p& pos, const u32 t, const Vec3p& dir, const f32 inten, const Vec3p& col, const f32 r)
            : position(pos), type(t), direction(dir), intensity(inten), Colour(col), range(r) {}

        Vec3p position = Vec3p(0.0f);
        u32 type = ~0u;
        Vec3p direction = Vec3p(0.0f);
        f32 intensity = 1.0f;
        Vec3p Colour = Vec3p(1.0f);
        f32 range = 0.0f;
        f32 innerConeAngle = 0.0f;
        f32 outerConeAngle = 0.0f;
        float radius = 0.0f; // 0.0f for point light radius > 0.0f for sphere
        u32 _[1] = { 0 }; // pad to 64 bytes
    };

    struct AttributeData
    {
        u32 offset = ~0;
        u32 count = ~0;
        u32 byteStride = ~0;
        u32 bufferIndex = ~0;
        bool hasData = false;
    };

    class SceneMesh
    {
    public:
        SceneMesh() = default;
        virtual ~SceneMesh() = default;
        SceneMesh(SceneMesh&) = delete;
        SceneMesh& operator=(SceneMesh&) = delete;
        SceneMesh(SceneMesh&&) noexcept = delete;
        SceneMesh& operator=(SceneMesh&&) noexcept = delete;

        static std::unique_ptr<SceneMesh> CreateFromGLTFModelWithMeshIndex(const tg3_model& gltfMesh, i32 meshIndex, u32 customInstancesIndex, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUGLTFData>& GPUData);
    };
} // OWC
