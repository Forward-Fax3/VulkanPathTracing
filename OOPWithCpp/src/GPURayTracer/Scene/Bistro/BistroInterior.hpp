//
// Created by Cat God on 24/08/2026.
//

#pragma once
#include "BaseGPUScene.hpp"
#include "AssimpLoader.hpp"

namespace OWC
{
    class BistroInterior : public BaseGPUScene
    {
    public:
        BistroInterior();
        ~BistroInterior() override = default;

        [[nodiscard]] OWC_FORCE_INLINE std::shared_ptr<BaseTLAS>& GetTLAS() override { return m_WarehouseLoader.GetTLAS(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceMegaBufferPtr() const override { return m_WarehouseLoader.GetDeviceMegaBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceGeometryBufferPtr() const override { return m_WarehouseLoader.GetDeviceGeometryBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceMaterialBufferPtr() const override { return m_WarehouseLoader.GetDeviceMaterialBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetLightBufferPtr() const override { return m_WarehouseLoader.GetLightBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE u32 GetNumberOfLights() const override { return m_WarehouseLoader.GetNumberOfLights(); }

        //[[nodiscard]] OWC_FORCE_INLINE const std::shared_ptr<Graphics::TextureArraySampler>& GetTextureArraySampler() const override { return m_WarehouseLoader.GetTextureArraySampler(); }
        //[[nodiscard]] OWC_FORCE_INLINE const std::shared_ptr<Graphics::TextureArray>& GetColourTextureArray() const override { return m_WarehouseLoader.GetColourTextureArray(); }
        //[[nodiscard]] OWC_FORCE_INLINE const std::shared_ptr<Graphics::TextureArray>& GetNormalTextureArray() const override { return m_WarehouseLoader.GetNormalTextureArray(); }
        //[[nodiscard]] OWC_FORCE_INLINE const std::shared_ptr<Graphics::TextureArray>& GetMetallicRoughnessTextureArray() const override { return m_WarehouseLoader.GetMetallicRoughnessTextureArray(); }

        [[nodiscard]] OWC_FORCE_INLINE bool GetNeedScreenRefresh() const override { return m_WarehouseLoader.GetNeedScreenRefresh(); }

    private:
        AssimpLoader m_WarehouseLoader;
    };
} // OWC
