add_executable(StartProj
        StartProj/src/main.cpp
)

if (MSVC)
    # CMake passes .manifest files in the sources to the manifest tool automatically
    target_sources(StartProj PRIVATE StartProj/StartProj.manifest)
elseif (MINGW)
    # MinGW has no manifest tool, so embed it as a resource (type 24 = RT_MANIFEST, id 1 = executable)
    enable_language(RC)
    set(StartProj_RC ${CMAKE_CURRENT_BINARY_DIR}/StartProj_manifest.rc)
    file(WRITE ${StartProj_RC} "1 24 \"${CMAKE_CURRENT_SOURCE_DIR}/StartProj/StartProj.manifest\"\n")
    target_sources(StartProj PRIVATE ${StartProj_RC})
    set_source_files_properties(${StartProj_RC} PROPERTIES
            OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/StartProj/StartProj.manifest)
endif ()

set_target_properties(StartProj PROPERTIES OUTPUT_NAME StartProj)
set_target_properties(StartProj PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/StartProj
        LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/StartProj
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}/StartProj
)
add_dependencies(StartProj
        GraphicsProgrammingWithCppSSE4_2
        GraphicsProgrammingWithCppAVX2
        GraphicsProgrammingWithCppAVX512
)
target_include_directories(StartProj PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/StartProj/src
)
set_target_properties(StartProj PROPERTIES
        POSITION_INDEPENDENT_CODE False
        INTERPROCEDURAL_OPTIMIZATION ${LTO_ENABLED}
)
set(StartProj_FLAGS ${CMAKE_CONFIG_FLAGS})
list(FILTER StartProj_FLAGS EXCLUDE REGEX "polly|fplugin|fast-math|math-errno|trapping-math|unsafe-math")
target_compile_options(StartProj PRIVATE ${StartProj_FLAGS})

if (NOT MSVC)
    target_compile_options(StartProj PRIVATE -march=x86-64 -Wextra -Wpedantic)
endif()

if(MINGW)
    target_link_options(StartProj PRIVATE -static)
endif()

if (MINGW AND LTO_ENABLED)
    set_target_properties(StartProj PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION false
    )
    target_compile_options(StartProj PRIVATE -flto=thin)
    target_link_options(StartProj PRIVATE -flto=thin -fuse-ld=lld)
endif()

if (MINGW)
    target_link_libraries(StartProj PRIVATE stdc++exp)
endif ()
