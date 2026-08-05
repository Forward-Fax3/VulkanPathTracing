//
// Created by forwardfax3 on 05/08/2026.
//

#pragma once
#include "Core.hpp"
#include <imgui.h>


namespace ImGui::OWC
{
    bool SliderIntNWithAlignedText(const char* label, ImGuiDataType data_type, const char* const* text, void* values, int components, int v_min, int v_max, const char* format = nullptr, ImGuiSliderFlags flags = 0);

    // requires text to already be formatted use std::format or something similar like fmt
    OWC_FORCE_INLINE bool SliderInt2WithAlignedText(const char* label, const char* text1, const char* text2, ::OWC::i32 values[2], const int v_min, const int v_max, const ImGuiSliderFlags flags = 0)
    {
        const std::array texts = { text1, text2 };
        return  SliderIntNWithAlignedText(label, ImGuiDataType_S32, texts.data(), values, 2, v_min, v_max, nullptr, flags);
    }

    // requires text to already be formatted use std::format or something similar like fmt
    OWC_FORCE_INLINE bool SliderInt3WithAlignedText(const char* label, const char* text1, const char* text2, const char* text3, ::OWC::i32 values[3], const int v_min, const int v_max, const ImGuiSliderFlags flags = 0)
    {
        const std::array texts = { text1, text2, text3 };
        return  SliderIntNWithAlignedText(label, ImGuiDataType_S32, texts.data(), values, 3, v_min, v_max, nullptr, flags);
    }

    // requires text to already be formatted use std::format or something similar like fmt
    OWC_FORCE_INLINE bool SliderInt4WithAlignedText(const char* label, const char* text1, const char* text2, const char* text3, const char* text4, ::OWC::i32 values[4], const int v_min, const int v_max, const ImGuiSliderFlags flags = 0)
    {
        const std::array texts = { text1, text2, text3, text4 };
        return  SliderIntNWithAlignedText(label, ImGuiDataType_S32, texts.data(), values, 4, v_min, v_max, nullptr, flags);
    }

    // requires text to already be formatted use std::format or something similar like fmt
    OWC_FORCE_INLINE bool SliderInt2WithAlignedText(const char* label, const char* text1, const char* text2, ::OWC::u32 values[2], const int v_min, const int v_max, const ImGuiSliderFlags flags = 0)
    {
        const std::array texts = { text1, text2 };
        return  SliderIntNWithAlignedText(label, ImGuiDataType_U32, texts.data(), values, 2, v_min, v_max, nullptr, flags);
    }

    // requires text to already be formatted use std::format or something similar like fmt
    OWC_FORCE_INLINE bool SliderInt3WithAlignedText(const char* label, const char* text1, const char* text2, const char* text3, ::OWC::u32 values[3], const int v_min, const int v_max, const ImGuiSliderFlags flags = 0)
    {
        const std::array texts = { text1, text2, text3 };
        return  SliderIntNWithAlignedText(label, ImGuiDataType_U32, texts.data(), values, 3, v_min, v_max, nullptr, flags);
    }

    // requires text to already be formatted use std::format or something similar like fmt
    OWC_FORCE_INLINE bool SliderInt4WithAlignedText(const char* label, const char* text1, const char* text2, const char* text3, const char* text4, ::OWC::u32 values[4], const int v_min, const int v_max, const ImGuiSliderFlags flags = 0)
    {
        const std::array texts = { text1, text2, text3, text4 };
        return  SliderIntNWithAlignedText(label, ImGuiDataType_U32, texts.data(), values, 4, v_min, v_max, nullptr, flags);
    }
}
