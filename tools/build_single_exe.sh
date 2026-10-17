#!/usr/bin/env bash
# Build-time Linux prerequisites only; the resulting application needs no installer.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
check_root=${BODYCAM_CHECK_ROOT:-/workspace/bodycam-onefile-build}
mkdir -p "$check_root"
python3 -m venv "$check_root/venv"
"$check_root/venv/bin/python" -m pip install --disable-pip-version-check --no-cache-dir cmake==3.31.6 pefile==2024.8.26
toolchain=${LLVM_MINGW_ROOT:-$check_root/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64}
if [[ ! -x "$toolchain/bin/x86_64-w64-mingw32-clang++" ]]; then
    archive="$check_root/llvm-mingw.tar.xz"
    curl -fL --retry 2 -o "$archive" https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64.tar.xz
    printf '%s  %s\n' bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21 "$archive" | sha256sum -c -
    tar -xf "$archive" -C "$check_root"
    toolchain="$check_root/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64"
fi
mkdir -p "$check_root/case-headers"
for header in Windows CommCtrl Psapi Shellapi TlHelp32; do
    printf '#include <%s.h>\n' "${header,,}" > "$check_root/case-headers/$header.h"
done
cat > "$check_root/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_CXX_COMPILER "$toolchain/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "$toolchain/bin/x86_64-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "$toolchain/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_CXX_FLAGS_INIT "-I$check_root/case-headers -fms-extensions -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
EOF
cat > "$check_root/unicode-targets.cmake" <<'EOF'
function(bodycam_unicode_targets)
  foreach(target IN ITEMS BodycamDiagnostics BodycamFullscreenOverlay ToolPreflightTests PresentMonDelayedExitFixture RecorderRecoveryFixture PresentMonControllerTests RecorderControllerTests AudioVideoMuxerFixture AudioVideoMuxerTests ReportPackagerTests)
    if(TARGET ${target})
      target_link_options(${target} PRIVATE -municode)
    endif()
  endforeach()
endfunction()
cmake_language(DEFER CALL bodycam_unicode_targets)
EOF
commit=$(git -C "$repo" rev-parse HEAD)
if [[ -n $(git -C "$repo" status --porcelain) ]]; then commit="$commit-dirty"; fi
"$check_root/venv/bin/cmake" -S "$repo" -B "$check_root/build" \
    -DCMAKE_TOOLCHAIN_FILE="$check_root/toolchain.cmake" \
    -DCMAKE_PROJECT_INCLUDE="$check_root/unicode-targets.cmake" \
    -DPython3_EXECUTABLE="$check_root/venv/bin/python" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DBODYCAM_SINGLE_EXE=ON \
    -DBODYCAM_PAYLOAD_DIR="$check_root/payload" \
    -DBODYCAM_BUILD_SHA="$commit" -DBODYCAM_BUILD_BRANCH=feature/recording-gizmo-help \
    -DBODYCAM_BUILD_RUN=local-single-exe
"$check_root/venv/bin/cmake" --build "$check_root/build" --parallel 4
"$check_root/venv/bin/ctest" --test-dir "$check_root/build" -R '^SingleExe(Payload|Inspection|Tamper)Tests$' --output-on-failure --no-tests=error
"$check_root/venv/bin/python" "$repo/tools/verify_single_exe.py" "$check_root/build/BodycamDiagnostics.exe" "$check_root/payload/payload.json" > "$check_root/pe-validation.json"
sha256sum "$check_root/build/BodycamDiagnostics.exe"
printf 'User executable: %s\nWindows tests were compiled, not executed.\n' "$check_root/build/BodycamDiagnostics.exe"
