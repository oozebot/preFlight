@REM /|/ Copyright (c) preFlight 2025+ oozeBot, LLC
@REM /|/
@REM /|/ Released under AGPLv3 or higher
@REM /|/
@REM Ninja-based build script for preFlight (generates compile_commands.json)
@REM Prerequisites: Run build_win.bat -s deps first to build dependencies
@echo off
setlocal EnableDelayedExpansion

SET CONFIG=Release
SET BUILD_SUBDIR=
SET USE_FLUSH=0

REM Parse arguments
:parse_args
IF "%1"=="" GOTO done_args
IF /I "%1"=="-debug" (
    SET CONFIG=RelWithDebInfo
    SET BUILD_SUBDIR=_debug
)
IF /I "%1"=="-flush" SET USE_FLUSH=1
SHIFT
GOTO parse_args
:done_args

SET START_TIME=%TIME%

SET SCRIPT_DIR=%~dp0
SET BUILD_DIR=%SCRIPT_DIR%build%BUILD_SUBDIR%
SET DEPS_PATH_FILE=%SCRIPT_DIR%deps\build\.DEPS_PATH.txt

REM Read deps path from cache
IF NOT EXIST "%DEPS_PATH_FILE%" (
    echo ERROR: Dependencies not built. Run build_win.bat -s deps first.
    exit /b 1
)
FOR /F "tokens=* USEBACKQ" %%I IN ("%DEPS_PATH_FILE%") DO SET DESTDIR=%%I

echo **********************************************************************
echo ** Ninja Build for preFlight
echo ** Config: %CONFIG%
echo ** Deps:   %DESTDIR%
echo **********************************************************************

REM Flush resources if requested (forces icon/resource recompilation)
IF !USE_FLUSH!==1 (
    echo ** Flushing resources from !BUILD_DIR!...
    del "!BUILD_DIR!\src\preFlight.rc" 2>nul
    del "!BUILD_DIR!\src\preFlight-gcodeviewer.rc" 2>nul
    del "!BUILD_DIR!\src\CMakeFiles\preFlight_app_gui.dir\preFlight.rc.res" 2>nul
    del "!BUILD_DIR!\src\CMakeFiles\preFlight_app_console.dir\preFlight.rc.res" 2>nul
    del "!BUILD_DIR!\src\CMakeFiles\preFlight_app_gcodeviewer.dir\preFlight-gcodeviewer.rc.res" 2>nul
)

REM Find and setup MSVC environment
SET VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
IF NOT EXIST "%VSWHERE%" SET VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe
FOR /F "tokens=* USEBACKQ" %%I IN (`"%VSWHERE%" -latest -property installationPath`) DO SET MSVC_DIR=%%I

IF NOT EXIST "%MSVC_DIR%" (
    echo ERROR: Visual Studio not found.
    exit /b 1
)

echo ** Setting up MSVC environment from: %MSVC_DIR%
call "%MSVC_DIR%\Common7\Tools\vsdevcmd.bat" -arch=x64 -host_arch=x64 -app_platform=Desktop
IF %ERRORLEVEL% NEQ 0 exit /b 1

REM Create build directory
IF NOT EXIST "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM Configure with Ninja
echo.
echo ** Running CMake with Ninja generator...
cmake.exe .. -G Ninja ^
    -DCMAKE_BUILD_TYPE=%CONFIG% ^
    -DCMAKE_PREFIX_PATH="%DESTDIR%\usr\local" ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
IF %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

REM Build
echo.
echo ** Building with Ninja (24 parallel jobs)...
ninja -j 24
IF %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed.
    exit /b 1
)

CALL :DIFF_TIME ELAPSED_TIME %START_TIME% %TIME%
echo.
echo **********************************************************************
echo ** Build complete!
echo ** Finished at: %DATE% %TIME%
echo ** Elapsed time: %ELAPSED_TIME%
echo ** compile_commands.json: %BUILD_DIR%\compile_commands.json
echo ** Executable: %BUILD_DIR%\src\%CONFIG%\preFlight.exe
echo **********************************************************************

endlocal
exit /b 0

:DIFF_TIME
@REM Calculates elapsed time between two timestamps (TIME environment variable format)
@REM %1 - Output variable
@REM %2 - Start time
@REM %3 - End time
set START_ARG=%2
set END_ARG=%3
set END=!END_ARG:%TIME:~8,1%=%%100)*100+1!
set START=!START_ARG:%TIME:~8,1%=%%100)*100+1!
set /A DIFF=((((10!END:%TIME:~2,1%=%%100)*60+1!%%100)-((((10!START:%TIME:~2,1%=%%100)*60+1!%%100), DIFF-=(DIFF^>^>31)*24*60*60*100
set /A CC=DIFF%%100+100,DIFF/=100,SS=DIFF%%60+100,DIFF/=60,MM=DIFF%%60+100,HH=DIFF/60+100
set %1=%HH:~1%%TIME:~2,1%%MM:~1%%TIME:~2,1%%SS:~1%%TIME:~8,1%%CC:~1%
GOTO :EOF
