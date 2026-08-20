# TODO: fix library output

set(VULKAN_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS} CACHE PATH "System Vulkan Headers")

# --- Assimp: 3 independently-built SIMD variants (SSE4.2 / AVX2 / AVX512) ---
#
# Assimp's own CMakeLists.txt is old-style/directory-scoped (INCLUDE_DIRECTORIES(),
# ADD_DEFINITIONS() rather than target_include_directories()/target_compile_definitions()),
# so recompiling its source list into new hand-rolled targets (the way SDL3's variants are
# built elsewhere in this project) can't reliably pick up every include path/define it needs.
# Instead, each variant below is a fully separate, isolated build of the whole Assimp project
# via ExternalProject_Add, differing only in the SIMD compiler flags passed in. That re-runs
# Assimp's own build logic in full each time, so nothing gets missed - at the cost of a slower,
# 3x build.
include(ExternalProject)

set(ASSIMP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/assimp)

# This project uses custom build-type names (clang_Debug, clang_Debug_Optimized, clang_Dist)
# that Assimp's own CMakeLists.txt doesn't know about - if passed through unrecognized,
# CMAKE_BUILD_TYPE effectively falls through to "no optimization flags", silently producing
# an unoptimized Assimp build even when the outer project is built in a "release-like" config.
# Map our config names onto the standard Debug/Release CMake understands.
if (CMAKE_BUILD_TYPE STREQUAL "clang_Debug")
    set(ASSIMP_BUILD_TYPE "Debug")
elseif (CMAKE_BUILD_TYPE STREQUAL "clang_Debug_Optimized")
    set(ASSIMP_BUILD_TYPE "Release")
elseif (CMAKE_BUILD_TYPE STREQUAL "clang_Dist")
    set(ASSIMP_BUILD_TYPE "Release")
else ()
    set(ASSIMP_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif ()

# Options shared by every variant. Assimp's own CMakeLists.txt defaults ASSIMP_INSTALL and
# ASSIMP_BUILD_TESTS to ON, which is meant for standalone/system builds, not for use as a
# vendored submodule - and ASSIMP_WARNINGS_AS_ERRORS defaults ON, which can turn warnings
# from its own 3rd-party contrib code (draco, zlib, ...) into hard failures under stricter
# toolchains. ASSIMP_INJECT_DEBUG_POSTFIX is forced off so the output library name ("assimp",
# plus platform prefix/suffix) is predictable across configs - otherwise Debug builds get a
# "d" appended and we'd have to guess at it below.
set(ASSIMP_COMMON_CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        -DASSIMP_BUILD_TESTS=OFF
        -DASSIMP_INSTALL=ON
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
        -DASSIMP_BUILD_SAMPLES=OFF
        -DASSIMP_BUILD_DOCS=OFF
        -DASSIMP_WARNINGS_AS_ERRORS=OFF
        -DASSIMP_INJECT_DEBUG_POSTFIX=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_BUILD_TYPE=${ASSIMP_BUILD_TYPE}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        # Dependencies: Build zlib from source (minizip is automatically enabled by assimp
        # when ASSIMP_BUILD_ZLIB=ON - see assimp's CMakeLists.txt line 601)
        -DASSIMP_BUILD_ZLIB=ON
        # Ensure unzip headers are found: point to contrib directory where unzip.h exists
        -DUNZIP_INCLUDE_DIRS=${ASSIMP_SOURCE_DIR}/contrib/unzip
        # Build draco support disabled (can be enabled if needed for glTF)
        -DASSIMP_BUILD_DRACO=OFF
        -DASSIMP_BUILD_DRACO_STATIC=OFF
        -DASSIMP_NO_EXPORT=OFF
        -DASSIMP_ANDROID_JNIIOSYSTEM=OFF
        -DASSIMP_DOUBLE_PRECISION=OFF
        -DASSIMP_IGNORE_GIT_HASH=ON
        # Optional features (opt-in formats)
        -DASSIMP_BUILD_M3D_IMPORTER=OFF
        -DASSIMP_BUILD_M3D_EXPORTER=OFF
        -DASSIMP_BUILD_USD_IMPORTER=OFF
        -DASSIMP_BUILD_VRML_IMPORTER=OFF
        -DASSIMP_BUILD_NONFREE_C4D_IMPORTER=OFF
        -DASSIMP_HUNTER_ENABLED=OFF
        -DASSIMP_BUILD_USE_CCACHE=OFF
)
if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND ASSIMP_COMMON_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif ()
if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND ASSIMP_COMMON_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif ()

set(ASSIMP_STATIC_LIB_NAME "${CMAKE_STATIC_LIBRARY_PREFIX}assimp${CMAKE_STATIC_LIBRARY_SUFFIX}")

# Builds one isolated Assimp static-lib variant and exposes it as the imported target
# Assimp_<TargetArch>, ready to be used in target_link_libraries() like any other target.
function(add_assimp_variant TargetArch SimdFlags)
    # EpDriverName is the custom target ExternalProject_Add itself creates to drive the
    # download/configure/build/install steps. ImportedName is the separate, consumer-facing
    # imported library target (used in target_link_libraries()) - these must NOT share a name.
    set(EpDriverName Assimp_${TargetArch}_ep)
    set(ImportedName Assimp_${TargetArch})
    set(EpPrefix ${CMAKE_CURRENT_BINARY_DIR}/assimp-${TargetArch})
    set(EpInstallDir ${EpPrefix}/install)
    set(EpLibPath ${EpInstallDir}/lib/${ASSIMP_STATIC_LIB_NAME})

    # SimdFlags list items may use CMake's "SHELL:" prefix (meaningful only inside
    # target_compile_options() - it prevents flag de-duplication and is unpacked into
    # separate tokens there). CMAKE_CXX_FLAGS/CMAKE_C_FLAGS are plain strings passed
    # straight to the compiler, so "SHELL:" must be stripped or it gets sent verbatim
    # as a bogus argument (e.g. "SHELL:-mllvm" -> real flags "-mllvm -polly").
    set(SimdFlagsClean "")
    foreach (FlagItem IN LISTS SimdFlags)
        string(REGEX REPLACE "^SHELL:" "" FlagItem "${FlagItem}")
        list(APPEND SimdFlagsClean "${FlagItem}")
    endforeach ()
    string(REPLACE ";" " " SimdFlagsStr "${SimdFlagsClean}")

    ExternalProject_Add(${EpDriverName}
            SOURCE_DIR ${ASSIMP_SOURCE_DIR}
            PREFIX ${EpPrefix}
            INSTALL_DIR ${EpInstallDir}
            CMAKE_ARGS
            ${ASSIMP_COMMON_CMAKE_ARGS}
            -DCMAKE_INSTALL_PREFIX=${EpInstallDir}
            "-DCMAKE_CXX_FLAGS=${SimdFlagsStr}"
            "-DCMAKE_C_FLAGS=${SimdFlagsStr}"
            BUILD_BYPRODUCTS ${EpLibPath}
    )

    # The include dir doesn't exist until the external build's install step runs, but
    # target_include_directories() requires the path to exist at configure time - so
    # create it as a placeholder; ExternalProject's install step populates it later.
    file(MAKE_DIRECTORY ${EpInstallDir}/include)

    add_library(${ImportedName} STATIC IMPORTED GLOBAL)
    set_target_properties(${ImportedName} PROPERTIES
            IMPORTED_LOCATION ${EpLibPath}
            INTERFACE_INCLUDE_DIRECTORIES ${EpInstallDir}/include
    )
    add_dependencies(${ImportedName} ${EpDriverName})
endfunction()

add_assimp_variant(SSE4_2 "${SSE42_FLAGS}")
add_assimp_variant(AVX2 "${AVX2_FLAGS}")
add_assimp_variant(AVX512 "${AVX512_FLAGS}")