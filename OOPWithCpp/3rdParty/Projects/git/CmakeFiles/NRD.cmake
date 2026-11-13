set(NRD_STATIC_LIBRARY       ON  CACHE BOOL "" FORCE)
set(NRD_EMBEDS_SPIRV_SHADERS ON  CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXIL_SHADERS  OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXBC_SHADERS  OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/NRD EXCLUDE_FROM_ALL)

get_target_property(NRD_SOURCE_DIR NRD SOURCE_DIR)
get_target_property(NRD_RAW_SOURCES NRD SOURCES)

set(NRD_SOURCES "")
foreach (src ${NRD_RAW_SOURCES})
    # leave generator expressions and absolute paths alone
    if (src MATCHES "^\\$<" OR IS_ABSOLUTE "${src}")
        list(APPEND NRD_SOURCES "${src}")
    else ()
        list(APPEND NRD_SOURCES "${NRD_SOURCE_DIR}/${src}")
    endif ()
endforeach ()

# Collect every target NRD's own CMakeLists defines (the shader target included)
get_directory_property(NRD_DIR_TARGETS DIRECTORY ${NRD_SOURCE_DIR} BUILDSYSTEM_TARGETS)
get_target_property(NRD_MANUAL_DEPS NRD MANUALLY_ADDED_DEPENDENCIES)
get_target_property(NRD_LINK_LIBS   NRD LINK_LIBRARIES)

message(STATUS "NRD dir targets : ${NRD_DIR_TARGETS}")
message(STATUS "NRD manual deps : ${NRD_MANUAL_DEPS}")
message(STATUS "NRD link libs   : ${NRD_LINK_LIBS}")

set(NRD_BUILD_DEPS ${NRD_DIR_TARGETS})
list(REMOVE_ITEM NRD_BUILD_DEPS NRD)
if (NRD_MANUAL_DEPS)
    list(APPEND NRD_BUILD_DEPS ${NRD_MANUAL_DEPS})
endif ()
list(REMOVE_DUPLICATES NRD_BUILD_DEPS)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/nrd_compat.h
        "#pragma once
#include <cmath>
#include <cstdlib>
using std::abs;
")

if (MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(NRD_FORCE_INCLUDE /FI${CMAKE_CURRENT_BINARY_DIR}/nrd_compat.h)
else ()
    set(NRD_FORCE_INCLUDE -include ${CMAKE_CURRENT_BINARY_DIR}/nrd_compat.h)
endif ()

foreach (Target NRD_SSE4_2 NRD_AVX2 NRD_AVX512)
    add_library(${Target} STATIC ${NRD_SOURCES})

    target_include_directories(${Target} PUBLIC
            $<TARGET_PROPERTY:NRD,INTERFACE_INCLUDE_DIRECTORIES>)
    target_include_directories(${Target} PRIVATE
            $<TARGET_PROPERTY:NRD,INCLUDE_DIRECTORIES>)

    target_compile_definitions(${Target} PRIVATE
            $<TARGET_PROPERTY:NRD,COMPILE_DEFINITIONS>)
    target_compile_features(${Target} PRIVATE cxx_std_17)

    # make sure the embedded shader headers get generated first
    if (TARGET NRD_Shaders)
        add_dependencies(${Target} NRD_Shaders)
    endif ()

    set_target_properties(${Target} PROPERTIES
            POSITION_INDEPENDENT_CODE True
            INTERPROCEDURAL_OPTIMIZATION ${LTO_ENABLED}
            OUTPUT_NAME ${Target}
            ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/NRD)

    if (NRD_BUILD_DEPS)
        add_dependencies(${Target} ${NRD_BUILD_DEPS})
    endif ()
endforeach ()

target_compile_options(NRD_SSE4_2 PRIVATE
        ${SSE42_FLAGS}
        ${NRD_FORCE_INCLUDE}
)
target_compile_options(NRD_AVX2 PRIVATE
        ${AVX2_FLAGS}
        ${NRD_FORCE_INCLUDE}
)
target_compile_options(NRD_AVX512 PRIVATE
        ${AVX512_FLAGS}
        ${NRD_FORCE_INCLUDE}
)