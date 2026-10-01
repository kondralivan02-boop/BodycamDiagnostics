# Bodycam Diagnostics — development checkpoint

## Implemented
- Native C++20 / Win32 GUI.
- Automatic lookup of `Bodycam-Win64-Shipping.exe` and PID.
- F9 start / F10 stop global hotkeys.
- Per-test session folder.
- Crash-friendly `session.log` (flush after every event).
- Atomic `session.json` state (`running` / `stopped`).
- `system.json`: Windows version/build, RAM, logical processors, display adapters.
- PresentMon controller: capture only Bodycam PID to `presentmon.csv` using v2 metrics.
- FFmpeg controller: Windows Desktop Duplication (`ddagrab`) -> `h264_nvenc`.
- Recording split into 15-second MKV chunks to reduce loss on a hard crash.
- Graceful FFmpeg shutdown via stdin `q`, with forced termination only as fallback.
- Tool preflight: verifies PresentMon, FFmpeg, `h264_nvenc`, and `ddagrab` before a test starts.
- Live telemetry sampler: Bodycam CPU/RAM plus NVIDIA GPU load, VRAM, temperature and clocks via dynamically loaded NVML; writes `telemetry.csv` once per second.
- CSV metrics summarizer: average FPS, 1% low, 0.1% low, P50/P95/P99/P99.9 frametimes and 50/100 ms stutter counters.
- Automatic `summary.json` + `summary.txt` generation when a test stops.
- Built-in test profile selector for the four Bodycam comparison variants.
- Report packager: creates `report_bundle.zip` with diagnostics data but excludes video chunks.
- UI shortcuts to open the last session and copy the ZIP path.
- GitHub Actions Windows x64 build is working and has produced a valid `BodycamDiagnostics.exe`.

## Runtime session output
```
Sessions/
  <timestamp>_<preset>/
    session.log
    session.json
    system.json
    presentmon.csv
    telemetry.csv
    summary.json
    summary.txt
    report_bundle.zip
    video/
      video_00000.mkv
      video_00001.mkv
      ...
```

## Next modules
1. Optional remote/off-machine persistence for the club/Gizmo environment.
2. Chat/upload integration path for sending `report_bundle.zip` directly for analysis.
3. Runtime packaging/documentation for PresentMon + FFmpeg dependencies.
