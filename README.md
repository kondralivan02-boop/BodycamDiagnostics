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

## Optional offline single-file build

`BODYCAM_SINGLE_EXE=ON` embeds verified PresentMon/FFmpeg, help and licenses and links compiler runtime statically. The user receives only `BodycamDiagnostics.exe`; it unpacks checked resources to the current user's LocalAppData folder by default, without installation, runtime Internet access or administrator privileges. This mode uses the built-in window/Borderless HUD. The ordinary build remains the default.

### Keep Bodycam Diagnostics data on D:

Set the per-process `BODYCAM_DIAG_DATA_ROOT` environment variable **before launching** the single-file executable. This affects both unpacked runtime tools and diagnostic sessions (including reports and video). Example in PowerShell, using the real downloaded EXE path:

```powershell
$env:BODYCAM_DIAG_DATA_ROOT = 'D:\BodycamDiagnosticsData'
& 'D:\Downloads\BodycamDiagnostics\BodycamDiagnostics.exe'
```

Expected application-owned files: `D:\BodycamDiagnosticsData\Runtime\<payload-id>\` and `D:\BodycamDiagnosticsData\Sessions\`. No permanent environment setting or adjacent configuration file is required. The override is deliberately **fail-closed**: if set to an empty, relative, UNC, drive-root-only, inaccessible or reparse/junction path, the app displays an error and does not silently fall back to C:. The override must point to a writable directory on an accessible local drive. Run the command each time in a new PowerShell session, or set up a trusted shortcut that supplies the environment variable.

This controls **the application's own runtime and session paths**, not OS-managed locations. Windows, PowerShell, antivirus, GPU drivers and tools may still access the system drive; no guarantee of zero Windows-level C: I/O is made. Do not run this experimental build in a Gizmo-managed club until the administrator authorizes it and compatibility has been verified.

For reproducible Linux LLVM-MinGW building and explicit validation/licensing limitations, see [single-file build notes](docs/superpowers/plans/2026-10-12-single-exe-build-notes.md). This evaluation build is not an approved public release.
