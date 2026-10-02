cmake_minimum_required(VERSION 3.24)
project(BodycamDiagnostics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(
    SHA256
    "${CMAKE_SOURCE_DIR}/runtime-tools.sha256"
    BODYCAM_RUNTIME_TOOLS_MANIFEST_SHA256)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/RuntimeToolTrustAnchor.h.in"
    "${CMAKE_BINARY_DIR}/generated/RuntimeToolTrustAnchor.h"
    @ONLY)

set(BODYCAM_BUILD_SHA "unknown" CACHE STRING "BodycamDiagnostics source commit")
set(BODYCAM_BUILD_BRANCH "unknown" CACHE STRING "BodycamDiagnostics source branch")
set(BODYCAM_BUILD_RUN "unknown" CACHE STRING "BodycamDiagnostics CI run")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/BuildInfoGenerated.h.in"
    "${CMAKE_BINARY_DIR}/generated/BuildInfoGenerated.h"
    @ONLY)

add_executable(BodycamDiagnostics WIN32
    src/main.cpp
    src/SessionManager.cpp
    src/ProcessFinder.cpp
    src/SystemSnapshot.cpp
    src/PresentMonController.cpp
    src/RecorderController.cpp
    src/RecordingTimeline.cpp
    src/ToolPreflight.cpp
    src/TelemetrySampler.cpp
    src/MetricsCalculator.cpp
    src/FrameContinuity.cpp
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
    src/BuildInfo.cpp
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


target_include_directories(
    BodycamDiagnostics PRIVATE
    src
    "${CMAKE_BINARY_DIR}/generated")
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
    add_executable(AtomicOutputFileTests tests/AtomicOutputFileTests.cpp)
    target_include_directories(AtomicOutputFileTests PRIVATE src)
    target_compile_definitions(AtomicOutputFileTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME AtomicOutputFileTests COMMAND AtomicOutputFileTests)
    add_executable(BuildInfoTests tests/BuildInfoTests.cpp src/BuildInfo.cpp)
    target_include_directories(BuildInfoTests PRIVATE src "${CMAKE_BINARY_DIR}/generated")
    add_test(NAME BuildInfoTests COMMAND BuildInfoTests)
    add_executable(BodycamLogsTests tests/BodycamLogsTests.cpp src/BodycamLogs.cpp src/DiagnosticEvidence.cpp)
    target_include_directories(BodycamLogsTests PRIVATE src)
    target_compile_definitions(BodycamLogsTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME BodycamLogsTests COMMAND BodycamLogsTests)
    add_executable(DiagnosticEvidenceTests tests/DiagnosticEvidenceTests.cpp src/DiagnosticEvidence.cpp)
    target_include_directories(DiagnosticEvidenceTests PRIVATE src)
    add_test(NAME DiagnosticEvidenceTests COMMAND DiagnosticEvidenceTests)
    add_executable(ToolPreflightTests tests/ToolPreflightTests.cpp src/ToolPreflight.cpp)
    target_include_directories(
        ToolPreflightTests PRIVATE
        src
        "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(ToolPreflightTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(ToolPreflightTests PRIVATE bcrypt)
    add_test(NAME ToolPreflightTests COMMAND ToolPreflightTests "${CMAKE_SOURCE_DIR}/runtime-tools.sha256")
    add_executable(SessionManagerTests tests/SessionManagerTests.cpp src/SessionManager.cpp src/BuildInfo.cpp)
    target_include_directories(SessionManagerTests PRIVATE src "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(SessionManagerTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME SessionManagerTests COMMAND SessionManagerTests)
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
    add_executable(RecordingTimelineTests tests/RecordingTimelineTests.cpp src/RecordingTimeline.cpp)
    target_include_directories(RecordingTimelineTests PRIVATE src)
    add_test(NAME RecordingTimelineTests COMMAND RecordingTimelineTests)
    add_executable(CollectionPolicyTests tests/CollectionPolicyTests.cpp)
    target_include_directories(CollectionPolicyTests PRIVATE src)
    add_test(NAME CollectionPolicyTests COMMAND CollectionPolicyTests)
    add_executable(SessionMirrorTests tests/SessionMirrorTests.cpp src/SessionMirror.cpp)
    target_include_directories(SessionMirrorTests PRIVATE src)
    add_test(NAME SessionMirrorTests COMMAND SessionMirrorTests)
    add_executable(FrameContinuityTests tests/FrameContinuityTests.cpp src/FrameContinuity.cpp)
    target_include_directories(FrameContinuityTests PRIVATE src)
    add_test(NAME FrameContinuityTests COMMAND FrameContinuityTests)
    add_executable(MetricsGapTests tests/MetricsGapTests.cpp src/MetricsCalculator.cpp src/FrameContinuity.cpp src/StutterCorrelator.cpp src/SessionDiagnostics.cpp)
    target_include_directories(MetricsGapTests PRIVATE src)
    target_compile_definitions(MetricsGapTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME MetricsGapTests COMMAND MetricsGapTests)
    add_executable(StutterIndicatorTests tests/StutterIndicatorTests.cpp src/StutterCorrelator.cpp src/FrameContinuity.cpp)
    target_include_directories(StutterIndicatorTests PRIVATE src)
    target_compile_definitions(StutterIndicatorTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME StutterIndicatorTests COMMAND StutterIndicatorTests)
    add_executable(ValidatorFfmpegFixture tests/ValidatorFfmpegFixture.cpp)
    add_executable(WindowsDiagnosticsTests tests/WindowsDiagnosticsTests.cpp src/EnvironmentDiagnostics.cpp src/DiagnosticEvidence.cpp src/TestValidator.cpp src/ToolPreflight.cpp)
    target_include_directories(
        WindowsDiagnosticsTests PRIVATE
        src
        "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(WindowsDiagnosticsTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(WindowsDiagnosticsTests PRIVATE wevtapi bcrypt)
    add_test(NAME WindowsDiagnosticsTests COMMAND WindowsDiagnosticsTests $<TARGET_FILE:ValidatorFfmpegFixture>)
    if(MSVC)
        target_compile_options(AtomicOutputFileTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(BuildInfoTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(BodycamLogsTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(DiagnosticEvidenceTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(ToolPreflightTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(SessionManagerTests PRIVATE /utf-8 /W4 /WX)
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
        target_compile_options(RecordingTimelineTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(CollectionPolicyTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(SessionMirrorTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(StutterIndicatorTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(FrameContinuityTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(MetricsGapTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(ValidatorFfmpegFixture PRIVATE /utf-8 /W4 /WX)
        target_compile_options(WindowsDiagnosticsTests PRIVATE /utf-8 /W4 /WX)
    endif()
endif()
