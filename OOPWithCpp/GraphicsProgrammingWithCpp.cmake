file(GLOB_RECURSE GraphicsProgrammingWithCppFiles
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/src/**.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/src/**.hpp
)

file(GLOB_RECURSE GraphicsProgrammingWithCppPathsRaw
        LIST_DIRECTORIES true
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/src/*
)

set(GraphicsProgrammingWithCppPaths "")
foreach(path ${GraphicsProgrammingWithCppPathsRaw})
    if(IS_DIRECTORY ${path})
        list(APPEND GraphicsProgrammingWithCppPaths ${path})
    endif()
endforeach()

set(GraphicsProgrammingWithCppIncludeDirs
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/SDL/include
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/assimp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/ImGui
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/HeaderOnly/git/GLM
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/HeaderOnly/git/stbImage
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/HeaderOnly/git/VMAhpp/VulkanMemoryAllocator/include
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/HeaderOnly/git/VMAhpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/HeaderOnly/Src/TinyGLTF
        ${Vulkan_INCLUDE_DIRS}
)

if (WIN32)
    set(TargetLinks
            Winmm
            SetupAPI
            Version
    )
else ()
    set(TargetLinks "")
endif()

foreach (TargetArch SSE4_2 AVX2 AVX512)
    add_library(GraphicsProgrammingWithCpp${TargetArch} SHARED ${GraphicsProgrammingWithCppFiles})
    target_compile_options(GraphicsProgrammingWithCpp${TargetArch} PRIVATE
            -DGLM_FORCE_SIZE_T_LENGTH
            -DGLM_ENABLE_EXPERIMENTAL
            -DGLM_FORCE_DEFAULT_ALIGNED_GENTYPES
            -DGLM_FORCE_INLINE
            -DTINYGLTF_JSON_NO_EXCEPTIONS
    )
    target_include_directories(GraphicsProgrammingWithCpp${TargetArch} PUBLIC
            ${GraphicsProgrammingWithCppPaths}
            ${GraphicsProgrammingWithCppIncludeDirs}
    )
    set_target_properties(GraphicsProgrammingWithCpp${TargetArch} PROPERTIES
            POSITION_INDEPENDENT_CODE True
            INTERPROCEDURAL_OPTIMIZATION ${LTO_ENABLED}
            OUTPUT_NAME GraphicsProgrammingWithCpp${TargetArch}
            ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/GraphicsProgrammingWithCpp
            LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/GraphicsProgrammingWithCpp
            RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}/GraphicsProgrammingWithCpp
    )
    target_link_libraries(GraphicsProgrammingWithCpp${TargetArch} PUBLIC
            SDL3_${TargetArch}
            ImGui_${TargetArch}
            Assimp_${TargetArch}
            ${Vulkan_LIBRARIES}
            ${Polly_LINK_FLAGS}
            ${TargetLinks}
    )
    if (MINGW AND LTO_ENABLED)
        set_target_properties(GraphicsProgrammingWithCpp${TargetArch} PROPERTIES
                INTERPROCEDURAL_OPTIMIZATION false
        )
        target_compile_options(GraphicsProgrammingWithCpp${TargetArch} PRIVATE -flto=thin)
        target_link_options(GraphicsProgrammingWithCpp${TargetArch} PRIVATE -flto=thin -fuse-ld=lld)
    endif()
endforeach ()

target_compile_options(GraphicsProgrammingWithCppSSE4_2 PRIVATE
        ${SSE42_FLAGS}
        -DGLM_FORCE_SSE42
        -DTINYGLTF3_JSON_SIMD_SSE2 # TinyGLTF doesn't have an SSE4.2 specific SIMD code path, so we use the SSE2 SIMD code path for the SSE4.2 build
        -DSSE4_2
)
target_compile_options(GraphicsProgrammingWithCppAVX2 PRIVATE
        ${AVX2_FLAGS}
        -DGLM_FORCE_AVX2
        -DTINYGLTF3_JSON_SIMD_AVX2
        -DAVX2
)
target_compile_options(GraphicsProgrammingWithCppAVX512 PRIVATE
        ${AVX512_FLAGS}
        -DGLM_FORCE_AVX2 # GLM no longer supports AVX-512 specific code paths, so we use AVX2 for the AVX-512 build as well
        -DTINYGLTF3_JSON_SIMD_AVX2 # TinyGLTF also doesn't have AVX-512 specific code paths, so we use the AVX2 SIMD code path for the AVX-512 build as well
        -DAVX2
        -DAVX512
)
