// Win32 type/memory adapter used only while compiling the build-selected
// platform-neutral JIS conversion sources verbatim.

#pragma once

#include "../wincompat.h"

#include <cstring>

using VOID = void;
using TCHAR = char;

#define ZeroMemory(destination, length) std::memset((destination), 0, (length))
