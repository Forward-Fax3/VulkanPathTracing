cmake_minimum_required(VERSION 4.1)
project(OOPWithCpp LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS OFF)

SET(CMAKE_C_USE_RESPONSE_FILE_FOR_OBJECTS 1)
SET(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS 1)

SET(CMAKE_C_RESPONSE_FILE_LINK_FLAG "@")
SET(CMAKE_CXX_RESPONSE_FILE_LINK_FLAG "@")
SET(CMAKE_NINJA_FORCE_RESPONSE_FILE 1 CACHE INTERNAL "")

find_package(Vulkan REQUIRED)

file(GLOB LLVM_LIB_PATHS /usr/lib/llvm*/lib)
find_library(Polly_LIBRARY NAMES LLVMPolly.so PATHS /usr/lib ${LLVM_LIB_PATHS} NO_DEFAULT_PATH)

if (Polly_LIBRARY AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(Polly_FLAGS
            -fplugin=${Polly_LIBRARY}
            "SHELL:-mllvm -polly"
            "SHELL:-mllvm -polly-vectorizer=stripmine"
            "SHELL:-mllvm -polly-position=before-vectorizer"
            "SHELL:-mllvm -polly-parallel"
    )
    set(Polly_LINK_FLAGS -lgomp)
    message(STATUS "Polly found: ${Polly_LIBRARY}")
else()
    set(Polly_FLAGS "")
    set(Polly_LINK_FLAGS "")
    message(STATUS "Polly not found, proceeding without it")
endif ()


set(BUILD_TYPES
        msc_Debug
        clang_Debug
        msc_Debug_Optimized
        clang_Debug_Optimized
        msc_Dist
        clang_Dist
)

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${BUILD_TYPES})

if (CMAKE_BUILD_TYPE STREQUAL "clang_Debug")
    add_compile_definitions(DEBUG)
    set(CMAKE_CONFIG_FLAGS
            -g
            -gdwarf-4
            -O0
            -fno-omit-frame-pointer
            -fno-inline
            -fno-unroll-loops
            -fno-vectorize
            -ftrivial-auto-var-init=zero
            $<IF:$<PLATFORM_ID:Windows>,,-fsanitize=leak>
    )
    set(LTO_ENABLED False)
    set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Bin/${CMAKE_HOST_SYSTEM_NAME}/Debug/clang)
elseif (CMAKE_BUILD_TYPE STREQUAL "clang_Debug_Optimized")
    add_compile_definitions(DEBUG_O EN_ENABLE_ASSERTS)
    set(CMAKE_CONFIG_FLAGS
            -g
            -gdwarf-4
            -O2
            ${Polly_FLAGS}
            -ffast-math
            -fno-math-errno
            -fno-trapping-math
            -funsafe-math-optimizations
            -finline-functions
            -funroll-loops
            -fvectorize
            -ftrivial-auto-var-init=zero
    )
    set(LTO_ENABLED True)
    set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Bin/${CMAKE_HOST_SYSTEM_NAME}/Debug-Optimized/clang)
elseif (CMAKE_BUILD_TYPE STREQUAL "clang_Dist")
    add_compile_definitions(DIST NDEBUG)
    set(CMAKE_CONFIG_FLAGS
            -O3
            ${Polly_FLAGS}
            -ffast-math
            -fno-math-errno
            -fno-trapping-math
            -funsafe-math-optimizations
            -finline-functions
            -funroll-loops
            -fvectorize
    )
    set(LTO_ENABLED True)
    set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Bin/${CMAKE_HOST_SYSTEM_NAME}/Dist/clang)
elseif (CMAKE_BUILD_TYPE STREQUAL "msc_Debug")
    set(CMAKE_CONFIG_FLAGS
            /Zi
            /Od
            /RTC1
            /MDd
    )
    set(LTO_ENABLED False)
    set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Bin/${CMAKE_HOST_SYSTEM_NAME}/Debug/msc)
elseif (CMAKE_BUILD_TYPE STREQUAL "msc_Debug_Optimized")
    set(CMAKE_CONFIG_FLAGS
            /Zi
            /O2
            /MD
    )
    set(LTO_ENABLED True)
    set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Bin/${CMAKE_HOST_SYSTEM_NAME}/Debug-Optimized/msc)
elseif (CMAKE_BUILD_TYPE STREQUAL "msc_Dist")
    set(CMAKE_CONFIG_FLAGS
            /O2
            /MD
    )
    set(LTO_ENABLED True)
    set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Bin/${CMAKE_HOST_SYSTEM_NAME}/Dist/msc)
else()
    message(FATAL_ERROR "Unknown build type: ${CMAKE_BUILD_TYPE}")
endif()

if(MSVC)
    set(GeneralFlags
            /W4
            /permissive-
            /experimental:c11atomics
            /fp:fast
            ${CMAKE_CONFIG_FLAGS}
    )
    set(AVX512_FLAGS
            ${GeneralFlags}
            /arch:AVX512
    )
    set(AVX2_FLAGS
            ${GeneralFlags}
            /arch:AVX2
    )
    set(SSE42_FLAGS
            ${GeneralFlags}
            /arch:SSE4.2
    )
else()
    set(GeneralFlags
            -Wall
            #-Wextra
            #-Wpedantic
            -ffast-math
            ${CMAKE_CONFIG_FLAGS}
    )
    set(AVX512_FLAGS
            ${GeneralFlags}
            -march=x86-64-v4
            -mavx512vbmi
            -mavx512vbmi2
    )
    set(AVX2_FLAGS
            ${GeneralFlags}
            -march=x86-64-v3
    )
    set(SSE42_FLAGS
            ${GeneralFlags}
            -march=x86-64-v2
    )
endif()

include(OOPWithCpp/3rdParty/Projects/git/CmakeFiles/ImGui.cmake)
include(OOPWithCpp/3rdParty/Projects/git/CmakeFiles/SDL.cmake)
include(OOPWithCpp/3rdParty/Projects/git/CmakeFiles/Assimp.cmake)
include(OOPWithCpp/GraphicsProgrammingWithCpp.cmake)
include(StartProj/StartProj.cmake)
