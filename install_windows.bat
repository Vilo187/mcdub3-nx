@echo off
rem One-click Windows install of mcdub3-nx (Midnight Club 3: DUB Edition).
rem
rem   1. Put the extracted disc (default.xbe and the rest), or the disc's .iso,
rem      in the game folder next to this file.
rem   2. Double-click this file.
rem
rem It installs MSYS2 (the build tools, in C:\msys64; MSYS2_ROOT to choose
rem another folder) if it is missing, then lifts the game's code, builds it
rem and puts a "Midnight Club 3" shortcut on the desktop. The first run takes
rem about an hour; run it again after updating the code. Safe to re-run.
rem The log: work\logs\install_windows.log.
setlocal
title Midnight Club 3 - Windows install
pushd "%~dp0"
if not exist "work\logs" mkdir "work\logs"
set "LOG=%CD%\work\logs\install_windows.log"

rem x86-64 or ARM64 (a 32-bit window on a 64-bit Windows reports the real one here)
set "MC3_MSYSTEM=CLANG64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "MC3_MSYSTEM=CLANGARM64"
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "MC3_MSYSTEM=CLANGARM64"
if /i "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 (
    echo 32-bit Windows is not supported.
    goto :fail
)

if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
if exist "%MSYS2_ROOT%\usr\bin\bash.exe" goto :have_msys2
echo == Installing MSYS2 into %MSYS2_ROOT% (about 500 MB)
set "SFX=%TEMP%\msys2-base-latest.sfx.exe"
curl.exe -L --fail -o "%SFX%" https://github.com/msys2/msys2-installer/releases/latest/download/msys2-base-x86_64-latest.sfx.exe
if errorlevel 1 goto :fail
for %%D in ("%MSYS2_ROOT%\..") do set "MSYS2_PARENT=%%~fD"
"%SFX%" -y -o"%MSYS2_PARENT%"
if errorlevel 1 goto :fail
del "%SFX%"
if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" (
    echo MSYS2 did not unpack into %MSYS2_ROOT%
    goto :fail
)
:have_msys2

set "MSYSTEM=%MC3_MSYSTEM%"
set "CHERE_INVOKING=1"
set "BASH=%MSYS2_ROOT%\usr\bin\bash.exe"
echo == MSYS2 (%MSYSTEM%): first start and updates
rem The first update may replace MSYS2's own core and end the shell: run twice.
"%BASH%" -lc "pacman -Syuu --noconfirm" >> "%LOG%" 2>&1
"%BASH%" -lc "pacman -Syuu --noconfirm" >> "%LOG%" 2>&1
if errorlevel 1 (
    echo The MSYS2 update failed: see %LOG%
    goto :fail
)

echo == Lift and build (the log: %LOG%)
"%BASH%" -lc "bash tools/install_windows.sh 2>&1 | tee -a work/logs/install_windows.log; exit ${PIPESTATUS[0]}"
if errorlevel 1 goto :fail
popd
echo.
pause
exit /b 0

:fail
echo.
echo The install stopped. The log is %LOG%
popd
pause
exit /b 1
