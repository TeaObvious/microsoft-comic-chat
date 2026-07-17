#include "chat.h"
#include "chatdoc.h"
#include "chatview.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "protsupp.h"
#include "saywnd.h"
#include "status.h"
#include "textview.h"
#include "userinfo.h"

#include <QApplication>
#include <QTextCursor>

#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}

#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

QString resourceFormat(const QString& identifier, const QStringList& arguments)
{
    QString value = originalTextViewResourceString(identifier);
    for (int index = 0; index < arguments.size(); ++index)
        value.replace(QStringLiteral("%") + QString::number(index + 1),
                      arguments[index]);
    return value;
}

CHARFORMAT sourceColorFormat(COLORREF color)
{
    CHARFORMAT format{};
    format.cbSize = sizeof(CHARFORMAT);
    format.dwMask = CFM_COLOR | CFM_BOLD;
    format.crTextColor = color;
    format.bCharSet = ANSI_CHARSET;
    return format;
}

QColor messageColor(CTextCore& core, MSG_TYPE type, MEMBER_STATUS member,
                    BOOL header)
{
    QTextCharFormat* format = nullptr;
    REQUIRE(core.bGetMessageFormat(&format, type, member, header));
    REQUIRE(format != nullptr);
    return format->foreground().color();
}

QTextCharFormat* messageFormat(CTextCore& core, MSG_TYPE type,
                               MEMBER_STATUS member, BOOL header)
{
    QTextCharFormat* format = nullptr;
    REQUIRE(core.bGetMessageFormat(&format, type, member, header));
    REQUIRE(format != nullptr);
    return format;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.InitializeFonts();

    REQUIRE(!originalTextViewResourceString(
        QStringLiteral("IDS_NORMAL_HEADER")).isEmpty());
    REQUIRE(!originalTextViewResourceString(
        QStringLiteral("IDS_NOKICKREASON_INFO")).isEmpty());
    REQUIRE(sizeof(CHARFORMAT) == 60);

    const std::array<COLORREF, NREGULARFONTS> roleColors = {
        RGB(0, 128, 0), RGB(0, 0, 255), RGB(0, 0, 0),
        RGB(128, 0, 0), RGB(128, 128, 128), RGB(0, 0, 128),
        RGB(0, 128, 128), RGB(128, 0, 128), RGB(255, 0, 255),
        RGB(255, 0, 0)
    };
    std::memset(theApp.m_cfArray, 0, sizeof(theApp.m_cfArray));
    for (int index = 0; index < NREGULARFONTS; ++index)
        theApp.m_cfArray[index] = sourceColorFormat(roleColors[index]);
    const std::array<COLORREF, 4> highlightColors = {
        RGB(128, 128, 128), RGB(128, 128, 0),
        RGB(0, 255, 255), RGB(255, 0, 255)
    };
    for (int index = 0; index < NHIGHLIGHTEDFONTS; ++index) {
        CHARFORMAT& format =
            theApp.m_cfArray[NREGULARFONTS + index];
        format = sourceColorFormat(highlightColors[index / 2]);
        format.dwEffects = CFE_BOLD;
        if ((index & 1) == 0) {
            format.dwMask |= CFM_SIZE;
            format.yHeight = 240;
        }
    }

    {
        CTextCore textCore;
        theApp.m_bCfInitialized = false;
        theApp.m_bCfHLInitialized = false;
        theApp.m_iHostHighlight = 0;
        InitializeTextCore(&textCore, TRUE, FALSE);
        QTextCharFormat* defaultFormat = nullptr;
        REQUIRE(textCore.bGetTextViewDefaultFormat(&defaultFormat));
        REQUIRE(defaultFormat != nullptr);
        REQUIRE(defaultFormat->fontPointSize() == 10.0);

        theApp.m_bCfInitialized = true;
        theApp.m_bCfHLInitialized = true;
        InitializeTextCore(&textCore, TRUE, FALSE);
        for (int member = 0; member < msEndEnum; ++member) {
            const MEMBER_STATUS status = static_cast<MEMBER_STATUS>(member);
            REQUIRE(messageColor(textCore, mtJoin, status, FALSE)
                    == QColor(0, 128, 0));
            REQUIRE(messageColor(textCore, mtNormal, status, TRUE)
                    == QColor(0, 0, 255));
            REQUIRE(messageColor(textCore, mtNormal, status, FALSE)
                    == QColor(0, 0, 0));
            REQUIRE(messageColor(textCore, mtWhisper, status, TRUE)
                    == QColor(128, 0, 0));
            REQUIRE(messageColor(textCore, mtWhisper, status, FALSE)
                    == QColor(128, 128, 128));
            REQUIRE(messageColor(textCore, mtThought, status, TRUE)
                    == QColor(0, 0, 128));
            REQUIRE(messageColor(textCore, mtThought, status, FALSE)
                    == QColor(0, 128, 128));
            REQUIRE(messageColor(textCore, mtAction, status, FALSE)
                    == QColor(128, 0, 128));
            REQUIRE(messageColor(textCore, mtLeave, status, FALSE)
                    == QColor(255, 0, 0));
        }
        for (int type = mtBeginInfo; type < mtEndEnum; ++type) {
            REQUIRE(messageColor(textCore, static_cast<MSG_TYPE>(type),
                                 msHost, FALSE)
                    == QColor(255, 0, 255));
        }
        for (SHORT index = 0; index < g_nHighlightedFormats; ++index) {
            QTextCharFormat* format = nullptr;
            REQUIRE(textCore.bGetHighlightFormat(&format, index));
            REQUIRE(format != nullptr);
            REQUIRE(format->foreground().color()
                    == QColor(GetRValue(highlightColors[index / 2]),
                              GetGValue(highlightColors[index / 2]),
                              GetBValue(highlightColors[index / 2])));
            REQUIRE(format->fontWeight() == QFont::Bold);
            if ((index & 1) == 0)
                REQUIRE(format->fontPointSize() == 12.0);
            else
                REQUIRE(format->fontPointSize() == 0.0);
        }

        theApp.m_iHostHighlight = HH_BOLD_HEADERS | HH_BOLD_MESSAGES;
        InitializeTextCore(&textCore, TRUE, FALSE);
        REQUIRE(messageFormat(textCore, mtNormal, msHost, TRUE)
                    ->fontWeight() == QFont::Bold);
        REQUIRE(messageFormat(textCore, mtNormal, msHost, FALSE)
                    ->fontWeight() == QFont::Bold);
        REQUIRE(messageFormat(textCore, mtAction, msHost, FALSE)
                    ->fontWeight() == QFont::Bold);
        REQUIRE(messageFormat(textCore, mtNormal, msParticipant, TRUE)
                    ->fontWeight() == QFont::Normal);
        REQUIRE(messageFormat(textCore, mtNormal, msParticipant, FALSE)
                    ->fontWeight() == QFont::Normal);
    }

    {
        theApp.m_iHostHighlight = 0;
        theApp.m_bCfInitialized = true;
        theApp.m_bCfHLInitialized = true;
        CChatDoc textDocument;
        textDocument.m_bComicView = false;
        CTextView textRoleView(&textDocument);
        textDocument.m_textView = &textRoleView;

        CChatDoc comicDocument;
        comicDocument.m_bComicView = true;
        CTextView comicRoleView(&comicDocument);
        comicDocument.m_textView = &comicRoleView;

        CChatDoc statusDocument;
        statusDocument.m_bComicView = false;
        statusDocument.m_bStatusView = true;
        CTextView statusRoleView(&statusDocument);
        statusDocument.m_textView = &statusRoleView;

        theApp.m_cfArray[2].crTextColor = RGB(0, 128, 128);
        InitializeTextCores(FALSE, FALSE);
        REQUIRE(messageColor(textRoleView.m_textCore, mtNormal,
                             msParticipant, FALSE) == QColor(0, 128, 128));
        REQUIRE(messageColor(comicRoleView.m_textCore, mtNormal,
                             msParticipant, FALSE) == QColor(0, 0, 0));
        REQUIRE(messageColor(statusRoleView.m_textCore, mtNormal,
                             msParticipant, FALSE) == QColor(0, 0, 0));

        CHARFORMAT& sayFormat = theApp.m_cfArray[2];
        sayFormat = sourceColorFormat(RGB(0, 0, 0));
        sayFormat.dwMask |= CFM_FACE | CFM_SIZE | CFM_ITALIC
            | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_CHARSET;
        sayFormat.dwEffects = CFE_BOLD | CFE_ITALIC
            | CFE_UNDERLINE | CFE_STRIKEOUT;
        sayFormat.yHeight = 240;
        const QByteArray face = originalResourceString(
            QStringLiteral("ID_COMIC_FONT_NAME")).toLatin1();
        REQUIRE(!face.isEmpty() && face.size() < LF_FACESIZE);
        std::memcpy(sayFormat.szFaceName, face.constData(), face.size());

        theApp.m_bComicView = false;
        CSayWnd sayWindow(false, 0);
        textDocument.m_sayWnd = &sayWindow;
        SetChatDoc(&textDocument);
        InitializeTextCore(&textRoleView.m_textCore, FALSE, TRUE);
        REQUIRE(theApp.m_textFont.weight() == QFont::Bold);
        REQUIRE(theApp.m_textFont.italic());
        REQUIRE(theApp.m_textFont.underline());
        REQUIRE(theApp.m_textFont.strikeOut());
        REQUIRE(sayWindow.m_fontText.weight() == QFont::Normal);
        REQUIRE(!sayWindow.m_fontText.italic());
        REQUIRE(!sayWindow.m_fontText.underline());
        REQUIRE(!sayWindow.m_fontText.strikeOut());
        REQUIRE(sayWindow.m_fontText.pixelSize() == qAbs(nFontHeight));
        REQUIRE(sayWindow.m_fontText.family()
                == theApp.m_textFont.family());
        REQUIRE(theApp.m_charSet == ANSI_CHARSET);
        textDocument.m_sayWnd = nullptr;
        SetChatDoc(nullptr);
    }

    theApp.m_bCfInitialized = false;
    theApp.m_bCfHLInitialized = false;
    theApp.m_iHostHighlight = HH_BOLD_HEADERS | HH_BOLD_MESSAGES;
    theApp.m_bComicView = true;
    theApp.InitializeFonts();

    CChatDoc document;
    document.m_bComicView = false;
    SetChatDoc(&document);

    {
        CTextView textView(&document);
        document.m_textView = &textView;
        REQUIRE(textView.m_textCore.dwGetTextViewBufferMaxSize() == 256000);

        const QString nick = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
        const QString message = originalTextViewResourceString(
            QStringLiteral("IDS_NOKICKREASON_INFO"));
        REQUIRE(!nick.isEmpty() && !message.isEmpty());
        CUserInfo sender(nick);
        const QByteArray messageBytes = message.toUtf8();

        textView.TextLine(&sender, nullptr, nullptr, messageBytes.constData(),
                          BM_SAY, 0, nullptr, -1);
        const QString normalHeader = resourceFormat(
            QStringLiteral("IDS_NORMAL_HEADER"),
            {originalTextViewResourceString(QStringLiteral("IDS_PART_HEADER")),
             sender.GetScreenName()});
        REQUIRE(textView.m_pRichEdit->toPlainText()
                == normalHeader + QLatin1Char('\n') + message);

        const QString beforePose = textView.m_pRichEdit->toPlainText();
        textView.TextLine(&sender, nullptr, nullptr, "<Chr>", BM_SAY,
                          1, nullptr, -1);
        REQUIRE(textView.m_pRichEdit->toPlainText() == beforePose);

        textView.ClearTextView();
        ShowSay(&document, &sender, message, nullptr, 0, BM_THINK);
        const QString thoughtHeader = resourceFormat(
            QStringLiteral("IDS_THOUGHT_HEADER"),
            {originalTextViewResourceString(QStringLiteral("IDS_PART_HEADER")),
             sender.GetScreenName()});
        REQUIRE(textView.m_pRichEdit->toPlainText()
                == thoughtHeader + QLatin1Char('\n') + message);

        textView.ClearTextView();
        const QString controlFull = QChar(chCtlBold) + message;
        OnTextMsg(&document, nick, QString(), controlFull, MT_CHANNELSEND);
        REQUIRE(textView.m_pRichEdit->toPlainText()
                == normalHeader + QLatin1Char('\n') + message);
        QTextCursor firstMessageCharacter(textView.m_pRichEdit->document());
        firstMessageCharacter.setPosition(normalHeader.size() + 1);
        firstMessageCharacter.movePosition(QTextCursor::NextCharacter,
                                           QTextCursor::KeepAnchor);
        REQUIRE(firstMessageCharacter.charFormat().fontWeight() == QFont::Bold);
    }

    g_mapNickToPtr->clear();
    document.DestroyUserState();

    {
        CStatusView statusView(&document);
        document.m_textView = &statusView;
        document.m_bStatusView = true;
        REQUIRE(statusView.m_textCore.nGetInsertBlank() == TEXT_VIEW_BLANK_NEVER);
        REQUIRE(statusView.m_textCore.dwGetTextViewBufferMaxSize() == 256000);

        const QString nick = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK"));
        const QString away = originalResourceString(
            QStringLiteral("IDS_DFLTAWAYMSG"));
        QString awayReport = originalResourceString(
            QStringLiteral("IDS_AWAYREPORT"));
        awayReport.replace(QStringLiteral("%1"), nick);
        awayReport.replace(QStringLiteral("%2"), away);
        serverConn.ProcessMessage(
            QStringLiteral(":%1 301 %2 %2 :%3")
                .arg(originalResourceString(QStringLiteral("IDS_DEFAULT_SERVER")),
                     nick, away));
        REQUIRE(statusView.m_pRichEdit->toPlainText() == awayReport);

        serverConn.ProcessMessage(
            QStringLiteral(":%1 305 %2 :%3")
                .arg(originalResourceString(QStringLiteral("IDS_DEFAULT_SERVER")),
                     nick, away));
        REQUIRE(statusView.m_pRichEdit->toPlainText()
                == awayReport + QStringLiteral("\n\n") + away);

        QTextCursor lastStatusCharacter(statusView.m_pRichEdit->document());
        lastStatusCharacter.movePosition(QTextCursor::End);
        lastStatusCharacter.movePosition(QTextCursor::PreviousCharacter,
                                         QTextCursor::KeepAnchor);
        REQUIRE(lastStatusCharacter.charFormat().foreground().color()
                == QColor(0, 128, 128));
    }
    document.m_textView = nullptr;
    document.m_bStatusView = false;

    {
        CChatDoc viewDocument;
        viewDocument.m_bComicView = false;
        CChatView chatView(&viewDocument);
        REQUIRE(dynamic_cast<CTextView*>(chatView.GetPrimaryView()) != nullptr);
        chatView.CreateStatusView();
        REQUIRE(dynamic_cast<CStatusView*>(chatView.GetPrimaryView()) != nullptr);
    }

    SetChatDoc(nullptr);
    return EXIT_SUCCESS;
}
