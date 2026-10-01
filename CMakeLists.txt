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
)

target_include_directories(BodycamDiagnostics PRIVATE src)
target_compile_definitions(BodycamDiagnostics PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(BodycamDiagnostics PRIVATE comctl32 shell32 shlwapi advapi32)
