#include "ccommon.h"
#include "format.h"
#include "intl.h"

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        ++failures;
    }
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    check(IsDBCSLeadByteEx(932, 0x81)
              && IsDBCSLeadByteEx(932, 0x9f)
              && IsDBCSLeadByteEx(932, 0xe0)
              && IsDBCSLeadByteEx(932, 0xfc)
              && !IsDBCSLeadByteEx(932, 0x80)
              && !IsDBCSLeadByteEx(932, 0xa0)
              && !IsDBCSLeadByteEx(932, 0xfd),
          "CP932 lead-byte ranges differ from intl.c");
    for (const int codePage : {936, 949, 950}) {
        check(IsDBCSLeadByteEx(codePage, 0x81)
                  && IsDBCSLeadByteEx(codePage, 0xfe)
                  && !IsDBCSLeadByteEx(codePage, 0x80)
                  && !IsDBCSLeadByteEx(codePage, 0xff),
              "CP936/949/950 lead-byte range differs from intl.c");
    }

    const QByteArray shiftJis = QByteArray::fromHex("4182a042");
    const char* start = shiftJis.constData();
    check(CharNextEx(932, start) == start + 1
              && CharNextEx(932, start + 1) == start + 3,
          "CharNextEx does not retain source byte stepping");
    check(IsTrailByte(932, start, start + 2)
              && !IsTrailByte(932, start, start + 1),
          "IsTrailByte does not retain source boundary detection");
    check(CharPrevEx(932, start, start + 3) == start + 1
              && CharPrevEx(932, start, start + 1) == start,
          "CharPrevEx does not retain source byte stepping");

    check(IsWrapDown932(TRUE, QByteArray::fromHex("8165").constData())
              && !IsWrapDown932(TRUE,
                                QByteArray::fromHex("8166").constData())
              && IsWrapUp932(TRUE,
                             QByteArray::fromHex("8141").constData())
              && IsWrapDown932(FALSE, "(")
              && IsWrapUp932(FALSE, ")"),
          "CP932 punctuation tables differ from intl.c");
    check(IsWrapDown949(TRUE, QByteArray::fromHex("a1ae").constData())
              && IsWrapUp949(TRUE,
                             QByteArray::fromHex("a1af").constData()),
          "CP949 punctuation tables differ from intl.c");
    check(IsWrapDown950(TRUE, QByteArray::fromHex("a15d").constData())
              && IsWrapUp950(TRUE,
                             QByteArray::fromHex("a141").constData()),
          "CP950 punctuation tables differ from intl.c");
    check(IsWrapDown936(TRUE, QByteArray::fromHex("a1ae").constData())
              && IsWrapUp936(TRUE,
                             QByteArray::fromHex("a1a3").constData()),
          "CP936 punctuation tables differ from intl.c");

    SetMime(ANSI_CHARSET);
    check(GetMime() == nullptr && iBytesofChar(0x82) == 1,
          "non-Far-East SetMime state differs from source");
    SetMime(SHIFTJIS_CHARSET);
    auto* mime = static_cast<SCRIPTINFO*>(GetMime());
    check(mime && mime->iCp == 932 && iBytesofChar(0x82) == 2
              && iBytesofChar('A') == 1,
          "Shift-JIS SetMime selection differs from source table");

    const QString hiragana = QStringLiteral("\u3042");
    const QByteArray native = IntlTextFromQString(QStringView(hiragana));
    check(native == QByteArray::fromHex("82a0"),
          "QString to Shift-JIS adapter differs");
    check(IntlTextToQString(native.constData(), native.size()) == hiragana,
          "Shift-JIS to QString adapter differs");

    const QByteArray utf8 = QByteArrayLiteral("A")
        + QByteArray::fromHex("e38182") + QByteArrayLiteral("B");
    CDWordArray utf8Formatting;
    utf8Formatting.Add(MAKELONG(wBold, 0));
    utf8Formatting.Add(MAKELONG(wItalic, 1));
    utf8Formatting.Add(MAKELONG(0, 4));
    CDWordArray* nativeFormatting = IntlFormattingFromUtf8(
        utf8.constData(), &utf8Formatting);
    check(nativeFormatting && nativeFormatting->GetSize() == 3
              && HIWORD(nativeFormatting->GetAt(0)) == 0
              && HIWORD(nativeFormatting->GetAt(1)) == 1
              && HIWORD(nativeFormatting->GetAt(2)) == 3,
          "UTF-8 to Shift-JIS formatting offsets differ");
    FreeAndNullFormatting(&nativeFormatting);

    QByteArray repeated = native + native + native;
    QImage surface(64, 64, QImage::Format_RGB32);
    QPainter painter(&surface);
    const QSize twoCharacters = GetFormattedTextExtent(
        &painter, repeated.constData(), 4, nullptr);
    int bytesFit = 0;
    BOOL hasBlankOrAlike = FALSE;
    QSize fitted;
    const BOOL found = FindSubStringForINTLThatFits(
        GetMime(), &painter, repeated.constData(), repeated.size(), nullptr,
        &bytesFit, &hasBlankOrAlike, &fitted, twoCharacters.width());
    check(found && hasBlankOrAlike && bytesFit > 0 && bytesFit <= 4
              && (bytesFit % 2) == 0,
          "Far-East fitted substring is not on a source DBCS boundary");

    SetMime(ANSI_CHARSET);
    return failures == 0 ? 0 : 1;
}
