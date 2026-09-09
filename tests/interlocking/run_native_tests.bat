@echo off
setlocal
REM Focused native tests only. Full application builds still use build.bat.
REM First configure via build.bat with PREFLIGHT_INTERLOCKING_TESTS=ON in cache.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "tokens=* USEBACKQ" %%I in (`"%VSWHERE%" -latest -products * -version 18 -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FLOW_TEST_MSVC=%%I"
if not defined FLOW_TEST_MSVC exit /b 2
call "%FLOW_TEST_MSVC%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %ERRORLEVEL%
cmake --build "%~dp0..\..\build" --target interlocking_flow_test interlocking_writer_test interlocking_pressure_equalizer_test --parallel 6
if errorlevel 1 exit /b %ERRORLEVEL%
ctest --test-dir "%~dp0..\..\build" --output-on-failure --no-tests=error -R "^interlocking_"
exit /b %ERRORLEVEL%
