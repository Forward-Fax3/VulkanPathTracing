//
// Created by Cat God on 11/08/2026.
//

#pragma once
#include "Core.hpp"
#include "BaseGPUScene.hpp"
#include "GLTFLoader.hpp"


namespace OWC
{
    class EXTMeshGPUInstancingExample : public BaseGPUScene
    {
    public:
        EXTMeshGPUInstancingExample();
        ~EXTMeshGPUInstancingExample() override = default;

        [[nodiscard]] OWC_FORCE_INLINE std::shared_ptr<BaseTLAS>& GetTLAS() override { return m_EXTMeshGPUInstancingLoader.GetTLAS(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceMegaBufferPtr() const override { return m_EXTMeshGPUInstancingLoader.GetDeviceMegaBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceGeometryBufferPtr() const override { return m_EXTMeshGPUInstancingLoader.GetDeviceGeometryBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceMaterialBufferPtr() const override { return m_EXTMeshGPUInstancingLoader.GetDeviceMaterialBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetLightBufferPtr() const override { return m_EXTMeshGPUInstancingLoader.GetLightBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE u32 GetNumberOfLights() const override { return m_EXTMeshGPUInstancingLoader.GetNumberOfLights(); }

        [[nodiscard]] OWC_FORCE_INLINE bool GetNeedScreenRefresh() const override { return m_EXTMeshGPUInstancingLoader.GetNeedScreenRefresh(); }

    private:
        GLTFLoader m_EXTMeshGPUInstancingLoader;
    };
} // OWC
