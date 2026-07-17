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
        const QString user = QString::fromUtf8(GetMyUserName());
        serverConn.ProcessMessage(
            QStringLiteral(":%1!%2@NoMachine JOIN :%3")
                .arg(nick, user, channel));
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

        QString sourceAvatar;
        GetNextAvatarName(sourceAvatar);
        REQUIRE(!sourceAvatar.isEmpty());
        serverConn.ProcessMessage(
            QStringLiteral("353 %1 = %2 :%1 %3")
                .arg(nick, channel, sourceAvatar));
        serverConn.ProcessMessage(
            QStringLiteral("366 %1 %2").arg(nick, channel));
        application.processEvents();
        REQUIRE(roomDocument->m_view != nullptr);
        REQUIRE(roomDocument->m_view->viewport()->size().width() > 0);
        REQUIRE(roomDocument->m_view->viewport()->size().height() > 0);
        QImage initialComic(roomDocument->m_view->viewport()->size(),
                            QImage::Format_RGB32);
        initialComic.fill(Qt::white);
        roomDocument->m_view->viewport()->render(&initialComic);
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
