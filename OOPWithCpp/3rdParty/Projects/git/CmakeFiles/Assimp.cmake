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

# --- ThinLTO support for MinGW ---
set(ASSIMP_LTO_COMPILE_FLAGS "")
set(ASSIMP_LTO_LINK_FLAGS "")
set(ASSIMP_LTO_AR "")
set(ASSIMP_LTO_RANLIB "")

if (MINGW AND LTO_ENABLED)
    # 1. Compile flags: produce LLVM bitcode instead of native object files
    set(ASSIMP_LTO_COMPILE_FLAGS "-flto=thin")

    # 2. Link flags: use ThinLTO and lld for the final link (used for Assimp's own test executables)
    set(ASSIMP_LTO_LINK_FLAGS "-flto=thin -fuse-ld=lld")

    # 3. For ThinLTO to work with static archives, CMake needs to use llvm-ar and llvm-ranlib
    #    instead of the GNU ones (which don't understand the thin index).
    #    These will be passed into the inner build.
    find_program(LLVM_AR NAMES llvm-ar)
    find_program(LLVM_RANLIB NAMES llvm-ranlib)
    if (LLVM_AR AND LLVM_RANLIB)
        set(ASSIMP_LTO_AR ${LLVM_AR})
        set(ASSIMP_LTO_RANLIB ${LLVM_RANLIB})
    else()
        message(WARNING "llvm-ar / llvm-ranlib not found. ThinLTO may fail for static libraries.")
        # Optionally disable LTO if tools missing:
        # set(LTO_ENABLED OFF)
    endif()
endif()

set(EXTRA_CMAKE_ARGS)
if (ASSIMP_LTO_AR)
    list(APPEND EXTRA_CMAKE_ARGS "-DCMAKE_AR=${ASSIMP_LTO_AR}")
endif()
if (ASSIMP_LTO_RANLIB)
    list(APPEND EXTRA_CMAKE_ARGS "-DCMAKE_RANLIB=${ASSIMP_LTO_RANLIB}")
endif()

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
        -DASSIMP_BUILD_SINGLETHREADED=OFF
)
if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND ASSIMP_COMMON_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif ()
if (CMAKE_TOOLCHAIN_FILE)
    list(APPEND ASSIMP_COMMON_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif ()

if (NOT CMAKE_BUILD_TYPE STREQUAL "clang_Debug")
    list(APPEND ASSIMP_COMMON_CMAKE_ARGS -DNDEBUG=1)
endif()

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
    # When ASSIMP_BUILD_ZLIB=ON, Assimp builds its own bundled zlib (CMake target
    # "zlibstatic") and uses it internally, but libassimp.a is a plain static archive -
    # it does NOT re-export zlib's symbols, so consumers must link zlibstatic themselves.
    # On Linux this often goes unnoticed (something else on the link line already drags
    # in libz), but MinGW/lld won't silently resolve inflate/crc32/uncompress/etc, which
    # is exactly the "undefined symbol" failure we're fixing. Assimp's own install step
    # installs this lib right alongside libassimp.a, so pick it up the same way.
    set(EpZlibLibPath ${EpInstallDir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}zlibstatic${CMAKE_STATIC_LIBRARY_SUFFIX})

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

    # NOTE: CMAKE_C_FLAGS / CMAKE_CXX_FLAGS below carry ONLY the SIMD flags - they don't
    # clobber CMAKE_C_FLAGS_RELEASE / CMAKE_CXX_FLAGS_RELEASE (CMake appends flags from
    # both when CMAKE_BUILD_TYPE=Release), so -O2/-DNDEBUG etc. from Release still apply
    # on top of these. This comment exists to flag that dependency explicitly, since it's
    # easy to assume passing CMAKE_C_FLAGS overrides everything and silently lose
    # optimization if that assumption is ever acted on.
    ExternalProject_Add(${EpDriverName}
            SOURCE_DIR ${ASSIMP_SOURCE_DIR}
            PREFIX ${EpPrefix}
            INSTALL_DIR ${EpInstallDir}
            CMAKE_GENERATOR ${CMAKE_GENERATOR}
            CMAKE_ARGS
            ${ASSIMP_COMMON_CMAKE_ARGS}
            -DCMAKE_INSTALL_PREFIX=${EpInstallDir}
            # --- Modify these two lines to append LTO compile flags ---
            "-DCMAKE_CXX_FLAGS=${SimdFlagsStr} ${ASSIMP_LTO_COMPILE_FLAGS}"
            "-DCMAKE_C_FLAGS=${SimdFlagsStr} ${ASSIMP_LTO_COMPILE_FLAGS}"
            # --- Add linker flags for executables/shared libs (inner build tests/tools) ---
            "-DCMAKE_EXE_LINKER_FLAGS=${ASSIMP_LTO_LINK_FLAGS}"
            "-DCMAKE_SHARED_LINKER_FLAGS=${ASSIMP_LTO_LINK_FLAGS}"
            # --- Override the archiver and ranlib for ThinLTO support ---
            ${EXTRA_CMAKE_ARGS}
            # --- Prevent CMake's built-in IPO machinery from interfering ---
            "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF"
            "-DCMAKE_POLICY_DEFAULT_CMP0069=NEW"
            BUILD_BYPRODUCTS ${EpLibPath} ${EpZlibLibPath}
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

    # Import the bundled zlibstatic too and attach it as a link dependency of the Assimp
    # imported target, so anything that links Assimp_<arch> transitively links zlib -
    # matching what would happen automatically if Assimp were a real (non-imported)
    # CMake target with proper target_link_libraries() propagation.
    set(ZlibImportedName Assimp_${TargetArch}_zlib)
    add_library(${ZlibImportedName} STATIC IMPORTED GLOBAL)
    set_target_properties(${ZlibImportedName} PROPERTIES
            IMPORTED_LOCATION ${EpZlibLibPath}
    )
    add_dependencies(${ZlibImportedName} ${EpDriverName})
    set_target_properties(${ImportedName} PROPERTIES
            INTERFACE_LINK_LIBRARIES ${ZlibImportedName}
    )
endfunction()

add_assimp_variant(SSE4_2 "${SSE42_FLAGS}")
add_assimp_variant(AVX2 "${AVX2_FLAGS}")
add_assimp_variant(AVX512 "${AVX512_FLAGS}")