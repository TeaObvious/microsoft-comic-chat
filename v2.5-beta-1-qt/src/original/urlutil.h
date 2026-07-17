// Ported from the build-selected artifacts/inc/urlutil.h and
// artifacts/core/urlutil.cpp.  QDesktopServices replaces ShellExecute only.

#pragma once

#include "wincompat.h"

using HRESULT = LONG;

constexpr HRESULT URLUTIL_NOERROR = 0;
constexpr HRESULT URLUTIL_S_FALSE = 1;
constexpr HRESULT URLUTIL_E_FAIL = static_cast<HRESULT>(0x80004005U);

extern const BYTE bLegalForURL[256];
extern const char szURLPREFIXSBROWSER[];
extern const char szURLPREFIXS[];

class CUrlRec {
public:
    CUrlRec() = default;
    ~CUrlRec() = default;

    HRESULT HrIdentifyUrls(const char* text, int* urlBounds, int* urlNumber);
    BOOL bLaunchUrl(const char* url, BOOL newBrowser = TRUE);

private:
    BOOL bIsUrlPrefix(const char* urlPrefix, BOOL* forBrowser = nullptr) const;
    BOOL bIsUrlSuffix(const char* urlSuffix) const;
    BOOL bIsUrlChar(UCHAR character) const
    {
        const BYTE flags = bLegalForURL[character];
        return flags && (flags & 0x01);
    }
    const char* FindUrlEnd(const char* colon) const;
    const char* FindPreceedingWord(const char* start, const char* colon) const;
};

