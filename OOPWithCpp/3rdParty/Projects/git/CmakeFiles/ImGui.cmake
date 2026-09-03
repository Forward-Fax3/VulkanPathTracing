foreach (Target ImGui_SSE4_2 ImGui_AVX2 ImGui_AVX512)
    add_library(${Target} STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/CmakeFiles/imconfig.h
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/backends/imgui_impl_sdl3.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/backends/imgui_impl_sdl3.h
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imconfig.h
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imgui.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imgui.h
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imgui_demo.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imgui_draw.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imgui_internal.h
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imgui_tables.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imgui_widgets.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imstb_rectpack.h
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imstb_textedit.h
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui/imstb_truetype.h
    )

    target_include_directories(${Target} PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/SDL/include
            ${Vulkan_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/SDL/include
    )

    set_target_properties(${Target} PROPERTIES
            OUTPUT_NAME ${Target}
            ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/ImGui
            LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/ImGui
            RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}/ImGui
            POSITION_INDEPENDENT_CODE True
            INTERPROCEDURAL_OPTIMIZATION ${LTO_ENABLED}
    )

    target_link_options(${Target} PRIVATE
            ${Polly_LINK_FLAGS}
    )
endforeach()

target_compile_options(ImGui_SSE4_2 PRIVATE
        ${SSE42_FLAGS}
        "-DIMGUI_USER_CONFIG=\"${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/CmakeFiles/imconfig.h\""
)
target_compile_options(ImGui_AVX2 PRIVATE
        ${AVX2_FLAGS}
        "-DIMGUI_USER_CONFIG=\"${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/CmakeFiles/imconfig.h\""
)
target_compile_options(ImGui_AVX512 PRIVATE
        ${AVX512_FLAGS}
        "-DIMGUI_USER_CONFIG=\"${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/CmakeFiles/imconfig.h\""
)
