#include "ccommon.h"
#include "chat.h"
#include "ircproto.h"

#include <iostream>

namespace {
bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}
}

int main()
{
    bool okay = true;

    okay &= expect(g_nMaxLengthSmall == 31, "small length");
    okay &= expect(g_nMaxLength == 255, "large length");
    okay &= expect(g_nMaxChanBuff == 200, "channel buffer length");
    okay &= expect(QByteArray(g_szNoMachine) == QByteArrayLiteral("NoMachine"),
                   "NoMachine source constant");

    okay &= expect(!bExtendedString(QByteArrayLiteral("A")),
                   "ASCII is not extended");
    okay &= expect(bExtendedString(QByteArray(1, static_cast<char>(0x80))),
                   "upper byte is extended");
    okay &= expect(!bExtendedWideString(QStringView(u"A")),
                   "wide ASCII is not extended");
    okay &= expect(bExtendedWideString(QStringView(u"\u00a2")),
                   "wide upper character is extended");

    okay &= expect(!bExtendedChannelName(QByteArrayLiteral("A")),
                   "plain channel character");
    okay &= expect(bExtendedChannelName(QByteArrayLiteral("A B")),
                   "channel space");
    okay &= expect(bExtendedChannelName(QByteArrayLiteral("A,B")),
                   "channel comma");
    okay &= expect(!bExtendedChannelName(
                       QByteArray(1, static_cast<char>(0x80)), TRUE),
                   "upper ANSI accepted");
    okay &= expect(bExtendedWideChannelName(QStringView(u"A\nB")),
                   "wide channel line feed");

    okay &= expect(!bExtendedNickname(QByteArrayLiteral("A-0")),
                   "source nickname alphabet");
    okay &= expect(bExtendedNickname(QByteArrayLiteral("0A")),
                   "nickname leading digit");
    okay &= expect(bExtendedNickname(QByteArrayLiteral("A.B")),
                   "nickname punctuation");
    okay &= expect(!bExtendedWideNickname(QStringView(u"[]{}_|`^-0")),
                   "wide nickname alphabet");
    okay &= expect(bExtendedWideNickname(QStringView(u"A\u00a2")),
                   "wide extended nickname");

    QByteArray encoded;
    INT encodedLength = -1;
    const QString utfSource = QString::fromUtf16(
        reinterpret_cast<const char16_t*>(u"A\u00a2\u20ac"));
    okay &= expect(bConvertWideStringToUTF8(
                       QStringView(utfSource), 0, &encoded, &encodedLength,
                       FALSE, FALSE, FALSE, FALSE),
                   "wide to source UTF-8");
    const QByteArray expectedUtf = QByteArrayLiteral("A")
        + QByteArray::fromHex("c2a2e282ac");
    okay &= expect(encoded == expectedUtf, "one/two/three byte encoding");
    okay &= expect(encodedLength == expectedUtf.size(), "encoded byte count");

    okay &= expect(bConvertWideStringToUTF8(
                       QStringView(u"A\n\r\t ,\\*?"), 0, &encoded,
                       &encodedLength, TRUE, FALSE, TRUE, TRUE),
                   "nickname postprocess");
    okay &= expect(encoded == QByteArrayLiteral("'A\\n\\r\\t\\b\\c\\\\\\*\\?"),
                   "nickname prefix and escapes");

    okay &= expect(bConvertWideStringToUTF8(
                       QStringView(u"A"), 0, &encoded, nullptr,
                       FALSE, TRUE, TRUE, FALSE)
                       && encoded == QByteArrayLiteral("%#A"),
                   "missing channel prefix");
    okay &= expect(bConvertWideStringToUTF8(
                       QStringView(u"&A"), 0, &encoded, nullptr,
                       FALSE, TRUE, TRUE, FALSE)
                       && encoded == QByteArrayLiteral("%&A"),
                   "local channel prefix");
    okay &= expect(bConvertWideStringToUTF8(
                       QStringView(u"%#A"), 0, &encoded, nullptr,
                       FALSE, TRUE, TRUE, FALSE)
                       && encoded == QByteArrayLiteral("%#A"),
                   "existing extended channel prefix");

    QString decoded;
    INT decodedCount = -1;
    okay &= expect(bConvertUTF8StringToWide(
                       expectedUtf, 0, &decoded, &decodedCount,
                       FALSE, FALSE, FALSE),
                   "source UTF-8 to wide");
    okay &= expect(decoded == utfSource, "source UTF-8 roundtrip");
    okay &= expect(decodedCount == 3, "decoded character count");

    okay &= expect(bConvertUTF8StringToWide(
                       QByteArrayLiteral("%#A\\c"), 0, &decoded,
                       &decodedCount, FALSE, TRUE, TRUE),
                   "channel decode");
    okay &= expect(decoded == QStringLiteral("%#A,"),
                   "channel decode prefixes and comma");
    okay &= expect(decodedCount == 4, "channel decode count");

    okay &= expect(bConvertUTF8StringToWide(
                       QByteArrayLiteral("'A\\*B"), 0, &decoded,
                       &decodedCount, TRUE, FALSE, TRUE),
                   "unknown postprocess escape");
    okay &= expect(decoded == QStringLiteral("AB"),
                   "unknown escape consumes both bytes");
    okay &= expect(decodedCount == 3,
                   "source counts consumed unknown escape iteration");

    okay &= expect(bConvertUTF8StringToWide(
                       QByteArray::fromHex("c2"), 0, &decoded, nullptr,
                       FALSE, FALSE, FALSE)
                       && decoded == QStringLiteral("?"),
                   "truncated two-byte sequence");
    okay &= expect(bConvertUTF8StringToWide(
                       QByteArray::fromHex("e282"), 0, &decoded, nullptr,
                       FALSE, FALSE, FALSE)
                       && decoded == QStringLiteral("??"),
                   "truncated three-byte sequence source loop");

    const QByteArray nextBytes = QByteArrayLiteral("A\\n")
        + QByteArray::fromHex("c2a2e282ac");
    const char* next = nextBytes.constData();
    next = SzNextUTF8Char(next);
    okay &= expect(next - nextBytes.constData() == 1, "next ASCII");
    next = SzNextUTF8Char(next);
    okay &= expect(next - nextBytes.constData() == 3, "next escape");
    next = SzNextUTF8Char(next);
    okay &= expect(next - nextBytes.constData() == 5, "next two-byte");
    next = SzNextUTF8Char(next);
    okay &= expect(next - nextBytes.constData() == 8, "next three-byte");

    QByteArray data;
    data.append('\0');
    data.append('\\');
    data.append('\n');
    data.append('\r');
    data.append('\t');
    data.append(' ');
    data.append(',');
    data.append('A');
    QByteArray wire;
    okay &= expect(bDataToString(data, &wire, FALSE), "data to string");
    okay &= expect(wire == QByteArrayLiteral("\\0\\\\\\n\\r\\t\\b\\cA"),
                   "data escape bytes");
    QByteArray roundtrip;
    okay &= expect(bStringToData(wire, &roundtrip), "string to data");
    okay &= expect(roundtrip == data, "data/string roundtrip");
    okay &= expect(!bStringToData(QByteArrayLiteral("\\x"), &roundtrip)
                       && roundtrip.isEmpty(),
                   "unknown data escape fails");
    okay &= expect(bDataToString(QByteArrayLiteral(" A,B"), &wire, TRUE)
                       && wire == QByteArrayLiteral(" A,B"),
                   "after-colon space and comma");

    BOOL freeWire = FALSE;
    QByteArray quoted;
    QByteArray lowSource;
    lowSource.append('A');
    lowSource.append(g_chLLQuoteCTCP);
    lowSource.append('\n');
    lowSource.append('\r');
    okay &= expect(bLowLevelQuoting(g_chLLQuoteCTCP, TRUE, lowSource,
                                    &quoted, &freeWire, FALSE),
                   "low-level quote");
    QByteArray lowExpected("A", 1);
    lowExpected.append(g_chLLQuoteCTCP);
    lowExpected.append(g_chLLQuoteCTCP);
    lowExpected.append(g_chLLQuoteCTCP);
    lowExpected.append('n');
    lowExpected.append(g_chLLQuoteCTCP);
    lowExpected.append('r');
    okay &= expect(freeWire && quoted == lowExpected,
                   "low-level quoted bytes");
    okay &= expect(bLowLevelUnquoting(g_chLLQuoteCTCP, TRUE, quoted,
                                      &roundtrip)
                       && roundtrip == lowSource,
                   "low-level roundtrip");
    okay &= expect(bLowLevelQuoting(g_chLLQuoteCTCP, TRUE,
                                    QByteArrayLiteral("A\rB"), &quoted,
                                    &freeWire, TRUE)
                       && quoted == QByteArrayLiteral("AB") && freeWire,
                   "remove carriage returns");
    QByteArray malformed("A", 1);
    malformed.append(g_chLLQuoteCTCP);
    malformed.append('x');
    okay &= expect(bLowLevelUnquoting(g_chLLQuoteCTCP, TRUE, malformed,
                                      &roundtrip)
                       && roundtrip == malformed,
                   "malformed low-level quote remains unchanged");

    QByteArray converted;
    BOOL convertedBuffer = TRUE;
    okay &= expect(bConvertString(FALSE, ANSI_CHARSET,
                                  QByteArrayLiteral("A"), &converted,
                                  &convertedBuffer)
                       && converted == QByteArrayLiteral("A")
                       && !convertedBuffer,
                   "ANSI conversion fast path");

    const QByteArray shiftJisPair = QByteArray::fromHex("82a0");
    const QByteArray expectedJisPair = QByteArray::fromHex(
        "1b244224221b2842");
    okay &= expect(bConvertString(FALSE, SHIFTJIS_CHARSET, shiftJisPair,
                                  &converted, &convertedBuffer)
                       && convertedBuffer && converted == expectedJisPair,
                   "Shift-JIS to original IRC JIS bytes");
    okay &= expect(bConvertString(TRUE, SHIFTJIS_CHARSET, converted,
                                  &roundtrip, &convertedBuffer)
                       && convertedBuffer && roundtrip == shiftJisPair,
                   "original IRC JIS to Shift-JIS bytes");

    QByteArray fullWidthKana;
    okay &= expect(bSB2DBKatakana(QByteArray::fromHex("a6"),
                                  &fullWidthKana, &convertedBuffer)
                       && convertedBuffer
                       && fullWidthKana == QByteArray::fromHex("8392"),
                   "single-byte to double-byte Katakana");

    const QByteArray isoEastEurope = QByteArray::fromHex("a1");
    okay &= expect(bConvertString(TRUE, EASTEUROPE_CHARSET, isoEastEurope,
                                  &converted, &convertedBuffer)
                       && convertedBuffer
                       && converted == QByteArray::fromHex("a5"),
                   "ISO-8859-2 to Windows-1250");
    okay &= expect(bConvertString(FALSE, EASTEUROPE_CHARSET, converted,
                                  &roundtrip, &convertedBuffer)
                       && roundtrip == isoEastEurope,
                   "Windows-1250 to ISO-8859-2");

    const QString wideShiftJis = QStringLiteral("\u3042");
    okay &= expect(bWideToCharacterSet(QStringView(wideShiftJis),
                                       SHIFTJIS_CHARSET, &converted)
                       && converted == shiftJisPair,
                   "wide to Shift-JIS platform boundary");
    QString wideRoundtrip;
    okay &= expect(bCharacterSetToWide(converted, SHIFTJIS_CHARSET,
                                       &wideRoundtrip)
                       && wideRoundtrip == wideShiftJis,
                   "Shift-JIS to wide platform boundary");

    const QString encodedNick = EncodeNick(QStringLiteral("A B,*?\\\n"), true);
    okay &= expect(encodedNick.toUtf8()
                       == QByteArrayLiteral("'A\\bB\\c\\*\\?\\\\\\n"),
                   "ircproto nickname delegates to ccommon");
    okay &= expect(DecodeNick(QStringLiteral("'A\\bB\\c\\\\\\n"))
                       == QStringLiteral("A B,\\\n"),
                   "ircproto nickname decode delegates to ccommon");
    okay &= expect(EncodeChan(QStringLiteral("A B"))
                       == QStringLiteral("%#A\\bB"),
                   "ircproto channel delegates to ccommon");
    okay &= expect(DecodeChan(QStringLiteral("%#A\\bB"))
                       == QStringLiteral("A B"),
                   "ircproto channel decode strips source prefixes");

    CIrcProto protocol;
    const QString utfParameter = QStringLiteral("A\u00a2\u20ac");
    okay &= expect(protocol.EncodeString(utfParameter, ENC_UTF8).toUtf8()
                       == expectedUtf,
                   "ircproto UTF-8 parameter delegates to ccommon");
    int parameterEncoding = ENC_UTF8;
    okay &= expect(protocol.StrEncodeCommandParam(
                       AT_MESSAGE, &parameterEncoding, utfParameter).toUtf8()
                       == expectedUtf,
                   "command parameter delegates to EncodeString");

    theApp.m_charSet = SHIFTJIS_CHARSET;
    okay &= expect(protocol.EncodeStringBytes(wideShiftJis, ENC_DBCS)
                       == QByteArray::fromHex("1b244224221b2842"),
                   "protocol DBCS path uses Shift-JIS/JIS conversion");
    okay &= expect(DecodeString(expectedJisPair, ENC_DBCS)
                       == wideShiftJis,
                   "incoming original JIS string conversion");
    QString incomingDbcs = QString::fromLatin1(expectedJisPair);
    CSInString(&incomingDbcs);
    okay &= expect(incomingDbcs == wideShiftJis,
                   "CSInString DBCS selection");
    incomingDbcs = QString::fromLatin1(expectedJisPair);
    CSInPlace(&incomingDbcs);
    okay &= expect(incomingDbcs == wideShiftJis,
                   "CSInPlace DBCS selection");

    const QString wideDbcsChannel = QLatin1Char('#') + wideShiftJis;
    const QString encodedDbcsChannel = EncodeChan(wideDbcsChannel);
    okay &= expect(encodedDbcsChannel.toLatin1()
                       == QByteArrayLiteral("#") + expectedJisPair,
                   "DBCS channel outgoing JIS conversion");
    okay &= expect(DecodeChan(encodedDbcsChannel) == wideDbcsChannel,
                   "DBCS channel incoming JIS conversion");

    QString incomingUtf = QString::fromLatin1(expectedUtf);
    CSInString(&incomingUtf, QStringLiteral("%"));
    okay &= expect(incomingUtf == utfSource,
                   "CSInString percent-channel UTF-8 selection");
    theApp.m_charSet = ANSI_CHARSET;

    QString ansiDbcs = QString::fromLatin1(expectedUtf);
    const QString unchangedAnsiDbcs = ansiDbcs;
    CSInString(&ansiDbcs);
    okay &= expect(ansiDbcs == unchangedAnsiDbcs,
                   "CSInString ANSI DBCS fast path");

    return okay ? 0 : 1;
}
