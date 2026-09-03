//
// Created by forwardfax3 on 15/04/2026.
//

#pragma once
#include "Core.hpp"
#include "GPUBuffers.hpp"

#include <tiny_gltf_v3.h>
#include <assimp/scene.h>
#include <memory>


namespace OWC
{
    struct GPUData
    {
        u64 indicesOffset = 0;
        u64 vertexOffset = 0;
        u32 materialIndex = 0;
        u32 is32BitIndices = 0;
        u32 _[2] = { 0, 0 }; // pad to 32 bytes
    };

    struct GPUMaterialData
    {
        Vec4p baseColourFactor = Vec4p(1.0f);
        f32 metallicFactor = 1.0f;
        f32 roughnessFactor = 1.0f;
        u32 baseColourTextureIndex = ~0u;
        u32 baseColourTextureCoords = ~0u;
        u32 metallicRoughnessTextureIndex = ~0u;
        u32 metallicRoughnessTextureCoords = ~0u;
        Vec3p emissiveFactor = Vec3p(0.0f);
        u32 normalTextureIndex = ~0u;
        u32 normalTextureCoords = ~0u;
        u32 emissiveTextureIndex = ~0u;
        u32 emissiveTextureCoords = ~0u;
        f32 alphaCutoff = 0.5f;
        //u32 _[2] = { 0, 0 }; // pad to 80 bytes
    };

    struct alignas(64) VertexData
    {
        Vec3p normal;
        Vec4p tangent;
        Vec4p colour;
        Vec2p UV[2];
        u32 pad[1]; //pad to 64 bytes
    };

    struct GPULightData
    {
        OWC_FORCE_INLINE GPULightData() = default;
        OWC_FORCE_INLINE GPULightData(const Vec3p& pos, const u32 t, const Vec3p& dir, const f32 inten, const Vec3p& col, const f32 r, const f32 innerConeAngle, const f32 outerConeAngle)
            : position(pos), type(t), direction(dir), intensity(inten), colour(col), range(r), innerConeAngle(innerConeAngle), outerConeAngle(outerConeAngle) {}
        OWC_FORCE_INLINE GPULightData(const Vec3p& pos, const u32 t, const Vec3p& dir, const f32 inten, const Vec3p& col, const f32 r, const float rad)
            : position(pos), type(t), direction(dir), intensity(inten), colour(col), range(r), radius(rad) {}
        OWC_FORCE_INLINE GPULightData(const Vec3p& pos, const u32 t, const Vec3p& dir, const f32 inten, const Vec3p& col, const f32 r)
            : position(pos), type(t), direction(dir), intensity(inten), colour(col), range(r) {}

        Vec3p position = Vec3p(0.0f);
        u32 type = ~0u;
        Vec3p direction = Vec3p(0.0f);
        f32 intensity = 1.0f;
        Vec3p colour = Vec3p(1.0f);
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

        static std::unique_ptr<SceneMesh> CreateFromGLTFModelWithMeshIndex(const tg3_model& gltfMesh, i32 meshIndex, u32 customInstancesIndex, const std::unordered_map<uSize, uSize>& gltfBufferPosToIndicesOffset, uSize verticesBufferOffset, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUData>& GPUData, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData);
        static std::unique_ptr<SceneMesh> CreateFromAssimpSceneWithMeshIndex(const aiScene& scene, const std::unordered_map<aiFace*, u64>& assimpFacePtrToGPUOffset, u32 meshIndex, uSize vertexDataOffset, u32 customInstancesIndex, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUData>& GPUData, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData);
    };
} // OWC
