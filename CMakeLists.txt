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
    src/TestValidator.cpp
    src/CrashCollector.cpp
    src/PerformanceHealth.cpp
    src/StutterCorrelator.cpp
    src/BodycamLogs.cpp
    src/EnvironmentDiagnostics.cpp
    src/DiagnosticEvidence.cpp
    src/SessionDiagnostics.cpp
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
target_link_libraries(BodycamDiagnostics PRIVATE comctl32 shell32 shlwapi advapi32 ole32 dwmapi uxtheme wevtapi)


if(MSVC)
    target_compile_options(BodycamDiagnostics PRIVATE /utf-8)
    target_compile_options(BodycamFullscreenOverlay PRIVATE /utf-8)
    target_link_options(
        BodycamFullscreenOverlay PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTUAC:level='asInvoker' uiAccess='true'")
endif()


include(CTest)
if(BUILD_TESTING)
    add_executable(BodycamLogsTests tests/BodycamLogsTests.cpp src/BodycamLogs.cpp src/DiagnosticEvidence.cpp)
    target_include_directories(BodycamLogsTests PRIVATE src)
    target_compile_definitions(BodycamLogsTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME BodycamLogsTests COMMAND BodycamLogsTests)
    add_executable(DiagnosticEvidenceTests tests/DiagnosticEvidenceTests.cpp src/DiagnosticEvidence.cpp)
    target_include_directories(DiagnosticEvidenceTests PRIVATE src)
    add_test(NAME DiagnosticEvidenceTests COMMAND DiagnosticEvidenceTests)
    add_executable(WindowsDiagnosticsTests tests/WindowsDiagnosticsTests.cpp src/EnvironmentDiagnostics.cpp src/DiagnosticEvidence.cpp src/TestValidator.cpp)
    target_include_directories(WindowsDiagnosticsTests PRIVATE src)
    target_compile_definitions(WindowsDiagnosticsTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(WindowsDiagnosticsTests PRIVATE wevtapi)
    add_test(NAME WindowsDiagnosticsTests COMMAND WindowsDiagnosticsTests)
    if(MSVC)
        target_compile_options(BodycamLogsTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(DiagnosticEvidenceTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(WindowsDiagnosticsTests PRIVATE /utf-8 /W4 /WX)
    endif()
endif()
