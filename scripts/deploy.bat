@echo off
setlocal enabledelayedexpansion

:: ================================================================
::  gwxt-gui one-click packaging script
::
::  Usage:
::    deploy.bat              same as folder
::    deploy.bat folder       portable folder (exe + Qt DLLs, ~60-80 MB)
::    deploy.bat standalone   single-file exe (static link, ~15-25 MB)
::
::  standalone needs a statically built Qt 5.15.2:
::    - uses C:\Qt\5.15.2-static or %QT_STATIC_DIR% if present
::    - otherwise run scripts\build-qt-static.bat first (one-time,
::      about 30-60 minutes)
::
::  NOTE: keep this file pure ASCII - Chinese Windows cmd reads batch
::  files as GBK and UTF-8 Chinese comments get misparsed.
:: ================================================================

set "MODE=%~1"
if "%MODE%"=="" set "MODE=folder"
if /i not "%MODE%"=="folder" if /i not "%MODE%"=="standalone" (
    echo [ERROR] Unknown mode: %MODE%
    echo Usage: deploy.bat [folder^|standalone]
    pause & exit /b 1
)
echo Mode: %MODE%
echo.

:: ---- Locate project root ---------------------------------------
set "PROJECT_DIR=%~dp0.."
pushd "%PROJECT_DIR%" 2>nul || (
    echo [ERROR] Cannot find project directory.
    echo        Expected layout: gwxt-gui\scripts\deploy.bat
    pause
    exit /b 1
)
echo Project: %CD%
echo.

:: ---- 0. Locate compiler (MinGW GCC 8.x shared by both kits) ----
echo [0/5] Scanning for MinGW GCC 8.x ...

set "COMPILER_BIN="
for /f "delims=" %%a in ('dir /s /b C:\Qt\g++.exe 2^>nul') do (
    call :check_gcc "%%a"
    if !ERRORLEVEL! EQU 0 (
        for %%b in ("%%a") do set "COMPILER_BIN=%%~dpb"
        set "COMPILER_BIN=!COMPILER_BIN:~0,-1!"
        set "COMPILER_TYPE=MinGW"
        goto :compiler_found
    )
)
for /f "delims=" %%a in ('where g++.exe 2^>nul') do (
    call :check_gcc "%%a"
    if !ERRORLEVEL! EQU 0 (
        for %%b in ("%%a") do set "COMPILER_BIN=%%~dpb"
        set "COMPILER_BIN=!COMPILER_BIN:~0,-1!"
        set "COMPILER_TYPE=MinGW"
        goto :compiler_found
    )
)
where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set "COMPILER_TYPE=MSVC"
    goto :compiler_found
)

echo [ERROR] No suitable compiler found.
echo   Qt 5.15.2 requires MinGW GCC 8.1.0 (install via Qt MaintenanceTool:
echo   Developer and Designer Tools - MinGW 8.1.0 64-bit^).
pause & exit /b 1

:compiler_found
echo       compiler: !COMPILER_TYPE!  %COMPILER_BIN%

:: ================================================================
::  Locate qmake: standalone -> static kit; folder -> shared kit
:: ================================================================
if /i "%MODE%"=="standalone" goto :qmake_static

:: ---- folder mode: find shared Qt 5.15.2 kit --------------------
set "QMAKE_EXE="
set "QT_KIT_NAME="
for %%r in ("C:\Qt\5.15.2" "C:\Qt\5.15" "D:\Qt\5.15.2") do (
    if exist %%r (
        for /d %%k in (%%r\*) do (
            if exist "%%k\bin\qmake.exe" (
                set "QMAKE_EXE=%%k\bin\qmake.exe"
                set "QT_KIT_NAME=%%~nxk"
                goto :qmake_found
            )
        )
    )
)
for /f "delims=" %%a in ('where qmake 2^>nul') do (
    set "QMAKE_EXE=%%a"
    goto :qmake_found
)
echo [ERROR] qmake.exe not found.
pause & exit /b 1

:: ---- standalone mode: find static Qt kit -----------------------
:qmake_static
if "%QT_STATIC_DIR%"=="" set "QT_STATIC_DIR=C:\Qt\5.15.2-static"

if exist "%QT_STATIC_DIR%\bin\qmake.exe" (
    set "QMAKE_EXE=%QT_STATIC_DIR%\bin\qmake.exe"
    set "QT_KIT_NAME=%QT_STATIC_DIR%"
    goto :qmake_found
)

echo [ERROR] Static Qt not found: %QT_STATIC_DIR%
echo.
echo   Standalone build needs a statically compiled Qt 5.15.2.
echo   One-time setup (about 30-60 minutes):
echo.
echo     scripts\build-qt-static.bat
echo.
echo   Or point to an existing static build:
echo     set QT_STATIC_DIR=D:\Qt\my-static-qt
echo     deploy.bat standalone
pause & exit /b 1

:qmake_found
for %%a in ("%QMAKE_EXE%") do set "QMAKE_BIN=%%~dpa"
set "QMAKE_BIN=!QMAKE_BIN:~0,-1!"
echo       Qt kit:   %QT_KIT_NAME%  ^( %QMAKE_BIN% ^)

:: MSVC path does not require GCC 8.x (kept for folder/MSVC users)
if "%COMPILER_TYPE%"=="MSVC" (
    set "PATH=%QMAKE_BIN%;%PATH%"
) else (
    set "PATH=%COMPILER_BIN%;%QMAKE_BIN%;%PATH%"
)
echo.

:: ---- 1. Clean -------------------------------------------------
echo [1/5] Cleaning old build...
if exist "release"  rmdir /s /q "release"
if exist "portable" rmdir /s /q "portable"
if exist "build"    rmdir /s /q "build"
if exist "Makefile" del /q "Makefile" >nul 2>&1
if exist ".qmake.stash" del /q ".qmake.stash" >nul 2>&1

:: ---- 2. Build Release -----------------------------------------
echo [2/5] Building Release (%MODE%)...
qmake gwxt-gui.pro "CONFIG+=release"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] qmake failed.
    pause & exit /b 1
)

if "%COMPILER_TYPE%"=="MinGW" (
    mingw32-make -j4 release
) else (
    nmake release
)
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed.
    pause & exit /b 1
)

if not exist "release\gwxt-gui.exe" (
    echo [ERROR] Build output not found.
    pause & exit /b 1
)
echo       Done

:: ---- standalone: done after copying single exe -----------------
if /i "%MODE%"=="standalone" goto :package_standalone

:: ---- 3. windeployqt (folder mode) -----------------------------
echo [3/5] Collecting DLLs via windeployqt...
if exist "%QMAKE_BIN%\windeployqt.exe" (
    "%QMAKE_BIN%\windeployqt.exe" release\gwxt-gui.exe --no-translations
) else (
    echo [WARN] windeployqt not found
)

:: ---- 4. Quick verification ------------------------------------
echo [4/5] Verifying DLL set...
set MISSING=0
for %%f in (Qt5Core.dll Qt5Gui.dll Qt5Widgets.dll Qt5Network.dll) do (
    if not exist "release\%%f" (
        echo       MISSING: %%f
        set MISSING=1
    )
)
if not exist "release\platforms\qwindows.dll" (
    echo       MISSING: platforms\qwindows.dll
    set MISSING=1
)
if %MISSING% EQU 1 (
    echo [WARN] Some DLLs missing - app may not run on other PCs
) else (
    echo       All essential DLLs present
)

:: ---- 5. Create portable folder ---------------------------------
echo [5/5] Creating portable folder...
goto :package_folder

:: ================================================================
::  standalone: verify + copy single exe
:: ================================================================
:package_standalone
echo [3/5] Verifying static link (no Qt DLLs emitted)...
:: static build must not emit Qt5*.dll into release\
dir /b release\Qt5*.dll >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [ERROR] Found Qt5*.dll in release\ - this is NOT a static build.
    echo         The Qt kit "%QT_KIT_NAME%" appears to be dynamically linked.
    pause & exit /b 1
)
echo       OK - no Qt DLLs

echo [4/5] Creating standalone output...
if exist "portable" rmdir /s /q "portable"
mkdir portable
copy /y "release\gwxt-gui.exe" "portable\" >nul
goto :done

:: ================================================================
::  folder: copy exe + DLL tree
:: ================================================================
:package_folder
if exist "portable" rmdir /s /q "portable"
mkdir portable
copy /y "release\gwxt-gui.exe" "portable\" >nul
for %%f in (release\*.dll) do copy /y "%%f" "portable\" >nul
for /d %%d in (release\*) do xcopy /E /I /Q "%%d" "portable\%%~nxd" >nul

echo [5/5] Verifying...
for /f "tokens=*" %%a in ('dir /s /b portable ^| find /c /v ""') do set FCOUNT=%%a
for /f "tokens=3" %%a in ('dir /s portable ^| findstr /i "File(s)"') do set FSIZE=%%a
echo       Files: %FCOUNT%   Size: %FSIZE% bytes
goto :done

:: ================================================================
:done
echo.
echo ============================================================
echo   DONE - %MODE% build complete
echo.
echo   %CD%\portable\
if /i "%MODE%"=="standalone" (
    echo   Single file: gwxt-gui.exe  - copy anywhere and run.
    echo   Works with no installation, no DLLs, no VC++ runtime.
) else (
    echo   Copy this folder to USB. Run gwxt-gui.exe on any PC.
)
echo ============================================================
echo.
popd
pause
exit /b 0

:: ================================================================
::  Subroutine: check if g++.exe is GCC 8.x
:: ================================================================
:check_gcc
set "GXX=%~1"
set "GCC_VER="
set "MAJOR="
for /f "tokens=*" %%v in ('"%GXX%" -dumpversion 2^>nul') do set "GCC_VER=%%v"
if "%GCC_VER%"=="" exit /b 1
for /f "tokens=1 delims=." %%m in ("%GCC_VER%") do set "MAJOR=%%m"
if "%MAJOR%"=="8" exit /b 0
exit /b 1
