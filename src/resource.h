// resource.h — resource identifiers and the version number.

#pragma once

// Must be 1: Windows shows the lowest-numbered icon resource as the file's
// Explorer icon, and it is also what the taskbar uses for a pinned shortcut.
#define IDI_SNIPTEXT 1

// The Tesseract trained model, embedded so the program stays one file with
// nothing to install beside it and no network. Present only in builds
// compiled with SNIPTEXT_WITH_TESSERACT.
#define IDR_TESSDATA_ENG 100

// The version, in the two forms the toolchain needs. Included by both
// SnipText.rc and App.cpp, so the file properties, the startup log line and
// the resource block can never disagree with each other.
//
// Still has to be kept in step by hand with VERSION and with
// SnipText.manifest — see docs/RELEASING.md.
#define SNIPTEXT_VERSION_COMMA  1,6,0,0
#define SNIPTEXT_VERSION_STRING "1.6.0.0"
#define SNIPTEXT_VERSION_WIDE   L"1.6.0"

// --- which build is this? ---------------------------------------------------
//
// The build system defines these; the fallbacks keep a compiler invoked by
// hand working. SNIPTEXT_BUILD_SHA is a NARROW literal because passing a wide
// one through a CMake -D and a .bat quoting layer intact is more trouble than
// it is worth, so it is widened here instead.
//
// The widening is done with token pasting rather than by writing
// `L"" SNIPTEXT_BUILD_SHA`. Adjacent narrow and wide literals do concatenate
// to wide under C++11 and later, and MSVC does implement that — but MSVC's own
// C2308 documentation still says flatly that you cannot concatenate a wide and
// a non-wide string, and this is not worth a twenty-minute CI round trip to
// find out. SNIPTEXT_WIDEN is the same two-step used by _CRT_WIDE and __TEXT
// in the Windows headers, and is guaranteed. Two steps are required: the
// argument has to be macro-expanded before `L ## x` pastes onto it.
// The build system always supplies a non-empty SHA — "local" when git is not
// there to ask — so that this stays plain string-literal concatenation. A
// ternary would have produced an expression, and an expression cannot be
// pasted next to L"..." the way the title row and the log line paste it.
#ifndef SNIPTEXT_BUILD_SHA
#define SNIPTEXT_BUILD_SHA "local"
#endif
#ifndef SNIPTEXT_IS_RELEASE
#define SNIPTEXT_IS_RELEASE 0
#endif

#define SNIPTEXT_WIDEN2(x) L ## x
#define SNIPTEXT_WIDEN(x)  SNIPTEXT_WIDEN2(x)

// "1.5.0" on a tagged release, "1.5.0 (808fa04)" on anything else, and a
// trailing + on the hash when the tree had uncommitted changes.
#if SNIPTEXT_IS_RELEASE
#define SNIPTEXT_VERSION_DISPLAY SNIPTEXT_VERSION_WIDE
#else
#define SNIPTEXT_VERSION_DISPLAY \
    SNIPTEXT_VERSION_WIDE L" (" SNIPTEXT_WIDEN(SNIPTEXT_BUILD_SHA) L")"
#endif
