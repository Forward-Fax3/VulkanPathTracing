//
// Created by forwardfax3 on 17/04/2026.
//

#pragma once
#include <memory>
#include <map>

#include "SceneMesh.hpp"
#include "BaseTLAS.hpp"
#include "UniformBuffer.hpp"

#include "GLTFLayer.hpp"


namespace OWC
{
    class GLTFLoader
    {
    public:
        GLTFLoader() = delete;
        GLTFLoader(std::string_view gltfFilePath, std::string_view layerName);
        ~GLTFLoader();
        GLTFLoader(GLTFLoader&) = delete;
        GLTFLoader& operator=(GLTFLoader&) = delete;
        GLTFLoader(GLTFLoader&&) noexcept = delete;
        GLTFLoader& operator=(GLTFLoader&&) noexcept = delete;

        [[nodiscard]] std::shared_ptr<BaseTLAS>& GetTLAS() { return m_TLAS; }
        [[nodiscard]] uSize GetDeviceMegaBufferPtr() const { return m_GPUBuffer->GetDeviceBufferPtr(); }
        [[nodiscard]] uSize GetDeviceGeometryBufferPtr() const { return m_GPUBuffer->GetDeviceBufferPtr() + m_GeometryBufferOffset; }
        [[nodiscard]] uSize GetDeviceMaterialBufferPtr() const { return m_GPUBuffer->GetDeviceBufferPtr() + m_MaterialBufferOffset; }
        [[nodiscard]] uSize GetLightBufferPtr() const { return m_LightBuffer ? m_LightBuffer->GetDeviceBufferPtr() : 0; }
        [[nodiscard]] u32 GetNumberOfLights() const { return m_NumberOfLights; }

        [[nodiscard]] bool GetNeedScreenRefresh() const { return m_Layer->GetNeedScreenRefresh(); }

    private:
        void IterateThroughNodes(u32 nodeIndex, Mat4 parentTransform, u32& customInstancesIndex, std::vector<GPULightData>& lightData, std::map<i32, std::unique_ptr<SceneMesh>>& meshes, std::vector<std::pair<Mat4, i32>>& meshIndexes);

    private:
        tg3_model m_Model = {};
        std::shared_ptr<BaseTLAS> m_TLAS;
        std::shared_ptr<Graphics::GeneralBuffer> m_GPUBuffer;
        std::shared_ptr<Graphics::GeneralBuffer> m_LightBuffer;
        std::shared_ptr<GLTFLayer> m_Layer;
        std::vector<GPUGLTFData> m_GPUData;
        uSize m_GeometryBufferSize = 0;
        uSize m_MaterialBufferSize = 0;
        uSize m_GeometryBufferOffset = 0;
        uSize m_MaterialBufferOffset = 0;
        u32 m_NumberOfLights = 0;
    };
}// OWC
