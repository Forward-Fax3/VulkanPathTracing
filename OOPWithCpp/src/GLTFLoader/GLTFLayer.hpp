//
// Created by forwardfax3 on 07/08/2026.
//

#pragma once
#include "Core.hpp"
#include "Layer.hpp"

#include "SceneMesh.hpp"
#include "UniformBuffer.hpp"


namespace OWC
{
    class GLTFLayer : public Layer
    {
    public:
        GLTFLayer() = delete;
        GLTFLayer(const std::shared_ptr<Graphics::GeneralBuffer>& lightBuffer, std::vector<GPULightData>&& lightData, const std::string_view& layerName);
        ~GLTFLayer() override = default;

        void ImGuiRender() override;

        [[nodiscard]] bool GetNeedScreenRefresh() const { return m_ScreenNeedRefresh; }

    private:
        std::shared_ptr<Graphics::GeneralBuffer> m_LightBuffer;
        std::vector<GPULightData> m_LightData;
        std::string layerName;
        bool m_ShowLights = false;
        bool m_ScreenNeedRefresh = false;
    };
}
