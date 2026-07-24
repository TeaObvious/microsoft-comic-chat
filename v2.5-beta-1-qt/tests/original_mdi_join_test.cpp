#include "avatar.h"
#include "backdrop.h"
#include "chat.h"
#include "chatdoc.h"
#include "childfrm.h"
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
#include <QLabel>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QStatusBar>

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
        const DWORD savedFlags1 = theApp.m_flags1;
        theApp.m_flags1 &= ~DWORD(F1_MAXMDI);
        CMainFrame hiddenFrame;
        theApp.m_pMainWnd = &hiddenFrame;
        CChatDoc* firstDocument = hiddenFrame.CreateNewDocument();
        REQUIRE(firstDocument != nullptr);
        application.processEvents();
        REQUIRE(hiddenFrame.GetMDIArea()->activeSubWindow() != nullptr);
        REQUIRE(hiddenFrame.GetMDIArea()->activeSubWindow()->isMaximized());
        theApp.m_pMainWnd = nullptr;
        theApp.m_flags1 = savedFlags1;
    }

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
        frame.show();
        application.processEvents();
        CChildFrame* statusChild = nullptr;
        for (QMdiSubWindow* window :
             frame.GetMDIArea()->subWindowList()) {
            auto* child = dynamic_cast<CChildFrame*>(window);
            if (child && child->GetDocument() == statusDocument) {
                statusChild = child;
                break;
            }
        }
        REQUIRE(statusChild != nullptr);
        CStatusView* statusView = GetStatusView();
        REQUIRE(statusView != nullptr);
        REQUIRE(statusView->m_pRichEdit != nullptr);
        const QString statusBefore =
            statusView->m_pRichEdit->toPlainText();
        CIrcPrint uninitializedPrint;
        uninitializedPrint.m_iType = PT_NOTINIT;
        const QString uninitializedLine =
            QStringLiteral("source-backed PT_NOTINIT line");
        AddToStatus(uninitializedPrint, uninitializedLine);
        const QString statusAfter =
            statusView->m_pRichEdit->toPlainText();
        REQUIRE(statusAfter.size() > statusBefore.size());
        REQUIRE(statusAfter.endsWith(uninitializedLine));

        statusChild->show();
        application.processEvents();
        REQUIRE(statusChild->isHidden());
        REQUIRE(frame.GetTabBar()->FindTabNum(statusDocument) < 0);

        CIrcProto* defaultProtocol = GetIrcProto();
        REQUIRE(defaultProtocol != nullptr);
        const ConnectionStatus savedDefaultStatus =
            defaultProtocol->GetConnectionStatus();
        const bool savedDisableMotd = theApp.m_bDisableMOTD;
        const bool savedInSearch = theApp.m_bInSearch;
        const bool savedAway = theApp.m_bAway;
        defaultProtocol->SetConnectionStatus(CX_DISCONNECTED);
        REQUIRE(theApp.OnUpdateSessionConnect());
        REQUIRE(!theApp.OnUpdateDisconnect());
        REQUIRE(!theApp.OnUpdateNewroom());
        defaultProtocol->SetConnectionStatus(CX_NOCHANNEL);
        REQUIRE(!theApp.OnUpdateSessionConnect());
        REQUIRE(theApp.OnUpdateDisconnect());
        REQUIRE(theApp.OnUpdateNewroom());
        theApp.m_bDisableMOTD = false;
        REQUIRE(theApp.OnUpdateMotd());
        theApp.m_bInSearch = false;
        REQUIRE(theApp.OnUpdateCanSearch());
        theApp.m_bInSearch = true;
        REQUIRE(!theApp.OnUpdateCanSearch());
        theApp.m_bAway = true;
        BOOL awayChecked = FALSE;
        REQUIRE(theApp.OnUpdateAwayToggle(&awayChecked));
        REQUIRE(awayChecked);
        defaultProtocol->SetConnectionStatus(savedDefaultStatus);
        theApp.m_bDisableMOTD = savedDisableMotd;
        theApp.m_bInSearch = savedInSearch;
        theApp.m_bAway = savedAway;

        BOOL checked = TRUE;
        REQUIRE(statusView != nullptr);
        REQUIRE(!statusView->OnUpdateViewComics(&checked));
        REQUIRE(!checked);
        checked = FALSE;
        REQUIRE(!statusView->OnUpdateViewText(&checked));
        REQUIRE(checked);

        checked = TRUE;
        REQUIRE(theApp.OnUpdateViewStatuswindow(&checked));
        REQUIRE(!checked);
        theApp.OnViewStatuswindow();
        application.processEvents();
        REQUIRE(theApp.m_flags0 & F0_SHOWSTATUSWINDOW);
        REQUIRE(frame.GetActiveDocument() == statusDocument);
        REQUIRE(frame.GetMDIArea()->activeSubWindow() != nullptr);
        statusChild->hide();
        application.processEvents();
        REQUIRE(!statusChild->isHidden());
        REQUIRE(frame.GetTabBar()->FindTabNum(statusDocument) >= 0);
        theApp.OnViewStatuswindow();
        application.processEvents();
        REQUIRE(!(theApp.m_flags0 & F0_SHOWSTATUSWINDOW));
        REQUIRE(frame.GetActiveDocument() == nullptr);
        REQUIRE(frame.GetMDIArea()->activeSubWindow() == nullptr);
        REQUIRE(!frame.GetMDIArea()->signalsBlocked());

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

        const DWORD savedFlags0 = theApp.m_flags0;
        theApp.m_flags0 &= ~DWORD(
            F0_AUTOARRANGEWNDS | F0_AUTOARRANGEISVERT);
        roomDocument->SetFocusToSayWnd();
        application.processEvents();
        QWidget* focusBefore = QApplication::focusWidget();
        REQUIRE(focusBefore != nullptr);
        frame.ShowStatusWindow(true, false);
        application.processEvents();
        REQUIRE(frame.GetActiveDocument() == roomDocument);
        REQUIRE(QApplication::focusWidget() == focusBefore);
        REQUIRE(theApp.m_flags0 & F0_SHOWSTATUSWINDOW);

        QList<QMdiSubWindow*> visibleWindows;
        for (QMdiSubWindow* window :
             frame.GetMDIArea()->subWindowList()) {
            if (!window->isHidden()) visibleWindows.append(window);
        }
        REQUIRE(visibleWindows.size() == 2);
        visibleWindows[0]->setGeometry(20, 30, 260, 210);
        visibleWindows[1]->setGeometry(330, 70, 310, 250);
        application.processEvents();
        const QRect firstBefore = visibleWindows[0]->geometry();
        const QRect secondBefore = visibleWindows[1]->geometry();

        theApp.m_flags0 |=
            F0_AUTOARRANGEWNDS | F0_AUTOARRANGEISVERT;
        frame.AutoArrangeWindows();
        REQUIRE(visibleWindows[0]->geometry() == firstBefore);
        REQUIRE(visibleWindows[1]->geometry() == secondBefore);
        theApp.m_flags0 &= ~DWORD(F0_AUTOARRANGEISVERT);
        application.processEvents();
        const int viewportHeight =
            frame.GetMDIArea()->viewport()->height();
        REQUIRE(visibleWindows[0]->geometry().height()
                == viewportHeight);
        REQUIRE(visibleWindows[1]->geometry().height()
                == viewportHeight);

        theApp.m_flags0 &= ~DWORD(F0_AUTOARRANGEWNDS);
        theApp.OnViewStatuswindow();
        application.processEvents();
        REQUIRE(frame.GetActiveDocument() == roomDocument);
        REQUIRE(!(theApp.m_flags0 & F0_SHOWSTATUSWINDOW));
        theApp.m_flags0 = savedFlags0;

        BOOL toolbarChecked = FALSE;
        REQUIRE(theApp.OnUpdateViewToolBar(
            ID_VIEW_TOOLBAR_MAIN, &toolbarChecked));
        const BOOL toolbarWasChecked = toolbarChecked;
        REQUIRE(theApp.OnViewToolBar(ID_VIEW_TOOLBAR_MAIN));
        REQUIRE(theApp.OnUpdateViewToolBar(
            ID_VIEW_TOOLBAR_MAIN, &toolbarChecked));
        REQUIRE(toolbarChecked != toolbarWasChecked);
        REQUIRE(theApp.OnViewToolBar(ID_VIEW_TOOLBAR_MAIN));
        REQUIRE(!theApp.OnViewToolBar(UINT(-1)));

        BOOL tabbarChecked = FALSE;
        REQUIRE(theApp.OnUpdateViewTabbar(&tabbarChecked));
        const BOOL tabbarWasChecked = tabbarChecked;
        theApp.OnViewTabbar();
        application.processEvents();
        REQUIRE(theApp.OnUpdateViewTabbar(&tabbarChecked));
        REQUIRE(tabbarChecked != tabbarWasChecked);
        theApp.OnViewTabbar();

        const QString savedConnectedService =
            theApp.m_strConnectedService;
        const QString savedConnectedServer =
            theApp.m_strConnectedServer;
        const ConnectionStatus savedRoomStatus =
            roomDocument->m_proto->GetConnectionStatus();
        theApp.m_strConnectedService =
            QStringLiteral("//Test Network/logical.example");
        theApp.m_strConnectedServer =
            QStringLiteral("physical.example");
        const QString prettyServer =
            QStringLiteral("physical.example (Test Network)");

        QString expectedConnected = originalResourceString(
            QStringLiteral("ID_CONNECTED"));
        expectedConnected.replace(
            QStringLiteral("%1"),
            roomDocument->m_proto->m_strPrettyChannel);
        expectedConnected.replace(QStringLiteral("%2"), prettyServer);
        roomDocument->m_proto->SetConnectionStatus(CX_INCHANNEL);
        roomDocument->m_proto->UpdateStatus();
        application.processEvents();
        REQUIRE(roomDocument->m_strStatus == expectedConnected);
        bool statusPaneMatches = false;
        for (QLabel* label :
             frame.statusBar()->findChildren<QLabel*>()) {
            if (label->text() == expectedConnected)
                statusPaneMatches = true;
        }
        REQUIRE(statusPaneMatches);

        QString expectedNoChannel = originalResourceString(
            QStringLiteral("ID_NOCHANNEL"));
        expectedNoChannel.replace(
            QStringLiteral("%1"), prettyServer);
        roomDocument->m_proto->SetConnectionStatus(CX_NOCHANNEL);
        roomDocument->m_proto->UpdateStatus();
        application.processEvents();
        REQUIRE(roomDocument->m_strStatus == expectedNoChannel);
        statusPaneMatches = false;
        for (QLabel* label :
             frame.statusBar()->findChildren<QLabel*>()) {
            if (label->text() == expectedNoChannel)
                statusPaneMatches = true;
        }
        REQUIRE(statusPaneMatches);

        theApp.m_strConnectedService = savedConnectedService;
        theApp.m_strConnectedServer = savedConnectedServer;
        roomDocument->m_proto->SetConnectionStatus(savedRoomStatus);
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
