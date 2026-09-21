cmake_minimum_required(VERSION 3.24)
project(BetterXcloudDLSS5 VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Better Xcloud DLSS5 currently targets Windows x64 only.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "MSVC / Visual Studio 2022 or newer is required.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(DLSS5_UPSTREAM "${CMAKE_SOURCE_DIR}/external/dlss5-video-player" CACHE PATH
    "Path to the pinned 2600th/dlss5-video-player checkout")
set(DLSS_SDK "${CMAKE_SOURCE_DIR}/external/DLSS" CACHE PATH
    "Path to the pinned NVIDIA/DLSS checkout")

if(NOT EXISTS "${DLSS5_UPSTREAM}/src/D3D12Renderer.cpp")
    message(FATAL_ERROR "DLSS5 Video Player source not found at ${DLSS5_UPSTREAM}")
endif()
if(NOT EXISTS "${DLSS_SDK}/include/nvsdk_ngx.h")
    message(FATAL_ERROR "NVIDIA DLSS SDK not found at ${DLSS_SDK}")
endif()

set(DLSS5_SRC "${DLSS5_UPSTREAM}/src")
set(NVOF_SDK "${DLSS5_UPSTREAM}/external/nvof")
set(DLSS_LIB_DIR "${DLSS_SDK}/lib/Windows_x86_64/x64")
set(DLSS_NGX_LIBRARY "$<IF:$<CONFIG:Debug>,nvsdk_ngx_d_dbg,nvsdk_ngx_d>")

add_executable(XCloudDLSS5Host WIN32
    src/main.cpp
    src/WindowFinder.cpp
    src/WindowFinder.h
    src/WindowCapture.cpp
    src/WindowCapture.h

    # Reuse the proven renderer/guide path from the pinned upstream project.
    "${DLSS5_SRC}/D3D12Renderer.cpp"
    "${DLSS5_SRC}/DLSSBackend.cpp"
    "${DLSS5_SRC}/TemporalGuides.cpp"
    "${DLSS5_SRC}/OpticalFlowNvof.cpp"
    "${DLSS5_SRC}/RuntimePolicy.cpp"
    "${DLSS5_SRC}/GpuPreference.cpp"
)

target_include_directories(XCloudDLSS5Host PRIVATE
    src
    "${DLSS5_SRC}"
    "${DLSS_SDK}/include"
)

if(EXISTS "${NVOF_SDK}/nvOpticalFlowD3D12.h")
    target_include_directories(XCloudDLSS5Host PRIVATE "${NVOF_SDK}")
    target_compile_definitions(XCloudDLSS5Host PRIVATE DLSS_VIDEO_PLAYER_HAS_NVOF)
    message(STATUS "NVIDIA Optical Flow headers found; NVOFA enabled.")
else()
    message(WARNING "NVOF headers missing; CPU guide fallback will be used.")
endif()

target_compile_definitions(XCloudDLSS5Host PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00
)
target_compile_options(XCloudDLSS5Host PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
target_link_directories(XCloudDLSS5Host PRIVATE "${DLSS_LIB_DIR}")

target_link_libraries(XCloudDLSS5Host PRIVATE
    d3d11 d3d12 dxgi dxguid d3dcompiler
    user32 gdi32 dwmapi shell32 shlwapi ole32 version
    winmm winhttp windowsapp
    ${DLSS_NGX_LIBRARY}
)

# The executable is deliberately the only project-produced binary. At install
# time it is placed inside the upstream neural-runtime directory so the
# upstream ReShade/RenoDX proxy is local to this process, not the browser.
set_target_properties(XCloudDLSS5Host PROPERTIES
    OUTPUT_NAME "XCloudDLSS5Host"
)
