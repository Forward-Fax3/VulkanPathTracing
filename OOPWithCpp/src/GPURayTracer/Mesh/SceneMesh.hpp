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
        u64 tangentOffset = 0;
        u64 texCoordsOffset = 0;
        u32 materialIndex = 0;
        u32 is16BitIndices = 0; // bool
        u32 _[2] = { 0, 0 }; // pad to 64 bytes
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
        u32 _[2] = { 0, 0 }; // pad to 80 bytes
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

    OWC_FORCE_INLINE u32 GetElementByteSize(const tg3_accessor& acc)
    {
        switch (acc.type)
        {
        case TG3_TYPE_SCALAR:
            return 1;
        case TG3_TYPE_VEC2:
            return 2;
        case TG3_TYPE_VEC3:
            return 3;
        case TG3_TYPE_VEC4:
            return 4;
        default:
            return 0;
        }
    };

    OWC_FORCE_INLINE u32 componentTypeToSize(const tg3_accessor& acc)
    {
        switch (acc.component_type)
        {
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
            return sizeof(uint8_t);
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
            return sizeof(uint16_t);
        case TG3_COMPONENT_TYPE_UNSIGNED_INT:
            return sizeof(uint32_t);
        case TG3_COMPONENT_TYPE_FLOAT:
            return sizeof(float);
        default:
            return 0;
        }
    };

    inline AttributeData extractAttribute(const tg3_primitive& prim, const std::string& attributeName, const tg3_model& model)
    {
        for (u32 i = 0; i < prim.attributes_count; i++)
        {
            if (const auto& [key, accessorIndex] = prim.attributes[i]; tg3_str_equals_cstr(key, attributeName.c_str()))
            {
                const tg3_accessor& acc = model.accessors[accessorIndex];
                const tg3_buffer_view& bv = model.buffer_views[acc.buffer_view];

                return {
                    .offset = static_cast<u32>(bv.byte_offset + acc.byte_offset),
                    .count = static_cast<u32>(acc.count),
                    .byteStride = (bv.byte_stride ? static_cast<u32>(bv.byte_stride) : componentTypeToSize(acc) * GetElementByteSize(acc)),
                    .bufferIndex = static_cast<u32>(bv.buffer),
                    .hasData = true
                };
            }
        }
        return {};
    }

    inline AttributeData extractAttribute(const tg3_accessor& acc, const tg3_model& model)
    {
        const tg3_buffer_view& bv = model.buffer_views[acc.buffer_view];

        return {
            .offset = static_cast<u32>(bv.byte_offset + acc.byte_offset),
            .count = static_cast<u32>(acc.count),
            .byteStride = (bv.byte_stride ? static_cast<u32>(bv.byte_stride) : componentTypeToSize(acc) * GetElementByteSize(acc)),
            .bufferIndex = static_cast<u32>(bv.buffer),
            .hasData = true
        };
    }

    inline std::optional<tg3_value> doesGLTFExtensionExist(const tg3_node& node, const std::string& extensionName)
    {
        for (u32 i = 0; i < node.ext.extensions_count; i++)
            if (const auto& [name, value] = node.ext.extensions[i];
                tg3_str_equals_cstr(name, extensionName.c_str()))
                return value;

        return std::nullopt;
    }

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
