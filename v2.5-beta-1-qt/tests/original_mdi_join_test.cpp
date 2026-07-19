#include "avatar.h"
#include "backdrop.h"
#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "ircsock.h"
#include "mainfrm.h"
#include "originalassets.h"
#include "panel.h"
#include "pageview.h"
#include "setupdlg.h"
#include "status.h"
#include "tabbar.h"

#include <QApplication>
#include <QImage>

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.InitializeFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont,
                                     theApp.m_comicsColor));
    InitializeBackDrops();
    InitializeAvatars();
    REQUIRE(CommunicationInits());

    {
        CMainFrame frame;
        theApp.m_pMainWnd = &frame;
        frame.resize(1000, 760);
        CChatDoc* statusDocument = frame.CreateStatusWindow();
        REQUIRE(statusDocument != nullptr);
        REQUIRE(statusDocument->m_bStatusView);
        REQUIRE(statusDocument->m_proto->m_strChannel
                == QString::fromLatin1(STATUS_WINDOW_NAME));
        REQUIRE(frame.GetTabBar()->FindTabNum(statusDocument) < 0);
        REQUIRE(g_docs.size() == 1);

        const QString nick = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK"));
        const QString channel = originalResourceString(
            QStringLiteral("IDS_DEFAULT_CHANNEL"));
        const QString server = originalResourceString(
            QStringLiteral("IDS_DEFAULT_SERVER"));
        const QString user = QString::fromUtf8(GetMyUserName());
        serverConn.ProcessMessage(
            QStringLiteral(":%1!%2@%3 JOIN :%4")
                .arg(nick, user, server, channel));
        application.processEvents();

        CChatDoc* roomDocument = LookupDoc(channel);
        REQUIRE(roomDocument != nullptr);
        REQUIRE(roomDocument != statusDocument);
        REQUIRE(g_docs.size() == 2);
        REQUIRE(frame.GetActiveDocument() == roomDocument);
        REQUIRE(frame.GetTabBar()->FindTabNum(roomDocument) >= 0);
        REQUIRE(roomDocument->GetTitle() == DecodeChan(channel));
        REQUIRE(roomDocument->GetConnectionStatus() == CX_INCHANNEL);
        REQUIRE(roomDocument->m_proto != GetIrcProto());
        REQUIRE(serverConn.m_queries.FindQuery(ctNames) != nullptr);
        REQUIRE(serverConn.m_queries.FindQuery(ctTopic) != nullptr);
        REQUIRE(serverConn.m_queries.FindQuery(ctGetChannelMode) != nullptr);
        REQUIRE(serverConn.m_queries.FindQuery(ctWho) != nullptr);
        REQUIRE(roomDocument->m_puiSelf == nullptr);

        frame.show();
        application.processEvents();

        REQUIRE(roomDocument->m_pages.size() == 1);
        CPage* titlePage = roomDocument->m_pages.first();
        REQUIRE(titlePage != nullptr);
        REQUIRE(titlePage->m_panels.size() == 1);
        CPanel* titlePanel = titlePage->m_panels.first();
        REQUIRE(titlePanel != nullptr);
        REQUIRE(titlePanel->m_elements.size() == 2);
        REQUIRE(roomDocument->m_view != nullptr);
        REQUIRE(roomDocument->m_view->viewport()->size().width() > 0);
        REQUIRE(roomDocument->m_view->viewport()->size().height() > 0);
        QImage beforeNames(roomDocument->m_view->viewport()->size(),
                           QImage::Format_RGB32);
        beforeNames.fill(Qt::white);
        roomDocument->m_view->viewport()->render(&beforeNames);

        QString sourceAvatar;
        GetNextAvatarName(sourceAvatar);
        REQUIRE(!sourceAvatar.isEmpty());
        serverConn.ProcessMessage(
            QStringLiteral(":%1 353 %2 = %3 :%2 %4")
                .arg(server, nick, channel, sourceAvatar));
        application.processEvents();
        REQUIRE(roomDocument->m_pages.size() == 1);
        REQUIRE(roomDocument->m_pages.first() == titlePage);
        REQUIRE(titlePage->m_panels.size() == 1);
        REQUIRE(titlePage->m_panels.first() == titlePanel);
        REQUIRE(titlePanel->m_elements.size() == 2);
        REQUIRE(roomDocument->m_puiSelf != nullptr);
        REQUIRE(roomDocument->m_mapNickToPtr.size() == 2);

        serverConn.ProcessMessage(
            QStringLiteral(":%1 366 %2 %3").arg(server, nick, channel));
        application.processEvents();
        REQUIRE(roomDocument->m_pages.size() == 1);
        REQUIRE(roomDocument->m_pages.first() == titlePage);
        REQUIRE(titlePage->m_panels.size() == 1);
        REQUIRE(titlePage->m_panels.first() == titlePanel);
        REQUIRE(titlePanel->m_elements.size() == 6);
        REQUIRE(dynamic_cast<CBodyUnary*>(titlePanel->m_elements[2]) != nullptr);
        REQUIRE(dynamic_cast<CStarLabel*>(titlePanel->m_elements[3]) != nullptr);
        REQUIRE(dynamic_cast<CBodyUnary*>(titlePanel->m_elements[4]) != nullptr);
        REQUIRE(dynamic_cast<CStarLabel*>(titlePanel->m_elements[5]) != nullptr);
        QImage initialComic(roomDocument->m_view->viewport()->size(),
                            QImage::Format_RGB32);
        initialComic.fill(Qt::white);
        roomDocument->m_view->viewport()->render(&initialComic);
        REQUIRE(initialComic != beforeNames);
        bool hasSourceDrawing = false;
        for (int y = 0; y < initialComic.height() && !hasSourceDrawing; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(
                initialComic.constScanLine(y));
            for (int x = 0; x < initialComic.width(); ++x) {
                if ((row[x] & 0x00ffffffU) != 0x00ffffffU) {
                    hasSourceDrawing = true;
                    break;
                }
            }
        }
        REQUIRE(hasSourceDrawing);
    }

    theApp.m_pMainWnd = nullptr;
    theApp.m_pDoc = nullptr;
    SetChatDoc(nullptr);
    CommunicationCleanup();
    CUnitPanelPage::DestroyFonts();
    DestroyAvatars();
    DestroyBackDropArt();
    return EXIT_SUCCESS;
}
