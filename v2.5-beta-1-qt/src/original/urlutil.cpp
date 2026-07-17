// Ported from v2.5-beta-1-modern/urlutil.cpp, which includes the shared
// artifacts/core/urlutil.cpp selected by chat.mak.

#include "urlutil.h"

#include "intl.h"

#include <QByteArray>
#include <QDesktopServices>
#include <QUrl>

#include <cctype>
#include <cstring>

const BYTE bLegalForURL[256] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x02,0x11,0x02,0x13,0x01,0x03,0x01,0x01,0x01,0x11,0x01,0x01,0x11,0x01,0x11,0x03,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,0x01,0x02,0x13,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,0x03,0x02,0x03,0x01,
    0x03,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x02,0x03,0x02,0x13,0x00,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01
};

const char szURLPREFIXSBROWSER[] =
    "http\0ftp\0https\0gopher\0\0";
const char szURLPREFIXS[] =
    "mic\0news\0mailto\0nntp\0telnet\0wais\0prospero\0\0";

namespace {
const char* nextCharacter(const char* position)
{
    if (!position || !*position) return position;
    return position + iBytesofChar(static_cast<BYTE>(*position));
}

const char* previousCharacter(const char* start, const char* position)
{
    if (!start || !position || position <= start) return start;
    const char* previous = start;
    const char* current = start;
    while (*current && current < position) {
        previous = current;
        const char* next = nextCharacter(current);
        if (next >= position) return current;
        current = next;
    }
    return previous;
}

bool sameAsciiNoCase(const char* left, const char* right, int rightLength)
{
    if (!left || !right || static_cast<int>(std::strlen(left)) != rightLength)
        return false;
    for (int index = 0; index < rightLength; ++index) {
        const unsigned char first = static_cast<unsigned char>(left[index]);
        const unsigned char second = static_cast<unsigned char>(right[index]);
        if (std::tolower(first) != std::tolower(second)) return false;
    }
    return true;
}
}

BOOL CUrlRec::bIsUrlPrefix(const char* urlPrefix, BOOL* forBrowser) const
{
    if (!urlPrefix) return FALSE;
    const int length = static_cast<int>(std::strlen(urlPrefix));
    for (const char* prefix = szURLPREFIXSBROWSER; *prefix;
         prefix += std::strlen(prefix) + 1) {
        if (sameAsciiNoCase(prefix, urlPrefix, length)) {
            if (forBrowser) *forBrowser = TRUE;
            return TRUE;
        }
    }
    for (const char* prefix = szURLPREFIXS; *prefix;
         prefix += std::strlen(prefix) + 1) {
        if (sameAsciiNoCase(prefix, urlPrefix, length)) {
            if (forBrowser) *forBrowser = FALSE;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CUrlRec::bIsUrlSuffix(const char* urlSuffix) const
{
    if (!urlSuffix || !*urlSuffix) return FALSE;
    int end = 0;
    int last = 0;
    for (const char* character = urlSuffix; *character;
         character = nextCharacter(character), ++end) {
        if (iBytesofChar(static_cast<BYTE>(*character)) == 2) {
            last = end + 1;
        } else {
            const BYTE flags = bLegalForURL[static_cast<BYTE>(*character)];
            if (!flags || !(flags & 0x01)) return FALSE;
            if (!(flags & 0x10)) last = end + 1;
        }
    }
    return last != 1;
}

const char* CUrlRec::FindPreceedingWord(const char* start,
                                        const char* colon) const
{
    const char* word = colon;
    while (word > start) {
        const char* previous = previousCharacter(start, word);
        const unsigned char value = static_cast<unsigned char>(*previous);
        if (!bIsUrlChar(value) || std::ispunct(value)) break;
        word = previous;
    }
    return word;
}

const char* CUrlRec::FindUrlEnd(const char* colon) const
{
    const char* end = colon;
    while (*end && (iBytesofChar(static_cast<BYTE>(*end)) == 2
                    || bIsUrlChar(static_cast<UCHAR>(*end)))) {
        end = nextCharacter(end);
    }
    while (end > colon) {
        const char* previous = previousCharacter(colon, end);
        const unsigned char value = static_cast<unsigned char>(*previous);
        if (value == '\\' || value == '/' || !std::ispunct(value)
            || previous <= colon) {
            break;
        }
        end = previous;
    }
    return end;
}

HRESULT CUrlRec::HrIdentifyUrls(const char* text, int* urlBounds,
                                int* urlNumber)
{
    if (!text || !urlBounds || !urlNumber) return URLUTIL_E_FAIL;
    const int capacity = *urlNumber;
    int count = 0;
    const char* start = text;
    while (const char* colon = std::strchr(start, ':')) {
        const char* word = FindPreceedingWord(start, colon);
        const QByteArray prefix(word, static_cast<int>(colon - word));
        if (!bIsUrlPrefix(prefix.constData(), nullptr)) {
            start = colon + 1;
            continue;
        }
        const char* end = FindUrlEnd(colon);
        const QByteArray possibleUrl(word, static_cast<int>(end - word));
        const char* suffix = possibleUrl.constData() + (colon - word) + 1;
        if (bIsUrlSuffix(suffix)) {
            if (count >= capacity) {
                *urlNumber = count;
                return URLUTIL_S_FALSE;
            }
            urlBounds[count * 2] = static_cast<int>(word - text);
            urlBounds[count * 2 + 1] = static_cast<int>(end - text);
            ++count;
        }
        start = end > colon ? end : colon + 1;
    }
    *urlNumber = count;
    return URLUTIL_NOERROR;
}

BOOL CUrlRec::bLaunchUrl(const char* url, BOOL newBrowser)
{
    Q_UNUSED(newBrowser);
    if (!url) return FALSE;
    const char* colon = std::strchr(url, ':');
    if (!colon) return FALSE;
    const char* word = FindPreceedingWord(url, colon);
    const QByteArray prefix(word, static_cast<int>(colon - word));
    if (!bIsUrlPrefix(prefix.constData(), nullptr) || !bIsUrlSuffix(colon + 1))
        return FALSE;
    return QDesktopServices::openUrl(QUrl::fromEncoded(QByteArray(url)));
}
