@echo off
setlocal enabledelayedexpansion

:: ================================================================
::  build-qt-static.bat - one-time build of static Qt 5.15.2
::                        (qtbase only)
::
::  Output: C:\Qt\5.15.2-static  (used by deploy.bat standalone)
::  Time:   about 30-60 minutes (CPU dependent), ~3 GB disk
::
::  Usage:
::    build-qt-static.bat                  auto-download source (internet)
::    build-qt-static.bat D:\path\qtbase-everywhere-src-5.15.2.zip
::    build-qt-static.bat D:\path\qtbase-everywhere-src-5.15.2   (extracted dir)
::
::  Requires: Qt official MinGW 8.1 toolchain (C:\Qt\Tools\mingw810_64)
::
::  NOTE: keep this file pure ASCII - Chinese Windows cmd reads batch
::  files as GBK and UTF-8 Chinese comments get misparsed.
:: ================================================================

set "SRC=%~1"
set "PREFIX=C:\Qt\5.15.2-static"
set "QTBASE_ZIP=qtbase-everywhere-src-5.15.2.zip"
set "QTBASE_DIR=qtbase-everywhere-src-5.15.2"
set "URL=https://download.qt.io/archive/qt/5.15/5.15.2/submodules/%QTBASE_ZIP%"
set "WORKDIR=%USERPROFILE%\qt-static-build"

echo ============================================================
echo   Static Qt 5.15.2 builder  -^>  %PREFIX%
echo ============================================================
echo.

:: ---- skip if already installed ---------------------------------
if exist "%PREFIX%\bin\qmake.exe" (
    echo [SKIP] Static Qt already installed: %PREFIX%
    echo        Delete the folder to rebuild.
    pause & exit /b 0
)

:: ---- [1/7] MinGW 8.1 -------------------------------------------
echo [1/7] Locating MinGW 8.1 ...
set "MINGW_BIN="
if exist "C:\Qt\Tools\mingw810_64\bin\g++.exe" set "MINGW_BIN=C:\Qt\Tools\mingw810_64\bin"
if "%MINGW_BIN%"=="" (
    for /f "delims=" %%a in ('dir /s /b C:\Qt\Tools\g++.exe 2^>nul') do (
        for /f "tokens=*" %%v in ('"%%a" -dumpversion 2^>nul') do (
            for /f "tokens=1 delims=." %%m in ("%%v") do (
                if "%%m"=="8" (
                    for %%b in ("%%a") do set "MINGW_BIN=%%~dpb"
                )
            )
        )
        if not "!MINGW_BIN!"=="" goto :mingw_ok
    )
)
:mingw_ok
if defined MINGW_BIN set "MINGW_BIN=!MINGW_BIN:~0,-1!"
if not exist "!MINGW_BIN!\g++.exe" (
    echo [ERROR] MinGW 8.1 not found. Install via Qt MaintenanceTool:
    echo         Developer and Designer Tools - MinGW 8.1.0 64-bit
    pause & exit /b 1
)
echo       !MINGW_BIN!
set "PATH=!MINGW_BIN!;!PATH!"
echo.

:: ---- [2/7] source ----------------------------------------------
echo [2/7] Acquiring qtbase 5.15.2 source ...
if not "%SRC%"=="" goto :src_given

:: no argument: reuse previous download, else download
if exist "%WORKDIR%\%QTBASE_DIR%\configure.bat" (
    set "SRC=%WORKDIR%\%QTBASE_DIR%"
    goto :src_ready
)
goto :acquire

:: argument given: zip file or extracted dir
:src_given
if /i "%SRC:~-4%"==".zip" (
    if not exist "%SRC%" (
        echo [ERROR] Zip not found: %SRC%
        pause & exit /b 1
    )
    if not exist "%WORKDIR%" mkdir "%WORKDIR%"
    set "ZIP=%SRC%"
    goto :extract
)
if exist "%SRC%\configure.bat" goto :src_ready
echo [ERROR] Not a qtbase source dir (no configure.bat): %SRC%
pause & exit /b 1

:: download + extract
:acquire
if not exist "%WORKDIR%" mkdir "%WORKDIR%"
set "ZIP=%WORKDIR%\%QTBASE_ZIP%"
if not exist "%ZIP%" (
    echo       Downloading %URL%
    powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; (New-Object Net.WebClient).DownloadFile('%URL%','%ZIP%')"
    if not exist "%ZIP%" (
        echo [ERROR] Download failed. Offline? Download manually then run:
        echo         build-qt-static.bat %ZIP%
        pause & exit /b 1
    )
)

:extract
echo       Extracting ...
where 7z >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    7z x -y -o"%WORKDIR%" "%ZIP%" >nul
) else (
    powershell -NoProfile -Command "Expand-Archive -Force '%ZIP%' '%WORKDIR%'"
)
if not exist "%WORKDIR%\%QTBASE_DIR%\configure.bat" (
    echo [ERROR] Extraction failed. Extract %ZIP% manually, then run:
    echo         build-qt-static.bat %WORKDIR%\%QTBASE_DIR%
    pause & exit /b 1
)
set "SRC=%WORKDIR%\%QTBASE_DIR%"

:src_ready
echo       %SRC%
echo.

:: ---- [3/7] configure -------------------------------------------
echo [3/7] Configuring static build (release, qtbase minimal) ...
pushd "%SRC%"
call configure.bat ^
    -static -release ^
    -opensource -confirm-license ^
    -platform win32-g++ ^
    -prefix "%PREFIX%" ^
    -nomake examples -nomake tests ^
    -no-openssl -no-opengl -no-dbus ^
    -strip
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] configure failed.
    popd & pause & exit /b 1
)
echo.

:: ---- [4/7] build ------------------------------------------------
echo [4/7] Building (this takes 30-60 minutes) ...
set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"
mingw32-make -j%JOBS%
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed.
    popd & pause & exit /b 1
)
echo.

:: ---- [5/7] install ----------------------------------------------
echo [5/7] Installing to %PREFIX% ...
mingw32-make install
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Install failed.
    popd & pause & exit /b 1
)
popd
echo.

:: ---- [6/7] verify ------------------------------------------------
echo [6/7] Verifying ...
if not exist "%PREFIX%\bin\qmake.exe" (
    echo [ERROR] Install incomplete: %PREFIX%\bin\qmake.exe missing.
    pause & exit /b 1
)
echo       qmake: %PREFIX%\bin\qmake.exe
echo.

:: ---- [7/7] done --------------------------------------------------
echo [7/7] DONE
echo ============================================================
echo   Static Qt installed: %PREFIX%
echo.
echo   Now build the standalone exe:
echo     scripts\deploy.bat standalone
echo ============================================================
pause
exit /b 0
