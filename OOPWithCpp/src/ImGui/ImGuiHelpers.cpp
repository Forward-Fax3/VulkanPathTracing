//
// Created by forwardfax3 on 05/08/2026.
//

#include <ImGuiHelpers.hpp>
#include <imgui_internal.h>
#include <glm/glm.hpp>

namespace ImGui::OWC
{
    bool SliderScalerNWithAlignedText(const char* label, const ImGuiDataType data_type, const char* const* text, void* values, const int components, const void* v_min, const void* v_max, const char* format, const ImGuiSliderFlags flags)
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        const float step = (CalcItemWidth() + GImGui->Style.ItemInnerSpacing.x) / static_cast<float>(components);

        BeginGroup();
        PushID(label);
        PushID("SliderScalerNWithAlignedText_Text");
        float offset = 0.0f;

        for (int i = 0; i < components; i++)
        {
            PushID(i);
            if (i > 0)
                SameLine(offset);
            TextUnformatted(text[i]);
            PopID();
            const auto offset1 = glm::fma(step, static_cast<float>(i), step);
            const auto offset2 = offset + CalcTextSize(text[i]).x + GImGui->Style.ItemInnerSpacing.x;
            offset = ImMax(offset1, offset2);
        }
        PopID();

        PushID("SliderScalerNWithAlignedText_Slider");
        const bool value_changed = SliderScalarN("", data_type, values, components, v_min, v_max, format, flags);
        PopID();

        const char* label_end = FindRenderedTextEnd(label);
        if (label != label_end)
        {
            SameLine(0, GImGui->Style.ItemInnerSpacing.x);
            TextEx(label, label_end);
        }

        PopID();
        EndGroup();
        return value_changed;
    }
}
