@echo off
rem Builds and runs the Editor Mode engine tests with MSVC (any Visual Studio 2019+ with the C++ tools).
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found: Visual Studio is not installed
    exit /b 2
)

set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if "%VSDIR%"=="" (
    echo No Visual Studio with the C++ tools was found
    exit /b 2
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 2

if not exist "%~dp0build" mkdir "%~dp0build"
cl /nologo /std:c++20 /EHsc /W4 /permissive- /Fo"%~dp0build\\" /Fe"%~dp0build\editor_engine_tests.exe" "%~dp0editor_engine_tests.cpp" "%~dp0..\src\editor\EditorEngine.cpp"
if errorlevel 1 exit /b 1

"%~dp0build\editor_engine_tests.exe"
exit /b %errorlevel%
