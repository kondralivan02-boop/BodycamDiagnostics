# Bodycam Diagnostics: release handoff, 12 October 2026

## Scope and status

- Repository: `kondralivan02-boop/BodycamDiagnostics`.
- Work only on `feature/recording-gizmo-help`, PR #3 (Draft, do not merge before validation).
- On 12 October, before this handoff, the feature head was `16b1a25c7119cad904e4c4c8249fe6490062754e`.
- Last observed independent branches: `stable` = `c238eb102f9c89e603aaa4844628470fc40cea43`; `main` = `119a63c4ee63f3dc48f0dcc447265d65d80b2d1d`. Refresh before any future merge; the PR base may have moved.
- **No GitHub Actions runs** until the owner explicitly approves. Codex build environment was unavailable due to user-reported limits. Do not spend Actions minutes as a workaround.
- AutoTune/automatic game configuration optimization is **deferred**, not part of this PR. Keep the stable build intact.

## Confirmed source changes since the last independently compiled commit

The local LLVM-MinGW cross-build previously succeeded only for `7b35724`, not for the commits below. Windows CTest and gameplay tests for the current feature head **have not been executed**.

- `3faf369`: store WASAPI audio at `video/part_N/game_audio.wav`, use shared paths and per-recording audio completion state, add multi-part mux tests.
- `471a2a5`: reject a WAV capture if no audio frames were received; add missing-WAV fallback test.
- `d60b1fe`: when FFmpeg fails to mux audio, retry video-only; retain raw video on double failure; add fixture cases.
- `16b1a25`: nonthrowing cleanup in the audio retry path, display a generic video warning instead of incorrectly labeling it a cleanup warning.
- This handoff updates the stable Release manifest to include `СПРАВКА.txt` (the normal `dist/` artifact already included it).

**Verification done:** live GitHub branch/PR reads, source inspection, exact-text consistency checks of edited files. These are static checks, **not** compilation, unit test execution, or on-device proof.

## Next engineer: validation gate (no redundant broad redesign)

1. Inspect HEAD, working tree, PR base and current `main`; do not overwrite concurrent changes. Read this file and `docs/superpowers/plans/2026-10-02-recording-gizmo-help.md`.
2. Use an authorized build-capable environment with MSVC x64, Windows SDK, CMake 3.24+. On Windows run `./build_windows.ps1`. The script builds Release but does **not** run tests.
3. Run `ctest --test-dir build -C Release --output-on-failure`. Then run `tests/RuntimeToolSourceTests.ps1` and `tests/EnvironmentSelfTestTests.ps1` and validate generated diagnostic JSON. **Do not substitute the old `7b35724` compilation result for this check.**
4. Pay particular attention to `AudioVideoMuxerTests`, `WindowsDiagnosticsTests`, `RecordingTimelineTests`, `RecorderControllerTests`, and `ReportPackagerTests`; review warnings as well as process exit codes.
5. Check the portable package includes `BodycamDiagnostics.exe`, `BodycamFullscreenOverlay.exe`, `СПРАВКА.txt`, runtime trust manifest, installer/validation scripts, and documented runtime-tool layout. Separately check the stable GitHub Release's declared asset list includes `СПРАВКА.txt`; **do not publish a release** until all gates are satisfied.
6. When a suitable machine is available, run `ValidateEnvironment.cmd` with the pinned, hash-verified `PresentMon.exe` and `ffmpeg.exe`. Test F9 diagnostics with F8 off, then F8 on/off twice in one session. Confirm two finalized videos, correct per-part WASAPI sound, and `validation.json` / `report_bundle.zip`. Test optional audio failure producing video-only output. Verify that FFmpeg double failure retains raw segments and diagnostics.
7. Finally, do an actual Bodycam session after the club's Gizmo update. Test Gizmo mode without bypassing club security; don't confuse successful compilation with successful runtime operation. Keep PR #3 Draft until these tests pass.

## Important boundaries

- F9 toggles the diagnostic session, F8 independently toggles optional video; video is off by default.
- PresentMon remains mandatory; FFmpeg optional unless video is actively requested.
- Do not weaken SHA-256 runtime-tool verification, manipulate anti-cheat/Gizmo, use hidden hooks, or silently enable Windows Event Log collection in Gizmo mode.
- A report ZIP deliberately excludes full video files; finalized video and raw recovery fragments remain in the session directory. Capture logs belong in the report.
- If a new fault appears, reproduce with a targeted test before changing code. Record exact results and affected commit. Do not claim `СОБРАНО` or `ПРОВЕРЕНО НА УСТРОЙСТВЕ` without evidence.
