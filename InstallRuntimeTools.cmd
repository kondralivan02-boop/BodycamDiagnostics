@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0InstallRuntimeTools.ps1" %*
set "EXITCODE=%ERRORLEVEL%"
if not "%EXITCODE%"=="0" (
  echo.
  echo Runtime tool installation failed with exit code %EXITCODE%.
)
exit /b %EXITCODE%
