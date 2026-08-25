#pragma once
#include "Core.hpp"

#include <tiny_gltf_v3.h>

#include "SceneMesh.hpp"


namespace OWC
{
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
    }

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
    }

    u32 computeMaxIndex(const tg3_model& model, const tg3_accessor& indexAccessor, u32 fallbackMaxVertex);

    AttributeData extractAttribute(const tg3_primitive& prim, const std::string& attributeName, const tg3_model& model);

    AttributeData extractAttribute(const tg3_accessor& acc, const tg3_model& model);

    std::optional<tg3_value> doesGLTFExtensionExist(const tg3_node& node, const std::string& extensionName);
}