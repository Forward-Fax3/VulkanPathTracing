//
// Created by forwardfax3 on 21/04/2026.
//

#include "VulkanSceneMesh.hpp"
#include "VulkanUniformBuffer.hpp"
#include "Core.hpp"
#include "Log.hpp"
#include "VulkanCore.hpp"

#include <vulkan/vulkan.hpp>

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <cstring>

static constexpr OWC::u32 DEFAULT_TRIANGLES = 0xFFFFFFFFul;

namespace OWC
{
    VulkanSceneMesh::VulkanSceneMesh(const tg3_model& model, const i32 meshIndex, u32 customInstancesIndex, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUGLTFData>& GPUData)
        : m_CustomInstanceIndex(customInstancesIndex)
    {
        const tg3_mesh& mesh = model.meshes[meshIndex];

        auto computeMaxIndex = [&model](const tg3_accessor& indexAccessor, const u32 fallbackMaxVertex) -> u32
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
        };

        const auto vulkanBuffer = std::dynamic_pointer_cast<Graphics::VulkanGeneralBuffer>(GPUBuffer);
        if (!vulkanBuffer)
        {
            // This should not happen but just in case
            Log<LogLevel::Error>("Provided GPU buffer is not a VulkanGeneralBuffer, which is required for VulkanSceneMesh. Skipping mesh {}.", meshIndex);
            return;
        }

        m_Geometries.reserve(mesh.primitives_count);
        m_PrimitiveCount.reserve(mesh.primitives_count);
        m_BuildRanges.reserve(mesh.primitives_count);
        m_Triangles.reserve(mesh.primitives_count);

        for (u32 i = 0; i < mesh.primitives_count; i++)
        {
            const tg3_primitive& prim = mesh.primitives[i];

            if (prim.mode != TG3_MODE_TRIANGLES && prim.mode != DEFAULT_TRIANGLES)
            {
                Log<LogLevel::Error>("Mesh primitive {} in mesh {} is not in triangle mode, which is required for ray tracing. Skipping this primitive.", i, meshIndex);
                continue;
            }
            if (prim.indices == -1)
            {
                Log<LogLevel::Warn>("Mesh primitive {} in mesh {} does not have an index accessor, which is required for ray tracing. Skipping this primitive.", i, meshIndex);
                continue;
            }

            AttributeData positionData = extractAttribute(prim, "POSITION", model);

            auto& accessor = model.accessors[prim.indices];
            AttributeData indexData = extractAttribute(accessor, model);

            const u32 fallbackMaxVertex = positionData.count > 0 ? positionData.count - 1 : 0;
            const u32 maxIndex = computeMaxIndex(accessor, fallbackMaxVertex);

            const u32 positionStride = positionData.byteStride > 0 ? positionData.byteStride : static_cast<u32>(sizeof(float) * 3);

            m_Triangles.emplace_back(
                vk::Format::eR32G32B32Sfloat,
                vk::DeviceOrHostAddressConstKHR().setDeviceAddress(vulkanBuffer->GetBufferDeviceAddress() + positionData.offset),
                positionStride,
                maxIndex,
                accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT ? vk::IndexType::eUint16 : vk::IndexType::eUint32,
                vk::DeviceOrHostAddressConstKHR().setDeviceAddress(vulkanBuffer->GetBufferDeviceAddress() + indexData.offset)
            );

            bool isOpaque = false;
            if (prim.material != -1)
                if (const auto& material = model.materials[prim.material]; tg3_str_equals_cstr(material.alpha_mode, "OPAQUE"))
                    isOpaque = true;

            m_Geometries.emplace_back(
                vk::GeometryTypeKHR::eTriangles,
                vk::AccelerationStructureGeometryDataKHR()
                    .setTriangles(m_Triangles.back()),
                (isOpaque) ? vk::GeometryFlagBitsKHR::eOpaque : vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation
            );

            m_PrimitiveCount.emplace_back(indexData.count / 3);
            m_BuildRanges.emplace_back(indexData.count / 3, 0, 0, 0);

            const auto normalData = extractAttribute(prim, "NORMAL", model);
            const auto colourData = extractAttribute(prim, "COLOR_0", model);
            assert((normalData.byteStride == 12 || normalData.hasData == false) && (colourData.byteStride == 16 || colourData.hasData == false));

            const auto tangentData = extractAttribute(prim, "TANGENT", model);
            const auto texCoordsData = extractAttribute(prim, "TEXCOORD_0", model);

            GPUData.emplace_back(
                positionData.offset,
                indexData.offset,
                normalData.hasData ? static_cast<u64>(normalData.offset) : ~0ULL,
                colourData.hasData ? static_cast<u64>(colourData.offset) : ~0ULL,
                tangentData.hasData ? static_cast<u64>(tangentData.offset) : ~0ULL,
                texCoordsData.hasData ? static_cast<u64>(texCoordsData.offset) : ~0ULL,
                prim.material,
                accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT ? static_cast<u32>(true) : static_cast<u32>(false)
            );
        }

        Log<LogLevel::Trace>("Mesh {} prepped {} geometries from {} primitives", meshIndex, m_Geometries.size(), mesh.primitives_count);
    }

    /*const AttributeData& VulkanSceneMesh::GetAttributeData(const std::string& attributeName) const
    {
        static AttributeData emptyData{};
        return emptyData;
    }*/
} // OWC