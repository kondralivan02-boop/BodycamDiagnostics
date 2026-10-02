@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ValidateEnvironment.ps1" %*
set "EXITCODE=%ERRORLEVEL%"
echo.
if "%EXITCODE%"=="0" (
  echo Environment self-test passed.
) else (
  echo Environment self-test failed with exit code %EXITCODE%.
)
exit /b %EXITCODE%
