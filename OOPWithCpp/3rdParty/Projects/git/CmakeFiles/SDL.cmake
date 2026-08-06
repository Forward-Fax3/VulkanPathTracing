set(SDL_USE_SSE4 ON CACHE BOOL "Use SSE4.2")
set(SDL_USE_AVX2 ON CACHE BOOL "Use AVX2")
set(SDL_USE_AVX512 ON CACHE BOOL "Use AVX512")
set(SDL_USE_CMAKE_BUILD TRUE CACHE BOOL "Force CMake build")

set(VULKAN_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS} CACHE PATH "System Vulkan Headers")

if (WIN32 AND MSVC)
    cmake_path(GET CMAKE_RC_COMPILER PARENT_PATH _rc_arch_dir)    # .../x64
    cmake_path(GET _rc_arch_dir PARENT_PATH _rc_ver_dir)          # .../10.0.26100.0
    cmake_path(GET _rc_ver_dir FILENAME _rc_version)              # 10.0.26100.0
    cmake_path(GET _rc_ver_dir PARENT_PATH _rc_bin_dir)           # .../bin
    cmake_path(GET _rc_bin_dir PARENT_PATH _rc_sdk_root)          # .../Windows Kits/10

    set(CMAKE_RC_FLAGS "/I\"${_rc_sdk_root}/Include/${_rc_version}/um\" /I\"${_rc_sdk_root}/Include/${_rc_version}/shared\"")
endif ()

set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/SDL)

# --- Filter out GameInput sources from SDL3 (MinGW only) ---
# The Windows GameInput SDK (gameinput.h) is unavailable on MinGW.
# On MSVC, the real GameInput sources compile fine with the Windows SDK.
# We provide stubs in SDL_gameinput_stubs.cpp for the symbols that other
# SDL source files reference unconditionally.
get_target_property(SDL3_SOURCES SDL3::SDL3 SOURCES)
if(MINGW AND SDL3_SOURCES)
    list(FILTER SDL3_SOURCES EXCLUDE REGEX ".*[/\\\\](gameinput|SDL_gameinput|SDL_windowsgameinput|SDL_gameinputjoystick).*")
endif()

if (UNIX)
    find_path(XKBCOMMON_INCLUDE_DIR NAMES xkbcommon/xkbcommon.h)
    find_library(XKBCOMMON_LIB NAMES xkbcommon)

    if(NOT XKBCOMMON_INCLUDE_DIR OR NOT XKBCOMMON_LIB)
        message(FATAL_ERROR "libxkbcommon not found")
    endif()

    file(GLOB wayland_protocols
            ${CMAKE_CURRENT_BINARY_DIR}/OOPWithCpp/3rdParty/Projects/git/SDL/wayland-generated-protocols/*
    )

    find_path(LIBUSB_INCLUDE_DIR
            NAMES libusb.h
            PATH_SUFFIXES libusb-1.0
    )

    find_library(LIBUSB_LIBRARY
            NAMES usb-1.0 usb
    )

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(libusb DEFAULT_MSG LIBUSB_INCLUDE_DIR)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBDECOR REQUIRED IMPORTED_TARGET libdecor-0)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
    pkg_check_modules(FRIBIDI REQUIRED IMPORTED_TARGET fribidi)
    pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)

    set (TargetDirectories
            ${DBUS_INCLUDE_DIRS}
            ${LIBUSB_INCLUDE_DIR}
            ${LIBDECOR_INCLUDE_DIRS}
            ${PIPEWIRE_INCLUDE_DIRS}
            ${FRIBIDI_INCLUDE_DIRS}
            ${DRM_INCLUDE_DIRS}
            ${CMAKE_CURRENT_BINARY_DIR}/OOPWithCpp/3rdParty/Projects/git/SDL/wayland-generated-protocols/
    )

    set (TargetLinks
            ${WAYLAND_LIBRARIES}
            ${XKBCOMMON_LIB}
            ${Polly_LINK_FLAGS}
    )
else ()
    set (TargetDirectories "")
    set (TargetLinks
            imm32
            hid
    )
endif ()

string(TOLOWER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_LOWER)

foreach (Target SDL3_SSE4_2 SDL3_AVX2 SDL3_AVX512)
    add_library(${Target} STATIC)
    target_sources(${Target} PRIVATE
            ${SDL3_SOURCES}
    )
    if (MINGW)
        target_sources(${Target} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/CmakeFiles/SDL_gameinput_stubs.cpp
        )
    endif()

    target_include_directories(${Target} PUBLIC
            ${CMAKE_CURRENT_BINARY_DIR}/OOPWithCpp/3rdParty/Projects/git/SDL/include-config-${CMAKE_BUILD_TYPE_LOWER}/build_config
            $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
            $<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>
            ${CMAKE_CURRENT_SOURCE_DIR}/OOPWithCpp/3rdParty/Projects/git/SDL/src
            ${TargetDirectories}
    )

    set_target_properties(${Target} PROPERTIES
            POSITION_INDEPENDENT_CODE True
            INTERPROCEDURAL_OPTIMIZATION ${LTO_ENABLED}
            OUTPUT_NAME ${Target}
            ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/SDL
            LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/SDL
            RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}/SDL
    )
    target_link_libraries(${Target} PUBLIC ${TargetLinks})
endforeach()

target_compile_options(SDL3_SSE4_2 PRIVATE
        ${SSE42_FLAGS}
        -DSDL_STATIC=1
        -DSDL_SHARED=0
        -DSDL_VULKAN_STATIC=OFF

        # disable avx and above code paths in sse4.2 build
        -DSDL_DISABLE_AVX
        -DSDL_DISABLE_AVX2
        -DSDL_DISABLE_AVX512F
)
target_compile_options(SDL3_AVX2 PRIVATE
        ${AVX2_FLAGS}
        -DSDL_STATIC=1
        -DSDL_SHARED=0
        -DSDL_VULKAN_STATIC=OFF

        # disable avx512 code paths in avx2 build
        -DSDL_DISABLE_AVX512F
)
target_compile_options(SDL3_AVX512 PRIVATE
        ${AVX512_FLAGS}
        -DSDL_STATIC=1
        -DSDL_SHARED=0
        -DSDL_VULKAN_STATIC=OFF
)
