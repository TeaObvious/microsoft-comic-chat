// Ported from the build-selected artifacts/inc/ccomp.h used by
// v2.5-beta-1-modern/ccomp.cpp.

#pragma once

#include "wincompat.h"

#include <QByteArray>

// Break down of a nickname!username@ipaddress mask for user comparing.
// The Win32 original stores pointers into the caller's mutable buffer.  The
// Qt boundary keeps one owned byte copy so the same pointer/length fields stay
// valid for the lifetime of this structure.
struct PRUSERMATCH {
    PRUSERMATCH() = default;
    PRUSERMATCH(const PRUSERMATCH& other);
    PRUSERMATCH& operator=(const PRUSERMATCH& other);

    QByteArray m_maskStorage;
    const char* szTheMask = nullptr;
    const char* szNickname = nullptr;
    UINT cbNickname = 0;
    const char* szUserName = nullptr;
    UINT cbUserName = 0;
    const char* szIPAddress = nullptr;
    UINT cbIPAddress = 0;
};

using PPRUSERMATCH = PRUSERMATCH*;

BOOL bMatchAll(const char* string, UINT length);
BOOL bGetUserMatchFromMask(const char* identityMask,
                           PPRUSERMATCH userMatch);
BOOL bIsMaskCompare(const char* mask, UINT maskLength,
                    const char* string, UINT stringLength);
BOOL bIsMatch(const PRUSERMATCH* userMatch, const char* nickname,
              const char* userName, const char* ipAddress);
