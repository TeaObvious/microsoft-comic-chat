// Ported from v2.5-beta-1-modern/format.cpp. RichEdit/HDC operations are the
// Qt boundary; formatting DWORDs, control parsing and offset operations retain
// the original representation and branch order.

#include "format.h"

#include "chat.h"
#include "defines.h"
#include "intl.h"
#include "urlutil.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QStringList>
#include <QTextCursor>
#include <QTextEdit>

#include <algorithm>
#include <cctype>
#include <cstring>

COLORREF linkColor = RGB(0, 0, 255);
CUrlRec g_urlRec;

namespace {
constexpr char formatChEOS = '\0';
constexpr char formatChComma = ',';
constexpr char formatChTransparent = 0x01;

bool decimal(char value)
{
    return value >= '0' && value <= '9';
}

QString firstInstalled(const char* const* names, int count)
{
    const QStringList families = QFontDatabase::families();
    for (int index = 0; index < count; ++index) {
        const QString candidate = QString::fromLatin1(names[index]);
        for (const QString& family : families) {
            if (family.compare(candidate, Qt::CaseInsensitive) == 0) {
                return family;
            }
        }
    }
    return {};
}

QFont fontForFormat(const QFont& original, WORD format)
{
    QFont font = original;
    font.setBold(format & wBold);
    font.setItalic(format & wItalic);
    font.setUnderline(format & wUnderline);
    static const QString fixed = firstInstalled(FIXEDPITCHFACENAMES, FIXEDPITCHNUMBER);
    static const QString symbol = firstInstalled(SYMBOLFACENAMES, SYMBOLNUMBER);
    if ((format & wFixedPitch) && !fixed.isEmpty()) {
        font.setFamily(fixed);
        font.setStyleHint(QFont::TypeWriter);
    } else if ((format & wSymbol) && !symbol.isEmpty()) {
        font.setFamily(symbol);
    }
    return font;
}

QSize extentForBytes(const QFont& font, const char* bytes, int length)
{
    if (length <= 0) {
        return {};
    }
    const QString text = IntlTextToQString(bytes, length);
    const QFontMetricsF metrics(font);
    return QSize(static_cast<int>(metrics.horizontalAdvance(text)),
                 static_cast<int>(metrics.height()));
}

// EM_FINDTEXTEX flags used by the original SzReplaceFormattedString caller.
constexpr UINT g_uMatchWholeWord = 0x00000002U;
constexpr UINT g_uMatchCase = 0x00000004U;

QVector<WORD> expandedFormatting(const QByteArray& text,
                                 const CDWordArray* formatting)
{
    QVector<WORD> result(text.size(), 0);
    WORD current = 0;
    int section = 0;
    for (int offset = 0; offset < text.size(); ++offset) {
        while (formatting && section < formatting->GetSize()
               && HIWORD(formatting->GetAt(section)) <= offset) {
            current = LOWORD(formatting->GetAt(section));
            ++section;
        }
        result[offset] = current;
    }
    return result;
}

CDWordArray compressedFormatting(const QVector<WORD>& formatting)
{
    CDWordArray result;
    WORD previous = 0;
    for (int offset = 0; offset < formatting.size(); ++offset) {
        const WORD current = formatting.at(offset);
        if (current != previous) {
            result.Add(MAKELONG(current, static_cast<WORD>(offset)));
            previous = current;
        }
    }
    return result;
}

bool asciiDelimiter(unsigned char value)
{
    // GetStringTypeEx(CT_CTYPE1) in the original accepts SPACE, BLANK,
    // CONTROL and PUNCTUATION as whole-word delimiters. Keep that exact
    // classification for the source's single-byte ASCII domain. ACP/DBCS
    // locale classification remains a platform-boundary item.
    return value < 0x80U
        && (std::isspace(value) || std::iscntrl(value) || std::ispunct(value));
}

bool bytesEqual(unsigned char left, unsigned char right, bool matchCase)
{
    if (matchCase) return left == right;
    return std::tolower(left) == std::tolower(right);
}

int findFormattedReplacement(const QByteArray& input, const QByteArray& needle,
                             int from, UINT flags)
{
    if (needle.isEmpty() || input.size() < needle.size()) return -1;
    const bool matchCase = flags & g_uMatchCase;
    const bool wholeWord = flags & g_uMatchWholeWord;
    const int last = input.size() - needle.size();
    for (int offset = std::max(0, from); offset <= last; ++offset) {
        if (wholeWord
            && ((offset > 0
                 && !asciiDelimiter(static_cast<unsigned char>(input.at(offset - 1))))
                || (offset + needle.size() < input.size()
                    && !asciiDelimiter(static_cast<unsigned char>(
                        input.at(offset + needle.size())))))) {
            continue;
        }
        bool matches = true;
        for (int index = 0; index < needle.size(); ++index) {
            if (!bytesEqual(static_cast<unsigned char>(input.at(offset + index)),
                            static_cast<unsigned char>(needle.at(index)), matchCase)) {
                matches = false;
                break;
            }
        }
        if (matches) return offset;
    }
    return -1;
}
}

const char* SzSkipOneFormat(const char* input, WORD* outputFormat)
{
    const char* read = input;
    WORD format = outputFormat ? *outputFormat : 0;
    switch (*read) {
    case chCtlColor:
        if (!decimal(*(read + 1))) {
            if (*(read + 1) == ',') {
                ++read;
                if (decimal(*(read + 1))) {
                    if (outputFormat) {
                        format &= ~wForeground;
                        format |= wBackground;
                        format &= 0xff00;
                    }
                    ++read;
                    if (decimal(*(read + 1))) {
                        if (outputFormat) {
                            format |= (((*read - '0') * 10 + (*(read + 1) - '0')) % 16);
                        }
                        ++read;
                    } else if (outputFormat) {
                        format |= (*read - '0');
                    }
                } else {
                    if (outputFormat) {
                        format &= ~wForeground;
                        format &= ~wBackground;
                        format &= 0xff00;
                    }
                    --read;
                }
            } else if (outputFormat) {
                format &= ~wForeground;
                format &= ~wBackground;
                format &= 0xff00;
            }
            break;
        }

        ++read;
        if (outputFormat) {
            format |= wForeground;
            format &= 0xff0f;
        }
        if (decimal(*(read + 1))) {
            if (outputFormat) {
                format |= (((*read - '0') * 10 + (*(read + 1) - '0')) % 16) << 4;
            }
            ++read;
            if (*(read + 1) == ',') {
                ++read;
                if (decimal(*(read + 1))) {
                    if (outputFormat) {
                        format |= wBackground;
                        format &= 0xfff0;
                    }
                    ++read;
                    if (decimal(*(read + 1))) {
                        if (outputFormat) {
                            format |= ((*read - '0') * 10 + (*(read + 1) - '0')) % 16;
                        }
                        ++read;
                    } else if (outputFormat) {
                        format |= (*read - '0');
                    }
                } else {
                    --read;
                }
            }
        } else if (*(read + 1) == ',') {
            if (outputFormat) {
                format |= (*read - '0') << 4;
            }
            ++read;
            if (decimal(*(read + 1))) {
                if (outputFormat) {
                    format |= wBackground;
                    format &= 0xfff0;
                }
                ++read;
                if (decimal(*(read + 1))) {
                    if (outputFormat) {
                        format |= ((*read - '0') * 10 + (*(read + 1) - '0')) % 16;
                    }
                    ++read;
                } else if (outputFormat) {
                    format |= (*read - '0');
                }
            } else {
                --read;
            }
        } else if (outputFormat) {
            format |= (*read - '0') << 4;
        }
        break;

    case chCtlBold:
        if (outputFormat) format ^= wBold;
        break;
    case chCtlItalic:
        if (outputFormat) format ^= wItalic;
        break;
    case chCtlFixedPitchFont:
        if (outputFormat) format ^= wFixedPitch;
        break;
    case chCtlUnderline:
        if (outputFormat) format ^= wUnderline;
        break;
    case chCtlSymbol:
        if (outputFormat) format ^= wSymbol;
        break;
    default:
        break;
    }
    if (outputFormat) {
        *outputFormat = format;
    }
    return read + 1;
}

short nResettingSequence(const char* input, char* resetSequence)
{
    short result = 0;
    const char* read = input;
    WORD format = 0;
    while (*read != formatChEOS) {
        switch (*read) {
        case chCtlColor:
        case chCtlBold:
        case chCtlItalic:
        case chCtlFixedPitchFont:
        case chCtlUnderline:
        case chCtlSymbol:
            read = SzSkipOneFormat(read, &format);
            break;
        default:
            ++read;
            break;
        }
    }
    if (format & wBold) resetSequence[result++] = chCtlBold;
    if (format & wItalic) resetSequence[result++] = chCtlItalic;
    if (format & wUnderline) resetSequence[result++] = chCtlUnderline;
    if (format & wFixedPitch) resetSequence[result++] = chCtlFixedPitchFont;
    if (format & wSymbol) resetSequence[result++] = chCtlSymbol;
    if ((format & wForeground) || (format & wBackground)) {
        resetSequence[result++] = chCtlColor;
    }
    resetSequence[result] = formatChEOS;
    return result;
}

char* SzControlLess(char* input, CDWordArray* formatting)
{
    if (formatting) formatting->RemoveAll();
    char* output = nullptr;
    char* read = input;
    char* write = nullptr;
    WORD format = 0;
    BOOL newFormatInPlace = FALSE;
    while (*read != formatChEOS) {
        switch (*read) {
        case chCtlColor:
        case chCtlBold:
        case chCtlItalic:
        case chCtlFixedPitchFont:
        case chCtlUnderline:
        case chCtlSymbol:
            newFormatInPlace = TRUE;
            read = const_cast<char*>(SzSkipOneFormat(read, &format));
            break;
        default:
            if (!output) {
                write = output = input;
            } else {
                ++write;
            }
            *write = *read;
            if (newFormatInPlace && formatting) {
                formatting->Add(MAKELONG(format, static_cast<WORD>(write - output)));
                newFormatInPlace = FALSE;
            }
            ++read;
            break;
        }
    }
    if (!output) {
        output = input;
    } else {
        *(++write) = formatChEOS;
    }
    return output;
}

short nFillFormatting(char* output, WORD current, WORD next, char firstFormattedChar)
{
    short count = 0;
    *output = formatChEOS;
    if ((next & wBold) != (current & wBold)) output[count++] = chCtlBold;
    if ((next & wItalic) != (current & wItalic)) output[count++] = chCtlItalic;
    if ((next & wUnderline) != (current & wUnderline)) output[count++] = chCtlUnderline;
    if ((next & wFixedPitch) != (current & wFixedPitch)) output[count++] = chCtlFixedPitchFont;
    if ((next & wSymbol) != (current & wSymbol)) output[count++] = chCtlSymbol;
    // Preserve the original foreground comparison, including its low-nibble typo.
    if (((next & wForeground) != (current & wForeground))
        || ((next & 0x00f0) != (current & 0x000f))
        || ((next & 0x000f) != (current & 0x000f))) {
        output[count++] = chCtlColor;
        if (next & wForeground) {
            const BYTE foreground = static_cast<BYTE>(((next >> 4) & 0x000f) + 16);
            output[count++] = static_cast<char>('0' + foreground / 10);
            output[count++] = static_cast<char>('0' + foreground % 10);
            if (((next & wBackground) != (current & wBackground))
                || ((next & 0x000f) != (current & 0x000f))) {
                if (next & wBackground) {
                    const BYTE background = static_cast<BYTE>((next & 0x000f) + 16);
                    output[count++] = formatChComma;
                    output[count++] = static_cast<char>('0' + background / 10);
                    output[count++] = static_cast<char>('0' + background % 10);
                }
            }
        } else if (decimal(firstFormattedChar)) {
            output[count++] = '0';
            output[count++] = '1';
        }
    }
    output[count] = formatChEOS;
    return count;
}

char* SzControlFull(const char* input, CDWordArray* formatting)
{
    const int sections = formatting ? formatting->GetSize() : 0;
    auto* result = new char[std::strlen(input) + MAX_FORMATTINGPERBYTE * sections + 1];
    int resultLength = 0;
    WORD currentFormat = 0;
    WORD currentOffset = 0;
    for (int index = 0; index < sections; ++index) {
        const DWORD element = formatting->GetAt(index);
        const WORD nextFormat = LOWORD(element);
        const WORD nextOffset = HIWORD(element);
        std::memcpy(result + resultLength, input + currentOffset, nextOffset - currentOffset);
        resultLength += nextOffset - currentOffset;
        resultLength += nFillFormatting(result + resultLength, currentFormat, nextFormat,
                                        *(input + nextOffset));
        if ((nextFormat & wLink) != (currentFormat & wLink)) {
            result[resultLength++] = chCtlLink;
        }
        currentFormat = nextFormat;
        currentOffset = nextOffset;
    }
    std::strcpy(result + resultLength, input + currentOffset);
    return result;
}

char* SzReplaceFormattedString(const char* controlLessReplaceWhat,
                               const char* controlLessReplaceBy,
                               const char* controlLessReplaceIn,
                               CDWordArray* replaceByFormatting,
                               CDWordArray* replaceInFormatting,
                               UINT flags)
{
    if (!controlLessReplaceWhat || !controlLessReplaceBy || !controlLessReplaceIn) {
        return nullptr;
    }

    const QByteArray needle(controlLessReplaceWhat);
    const QByteArray replacement(controlLessReplaceBy);
    QByteArray replaced(controlLessReplaceIn);
    QVector<WORD> replacedFormatting = expandedFormatting(replaced, replaceInFormatting);
    const QVector<WORD> replacementFormatting =
        expandedFormatting(replacement, replaceByFormatting);

    int from = 0;
    while (true) {
        const int position = findFormattedReplacement(replaced, needle, from, flags);
        if (position < 0) break;

        replaced.replace(position, needle.size(), replacement);
        replacedFormatting.remove(position, needle.size());
        for (int index = 0; index < replacementFormatting.size(); ++index) {
            replacedFormatting.insert(position + index, replacementFormatting.at(index));
        }

        // The RichEdit implementation resumes immediately after the inserted
        // clipboard text. This intentionally does not recursively replace text
        // contained in the replacement itself.
        from = position + replacement.size();
    }

    CDWordArray formatting = compressedFormatting(replacedFormatting);
    return SzControlFull(replaced.constData(), &formatting);
}

QSize GetFormattedTextExtent(QPainter* painter, const char* input, DWORD byteLength,
                             CDWordArray* formatting)
{
    if (!painter || !input) return {};
    const int maximumLength = static_cast<int>(byteLength ? byteLength : std::strlen(input));
    if (!bSizorPresent(formatting)) {
        return extentForBytes(painter->font(), input, maximumLength);
    }

    QSize size;
    const QFont original = painter->font();
    QFont current = original;
    int currentOffset = 0;
    BOOL transparency = FALSE;
    for (int index = 0; formatting && index < formatting->GetSize(); ++index) {
        const DWORD element = formatting->GetAt(index);
        const WORD format = LOWORD(element);
        const int formatOffset = HIWORD(element);
        const int nextOffset = std::min(formatOffset, maximumLength);
        QByteArray transparent;
        const char* bytes = input + currentOffset;
        if (transparency) {
            transparent = QByteArray(nextOffset - currentOffset, formatChTransparent);
            bytes = transparent.constData();
        }
        const QSize chunk = extentForBytes(current, bytes, nextOffset - currentOffset);
        size.rwidth() += chunk.width();
        size.setHeight(std::max(size.height(), chunk.height()));
        currentOffset = formatOffset;
        if (formatOffset >= maximumLength) break;
        current = fontForFormat(original, format);
        transparency = (format & wForeground) && (format & wBackground)
            && (((format >> 4) & 0x000f) == (format & 0x000f));
    }
    if (maximumLength > currentOffset) {
        QByteArray transparent;
        const char* bytes = input + currentOffset;
        if (transparency) {
            transparent = QByteArray(maximumLength - currentOffset, formatChTransparent);
            bytes = transparent.constData();
        }
        const QSize chunk = extentForBytes(current, bytes, maximumLength - currentOffset);
        size.rwidth() += chunk.width();
        size.setHeight(std::max(size.height(), chunk.height()));
    }
    return size;
}

BYTE GetColorCode(COLORREF color)
{
    if (color == RGB(255, 255, 255)) return 0;
    if (color == RGB(0, 0, 128)) return 2;
    if (color == RGB(0, 128, 0)) return 3;
    if (color == RGB(255, 0, 0)) return 4;
    if (color == RGB(128, 0, 0)) return 5;
    if (color == RGB(128, 0, 128)) return 6;
    if (color == RGB(128, 128, 0)) return 7;
    if (color == RGB(255, 255, 0)) return 8;
    if (color == RGB(0, 255, 0)) return 9;
    if (color == RGB(0, 128, 128)) return 10;
    if (color == RGB(0, 255, 255)) return 11;
    if (color == RGB(0, 0, 255)) return 12;
    if (color == RGB(255, 0, 255)) return 13;
    if (color == RGB(128, 128, 128)) return 14;
    if (color == RGB(192, 192, 192)) return 15;
    return 1;
}

COLORREF GetRBGColor(BYTE code)
{
    switch (code) {
    case 0: return RGB(255, 255, 255);
    case 2: return RGB(0, 0, 128);
    case 3: return RGB(0, 128, 0);
    case 4: return RGB(255, 0, 0);
    case 5: return RGB(128, 0, 0);
    case 6: return RGB(128, 0, 128);
    case 7: return RGB(128, 128, 0);
    case 8: return RGB(255, 255, 0);
    case 9: return RGB(0, 255, 0);
    case 10: return RGB(0, 128, 128);
    case 11: return RGB(0, 255, 255);
    case 12: return RGB(0, 0, 255);
    case 13: return RGB(255, 0, 255);
    case 14: return RGB(128, 128, 128);
    case 15: return RGB(192, 192, 192);
    default: return RGB(0, 0, 0);
    }
}

void FreeAndNullFormatting(CDWordArray** formatting)
{
    if (!formatting || !*formatting) return;
    (*formatting)->RemoveAll();
    delete *formatting;
    *formatting = nullptr;
}

CDWordArray* CopyFormatting(CDWordArray* formatting)
{
    if (!formatting) return nullptr;
    auto* copy = new CDWordArray;
    for (int index = 0; index <= formatting->GetUpperBound(); ++index) {
        copy->Add(formatting->GetAt(index));
    }
    return copy;
}

CDWordArray* CopyLinksFormatting(CDWordArray* formatting)
{
    if (!formatting) return nullptr;
    CDWordArray* copy = nullptr;
    BOOL inLink = FALSE;
    for (int index = 0; index <= formatting->GetUpperBound(); ++index) {
        const DWORD element = formatting->GetAt(index);
        if ((LOWORD(element) & wLink) && !inLink) {
            inLink = TRUE;
            copy = AddFormat(copy, MAKELONG(wLink, HIWORD(element)));
        } else if (!(LOWORD(element) & wLink) && inLink) {
            inLink = FALSE;
            copy = AddFormat(copy, MAKELONG(0, HIWORD(element)));
        }
    }
    return copy;
}

BOOL bFormattingsEqual(CDWordArray* first, CDWordArray* second)
{
    const int firstCount = first ? first->GetSize() : 0;
    const int secondCount = second ? second->GetSize() : 0;
    if (firstCount != secondCount) return FALSE;
    for (int index = 0; index < firstCount; ++index) {
        if (first->GetAt(index) != second->GetAt(index)) return FALSE;
    }
    return TRUE;
}

CDWordArray* AddFormat(CDWordArray* formatting, DWORD element)
{
    if (!formatting) formatting = new CDWordArray;
    formatting->Add(element);
    return formatting;
}

CDWordArray* InsertFormat(CDWordArray* formatting, BOOL addFormat, WORD format,
                         WORD offset)
{
    if (!formatting || !formatting->GetSize()) {
        if (!formatting) formatting = new CDWordArray;
        formatting->Add(MAKELONG(format, offset));
        return formatting;
    }
    int index = 0;
    WORD temporaryOffset = 0;
    for (; index <= formatting->GetUpperBound(); ++index) {
        temporaryOffset = HIWORD(formatting->GetAt(index));
        if (temporaryOffset >= offset) break;
    }
    WORD temporaryFormat;
    if (index <= formatting->GetUpperBound() && temporaryOffset == offset) {
        temporaryFormat = LOWORD(formatting->GetAt(index));
    } else if (index == 0) {
        temporaryFormat = 0;
    } else {
        temporaryFormat = LOWORD(formatting->GetAt(index - 1));
    }
    temporaryFormat = addFormat ? static_cast<WORD>(temporaryFormat | format)
                                : static_cast<WORD>(temporaryFormat & ~format);
    if (index <= formatting->GetUpperBound() && temporaryOffset == offset) {
        formatting->SetAt(index, MAKELONG(temporaryFormat, offset));
    } else {
        formatting->InsertAt(index, MAKELONG(temporaryFormat, offset));
    }
    if (!addFormat) {
        for (int previous = index - 1; previous > 0; --previous) {
            temporaryFormat = LOWORD(formatting->GetAt(previous));
            temporaryOffset = HIWORD(formatting->GetAt(previous));
            if (temporaryFormat & format) break;
            temporaryFormat |= format;
            formatting->SetAt(previous, MAKELONG(temporaryFormat, temporaryOffset));
        }
    }
    return formatting;
}

CDWordArray* CutFormattingArray(CDWordArray* formatting, SHORT newStringLength)
{
    if (!formatting) return nullptr;
    for (int index = 0; index <= formatting->GetUpperBound();) {
        if (HIWORD(formatting->GetAt(index)) >= newStringLength) {
            formatting->RemoveAt(index);
        } else {
            ++index;
        }
    }
    if (!formatting->GetSize()) FreeAndNullFormatting(&formatting);
    return formatting;
}

CDWordArray* PullFormattingOffsets(CDWordArray* formatting, SHORT deltaOffset)
{
    if (!formatting) return nullptr;
    if (!deltaOffset) return CopyFormatting(formatting);
    WORD latestFormat = 0;
    CDWordArray* result = nullptr;
    for (int index = 0; index <= formatting->GetUpperBound(); ++index) {
        const DWORD element = formatting->GetAt(index);
        if (HIWORD(element) >= deltaOffset) {
            if (!result) {
                result = new CDWordArray;
                if (latestFormat) result->Add(MAKELONG(latestFormat, 0));
            }
            result->Add(MAKELONG(LOWORD(element), HIWORD(element) - deltaOffset));
        } else {
            latestFormat = LOWORD(element);
        }
    }
    if (!result && latestFormat) {
        result = new CDWordArray;
        result->Add(MAKELONG(latestFormat, 0));
    }
    return result;
}

void PushFormattingOffsets(CDWordArray* formatting, SHORT deltaOffset)
{
    if (!formatting || !deltaOffset) return;
    for (int index = 0; index <= formatting->GetUpperBound(); ++index) {
        const DWORD element = formatting->GetAt(index);
        formatting->SetAt(index, MAKELONG(LOWORD(element), HIWORD(element) + deltaOffset));
    }
}

void PushFormattingOffsetsDW(DWORD* formatting, INT count, SHORT deltaOffset)
{
    if (!formatting || !count || !deltaOffset) return;
    for (int index = 0; index < count; ++index) {
        formatting[index] = MAKELONG(LOWORD(formatting[index]),
                                     HIWORD(formatting[index]) + deltaOffset);
    }
}

CDWordArray* IdentifyURLs(CDWordArray* formatting, const char* message)
{
    if (!message) return formatting;
    int urlBounds[MAX_URL_INTEXT * 2]{};
    int urlNumber = MAX_URL_INTEXT;
    g_urlRec.HrIdentifyUrls(message, urlBounds, &urlNumber);
    if (!urlNumber) return formatting;
    if (!formatting) {
        for (int index = 0; index < urlNumber; ++index) {
            formatting = AddFormat(formatting,
                MAKELONG(wLink, static_cast<WORD>(urlBounds[index * 2])));
            formatting = AddFormat(formatting,
                MAKELONG(0, static_cast<WORD>(urlBounds[index * 2 + 1])));
        }
    } else {
        for (int index = 0; index < urlNumber; ++index) {
            formatting = InsertFormat(formatting, TRUE, wLink,
                static_cast<WORD>(urlBounds[index * 2]));
            formatting = InsertFormat(formatting, FALSE, wLink,
                static_cast<WORD>(urlBounds[index * 2 + 1]));
        }
    }
    return formatting;
}

CDWordArray* MarkHotLinks(CDWordArray* formatting, char* message, char identifier)
{
    BOOL hotlinkOn = FALSE;
    char* destination = nullptr;
    char* source = message;
    while (*source) {
        if (*source == identifier) {
            if (!destination) destination = source;
            hotlinkOn = !hotlinkOn;
            if (formatting) {
                formatting = InsertFormat(formatting, hotlinkOn, wLink,
                                          static_cast<WORD>(destination - message));
            } else {
                formatting = AddFormat(formatting,
                    MAKELONG(hotlinkOn ? wLink : 0,
                             static_cast<WORD>(destination - message)));
            }
            ++source;
        } else if (destination) {
            if (iBytesofChar(static_cast<BYTE>(*source)) == 2
                && source[1] != '\0') {
                *(destination++) = *(source++);
            }
            *(destination++) = *(source++);
        } else {
            source += iBytesofChar(static_cast<BYTE>(*source)) == 2
                    && source[1] != '\0' ? 2 : 1;
        }
    }
    if (destination) {
        if (hotlinkOn) {
            formatting = InsertFormat(formatting, FALSE, wLink,
                                      static_cast<WORD>(destination - message));
        }
        *destination = '\0';
    }
    return formatting;
}

BOOL bSizorPresent(CDWordArray* formatting)
{
    if (!formatting) return FALSE;
    for (int index = 0; index <= formatting->GetUpperBound(); ++index) {
        const WORD format = LOWORD(formatting->GetAt(index));
        if ((format & wBold) || (format & wItalic) || (format & wUnderline)
            || (format & wFixedPitch) || (format & wSymbol)
            || (format & wBackground)) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL bURLPresent(CDWordArray* formatting)
{
    if (!formatting) return FALSE;
    for (int index = 0; index <= formatting->GetUpperBound(); ++index) {
        if (LOWORD(formatting->GetAt(index)) & wLink) return TRUE;
    }
    return FALSE;
}

BOOL FLaunchBrowser(const char* url)
{
    return g_urlRec.bLaunchUrl(url, theApp.m_bEmbedded);
}

short FFixedPitchFont(const QString& faceName)
{
    for (short index = 0; index < FIXEDPITCHNUMBER; ++index) {
        if (faceName.compare(QString::fromLatin1(FIXEDPITCHFACENAMES[index]),
                             Qt::CaseInsensitive) == 0)
            return index;
    }
    return -1;
}

short FSymbolFont(const QString& faceName)
{
    for (short index = 0; index < SYMBOLNUMBER; ++index) {
        if (faceName.compare(QString::fromLatin1(SYMBOLFACENAMES[index]),
                             Qt::CaseInsensitive) == 0)
            return index;
    }
    return -1;
}

short nGetSpecialFontIndex(BOOL fixedPitchFont)
{
    const char* const* names = fixedPitchFont
        ? FIXEDPITCHFACENAMES : SYMBOLFACENAMES;
    const short count = fixedPitchFont ? FIXEDPITCHNUMBER : SYMBOLNUMBER;
    const QStringList families = QFontDatabase::families();
    for (short index = 0; index < count; ++index) {
        const QString wanted = QString::fromLatin1(names[index]);
        for (const QString& family : families) {
            if (family.compare(wanted, Qt::CaseInsensitive) == 0)
                return index;
        }
    }
    return -1;
}

BOOL bLOGFONTToCHARFORMAT(LOGFONT* logFont, COLORREF color, DWORD mask,
                          CHARFORMAT* charFormat)
{
    if (!logFont || !charFormat) return FALSE;

    std::memset(charFormat, 0, sizeof(CHARFORMAT));
    charFormat->cbSize = sizeof(CHARFORMAT);
    charFormat->dwMask = mask ? mask
        : (CFM_FACE | CFM_SIZE | CFM_OFFSET | CFM_COLOR | CFM_ITALIC
           | CFM_STRIKEOUT | CFM_UNDERLINE | CFM_CHARSET);

    if (logFont->lfWeight && ((mask & CFM_BOLD) || !mask))
        charFormat->dwMask |= CFM_BOLD;
    if (logFont->lfWeight >= 700 && (charFormat->dwMask & CFM_BOLD))
        charFormat->dwEffects |= CFE_BOLD;
    if (logFont->lfItalic && (charFormat->dwMask & CFM_ITALIC))
        charFormat->dwEffects |= CFE_ITALIC;
    if (logFont->lfUnderline && (charFormat->dwMask & CFM_UNDERLINE))
        charFormat->dwEffects |= CFE_UNDERLINE;
    if (logFont->lfStrikeOut && (charFormat->dwMask & CFM_STRIKEOUT))
        charFormat->dwEffects |= CFE_STRIKEOUT;

    if (charFormat->dwMask & CFM_SIZE) {
        QScreen* screen = QGuiApplication::primaryScreen();
        const int dpi = screen
            ? qMax(1, qRound(screen->logicalDotsPerInchY())) : 96;
        charFormat->yHeight = static_cast<LONG>(
            (qAbs(logFont->lfHeight) * 1440) / dpi);
    }
    if (charFormat->dwMask & CFM_COLOR)
        charFormat->crTextColor = color;

    charFormat->bCharSet = logFont->lfCharSet;
    charFormat->bPitchAndFamily = logFont->lfPitchAndFamily;
    if (charFormat->dwMask & CFM_FACE) {
        std::strncpy(charFormat->szFaceName, logFont->lfFaceName,
                     LF_FACESIZE - 1);
        charFormat->szFaceName[LF_FACESIZE - 1] = '\0';
    }
    return TRUE;
}

CDWordArray* PRGDWGetFormatting(const QTextEdit* richEdit,
                                const QFont* defaultFont,
                                COLORREF defaultColor)
{
    if (!richEdit || !defaultFont) return nullptr;

    BOOL bold = defaultFont->weight() >= QFont::Bold;
    BOOL italic = defaultFont->italic();
    BOOL underline = defaultFont->underline();
    BOOL fixedPitch = FFixedPitchFont(defaultFont->family()) > -1;
    BOOL symbol = FSymbolFont(defaultFont->family()) > -1;
    COLORREF color = defaultColor;
    WORD format = 0;
    if (bold) format |= wBold;
    if (italic) format |= wItalic;
    if (underline) format |= wUnderline;
    if (fixedPitch) format |= wFixedPitch;
    if (symbol) format |= wSymbol;

    CDWordArray* formatting = nullptr;
    const QString text = richEdit->toPlainText();
    for (int index = 0; index < text.size(); ++index) {
        QTextCursor cursor(richEdit->document());
        cursor.setPosition(index);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        const QTextCharFormat charFormat = cursor.charFormat();
        const QFont font = charFormat.font();
        BOOL changed = FALSE;

        const BOOL nextBold = font.weight() >= QFont::Bold;
        if (nextBold != bold) {
            changed = TRUE;
            bold = nextBold;
            if (bold) format |= wBold; else format &= ~wBold;
        }
        const BOOL nextItalic = font.italic();
        if (nextItalic != italic) {
            changed = TRUE;
            italic = nextItalic;
            if (italic) format |= wItalic; else format &= ~wItalic;
        }
        const BOOL nextUnderline = font.underline();
        if (nextUnderline != underline) {
            changed = TRUE;
            underline = nextUnderline;
            if (underline) format |= wUnderline; else format &= ~wUnderline;
        }
        const BOOL nextFixedPitch = FFixedPitchFont(font.family()) > -1;
        if (nextFixedPitch != fixedPitch) {
            changed = TRUE;
            fixedPitch = nextFixedPitch;
            if (fixedPitch) format |= wFixedPitch; else format &= ~wFixedPitch;
        }
        const BOOL nextSymbol = FSymbolFont(font.family()) > -1;
        if (nextSymbol != symbol) {
            changed = TRUE;
            symbol = nextSymbol;
            if (symbol) format |= wSymbol; else format &= ~wSymbol;
        }

        const QColor qcolor = charFormat.foreground().style() == Qt::NoBrush
            ? QColor(GetRValue(defaultColor), GetGValue(defaultColor), GetBValue(defaultColor))
            : charFormat.foreground().color();
        const COLORREF nextColor = RGB(qcolor.red(), qcolor.green(), qcolor.blue());
        if (nextColor != color) {
            changed = TRUE;
            color = nextColor;
            format &= 0xff0f;
            if (color != defaultColor) {
                format |= wForeground;
                format |= static_cast<WORD>(GetColorCode(color) << 4);
            } else {
                format &= ~wForeground;
            }
        }

        if (changed) {
            if (!formatting) formatting = new CDWordArray;
            formatting->Add(MAKELONG(format, static_cast<WORD>(index)));
        }
    }
    return formatting;
}
