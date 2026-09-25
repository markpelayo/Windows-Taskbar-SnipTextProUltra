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
    src/Hotkeys.cpp
    src/Log.cpp
    src/MediaFolder.cpp
    src/Ocr.cpp
    src/RecordingIndicator.cpp
    src/RegionOverlay.cpp
    src/ScreenRecorder.cpp
    src/Settings.cpp
    src/TesseractOcr.cpp
    src/TextNormalizer.cpp
    src/Toast.cpp
    src/Util.cpp
    src/VideoSettings.cpp
    src/SnipText.rc
)

# --- the optional second OCR engine ----------------------------------------
#
# Windows.Media.Ocr is a language recogniser: it scores what it reads against
# a lexicon, discards regions with no dictionary word in them, and rewrites
# low-confidence characters into whatever makes a word. Neither behaviour can
# be switched off, which makes it wrong for serial numbers, hashes and symbol
# strings.
#
# Tesseract can have its dictionaries disabled, so it reports what it saw.
# It is statically linked and its model is embedded, so the executable stays
# a single self-contained file with no network and nothing to install.
#
# Off by default so that a plain `cmake -B build` still works with nothing
# but the Windows SDK, exactly as build.bat does.
option(SNIPTEXT_WITH_TESSERACT "Embed Tesseract as a fallback OCR engine" OFF)

if(SNIPTEXT_WITH_TESSERACT)
    find_package(Tesseract CONFIG REQUIRED)
    find_package(Leptonica CONFIG REQUIRED)

    # Fetched at configure time rather than committed: 4 MB of binary that is
    # the same for everyone does not belong in the history.
    set(TESSDATA_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/eng.traineddata")
    if(NOT EXISTS "${TESSDATA_FILE}")
        message(STATUS "Fetching the Tesseract trained model...")
        execute_process(
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch-tessdata.ps1"
            RESULT_VARIABLE TESSDATA_RESULT)
        if(NOT TESSDATA_RESULT EQUAL 0)
            message(FATAL_ERROR "Could not fetch eng.traineddata. Run scripts/fetch-tessdata.ps1 by hand, or configure with -DSNIPTEXT_WITH_TESSERACT=OFF.")
        endif()
    endif()
endif()

# --- which build is this? ---------------------------------------------------
#
# A released binary says "v1.5.0". A build from a working tree says
# "v1.5.0 (808fa04)", because during a round of UI changes the version number
# is the same on every build and is therefore useless for answering "is the
# thing you are looking at the thing I just changed?" — the question that cost
# real time on v1.2.0 and v1.3.0, when a stale commit was tagged and nothing
# on screen could have revealed it.
#
# CI passes -DSNIPTEXT_RELEASE=ON on tag builds only.
option(SNIPTEXT_RELEASE "This is a tagged release build" OFF)

set(SNIPTEXT_BUILD_SHA "")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" --no-optional-locks rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE SNIPTEXT_BUILD_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    # A dirty tree is a different binary from the commit it claims to be, and
    # saying so is the entire point of showing the hash at all.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" --no-optional-locks diff --quiet HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE SNIPTEXT_DIRTY
        ERROR_QUIET)
    # Guarded on the hash having worked. git can be installed and still have
    # nothing to say — an export rather than a checkout, say — in which case
    # rev-parse fails, diff fails too, and an unguarded append would produce a
    # version reading "1.5.0 (+)": a dirty marker on a hash that is not there.
    if(NOT SNIPTEXT_DIRTY EQUAL 0 AND NOT SNIPTEXT_BUILD_SHA STREQUAL "")
        set(SNIPTEXT_BUILD_SHA "${SNIPTEXT_BUILD_SHA}+")
    endif()
endif()
# Never empty: resource.h pastes this straight into a string literal, so an
# empty value would render as "1.5.0 ()".
if(SNIPTEXT_BUILD_SHA STREQUAL "")
    set(SNIPTEXT_BUILD_SHA "local")
endif()

add_executable(SnipText WIN32 ${SOURCES})

# The CMake target stays "SnipText" so the generated project names are short;
# the file on disk is SnipTextProUltra.exe.
set_target_properties(SnipText PROPERTIES OUTPUT_NAME "SnipTextProUltra")

target_include_directories(SnipText PRIVATE src)

target_compile_definitions(SnipText PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    # The backslashes are required, not decorative. Written as
    # SNIPTEXT_BUILD_SHA="${...}" the quotes are consumed by CMake's own
    # argument handling before the compiler sees them, and the macro arrives as
    # the bare token 808fa04 — which then fails to compile, or worse, compiles
    # as an identifier. Escaped, the macro is a string literal as intended.
    SNIPTEXT_BUILD_SHA=\"${SNIPTEXT_BUILD_SHA}\"
    SNIPTEXT_IS_RELEASE=$<IF:$<BOOL:${SNIPTEXT_RELEASE}>,1,0>
)

if(SNIPTEXT_WITH_TESSERACT)
    # This reaches the resource compiler as well as the C++ one — CMake
    # expands a target's COMPILE_DEFINITIONS into the RC command line, which
    # is how UNICODE already gets there — and that is what makes SnipText.rc
    # embed the model.
    target_compile_definitions(SnipText PRIVATE SNIPTEXT_WITH_TESSERACT)
    target_link_libraries(SnipText PRIVATE Tesseract::libtesseract leptonica)
endif()

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
    # Belt and braces for the Visual Studio generator, which writes
    # <GenerateManifest> from its own property rather than from the link
    # flags, so /MANIFEST:NO alone does not always reach it.
    set_target_properties(SnipText PROPERTIES VS_GLOBAL_GenerateManifest false)
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
