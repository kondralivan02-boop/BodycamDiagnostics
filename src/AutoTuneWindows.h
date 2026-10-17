#pragma once
#include <Windows.h>
#include <filesystem>

// Explicit, opt-in experiment. No game-process hooks, input automation,
// privilege changes or writes at application startup.
void runGameAutoTune(HWND owner, const std::filesystem::path& dataRoot,
                     bool restore, bool gizmoMode, bool diagnosticsRunning);
