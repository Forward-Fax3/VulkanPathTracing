//
// Created by forwardfax3 on 16/04/2026.
//

#pragma once
#include "BaseTLAS.hpp"


namespace OWC
{
    class BaseGPUScene
    {
    public:
        BaseGPUScene();
        virtual ~BaseGPUScene() = default;
        BaseGPUScene(BaseGPUScene&) = delete;
        BaseGPUScene& operator=(BaseGPUScene&) = delete;
        BaseGPUScene(BaseGPUScene&&) noexcept = delete;
        BaseGPUScene& operator=(BaseGPUScene&&) noexcept = delete;

        [[nodiscard]] virtual std::shared_ptr<BaseTLAS>& GetTLAS() = 0;
        [[nodiscard]] virtual uSize GetDeviceMegaBufferPtr() const = 0;
        [[nodiscard]] virtual uSize GetDeviceGeometryBufferPtr() const = 0;
        [[nodiscard]] virtual uSize GetDeviceMaterialBufferPtr() const = 0;
        [[nodiscard]] virtual uSize GetLightBufferPtr() const = 0;
        [[nodiscard]] virtual u32 GetNumberOfLights() const = 0;

        [[nodiscard]] virtual const std::shared_ptr<Graphics::TextureArraySampler>& GetTextureArraySampler() const { return m_EmptyTextureArraySampler; }
        [[nodiscard]] virtual const std::shared_ptr<Graphics::TextureArray>& GetColourTextureArray() const { return m_EmptyTextureArray; }
        [[nodiscard]] virtual const std::shared_ptr<Graphics::TextureArray>& GetNormalTextureArray() const { return m_EmptyTextureArray; }
        [[nodiscard]] virtual const std::shared_ptr<Graphics::TextureArray>& GetMetallicRoughnessTextureArray() const { return m_EmptyTextureArray; }
        [[nodiscard]] virtual const std::shared_ptr<Graphics::TextureArray>& GetEmissiveTextureArray() const { return m_EmptyTextureArray; }

        [[nodiscard]] virtual bool GetNeedScreenRefresh() const { return false; }

    private:
        std::shared_ptr<Graphics::TextureArraySampler> m_EmptyTextureArraySampler;
        std::shared_ptr<Graphics::TextureArray> m_EmptyTextureArray;

        static std::weak_ptr<Graphics::TextureArraySampler> s_EmptyTextureArraySampler;
        static std::weak_ptr<Graphics::TextureArray> s_EmptyTextureArray;
    };
}
