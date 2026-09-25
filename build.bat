@echo off
setlocal enabledelayedexpansion

rem build.bat - compiles SnipTextProUltra.exe with MSVC. No CMake, no NuGet, no
rem third-party anything: the Windows SDK has everything this program uses.
rem
rem   build.bat            build into build\SnipTextProUltra.exe
rem   build.bat run        build, then launch it
rem   build.bat test       build and run the text-normaliser tests
rem   build.bat clean      delete the build folder
rem
rem Run it from a "x64 Native Tools Command Prompt for VS", or let it find
rem vcvars64 itself.

set ROOT=%~dp0
set OUT=%ROOT%build

if /i "%1"=="clean" (
    if exist "%OUT%" rmdir /s /q "%OUT%"
    echo Cleaned.
    exit /b 0
)

where cl.exe >nul 2>&1
if errorlevel 1 (
    echo Looking for Visual Studio...
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo   Visual Studio was not found. Install the "Desktop development
        echo   with C++" workload, or open a x64 Native Tools Command Prompt.
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * ^
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSPATH=%%i"
    )
    if not defined VSPATH (
        echo   No C++ toolset found in any Visual Studio installation.
        exit /b 1
    )
    call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
)

if not exist "%OUT%" mkdir "%OUT%"

rem /MT  - static runtime, so the finished exe is one self-contained file
rem /W4 /WX - a warning in a program meant to run for weeks is usually a bug
rem        that has not happened yet
rem /GR- - no RTTI; nothing here uses dynamic_cast or typeid
rem /O2 /GL /LTCG - optimise across translation units
rem /MANIFEST:NO (on the link line) - the manifest is embedded by
rem        SnipText.rc; letting the linker generate a second one makes
rem        CVTRES fail with CVT1100, duplicate resource
set CFLAGS=/nologo /std:c++17 /EHsc /GR- /W4 /WX /permissive- /utf-8 /O2 /GL /MT ^
    /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"%ROOT%src"

rem uuid.lib carries the FOLDERID_* GUID definitions; msimg32.lib carries
rem AlphaBlend. CMake links both implicitly, cl.exe from a command line does
rem not.
set LIBS=kernel32.lib user32.lib gdi32.lib gdiplus.lib shell32.lib shlwapi.lib ^
    ole32.lib oleaut32.lib comdlg32.lib comctl32.lib advapi32.lib dwmapi.lib ^
    winmm.lib shcore.lib uuid.lib msimg32.lib ^
    runtimeobject.lib mf.lib mfplat.lib mfreadwrite.lib mfuuid.lib propsys.lib

echo Compiling resources...
rc.exe /nologo /fo "%OUT%\SnipText.res" "%ROOT%src\SnipText.rc"
if errorlevel 1 exit /b 1

echo Compiling...
cl.exe %CFLAGS% /Fo"%OUT%\\" /Fe"%OUT%\SnipTextProUltra.exe" ^
    "%ROOT%src\main.cpp" ^
    "%ROOT%src\App.cpp" ^
    "%ROOT%src\Annotation.cpp" ^
    "%ROOT%src\Bitmap.cpp" ^
    "%ROOT%src\Capture.cpp" ^
    "%ROOT%src\Clipboard.cpp" ^
    "%ROOT%src\EditorSettings.cpp" ^
    "%ROOT%src\EditorWindow.cpp" ^
    "%ROOT%src\Hotkeys.cpp" ^
    "%ROOT%src\Log.cpp" ^
    "%ROOT%src\MediaFolder.cpp" ^
    "%ROOT%src\Ocr.cpp" ^
    "%ROOT%src\RecordingIndicator.cpp" ^
    "%ROOT%src\RegionOverlay.cpp" ^
    "%ROOT%src\ScreenRecorder.cpp" ^
    "%ROOT%src\Settings.cpp" ^
    "%ROOT%src\TesseractOcr.cpp" ^
    "%ROOT%src\TextNormalizer.cpp" ^
    "%ROOT%src\Toast.cpp" ^
    "%ROOT%src\Util.cpp" ^
    "%ROOT%src\VideoSettings.cpp" ^
    /link /LTCG /SUBSYSTEM:WINDOWS /MANIFEST:NO "%OUT%\SnipText.res" %LIBS%
if errorlevel 1 exit /b 1

echo Built %OUT%\SnipTextProUltra.exe

if /i "%1"=="test" (
    echo Building tests...
    cl.exe /nologo /std:c++17 /EHsc /W4 /permissive- /utf-8 /MT ^
        /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"%ROOT%src" ^
        /Fo"%OUT%\t_" /Fe"%OUT%\SnipTextTests.exe" ^
        "%ROOT%tests\TextNormalizerTests.cpp" ^
        "%ROOT%src\TextNormalizer.cpp" ^
        "%ROOT%src\Util.cpp" ^
        /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib ole32.lib shcore.lib uuid.lib
    if errorlevel 1 exit /b 1
    "%OUT%\SnipTextTests.exe"
    exit /b !errorlevel!
)

if /i "%1"=="run" start "" "%OUT%\SnipTextProUltra.exe"
exit /b 0
