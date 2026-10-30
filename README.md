# Bodycam Diagnostics

> **ПРАВА НА ИСХОДНЫЙ КОД / PROPRIETARY — ALL RIGHTS RESERVED.**
> Оригинальные исходники и материалы Bodycam Diagnostics не распространяются
> под открытой лицензией. Публикация на GitHub не разрешает присваивать код,
> выдавать проект за собственную разработку, перерабатывать, распространять
> или продавать его без прямого письменного разрешения правообладателя.
> Исключения, предусмотренные законом, условиями GitHub и действительными
> лицензиями сторонних компонентов, сохраняются. Подробности: [LICENSE](LICENSE).
>
> **EN:** Original project materials are proprietary; public visibility does
> not grant permission to reuse, rebrand, redistribute or monetize them.
> See [LICENSE](LICENSE). Third-party materials retain their own licenses.


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
- Process/system CPU and RAM telemetry to `telemetry.csv`, including the busiest observed process thread.
- Per-process GPU 3D-engine utilization via Windows PDH GPU Engine counters (vendor-neutral when the WDDM driver exposes them); NVIDIA NVML additionally supplies board utilization, VRAM, temperature, clocks and power.
- GPU Engine values are the maximum utilization among this process's 3D engine instances, not a sum and not a whole-GPU utilization figure. Missing counters remain blank rather than being reported as zero.
- Automatic post-test metrics: average FPS, 1% low, 0.1% low, frametime percentiles and stutter counters.
- F9 AutoTune feedback correlates repeated >=100 ms stutters with per-process GPU 3D-engine and maximum single-thread CPU load. It can identify a Performance-profile candidate or CPU/mixed pressure; it never changes game settings automatically. High/Medium/Low are coverage-and-repetition tiers, not statistical probabilities or FPS predictions.
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

## PresentMon failure diagnostics

Capture stdout remains exclusively in the FPS CSV. PresentMon stderr is drained separately and saved to `<csv filename>.stderr.log` when the controller finishes or detects an early exit. The standard `presentmon.csv.stderr.log` is included in the diagnostic report ZIP. The log retains at most 16 KiB plus a truncation marker; startup/health errors include the exit code and at most 4 KiB of sanitized diagnostic text. Existing stderr logs are never overwritten: a collision or setup error refuses the new capture explicitly. The reader does not wait for EOF from descendants, and shutdown drains at most 64 KiB before closing its pipe. OS file operations themselves have no hard cancellation deadline. This improves evidence collection; it does not fix the underlying PresentMon failure or APPCRASH `0xC0000005`.

## Experimental Bodycam game AutoTune (disabled by default)

An isolated, **opt-in** game-configuration prototype is available only if built with
`-DBODYCAM_EXPERIMENTAL_AUTOTUNE=ON`. Production builds are unchanged. In that
experimental build, open **НАСТРОЙКИ** to preview automatic Performance/Balanced/
Quality scalability suggestions or restore the original file. The initial suggestion
uses a vendor-neutral DXGI hardware-adapter inventory (NVIDIA, AMD, Intel and
other DXGI adapters), dedicated and shared memory as separate values, total RAM,
CPU logical-processor count and display resolution. Dedicated-memory values are
normalized near whole-GiB capacity classes using the same bounded tolerance at
every size; shared UMA memory is never counted as dedicated VRAM. On DXGI 1.6
systems, the first hardware adapter in Windows' high-performance preference
order is selected. If multiple adapters exist but that ordering is unavailable,
AutoTune refuses to guess. A per-game Windows GPU preference can still override
the DXGI ordering, so the preview warns when more than one adapter is present.
Integrated/shared-only adapters receive a conservative Performance baseline.
The preview also identifies the CPU model, physical/logical core counts and GPU
vendor/model. For resolution, it prefers a unique, valid
LastUserConfirmedResolutionSizeX/Y pair in GameUserSettings.ini, then a unique
ResolutionSizeX/Y pair; otherwise it falls back to the desktop mode. The
configured output resolution is not treated as proof of internal render
resolution or dynamic-resolution behavior. A High/Medium/Low label describes
the completeness of baseline inputs only; it is not a calibrated FPS prediction.
This remains a capacity-based starting heuristic, **not a measured optimum**:
VRAM and model names alone do not measure GPU compute throughput or CPU
bottlenecks. Compare F9 captures under equivalent conditions before judging.

The prototype looks for one and only one accessible `GameUserSettings.ini` under
`%LOCALAPPDATA%\\Bodycam\\Saved\\Config\\Window`, `Windows` or `WindowsNoEditor`.
It only changes **existing, numeric** `sg.*Quality` entries in a valid
`[ScalabilityGroups]` section, preserving comments, unrelated keys and line
endings. It refuses unsupported/ambiguous/duplicate configurations. It does not
control DLSS, Frame Generation, unknown Bodycam-specific settings, anti-cheat,
mouse, keyboard, game memory or system/Gizmo permissions.

The game and F9 diagnostics must be stopped, Gizmo compatibility mode disabled,
and **the user must explicitly approve a change preview**. Before any change,
a never-overwritten original backup is placed at
`<diagnostics data root>/AutoTuneBackups/<config folder>-GameUserSettings.original.ini`.
The INI is replaced with a flushed temporary file only if its content remained
unchanged since preview. Restore requires a separate explicit confirmation.
The optimizer never bypasses an inaccessible game configuration folder: on
managed club computers an administrator may need to authorize ordinary writes.
A diagnostic data root on D: can be selected via `BODYCAM_DIAG_DATA_ROOT` as
already documented; the game itself still keeps its legitimate config in LocalAppData.

**Validation boundary:** the portable planner test is runnable on Linux; Windows
build, UI, real Bodycam v0.8 config behavior and before/after benchmarking
remain unverified. Keep this experiment off in ordinary builds until tested.
