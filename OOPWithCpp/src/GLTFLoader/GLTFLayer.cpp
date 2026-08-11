//
// Created by forwardfax3 on 07/08/2026.
//

#include "GLTFLayer.hpp"

#include "imgui.h"
#include "ImGuiHelpers.hpp"
#include "glm/gtc/type_ptr.hpp"

namespace OWC
{
    GLTFLayer::GLTFLayer(const std::shared_ptr<Graphics::GeneralBuffer>& lightBuffer, std::vector<GPULightData>&& lightData, const std::string_view& layerName)
        : m_LightBuffer(lightBuffer), m_LightData(std::move(lightData)), layerName(layerName) {}

    void GLTFLayer::ImGuiRender()
    {
        m_ScreenNeedRefresh = false;

        ImGui::Begin(layerName.data());

        ImGui::Checkbox("Show Lights", &m_ShowLights);

        if (m_ShowLights)
        {
            ImGui::TextUnformatted("Light Data:");

            for (uSize i = 0; i != m_LightData.size(); i++)
            {
                GPULightData& light = m_LightData[i];
                ImGui::PushID(static_cast<int>(i));
                if (i > 0)
                    ImGui::Separator();
                ImGui::Text("Light %zu\nIntensity", i);
                m_ScreenNeedRefresh |= ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 10000.0f, "%.1f");
                m_ScreenNeedRefresh |= ImGui::OWC::SliderFloat3WithAlignedText("Colour", "Red", "Green", "Blue", glm::value_ptr(light.Colour), 0.0f, 1.0f);
                if (light.type == 1) // point light
                    m_ScreenNeedRefresh |= ImGui::DragFloat("Radius", &light.radius, 0.1f, 0.0f, 100.0f, "%.2f");
                ImGui::PopID();
            }
        }

        ImGui::End();

        if (m_ScreenNeedRefresh)
        {
            m_LightBuffer->UpdateBufferData(std::bit_cast<u8*>(m_LightData.data()));
        }
    }
}
