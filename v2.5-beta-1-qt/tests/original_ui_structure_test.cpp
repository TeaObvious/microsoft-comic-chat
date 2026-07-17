#include "avatar.h"
#include "backdrop.h"
#include "bodycam.h"
#include "chat.h"
#include "chatdoc.h"
#include "chatview.h"
#include "childfrm.h"
#include "colordlg.h"
#include "doskey.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "originalassets.h"
#include "panel.h"
#include "saywnd.h"
#include "spltchat.h"
#include "status.h"
#include "tabbar.h"
#include "avatario.h"

#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QMdiArea>
#include <QDebug>
#include <QImage>
#include <QKeyEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QTextCursor>

#include <cmath>
#include <cstdlib>

namespace {
bool nearPercent(int part, int total, int expected)
{
    return total > 0 && std::abs(part * 100 - total * expected) <= total * 2;
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    theApp.InitializeFonts();

    if (originalDialogCaption(QStringLiteral("IDD_CHOOSECOLOR"))
            != QStringLiteral("Choose Color")) {
        return EXIT_FAILURE;
    }

    {
        CColorDlg colorDialog(static_cast<LONG>(RGB(255, 0, 0)));
        COLORREF selected = 0;
        if (!colorDialog.GetSelectedColorRGB(&selected)
            || selected != RGB(255, 0, 0)) {
            return EXIT_FAILURE;
        }
    }

    {
        CDosKey doskey;
        for (int index = 0; index < 65; ++index)
            doskey.bAppendEntry(QString::number(index), nullptr);
        CDWordArray* formatting = reinterpret_cast<CDWordArray*>(quintptr(1));
        if (doskey.StrGetPrevEntry(&formatting) != QStringLiteral("64")
            || formatting != nullptr) {
            return EXIT_FAILURE;
        }
        QString oldest;
        for (int index = 0; index < 63; ++index)
            oldest = doskey.StrGetPrevEntry(&formatting);
        if (oldest != QStringLiteral("1")) return EXIT_FAILURE;
    }

    {
        CSplitChatV splitter;
        splitter.addWidget(new QWidget(&splitter));
        splitter.addWidget(new QWidget(&splitter));
        splitter.resize(1000, 400);
        splitter.show();
        application.processEvents();
        const QList<int> sizes = splitter.sizes();
        const int total = sizes.value(0) + sizes.value(1);
        if (!nearPercent(sizes.value(0), total, 80)) return EXIT_FAILURE;
    }

    {
        CSplitChat splitter;
        splitter.addWidget(new QWidget(&splitter));
        splitter.addWidget(new QWidget(&splitter));
        splitter.resize(400, 1000);
        splitter.show();
        application.processEvents();
        const QList<int> sizes = splitter.sizes();
        const int total = sizes.value(0) + sizes.value(1);
        if (!nearPercent(sizes.value(1), total, 70)) return EXIT_FAILURE;
    }

    {
        CFixedSplitter splitter;
        splitter.addWidget(new QWidget(&splitter));
        splitter.addWidget(new QWidget(&splitter));
        splitter.resize(400, 500);
        splitter.show();
        application.processEvents();
        const QList<int> sizes = splitter.sizes();
        if (sizes.size() != 2 || sizes[1] != splitter.SayMinimumPixels()) {
            qWarning() << "fixed splitter" << sizes << splitter.SayMinimumPixels();
            return EXIT_FAILURE;
        }
    }

    {
        CSayWnd::SetDefaultButtons(SB_SAY | SB_THINK | SB_WHISPER | SB_ACTION | SB_SOUND);
        CSayWnd sayWindow;
        sayWindow.resize(400, 40);
        sayWindow.show();
        application.processEvents();
        CSayToolBar* bar = sayWindow.GetSayBar();
        if (!bar || sayWindow.m_cntBalloons != 5 || sayWindow.m_cxSayBar != 120
            || bar->actions().size() != 5
            || bar->actions()[0]->data().toString() != QStringLiteral("ID_ACTIONS_SAY")
            || bar->actions()[1]->data().toString() != QStringLiteral("ID_ACTIONS_THINK")
            || bar->actions()[2]->data().toString() != QStringLiteral("ID_ACTIONS_WHISPER")
            || bar->actions()[3]->data().toString() != QStringLiteral("ID_SEND_ACTION")
            || bar->actions()[4]->data().toString() != QStringLiteral("ID_PLAY_SOUND")
            || bar->actions()[4]->isEnabled()
            || sayWindow.GetSayEdit()->font().pixelSize() != qAbs(nFontHeight)
            || sayWindow.GetSayEdit()->geometry() != QRect(0, 0, 280, 40)
            || bar->geometry() != QRect(274, -3, 132, 29)) {
            qWarning() << "saybar" << sayWindow.m_cntBalloons << sayWindow.m_cxSayBar
                       << (bar ? bar->geometry() : QRect())
                       << sayWindow.GetSayEdit()->geometry();
            return EXIT_FAILURE;
        }

        const QImage sourceSayStrip(originalFileResourcePath(
            QStringLiteral("IDB_SAY_BAR"), QStringLiteral("BITMAP")));
        if (sourceSayStrip.height() != 17 || sourceSayStrip.width() < 4 * 17) {
            qWarning() << "say icon source dimensions" << sourceSayStrip.size();
            return EXIT_FAILURE;
        }
        for (int button = 0; button < 4; ++button) {
            const QImage icon = bar->actions()[button]->icon()
                .pixmap(QSize(17, 17), QIcon::Normal, QIcon::Off).toImage();
            if (icon.size() != QSize(17, 17)) return EXIT_FAILURE;
            int transparent = 0;
            int opaque = 0;
            for (int y = 0; y < 17; ++y) {
                for (int x = 0; x < 17; ++x) {
                    const bool sourceGray = sourceSayStrip.pixelColor(
                        button * 17 + x, y).rgb() == qRgb(192, 192, 192);
                    const int alpha = icon.pixelColor(x, y).alpha();
                    if ((sourceGray && alpha != 0)
                        || (!sourceGray && alpha == 0)) {
                        qWarning() << "say icon mask" << button << x << y
                                   << sourceGray << alpha;
                        return EXIT_FAILURE;
                    }
                    if (alpha == 0) ++transparent;
                    else ++opaque;
                }
            }
            if (transparent == 0 || opaque == 0) return EXIT_FAILURE;
        }

        CSayCtrl* edit = sayWindow.GetSayEdit();
        edit->setPlainText(QStringLiteral("AB"));
        QTextCursor cursor(edit->document());
        cursor.setPosition(0);
        cursor.setPosition(1, QTextCursor::KeepAnchor);
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        cursor.mergeCharFormat(bold);
        CDWordArray* formatting = PRGDWGetFormatting(
            edit, &sayWindow.m_fontText, edit->m_crTextColor);
        if (!formatting || formatting->GetSize() != 2
            || formatting->GetAt(0) != MAKELONG(wBold, 0)
            || formatting->GetAt(1) != MAKELONG(0, 1)) {
            qWarning() << "say formatting"
                       << (formatting ? formatting->GetSize() : -1);
            FreeAndNullFormatting(&formatting);
            return EXIT_FAILURE;
        }
        FreeAndNullFormatting(&formatting);
    }

    {
        CSayWnd::SetDefaultButtons(0);
        CSayWnd statusInput;
        statusInput.resize(400, 40);
        statusInput.show();
        application.processEvents();
        if (statusInput.GetSayBar() || statusInput.m_cntBalloons != 0
            || statusInput.m_cxSayBar != 0
            || statusInput.GetSayEdit()->geometry() != QRect(0, 0, 400, 40)) {
            return EXIT_FAILURE;
        }
    }

    theApp.InitVals();
    theApp.InitializeFonts();
    CUnitPanelPage::SetFonts(theApp.m_comicsFont, theApp.m_comicsColor);
    InitializeBackDrops();
    InitializeAvatars();
    LoadEmotionStrings();

    {
        CChatDoc forwardingDocument;
        SetChatDoc(&forwardingDocument);
        CSplitSay splitter;
        splitter.addWidget(new QWidget(&splitter));
        auto* sayWindow = new CSayWnd(&splitter);
        splitter.addWidget(sayWindow);
        forwardingDocument.m_sayWnd = sayWindow;
        splitter.resize(400, 200);
        splitter.show();
        application.processEvents();

        const QString sourceCharacter = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK")).left(1);
        if (sourceCharacter.isEmpty()) return EXIT_FAILURE;
        QKeyEvent splitterCharacter(QEvent::KeyPress, 0, Qt::NoModifier,
                                    sourceCharacter);
        splitterCharacter.ignore();
        QApplication::sendEvent(&splitter, &splitterCharacter);
        if (sayWindow->GetSayEdit()->toPlainText() != sourceCharacter) {
            qWarning() << "splitter character forwarding"
                       << sayWindow->GetSayEdit()->toPlainText();
            return EXIT_FAILURE;
        }

        sayWindow->GetSayEdit()->clear();
        CTabBarTabCtrl tabControl;
        QKeyEvent tabCharacter(QEvent::KeyPress, 0, Qt::NoModifier,
                               sourceCharacter);
        tabCharacter.ignore();
        QApplication::sendEvent(&tabControl, &tabCharacter);
        if (sayWindow->GetSayEdit()->toPlainText() != sourceCharacter) {
            qWarning() << "tab character forwarding"
                       << sayWindow->GetSayEdit()->toPlainText();
            return EXIT_FAILURE;
        }

        forwardingDocument.m_sayWnd = nullptr;
        SetChatDoc(nullptr);
    }

    CChatDoc roomB;
    CChatDoc status;
    CChatDoc roomA;
    status.m_bStatusView = true;
    CTabBar tabBar;
    const QString roomTitle = originalResourceString(
        QStringLiteral("IDR_MAINFRAME")).section(QLatin1Char('\n'), 1, 1);
    tabBar.AddMDITab(originalResourceString(QStringLiteral("IDS_STATUSTITLE")), &status, false);
    tabBar.AddMDITab(roomTitle + QStringLiteral("2"), &roomB, false);
    tabBar.AddMDITab(roomTitle + QStringLiteral("1"), &roomA, false);
    if (tabBar.height() != 29
        || tabBar.GetTabString(0) != originalResourceString(QStringLiteral("IDS_STATUSTITLE"))
        || tabBar.GetTabString(1) != roomTitle + QStringLiteral("1")
        || tabBar.GetTabString(2) != roomTitle + QStringLiteral("2")
        || tabBar.GetTabDoc(0) != &status) {
        qWarning() << "tabbar" << tabBar.height() << tabBar.GetTabString(0)
                   << tabBar.GetTabString(1) << tabBar.GetTabString(2);
        return EXIT_FAILURE;
    }

    const QImage sourceTabStrip(originalFileResourcePath(
        QStringLiteral("IDB_TABS"), QStringLiteral("BITMAP")));
    const QImage roomTabIcon = tabBar.TabControl()->tabIcon(1)
        .pixmap(QSize(16, 16)).toImage();
    if (sourceTabStrip.size() != QSize(64, 16)
        || roomTabIcon.size() != QSize(16, 16)) {
        qWarning() << "tab icon dimensions" << sourceTabStrip.size()
                   << roomTabIcon.size();
        return EXIT_FAILURE;
    }
    int transparentPixels = 0;
    int opaquePixels = 0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool sourceGreen = sourceTabStrip.pixelColor(x, y).rgb()
                == qRgb(0, 255, 0);
            const int alpha = roomTabIcon.pixelColor(x, y).alpha();
            if ((sourceGreen && alpha != 0) || (!sourceGreen && alpha == 0)) {
                qWarning() << "tab icon mask" << x << y << sourceGreen << alpha;
                return EXIT_FAILURE;
            }
            if (alpha == 0) ++transparentPixels;
            else ++opaquePixels;
        }
    }
    if (transparentPixels == 0 || opaquePixels == 0) return EXIT_FAILURE;

    CChatDoc document;
    SetChatDoc(&document);
    CMainFrame frame(&document);

    if (!frame.GetMDIArea()
        || frame.GetMDIArea()->subWindowList().size() != 1
        || frame.GetActiveDocument() != &document
        || document.GetTitle() != roomTitle + QStringLiteral("1")) {
        qWarning() << "mdi initial" << frame.GetActiveDocument()
                   << document.GetTitle();
        return EXIT_FAILURE;
    }

    CChatDoc* statusDocument = frame.CreateStatusWindow();
    if (!statusDocument || !statusDocument->m_bStatusView
        || !statusDocument->m_proto
        || statusDocument->m_proto->m_strChannel
               != QString::fromLatin1(STATUS_WINDOW_NAME)
        || !GetStatusView()
        || frame.GetTabBar()->FindTabNum(statusDocument) >= 0) {
        qWarning() << "mdi status";
        return EXIT_FAILURE;
    }
    frame.ShowStatusWindow(true);
    application.processEvents();
    if (!(theApp.m_flags0 & F0_SHOWSTATUSWINDOW)
        || frame.GetActiveDocument() != statusDocument
        || frame.GetTabBar()->FindTabNum(statusDocument) < 0) {
        qWarning() << "mdi status show";
        return EXIT_FAILURE;
    }
    frame.ShowStatusWindow(false);
    application.processEvents();
    if (theApp.m_flags0 & F0_SHOWSTATUSWINDOW
        || frame.GetActiveDocument() != &document
        || frame.GetTabBar()->FindTabNum(statusDocument) >= 0) {
        qWarning() << "mdi status hide" << theApp.m_flags0
                   << frame.GetActiveDocument() << &document
                   << frame.GetTabBar()->FindTabNum(statusDocument);
        return EXIT_FAILURE;
    }

    CChatDoc* secondDocument = frame.CreateNewDocument();
    application.processEvents();
    if (!secondDocument
        || secondDocument->GetTitle() != roomTitle + QStringLiteral("2")
        || frame.GetMDIArea()->subWindowList().size() != 3
        || frame.GetActiveDocument() != secondDocument) {
        qWarning() << "mdi second";
        return EXIT_FAILURE;
    }

    const QString mdiFrameTitle = originalResourceString(
        QStringLiteral("AFX_IDS_APP_TITLE")) + QStringLiteral(" - [")
        + secondDocument->GetTitle() + QLatin1Char(']');
    if (frame.windowTitle() != mdiFrameTitle
        || frame.menuBar()->actions().size() != 9) {
        qWarning() << "frame" << frame.windowTitle() << frame.menuBar()->actions().size();
        return EXIT_FAILURE;
    }
    const QList<OriginalMenuItem> sourceMenu = originalMenuResource(QStringLiteral("IDR_MAINFRAME"));
    for (int index = 0; index < sourceMenu.size(); ++index) {
        if (frame.menuBar()->actions()[index]->text() != sourceMenu[index].text)
        {
            qWarning() << "menu" << index << frame.menuBar()->actions()[index]->text()
                       << sourceMenu[index].text;
            return EXIT_FAILURE;
        }
    }

    bool foundMain = false;
    bool foundMember = false;
    bool foundText = false;
    bool foundTab = false;
    for (QToolBar* toolbar : frame.findChildren<QToolBar*>()) {
        if (toolbar->windowTitle() == originalMenuItemText(QStringLiteral("ID_VIEW_TOOLBAR_MAIN")).remove(QLatin1Char('&'))) {
            foundMain = toolbar->actions().size() == 13;
        } else if (toolbar->windowTitle() == originalMenuItemText(QStringLiteral("ID_VIEW_TOOLBAR_MEMBER")).remove(QLatin1Char('&'))) {
            foundMember = toolbar->actions().size() == 8;
        } else if (toolbar->windowTitle() == originalMenuItemText(QStringLiteral("ID_VIEW_TOOLBAR_TEXT")).remove(QLatin1Char('&'))) {
            foundText = toolbar->actions().size() == 7;
        } else if (toolbar->windowTitle() == originalResourceString(QStringLiteral("IDS_TABTITLE"))) {
            foundTab = toolbar->height() == 29;
        }
    }
    if (!foundMain || !foundMember || !foundText || !foundTab) {
        qWarning() << "toolbars" << foundMain << foundMember << foundText << foundTab;
        return EXIT_FAILURE;
    }

    bool memberPaneWidthFound = false;
    for (QLabel* label : frame.statusBar()->findChildren<QLabel*>()) {
        if (label->width() == originalResourceString(
                QStringLiteral("IDS_MEMBER_COUNT_WIDTH")).toInt()) {
            memberPaneWidthFound = true;
        }
    }
    if (!memberPaneWidthFound) qWarning() << "status width";
    return memberPaneWidthFound ? EXIT_SUCCESS : EXIT_FAILURE;
}
