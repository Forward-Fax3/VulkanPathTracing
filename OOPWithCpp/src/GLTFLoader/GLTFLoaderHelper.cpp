#include "GLTFLoaderHelper.hpp"

namespace OWC
{
    u32 computeMaxIndex(const tg3_model& model, const tg3_accessor& indexAccessor, u32 fallbackMaxVertex)
    {
        if (indexAccessor.buffer_view < 0)
            return fallbackMaxVertex;

        const tg3_buffer_view& bufferView = model.buffer_views[indexAccessor.buffer_view];
        if (bufferView.buffer < 0)
            return fallbackMaxVertex;

        const tg3_buffer& buffer = model.buffers[bufferView.buffer];
        if (!buffer.data.data || buffer.data.count == 0)
        {
            return fallbackMaxVertex;
        }

        const uint64_t baseOffset = bufferView.byte_offset + indexAccessor.byte_offset;
        uint32_t componentSize = 0;
        switch (indexAccessor.component_type)
        {
            case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
                componentSize = sizeof(uint8_t);
                break;
            case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
                componentSize = sizeof(uint16_t);
                break;
            case TG3_COMPONENT_TYPE_UNSIGNED_INT:
                componentSize = sizeof(uint32_t);
                break;
            default:
                return fallbackMaxVertex;
        }

        const uint64_t stride = bufferView.byte_stride ? bufferView.byte_stride : componentSize;
        if (indexAccessor.count == 0)
            return fallbackMaxVertex;

        if (const uint64_t lastOffset = baseOffset + stride * (indexAccessor.count - 1);
            baseOffset >= buffer.data.count || lastOffset + componentSize > buffer.data.count)
            return fallbackMaxVertex;

        u32 maxIndex = 0;
        for (uint64_t i = 0; i < indexAccessor.count; i++)
        {
            const uint8_t* ptr = buffer.data.data + baseOffset + stride * i;
            u32 value = 0;
            switch (indexAccessor.component_type)
            {
                case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
                {
                    uint8_t v = 0;
                    std::memcpy(&v, ptr, sizeof(uint8_t));
                    value = static_cast<u32>(v);
                    break;
                }
                case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
                {
                    uint16_t v = 0;
                    std::memcpy(&v, ptr, sizeof(uint16_t));
                    value = static_cast<u32>(v);
                    break;
                }
                case TG3_COMPONENT_TYPE_UNSIGNED_INT:
                {
                    uint32_t v = 0;
                    std::memcpy(&v, ptr, sizeof(uint32_t));
                    value = v;
                    break;
                }
                default:
                    break;
            }
            maxIndex = std::max(maxIndex, value);
        }

        return maxIndex;
    }

    AttributeData extractAttribute(const tg3_primitive& prim, const std::string& attributeName, const tg3_model& model)
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

    AttributeData extractAttribute(const tg3_accessor& acc, const tg3_model& model)
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

    std::optional<tg3_value> doesGLTFExtensionExist(const tg3_node& node, const std::string& extensionName)
    {
        for (u32 i = 0; i < node.ext.extensions_count; i++)
            if (const auto& [name, value] = node.ext.extensions[i];
                tg3_str_equals_cstr(name, extensionName.c_str()))
                return value;

        return std::nullopt;
    }
}
