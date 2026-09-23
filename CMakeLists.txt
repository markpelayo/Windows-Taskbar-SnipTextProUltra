cmake_minimum_required(VERSION 3.20)
project(SnipText VERSION 1.0.0 LANGUAGES CXX)

# Windows only, by construction: every source file in here is Win32.
if(NOT WIN32)
    message(FATAL_ERROR "SnipText is a Windows program. Build it on Windows with MSVC.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Statically linked, so the finished program is one file with nothing to
# install beside it — the same deal as windows-taskbar-RollingCalendar.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(SOURCES
    src/main.cpp
    src/App.cpp
    src/Annotation.cpp
    src/Bitmap.cpp
    src/Capture.cpp
    src/Clipboard.cpp
    src/EditorSettings.cpp
    src/EditorWindow.cpp
    src/Log.cpp
    src/MediaFolder.cpp
    src/Ocr.cpp
    src/RegionOverlay.cpp
    src/ScreenRecorder.cpp
    src/Settings.cpp
    src/TextNormalizer.cpp
    src/Toast.cpp
    src/Util.cpp
    src/VideoSettings.cpp
    src/SnipText.rc
)

add_executable(SnipText WIN32 ${SOURCES})
target_include_directories(SnipText PRIVATE src)

target_compile_definitions(SnipText PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

if(MSVC)
    # /W4 with warnings-as-errors: a warning in a program that is meant to
    # run for weeks is usually a bug that has not happened yet.
    target_compile_options(SnipText PRIVATE /W4 /WX /permissive- /utf-8 /EHsc /GR-)

    # /MANIFEST:NO is required, not optional. SnipText.rc embeds the manifest
    # as resource 1 so that build.bat and CMake produce an identical binary
    # from one source of truth. The linker also generates a manifest of its
    # own by default, and two RT_MANIFEST resources with id 1 make CVTRES
    # fail outright:
    #   CVT1100: duplicate resource. type:MANIFEST, name:1, language:0x0409
    #   LNK1123: failure during conversion to COFF
    target_link_options(SnipText PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
endif()

target_link_libraries(SnipText PRIVATE
    user32 gdi32 gdiplus shell32 shlwapi ole32 oleaut32 comdlg32 comctl32
    advapi32 dwmapi winmm shcore uuid
    msimg32                # AlphaBlend, used by the overlay's dimming
    runtimeobject          # WinRT activation, for Windows.Media.Ocr
    mf mfplat mfreadwrite mfuuid   # Media Foundation, for the recorder
    propsys                # audio endpoint property store
)

# --- tests -----------------------------------------------------------------
# The geometry-driven text normaliser is the one piece of this program whose
# behaviour is worth asserting rather than eyeballing, because a regression in
# it produces plausible-looking wrong text rather than an obvious failure.
option(SNIPTEXT_BUILD_TESTS "Build the text-normaliser tests" ON)
if(SNIPTEXT_BUILD_TESTS)
    add_executable(SnipTextTests
        tests/TextNormalizerTests.cpp
        src/TextNormalizer.cpp
        src/Util.cpp
    )
    target_include_directories(SnipTextTests PRIVATE src)
    target_compile_definitions(SnipTextTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(SnipTextTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(SnipTextTests PRIVATE user32 shell32 ole32 shcore uuid)

    enable_testing()
    add_test(NAME text_normalizer COMMAND SnipTextTests)
endif()
