# Bodycam Diagnostics

Native Windows x64 utility for repeatable Bodycam DLSS / Frame Generation diagnostics.

## Current capabilities
- Native C++20 / Win32 UI, no Java/JVM.
- Automatic `Bodycam-Win64-Shipping.exe` detection.
- F9 start / F10 stop global hotkeys.
- Per-test crash-friendly session folders and logs.
- PresentMon capture scoped to the Bodycam PID.
- FFmpeg Desktop Duplication recording with NVIDIA NVENC, split into 15-second MKV chunks.
- Preflight checks for PresentMon, FFmpeg, `h264_nvenc`, and `ddagrab`.
- CPU/RAM and NVIDIA GPU/VRAM/temperature/clock telemetry to `telemetry.csv`.
- Automatic post-test metrics: average FPS, 1% low, 0.1% low, frametime percentiles and stutter counters.
- `summary.json` and `summary.txt` written at the end of each test.
- Automatic `report_bundle.zip` containing the logs, CSV telemetry/frametimes, system snapshot and summaries. Video chunks stay outside the ZIP to keep it small enough to upload for analysis.
- UI buttons to open the latest session folder and copy the report ZIP path.

## Runtime layout
Place external capture tools beside the executable:

```text
BodycamDiagnostics.exe
Tools/
  PresentMon.exe
  ffmpeg.exe
Sessions/
```

Each test creates its own folder inside `Sessions/`.

## Build
Requirements: CMake 3.24+, MSVC x64 Build Tools, and a Windows SDK.

```powershell
./build_windows.ps1
```

GitHub Actions builds `Release` x64 and uploads `BodycamDiagnostics.exe` as an artifact.
