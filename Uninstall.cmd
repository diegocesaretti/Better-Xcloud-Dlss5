@echo off
setlocal
title Better Xcloud DLSS5 - Uninstaller
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\Uninstall.ps1"
set EXITCODE=%ERRORLEVEL%
if not "%EXITCODE%"=="0" (
  echo.
  echo Uninstall failed with code %EXITCODE%.
  pause
)
exit /b %EXITCODE%
