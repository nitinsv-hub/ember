@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "VCVARS="

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not defined VCVARS (
  for %%p in (
    "%ProgramFiles%\Microsoft Visual Studio\18\Insiders"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
  ) do (
    if exist "%%~p\VC\Auxiliary\Build\vcvars64.bat" (
      if not defined VCVARS set "VCVARS=%%~p\VC\Auxiliary\Build\vcvars64.bat"
    )
  )
)
if not defined VCVARS (
  echo ERROR: no MSVC toolchain found. Install "Desktop development with C++".
  exit /b 1
)

call "%VCVARS%" >nul 2>&1

if not exist "%ROOT%build" mkdir "%ROOT%build"
pushd "%ROOT%build"

cl /nologo /std:c++20 /EHsc /O2 /W4 /MT ^
   /I"%ROOT%src" ^
   /Fe:ember.exe ^
   "%ROOT%src\main.cpp" "%ROOT%src\cl_backend.cpp" "%ROOT%src\vk_interop.cpp" ^
   "%ROOT%src\scene.cpp" "%ROOT%src\bvh.cpp" "%ROOT%src\renderer.cpp" "%ROOT%src\image.cpp"
set BUILD_RC=%ERRORLEVEL%
popd

if not "%BUILD_RC%"=="0" (
  echo BUILD FAILED
  exit /b %BUILD_RC%
)
echo.
echo built: %ROOT%build\ember.exe
exit /b 0
