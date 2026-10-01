cmake_minimum_required(VERSION 3.24)
project(BodycamDiagnostics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(BodycamDiagnostics WIN32
    src/main.cpp
    src/SessionManager.cpp
    src/ProcessFinder.cpp
    src/SystemSnapshot.cpp
    src/PresentMonController.cpp
    src/RecorderController.cpp
    src/ToolPreflight.cpp
    src/TelemetrySampler.cpp
    src/MetricsCalculator.cpp
    src/ReportPackager.cpp
    src/SessionMirror.cpp
    src/AudioLoopbackRecorder.cpp
    src/AudioVideoMuxer.cpp
    src/LiveFrameMetrics.cpp
    src/DiagnosticsOverlay.cpp
    src/FullscreenOverlayBridge.cpp
)

add_executable(BodycamFullscreenOverlay WIN32
    src/FullscreenOverlayMain.cpp
    src/DiagnosticsOverlay.cpp
)

target_include_directories(BodycamFullscreenOverlay PRIVATE src)
target_compile_definitions(BodycamFullscreenOverlay PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(BodycamFullscreenOverlay PRIVATE user32 gdi32 advapi32)


target_include_directories(BodycamDiagnostics PRIVATE src)
target_compile_definitions(BodycamDiagnostics PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(BodycamDiagnostics PRIVATE comctl32 shell32 shlwapi advapi32 ole32 dwmapi uxtheme)


if(MSVC)
    target_compile_options(BodycamDiagnostics PRIVATE /utf-8)
    target_compile_options(BodycamFullscreenOverlay PRIVATE /utf-8)
    target_link_options(
        BodycamFullscreenOverlay PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/FullscreenOverlay.manifest")
endif()
