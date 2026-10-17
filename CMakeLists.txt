cmake_minimum_required(VERSION 3.24)
project(BodycamDiagnostics LANGUAGES CXX)

option(BODYCAM_SINGLE_EXE "Embed verified tools and help for offline single-file use" OFF)
set(BODYCAM_PAYLOAD_DIR "" CACHE PATH "Prepared and verified single-EXE payload directory")

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
    src/AppStorage.cpp
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
target_link_libraries(BodycamDiagnostics PRIVATE comctl32 shell32 shlwapi advapi32 ole32 dwmapi uxtheme wevtapi bcrypt uuid)

if(BODYCAM_SINGLE_EXE)
    file(GLOB single_exe_licenses CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/licenses/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/prepare_single_exe.py"
        "${CMAKE_SOURCE_DIR}/runtime-tools.sha256"
        "${CMAKE_SOURCE_DIR}/runtime-tools.sources.json"
        "${CMAKE_SOURCE_DIR}/src/SingleExe.manifest"
        "${CMAKE_SOURCE_DIR}/СПРАВКА.txt" ${single_exe_licenses})
    if(NOT WIN32)
        message(FATAL_ERROR "BODYCAM_SINGLE_EXE requires a Windows target (LLVM-MinGW is supported)")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(NOT BODYCAM_PAYLOAD_DIR)
        set(BODYCAM_PAYLOAD_DIR "${CMAKE_BINARY_DIR}/payload")
    endif()
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/prepare_single_exe.py"
        --repo "${CMAKE_SOURCE_DIR}" --output "${BODYCAM_PAYLOAD_DIR}"
        RESULT_VARIABLE payload_result)
    if(NOT payload_result EQUAL 0)
        message(FATAL_ERROR "Cannot prepare verified embedded runtime payload")
    endif()
    enable_language(RC)
    target_sources(BodycamDiagnostics PRIVATE src/EmbeddedRuntime.cpp "${BODYCAM_PAYLOAD_DIR}/payload.rc")
    target_include_directories(BodycamDiagnostics PRIVATE "${BODYCAM_PAYLOAD_DIR}")
    target_compile_definitions(BodycamDiagnostics PRIVATE BODYCAM_SINGLE_EXE=1)
    add_custom_command(TARGET BodycamDiagnostics POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/verify_single_exe.py"
            "$<TARGET_FILE:BodycamDiagnostics>" "${BODYCAM_PAYLOAD_DIR}/payload.json"
        COMMENT "Verify embedded resources and reject external DLL dependencies")
    if(MINGW)
        target_link_options(BodycamDiagnostics PRIVATE -static)
    elseif(MSVC)
        set_property(TARGET BodycamDiagnostics PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_options(BodycamDiagnostics PRIVATE /MANIFEST:NO)
    endif()
endif()


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
    add_executable(BoundedDiagnosticOutputTests tests/BoundedDiagnosticOutputTests.cpp)
    target_include_directories(BoundedDiagnosticOutputTests PRIVATE src)
    add_test(NAME BoundedDiagnosticOutputTests COMMAND BoundedDiagnosticOutputTests)
    if(WIN32)
        add_executable(AppStorageTests tests/AppStorageTests.cpp src/AppStorage.cpp)
        target_include_directories(AppStorageTests PRIVATE src)
        target_compile_definitions(AppStorageTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(AppStorageTests PRIVATE shell32 ole32 uuid)
        if(MINGW)
            target_link_options(AppStorageTests PRIVATE -static)
        endif()
        add_test(NAME AppStorageTests COMMAND AppStorageTests)
    endif()
    if(BODYCAM_SINGLE_EXE)
        add_test(NAME SingleExePayloadTests COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_single_exe_payload.py")
        add_test(NAME SingleExeInspectionTests COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_single_exe_inspection.py")
        add_test(NAME SingleExeTamperTests COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/check_single_exe_tamper.py" "$<TARGET_FILE:BodycamDiagnostics>" "${BODYCAM_PAYLOAD_DIR}/payload.json")
        add_executable(EmbeddedRuntimeTests tests/EmbeddedRuntimeTests.cpp src/EmbeddedRuntime.cpp src/AppStorage.cpp src/ToolPreflight.cpp "${BODYCAM_PAYLOAD_DIR}/payload.rc")
        target_include_directories(EmbeddedRuntimeTests PRIVATE src "${BODYCAM_PAYLOAD_DIR}" "${CMAKE_BINARY_DIR}/generated")
        target_compile_definitions(EmbeddedRuntimeTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(EmbeddedRuntimeTests PRIVATE bcrypt shell32 ole32 uuid)
        if(MINGW)
            target_link_options(EmbeddedRuntimeTests PRIVATE -static)
        endif()
        add_test(NAME EmbeddedRuntimeTests COMMAND EmbeddedRuntimeTests)
    endif()
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
    add_executable(PresentMonStderrFixture tests/PresentMonStderrFixture.cpp)
    add_executable(PresentMonExitFixture tests/PresentMonExitFixture.cpp)
    add_executable(PresentMonDelayedExitFixture tests/PresentMonDelayedExitFixture.cpp)
    add_executable(RecorderRecoveryFixture tests/RecorderRecoveryFixture.cpp)
    add_executable(PresentMonControllerTests tests/PresentMonControllerTests.cpp src/PresentMonController.cpp)
    target_include_directories(PresentMonControllerTests PRIVATE src)
    target_compile_definitions(PresentMonControllerTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME PresentMonControllerTests COMMAND PresentMonControllerTests $<TARGET_FILE:PresentMonExitFixture> $<TARGET_FILE:PresentMonDelayedExitFixture> $<TARGET_FILE:PresentMonStderrFixture>)
    set_tests_properties(PresentMonControllerTests PROPERTIES TIMEOUT 30)
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
    add_executable(MetricsGapTests tests/MetricsGapTests.cpp src/MetricsCalculator.cpp src/FrameContinuity.cpp src/StutterCorrelator.cpp src/SessionDiagnostics.cpp src/PerformanceHealth.cpp)
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
        target_compile_options(BoundedDiagnosticOutputTests PRIVATE /utf-8 /W4 /WX)
        target_compile_options(PresentMonStderrFixture PRIVATE /utf-8 /W4 /WX)
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
