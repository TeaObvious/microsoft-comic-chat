// The Modern build selects this shared header for jis2sjis.cpp/sjis2jis.cpp.

#pragma once

#include "wincompat.h"

#include "../../../artifacts/inc/fechrcnv.h"

void ShiftJISChar_to_JISChar(UCHAR* shiftJis, UCHAR* jis);
void JISChar_to_ShiftJISChar(UCHAR* jis, UCHAR* shiftJis);
