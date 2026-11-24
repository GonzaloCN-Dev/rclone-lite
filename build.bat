@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo No se encontro vswhere.exe. Instala Visual Studio 2022 o posterior con las herramientas de C++.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath`) do set "VS=%%i"
if not defined VS (
    echo No se encontro una instalacion de Visual Studio con C++ y CMake.
    exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CM=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%CM%\CMake\bin;%CM%\Ninja;%PATH%"
cd /d "%~dp0"
set "CFG=%~1"
if "%CFG%"=="" set "CFG=Release"
if not exist build\build.ninja (
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
)
cmake --build build || exit /b 1
