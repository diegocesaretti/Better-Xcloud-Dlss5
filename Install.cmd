@echo off
setlocal
title Better Xcloud DLSS5 - Installer
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\Install.ps1"
set EXITCODE=%ERRORLEVEL%
if not "%EXITCODE%"=="0" (
  echo.
  echo Installation failed with code %EXITCODE%.
  echo Press any key to close.
  pause >nul
)
exit /b %EXITCODE%
