// Ported from the build-selected v2.5-beta-1-modern ccommon.cpp and
// artifacts/core/ccommon.cpp. QByteArray/QString replace only the original
// caller-owned TCHAR/WCHAR buffers.

#include "ccommon.h"

#include "fechrcnv.h"

#include <QtGlobal>

#include <cerrno>
#include <iconv.h>

namespace {
qsizetype nulTerminatedSize(const QByteArray& value)
{
    const qsizetype nul = value.indexOf('\0');
    return nul < 0 ? value.size() : nul;
}

qsizetype boundedSize(const QByteArray& value, INT requested)
{
    const qsizetype terminated = nulTerminatedSize(value);
    return requested > 0 ? qMin<qsizetype>(requested, terminated)
                         : terminated;
}

qsizetype boundedSize(QStringView value, INT requested)
{
    qsizetype terminated = 0;
    while (terminated < value.size() && !value.at(terminated).isNull()) {
        ++terminated;
    }
    return requested > 0 ? qMin<qsizetype>(requested, terminated)
                         : terminated;
}

bool firstNicknameCharacter(unsigned int character)
{
    return (character >= 'a' && character <= 'z')
        || (character >= 'A' && character <= 'Z')
        || character == '[' || character == ']' || character == '{'
        || character == '}' || character == '_' || character == '|'
        || character == '`' || character == '^';
}

bool laterNicknameCharacter(unsigned int character)
{
    return firstNicknameCharacter(character)
        || (character >= '0' && character <= '9') || character == '-';
}

bool convertCodePage(const QByteArray& input, const char* from,
                     const char* to, QByteArray* output)
{
    if (!output) return false;
    iconv_t converter = iconv_open(to, from);
    if (converter == reinterpret_cast<iconv_t>(-1)) return false;

    QByteArray converted(qMax<qsizetype>(32, input.size() * 4 + 16), '\0');
    char* inputPosition = const_cast<char*>(input.constData());
    size_t inputRemaining = static_cast<size_t>(input.size());
    size_t outputUsed = 0;
    bool success = true;

    while (inputRemaining > 0) {
        char* outputPosition = converted.data() + outputUsed;
        size_t outputRemaining = static_cast<size_t>(converted.size()) - outputUsed;
        const size_t result = iconv(converter, &inputPosition, &inputRemaining,
                                    &outputPosition, &outputRemaining);
        outputUsed = static_cast<size_t>(outputPosition - converted.data());
        if (result != static_cast<size_t>(-1)) break;
        if (errno != E2BIG) {
            success = false;
            break;
        }
        converted.resize(converted.size() * 2);
    }

    iconv_close(converter);
    if (!success) return false;
    converted.resize(static_cast<qsizetype>(outputUsed));
    *output = converted;
    return true;
}

QString fromUtf16Le(const QByteArray& bytes)
{
    QString result;
    result.reserve(bytes.size() / 2);
    for (qsizetype index = 0; index + 1 < bytes.size(); index += 2) {
        const unsigned int low = static_cast<UCHAR>(bytes.at(index));
        const unsigned int high = static_cast<UCHAR>(bytes.at(index + 1));
        result.append(QChar(low | (high << 8U)));
    }
    return result;
}

QByteArray toUtf16Le(QStringView string)
{
    QByteArray result;
    result.reserve(string.size() * 2);
    for (QChar character : string) {
        const unsigned int value = character.unicode();
        result.append(static_cast<char>(value & 0xffU));
        result.append(static_cast<char>((value >> 8U) & 0xffU));
    }
    return result;
}

const char* codePageForCharacterSet(BYTE characterSet)
{
    switch (characterSet) {
    case SHIFTJIS_CHARSET: return "SHIFT-JIS";
    case HANGEUL_CHARSET: return "CP949";
    case GB2312_CHARSET: return "CP936";
    case CHINESEBIG5_CHARSET: return "CP950";
    case EASTEUROPE_CHARSET: return "WINDOWS-1250";
    case GREEK_CHARSET: return "WINDOWS-1253";
    case TURKISH_CHARSET: return "WINDOWS-1254";
    case HEBREW_CHARSET: return "WINDOWS-1255";
    case ARABIC_CHARSET: return "WINDOWS-1256";
    case BALTIC_CHARSET: return "WINDOWS-1257";
    case RUSSIAN_CHARSET: return "WINDOWS-1251";
    case THAI_CHARSET: return "WINDOWS-874";
    default: return nullptr;
    }
}
}

BOOL bExtendedString(const QByteArray& input)
{
    const qsizetype length = nulTerminatedSize(input);
    for (qsizetype index = 0; index < length; ++index) {
        if (static_cast<UCHAR>(input.at(index)) > 0x7fU) return TRUE;
    }
    return FALSE;
}

BOOL bExtendedWideString(QStringView input)
{
    for (QChar character : input) {
        if (character.isNull()) break;
        if (character.unicode() > 0x7fU) return TRUE;
    }
    return FALSE;
}

BOOL bExtendedChannelName(const QByteArray& channelName,
                          BOOL acceptUpperAnsi)
{
    const qsizetype length = nulTerminatedSize(channelName);
    for (qsizetype index = 0; index < length; ++index) {
        const UCHAR character = static_cast<UCHAR>(channelName.at(index));
        if ((character > 0x7fU && !acceptUpperAnsi)
            || character == static_cast<UCHAR>(g_chSpace)
            || character == static_cast<UCHAR>(g_chBell)
            || character == static_cast<UCHAR>(g_chCR)
            || character == static_cast<UCHAR>(g_chLF)
            || character == static_cast<UCHAR>(g_chComma)) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL bExtendedWideChannelName(QStringView channelName)
{
    for (QChar character : channelName) {
        if (character.isNull()) break;
        const unsigned int value = character.unicode();
        if (value > 0x7fU || value == static_cast<unsigned int>(g_chSpace)
            || value == static_cast<unsigned int>(g_chBell)
            || value == static_cast<unsigned int>(g_chCR)
            || value == static_cast<unsigned int>(g_chLF)
            || value == static_cast<unsigned int>(g_chComma)) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL bExtendedNickname(const QByteArray& nickname)
{
    const qsizetype length = nulTerminatedSize(nickname);
    if (length == 0) return FALSE;
    if (!firstNicknameCharacter(static_cast<UCHAR>(nickname.at(0)))) {
        return TRUE;
    }
    for (qsizetype index = 1; index < length; ++index) {
        if (!laterNicknameCharacter(static_cast<UCHAR>(nickname.at(index)))) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL bExtendedWideNickname(QStringView nickname)
{
    const qsizetype length = boundedSize(nickname, 0);
    if (length == 0) return FALSE;
    if (!firstNicknameCharacter(nickname.at(0).unicode())) return TRUE;
    for (qsizetype index = 1; index < length; ++index) {
        if (!laterNicknameCharacter(nickname.at(index).unicode())) return TRUE;
    }
    return FALSE;
}

BOOL bConvertWideStringToUTF8(QStringView input, INT inputLength,
                              QByteArray* output, INT* outputLength,
                              BOOL nickname, BOOL channelName,
                              BOOL postProcess, BOOL escapeWildcards)
{
    if (!output || (nickname && channelName)) return FALSE;
    output->clear();
    if (outputLength) *outputLength = 0;

    const qsizetype length = boundedSize(input, inputLength);
    if (length <= 0) return FALSE;
    output->reserve(static_cast<qsizetype>(3) * (length + 1));

    qsizetype index = 0;
    if (nickname) {
        output->append(g_chExtNckPfx);
    } else if (channelName) {
        output->append(g_chExtChnPfx);
        if (input.at(index) == QLatin1Char('%')) ++index;
        if (index < length
            && (input.at(index) == QLatin1Char('#')
                || input.at(index) == QLatin1Char('&'))) {
            output->append(input.at(index).toLatin1());
            ++index;
        } else {
            output->append(g_chGblChnPfx);
        }
    }

    for (; index < length; ++index) {
        const unsigned int character = input.at(index).unicode();
        if (postProcess && character == static_cast<unsigned int>(g_chLF)) {
            output->append("\\n", 2);
        } else if (postProcess
                   && character == static_cast<unsigned int>(g_chCR)) {
            output->append("\\r", 2);
        } else if (postProcess
                   && character == static_cast<unsigned int>(g_chTab)) {
            output->append("\\t", 2);
        } else if (postProcess
                   && character == static_cast<unsigned int>(g_chSpace)) {
            output->append("\\b", 2);
        } else if (postProcess
                   && character == static_cast<unsigned int>(g_chComma)) {
            output->append("\\c", 2);
        } else if (postProcess
                   && character == static_cast<unsigned int>(g_chBackSlash)) {
            output->append("\\\\", 2);
        } else if (escapeWildcards && character == '*') {
            output->append("\\*", 2);
        } else if (escapeWildcards && character == '?') {
            output->append("\\?", 2);
        } else if (character <= 0x7fU) {
            output->append(static_cast<char>(character));
        } else if (character <= 0x07ffU) {
            output->append(static_cast<char>(0xc0U | ((character >> 6U) & 0x1fU)));
            output->append(static_cast<char>(0x80U | (character & 0x3fU)));
        } else {
            output->append(static_cast<char>(0xe0U | ((character >> 12U) & 0x0fU)));
            output->append(static_cast<char>(0x80U | ((character >> 6U) & 0x3fU)));
            output->append(static_cast<char>(0x80U | (character & 0x3fU)));
        }
    }

    if (outputLength) *outputLength = static_cast<INT>(output->size());
    return TRUE;
}

BOOL bConvertUTF8StringToWide(const QByteArray& input, INT inputLength,
                              QString* output, INT* outputLength,
                              BOOL nickname, BOOL channelName,
                              BOOL postProcess)
{
    if (!output || (nickname && channelName)) return FALSE;
    output->clear();
    if (outputLength) *outputLength = 0;

    const qsizetype length = boundedSize(input, inputLength);
    if (length <= 0) return FALSE;
    output->reserve(length);

    qsizetype index = 0;
    INT convertedCount = 0;
    if (nickname) {
        if (input.at(0) == g_chExtNckPfx) ++index;
    } else if (channelName) {
        if (index < length && input.at(index) == g_chExtChnPfx) {
            ++index;
            output->append(QLatin1Char('%'));
            ++convertedCount;
        }
        if (index < length
            && (input.at(index) == g_chGblChnPfx
                || input.at(index) == g_chLclChnPfx)) {
            output->append(QLatin1Char(input.at(index)));
            ++index;
            ++convertedCount;
        }
    }

    while (index < length && input.at(index) != g_chEOS) {
        const UCHAR first = static_cast<UCHAR>(input.at(index));
        if (postProcess && first == static_cast<UCHAR>(g_chBackSlash)) {
            ++index;
            if (index < length) {
                switch (input.at(index)) {
                case '0': output->append(QChar(0)); break;
                case 'n': output->append(QLatin1Char('\n')); break;
                case 'r': output->append(QLatin1Char('\r')); break;
                case 't': output->append(QLatin1Char('\t')); break;
                case 'b': output->append(QLatin1Char(' ')); break;
                case 'c': output->append(QLatin1Char(',')); break;
                case '\\': output->append(QLatin1Char('\\')); break;
                default: break;
                }
            }
        } else if (first <= 0x7fU) {
            output->append(QChar(first));
        } else if ((first & 0xe0U) == 0xc0U) {
            if (length - index >= 2) {
                const UCHAR second = static_cast<UCHAR>(input.at(index + 1));
                output->append(QChar(((first & 0x1fU) << 6U)
                                     | (second & 0x3fU)));
                ++index;
            } else {
                output->append(QLatin1Char('?'));
            }
        } else {
            if (length - index >= 3) {
                const UCHAR second = static_cast<UCHAR>(input.at(index + 1));
                const UCHAR third = static_cast<UCHAR>(input.at(index + 2));
                output->append(QChar(((first & 0x0fU) << 12U)
                                     | ((second & 0x3fU) << 6U)
                                     | (third & 0x3fU)));
                index += 2;
            } else {
                output->append(QLatin1Char('?'));
            }
        }
        ++convertedCount;
        ++index;
    }

    if (outputLength) *outputLength = convertedCount;
    return TRUE;
}

const CHAR* SzNextUTF8Char(const CHAR* input)
{
    if (!input || !*input) return input;
    const UCHAR first = static_cast<UCHAR>(*input);
    if (first == static_cast<UCHAR>(g_chBackSlash)) {
        switch (*(input + 1)) {
        case '0': return input + 1;
        case 'n':
        case 'r':
        case 't':
        case 'b':
        case 'c':
        case '\\': return input + 2;
        default: return input + 1;
        }
    }
    if (first <= 0x7fU) return input + 1;
    if ((first & 0xe0U) == 0xc0U) return *(input + 1) ? input + 2 : input + 1;
    return *(input + 1) && *(input + 2) ? input + 3 : input + 1;
}

BOOL bSB2DBKatakana(const QByteArray& input, QByteArray* output,
                    BOOL* changed)
{
    if (!output) return FALSE;
    if (changed) *changed = FALSE;
    output->clear();
    output->reserve(input.size() * 2);

    bool convertedAny = false;
    qsizetype index = 0;
    while (index < input.size()) {
        const UCHAR character = static_cast<UCHAR>(input.at(index));
        if (character < 0xa1U || character > 0xdfU) {
            output->append(input.at(index));
            if (SJISISKANJI(character) && index + 1 < input.size()) {
                output->append(input.at(index + 1));
                index += 2;
            } else {
                ++index;
            }
            continue;
        }

        const qsizetype runStart = index;
        while (index < input.size()) {
            const UCHAR runCharacter = static_cast<UCHAR>(input.at(index));
            if (runCharacter < 0xa1U || runCharacter > 0xdfU) break;
            ++index;
        }
        const QByteArray run = input.mid(runStart, index - runStart);
        QByteArray utf16;
        if (!convertCodePage(run, "SHIFT-JIS", "UTF-16LE", &utf16)) {
            return FALSE;
        }
        const QString fullWidth = fromUtf16Le(utf16).normalized(
            QString::NormalizationForm_KC);
        QByteArray mapped;
        if (!convertCodePage(toUtf16Le(QStringView(fullWidth)), "UTF-16LE",
                             "SHIFT-JIS", &mapped)) {
            return FALSE;
        }
        output->append(mapped);
        convertedAny = true;
    }

    if (changed) *changed = convertedAny;
    return TRUE;
}

BOOL bConvertString(BOOL incomingText, BYTE characterSet,
                    const QByteArray& input, QByteArray* output,
                    BOOL* changed)
{
    if (!output) return FALSE;
    if (changed) *changed = FALSE;

    if (input.isEmpty() || characterSet == ANSI_CHARSET) {
        *output = input;
        return TRUE;
    }

    if (characterSet == SHIFTJIS_CHARSET) {
        QByteArray terminated = input;
        terminated.append('\0');
        UCHAR* converted = nullptr;
        int convertedLength = 0;
        if (incomingText) {
            convertedLength = OurJIS_to_ShiftJIS(
                reinterpret_cast<UCHAR*>(terminated.data()), -1,
                &converted, 0);
        } else {
            QByteArray fullWidth;
            BOOL fullWidthChanged = FALSE;
            if (!bSB2DBKatakana(input, &fullWidth, &fullWidthChanged)) {
                return FALSE;
            }
            fullWidth.append('\0');
            convertedLength = OurShiftJIS_to_JIS(
                reinterpret_cast<UCHAR*>(fullWidth.data()), -1,
                &converted, 0);
        }
        if (convertedLength <= 0 || !converted) {
            delete[] converted;
            *output = input;
            return TRUE;
        }
        *output = QByteArray(reinterpret_cast<char*>(converted),
                             qMax(0, convertedLength - 1));
        delete[] converted;
        if (changed) *changed = TRUE;
        return TRUE;
    }

    if (characterSet == EASTEUROPE_CHARSET) {
        const char* from = incomingText ? "ISO-8859-2" : "WINDOWS-1250";
        const char* to = incomingText ? "WINDOWS-1250" : "ISO-8859-2";
        if (!convertCodePage(input, from, to, output)) {
            *output = input;
            return TRUE;
        }
        if (changed) *changed = TRUE;
        return TRUE;
    }

    *output = input;
    return TRUE;
}

BOOL bWideToCharacterSet(QStringView input, BYTE characterSet,
                         QByteArray* output)
{
    if (!output) return FALSE;
    if (const char* codePage = codePageForCharacterSet(characterSet)) {
        return convertCodePage(toUtf16Le(input), "UTF-16LE", codePage,
                               output) ? TRUE : FALSE;
    }
    *output = input.toString().toLocal8Bit();
    return TRUE;
}

BOOL bCharacterSetToWide(const QByteArray& input, BYTE characterSet,
                         QString* output)
{
    if (!output) return FALSE;
    if (const char* codePage = codePageForCharacterSet(characterSet)) {
        QByteArray utf16;
        if (!convertCodePage(input, codePage, "UTF-16LE", &utf16)) {
            return FALSE;
        }
        *output = fromUtf16Le(utf16);
        return TRUE;
    }
    *output = QString::fromLocal8Bit(input);
    return TRUE;
}

BOOL bDataToString(const QByteArray& source, QByteArray* destination,
                   BOOL afterColon)
{
    if (!destination) return FALSE;
    destination->clear();
    destination->reserve(source.size() * 2);
    for (char character : source) {
        switch (character) {
        case g_chEOS: destination->append("\\0", 2); break;
        case g_chBackSlash: destination->append("\\\\", 2); break;
        case g_chLF: destination->append("\\n", 2); break;
        case g_chCR: destination->append("\\r", 2); break;
        case g_chTab: destination->append("\\t", 2); break;
        case g_chSpace:
            if (afterColon) destination->append(g_chSpace);
            else destination->append("\\b", 2);
            break;
        case g_chComma:
            if (afterColon) destination->append(g_chComma);
            else destination->append("\\c", 2);
            break;
        default: destination->append(character); break;
        }
    }
    return TRUE;
}

BOOL bStringToData(const QByteArray& source, QByteArray* destination)
{
    if (!destination) return FALSE;
    destination->clear();
    const qsizetype length = nulTerminatedSize(source);
    destination->reserve(length);
    for (qsizetype index = 0; index < length; ++index) {
        char character = source.at(index);
        if (character == g_chBackSlash) {
            if (++index >= length) {
                destination->clear();
                return FALSE;
            }
            switch (source.at(index)) {
            case '0': character = g_chEOS; break;
            case 'n': character = g_chLF; break;
            case 'r': character = g_chCR; break;
            case 't': character = g_chTab; break;
            case 'b': character = g_chSpace; break;
            case 'c': character = g_chComma; break;
            case g_chBackSlash: character = g_chBackSlash; break;
            default:
                destination->clear();
                return FALSE;
            }
        }
        destination->append(character);
    }
    return TRUE;
}

BOOL bLowLevelQuoting(CHAR quotingChar, BOOL treatAsByteArray,
                      const QByteArray& source, QByteArray* destination,
                      BOOL* freeDestination, BOOL removeCarriageReturns)
{
    Q_UNUSED(treatAsByteArray);
    if (!destination || !freeDestination) return FALSE;
    destination->clear();
    *freeDestination = FALSE;
    const qsizetype length = nulTerminatedSize(source);
    bool needsChange = false;
    for (qsizetype index = 0; index < length; ++index) {
        const char character = source.at(index);
        if (character == quotingChar || character == g_chLF
            || character == g_chCR) {
            needsChange = true;
            break;
        }
    }
    if (!needsChange) {
        *destination = source.left(length);
        return TRUE;
    }

    destination->reserve(length * 2);
    for (qsizetype index = 0; index < length; ++index) {
        const char character = source.at(index);
        if (character == g_chLF) {
            destination->append(quotingChar);
            destination->append('n');
        } else if (character == g_chCR) {
            if (!removeCarriageReturns) {
                destination->append(quotingChar);
                destination->append('r');
            }
        } else {
            if (character == quotingChar) destination->append(quotingChar);
            destination->append(character);
        }
    }
    *freeDestination = TRUE;
    return TRUE;
}

BOOL bLowLevelUnquoting(CHAR quotingChar, BOOL treatAsByteArray,
                        const QByteArray& source, QByteArray* destination)
{
    Q_UNUSED(treatAsByteArray);
    if (!destination) return FALSE;
    const qsizetype length = nulTerminatedSize(source);
    bool quotedCharacter = false;
    bool quotedString = true;
    for (qsizetype index = 0; index < length; ++index) {
        if (source.at(index) != quotingChar) continue;
        if (index + 1 >= length) {
            quotedString = false;
            break;
        }
        const char next = source.at(index + 1);
        if (next == 'n' || next == 'r') {
            quotedCharacter = true;
            ++index;
        } else if (next == quotingChar) {
            ++index;
        } else {
            quotedString = false;
            break;
        }
    }

    if (!quotedString) {
        *destination = source.left(length);
        return TRUE;
    }

    destination->clear();
    destination->reserve(length);
    for (qsizetype index = 0; index < length; ++index) {
        char character = source.at(index);
        if (character == quotingChar) {
            if (++index >= length) return FALSE;
            const char next = source.at(index);
            if (next == 'n') character = g_chLF;
            else if (next == 'r') character = g_chCR;
            else if (next == quotingChar) character = quotingChar;
            else return FALSE;
        }
        destination->append(character);
    }
    Q_UNUSED(quotedCharacter);
    return TRUE;
}
