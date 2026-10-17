# Single-file build and validation

Implementation of `2026-10-12-codex-one-file-exe.md` on the existing isolated cloud checkout, branch `feature/recording-gizmo-help`. Do not create a worktree, trigger Actions, change main/stable or merge PR #3.

## Reproduce in Codex Linux

```sh
./tools/build_single_exe.sh
```

The script installs build-only CMake 3.31.6 and pefile 2024.8.26 in a workspace virtualenv; obtains checksum-verified LLVM-MinGW 20260922; downloads only the two existing pinned runtime sources; verifies archive and executable SHA-256 values. `BODYCAM_CHECK_ROOT` selects a scratch directory outside the checkout. `LLVM_MINGW_ROOT` can select an already verified toolchain. The default output is `/workspace/bodycam-onefile-build/build/BodycamDiagnostics.exe`.

CMake `BODYCAM_SINGLE_EXE=ON` embeds five RCDATA resources: PresentMon, FFmpeg, the original hash manifest, help and third-party notices. Every resource has a compiled SHA-256. The post-link check verifies exact resource bytes, AMD64 GUI format, explicit `asInvoker`/`uiAccess=false` manifest and system-only imports for all three executables. The application links LLVM C++/unwind runtime statically. The default `OFF` mode and existing targets remain available. Host Python tests run during Linux validation; Windows test executables are not runnable without Windows or an explicitly configured emulator.

## Runtime

There is no runtime download, installer, adjacent DLL or elevation. By default, assets are written atomically beneath the current user's known LocalAppData folder:

`%LOCALAPPDATA%\BodycamDiagnostics\Runtime\<payload SHA-256>\`

An explicit per-process `BODYCAM_DIAG_DATA_ROOT=D:\BodycamDiagnosticsData` override instead stores the runtime at `D:\BodycamDiagnosticsData\Runtime\<payload SHA-256>\` and sessions at `D:\BodycamDiagnosticsData\Sessions\`. The same root-selection function is used for both. A configured but invalid root fails closed, never silently falling back to LocalAppData. Only fully qualified, non-root-only paths on an accessible local drive are allowed; path traversal, junctions, symlinks, non-directories and inaccessible directories are rejected. The configured Sessions path is safely created before use. Normal/default storage behavior is unchanged.

Each cached file is hashed before reuse. A mismatch, reparse path, inaccessible directory or another instance holding the extraction lock gives a visible error and prevents tool execution. Existing mismatching files are never overwritten. The mutex-equivalent file lock serializes unpacking; it never deletes diagnostic data. The generated trust anchor is still enforced, and existing preflight/finalization trust checks use the extracted manifest. ToolPreflight is unchanged.

Single-file sessions default to `%LOCALAPPDATA%\BodycamDiagnostics\Sessions\` unless `BODYCAM_DIAG_DATA_ROOT` selects a valid local root; their actual paths appear in the event log. The ordinary build keeps writable adjacent Sessions and falls back to the user profile if that location is unavailable. SessionManager already allocates unique session directories, preserving earlier logs and recordings. Help opens extracted text; its menu also opens the complete embedded license notices offline. Single-file mode uses the built-in window/Borderless HUD and logs the absence of a signed UIAccess helper. Gizmo policy remains unchanged.

## Verification boundaries

Python tests exercise verified offline cache reuse, bad downloads, conflicting manifests, extracted hash failure, duplicate archive tools required MinGW runtime notices and import policy. Three real-PE tamper checks reject corrupt resource bytes, changed elevation manifest and non-system DLL imports. Windows `EmbeddedRuntimeTests` covers unpacking, idempotent timestamps, trusted manifest, all extracted SHA-256 values, refusal of tampered cache and blocked directory. `AppStorageTests` covers per-user storage, portable legacy storage and blocked Sessions preserving user files. These Windows integration tests must execute on Windows; cross-compilation is not a passing runtime result.

The pinned FFmpeg archive identifies GPLv3 and FFmpeg commit `946fcce07b`. PresentMon's tagged MIT license and LLVM runtime notices are embedded along with the complete FFmpeg GPL license and upstream build README. Exact Corresponding Source for all libraries statically linked into the upstream FFmpeg binary has not been confirmed. This is a private evaluation build, not an approved public redistribution. Public release requires a GPL-compliant source distribution channel covering those sources and scripts.

Windows MSVC build, actual no-admin/denied ACL/reparse behavior, UI/DPI, F8/F9, NVENC/WASAPI, PresentMon, actual report ZIP and club/Gizmo behavior still require target-machine validation. Never substitute PE/resource checks for that validation.

The video validator must receive the exact trusted FFmpeg path from the active runtime. In single-file mode FFmpeg lives under `%LOCALAPPDATA%\\BodycamDiagnostics\\Runtime\\<payload>\\`, not beside the `Sessions` directory; falling back to the legacy portable lookup would incorrectly mark recorded single-file sessions INVALID during duration probing.
