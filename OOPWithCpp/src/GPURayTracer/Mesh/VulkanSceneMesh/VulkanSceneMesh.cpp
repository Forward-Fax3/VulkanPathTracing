//
// Created by forwardfax3 on 21/04/2026.
//

#include "VulkanSceneMesh.hpp"
#include "VulkanUniformBuffer.hpp"
#include "Core.hpp"
#include "Log.hpp"
#include "VulkanCore.hpp"
#include "GLTFLoaderHelper.hpp"
#include "AssimpLoaderHelper.hpp"

#include <vulkan/vulkan.hpp>

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <cstring>

static constexpr OWC::i32 DEFAULT_TRIANGLES = -1l;

namespace OWC
{
    VulkanSceneMesh::VulkanSceneMesh(const tg3_model& model, i32 meshIndex, u32 customInstancesIndex, const std::unordered_map<uSize, uSize>& gltfBufferPosToIndicesOffset, uSize verticesPosOffset, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUData>& GPUData, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData)
        : m_CustomInstanceIndex(customInstancesIndex)
    {
        const tg3_mesh& mesh = model.meshes[meshIndex];

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

            const AttributeData positionData = extractAttribute(prim, "POSITION", model);

            auto& accessor = model.accessors[prim.indices];
            const AttributeData indexData = extractAttribute(accessor, model);

            const u32 fallbackMaxVertex = positionData.count > 0 ? positionData.count - 1 : 0;
            const u32 maxIndex = computeMaxIndex(model, accessor, fallbackMaxVertex);

            const u32 positionStride = positionData.byteStride;

            m_Triangles.emplace_back(
                vk::Format::eR32G32B32Sfloat,
                vk::DeviceOrHostAddressConstKHR().setDeviceAddress(vulkanBuffer->GetBufferDeviceAddress() + verticesPosOffset + verticesData.size() * sizeof(Vec3p)),
                sizeof(Vec3p),
                positionData.count,
                accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT ? vk::IndexType::eUint16 : vk::IndexType::eUint32,
                vk::DeviceOrHostAddressConstKHR().setDeviceAddress(vulkanBuffer->GetBufferDeviceAddress() + gltfBufferPosToIndicesOffset.at(indexData.offset))
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
            const auto texCoords0Data = extractAttribute(prim, "TEXCOORD_0", model);
            const auto texCoords1Data = extractAttribute(prim, "TEXCOORD_1", model);

            std::vector<Vec3> computedNormals;
            if (!normalData.hasData)
            {
                //computedNormals.assign(maxIndex, Vec3(0.0f));
                computedNormals.assign(positionData.count, Vec3(0.0f));

                // Walk triangles using the index buffer to accumulate face normals
                const auto* indexBufferData = model.buffers[indexData.bufferIndex].data.data + indexData.offset;

                auto getIndex = [&](const u32 k) -> u32
                {
                    if (accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT)
                        return std::bit_cast<const u16*>(indexBufferData)[k];
                    else
                        return std::bit_cast<const u32*>(indexBufferData)[k];
                };

                auto getPos = [&](const u32 vtxIdx) -> Vec3
                {
                    return glm::make_vec3(std::bit_cast<float*>(model.buffers[positionData.bufferIndex].data.data + positionData.offset + vtxIdx * positionStride));
                };

                for (u32 t = 0; t < indexData.count / 3; t++)
                {
                    const u32 i0 = getIndex(t * 3 + 0);
                    const u32 i1 = getIndex(t * 3 + 1);
                    const u32 i2 = getIndex(t * 3 + 2);

                    const Vec3 p0 = getPos(i0);
                    const Vec3 p1 = getPos(i1);
                    const Vec3 p2 = getPos(i2);

                    // Unnormalized face normal — magnitude is proportional to triangle area,
                    // which naturally weights larger triangles more when accumulated (area-weighted normals)
                    const Vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);

                    computedNormals[i0] += faceNormal;
                    computedNormals[i1] += faceNormal;
                    computedNormals[i2] += faceNormal;
                }

                for (auto& n : computedNormals)
                {
                    const float len = glm::length(n);
                    n = len > 1e-8f ? (n / len) : Vec3(0.0f, 1.0f, 0.0f); // fallback for degenerate cases
                }
            }

            std::vector<glm::vec4> computedTangents; // xyz = tangent, w = handedness
            if (!tangentData.hasData && texCoords0Data.hasData)
            {
                //std::vector tan1(maxIndex, Vec3(0.0f));
                //std::vector tan2(maxIndex, Vec3(0.0f));
                std::vector tan1(positionData.count, Vec3(0.0f));
                std::vector tan2(positionData.count, Vec3(0.0f));

                const auto* indexBufferData = std::bit_cast<const u8*>(model.buffers[indexData.bufferIndex].data.data + indexData.offset);

                auto getIndex = [&](const u32 k) -> u32
                {
                    if (accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT)
                        return std::bit_cast<const u16*>(indexBufferData)[k];
                    else
                        return std::bit_cast<const u32*>(indexBufferData)[k];
                };

                auto getPos = [&](const u32 vtxIdx) -> Vec3
                {
                    return glm::make_vec3(std::bit_cast<float*>(model.buffers[positionData.bufferIndex].data.data
                        + positionData.offset + vtxIdx * positionStride));
                };

                const u32 uvStride = texCoords0Data.byteStride > 0 ? texCoords0Data.byteStride : static_cast<u32>(sizeof(float) * 2);
                auto getUV = [&](const u32 vtxIdx) -> Vec2
                {
                    return glm::make_vec2(std::bit_cast<float*>(model.buffers[texCoords0Data.bufferIndex].data.data + texCoords0Data.offset + vtxIdx * uvStride));
                };

                for (u32 t = 0; t < indexData.count / 3; t++)
                {
                    const u32 i0 = getIndex(t * 3 + 0);
                    const u32 i1 = getIndex(t * 3 + 1);
                    const u32 i2 = getIndex(t * 3 + 2);

                    const Vec3 p0 = getPos(i0), p1 = getPos(i1), p2 = getPos(i2);
                    const Vec2 uv0 = getUV(i0), uv1 = getUV(i1), uv2 = getUV(i2);

                    const Vec3 edge1 = p1 - p0;
                    const Vec3 edge2 = p2 - p0;
                    const Vec2 deltaUV1 = uv1 - uv0;
                    const Vec2 deltaUV2 = uv2 - uv0;

                    const float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
                    // Skip degenerate UV triangles (zero UV area) rather than dividing by ~0
                    if (std::abs(denom) < 1e-12f)
                        continue;

                    const float r = 1.0f / denom;

                    const Vec3 tangent   = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * r;
                    const Vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * r;

                    tan1[i0] += tangent; tan1[i1] += tangent; tan1[i2] += tangent;
                    tan2[i0] += bitangent; tan2[i1] += bitangent; tan2[i2] += bitangent;
                }

                //computedTangents.assign(maxIndex, Vec4(0.0f));
                computedTangents.assign(positionData.count, Vec4(0.0f));
                //for (u32 v = 0; v < maxIndex; v++)
                for (u32 v = 0; v < positionData.count; v++)
                {
                    const Vec3 n = normalData.hasData ?
                        Vec3(glm::make_vec3(std::bit_cast<float*>(model.buffers[normalData.bufferIndex].data.data + normalData.offset + v * normalData.byteStride)))
                        : computedNormals[v];

                    // Gram-Schmidt orthogonalize tangent against the normal
                    Vec3 t = tan1[v] - n * glm::dot(n, tan1[v]);
                    const float len = glm::length(t);
                    t = (len > 1e-8f) ? (t / len) : Vec3(1.0f, 0.0f, 0.0f); // fallback for degenerate/unused vertices

                    // Handedness: +1 or -1, tells you whether bitangent = cross(n,t) or -cross(n,t)
                    const float handedness = (glm::dot(glm::cross(n, t), tan2[v]) < 0.0f) ? -1.0f : 1.0f;

                    computedTangents[v] = Vec4(t, handedness);
                }
            }

            GPUData.emplace_back(
                gltfBufferPosToIndicesOffset.at(indexData.offset),
                 alignUp(verticesPosOffset + verticesPos.capacity() * sizeof(Vec3p), alignof(VertexData)) + verticesData.size() * sizeof(VertexData),
                prim.material,
                accessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT ? static_cast<u32>(true) : static_cast<u32>(false)
            );

            for (u32 j = 0; j < positionData.count; j++)
            {
                const Vec3p pos = glm::make_vec3(std::bit_cast<float*>(model.buffers[positionData.bufferIndex].data.data + positionData.offset + j * positionStride));

                Vec3 norm;
                if (normalData.hasData)
                    norm = glm::make_vec3(std::bit_cast<float*>(model.buffers[normalData.bufferIndex].data.data + normalData.offset + j * normalData.byteStride));
                else
                    norm = computedNormals[j];

                Vec4 tang;
                if (tangentData.hasData)
                    tang = glm::make_vec4(std::bit_cast<float*>(model.buffers[tangentData.bufferIndex].data.data + tangentData.offset + j * tangentData.byteStride));
                else if (texCoords0Data.hasData)
                    tang = glm::vec4(computedTangents[j]);
                else
                    tang = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

                Vec4p colour = colourData.hasData ?
                    Vec4p(glm::make_vec4(std::bit_cast<float*>(model.buffers[colourData.bufferIndex].data.data + colourData.offset + j * colourData.byteStride)))
                    : Vec4p(1.0f);

                Vec2p UV0 = texCoords0Data.hasData ?
                    Vec2p(glm::make_vec2(std::bit_cast<float*>(model.buffers[texCoords0Data.bufferIndex].data.data + texCoords0Data.offset + j * texCoords0Data.byteStride)))
                    : Vec2p(0.0f);
                Vec2p UV1 = texCoords1Data.hasData ?
                    Vec2p(glm::make_vec2(std::bit_cast<float*>(model.buffers[texCoords1Data.bufferIndex].data.data + texCoords1Data.offset + j * texCoords1Data.byteStride)))
                    : Vec2p(0.0f);

                verticesPos.emplace_back(pos);
                verticesData.emplace_back(VertexData(norm, tang, colour, { UV0, UV1 }));
            }
        }

        Log<LogLevel::Trace>("Mesh {} prepped {} geometries from {} primitives", meshIndex, m_Geometries.size(), mesh.primitives_count);
    }

    VulkanSceneMesh::VulkanSceneMesh(const aiScene& scene, const std::unordered_map<aiFace*, u64>& assimpFacePtrToGPUOffset, u32 meshIndex, uSize vertexPosOffset, const u32 customInstancesIndex, const std::shared_ptr<Graphics::GeneralBuffer>& GPUBuffer, std::vector<GPUData>& GPUData, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData)
        : m_CustomInstanceIndex(customInstancesIndex)
    {
        const aiMesh& mesh = *scene.mMeshes[meshIndex];

        const auto vulkanBuffer = std::dynamic_pointer_cast<Graphics::VulkanGeneralBuffer>(GPUBuffer);
        if (!vulkanBuffer)
        {
            // This should not happen but just in case
            Log<LogLevel::Error>("Provided GPU buffer is not a VulkanGeneralBuffer, which is required for VulkanSceneMesh. Skipping mesh {}.", meshIndex);
            return;
        }

        GPUData.emplace_back(
            assimpFacePtrToGPUOffset.at(mesh.mFaces),
            alignUp(vertexPosOffset + verticesPos.capacity() * sizeof(Vec3p), alignof(VertexData)) + verticesData.size() * sizeof(VertexData),
            mesh.mMaterialIndex,
            static_cast<u32>(true)
        );

        m_Geometries.reserve(1);
        m_PrimitiveCount.reserve(1);
        m_BuildRanges.reserve(1);
        m_Triangles.reserve(1);

        m_Triangles.emplace_back(
            vk::Format::eR32G32B32Sfloat,
            vk::DeviceOrHostAddressConstKHR().setDeviceAddress((vulkanBuffer->GetBufferDeviceAddress() + vertexPosOffset + verticesPos.size() * sizeof(Vec3p))),
            sizeof(Vec3p),
            static_cast<u32>(mesh.mNumVertices),
            vk::IndexType::eUint32,
            vk::DeviceOrHostAddressConstKHR().setDeviceAddress(vulkanBuffer->GetBufferDeviceAddress() + assimpFacePtrToGPUOffset.at(mesh.mFaces))
        );

        bool isOpaque = true;
        if (mesh.mMaterialIndex != std::numeric_limits<u32>::max())
        {
            const auto [mode, cutoff] = GetMaterialAlpha(scene.mMaterials[mesh.mMaterialIndex]);
            isOpaque = mode == AlphaMode::Opaque;
        }

        m_Geometries.emplace_back(
            vk::GeometryTypeKHR::eTriangles,
            vk::AccelerationStructureGeometryDataKHR().setTriangles(m_Triangles.back()),
            (isOpaque) ? vk::GeometryFlagBitsKHR::eOpaque : vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation
        );

        m_PrimitiveCount.emplace_back(mesh.mNumFaces);
        m_BuildRanges.emplace_back(mesh.mNumFaces, 0, 0, 0);

        for (u32 i = 0; i < mesh.mNumVertices; i++)
        {
            // compiler magic should fix this up

            Vec3p pos {
                mesh.mVertices[i].x,
                mesh.mVertices[i].y,
                mesh.mVertices[i].z
            };
            Vec3 normAligned {
                mesh.mNormals[i].x,
                mesh.mNormals[i].y,
                mesh.mNormals[i].z
            };
            Vec3 tangXYZ {
                mesh.mTangents[i].x,
                mesh.mTangents[i].y,
                mesh.mTangents[i].z
            };
            Vec3 bitang {
                mesh.mBitangents[i].x,
                mesh.mBitangents[i].y,
                mesh.mBitangents[i].z
            };

            float handedness = glm::dot(glm::cross(normAligned, tangXYZ), bitang) < 0.0f ? -1.0f : 1.0f;
            Vec3p norm = normAligned;
            Vec4p tang { tangXYZ, handedness };

            Vec4p colour = mesh.HasVertexColors(0) ? Vec4p {
                mesh.mColors[0][i].r,
                mesh.mColors[0][i].g,
                mesh.mColors[0][i].b,
                mesh.mColors[0][i].a
            } : Vec4p(1.0f);
            Vec2p uv0 = mesh.HasTextureCoords(0) ? Vec2p {
                mesh.mTextureCoords[0][i].x,
                mesh.mTextureCoords[0][i].y
            } : Vec2p(0.0f);
            Vec2p uv1 = mesh.HasTextureCoords(1) ? Vec2p {
                mesh.mTextureCoords[1][i].x,
                mesh.mTextureCoords[1][i].y
            } : Vec2p(0.0f);

            VertexData vertexData(norm, tang, colour, { uv0, uv1 });

            verticesPos.emplace_back(pos);
            verticesData.emplace_back(vertexData);
        }
    }
} // OWC