@echo off
setlocal
REM Small standalone production-helper test; app builds still use build.bat.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "tokens=* USEBACKQ" %%I in (`"%VSWHERE%" -latest -products * -version 18 -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FLOW_TEST_MSVC=%%I"
if not defined FLOW_TEST_MSVC exit /b 2
call "%FLOW_TEST_MSVC%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%~dp0build" mkdir "%~dp0build"
cl /nologo /std:c++17 /EHsc /W4 /O2 /I"%~dp0..\..\src" "%~dp0test_flow_limit.cpp" /Fo"%~dp0build\test_flow_limit.obj" /Fe"%~dp0build\test_flow_limit.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
"%~dp0build\test_flow_limit.exe"
exit /b %ERRORLEVEL%
