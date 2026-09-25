// resource.h — resource identifiers and the version number.

#pragma once

// Must be 1: Windows shows the lowest-numbered icon resource as the file's
// Explorer icon, and it is also what the taskbar uses for a pinned shortcut.
#define IDI_SNIPTEXT 1

// The version, in the two forms the toolchain needs. Included by both
// SnipText.rc and App.cpp, so the file properties, the startup log line and
// the resource block can never disagree with each other.
//
// Still has to be kept in step by hand with VERSION and with
// SnipText.manifest — see docs/RELEASING.md.
#define SNIPTEXT_VERSION_COMMA  1,3,1,0
#define SNIPTEXT_VERSION_STRING "1.3.1.0"
#define SNIPTEXT_VERSION_WIDE   L"1.3.1"
