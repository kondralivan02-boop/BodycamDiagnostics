# Локальная проверка компиляции — 2 октября 2026

## Результат

Исходники коммита `7b357241c220d57f19e604adda77f08be3ae892e` успешно скомпилированы и слинкованы для Windows x64 из Linux. Получены `BodycamDiagnostics.exe`, `BodycamFullscreenOverlay.exe`, все 19 зарегистрированных тестовых целей и вспомогательные EXE тестов. Команда полной сборки завершилась с кодом 0.

Это дополнительная проверка компиляции. Windows CTest, PowerShell-проверки, работа интерфейса, запись через FFmpeg/NVENC, PresentMon и совместимость с Gizmo в этой среде не проверялись. Последняя полная проверка штатным Windows CI остаётся на промежуточном `ec1a784`. PR #3 остаётся черновиком.

Исходники программы и штатный `build_windows.ps1` для этой проверки не менялись. Дополнительные настройки компилятора находились во временной папке. Проверка не расходовала минуты GitHub Actions.

## Среда

- Ubuntu 24.04, x86_64.
- CMake 4.4.3, установлен во временную папку.
- LLVM-MinGW `20260922`, Clang 23.1.2, UCRT, цель Windows x64.
- Официальный архив: https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz
- SHA-256 архива, проверенный перед распаковкой: `bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21`.

## Как воспроизвести

Распаковать указанный инструмент в `TOOLCHAIN_ROOT`, установить CMake, создать отдельную временную папку `CHECK_ROOT`. Эти обозначения нужно заменить абсолютными путями своей среды.

В `CHECK_ROOT/case-headers` создать пять небольших заголовков: `Windows.h`, `CommCtrl.h`, `Psapi.h`, `Shellapi.h`, `TlHelp32.h`. Каждый содержит только включение одноимённого заголовка в нижнем регистре, например `#include <windows.h>`. Это учитывает регистр имён файлов в Linux; используются настоящие заголовки MinGW, без подмены Windows API.

Файл `CHECK_ROOT/toolchain.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_CXX_COMPILER "TOOLCHAIN_ROOT/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "TOOLCHAIN_ROOT/bin/x86_64-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "TOOLCHAIN_ROOT/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_CXX_FLAGS_INIT "-ICHECK_ROOT/case-headers -fms-extensions -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

Файл `CHECK_ROOT/unicode-targets.cmake`:

```cmake
function(bodycam_cross_check_entry_points)
  foreach(target IN ITEMS
      BodycamDiagnostics BodycamFullscreenOverlay ToolPreflightTests
      PresentMonDelayedExitFixture RecorderRecoveryFixture
      PresentMonControllerTests RecorderControllerTests
      AudioVideoMuxerFixture AudioVideoMuxerTests ReportPackagerTests)
    if(TARGET ${target})
      target_link_options(${target} PRIVATE -municode)
    endif()
  endforeach()
endfunction()
cmake_language(DEFER CALL bodycam_cross_check_entry_points)
```

Из корня исходников:

```sh
cmake -S . -B CHECK_ROOT/build \
  -DCMAKE_TOOLCHAIN_FILE=CHECK_ROOT/toolchain.cmake \
  -DCMAKE_PROJECT_INCLUDE=CHECK_ROOT/unicode-targets.cmake \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DBODYCAM_BUILD_SHA=7b357241c220d57f19e604adda77f08be3ae892e \
  -DBODYCAM_BUILD_BRANCH=feature/recording-gizmo-help \
  -DBODYCAM_BUILD_RUN=local-cross-check
cmake --build CHECK_ROOT/build --parallel 4
ctest --test-dir CHECK_ROOT/build -N
```

`ctest -N` только перечисляет тесты. Он не выполняет их и не означает, что они прошли.

## Полученные файлы и ограничения

| Файл | Размер, байт | SHA-256 |
| --- | ---: | --- |
| BodycamDiagnostics.exe | 759296 | `ca22df1bbf9bdf5d1bc394577c507e4d6fabb43280785f583a7b001ea0406796` |
| BodycamFullscreenOverlay.exe | 95744 | `49cf113d8c21e9cb7934d0c59e2044b3d4171ecba582a1684217a3a6080b949c` |

Заголовки PE подтверждают `COFF-x86-64`, `IMAGE_FILE_MACHINE_AMD64`, GUI subsystem. В импортах этой пробной сборки присутствуют `libc++.dll` и `libunwind.dll`, кроме системных DLL Windows. Поэтому эти EXE нельзя выдавать за готовый однофайловый пакет. Кроме того, ветка CMake с MSVC-манифестом `uiAccess` для оверлея здесь не выполняется.

Исполняемые файлы оставлены промежуточным результатом проверки; выпуск для пользователей не публиковался. Для выдачи готовой программы нужно выполнить штатную Windows-сборку, CTest и проверки пакета, затем контрольный запуск в клубе после обновления Gizmo. Для локальной Windows-сборки уже имеется `build_windows.ps1`; нужны установленный MSVC с Windows SDK и CMake. Альтернатива — штатный Actions после восстановления доступной квоты.
