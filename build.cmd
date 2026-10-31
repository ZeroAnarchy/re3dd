@echo off
rem Builds build\d3d9.dll (32-bit) from src\. Needs Visual Studio (or Build Tools) with
rem "Desktop development with C++" (MSVC x86/x64 tools and a Windows SDK).
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo vswhere.exe not found - install Visual Studio with the C++ workload & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo No Visual Studio with the MSVC C++ tools found & exit /b 1)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /LD src\proxy.cpp /Fo:"build\proxy.obj" /link /DEF:src\d3d9.def /OUT:"build\d3d9.dll" /IMPLIB:"build\proxy.lib" user32.lib ole32.lib windowscodecs.lib
exit /b %ERRORLEVEL%
