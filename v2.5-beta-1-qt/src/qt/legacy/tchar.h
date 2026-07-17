// Narrow TCHAR adapter for the original non-UNICODE Comic Chat build.

#pragma once

#include <cstring>

#define _tcslen(value) std::strlen(value)
