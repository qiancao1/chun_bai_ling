@echo off
rem ===========================================================================
rem  qiancao / chun-bai-ling  --  one-click wrapper for libs/ffmpeg/build-win.sh
rem
rem  WHY THIS FILE EXISTS
rem    FFmpeg's ./configure is a POSIX shell script, so cmd.exe CANNOT build
rem    FFmpeg by itself. This .bat only LOCATES a bash and forwards to
rem    build-win.sh. All real work (download source / configure / make) lives
rem    in the shell script, which also prints the Chinese status messages.
rem
rem  PREREQUISITE  (any ONE of these)
rem    1) Strawberry Perl   installed at C:\Strawberry
rem       -> already ships MinGW-w64 gcc + gmake + nasm. Simplest path.
rem    2) MSYS2             installed at C:\msys64
rem       -> after install:  pacman -S mingw-w64-x86_64-gcc make nasm
rem    3) Git for Windows   (provides bash only)
rem       -> a MinGW-w64 toolchain must already be in PATH.
rem
rem  USAGE  (double-click works too)
rem    build-win.bat                             full build
rem    build-win.bat STAGE=verify                only re-check win-x64/
rem    build-win.bat STAGE=configure             toolchain sanity test, no compile
rem    build-win.bat STAGE=build                 skip configure (already configured)
rem    build-win.bat STAGE=build JOBS=8 FFVER=8.0.1
rem
rem  OUTPUT
rem    libs/ffmpeg/win-x64/   bin\*.dll + bin\*.lib  (MSVC links these)
rem                           lib\*.dll.a *.def    include\
rem    CMake auto-detects the directory; without it the build still succeeds
rem    and simply drops the in-process libav layer (QA_LIBAV_ENABLED=OFF).
rem
rem  NOTE: this file is intentionally ASCII-only. cmd.exe parses .bat files
rem        under the OEM codepage, so non-ASCII text here gets garbled on
rem        other people's machines. Chinese output comes from build-win.sh.
rem ===========================================================================

setlocal enableextensions
chcp 65001 >nul 2>&1
title Build FFmpeg win-x64 for qiancao

rem --- target script, converted to forward slashes for MSYS bash -------------
set "SCRIPT=%~dp0build-win.sh"
set "SCRIPT=%SCRIPT:\=/%"

rem --- locate bash (first hit wins) -----------------------------------------
rem Deliberately a plain "if exist" chain instead of a `for %%P in (...)`: a
rem literal "(x86)" inside a for IN clause is a known cmd parsing trap.
set "BASH="
if not defined BASH if exist "%ProgramFiles%\Git\bin\bash.exe"               set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined BASH if exist "C:\Program Files (x86)\Git\bin\bash.exe"       set "BASH=C:\Program Files (x86)\Git\bin\bash.exe"
if not defined BASH if exist "%LOCALAPPDATA%\Programs\Git\bin\bash.exe"      set "BASH=%LOCALAPPDATA%\Programs\Git\bin\bash.exe"
if not defined BASH if exist "C:\msys64\usr\bin\bash.exe"                    set "BASH=C:\msys64\usr\bin\bash.exe"
if not defined BASH if exist "C:\Strawberry\c\bin\bash.exe"                  set "BASH=C:\Strawberry\c\bin\bash.exe"

rem Fall back to PATH -- but SKIP %SystemRoot%\System32\bash.exe, which is the
rem WSL launcher, not a real MSYS shell (running the script there would fail).
if not defined BASH (
    for /f "delims=" %%B in ('where bash 2^>nul') do (
        if not defined BASH (
            echo %%B | findstr /i /c:"\System32\" >nul || set "BASH=%%B"
        )
    )
)

if not defined BASH (
    echo.
    echo [ERROR] No usable bash found. Install ONE of these first:
    echo.
    echo    1. Strawberry Perl   https://strawberryperl.com/
    echo       ^(ships MinGW-w64 gcc + gmake + nasm; install to C:\Strawberry^)
    echo.
    echo    2. MSYS2             https://www.msys2.org/
    echo       after install run:  pacman -S mingw-w64-x86_64-gcc make nasm
    echo.
    echo    3. Git for Windows   https://git-scm.com/download/win
    echo       ^(provides bash only; a MinGW-w64 toolchain must be in PATH^)
    echo.
    echo    Tip: you can skip FFmpeg entirely -- qiancao still builds without it.
    echo.
    pause
    exit /b 1
)

if not exist "%SCRIPT%" (
    echo.
    echo [ERROR] Not found: %SCRIPT%
    echo         Keep build-win.bat next to build-win.sh in libs\ffmpeg\.
    echo.
    pause
    exit /b 1
)

echo.
echo   bash   : %BASH%
echo   script : %SCRIPT%
echo.

"%BASH%" "%SCRIPT%" %*
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
    echo [OK] Artifacts are in libs\ffmpeg\win-x64\
    echo      ^(bin\*.dll + bin\*.lib + include\ -- CMake picks them up automatically^)
) else (
    echo [FAILED] exit code %RC% -- scroll up for the reason.
    echo          Tip: STAGE=configure only tests the toolchain, no compile.
)
echo.
pause
exit /b %RC%
