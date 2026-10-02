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
    src/CaptureStartupPolicy.cpp
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
target_link_libraries(BodycamDiagnostics PRIVATE comctl32 shell32 shlwapi advapi32 ole32 dwmapi uxtheme wevtapi bcrypt)


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
    add_executable(ToolPreflightTests tests/ToolPreflightTests.cpp src/ToolPreflight.cpp)
    target_include_directories(ToolPreflightTests PRIVATE src)
    target_compile_definitions(ToolPreflightTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(ToolPreflightTests PRIVATE bcrypt)
    add_test(NAME ToolPreflightTests COMMAND ToolPreflightTests)
    add_executable(SessionDiagnosticsReportTests tests/SessionDiagnosticsReportTests.cpp src/SessionDiagnostics.cpp)
    target_include_directories(SessionDiagnosticsReportTests PRIVATE src)
    target_compile_definitions(SessionDiagnosticsReportTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME SessionDiagnosticsReportTests COMMAND SessionDiagnosticsReportTests)
    add_executable(PresentMonExitFixture tests/PresentMonExitFixture.cpp)
    add_executable(PresentMonDelayedExitFixture tests/PresentMonDelayedExitFixture.cpp)
    add_executable(RecorderRecoveryFixture tests/RecorderRecoveryFixture.cpp)
    add_executable(PresentMonControllerTests tests/PresentMonControllerTests.cpp src/PresentMonController.cpp)
    target_include_directories(PresentMonControllerTests PRIVATE src)
    target_compile_definitions(PresentMonControllerTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME PresentMonControllerTests COMMAND PresentMonControllerTests $<TARGET_FILE:PresentMonExitFixture> $<TARGET_FILE:PresentMonDelayedExitFixture>)
    add_executable(RecorderControllerTests tests/RecorderControllerTests.cpp src/RecorderController.cpp)
    target_include_directories(RecorderControllerTests PRIVATE src)
    target_compile_definitions(RecorderControllerTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME RecorderControllerTests COMMAND RecorderControllerTests $<TARGET_FILE:PresentMonExitFixture> $<TARGET_FILE:RecorderRecoveryFixture>)
    add_executable(AudioVideoMuxerFixture tests/AudioVideoMuxerFixture.cpp)
    add_executable(AudioVideoMuxerTests tests/AudioVideoMuxerTests.cpp src/AudioVideoMuxer.cpp)
    target_include_directories(AudioVideoMuxerTests PRIVATE src)
    target_compile_definitions(AudioVideoMuxerTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME AudioVideoMuxerTests COMMAND AudioVideoMuxerTests $<TARGET_FILE:AudioVideoMuxerFixture>)
    add_executable(ReportPackagerTests tests/ReportPackagerTests.cpp src/ReportPackager.cpp)
    target_include_directories(ReportPackagerTests PRIVATE src)
    target_compile_definitions(ReportPackagerTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME ReportPackagerTests COMMAND ReportPackagerTests $<TARGET_FILE:PresentMonExitFixture>)
    add_executable(CaptureStartupPolicyTests tests/CaptureStartupPolicyTests.cpp src/CaptureStartupPolicy.cpp)
    target_include_directories(CaptureStartupPolicyTests PRIVATE src)
    add_test(NAME CaptureStartupPolicyTests COMMAND CaptureStartupPolicyTests)
    add_executable(SessionMirrorTests tests/SessionMirrorTests.cpp src/SessionMirror.cpp)
    target_include_directories(SessionMirrorTests PRIVATE src)
    add_test(NAME SessionMirrorTests COMMAND SessionMirrorTests)
    add_executable(StutterIndicatorTests tests/StutterIndicatorTests.cpp src/StutterCorrelator.cpp)
    target_include_directories(StutterIndicatorTests PRIVATE src)
    target_compile_definitions(StutterIndicatorTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME StutterIndicatorTests COMMAND StutterIndicatorTests)
    add_executable(WindowsDiagnosticsTests tests/WindowsDiagnosticsTests.cpp src/EnvironmentDiagnostics.cpp src/DiagnosticEvidence.cpp src/TestValidator.cpp)
    target_include_directories(WindowsDiagnosticsTests PRIVATE src)
    target_compile_definitions(WindowsDiagnosticsTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(WindowsDiagnosticsTests PRIVATE wevtapi)
    add_test(NAME WindowsDiagnosticsTests COMMAND WindowsDiagnosticsTests)
    if(MSVC)
        target_compile_options(BodycamLogsTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(DiagnosticEvidenceTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(ToolPreflightTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(SessionDiagnosticsReportTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(PresentMonExitFixture PRIVATE /utf-8 /W4 /WX)
        target_compile_options(PresentMonDelayedExitFixture PRIVATE /utf-8 /W4 /WX)
        target_compile_options(RecorderRecoveryFixture PRIVATE /utf-8 /W4 /WX)
        target_compile_options(PresentMonControllerTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(RecorderControllerTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(AudioVideoMuxerFixture PRIVATE /utf-8 /W4 /WX)
        target_compile_options(AudioVideoMuxerTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(ReportPackagerTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(CaptureStartupPolicyTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(SessionMirrorTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(StutterIndicatorTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(WindowsDiagnosticsTests PRIVATE /utf-8 /W4 /WX)
    endif()
endif()
