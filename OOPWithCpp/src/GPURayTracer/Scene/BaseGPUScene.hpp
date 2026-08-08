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
        BaseGPUScene() = default;
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

        [[nodiscard]] virtual bool GetNeedScreenRefresh() const { return false; }
    };
}
