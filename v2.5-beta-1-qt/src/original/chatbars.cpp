// Ported from v2.5-beta-1-modern/chatbars.cpp. Toolbar resources, styles,
// band order, toggling and context-menu flow retain the original boundaries.

#include "chatbars.h"

#include "chat.h"
#include "originalassets.h"
#include "resource.h"

#include <QAction>
#include <QActionGroup>
#include <QCursor>
#include <QMainWindow>
#include <QMenu>
#include <QPixmap>
#include <QSize>

#include <memory>
#include <utility>

namespace {
constexpr UINT nIDs[] = {
    IDR_MAINFRAME, IDR_USERTOOLBAR, IDR_TEXTTOOLBAR, 0
};
constexpr UINT nShowFlags[] = {
    SB_TOOLBAR_MAIN, SB_TOOLBAR_MEMBER, SB_TOOLBAR_TEXT, 0
};

QString resourceIdentifier(UINT id)
{
    switch (id) {
    case IDR_MAINFRAME: return QStringLiteral("IDR_MAINFRAME");
    case IDR_USERTOOLBAR: return QStringLiteral("IDR_USERTOOLBAR");
    case IDR_TEXTTOOLBAR: return QStringLiteral("IDR_TEXTTOOLBAR");
    default: return {};
    }
}

QString toolbarTitle(UINT id)
{
    switch (id) {
    case IDR_MAINFRAME:
        return originalMenuItemText(
            QStringLiteral("ID_VIEW_TOOLBAR_MAIN")).remove(QLatin1Char('&'));
    case IDR_USERTOOLBAR:
        return originalMenuItemText(
            QStringLiteral("ID_VIEW_TOOLBAR_MEMBER")).remove(QLatin1Char('&'));
    case IDR_TEXTTOOLBAR:
        return originalMenuItemText(
            QStringLiteral("ID_VIEW_TOOLBAR_TEXT")).remove(QLatin1Char('&'));
    default:
        return {};
    }
}

UINT commandID(const QString& command)
{
#define COMMAND_ID(name) \
    if (command == QLatin1String(#name)) return name
    COMMAND_ID(ID_SESSION_CONNECT);
    COMMAND_ID(ID_SESSION_DISCONNECT);
    COMMAND_ID(ID_SESSION_NEWROOM);
    COMMAND_ID(ID_SESSION_LEAVE);
    COMMAND_ID(ID_ROOM_CREATEROOM);
    COMMAND_ID(ID_VIEW_COMICS);
    COMMAND_ID(ID_VIEW_TEXT);
    COMMAND_ID(ID_CHATROOM_LIST);
    COMMAND_ID(ID_USER_LIST);
    COMMAND_ID(ID_FAVORITES_OPENFAVORITES);
    COMMAND_ID(ID_AWAY_TOGGLE);
    COMMAND_ID(ID_GETIDENTITY);
    COMMAND_ID(ID_MEMBER_IGNORE);
    COMMAND_ID(ID_WHISPERBOX_MLIST);
    COMMAND_ID(ID_SEND_EMAIL);
    COMMAND_ID(ID_VISIT_HOMEPAGE);
    COMMAND_ID(ID_START_NETMEETING);
    COMMAND_ID(ID_SETFONT);
    COMMAND_ID(ID_SETCOLOR);
    COMMAND_ID(ID_SWITCHBOLD);
    COMMAND_ID(ID_SWITCHITALIC);
    COMMAND_ID(ID_SWITCHUNDERLINED);
    COMMAND_ID(ID_SWITCHFIXEDPITCH);
    COMMAND_ID(ID_SWITCHSYMBOL);
#undef COMMAND_ID
    return 0;
}

QIcon toolbarIcon(const QString& path, int index, const QSize& buttonSize)
{
    if (path.isEmpty() || index < 0 || buttonSize.isEmpty()) return {};
    const QPixmap strip(path);
    if (strip.isNull()) return {};
    return QIcon(strip.copy(index * buttonSize.width(), 0,
                            buttonSize.width(), buttonSize.height()));
}

void appendContextItems(QMenu* menu, const QList<OriginalMenuItem>& items,
                        const CChatToolBar::CommandInvoker& invoke)
{
    for (const OriginalMenuItem& item : items) {
        if (item.type == OriginalMenuItemType::Separator) {
            menu->addSeparator();
        } else if (item.type == OriginalMenuItemType::Popup) {
            QMenu* child = menu->addMenu(item.text);
            appendContextItems(child, item.children, invoke);
        } else {
            QAction* action = menu->addAction(item.text);
            action->setData(item.commandIdentifier);
            action->setStatusTip(originalResourceString(
                item.commandIdentifier).section(QLatin1Char('\n'), 0, 0));
            QObject::connect(action, &QAction::triggered, menu,
                             [invoke, command = item.commandIdentifier] {
                if (invoke) invoke(command);
            });
        }
    }
}
}

CChatToolBar::CChatToolBar(QObject* parent)
    : CCoolBarEx(parent)
{
}

BOOL CChatToolBar::Create(QMainWindow* parentWindow, BOOL,
                          ActionFactory actionFactory,
                          CommandInvoker commandInvoker,
                          FavoritesMenuProvider favoritesMenuProvider)
{
    m_actionFactory = std::move(actionFactory);
    m_commandInvoker = std::move(commandInvoker);
    m_favoritesMenuProvider = std::move(favoritesMenuProvider);
    BOOL visibility[3];
    for (int index = 0; index < 3; ++index)
        visibility[index] = (theApp.m_iShowBars & nShowFlags[index]) != 0;
    return CCoolBarEx::Create(parentWindow, nIDs, visibility);
}

CCoolToolBarEx* CChatToolBar::CreateToolBar(UINT id)
{
    const QString identifier = resourceIdentifier(id);
    const OriginalToolbarResource resource = originalToolbarResource(identifier);
    if (identifier.isEmpty() || resource.items.isEmpty()) return nullptr;

    auto* toolbar = new CCoolToolBarEx(toolbarTitle(id), m_parentWnd);
    toolbar->setObjectName(identifier);
    toolbar->setIconSize(QSize(resource.buttonWidth, resource.buttonHeight));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setContextMenuPolicy(Qt::CustomContextMenu);
    const QString bitmapPath = originalFileResourcePath(
        identifier, QStringLiteral("BITMAP"));
    const QSize buttonSize(resource.buttonWidth, resource.buttonHeight);
    int iconIndex = 0;
    for (const OriginalToolbarItem& item : resource.items) {
        if (item.separator) {
            toolbar->addSeparator();
            continue;
        }
        const QIcon icon = toolbarIcon(bitmapPath, iconIndex++, buttonSize);
        QAction* action = m_actionFactory
            ? m_actionFactory(toolbar, item.commandIdentifier, icon)
            : toolbar->addAction(icon, originalResourceString(
                  item.commandIdentifier).section(QLatin1Char('\n'), 1, 1));
        if (!action) {
            delete toolbar;
            return nullptr;
        }
        action->setProperty("originalCommandID", commandID(
            item.commandIdentifier));
        action->setProperty("originalButtonStyle", DWORD{0});
    }
    QObject::connect(toolbar, &QToolBar::customContextMenuRequested, toolbar,
                     [this, toolbar](const QPoint& point) {
        OnContextMenu(toolbar, point);
    });
    return toolbar;
}

void CChatToolBar::OnPrepareToolBar(UINT id, CCoolToolBarEx* toolbar)
{
    if (!toolbar) return;
    switch (id) {
    case IDR_MAINFRAME: {
        toolbar->ModifyButtonStyle(ID_FAVORITES_OPENFAVORITES,
                                   TBSTYLE_DROPDOWN);
        toolbar->ModifyButtonStyle(ID_VIEW_COMICS,
                                   TBSTYLE_CHECKGROUP | TBSTYLE_CHECK);
        toolbar->ModifyButtonStyle(ID_VIEW_TEXT, TBSTYLE_CHECK);
        toolbar->ModifyButtonStyle(ID_CHATROOM_LIST, TBSTYLE_GROUP);
        QAction* comics = toolbar->GetButtonFromID(ID_VIEW_COMICS);
        QAction* text = toolbar->GetButtonFromID(ID_VIEW_TEXT);
        if (comics && text) {
            auto* group = new QActionGroup(toolbar);
            group->setExclusive(true);
            group->addAction(comics);
            group->addAction(text);
        }
        if (m_favoritesMenuProvider) {
            if (QMenu* menu = m_favoritesMenuProvider())
                toolbar->SetButtonMenu(ID_FAVORITES_OPENFAVORITES, menu);
        }
        break;
    }
    case IDR_USERTOOLBAR:
        toolbar->ModifyButtonStyle(ID_AWAY_TOGGLE, TBSTYLE_CHECK);
        break;
    case IDR_TEXTTOOLBAR:
        toolbar->ModifyButtonStyle(ID_SWITCHBOLD, TBSTYLE_CHECK);
        toolbar->ModifyButtonStyle(ID_SWITCHITALIC, TBSTYLE_CHECK);
        toolbar->ModifyButtonStyle(ID_SWITCHUNDERLINED, TBSTYLE_CHECK);
        toolbar->ModifyButtonStyle(ID_SWITCHFIXEDPITCH, TBSTYLE_CHECK);
        toolbar->ModifyButtonStyle(ID_SWITCHSYMBOL, TBSTYLE_CHECK);
        break;
    default:
        break;
    }
}

void CChatToolBar::ToggleBar(UINT which)
{
    const UINT showing = theApp.m_iShowBars & SB_TOOLBAR_ANY;
    if (which != CHAT_TOOLBAR_WHOLE && which > CHAT_TOOLBAR_TEXT) return;
    const UINT flags = which == CHAT_TOOLBAR_WHOLE
        ? SB_TOOLBAR_ANY : nShowFlags[which];
    const BOOL isShowing = (showing & flags) != 0;
    const UINT newShowing = which == CHAT_TOOLBAR_WHOLE
        ? (isShowing ? 0 : SB_TOOLBAR_ANY)
        : (showing ^ flags);

    if (which != CHAT_TOOLBAR_WHOLE)
        ShowBar(nIDs[which], !isShowing);
    if (which == CHAT_TOOLBAR_WHOLE || showing == 0 || newShowing == 0)
        SetWholeBarVisible(newShowing != 0);

    theApp.m_iShowBars = (theApp.m_iShowBars & ~SB_TOOLBAR_ANY)
        | newShowing;
}

QMenu* CChatToolBar::CreateContextMenu(QWidget* parent) const
{
    auto* menu = new QMenu(parent);
    const QList<OriginalMenuItem> resource = originalMenuResource(
        QStringLiteral("IDR_TOOLBARCONTEXT"));
    const QList<OriginalMenuItem> items = resource.size() == 1
        && resource.first().type == OriginalMenuItemType::Popup
        ? resource.first().children : resource;
    appendContextItems(menu, items, m_commandInvoker);
    for (QAction* action : menu->actions()) {
        const QString command = action->data().toString();
        UINT flag = 0;
        if (command == QLatin1String("ID_VIEW_TOOLBAR_MAIN"))
            flag = SB_TOOLBAR_MAIN;
        else if (command == QLatin1String("ID_VIEW_TOOLBAR_MEMBER"))
            flag = SB_TOOLBAR_MEMBER;
        else if (command == QLatin1String("ID_VIEW_TOOLBAR_TEXT"))
            flag = SB_TOOLBAR_TEXT;
        if (flag) {
            action->setCheckable(true);
            action->setChecked((theApp.m_iShowBars & flag) != 0);
        }
    }
    return menu;
}

void CChatToolBar::OnContextMenu(CCoolToolBarEx* toolbar,
                                 const QPoint& point)
{
    if (!toolbar) return;
    std::unique_ptr<QMenu> menu(CreateContextMenu(toolbar));
    const QPoint global = point == QPoint(-1, -1)
        ? QCursor::pos() : toolbar->mapToGlobal(point);
    menu->exec(global);
}
