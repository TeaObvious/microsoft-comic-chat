#include "avatar.h"
#include "bodycam.h"
#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "ircproto.h"
#include "ircsock.h"
#include "memblst.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "saywnd.h"
#include "setupdlg.h"
#include "textview.h"
#include "userinfo.h"

#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QDialog>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QWidget>

#include <cstdio>
#include <cstdlib>

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

template<typename Invocation>
BOOL invokeWithoutMessageBox(QApplication& application,
                             Invocation invocation)
{
    bool messageBoxShown = false;
    QTimer::singleShot(0, &application, [&] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* messageBox = qobject_cast<QMessageBox*>(widget);
            if (!messageBox || !messageBox->isVisible()) continue;
            messageBoxShown = true;
            messageBox->reject();
        }
    });
    const BOOL result = invocation();
    application.processEvents();
    REQUIRE(!messageBoxShown);
    return result;
}

CUserInfo* otherUser(CChatDoc& document)
{
    for (CUserInfo* pui : document.m_allChannelPuis) {
        if (pui && pui != document.m_puiSelf) return pui;
    }
    return nullptr;
}

QPoint findAvatarPoint(CPageView* view, UINT* avatarID)
{
    for (int y = 0; y < view->viewport()->height(); y += 2) {
        for (int x = 0; x < view->viewport()->width(); x += 2) {
            POINT point{x, y};
            const UINT found = view->FindAvatarUnderPoint(point);
            if (found) {
                if (avatarID) *avatarID = found;
                return QPoint(x, y);
            }
        }
    }
    return QPoint(-1, -1);
}

QPoint findLabelPoint(CPageView* view)
{
    for (int y = 0; y < view->viewport()->height(); y += 2) {
        for (int x = 0; x < view->viewport()->width(); x += 2) {
            POINT point{x, y};
            POINT panelPoint{};
            void* panel = nullptr;
            if (view->FindLabelUnderPoint(point, panelPoint, panel))
                return QPoint(x, y);
        }
    }
    return QPoint(-1, -1);
}

void sendMousePress(CPageView* view, const QPoint& point,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(point),
                      Qt::LeftButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(view->viewport(), &event);
}

void sendRightMousePress(CPageView* view, const QPoint& point)
{
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(point),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &event);
}

void sendContextMenu(QWidget* widget, QContextMenuEvent::Reason reason,
                     const QPoint& point)
{
    QContextMenuEvent event(reason, point, widget->mapToGlobal(point));
    QApplication::sendEvent(widget, &event);
}

QAction* findCommand(QMenu* menu, const QString& command)
{
    if (!menu) return nullptr;
    for (QAction* action : menu->actions()) {
        if (action->data().toString() == command) return action;
        if (QAction* nested = findCommand(action->menu(), command))
            return nested;
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

template<typename Trigger, typename Inspect>
void inspectContextPopup(QApplication& application, Trigger trigger,
                         Inspect inspect)
{
    bool inspected = false;
    QTimer::singleShot(0, &application, [&] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* menu = qobject_cast<QMenu*>(widget);
            if (!menu || !menu->isVisible()) continue;
            inspect(menu);
            inspected = true;
            menu->close();
            return;
        }
    });
    trigger();
    application.processEvents();
    REQUIRE(inspected);
}

void sendKeyPress(QWidget* view, int key,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                  const QString& text = QString())
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    QApplication::sendEvent(view, &event);
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    theApp.InitVals();
    theApp.m_bComicView = true;
    theApp.InitializeComicsFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont,
                                     theApp.m_comicsColor));
    InitializeBackDrops();
    InitializeAvatars();

    bool aboutInspected = false;
    QTimer::singleShot(0, &application, [&] {
        auto* about = qobject_cast<QDialog*>(
            QApplication::activeModalWidget());
        REQUIRE(about != nullptr);
        REQUIRE(about->objectName() == QStringLiteral("IDD_ABOUTBOX"));
        REQUIRE(about->windowTitle() == originalDialogCaption(
            QStringLiteral("IDD_ABOUTBOX")));

        QLabel* tiki = about->findChild<QLabel*>(
            QStringLiteral("IDC_TIKI"));
        QLabel* version = about->findChild<QLabel*>(
            QStringLiteral("IDC_VERSION"));
        QLabel* warning = about->findChild<QLabel*>(
            QStringLiteral("IDC_WARNING"));
        QGroupBox* license = about->findChild<QGroupBox*>(
            QStringLiteral("IDC_LICENSE"));
        QPushButton* ok = about->findChild<QPushButton*>(
            QStringLiteral("IDOK"));
        REQUIRE(tiki != nullptr);
        REQUIRE(!tiki->pixmap(Qt::ReturnByValue).isNull());
        REQUIRE(version != nullptr);
        QString versionText;
        GetVersionString(versionText);
        REQUIRE(version->text() == versionText);
        REQUIRE(warning != nullptr);
        REQUIRE(warning->text() == originalResourceString(
            QStringLiteral("IDS_WARNING_TEXT")));
        REQUIRE(license != nullptr);
        REQUIRE(license->title() == originalDialogControlText(
            QStringLiteral("IDD_ABOUTBOX"),
            QStringLiteral("IDC_LICENSE")));
        REQUIRE(ok != nullptr);
        REQUIRE(ok->text() == originalDialogControlText(
            QStringLiteral("IDD_ABOUTBOX"), QStringLiteral("IDOK")));
        aboutInspected = true;
        about->accept();
    });
    theApp.OnAppAbout();
    REQUIRE(aboutInspected);

    QString selfAvatarName;
    QString otherAvatarName;
    GetNextAvatarName(selfAvatarName);
    GetNextAvatarName(otherAvatarName);
    REQUIRE(!selfAvatarName.isEmpty());
    REQUIRE(!otherAvatarName.isEmpty());
    REQUIRE(selfAvatarName.compare(otherAvatarName, Qt::CaseInsensitive) != 0);
    theApp.m_myCharacterName = selfAvatarName;

    REQUIRE(CommunicationInits());
    {
        CChatDoc document;
        SetChatDoc(&document);

        QWidget host;
        host.resize(1800, 1200);
        auto* view = new CPageView(&host);
        auto* say = new CSayWnd(&host);
        auto* members = new CMemberList(&host);
        view->setGeometry(0, 0, 900, 650);
        say->setGeometry(0, 660, 900, 30);
        members->setGeometry(910, 0, 250, 300);
        document.m_view = view;
        document.m_sayWnd = say;
        document.m_memberList = members;

        auto* bodyCam = new CBodyCam(&host);
        bodyCam->setGeometry(1170, 0, 240, 320);
        host.show();
        application.processEvents();

        inspectContextPopup(application, [&] {
            sendContextMenu(bodyCam, QContextMenuEvent::Keyboard,
                            QPoint(-1, -1));
        }, [&](QMenu* menu) {
            REQUIRE(directCommands(menu) == QStringList({
                QStringLiteral("ID_BODYCONTEXT_FREEZE"),
                QStringLiteral("ID_BODYCONTEXT_SENDEXPRESSION")
            }));
            QAction* freeze = findCommand(
                menu, QStringLiteral("ID_BODYCONTEXT_FREEZE"));
            QAction* sendExpression = findCommand(
                menu, QStringLiteral("ID_BODYCONTEXT_SENDEXPRESSION"));
            REQUIRE(freeze != nullptr && freeze->isCheckable()
                    && !freeze->isChecked());
            REQUIRE(freeze->text() == originalMenuItemText(
                QStringLiteral("ID_BODYCONTEXT_FREEZE")));
            REQUIRE(sendExpression != nullptr);
            REQUIRE(sendExpression->text() == originalMenuItemText(
                QStringLiteral("ID_BODYCONTEXT_SENDEXPRESSION")));
        });

        const QString nick = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK"));
        const QString channel = originalResourceString(
            QStringLiteral("IDS_DEFAULT_CHANNEL"));
        const QString user = QString::fromUtf8(GetMyUserName());
        serverConn.ProcessMessage(
            QStringLiteral(":%1!%2@127.0.0.1 JOIN :%3")
                .arg(nick, user, channel));
        serverConn.ProcessMessage(
            QStringLiteral("353 %1 = %2 :%1 %3")
                .arg(nick, channel, otherAvatarName));
        serverConn.ProcessMessage(
            QStringLiteral("366 %1 %2").arg(nick, channel));

        CUserInfo* other = otherUser(document);
        REQUIRE(g_puiSelf != nullptr);
        REQUIRE(other != nullptr);
        REQUIRE(members->count() == 2);

        other->SetFlag(UF_AUTODOWNLOAD | UF_INTERACTIVEDOWNLOAD, true);
        REQUIRE(!invokeWithoutMessageBox(application, [&] {
            return theApp.StartDownloadingAvatar(other, &document, TRUE);
        }));
        REQUIRE(!other->CheckFlag(UF_AUTODOWNLOAD));
        REQUIRE(!other->CheckFlag(UF_INTERACTIVEDOWNLOAD));
        const QByteArray backdrop = originalResourceString(
            QStringLiteral("IDS_DEFAULT_BACKDROP")).toUtf8();
        const QByteArray artUrl = originalResourceString(
            QStringLiteral("IDS_URL_MSPREFIX")).toUtf8();
        REQUIRE(!invokeWithoutMessageBox(application, [&] {
            return theApp.StartDownloadingBackdrop(
                backdrop.constData(), artUrl.constData());
        }));

        // This is the continuation text hard-coded by balloon.cpp itself.
        AddAndExecute(new SayEntry(other, QStringLiteral("..."),
                                   NoFormattingSentinel()), &document);
        view->SetPanelsWide(2);
        application.processEvents();

        UINT hitAvatarID = 0;
        const QPoint avatarPoint = findAvatarPoint(view, &hitAvatarID);
        REQUIRE(avatarPoint.x() >= 0);
        CAvatarX* hitAvatar = GetAvatar(static_cast<USHORT>(hitAvatarID));
        REQUIRE(hitAvatar != nullptr);
        REQUIRE(hitAvatar->m_userInfo == other);
        REQUIRE(findLabelPoint(view).x() >= 0);

        sendMousePress(view, avatarPoint);
        REQUIRE(members->currentUser() == other);
        REQUIRE(members->SelectedMemberCount() == 1);

        // Context-menu labels and dynamic entries come from chat.rc and the
        // already configured source-backed macro/character state.
        theApp.m_macros[0].m_bDefined = TRUE;
        theApp.m_macros[0].m_strName = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK"));
        theApp.m_macros[0].m_strValue = originalResourceString(
            QStringLiteral("IDS_DEFAULT_CHANNEL"));
        theApp.m_iAutoPage = -1;
        other->ComicUser(true);
        other->SetAvatarRealInfo(otherAvatarName, originalResourceString(
            QStringLiteral("IDS_URL_MSPREFIX")));
        other->SetFullName(otherAvatarName + QLatin1Char('@')
                           + originalResourceString(
                               QStringLiteral("IDS_DEFAULT_SERVER")));

        sendRightMousePress(view, avatarPoint);
        inspectContextPopup(application, [&] {
            sendContextMenu(view->viewport(), QContextMenuEvent::Mouse,
                            avatarPoint);
        }, [&](QMenu* menu) {
            REQUIRE(mousedPui == other);
            const QStringList commands = directCommands(menu);
            const int profile = commands.indexOf(
                QStringLiteral("ID_MEMBER_GETINFO"));
            const int identity = commands.indexOf(
                QStringLiteral("ID_GETIDENTITY"));
            const int character = commands.indexOf(
                QStringLiteral("ID_MEMBER_GETCHAR"));
            const int whisper = commands.indexOf(
                QStringLiteral("ID_WHISPERBOX_MLIST"));
            REQUIRE(profile >= 0);
            REQUIRE(identity == profile + 1);
            REQUIRE(character == identity + 1);
            REQUIRE(whisper == character + 1);
            REQUIRE(findCommand(menu, QStringLiteral("ID_ADDTONOTIFICATIONS"))
                    != nullptr);
            QAction* getCharacter = findCommand(
                menu, QStringLiteral("ID_MEMBER_GETCHAR"));
            REQUIRE(getCharacter != nullptr);
            REQUIRE(getCharacter->text() == originalResourceString(
                QStringLiteral("IDS_GET_CHARACTER")));
            REQUIRE(getCharacter->isEnabled());
            const QStringList enabledCommands = {
                QStringLiteral("ID_MEMBER_GETINFO"),
                QStringLiteral("ID_GETIDENTITY"),
                QStringLiteral("ID_WHISPERBOX_MLIST"),
                QStringLiteral("ID_ADDTONOTIFICATIONS"),
                QStringLiteral("ID_MEMBER_IGNORE"),
                QStringLiteral("ID_SEND_EMAIL"),
                QStringLiteral("ID_VISIT_HOMEPAGE"),
                QStringLiteral("ID_GET_VERSION"),
                QStringLiteral("ID_PING_USER"),
                QStringLiteral("ID_GET_LOCALTIME"),
                QStringLiteral("ID_SEND_FILE")
            };
            for (const QString& command : enabledCommands) {
                QAction* action = findCommand(menu, command);
                REQUIRE(action != nullptr);
                REQUIRE(action->isEnabled());
            }
            REQUIRE(!findCommand(menu, QStringLiteral("ID_MEMBER_IGNORE"))
                         ->isChecked());
            QAction* defineMacro = findCommand(
                menu, QStringLiteral("ID_DEFINE_MACRO"));
            QAction* macro = findCommand(menu, QStringLiteral("ID_MACRO_A0"));
            REQUIRE(defineMacro != nullptr);
            REQUIRE(macro != nullptr);
            REQUIRE(menu->actions().size() > MACROSUBMENUCOMIC);
            REQUIRE(menu->actions().at(MACROSUBMENUCOMIC)->menu() != nullptr);
            REQUIRE(findCommand(
                menu->actions().at(MACROSUBMENUCOMIC)->menu(),
                QStringLiteral("ID_DEFINE_MACRO")) == defineMacro);
            REQUIRE(macro->text() == QStringLiteral("%1\tAlt+0").arg(
                originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"))));
            REQUIRE(!findCommand(menu, QStringLiteral("ID_START_NETMEETING"))
                         ->isEnabled());
            QAction* ping = findCommand(
                menu, QStringLiteral("ID_PING_USER"));
            REQUIRE(ping != nullptr);
            REQUIRE(ping->isEnabled());
            ping->trigger();
        });
        REQUIRE(other->CheckFlag(UF_REQUESTPING));

        auto* memberWidget = qobject_cast<QListWidget*>(members->FocusWidget());
        REQUIRE(memberWidget != nullptr);
        QListWidgetItem* otherItem = nullptr;
        for (int index = 0; index < memberWidget->count(); ++index) {
            QListWidgetItem* item = memberWidget->item(index);
            if (item->data(Qt::UserRole).value<void*>() == other) {
                otherItem = item;
                break;
            }
        }
        REQUIRE(otherItem != nullptr);
        mousedPui = nullptr;
        const QPoint memberPoint = memberWidget->visualItemRect(otherItem).center();
        inspectContextPopup(application, [&] {
            sendContextMenu(memberWidget->viewport(),
                            QContextMenuEvent::Mouse, memberPoint);
        }, [&](QMenu* menu) {
            REQUIRE(findCommand(menu, QStringLiteral("ID_MEMBER_GETINFO"))
                    != nullptr);
        });
        REQUIRE(mousedPui == other);

        mousedPui = nullptr;
        inspectContextPopup(application, [&] {
            sendContextMenu(memberWidget->viewport(),
                            QContextMenuEvent::Keyboard, QPoint(-1, -1));
        }, [&](QMenu* menu) {
            REQUIRE(findCommand(menu, QStringLiteral("ID_MEMBER_GETINFO"))
                    != nullptr);
        });
        REQUIRE(mousedPui == other);

        document.m_bComicView = false;
        other->Ignore(true);
        inspectContextPopup(application, [&] {
            sendContextMenu(memberWidget->viewport(),
                            QContextMenuEvent::Mouse, memberPoint);
        }, [&](QMenu* menu) {
            REQUIRE(findCommand(menu, QStringLiteral("ID_MEMBER_GETCHAR"))
                    == nullptr);
            REQUIRE(menu->actions().size() > MACROSUBMENUTEXT);
            QMenu* macroMenu = menu->actions().at(MACROSUBMENUTEXT)->menu();
            REQUIRE(macroMenu != nullptr);
            REQUIRE(findCommand(macroMenu, QStringLiteral("ID_DEFINE_MACRO"))
                    != nullptr);
            QAction* ignore = findCommand(
                menu, QStringLiteral("ID_MEMBER_IGNORE"));
            REQUIRE(ignore != nullptr && ignore->isEnabled()
                    && ignore->isChecked());
        });
        other->Ignore(false);
        document.m_bComicView = true;

        g_puiSelf->SetOperator(true);
        document.m_proto->m_dwModes &= ~DWORD(CM_MODERATED);
        inspectContextPopup(application, [&] {
            sendContextMenu(view->viewport(), QContextMenuEvent::Mouse,
                            avatarPoint);
        }, [&](QMenu* menu) {
            QAction* kick = findCommand(
                menu, QStringLiteral("ID_ADMINISTRATOR_KICK"));
            QAction* ban = findCommand(menu, QStringLiteral("ID_ADMIN_BAN"));
            QAction* hostAction = findCommand(
                menu, QStringLiteral("ID_MAKEADMIN"));
            QAction* speakerAction = findCommand(
                menu, QStringLiteral("ID_MAKESPEAKER"));
            QAction* spectatorAction = findCommand(
                menu, QStringLiteral("ID_MAKESPECTATOR"));
            REQUIRE(kick != nullptr && kick->isEnabled());
            REQUIRE(ban != nullptr && ban->isEnabled());
            REQUIRE(hostAction != nullptr && hostAction->isEnabled()
                    && !hostAction->isChecked());
            REQUIRE(speakerAction != nullptr && speakerAction->isEnabled()
                    && speakerAction->isChecked());
            REQUIRE(spectatorAction != nullptr
                    && !spectatorAction->isEnabled()
                    && !spectatorAction->isChecked());
        });

        document.m_proto->m_dwModes |= CM_MODERATED;
        other->SetFlag(UF_SPECTATOR, true);
        inspectContextPopup(application, [&] {
            sendContextMenu(view->viewport(), QContextMenuEvent::Mouse,
                            avatarPoint);
        }, [&](QMenu* menu) {
            QAction* speakerAction = findCommand(
                menu, QStringLiteral("ID_MAKESPEAKER"));
            QAction* spectatorAction = findCommand(
                menu, QStringLiteral("ID_MAKESPECTATOR"));
            REQUIRE(speakerAction != nullptr && speakerAction->isEnabled()
                    && !speakerAction->isChecked());
            REQUIRE(spectatorAction != nullptr
                    && spectatorAction->isEnabled()
                    && spectatorAction->isChecked());
        });
        other->SetFlag(UF_SPECTATOR, false);
        document.m_proto->m_dwModes &= ~DWORD(CM_MODERATED);
        g_puiSelf->SetOperator(false);

        inspectContextPopup(application, [&] {
            sendContextMenu(view->viewport(), QContextMenuEvent::Keyboard,
                            QPoint(-1, -1));
        }, [&](QMenu* menu) {
            QAction* copy = findCommand(menu, QStringLiteral("ID_EDIT_COPY"));
            QAction* comics = findCommand(
                menu, QStringLiteral("ID_VIEW_COMICS"));
            QAction* text = findCommand(menu, QStringLiteral("ID_VIEW_TEXT"));
            REQUIRE(copy != nullptr && !copy->isEnabled());
            REQUIRE(comics != nullptr && comics->isChecked());
            REQUIRE(text != nullptr && !text->isChecked());
            REQUIRE(findCommand(menu, QStringLiteral("ID_CLEAR_HISTORY"))
                    != nullptr);
            REQUIRE(findCommand(menu, QStringLiteral("ID_CHANNELPROPS"))
                    != nullptr);
        });

        document.OnViewIcon();
        REQUIRE(document.m_bIconMembers);
        QPoint blankPoint(-1, -1);
        for (int y = memberWidget->viewport()->height() - 1;
             y >= 0 && blankPoint.x() < 0; --y) {
            for (int x = memberWidget->viewport()->width() - 1;
                 x >= 0; --x) {
                const QPoint candidate(x, y);
                if (!memberWidget->itemAt(candidate)) {
                    blankPoint = candidate;
                    break;
                }
            }
        }
        REQUIRE(blankPoint.x() >= 0);
        inspectContextPopup(application, [&] {
            sendContextMenu(memberWidget->viewport(),
                            QContextMenuEvent::Mouse, blankPoint);
        }, [&](QMenu* menu) {
            QAction* list = findCommand(menu, QStringLiteral("ID_VIEW_LIST"));
            QAction* icon = findCommand(menu, QStringLiteral("ID_VIEW_ICON"));
            REQUIRE(list != nullptr && list->isEnabled());
            REQUIRE(icon != nullptr && icon->isChecked());
            list->trigger();
        });
        REQUIRE(!document.m_bIconMembers);

        theApp.m_macros[0] = CMacro{};
        other->SetAvatarRealInfo(QString(), QString());

        g_puiSelf->SelectInMemberList(g_puiSelf, TRUE, FALSE);
        sendMousePress(view, avatarPoint, Qt::ShiftModifier);
        REQUIRE(members->SelectedMemberCount() == 2);
        sendContextMenu(memberWidget->viewport(),
                        QContextMenuEvent::Keyboard, QPoint(-1, -1));
        application.processEvents();
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* menu = qobject_cast<QMenu*>(widget);
            REQUIRE(!menu || !menu->isVisible());
        }
        members->MakeVisible(other);

        say->GetSayEdit()->clear();
        host.activateWindow();
        view->setFocus();
        application.processEvents();
        sendKeyPress(view, Qt::Key_Tab);
        REQUIRE(QApplication::focusWidget() == say->GetSayEdit());

        const QString sourceText = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK"));
        REQUIRE(!sourceText.isEmpty());
        say->GetSayEdit()->clear();
        view->setFocus();
        sendKeyPress(view, 0, Qt::NoModifier, sourceText.left(1));
        REQUIRE(say->GetSayEdit()->toPlainText() == sourceText.left(1));

        // Repeated source continuation entries produce real history-backed
        // panels without introducing a test conversation string.
        for (int count = 0; count < 8; ++count) {
            AddAndExecute(new SayEntry(other, QStringLiteral("..."),
                                       NoFormattingSentinel()), &document);
        }
        view->SetPanelsWide(1);
        view->resize(350, 220);
        application.processEvents();
        application.processEvents();
        REQUIRE(view->verticalScrollBar()->maximum() > 0);
        view->verticalScrollBar()->setValue(0);
        sendKeyPress(view, Qt::Key_Down);
        REQUIRE(view->verticalScrollBar()->value()
                == view->verticalScrollBar()->singleStep());
        sendKeyPress(view, Qt::Key_PageDown);
        REQUIRE(view->verticalScrollBar()->value()
                > view->verticalScrollBar()->singleStep());
        sendKeyPress(view, Qt::Key_End);
        REQUIRE(view->verticalScrollBar()->value()
                == view->verticalScrollBar()->maximum());
        sendKeyPress(view, Qt::Key_Home);
        REQUIRE(view->verticalScrollBar()->value() == 0);

        sendKeyPress(say->GetSayEdit(), Qt::Key_PageDown);
        REQUIRE(view->verticalScrollBar()->value() > 0);

        auto* textView = new CTextView(&document, &host);
        textView->setGeometry(1420, 0, 300, 180);
        textView->show();
        document.m_bComicView = false;
        document.m_textView = textView;
        QString textHistory;
        for (int line = 0; line < 80; ++line) {
            if (!textHistory.isEmpty()) textHistory += QLatin1Char('\n');
            textHistory += sourceText;
        }
        textView->m_pRichEdit->setPlainText(textHistory);
        application.processEvents();
        REQUIRE(textView->m_pRichEdit->verticalScrollBar()->maximum() > 0);
        textView->m_pRichEdit->verticalScrollBar()->setValue(0);
        sendKeyPress(say->GetSayEdit(), Qt::Key_PageDown);
        REQUIRE(textView->m_pRichEdit->verticalScrollBar()->value() > 0);
        delete textView;
        document.m_bComicView = true;

        const QList<HistoryEntry*> historyBefore = document.m_history;
        view->SetPanelsWide(1);
        view->resize(1600, 900);
        application.processEvents();
        application.processEvents();
        REQUIRE(CUnitPanelPage::GetUnitPanelsPerRow() == view->FitPanelsWide());
        REQUIRE(CUnitPanelPage::GetUnitPanelsPerRow() != 1);
        REQUIRE(document.m_history == historyBefore);
        REQUIRE(!document.m_pages.isEmpty());
        REQUIRE(document.m_pages.first()->m_panels.size() > 1);

        document.m_view = nullptr;
        document.m_sayWnd = nullptr;
        document.m_memberList = nullptr;
    }

    SetChatDoc(nullptr);
    mousedPui = nullptr;
    CommunicationCleanup();
    CUnitPanelPage::DestroyFonts();
    DestroyBackDropArt();
    DestroyAvatars();
    return 0;
}
