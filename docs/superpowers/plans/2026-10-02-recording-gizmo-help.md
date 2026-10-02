# Recording, Gizmo Mode, and Russian Help Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run diagnostics independently of optional video, explain all controls in Russian, and reduce optional system reads for old Gizmo.

**Architecture:** Keep PresentMon as the mandatory source. A small recording timeline owns enabled intervals and status; the window starts/stops FFmpeg on demand. Reports consume the timeline and a shared frame-gap classifier. Compatibility mode gates optional collectors at their call sites.

**Tech Stack:** C++20, Win32, CMake/CTest, Windows CI.

**Spec:** `docs/superpowers/specs/2026-10-02-recording-gizmo-help-design.md`

## Контрольная точка — 2 октября 2026

- Работа сохранена в `feature/recording-gizmo-help`, черновик PR #3, исходная база `402ca37`.
- Код задач 1–5 реализован. Видео запускается по F8, каждое включение сохраняется отдельным MKV; неудачные попытки отмечаются в отчёте. После ошибки запуска остатки сохраняются отдельно перед повторной записью.
- Короткие ролики также проверяются через доверенный FFmpeg. Факт сбоя сохраняется даже после успешной повторной записи.
- Задача 6: в отдельном документе описана оценка одного EXE; основной формат остаётся переносимой папкой с проверяемыми инструментами.
- Локально проверены переносимые тесты политики запуска, интервалов записи, классификации разрывов & режима Gizmo. Последняя полная Windows проверка прошла на промежуточном `ec1a784`; последующие правки Windows проверку ещё не прошли.
- Блокировка: Windows & временное Ubuntu задание в запусках Actions `37019779717` / `37019784688` завершаются до первого шага, без доступного лога. Проверочное задание удалено.
- Следующее действие: получить сообщение об ошибке со страницы Actions после входа владельца, восстановить запуск, проверить полный CTest & пакет, затем провести контрольный тест в клубе после обновления Gizmo. До этого PR остаётся черновиком.

## Global Constraints

- F9 toggles test; F8 toggles video during an active test; video starts off.
- Do not mask or bypass Gizmo; keep PresentMon and basic telemetry.
- The primary portable package retains SHA-256 verified external tools.
- UTF-8 `СПРАВКА.txt` ships with the Windows runtime package.

## Review Focus

- Repeated start/stop video within one test must not overwrite earlier video or hide missing intervals; Task 2 tests multiple intervals.
- FFmpeg missing when video is off must allow diagnostics, while pressing record explains the failure; Task 1 and 2 tests.
- An FFmpeg exit after recording starts must preserve the diagnostic session; Task 2 tests loss.
- A timestamp rewind or large gap must not become a 1% low or correlated stutter; Task 3 tests both reports.
- Compatibility mode must skip all three optional system sources without claiming their absence is an error; Task 4 tests the policy.

---

### Task 1: Optional video startup policy

**Files:** Modify `src/CaptureStartupPolicy.{h,cpp}`, `tests/CaptureStartupPolicyTests.cpp`, `src/main.cpp`.

**Interfaces:** `requiredCaptureSourcesReady(bool presentMonStarted, bool mirrorConfigured, bool mirrorStarted) noexcept` remains mandatory-source gate.

- [ ] Write failing test: PresentMon true with no recorder allows start; missing PresentMon or failed configured mirror rejects.
- [ ] Run `CaptureStartupPolicyTests` and observe failure.
- [ ] Remove recorder from mandatory gate and preflight; start session with video disabled.
- [ ] Run test and confirm pass.
- [ ] Commit.

### Task 2: Recording control and timeline

**Files:** Create `src/RecordingTimeline.{h,cpp}`, `tests/RecordingTimelineTests.cpp`; modify `src/main.cpp`, `src/RecorderController.cpp`, `src/TestValidator.{h,cpp}`, `CMakeLists.txt`.

**Interfaces:** `RecordingTimeline::start(time_point)`, `stop(time_point)`, `lost(time_point)`, `intervals()`, `requestedSeconds()`; `validateTestSession(..., const std::vector<RecordingInterval>& intervals)`.

- [ ] Write failing test: default disabled, two distinct intervals, loss state, recorded duration excluding disabled time, optional video validation.
- [ ] Run tests and observe failure.
- [ ] Add F8/button, trusted FFmpeg check on record, stop/loss without ending test, controlled recorder state and interval output in session/report JSON.
- [ ] Run tests and confirm pass.
- [ ] Commit.

### Task 3: PresentMon capture gaps and consistent metrics

**Files:** Create `src/FrameContinuity.{h,cpp}`, `tests/FrameContinuityTests.cpp`; modify `src/MetricsCalculator.{h,cpp}`, `src/StutterCorrelator.cpp`, `src/SessionDiagnostics.cpp`, `CMakeLists.txt`.

**Interfaces:** frame classifier takes adjacent timestamps, frame time, and present mode; emits accepted frame or `CAPTURE_GAP`; metrics and correlator consume same classifier.

- [ ] Write failing synthetic CSV test with ordinary frames, huge gap and timestamp rewind; assert max frametime/stutter counts agree in summary and diagnostics.
- [ ] Run test and observe failure.
- [ ] Normalize frame stream once for both computations and report gap count/intervals.
- [ ] Run tests and confirm pass.
- [ ] Commit.

### Task 4: Gizmo compatibility mode

**Files:** Create `src/CollectionPolicy.h`, `tests/CollectionPolicyTests.cpp`; modify `src/main.cpp`, `CMakeLists.txt`.

**Interfaces:** `CollectionPolicy::forGizmo(bool)` exposes event-log, game-log and fullscreen-helper flags; disabled sources use status `disabled_by_gizmo_mode`.

- [ ] Write failing policy test for normal and Gizmo modes.
- [ ] Run test and observe failure.
- [ ] Add checkbox, skip event/game logs/fullscreen helper, record disabled source entries and diagnostic limitations.
- [ ] Run tests and confirm pass.
- [ ] Commit.

### Task 5: Russian help and package

**Files:** Create `СПРАВКА.txt`; modify `src/main.cpp`, `.github/workflows/windows-build.yml`, `README.md`.

**Interfaces:** Help button opens packaged UTF-8 file via ShellExecute; absent file shows Russian path/error.

- [ ] Add CI package assertion for help file and verify it fails before packaging change.
- [ ] Add document covering controls, sources, outputs, statuses, privacy, Gizmo and troubleshooting; wire button and package checksum.
- [ ] Run CI tests and inspect package artifact.
- [ ] Commit.

### Task 6: Experimental single EXE feasibility

**Files:** `docs/superpowers/plans/2026-10-02-single-exe-feasibility.md`, potentially CI packaging files.

**Interfaces:** Separate artifact only if verified runtime extraction and cleanup can be proven; primary package unchanged.

- [ ] Evaluate embedding size, hash validation, user-writable extraction, Gizmo interaction and cleanup.
- [ ] Implement separate artifact with an integration test if feasible; otherwise document concrete blocker and leave portable release as primary.
- [ ] Run Windows CI and record results.
- [ ] Commit.
