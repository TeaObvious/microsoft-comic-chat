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
#include "memblst.h"
#include "originalassets.h"
#include "panel.h"
#include "proppage.h"
#include "protsupp.h"
#include "saywnd.h"
#include "setupdlg.h"
#include "spltchat.h"
#include "status.h"
#include "tabbar.h"
#include "avatario.h"
#include "userinfo.h"

#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMdiArea>
#include <QDebug>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTextCursor>
#include <QTimer>

#include <cmath>
#include <cstdlib>

namespace {
bool nearPercent(int part, int total, int expected)
{
    return total > 0 && std::abs(part * 100 - total * expected) <= total * 2;
}

QMenu* menuWithDirectCommand(QMenuBar* menuBar, const QString& command)
{
    if (!menuBar) return nullptr;
    for (QAction* rootAction : menuBar->actions()) {
        QMenu* menu = rootAction->menu();
        if (!menu) continue;
        for (QAction* action : menu->actions()) {
            if (action->data().toString() == command) return menu;
        }
    }
    return nullptr;
}

QAction* directCommand(QMenu* menu, const QString& command)
{
    if (!menu) return nullptr;
    for (QAction* action : menu->actions()) {
        if (action->data().toString() == command) return action;
    }
    return nullptr;
}

QStringList directCommands(QMenu* menu)
{
    QStringList commands;
    if (!menu) return commands;
    for (QAction* action : menu->actions()) {
        const QString command = action->data().toString();
        if (!command.isEmpty()) commands.append(command);
    }
    return commands;
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

    SetArtDir("artpack1");
    {
        CChatDoc artResetDocument;
        artResetDocument.m_bComicView = false;
        SetChatDoc(&artResetDocument);
        if (!ArtDirsOK()
            || QFileInfo(theApp.GetAvatarDir()).fileName().compare(
                   QStringLiteral("comicart"), Qt::CaseInsensitive) != 0
            || theApp.GetAvatarDir() != theApp.GetBackDropDir()) {
            qWarning() << "document default ArtDir reset"
                       << theApp.GetAvatarDir() << theApp.GetBackDropDir();
            return EXIT_FAILURE;
        }
        SetChatDoc(nullptr);
    }

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
    theApp.m_pMainWnd = &frame;
    frame.show();
    application.processEvents();
    frame.UpdateMacroMenu();

    int comicFontActions = 0;
    for (QAction* action : frame.findChildren<QAction*>()) {
        if (action->data().toString() != QLatin1String("ID_SETFONT"))
            continue;
        ++comicFontActions;
        if (!action->isEnabled()) {
            qWarning() << "comic font command disabled";
            return EXIT_FAILURE;
        }
    }
    if (comicFontActions == 0) return EXIT_FAILURE;

    if (!frame.GetMDIArea()
        || frame.GetMDIArea()->subWindowList().size() != 1
        || frame.GetActiveDocument() != &document
        || document.GetTitle() != roomTitle + QStringLiteral("1")) {
        qWarning() << "mdi initial" << frame.GetActiveDocument()
                   << document.GetTitle();
        return EXIT_FAILURE;
    }

    {
        CBodyCam* bodyCam = document.m_bodyCam;
        auto* sayWindow = dynamic_cast<CSayWnd*>(document.m_sayWnd);
        if (!bodyCam || !sayWindow || !document.m_memberList)
            return EXIT_FAILURE;

        const QString sourceCharacter = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK")).left(1);
        sayWindow->GetSayEdit()->clear();
        bodyCam->setFocus();
        application.processEvents();
        QKeyEvent characterEvent(QEvent::KeyPress, 0, Qt::NoModifier,
                                 sourceCharacter);
        QApplication::sendEvent(bodyCam, &characterEvent);
        if (sayWindow->GetSayEdit()->toPlainText() != sourceCharacter
            || QApplication::focusWidget() != sayWindow->GetSayEdit()) {
            qWarning() << "BodyCam character forwarding"
                       << sayWindow->GetSayEdit()->toPlainText()
                       << QApplication::focusWidget();
            return EXIT_FAILURE;
        }

        bodyCam->setFocus();
        QKeyEvent tabEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(bodyCam, &tabEvent);
        application.processEvents();
        if (!frame.GetTabBar()->TabControl()->hasFocus()) {
            qWarning() << "BodyCam forward focus cycle"
                       << QApplication::focusWidget();
            return EXIT_FAILURE;
        }

        bodyCam->setFocus();
        QKeyEvent backtabEvent(QEvent::KeyPress, Qt::Key_Backtab,
                               Qt::ShiftModifier);
        QApplication::sendEvent(bodyCam, &backtabEvent);
        application.processEvents();
        if (!document.m_memberList->FocusWidget()->hasFocus()) {
            qWarning() << "BodyCam backward focus cycle"
                       << QApplication::focusWidget();
            return EXIT_FAILURE;
        }

        bool optionsSeen = false;
        bool characterPageSelected = false;
        QTimer::singleShot(0, [&] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = dynamic_cast<QDialog*>(widget);
                if (!dialog || dialog->windowTitle()
                        != originalResourceString(QStringLiteral("IDS_OPTIONS"))) {
                    continue;
                }
                optionsSeen = true;
                QTabWidget* tabs = dialog->findChild<QTabWidget*>();
                characterPageSelected = tabs
                    && dynamic_cast<CCharacterPage*>(tabs->currentWidget());
                dialog->reject();
            }
        });
        const QPointF doubleClickPoint(1.0, 1.0);
        QMouseEvent doubleClick(
            QEvent::MouseButtonDblClick, doubleClickPoint, doubleClickPoint,
            QPointF(bodyCam->mapToGlobal(QPoint(1, 1))), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bodyCam, &doubleClick);
        if (!optionsSeen || !characterPageSelected) {
            qWarning() << "BodyCam character-page double click"
                       << optionsSeen << characterPageSelected;
            return EXIT_FAILURE;
        }

        const ConnectionStatus savedStatus = document.GetConnectionStatus();
        document.m_proto->SetConnectionStatus(CX_CONNECTING);
        bool optionsWhileConnecting = false;
        QTimer::singleShot(0, [&] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = dynamic_cast<QDialog*>(widget);
                if (!dialog || dialog->windowTitle()
                        != originalResourceString(QStringLiteral("IDS_OPTIONS"))) {
                    continue;
                }
                optionsWhileConnecting = true;
                dialog->reject();
            }
        });
        QMouseEvent connectingDoubleClick(
            QEvent::MouseButtonDblClick, doubleClickPoint, doubleClickPoint,
            QPointF(bodyCam->mapToGlobal(QPoint(1, 1))), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bodyCam, &connectingDoubleClick);
        application.processEvents();
        document.m_proto->SetConnectionStatus(savedStatus);
        if (optionsWhileConnecting) {
            qWarning() << "BodyCam opened Character page while connecting";
            return EXIT_FAILURE;
        }
    }

    {
        CCharacterPage characterPage(&frame);
        CBodyCam* preview = nullptr;
        for (QWidget* child : characterPage.findChildren<QWidget*>()) {
            if ((preview = dynamic_cast<CBodyCam*>(child))) break;
        }
        QListWidget* avatarList = characterPage.findChild<QListWidget*>();
        if (!preview || !avatarList || preview->m_forcedDelete
            || !preview->m_avatar) {
            qWarning() << "Character-page BodyCam setup" << preview
                       << avatarList;
            return EXIT_FAILURE;
        }

        characterPage.show();
        application.processEvents();
        if (GetCharSelBodyCam() != preview
            || !RefreshBodyPreview(preview->m_avatar)) {
            qWarning() << "Character-page preview registration";
            return EXIT_FAILURE;
        }

        QContextMenuEvent previewContext(
            QContextMenuEvent::Mouse, QPoint(2, 2),
            preview->mapToGlobal(QPoint(2, 2)));
        QApplication::sendEvent(preview, &previewContext);
        application.processEvents();
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (qobject_cast<QMenu*>(widget) && widget->isVisible()) {
                qWarning() << "Character-page preview exposed a context menu";
                return EXIT_FAILURE;
            }
        }

        QString inactiveAvatar;
        const QFileInfoList artPackFiles = QDir(
            originalAssetDirectoryPath(QStringLiteral("artpack1")))
            .entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo& file : artPackFiles) {
            if (file.suffix().compare(QStringLiteral("avb"),
                                      Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString candidate = file.completeBaseName();
            const QString activeFile = originalFileInDirectoryPath(
                theApp.GetAvatarDir(), candidate + QStringLiteral(".avb"));
            if (activeFile.isEmpty() && !GetAvatar(candidate)) {
                inactiveAvatar = candidate;
                break;
            }
        }
        if (inactiveAvatar.isEmpty()) return EXIT_FAILURE;

        const QString previousAvatar = QString::fromUtf8(
            preview->m_avatar->OriginalName());
        QString displayName = inactiveAvatar;
        displayName[0] = displayName[0].toUpper();
        auto* unavailableItem = new QListWidgetItem(displayName, avatarList);
        unavailableItem->setData(Qt::UserRole, inactiveAvatar);
        QString shownMessage;
        QTimer::singleShot(0, [&] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* message = dynamic_cast<QMessageBox*>(widget);
                if (!message) continue;
                shownMessage = message->text();
                message->accept();
            }
        });
        avatarList->setCurrentItem(unavailableItem);
        QString expectedMessage = originalResourceString(
            QStringLiteral("IDS_INVALIDART"));
        expectedMessage.replace(QStringLiteral("%1"), displayName);
        if (shownMessage != expectedMessage
            || !avatarList->currentItem()
            || avatarList->currentItem()->data(Qt::UserRole).toString().compare(
                   previousAvatar, Qt::CaseInsensitive) != 0
            || QString::fromUtf8(preview->m_avatar->OriginalName()).compare(
                   previousAvatar, Qt::CaseInsensitive) != 0) {
            qWarning() << "Character-page invalid art recovery"
                       << shownMessage << expectedMessage;
            return EXIT_FAILURE;
        }
        {
            const QSignalBlocker blocker(avatarList);
            delete avatarList->takeItem(avatarList->row(unavailableItem));
        }

        const QString savedCharacter = QString::fromUtf8(GetMyCharacter());
        QListWidgetItem* applyItem = nullptr;
        for (int index = 0; index < avatarList->count(); ++index) {
            QListWidgetItem* candidate = avatarList->item(index);
            const QString candidateName = candidate->data(Qt::UserRole).toString();
            if (candidateName.compare(savedCharacter, Qt::CaseInsensitive) != 0
                && GetAvatar2(candidateName)) {
                applyItem = candidate;
                break;
            }
        }
        if (!applyItem) return EXIT_FAILURE;
        avatarList->setCurrentItem(applyItem);
        application.processEvents();
        const QString selectedCharacter = applyItem->data(Qt::UserRole).toString();
        if (QString::fromUtf8(GetMyCharacter()) != savedCharacter) {
            qWarning() << "Character selection changed application state before apply";
            return EXIT_FAILURE;
        }
        const bool savedComicView = document.m_bComicView;
        document.m_bComicView = false;
        characterPage.apply();
        document.m_bComicView = savedComicView;
        if (QString::fromUtf8(GetMyCharacter()).compare(
                selectedCharacter, Qt::CaseInsensitive) != 0) {
            qWarning() << "Character-page non-comic apply";
            return EXIT_FAILURE;
        }
        SetMyCharacter(savedCharacter);

        characterPage.hide();
        application.processEvents();
        if (GetCharSelBodyCam()) {
            qWarning() << "hidden Character-page preview remained registered";
            return EXIT_FAILURE;
        }
    }

    {
        if (!document.m_memberList || !document.m_proto)
            return EXIT_FAILURE;
        const QString savedNick = theApp.m_myNick;
        const INT savedAutoPage = theApp.m_iAutoPage;
        const ConnectionStatus savedStatus = document.GetConnectionStatus();
        const QString selfNick = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK"));
        QString memberName;
        GetNextAvatarName(memberName);
        if (memberName.isEmpty()) return EXIT_FAILURE;
        const QString server = originalResourceString(
            QStringLiteral("IDS_DEFAULT_SERVER"));
        CUserInfo selfMember(selfNick, selfNick + QLatin1Char('@') + server);
        CUserInfo otherMember(memberName,
                              memberName + QLatin1Char('@') + server);
        selfMember.ComicUser(true);
        otherMember.ComicUser(true);
        otherMember.SetAvatarRealInfo(memberName,
                                      originalResourceString(
                                          QStringLiteral("IDS_URL_MSPREFIX")));
        theApp.m_myNick = selfNick;
        theApp.m_iAutoPage = -1;
        document.m_bComicView = true;
        document.m_puiSelf = &selfMember;
        g_puiSelf = &selfMember;
        document.m_allChannelPuis.append(&selfMember);
        document.m_allChannelPuis.append(&otherMember);
        document.m_memberList->AddUser(&selfMember);
        document.m_memberList->AddUser(&otherMember);
        otherMember.SelectInMemberList(&otherMember, TRUE, FALSE);
        document.m_proto->SetConnectionStatus(CX_INCHANNEL);

        QMenu* memberMenu = menuWithDirectCommand(
            frame.menuBar(), QStringLiteral("ID_MEMBER_GETINFO"));
        if (!memberMenu) return EXIT_FAILURE;
        memberMenu->aboutToShow();
        application.processEvents();
        const QStringList commands = directCommands(memberMenu);
        const int profile = commands.indexOf(
            QStringLiteral("ID_MEMBER_GETINFO"));
        const int identity = commands.indexOf(
            QStringLiteral("ID_GETIDENTITY"));
        const int character = commands.indexOf(
            QStringLiteral("ID_MEMBER_GETCHAR"));
        QAction* notifications = directCommand(
            memberMenu, QStringLiteral("ID_ADDTONOTIFICATIONS"));
        QAction* getCharacter = directCommand(
            memberMenu, QStringLiteral("ID_MEMBER_GETCHAR"));
        if (profile < 0 || identity != profile + 1
            || character != identity + 1
            || !notifications || !notifications->isEnabled()
            || !getCharacter || !getCharacter->isEnabled()
            || getCharacter->text() != originalResourceString(
                QStringLiteral("IDS_GET_CHARACTER"))) {
            qWarning() << "member menu state" << commands
                       << (notifications && notifications->isEnabled())
                       << (getCharacter && getCharacter->isEnabled());
            return EXIT_FAILURE;
        }

        theApp.m_iAutoPage = 1;
        memberMenu->aboutToShow();
        if (directCommand(memberMenu,
                          QStringLiteral("ID_ADDTONOTIFICATIONS"))->isEnabled()) {
            return EXIT_FAILURE;
        }
        theApp.m_iAutoPage = -1;
        document.m_bComicView = false;
        memberMenu->aboutToShow();
        if (directCommand(memberMenu, QStringLiteral("ID_MEMBER_GETCHAR")))
            return EXIT_FAILURE;
        document.m_bComicView = true;
        memberMenu->aboutToShow();
        if (!directCommand(memberMenu, QStringLiteral("ID_MEMBER_GETCHAR")))
            return EXIT_FAILURE;

        document.m_memberList->Clear();
        document.m_allChannelPuis.clear();
        document.m_puiSelf = nullptr;
        g_puiSelf = nullptr;
        document.m_proto->SetConnectionStatus(savedStatus);
        theApp.m_myNick = savedNick;
        theApp.m_iAutoPage = savedAutoPage;
    }

    // The focused interaction checks require a visible, registered main
    // frame. Restore the pre-existing structure-test harness before its MDI
    // lifecycle checks; the offscreen plugin cannot reactivate a hidden
    // top-level window after nested modal loops.
    frame.hide();
    theApp.m_pMainWnd = nullptr;
    frame.ActivateDocument(&document);
    SetChatDoc(&document);
    application.processEvents();

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
