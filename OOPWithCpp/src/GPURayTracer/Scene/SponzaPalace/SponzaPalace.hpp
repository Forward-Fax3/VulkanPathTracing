//
// Created by Cat God on 11/08/2026.
//

#pragma once
#include "BaseGPUScene.hpp"
#include "GLTFLoader.hpp"


namespace OWC
{
    class SponzaPalace : public BaseGPUScene
    {
    public:
        SponzaPalace();
        ~SponzaPalace() override = default;

        [[nodiscard]] OWC_FORCE_INLINE std::shared_ptr<BaseTLAS>& GetTLAS() override { return m_SponzaPalaceLoader.GetTLAS(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceMegaBufferPtr() const override { return m_SponzaPalaceLoader.GetDeviceMegaBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceGeometryBufferPtr() const override { return m_SponzaPalaceLoader.GetDeviceGeometryBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceMaterialBufferPtr() const override { return m_SponzaPalaceLoader.GetDeviceMaterialBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetLightBufferPtr() const override { return m_SponzaPalaceLoader.GetLightBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE u32 GetNumberOfLights() const override { return m_SponzaPalaceLoader.GetNumberOfLights(); }

        [[nodiscard]] OWC_FORCE_INLINE bool GetNeedScreenRefresh() const override { return m_SponzaPalaceLoader.GetNeedScreenRefresh(); }

    private:
        GLTFLoader m_SponzaPalaceLoader;
    };
} // OWC
