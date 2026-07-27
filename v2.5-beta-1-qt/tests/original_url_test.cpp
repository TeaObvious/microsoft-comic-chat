#include "balloon.h"
#include "chat.h"
#include "format.h"
#include "histent.h"
#include "originalassets.h"
#include "panel.h"
#include "textcore.h"
#include "urlutil.h"
#include "userinfo.h"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QSet>
#include <QTextCursor>
#include <QTextEdit>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
[[noreturn]] void fail() { std::abort(); }

void requireAt(bool condition, int line)
{
    if (!condition) {
        std::fprintf(stderr, "require failed at line %d\n", line);
        fail();
    }
}

#define REQUIRE(condition) requireAt((condition), __LINE__)

WORD formatAt(const CDWordArray* formatting, int offset)
{
    WORD result = 0;
    for (int index = 0; formatting && index < formatting->GetSize(); ++index) {
        if (HIWORD(formatting->GetAt(index)) > offset) break;
        result = LOWORD(formatting->GetAt(index));
    }
    return result;
}

class CapturingPanel final : public CPanel {
public:
    void Draw(QtPaintDC*, POINT*, RECT*) override {}
    void LayoutAvatars() override {}
    BOOL LayoutBalloons(char**, CDWordArray**, char**) override { return TRUE; }
    BOOL LayoutBalloon(CBalloon*[], int, int, RECT&) override { return TRUE; }
    RECT GetBalloonRect() override { return {}; }
    CPanel* Clone() override { return new CapturingPanel; }
    void OnClickHotLink(UINT link, const char* text) override
    {
        links.insert(link);
        linkText = text ? QByteArray(text) : QByteArray();
    }

    QSet<UINT> links;
    QByteArray linkText;
};
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    theApp.InitVals();
    theApp.InitializeComicsFonts();
    CUnitPanelPage::SetUnitPanelWidth(MINUNITPANELWIDTH * 2);
    CUnitPanelPage::SetUnitPanelHeight(MINUNITPANELHEIGHT * 2);
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont,
                                     theApp.m_comicsColor));

    const QString sourceMessage = originalResourceString(
        QStringLiteral("IDS_MICONLY"));
    const QByteArray sourceBytes = sourceMessage.toUtf8();
    REQUIRE(!sourceBytes.isEmpty());

    CUrlRec recognizer;
    int bounds[MAX_URL_INTEXT * 2]{};
    int count = MAX_URL_INTEXT;
    REQUIRE(recognizer.HrIdentifyUrls(sourceBytes.constData(), bounds, &count)
            == URLUTIL_NOERROR);
    REQUIRE(count == 1);
    REQUIRE(bounds[0] >= 0 && bounds[1] > bounds[0]);
    REQUIRE(bounds[1] < sourceBytes.size());
    REQUIRE(sourceBytes.at(bounds[1]) == '.');
    const QByteArray sourceUrl = sourceBytes.mid(bounds[0],
                                                  bounds[1] - bounds[0]);
    REQUIRE(sourceUrl.startsWith(szURLPREFIXSBROWSER));

    const int sourceColon = sourceUrl.indexOf(':');
    REQUIRE(sourceColon > 0);
    const QByteArray sourceSuffix = sourceUrl.mid(sourceColon);
    const auto requirePrefixes = [&](const char* prefixes) {
        for (const char* prefix = prefixes; *prefix;
             prefix += std::strlen(prefix) + 1) {
            const QByteArray candidate = QByteArray(prefix) + sourceSuffix;
            int candidateBounds[2]{};
            int candidateCount = 1;
            REQUIRE(recognizer.HrIdentifyUrls(candidate.constData(),
                                              candidateBounds,
                                              &candidateCount)
                    == URLUTIL_NOERROR);
            REQUIRE(candidateCount == 1);
            REQUIRE(candidateBounds[0] == 0);
            REQUIRE(candidateBounds[1] == candidate.size());
        }
    };
    requirePrefixes(szURLPREFIXSBROWSER);
    requirePrefixes(szURLPREFIXS);

    const QByteArray oneCharacterSuffix = sourceUrl.left(sourceColon + 2);
    int rejectedBounds[2]{};
    int rejectedCount = 1;
    REQUIRE(recognizer.HrIdentifyUrls(oneCharacterSuffix.constData(),
                                      rejectedBounds, &rejectedCount)
            == URLUTIL_NOERROR);
    REQUIRE(rejectedCount == 0);
    REQUIRE(!recognizer.bLaunchUrl(oneCharacterSuffix.constData()));

    QByteArray overCapacity;
    for (int index = 0; index <= MAX_URL_INTEXT; ++index) {
        if (!overCapacity.isEmpty()) overCapacity.append(' ');
        overCapacity.append(sourceUrl);
    }
    int capacityBounds[MAX_URL_INTEXT * 2]{};
    int capacity = MAX_URL_INTEXT;
    REQUIRE(recognizer.HrIdentifyUrls(overCapacity.constData(),
                                      capacityBounds, &capacity)
            == URLUTIL_S_FALSE);
    REQUIRE(capacity == MAX_URL_INTEXT);

    CDWordArray* links = IdentifyURLs(nullptr, sourceBytes.constData());
    REQUIRE(links != nullptr);
    REQUIRE(links->GetSize() == 2);
    REQUIRE(links->GetAt(0) == MAKELONG(wLink, bounds[0]));
    REQUIRE(links->GetAt(1) == MAKELONG(0, bounds[1]));

    CDWordArray existing;
    existing.Add(MAKELONG(wBold, 0));
    REQUIRE(IdentifyURLs(&existing, sourceBytes.constData()) == &existing);
    REQUIRE(formatAt(&existing, bounds[0]) == (wBold | wLink));
    REQUIRE(formatAt(&existing, bounds[1]) == wBold);

    CUserInfo sourceUser(originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK")));
    SayEntry sourceEntry(&sourceUser, sourceMessage, NoFormattingSentinel());
    REQUIRE(bURLPresent(sourceEntry.m_prgdwFormatting));
    REQUIRE(formatAt(sourceEntry.m_prgdwFormatting, bounds[0]) & wLink);

    const QByteArray secondSourceUrl = originalResourceString(
        QStringLiteral("IDS_URL_MSPREFIX")).toUtf8();
    REQUIRE(!secondSourceUrl.isEmpty());
    QByteArray twoSourceUrls = sourceUrl;
    twoSourceUrls.append(' ');
    twoSourceUrls.append(secondSourceUrl);
    CDWordArray* twoLinks = IdentifyURLs(nullptr, twoSourceUrls.constData());
    REQUIRE(twoLinks != nullptr);
    REQUIRE(twoLinks->GetSize() == 4);

    CHotLinkLabel label(twoSourceUrls.constData(),
                        CUnitPanelPage::m_fiWNormal, twoLinks);
    label.m_format |= FT_LEFT_JUSTIFY;
    REQUIRE(label.SetBBox(0, -MINUNITPANELHEIGHT * 2,
                          MINUNITPANELWIDTH * 2, 0));
    CapturingPanel panel;
    const int xStep = 20;
    const int yStep = qMax(1, label.m_fontI->m_lineHeight / 3);
    for (int y = 0; y >= -MINUNITPANELHEIGHT * 2
                    && panel.links.size() < 2; y -= yStep) {
        for (int x = 0; x <= MINUNITPANELWIDTH * 2
                        && panel.links.size() < 2; x += xStep) {
            POINT point{x, y};
            label.OnLButtonDown(point, &panel);
        }
    }
    REQUIRE(panel.links.contains(0));
    REQUIRE(panel.links.contains(1));
    // The source intentionally passes URL slot zero for CHotLinkLabel.
    REQUIRE(panel.linkText == sourceUrl);

    const qsizetype hitsBefore = panel.links.size();
    POINT outside{MINUNITPANELWIDTH * 2 + 1, 1};
    label.OnLButtonDown(outside, &panel);
    REQUIRE(panel.links.size() == hitsBefore);

    const QString sourcePrefix = originalDialogControlText(
        QStringLiteral("IDD_ABOUTBOX"), QStringLiteral("IDC_COPY"));
    QFile resourceFile(originalAssetPath(QStringLiteral("chat.rc")));
    REQUIRE(resourceFile.open(QIODevice::ReadOnly));
    const QByteArray resourceBytes = resourceFile.readAll();
    REQUIRE(resourceBytes.contains("#pragma code_page(1252)"));
    REQUIRE(resourceBytes.contains(
        "Copyright \xA9 1996-1998 Microsoft Corporation"));
    REQUIRE(!resourceBytes.contains(
        "Copyright \xC2\xA9 1996-1998 Microsoft Corporation"));
    REQUIRE(sourcePrefix
            == QStringLiteral("Copyright © 1996-1998 Microsoft Corporation"));
    REQUIRE(sourcePrefix.toUtf8().size() > sourcePrefix.size());
    const QString textCoreMessage = sourcePrefix + QLatin1Char(' ')
        + QString::fromUtf8(sourceUrl);
    const QByteArray textCoreBytes = textCoreMessage.toUtf8();
    QTextEdit textEdit;
    CTextCore textCore;
    REQUIRE(textCore.AttachTextViewHWnd(&textEdit));
    REQUIRE(textCore.iDisplayMsgText(
                textCoreBytes.constData(), textCoreBytes.size(), mtNormal,
                msParticipant, TRUE, FALSE, FALSE, DEFAULT_INDENT)
            == 0);
    REQUIRE(textEdit.toPlainText() == textCoreMessage);
    const int textUrlStart = textCoreMessage.indexOf(
        QString::fromUtf8(sourceUrl));
    REQUIRE(textUrlStart > 0);
    QTextCursor textUrl(textEdit.document());
    textUrl.setPosition(textUrlStart);
    textUrl.movePosition(QTextCursor::NextCharacter,
                         QTextCursor::KeepAnchor);
    REQUIRE(textUrl.charFormat().isAnchor());
    REQUIRE(textUrl.charFormat().anchorHref() == QString::fromUtf8(sourceUrl));
    QTextCursor beforeTextUrl(textEdit.document());
    beforeTextUrl.setPosition(textUrlStart - 1);
    beforeTextUrl.movePosition(QTextCursor::NextCharacter,
                               QTextCursor::KeepAnchor);
    REQUIRE(!beforeTextUrl.charFormat().isAnchor());

    QTextCursor savedSelection(textEdit.document());
    savedSelection.setPosition(0);
    savedSelection.setPosition(1, QTextCursor::KeepAnchor);
    textEdit.setTextCursor(savedSelection);
    REQUIRE(textCore.iDisplayMsgText(sourceUrl.constData(), sourceUrl.size(),
                                     mtNormal, msParticipant, TRUE, FALSE,
                                     FALSE, DEFAULT_INDENT) == 0);
    REQUIRE(textEdit.textCursor().selectionStart() == 0);
    REQUIRE(textEdit.textCursor().selectionEnd() == 1);

    textCore.dwClearTextViewBuffer();
    REQUIRE(textCore.iDisplayMsgText(sourceUrl.constData(), sourceUrl.size(),
                                     mtNormal, msParticipant, FALSE, FALSE,
                                     FALSE, DEFAULT_INDENT) == 0);
    QTextCursor unlinked(textEdit.document());
    unlinked.setPosition(0);
    unlinked.movePosition(QTextCursor::NextCharacter,
                          QTextCursor::KeepAnchor);
    REQUIRE(!unlinked.charFormat().isAnchor());
    REQUIRE(!textCore.bHandleLink(QString::fromUtf8(oneCharacterSuffix)));
    textCore.DetachTextViewHWnd();

    const QString splitSourceUrl = originalResourceString(
        QStringLiteral("IDS_URL_MSPREFIX")) + originalResourceString(
            QStringLiteral("IDS_URL_PRODUCTNEWS"));
    const QByteArray splitSourceBytes = splitSourceUrl.toUtf8();
    int splitBounds[2]{};
    int splitCount = 1;
    REQUIRE(recognizer.HrIdentifyUrls(splitSourceBytes.constData(),
                                      splitBounds, &splitCount)
            == URLUTIL_NOERROR);
    REQUIRE(splitCount == 1);
    REQUIRE(splitBounds[0] == 0);
    REQUIRE(splitBounds[1] == splitSourceBytes.size() - 1);
    REQUIRE(splitSourceBytes.at(splitBounds[1]) == '=');
    const QByteArray splitRecognizedUrl = splitSourceBytes.mid(
        splitBounds[0], splitBounds[1] - splitBounds[0]);
    CDWordArray* splitFormatting = IdentifyURLs(
        nullptr, splitSourceBytes.constData());
    REQUIRE(splitFormatting != nullptr);
    REQUIRE(splitFormatting->GetSize() == 2);
    CBWoodringNormal splitBalloon(splitSourceBytes.constData(),
                                  splitFormatting, nullptr);
    FreeAndNullFormatting(&splitFormatting);
    REQUIRE(splitBalloon.SetBBox(0, -MINUNITPANELHEIGHT * 2,
                                 MINUNITPANELWIDTH * 2, 0));
    REQUIRE(splitBalloon.m_fInfo != nullptr);
    REQUIRE(splitBalloon.m_fInfo->m_nLines > 1);
    CDWordArray* restFormatting = nullptr;
    char* urlStartInRest = nullptr;
    char* rest = splitBalloon.SplitHeight(
        400 + CUnitPanelPage::m_fiWNormal->m_lineHeight,
        &restFormatting, &urlStartInRest);
    REQUIRE(rest != nullptr);
    REQUIRE(restFormatting != nullptr);
    REQUIRE(urlStartInRest != nullptr);
    REQUIRE(QByteArray(urlStartInRest) == splitRecognizedUrl);
    REQUIRE(QByteArray(rest).startsWith("..."));
    REQUIRE(!(formatAt(restFormatting, 0) & wLink));
    REQUIRE(formatAt(restFormatting, 3) & wLink);
    REQUIRE(QByteArray(splitBalloon.m_str).endsWith("..."));
    REQUIRE(!(formatAt(splitBalloon.m_prgdwFormatting,
                       static_cast<int>(std::strlen(splitBalloon.m_str)) - 1)
              & wLink));
    CBWoodringNormal restBalloon(rest, restFormatting, urlStartInRest);
    REQUIRE(restBalloon.m_prgszURLs != nullptr);
    REQUIRE(restBalloon.m_prgszURLs[0] != nullptr);
    REQUIRE(QByteArray(restBalloon.m_prgszURLs[0]) == splitRecognizedUrl);
    std::free(rest);
    delete[] urlStartInRest;
    FreeAndNullFormatting(&restFormatting);

    FreeAndNullFormatting(&twoLinks);
    FreeAndNullFormatting(&links);
    CUnitPanelPage::DestroyFonts();
    return 0;
}
