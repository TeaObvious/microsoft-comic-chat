// Ported from v2.5-beta-1-modern/intl.c. The disabled MIME-registry/HTML
// blocks are intentionally not compiled by the original Chat build path used
// here. QPainter is the replacement boundary for GDI text measurement.

#include "intl.h"

#include "ccommon.h"
#include "format.h"

#include <QFontMetrics>
#include <QLocale>
#include <QPainter>

#include <algorithm>
#include <cstring>

namespace {
constexpr BOOL fDoubleByte = TRUE;
constexpr BOOL fSingleByte = FALSE;

SCRIPTINFO* g_pMime = nullptr;

UCHAR byteAt(const char* text, int offset = 0)
{
    return text ? static_cast<UCHAR>(text[offset]) : 0;
}

bool farEastCodePage(int codePage)
{
    return codePage == 932 || codePage == 949
        || codePage == 950 || codePage == 936;
}

int byteSize(const SCRIPTINFO* mime, UCHAR character)
{
    return mime && IsDBCSLeadByteEx(mime->iCp, character) ? 2 : 1;
}

BOOL isWrapUp(const SCRIPTINFO* mime, BOOL dbcs, const char* text)
{
    return mime && mime->IsWrapUp ? mime->IsWrapUp(dbcs, text) : FALSE;
}

BOOL isWrapDown(const SCRIPTINFO* mime, BOOL dbcs, const char* text)
{
    return mime && mime->IsWrapDown ? mime->IsWrapDown(dbcs, text) : FALSE;
}

BOOL isBreakable(const SCRIPTINFO* mime, const char* text)
{
    if (!mime || !text) return FALSE;
    const UCHAR character = byteAt(text);
    return character == ' ' || character == '\t' || character == '\r'
        || character == '\n'
        || IsDBCSLeadByteEx(mime->iCp, character)
        || isWrapUp(mime, fSingleByte, text)
        || isWrapDown(mime, fSingleByte, text);
}

BYTE characterSetForCodePage(int codePage)
{
    switch (codePage) {
    case 932: return SHIFTJIS_CHARSET;
    case 949: return HANGEUL_CHARSET;
    case 950: return CHINESEBIG5_CHARSET;
    case 936: return GB2312_CHARSET;
    default: return ANSI_CHARSET;
    }
}

SCRIPTINFO aDefScriptInfo[] = {
    {0, 0, 1252, 1252, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 932, 50220, ICHRCNV_JIS, IsWrapUp932, IsWrapDown932},
    {0, 0, 932, 51932, ICHRCNV_EUCJP, IsWrapUp932, IsWrapDown932},
    {0, 0, 932, 932, ICHRCNV_SJIS, IsWrapUp932, IsWrapDown932},
    {0, 0, 949, 949, ICHRCNV_NONE, IsWrapUp949, IsWrapDown949},
    {0, 0, 1250, 1250, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 1250, 28592, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 1251, 1251, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 1251, 20866, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 874, 874, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 950, 950, ICHRCNV_NONE, IsWrapUp950, IsWrapDown950},
    {0, 0, 936, 936, ICHRCNV_NONE, IsWrapUp936, IsWrapDown936},
    {0, 0, 1253, 1253, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 1254, 1254, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 1255, 1255, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 1256, 1256, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 1257, 1257, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs},
    {0, 0, 1258, 1258, ICHRCNV_NONE, IsWrapSbcs, IsWrapSbcs}
};

BOOL measure(QPainter* painter, const char* string, int length,
             CDWordArray* formatting, QSize* size)
{
    if (!painter || !string || !size) return FALSE;
    *size = GetFormattedTextExtent(
        painter, string, static_cast<DWORD>(std::max(0, length)), formatting);
    return TRUE;
}
}

extern "C" BOOL IsDBCSLeadByteEx(int codePage, BYTE character)
{
    switch (codePage) {
    case 932:
        return (character >= 0x81 && character <= 0x9f)
            || (character >= 0xe0 && character <= 0xfc);
    case 936:
    case 949:
    case 950:
        return character >= 0x81 && character <= 0xfe;
    default:
        return FALSE;
    }
}

extern "C" BOOL IsStringBreakable(SCRIPTINFO* mime, const char* position,
                                    const char* start, int length)
{
    if (!mime || !position || !start || position >= start + length
        || !*position) {
        return FALSE;
    }
    position += byteSize(mime, byteAt(position));
    return position < start + length && isBreakable(mime, position);
}

extern "C" char* FindBreakableCharForFE(SCRIPTINFO* mime, char* start,
                                          int length, BOOL* wrapupAtTop)
{
    if (!mime || !start || length <= 0 || !wrapupAtTop) return nullptr;
    char* position = start;
    int remaining = length;
    int wrapupBytes = 0;
    *wrapupAtTop = TRUE;
    while (remaining--) {
        if (isWrapUp(mime, fDoubleByte, position)) {
            wrapupBytes = 2;
            break;
        }
        if (isWrapUp(mime, fSingleByte, position)
            || *position == ' ') {
            wrapupBytes = 1;
            break;
        }
        *wrapupAtTop = FALSE;
        if (isWrapDown(mime, fSingleByte, position)
            || IsDBCSLeadByteEx(mime->iCp, byteAt(position))) {
            break;
        }
        ++position;
    }
    if (remaining <= 0) return nullptr;
    if (wrapupBytes > 0) {
        char* next = position + wrapupBytes;
        if (next <= start + length - 1
            && (isWrapUp(mime, fDoubleByte, next)
                || isWrapUp(mime, fSingleByte, next))) {
            position = next;
        }
    }
    return position;
}

extern "C" char* CharNextEx(int codePage, const char* position)
{
    if (!position) return nullptr;
    return const_cast<char*>(position
        + (IsDBCSLeadByteEx(codePage, byteAt(position)) ? 2 : 1));
}

extern "C" BOOL IsTrailByte(int codePage, const char* start,
                              const char* current)
{
    if (!start || !current || current <= start) return FALSE;
    const char* position = current;
    while (position > start) {
        --position;
        if (!IsDBCSLeadByteEx(codePage, byteAt(position))) {
            ++position;
            break;
        }
    }
    return static_cast<BOOL>((current - position) & 1);
}

extern "C" char* CharPrevEx(int codePage, const char* start,
                              const char* position)
{
    if (!start || !position || position <= start) {
        return const_cast<char*>(position);
    }
    return const_cast<char*>(IsTrailByte(codePage, start, position - 1)
        ? position - 2 : position - 1);
}

extern "C" int AdjustPunctuation(SCRIPTINFO* mime, const char* start,
                                   const char* position, int* breakOffset,
                                   int length)
{
    if (!mime || !start || !position || length <= 0
        || position < start || position >= start + length) {
        return 0;
    }
    int adjustment = 0;
    if (isWrapDown(mime, fDoubleByte, position)
        || isWrapDown(mime, fSingleByte, position)) {
        adjustment = IsTrailByte(mime->iCp, start, position - 1) ? -2 : -1;
    } else {
        const int breakBytes = byteSize(mime, byteAt(position));
        const char* next = position + breakBytes;
        if (next < start + length
            && (isWrapUp(mime, fDoubleByte, next)
                || isWrapUp(mime, fSingleByte, next) || *next == ' ')) {
            int nextBytes = byteSize(mime, byteAt(next));
            const char* nested = next + nextBytes;
            if (nested < start + length
                && (isWrapUp(mime, fDoubleByte, nested)
                    || isWrapUp(mime, fSingleByte, nested))) {
                next = nested;
            }
            adjustment = static_cast<int>(next - position);
        }
    }
    const char* adjusted = position + adjustment;
    if (adjusted < start || adjusted > start + length - 1) return 0;
    if (breakOffset) *breakOffset += adjustment;
    return adjustment;
}

extern "C" BOOL IsWrapSbcs(BOOL, const char*) { return FALSE; }

extern "C" BOOL IsWrapDown932(BOOL dbcs, const char* text)
{
    if (!text) return FALSE;
    const UCHAR first = byteAt(text), second = byteAt(text, 1);
    return dbcs
        ? first == 0x81 && second >= 0x65 && second <= 0x79
              && (second & 1)
        : first == '(' || first == '<' || first == '[' || first == '{'
              || first == 0xa2;
}

extern "C" BOOL IsWrapUp932(BOOL dbcs, const char* text)
{
    if (!text) return FALSE;
    const UCHAR first = byteAt(text), second = byteAt(text, 1);
    return dbcs
        ? first == 0x81
              && ((second >= 0x41 && second <= 0x49)
                  || (second >= 0x66 && second <= 0x72 && !(second & 1)))
        : first == ')' || first == ',' || first == '.' || first == '>'
              || first == ']' || first == '}' || first == 0xa3;
}

extern "C" BOOL IsWrapDown949(BOOL dbcs, const char* text)
{
    if (!text) return FALSE;
    const UCHAR first = byteAt(text), second = byteAt(text, 1);
    return dbcs
        ? (first == 0xa1
           && ((second >= 0xae && second <= 0xbc && !(second & 1))
               || second == 0xcc))
              || (first == 0xa3
                  && (second == 0xa4 || second == 0xa8 || second == 0xdb
                      || second == 0xdc || second == 0xf8))
        : first == '$' || first == '(' || first == '[' || first == '\\'
              || first == '{';
}

extern "C" BOOL IsWrapUp949(BOOL dbcs, const char* text)
{
    if (!text) return FALSE;
    const UCHAR first = byteAt(text), second = byteAt(text, 1);
    return dbcs
        ? (first == 0xa1
           && ((second >= 0xaf && second <= 0xbd && (second & 1))
               || (second >= 0xc6 && second <= 0xc9) || second == 0xcb))
              || (first == 0xa3
                  && (second == 0xa1 || second == 0xa5 || second == 0xa9
                      || second == 0xac || second == 0xae || second == 0xba
                      || second == 0xbb || second == 0xbf || second == 0xdd
                      || second == 0xfd))
        : first == '!' || first == '%' || first == ')' || first == ','
              || first == '.' || first == ':' || first == ';' || first == '?'
              || first == ']' || first == '}';
}

extern "C" BOOL IsWrapDown950(BOOL dbcs, const char* text)
{
    if (!text) return FALSE;
    const UCHAR first = byteAt(text), second = byteAt(text, 1);
    return dbcs
        ? first == 0xa1
              && ((second >= 0x5d && second <= 0x7d)
                  || (second >= 0xa1 && second <= 0xab))
              && (second & 1)
        : first == '(' || first == '<' || first == '[' || first == '{';
}

extern "C" BOOL IsWrapUp950(BOOL dbcs, const char* text)
{
    if (!text) return FALSE;
    const UCHAR first = byteAt(text), second = byteAt(text, 1);
    return dbcs
        ? first == 0xa1
              && ((second >= 0x41 && second <= 0x49)
                  || (second >= 0x4d && second <= 0x54)
                  || (((second >= 0x5e && second <= 0x7e)
                       || (second >= 0xa2 && second <= 0xac))
                      && !(second & 1)))
        : first == ')' || first == ',' || first == '.' || first == '>'
              || first == ']' || first == '}';
}

extern "C" BOOL IsWrapDown936(BOOL dbcs, const char* text)
{
    if (!text) return FALSE;
    const UCHAR first = byteAt(text), second = byteAt(text, 1);
    return dbcs
        ? (first == 0xa1
           && (second == 0xae || second == 0xb0 || second == 0xb2
               || second == 0xb4 || second == 0xb6 || second == 0xb8
               || second == 0xba || second == 0xbc || second == 0xa4))
              || (first == 0xa3
                  && (second == 0xa8 || second == 0xdb || second == 0xfb
                      || second == 0xae))
        : first == '(' || first == '[' || first == '{';
}

extern "C" BOOL IsWrapUp936(BOOL dbcs, const char* text)
{
    if (!text) return FALSE;
    const UCHAR first = byteAt(text), second = byteAt(text, 1);
    return dbcs
        ? (first == 0xa1
           && ((second >= 0xa2 && second <= 0xad && (second & 1))
               || second == 0xaf || second == 0xb1 || second == 0xb3
               || second == 0xb5 || second == 0xb7 || second == 0xb9
               || second == 0xbb || second == 0xbd || second == 0xbf
               || second == 0xc3))
              || (first == 0xa3
                  && (second == 0xa1 || second == 0xa2 || second == 0xa7
                      || second == 0xa9 || second == 0xac || second == 0xae
                      || second == 0xba || second == 0xbb || second == 0xbf
                      || second == 0xdd || second == 0xe0 || second == 0xf3
                      || second == 0xfd))
        : first == '!' || first == ')' || first == ',' || first == '.'
              || first == ':' || first == ';' || first == '?' || first == ']'
              || first == '}';
}

extern "C" BOOL FindSubStringForINTLThatFits(
    void* value, QPainter* painter, const char* string, int byteLength,
    CDWordArray* formatting, int* bytesFit, BOOL* hasBlankOrAlike,
    QSize* size, int maximumExtent)
{
    auto* mime = static_cast<SCRIPTINFO*>(value);
    if (!mime || !painter || !string || byteLength <= 0 || !bytesFit
        || !hasBlankOrAlike || !size) {
        return FALSE;
    }

    *hasBlankOrAlike = FALSE;
    const bool farEast = farEastCodePage(mime->iCp);
    int back = byteLength - 1;
    int front = 0;
    int current = 0;
    int temporary = 0;
    const char* position = string + back;
    if (farEast && IsTrailByte(mime->iCp, string, position)) --position;

    while (true) {
        if (farEast ? (isBreakable(mime, position) && *position != '.')
                    : (*position == ' ')) {
            break;
        }
        if (position == string) return FALSE;
        position = CharPrevEx(mime->iCp, string, position);
    }
    back = static_cast<int>(position - string);

    if (back + 1 > maximumExtent) {
        temporary = std::min(maximumExtent, byteLength - 1);
        if (farEast
            && IsTrailByte(mime->iCp, string, string + temporary)) {
            --temporary;
        }
        while (temporary < back) {
            if (farEast ? isBreakable(mime, string + temporary)
                        : string[temporary] == ' ') {
                break;
            }
            ++temporary;
        }
        back = temporary;
    }

    *hasBlankOrAlike = TRUE;
    int breakBytes = farEast ? byteSize(mime, byteAt(string + back)) : 1;
    QSize frontSize;
    if (!measure(painter, string, back + breakBytes, formatting, size)) {
        return FALSE;
    }
    if (size->width() <= maximumExtent) {
        frontSize = *size;
        front = back;
        goto found_fit;
    }

    position = string;
    while (true) {
        if (farEast ? isBreakable(mime, position) : *position == ' ') break;
        position = CharNextEx(mime->iCp, position);
        if (position == string + back) {
            *bytesFit = back;
            return FALSE;
        }
    }
    front = static_cast<int>(position - string);
    breakBytes = farEast ? byteSize(mime, byteAt(string + front)) : 1;
    if (!measure(painter, string, front + breakBytes, formatting, size)) {
        return FALSE;
    }
    *bytesFit = front;
    if (size->width() > maximumExtent) return FALSE;
    frontSize = *size;

    while (front + breakBytes < back) {
        current = (front + back) / 2;
        if (farEast && IsTrailByte(mime->iCp, string, string + current)) {
            ++current;
        }
        for (temporary = current; temporary < back; ++temporary) {
            if (farEast
                ? IsStringBreakable(mime, string + temporary, string,
                                    byteLength)
                : string[temporary] == ' ') {
                break;
            }
        }
        breakBytes = farEast
            ? byteSize(mime, byteAt(string + temporary)) : 1;
        if (temporary < back) {
            QSize currentSize;
            if (!measure(painter, string, temporary + breakBytes,
                         formatting, &currentSize)) {
                return FALSE;
            }
            if (currentSize.width() <= maximumExtent) {
                front = temporary;
                frontSize = currentSize;
            } else {
                back = temporary;
            }
        }
        if (temporary < back) continue;

        if (farEast) {
            current -= IsTrailByte(mime->iCp, string,
                                   string + current - 1) ? 2 : 1;
        }
        for (temporary = current; temporary > front; --temporary) {
            if (farEast
                ? IsStringBreakable(mime, string + temporary, string,
                                    byteLength)
                : string[temporary] == ' ') {
                break;
            }
            if (farEast && IsTrailByte(mime->iCp, string,
                                       string + temporary - 1)) {
                --temporary;
            }
        }
        breakBytes = farEast
            ? byteSize(mime, byteAt(string + temporary)) : 1;
        if (temporary > front) {
            QSize currentSize;
            if (!measure(painter, string, temporary + breakBytes,
                         formatting, &currentSize)) {
                return FALSE;
            }
            if (currentSize.width() <= maximumExtent) {
                front = temporary;
                frontSize = currentSize;
            } else {
                back = temporary;
            }
        }
        if (temporary == front) break;
    }

found_fit:
    if (farEast
        && AdjustPunctuation(mime, string, string + front, &front,
                             byteLength)) {
        breakBytes = byteSize(mime, byteAt(string + front));
        if (!measure(painter, string, front + breakBytes, formatting,
                     &frontSize)) {
            return FALSE;
        }
    }
    *size = frontSize;
    if (breakBytes == 1 && string[front] == ' ') {
        *bytesFit = front;
    } else {
        breakBytes = farEast ? byteSize(mime, byteAt(string + front)) : 1;
        *bytesFit = front + breakBytes;
    }
    return (*bytesFit != 0 || *bytesFit == byteLength - 1) ? TRUE : FALSE;
}

extern "C" void* GetMime() { return g_pMime; }

extern "C" void SetMime(DWORD characterSet)
{
    int codePage = 0;
    switch (static_cast<BYTE>(characterSet)) {
    case SHIFTJIS_CHARSET: codePage = 932; break;
    case HANGEUL_CHARSET: codePage = 949; break;
    case CHINESEBIG5_CHARSET: codePage = 950; break;
    case GB2312_CHARSET: codePage = 936; break;
    default:
        g_pMime = nullptr;
        return;
    }
    for (SCRIPTINFO& info : aDefScriptInfo) {
        if (info.iCp == codePage) {
            g_pMime = &info;
            return;
        }
    }
    g_pMime = nullptr;
}

extern "C" int iBytesofChar(BYTE character)
{
    return g_pMime ? byteSize(g_pMime, character) : 1;
}

extern "C" BYTE GetCorrectCharSet()
{
    const QLocale locale = QLocale::system();
    switch (locale.language()) {
    case QLocale::Czech:
    case QLocale::Hungarian:
    case QLocale::Polish:
    case QLocale::Slovenian:
        return EASTEUROPE_CHARSET;
    case QLocale::Arabic: return ARABIC_CHARSET;
    case QLocale::Greek: return GREEK_CHARSET;
    case QLocale::Hebrew: return HEBREW_CHARSET;
    case QLocale::Russian: return RUSSIAN_CHARSET;
    case QLocale::Turkish: return TURKISH_CHARSET;
    case QLocale::Japanese: return SHIFTJIS_CHARSET;
    case QLocale::Korean: return HANGEUL_CHARSET;
    case QLocale::Thai: return THAI_CHARSET;
    case QLocale::Chinese:
        return locale.territory() == QLocale::Taiwan
            ? CHINESEBIG5_CHARSET : GB2312_CHARSET;
    default:
        return ANSI_CHARSET;
    }
}

QString IntlTextToQString(const char* bytes, int length)
{
    if (!bytes) return QString();
    if (length < 0) length = static_cast<int>(std::strlen(bytes));
    const QByteArray input(bytes, length);
    if (!g_pMime) return QString::fromUtf8(input);
    QString output;
    if (!bCharacterSetToWide(input, characterSetForCodePage(g_pMime->iCp),
                             &output)) {
        return QString::fromLatin1(input);
    }
    return output;
}

QByteArray IntlTextFromQString(QStringView text)
{
    if (!g_pMime) return text.toString().toUtf8();
    QByteArray output;
    if (!bWideToCharacterSet(text, characterSetForCodePage(g_pMime->iCp),
                             &output)) {
        return text.toString().toUtf8();
    }
    return output;
}

QByteArray IntlTextFromUtf8(const char* bytes, int length)
{
    if (!bytes) return QByteArray();
    if (length < 0) length = static_cast<int>(std::strlen(bytes));
    return IntlTextFromQString(QStringView(QString::fromUtf8(bytes, length)));
}

CDWordArray* IntlFormattingFromUtf8(const char* utf8,
                                    const CDWordArray* formatting)
{
    if (!formatting) return nullptr;
    if (!g_pMime || !utf8) {
        return CopyFormatting(const_cast<CDWordArray*>(formatting));
    }
    const QByteArray source(utf8);
    auto* converted = new CDWordArray;
    for (int index = 0; index < formatting->GetSize(); ++index) {
        const DWORD element = formatting->GetAt(index);
        const int oldOffset = std::min<int>(HIWORD(element), source.size());
        const QString prefix = QString::fromUtf8(source.constData(), oldOffset);
        const int newOffset = IntlTextFromQString(QStringView(prefix)).size();
        converted->Add(MAKELONG(LOWORD(element),
                                static_cast<WORD>(newOffset)));
    }
    return converted;
}
