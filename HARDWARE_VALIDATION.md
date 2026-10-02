# Hardware validation protocol

Target environment for the current Bodycam diagnostics campaign:

- GPU: NVIDIA GeForce RTX 4060 Ti
- Display: 1920x1080, 165 Hz
- Game: Bodycam
- Club/Gizmo environment where the local Windows image may be reset after a crash

## 1. Prepare a fresh machine

Place the stable release files in one writable folder.

Run:

```text
InstallRuntimeTools.cmd
ValidateEnvironment.cmd
```

Do not start the Bodycam capture sequence unless the self-test reports `Ready: YES`.

If a persistent mirror is configured, the self-test must also report it as writable.

Optional fullscreen HUD:

```text
EnableFullscreenOverlay.cmd
```

This requires one administrator approval because Windows UIAccess requires the helper to be installed and locally signed in a trusted location.

## 2. Capture matrix

Run one separate diagnostics session for each configuration:

1. DLSS Quality + Frame Generation OFF
2. DLSS Quality + AMD FSR Frame Generation
3. DLSS Quality + NVIDIA DLSS Frame Generation 2x
4. DLSS Quality + Smooth Motion ON — optional comparison

Keep all unrelated graphics settings unchanged between runs.

Use the same gameplay route or scenario when practical. Do not compare lobby-only data against match gameplay.

## 3. Per-run procedure

1. Start Bodycam and enter the intended gameplay scenario.
2. Select the matching preset label in Bodycam Diagnostics.
3. Press F9 once to start capture.
4. Play for long enough to include normal movement plus at least several representative frametime spikes.
5. Press F9 again to stop capture.
6. Wait until the application reports that the session/report is complete.
7. Confirm the session folder and persistent mirror, when configured, both contain the expected diagnostics.

Do not enable the NVIDIA recording overlay for this validation. Bodycam Diagnostics performs its own FFmpeg/NVENC capture.

## 4. Required evidence after each run

A usable run should contain, when the corresponding source was available:

- `build_info.json`
- `session.log`
- `system.json`
- `environment_capabilities.json`
- `presentmon.csv`
- `telemetry.csv`
- `summary.json` and `summary.txt`
- `validation.json` and `validation.txt`
- `performance_health.json` and `performance_health.txt`
- `stutter_events.json` and `stutter_events.txt`
- `session_diagnostics.json` and `session_diagnostics.txt`
- `windows_events.json`
- `video_capture.log`
- `video_with_audio.mkv` when video and WASAPI audio were both successfully captured
- `video_without_audio.mkv` when video capture succeeded but optional WASAPI audio was unavailable
- `report_bundle.zip`

If muxing fails, raw video chunks and audio are intentionally retained instead of being deleted.

## 5. Acceptance checks

Before performance comparisons are trusted:

- `build_info.json` must identify the exact commit SHA used for the capture

- capture validation must not report a required-source failure
- PresentMon must remain alive after startup
- FFmpeg must remain alive after startup
- `video_capture.log` must survive successful muxing and be present in the report bundle
- a configured persistent mirror must remain writable throughout the test
- any unexpected Bodycam exit must leave recoverable diagnostics in the session/mirror
- diagnostic indicators are correlations only; they are not proof of root cause

## 6. Comparison rule

Compare the four configurations using the same evidence hierarchy:

1. input feel and observable mouse latency
2. frametime distribution and stutter events
3. 1% low and 0.1% low
4. base FPS versus generated-frame FPS
5. GPU/CPU telemetry around the same stutter events
6. video bookmarks only when timeline alignment is reported as available

Do not treat generated FPS alone as a smoothness result.


## 7. External infrastructure contamination

The report now writes `infrastructure_evidence.json` and
`infrastructure_evidence.txt` from the same optional Windows Event Log
evidence already captured for the session.

The summary specifically counts observed iSCSI errors, Realtek adapter-reset
evidence, disk Event ID 153 retries and DNS Client Event ID 1014 timeouts.

`EXTERNAL INSTABILITY OBSERVED` means that multiple independent Windows
event sources formed a network/storage-path fault pattern during the session.
It marks the run as unsuitable for clean DLSS/FG comparison unless the user
can isolate a stable gameplay interval.

This is evidence classification only. It does not attribute root cause to
Bodycam, Gizmo, the NIC, a driver, a switch, the iSCSI server or storage.
