@echo off
rem MSYS2's shell for developing on Windows, opened in the repo folder: make
rem works here (make setup, make lift, make build run). Double-click it, or
rem run it from a Command Prompt. Needs MSYS2 (install_windows.bat sets it up).
setlocal
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
if not exist "%MSYS2_ROOT%\msys2_shell.cmd" (
    echo No MSYS2 in %MSYS2_ROOT%: run install_windows.bat first, or set MSYS2_ROOT.
    pause
    exit /b 1
)
set "ENVNAME=-clang64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ENVNAME=-clangarm64"
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "ENVNAME=-clangarm64"
pushd "%~dp0..\.."
call "%MSYS2_ROOT%\msys2_shell.cmd" %ENVNAME% -here
popd
