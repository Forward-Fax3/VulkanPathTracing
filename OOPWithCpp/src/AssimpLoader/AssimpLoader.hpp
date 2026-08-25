//
// Created by forwardfax3 on 17/04/2026.
//

#pragma once
#include <memory>
#include <map>

#include "SceneMesh.hpp"
#include "BaseTLAS.hpp"
#include "UniformBuffer.hpp"

#include "assimp/scene.h"

#include "AssimpLayer.hpp"


namespace OWC
{
    class AssimpLoader
    {
    public:
        AssimpLoader() = delete;
        AssimpLoader(std::string_view gltfFilePath, std::string_view layerName);
        ~AssimpLoader();
        AssimpLoader(AssimpLoader&) = delete;
        AssimpLoader& operator=(AssimpLoader&) = delete;
        AssimpLoader(AssimpLoader&&) noexcept = delete;
        AssimpLoader& operator=(AssimpLoader&&) noexcept = delete;

        [[nodiscard]] std::shared_ptr<BaseTLAS>& GetTLAS() { return m_TLAS; }
        [[nodiscard]] uSize GetDeviceMegaBufferPtr() const { return m_GPUBuffer->GetDeviceBufferPtr(); }
        [[nodiscard]] uSize GetDeviceGeometryBufferPtr() const { return m_GPUBuffer->GetDeviceBufferPtr() + m_GeometryBufferOffset; }
        [[nodiscard]] uSize GetDeviceMaterialBufferPtr() const { return m_GPUBuffer->GetDeviceBufferPtr() + m_MaterialBufferOffset; }
        [[nodiscard]] uSize GetLightBufferPtr() const { return m_GPUBuffer->GetDeviceBufferPtr() + m_LightBufferOffset; }
        [[nodiscard]] u32 GetNumberOfLights() const { return m_NumberOfLights; }

        [[nodiscard]] const std::shared_ptr<Graphics::TextureArraySampler>& GetTextureArraySampler() const { return m_TextureArraySampler; }
        [[nodiscard]] const std::shared_ptr<Graphics::TextureArray>& GetColourTextureArray() const { return m_ColourTextureArray; }
        [[nodiscard]] const std::shared_ptr<Graphics::TextureArray>& GetNormalTextureArray() const { return m_NormalTextureArray; }
        [[nodiscard]] const std::shared_ptr<Graphics::TextureArray>& GetMetallicRoughnessTextureArray() const { return m_MetallicRoughnessTextureArray; }
        [[nodiscard]] const std::shared_ptr<Graphics::TextureArray>& GetEmissiveTextureArray() const { return m_EmissiveTextureArray; }

        [[nodiscard]] bool GetNeedScreenRefresh() const { return m_Layer->GetNeedScreenRefresh(); }

    private:
        void IterateThroughNodes(const aiNode* node_ptr, const aiScene& scene, const std::unordered_map<aiFace*, u64>& assimpFacePtrToGPUOffset, uSize vertexDataOffset, Mat4 parentTransform, u32& customInstancesIndex, std::map<u32, std::unique_ptr<SceneMesh>>& meshes, std::vector<std::pair<Mat4, u32>>& meshIndexes, std::vector<GPUData>& GPUDatas, std::vector<Vec3p>& verticesPos, std::vector<VertexData>& verticesData);

    private:
        std::shared_ptr<BaseTLAS> m_TLAS;
        std::shared_ptr<Graphics::GeneralBuffer> m_GPUBuffer;
        std::shared_ptr<AssimpLayer> m_Layer;
        std::shared_ptr<Graphics::TextureArraySampler> m_TextureArraySampler;
        std::shared_ptr<Graphics::TextureArray> m_ColourTextureArray;
        std::shared_ptr<Graphics::TextureArray> m_NormalTextureArray;
        std::shared_ptr<Graphics::TextureArray> m_MetallicRoughnessTextureArray;
        std::shared_ptr<Graphics::TextureArray> m_EmissiveTextureArray;
        uSize m_LightBufferOffset = 0;
    uSize m_GeometryBufferOffset = 0;
        uSize m_MaterialBufferOffset = 0;
        u32 m_NumberOfLights = 0;
    };
}// OWC
