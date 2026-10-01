cmake_minimum_required(VERSION 3.28)

project(TranslucentExplorer
    VERSION 0.1.0
    DESCRIPTION "Native Win32 File Explorer-style application with a translucent, tintable interface"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Platform and toolchain guards (constitution Principle I, plan TC-001)
# ---------------------------------------------------------------------------
if(NOT MSVC)
    message(FATAL_ERROR "Translucent Explorer must be built with Microsoft Visual C++ (MSVC).")
endif()

if(CMAKE_GENERATOR MATCHES "^Visual Studio" AND NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR
        "Translucent Explorer targets x64 only. Configure with -A x64 "
        "(the CMakePresets.json presets set this). Current platform: '${CMAKE_GENERATOR_PLATFORM}'.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Translucent Explorer targets x64 only; the active toolchain is not 64-bit.")
endif()

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release builds use whole-program optimization (/GL at compile time, /LTCG for
# the linker and the librarian). CMake applies the flags to every tool that needs them.
include(CheckIPOSupported)
check_ipo_supported(RESULT te_ipo_supported OUTPUT te_ipo_output LANGUAGES CXX)
if(te_ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(WARNING "Link-time code generation is not available: ${te_ipo_output}")
endif()

# ---------------------------------------------------------------------------
# te_warnings: warning, conformance and hardening flags for application code.
# Applied to C++ sources only, so resource (.rc) compilation is not affected.
# Third-party headers included with <...> are compiled at /external:W0, so /WX
# applies only to new application-code warnings (constitution, plan R-13).
# ---------------------------------------------------------------------------
add_library(te_warnings INTERFACE)
target_compile_options(te_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:
        /W4
        /WX
        /permissive-
        /utf-8
        /EHsc
        /Zc:__cplusplus
        /guard:cf
        /sdl
        /external:anglebrackets
        /external:W0
    >
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O2>
)
target_link_options(te_warnings INTERFACE
    /GUARD:CF
    /DYNAMICBASE
)

# ---------------------------------------------------------------------------
# te_defs: Windows SDK configuration shared by every target.
# NTDDI_WIN10_NI = Windows 11 22H2 (build 22621) SDK surface, needed for
# DWMWA_SYSTEMBACKDROP_TYPE (research R-01).
# ---------------------------------------------------------------------------
add_library(te_defs INTERFACE)
target_compile_definitions(te_defs INTERFACE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=NTDDI_WIN10_NI
)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
