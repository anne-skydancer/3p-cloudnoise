@echo off
rem Builds build\cloudnoise_gen.exe with the pinned toolchain: MSVC v145 14.51, /MT /O2 /fp:precise, x64 SSE2.
rem Usage: scripts\build.cmd [winsdk-version]   (default: PINNED_WINSDK from scripts\pinned.cmd)
setlocal
call "%~dp0pinned.cmd"
if not "%~1"=="" set PINNED_WINSDK=%~1
set VCVARS=
set PF=%ProgramFiles%
if exist "%PF%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%PF%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat
if exist "%PF%\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%PF%\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat
if exist "%PF%\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%PF%\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat
if exist "%PF%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%PF%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if defined VCVARSALL_OVERRIDE set VCVARS=%VCVARSALL_OVERRIDE%
if not defined VCVARS goto novs
call "%VCVARS%" %PINNED_WINSDK% -vcvars_ver=%PINNED_VCVARS_VER% >nul
if errorlevel 1 exit /b 1
cl 2>&1 | findstr /C:"Version 19.51" >nul
if errorlevel 1 goto badcl
if not exist "%~dp0..\build" mkdir "%~dp0..\build"
cd /d "%~dp0..\build"
cl /nologo /MT /O2 /fp:precise /EHsc /std:c++17 /W3 /D_CRT_SECURE_NO_WARNINGS /I"%~dp0..\vendor\TileableVolumeNoise" "%~dp0..\generator\cloudnoise_gen.cpp" "%~dp0..\vendor\TileableVolumeNoise\TileableVolumeNoise.cpp" /Fe:cloudnoise_gen.exe
if errorlevel 1 exit /b 1
echo built %CD%\cloudnoise_gen.exe (cl 19.51, Windows SDK %PINNED_WINSDK%)
exit /b 0
:novs
echo Visual Studio 2026 with the v145 toolset not found
exit /b 1
:badcl
echo cl is not MSVC 19.51 (v145 14.51)
exit /b 1
