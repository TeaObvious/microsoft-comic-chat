// Ported from the build-selected artifacts/core/ccomp.cpp used by
// v2.5-beta-1-modern/ccomp.cpp.

#include "ccomp.h"

#include <cctype>
#include <cstring>

namespace {
unsigned char upperByte(char value)
{
    return static_cast<unsigned char>(
        std::toupper(static_cast<unsigned char>(value)));
}

void parseOwnedMask(PRUSERMATCH* userMatch)
{
    userMatch->szTheMask = userMatch->m_maskStorage.constData();
    userMatch->szNickname = userMatch->szTheMask;
    userMatch->szUserName = userMatch->szTheMask;
    userMatch->szIPAddress = nullptr;
    userMatch->cbNickname = 0;
    userMatch->cbUserName = 0;
    userMatch->cbIPAddress = 0;

    UINT last = 0;
    UINT index = 0;
    while (userMatch->szTheMask[index] != '\0') {
        if (userMatch->szTheMask[index] == '!') {
            userMatch->szUserName = userMatch->szTheMask + index + 1;
            userMatch->cbNickname = index - last;
            last = index + 1;
        } else if (userMatch->szTheMask[index] == '@') {
            userMatch->szIPAddress = userMatch->szTheMask + index + 1;
            userMatch->cbUserName = index - last;
            last = index + 1;
        }
        ++index;
    }

    if (userMatch->szIPAddress) {
        userMatch->cbIPAddress = index - last;
    } else if (userMatch->szUserName
               && userMatch->szUserName != userMatch->szNickname) {
        userMatch->cbUserName = index - last;
    } else {
        userMatch->cbNickname = index - last;
    }

    if (userMatch->szNickname
        && (userMatch->cbNickname == 0
            || bMatchAll(userMatch->szNickname,
                         userMatch->cbNickname))) {
        userMatch->szNickname = nullptr;
        userMatch->cbNickname = 0;
    }
    if (userMatch->szUserName
        && (userMatch->cbUserName == 0
            || bMatchAll(userMatch->szUserName,
                         userMatch->cbUserName))) {
        userMatch->szUserName = nullptr;
        userMatch->cbUserName = 0;
    }
    if (userMatch->szIPAddress
        && (userMatch->cbIPAddress == 0
            || bMatchAll(userMatch->szIPAddress,
                         userMatch->cbIPAddress))) {
        userMatch->szIPAddress = nullptr;
        userMatch->cbIPAddress = 0;
    }
}
}

PRUSERMATCH::PRUSERMATCH(const PRUSERMATCH& other)
    : m_maskStorage(other.m_maskStorage)
{
    parseOwnedMask(this);
}

PRUSERMATCH& PRUSERMATCH::operator=(const PRUSERMATCH& other)
{
    if (this != &other) {
        m_maskStorage = other.m_maskStorage;
        parseOwnedMask(this);
    }
    return *this;
}

BOOL bMatchAll(const char* string, UINT length)
{
    if (!string || length == 0) return FALSE;

    UINT index = 0;
    while (index < length && string[index] == '?') ++index;
    return index == length - 1 && string[index] == '*';
}

BOOL bGetUserMatchFromMask(const char* identityMask,
                           PPRUSERMATCH userMatch)
{
    if (!identityMask || !userMatch) return FALSE;
    userMatch->m_maskStorage = QByteArray(identityMask);
    parseOwnedMask(userMatch);
    return TRUE;
}

BOOL bIsMaskCompare(const char* mask, UINT maskLength,
                    const char* string, UINT stringLength)
{
    if (maskLength == 0) return TRUE;
    if (!mask || !string) return FALSE;

    UINT maskIndex = 0;
    UINT stringIndex = 0;
    UINT lastMask = ~UINT{0};
    UINT lastCharacter = 0;

    while (maskIndex != maskLength && stringIndex != stringLength) {
        if (mask[maskIndex] != '*' && mask[maskIndex] != '?') {
            if (upperByte(mask[maskIndex]) != upperByte(string[stringIndex])) {
                if (lastMask != ~UINT{0}) {
                    maskIndex = lastMask;
                    stringIndex = lastCharacter + 1;
                    lastCharacter = stringIndex;
                    continue;
                }
                return FALSE;
            }
            ++maskIndex;
            ++stringIndex;
            continue;
        }

        if (mask[maskLength - 1] != '*'
            && mask[maskLength - 1] != '?') {
            if (upperByte(mask[maskLength - 1])
                != upperByte(string[stringLength - 1])) {
                if (lastMask != ~UINT{0}) {
                    maskIndex = lastMask;
                    stringIndex = lastCharacter + 1;
                    lastCharacter = stringIndex;
                    continue;
                }
                return FALSE;
            }
            --maskLength;
            --stringLength;
            continue;
        }

        if (mask[maskIndex] == '?') {
            ++maskIndex;
            ++stringIndex;
            continue;
        }
        if (mask[maskLength - 1] == '?') {
            --maskLength;
            --stringLength;
            continue;
        }
        if (maskIndex + 1 == maskLength) return TRUE;

        if (lastMask != maskIndex) {
            lastMask = maskIndex;
            lastCharacter = stringIndex;
        }
        if (upperByte(mask[maskIndex + 1])
            == upperByte(string[stringIndex])) {
            maskIndex += 2;
            ++stringIndex;
            continue;
        }
        ++stringIndex;
    }

    if (maskIndex == maskLength && stringIndex == stringLength) return TRUE;
    return maskIndex + 1 == maskLength
        && mask[maskIndex] == '*'
        && stringIndex == stringLength;
}

BOOL bIsMatch(const PRUSERMATCH* userMatch, const char* nickname,
              const char* userName, const char* ipAddress)
{
    if (!userMatch || !nickname || !userName || !ipAddress) return FALSE;

    if (userMatch->szNickname && userMatch->cbNickname) {
        UINT maskLength = userMatch->cbNickname;
        const char* mask = userMatch->szNickname;
        const char* comparedNick =
            mask[0] != '\'' && nickname[0] == '\'' ? nickname + 1 : nickname;
        if (mask[0] == '\'' && nickname[0] != '\'') {
            ++mask;
            --maskLength;
        }
        if (!bIsMaskCompare(mask, maskLength, comparedNick,
                            static_cast<UINT>(std::strlen(comparedNick)))) {
            return FALSE;
        }
    }
    if (userMatch->szUserName && userMatch->cbUserName
        && !bIsMaskCompare(userMatch->szUserName,
                           userMatch->cbUserName, userName,
                           static_cast<UINT>(std::strlen(userName)))) {
        return FALSE;
    }
    if (userMatch->szIPAddress && userMatch->cbIPAddress
        && !bIsMaskCompare(userMatch->szIPAddress,
                           userMatch->cbIPAddress, ipAddress,
                           static_cast<UINT>(std::strlen(ipAddress)))) {
        return FALSE;
    }
    return TRUE;
}
