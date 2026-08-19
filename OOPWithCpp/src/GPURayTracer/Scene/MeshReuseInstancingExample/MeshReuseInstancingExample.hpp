//
// Created by Cat God on 11/08/2026.
//

#pragma once
#include "Core.hpp"
#include "BaseGPUScene.hpp"
#include "GLTFLoader.hpp"


namespace OWC
{
    class MeshReuseInstancingExample : public BaseGPUScene
    {
    public:
        MeshReuseInstancingExample();
        ~MeshReuseInstancingExample() override = default;

        [[nodiscard]] OWC_FORCE_INLINE std::shared_ptr<BaseTLAS>& GetTLAS() override { return m_MeshReuseInstancingLoader.GetTLAS(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceMegaBufferPtr() const override { return m_MeshReuseInstancingLoader.GetDeviceMegaBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceGeometryBufferPtr() const override { return m_MeshReuseInstancingLoader.GetDeviceGeometryBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetDeviceMaterialBufferPtr() const override { return m_MeshReuseInstancingLoader.GetDeviceMaterialBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE uSize GetLightBufferPtr() const override { return m_MeshReuseInstancingLoader.GetLightBufferPtr(); }
        [[nodiscard]] OWC_FORCE_INLINE u32 GetNumberOfLights() const override { return m_MeshReuseInstancingLoader.GetNumberOfLights(); }

        [[nodiscard]] OWC_FORCE_INLINE const std::shared_ptr<Graphics::TextureArraySampler>& GetTextureArraySampler() const override { return m_MeshReuseInstancingLoader.GetTextureArraySampler(); }
        [[nodiscard]] OWC_FORCE_INLINE const std::shared_ptr<Graphics::TextureArray>& GetColourTextureArray() const override { return m_MeshReuseInstancingLoader.GetColourTextureArray(); }
        [[nodiscard]] OWC_FORCE_INLINE const std::shared_ptr<Graphics::TextureArray>& GetNormalTextureArray() const override { return m_MeshReuseInstancingLoader.GetNormalTextureArray(); }
        [[nodiscard]] OWC_FORCE_INLINE const std::shared_ptr<Graphics::TextureArray>& GetMetallicRoughnessTextureArray() const override { return m_MeshReuseInstancingLoader.GetMetallicRoughnessTextureArray(); }

        [[nodiscard]] OWC_FORCE_INLINE bool GetNeedScreenRefresh() const override { return m_MeshReuseInstancingLoader.GetNeedScreenRefresh(); }

    private:
        GLTFLoader m_MeshReuseInstancingLoader;
    };
} // OWC
