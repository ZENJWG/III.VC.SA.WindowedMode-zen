@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0"
if errorlevel 1 (
    echo [build] ERROR: Unable to open project directory.
    exit /b 1
)

where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo [build] ERROR: Windows PowerShell is required for dependency downloads.
    popd
    exit /b 1
)

rem Usage:
rem   build.bat                      ^(Release / Win32 .asi^)
rem   build.bat GtaSA                ^(GtaSA / Win32 .asi^)
rem   build.bat -NoToolInstall       ^(do not auto-install VS Build Tools^)
rem   build.bat Release -Package     ^(also package release ZIP^)
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build.ps1" %*
set "BUILD_EXIT=%ERRORLEVEL%"
popd
exit /b %BUILD_EXIT%
