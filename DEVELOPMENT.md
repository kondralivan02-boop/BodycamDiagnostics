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
- Live telemetry sampler: Bodycam CPU/RAM plus NVIDIA GPU load, VRAM, temperature and clocks via dynamically loaded NVML; writes `telemetry.csv` every 500 ms.
- CSV metrics summarizer: average FPS, 1% low, 0.1% low, P50/P95/P99/P99.9 frametimes and 50/100 ms stutter counters.
- Automatic `summary.json` + `summary.txt` generation when a test stops.
- Built-in test profile selector for the four Bodycam comparison variants.
- Report packager: creates `report_bundle.zip` with diagnostics data but excludes video chunks.
- UI shortcuts to open the last session and copy the ZIP path.
- Optional persistent session mirror controlled by `settings.ini` or `BODYCAM_DIAG_MIRROR`; copies diagnostic files continuously and only mirrors MKV segments after their size stabilizes.
- GitHub Actions Windows x64 build is working.

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
1. Chat/upload integration path for sending `report_bundle.zip` directly for analysis.
2. Runtime packaging/documentation for PresentMon + FFmpeg dependencies.
3. Real-PC validation on Bodycam/RTX 4060 Ti, including recording load and NVENC stability.


## Environment diagnostics (October 2026)
- `environment_capabilities.json`: observed per-source status/errors; does not infer Gizmo or claim that all PC sources are readable. Startup success is distinct from capture completeness.
- `windows_events.json`: optional System/Application warnings/errors and WER event 1001, bounded to 256 events/channel and 64 KiB/event. Enumeration uses a 2-second budget/channel; native query/render API calls are synchronous and not hard-cancelled. Query covers session start minus 2 s through observed stop plus 2 s; collection is one-shot, so late delivery and post-reboot evidence can be missed.
- Event/stutter matches use an estimated PresentMon launch plus first-frame-relative timeline, with a 2000 ms interval search margin. Startup delay is unknown; this is not an accuracy guarantee. Clock discontinuity disables matching. A temporal match never proves causation or ownership by Bodycam.
- `bodycam_logs_manifest.json` plus `bodycam_logs/`: optional recent `.log` evidence from `%LOCALAPPDATA%/Bodycam/Saved/Logs`, on normal and crash/exit stops. No filesystem-wide scan or elevation. Select files modified since session start minus 2 minutes; retain at most 8 files, 2 MiB/file (tail), 8 MiB total, 256 scanned entries and a 2-second collection budget. File operations themselves are synchronous. Raw log lines can predate capture; no per-line timestamp alignment is claimed. Other Unreal install paths are not auto-discovered.
- Optional telemetry absence and denied/missing system/log sources are diagnostic limitations, not standalone INVALID reasons. Required PresentMon/video capture and unexpected exit retain their validation checks.
- These reports are included in `report_bundle.zip`. Tests run through CTest in Windows Actions; new test targets compile with `/W4 /WX`.
