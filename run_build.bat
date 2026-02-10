@REM /|/ Copyright (c) preFlight 2025+ oozeBot, LLC
@REM /|/
@REM /|/ Released under AGPLv3 or higher
@REM /|/
@echo off

setlocal EnableDelayedExpansion
SET USE_NINJA=0
SET USE_DEBUG=0
SET USE_CLEAN=0
SET USE_FLUSH=0
SET CONFIG=Release
SET BUILD_DIR=build

:parse_args
IF "%~1"=="" GOTO done_args
IF /I "%~1"=="-help" GOTO show_help
IF /I "%~1"=="--help" GOTO show_help
IF /I "%~1"=="-?" GOTO show_help
IF /I "%~1"=="-ninja" SET USE_NINJA=1
IF /I "%~1"=="--ninja" SET USE_NINJA=1
IF /I "%~1"=="-debug" (
    SET USE_DEBUG=1
    SET CONFIG=Debug
    SET BUILD_DIR=build_debug
)
IF /I "%~1"=="-clean" SET USE_CLEAN=1
IF /I "%~1"=="--clean" SET USE_CLEAN=1
IF /I "%~1"=="-flush" SET USE_FLUSH=1
IF /I "%~1"=="--flush" SET USE_FLUSH=1
SHIFT
GOTO parse_args

:show_help
echo.
echo preFlight Build Script
echo ======================
echo.
echo Usage: run_build.bat [options]
echo.
echo Options:
echo   -ninja     Use Ninja generator (faster, generates compile_commands.json)
echo   -debug     Build with debug symbols (RelWithDebInfo config to build_debug/)
echo   -clean     Clean build directory before building
echo   -flush     Force resource recompilation (icons, splash screen)
echo   -help      Show this help message
echo.
echo Examples:
echo   run_build.bat -ninja              Release build with Ninja
echo   run_build.bat -ninja -debug       Debug build with Ninja
echo   run_build.bat -ninja -flush       Release build, recompile resources
echo   run_build.bat -ninja -clean       Clean Release build
echo.
echo Output locations:
echo   Release:   build\src\Release\preFlight.exe
echo   Debug:     build_debug\src\RelWithDebInfo\preFlight.exe
echo.
endlocal
exit /b 0

:done_args

REM Only clean when -clean flag is passed (enables fast incremental builds)
IF %USE_CLEAN%==1 (
    echo Cleaning build for fresh start...
    IF %USE_NINJA%==1 (
        REM Ninja builds directly to build\src or build_debug\src
        rmdir /S /Q %BUILD_DIR%\src 2>nul
    ) ELSE (
        REM MSBuild builds to build\src\Release
        rmdir /S /Q build\src\%CONFIG% 2>nul
    )
    del %BUILD_DIR%\CMakeCache.txt 2>nul
) ELSE (
    echo Incremental build ^(use -clean for fresh build^)...
)

echo Starting preFlight build...
IF %USE_NINJA%==1 (
    SET NINJA_ARGS=
    IF %USE_DEBUG%==1 SET NINJA_ARGS=-debug
    IF %USE_FLUSH%==1 SET NINJA_ARGS=!NINJA_ARGS! -flush
    IF %USE_DEBUG%==1 (
        echo Using Ninja generator with DEBUG config
    ) ELSE (
        echo Using Ninja generator ^(generates compile_commands.json^)
    )
    call build_ninja.bat !NINJA_ARGS!
) ELSE (
    call build_win.bat -d="deps\build\destdir" -r=none -c %CONFIG%
)
echo Build completed with exit code: %ERRORLEVEL%
endlocal
pause