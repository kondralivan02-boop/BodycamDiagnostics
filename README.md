# Bodycam Diagnostics

Русская инструкция для пользователя находится в `СПРАВКА.txt` & открывается кнопкой «Справка» в программе. Тест запускается F9, запись видео во время теста переключается F8. Видео по умолчанию выключено. Флажок «Режим совместимости с Gizmo» отключает сбор событий Windows, логов игры и полноэкранный помощник; включайте его до начала теста.

Native Windows x64 utility for repeatable Bodycam DLSS / Frame Generation diagnostics.

## Current capabilities
- Native C++20 / Win32 UI, no Java/JVM.
- Automatic `Bodycam-Win64-Shipping.exe` detection.
- F9 global hotkey toggles test start / stop.
- Per-test crash-friendly session folders and logs.
- PresentMon capture scoped to the Bodycam PID.
- FFmpeg Desktop Duplication recording with NVIDIA NVENC, split into 15-second MKV chunks.
- Preflight checks for PresentMon, FFmpeg, trusted SHA-256 values from `runtime-tools.sha256`, `h264_nvenc`, and `ddagrab`.
- CPU/RAM and NVIDIA GPU/VRAM/temperature/clock telemetry to `telemetry.csv`.
- Automatic post-test metrics: average FPS, 1% low, 0.1% low, frametime percentiles and stutter counters.
- `summary.json` and `summary.txt` written at the end of each test.
- Automatic native C++ `report_bundle.zip` containing logs, CSV telemetry/frametimes, system snapshot and summaries; no `tar.exe` or external archiver is required. Video stays outside the ZIP. `video_capture.log` is preserved after mux and included in the report.
- UI buttons to open the latest session folder and copy the report ZIP path.
- Optional crash-survival mirror to another disk, network share or synced folder. Lightweight diagnostics are mirrored every few seconds, and completed 15-second MKV chunks are copied after they stop growing.

## Runtime layout
Preferred layout:

```text
BodycamDiagnostics.exe
runtime-tools.sha256
runtime-tools.sources.json
InstallRuntimeTools.ps1
InstallRuntimeTools.cmd
ValidateEnvironment.ps1
ValidateEnvironment.cmd
Tools/
  PresentMon.exe
  ffmpeg.exe
Sessions/
```

For restricted/Gizmo environments, both tools may instead be placed directly beside `BodycamDiagnostics.exe`. The application prefers `Tools\` when both locations exist. Keep `runtime-tools.sha256` beside the main EXE; runtime tools are not executed unless their hashes match the manifest.

## Runtime tool installer
Run `InstallRuntimeTools.cmd` beside the main EXE to fetch the pinned PresentMon and FFmpeg release assets into `Tools\`. The installer verifies the download SHA-256 values, verifies the extracted executables against `runtime-tools.sha256`, and only then copies them into the runtime folder. The binaries remain external dependencies and are not stored in this repository or attached to BodycamDiagnostics releases.

Each test creates its own folder inside `Sessions/`.

## Environment self-test
Run `ValidateEnvironment.cmd` before the first Bodycam capture on a new PC. It verifies the runtime manifest, PresentMon and FFmpeg hashes, `PresentMon --help`, `h264_nvenc`, `ddagrab`, a short NVENC encode, a real one-frame `ddagrab -> h264_nvenc` capture, write access to `Sessions\\`, and write access to the persistent mirror when one is configured. Results are written to `SelfTest/environment_selftest.json` and `.txt`.

## Optional persistent mirror
Create `settings.ini` beside the EXE:

```ini
[storage]
mirror_path=\\server\share\BodycamDiagnostics
```

You can also set the environment variable `BODYCAM_DIAG_MIRROR`. The INI value takes priority.

The mirror is intended for the club/Gizmo case: choose a location that survives a local PC reset. If the mirror is another local folder on the same reset image, it will not protect the data.

## Build
Requirements: CMake 3.24+, MSVC x64 Build Tools, and a Windows SDK.

```powershell
./build_windows.ps1
```

GitHub Actions builds `Release` x64, runs regression tests, uploads the runtime package as an artifact, and publishes separate GitHub Release assets on verified `stable` pushes.
