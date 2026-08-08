//
// Created by forwardfax3 on 17/04/2026.
//

#pragma once
#include <memory>
#include <map>

#include "Scene/BaseGPUScene.hpp"
#include "SceneMesh.hpp"
#include "BaseTLAS.hpp"
#include "UniformBuffer.hpp"

#include "SponzaPalaceLayer.hpp"


namespace OWC
{
    class SponzaPalace : public BaseGPUScene
    {
    public:
        SponzaPalace();
        ~SponzaPalace() override;
        SponzaPalace(SponzaPalace&) = delete;
        SponzaPalace& operator=(SponzaPalace&) = delete;
        SponzaPalace(SponzaPalace&&) noexcept = delete;
        SponzaPalace& operator=(SponzaPalace&&) noexcept = delete;

        [[nodiscard]] std::shared_ptr<BaseTLAS>& GetTLAS() override { return m_TLAS; }
        [[nodiscard]] uSize GetDeviceMegaBufferPtr() const override { return m_GPUBuffer->GetDeviceBufferPtr(); }
        [[nodiscard]] uSize GetDeviceGeometryBufferPtr() const override { return m_GPUBuffer->GetDeviceBufferPtr() + m_GeometryBufferOffset; }
        [[nodiscard]] uSize GetDeviceMaterialBufferPtr() const override { return m_GPUBuffer->GetDeviceBufferPtr() + m_MaterialBufferOffset; }
        [[nodiscard]] uSize GetLightBufferPtr() const override { return m_LightBuffer->GetDeviceBufferPtr(); }
        [[nodiscard]] u32 GetNumberOfLights() const override { return m_NumberOfLights; }

        [[nodiscard]] bool GetNeedScreenRefresh() const override { return m_SponzaPalaceLayer->GetNeedScreenRefresh(); }

    private:
        void IterateThroughNodes(u32 nodeIndex, Mat4 parentTransform, u32& customInstancesIndex, std::vector<GPULightData>& lightData, std::map<i32, std::unique_ptr<SceneMesh>>& meshes, std::vector<std::pair<Mat4, i32>>& meshIndexes);

    private:
        tg3_model m_Model = {};
        std::shared_ptr<BaseTLAS> m_TLAS;
        std::shared_ptr<Graphics::GeneralBuffer> m_GPUBuffer;
        std::shared_ptr<Graphics::GeneralBuffer> m_LightBuffer;
        std::shared_ptr<SponzaPalaceLayer> m_SponzaPalaceLayer;
        std::vector<GPUGLTFData> m_GPUData;
        uSize m_GeometryBufferSize = 0;
        uSize m_MaterialBufferSize = 0;
        uSize m_GeometryBufferOffset = 0;
        uSize m_MaterialBufferOffset = 0;
        u32 m_NumberOfLights = 0;
    };
}// OWC
