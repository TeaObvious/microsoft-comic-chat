// Ported from v2.5-beta-1-modern/memblst.cpp.

#include "memblst.h"

#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "originalassets.h"
#include "protsupp.h"
#include "resource.h"
#include "userinfo.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QVBoxLayout>

extern CUserInfo* mousedPui;

namespace {
constexpr int MEMBER_ICON_SIZE = 40;
constexpr int MEMBER_STATUS_SIZE = 16;
constexpr int MEMBER_ICON_GAP = 2;

int GetSort(CUserInfo* pui)
{
    if (pui->IsOperator()) {
        return 0;
    }
    if (pui->IsSpectator()) {
        return 2;
    }
    return 1;
}

int GetStatusImage(CUserInfo* pui)
{
    if (!pui) return 0;
    if (pui->Ignored()) return 3;
    if (pui->CheckFlag(UF_AWAY)) return 4;
    if (pui->IsOperator()) return 1;
    if (pui->IsSpectator()) return 2;
    return 0;
}

QString SortName(CUserInfo* pui)
{
    if (!pui) return {};
    QString name = pui->GetScreenName();
    if (name.startsWith(QLatin1Char('"'))) {
        // CompareStringsWithoutQuotes removes the first and last characters
        // after asserting that a source nickname beginning with a quote also
        // ends with one.
        name = name.size() > 1 ? name.mid(1, name.size() - 2) : QString();
    }
    return name;
}

QIcon GetMemberIcon(CUserInfo* pui, bool iconMode)
{
    if (!pui) return {};
    if (theApp.m_StatusIcons.size() != 5) theApp.InitStatusIcons();
    const int statusIndex = GetStatusImage(pui);
    const QIcon status = statusIndex >= 0
            && statusIndex < theApp.m_StatusIcons.size()
        ? theApp.m_StatusIcons.at(statusIndex) : QIcon();
    if (!iconMode) return status;

    const int avatarIndex = AddToImageList(pui);
    if (avatarIndex < 0 || avatarIndex >= theApp.m_ImageList.size())
        return status;

    const int width = MEMBER_STATUS_SIZE + MEMBER_ICON_GAP
        + MEMBER_ICON_SIZE;
    QPixmap combined(width, MEMBER_ICON_SIZE);
    combined.fill(Qt::transparent);
    QPainter painter(&combined);
    painter.drawPixmap(0, (MEMBER_ICON_SIZE - MEMBER_STATUS_SIZE) / 2,
                       status.pixmap(MEMBER_STATUS_SIZE,
                                     MEMBER_STATUS_SIZE));
    painter.drawPixmap(MEMBER_STATUS_SIZE + MEMBER_ICON_GAP, 0,
                       theApp.m_ImageList.at(avatarIndex).pixmap(
                           MEMBER_ICON_SIZE, MEMBER_ICON_SIZE));
    return QIcon(combined);
}

void UpdateListItem(QListWidgetItem* item, CUserInfo* pui, bool iconMode)
{
    if (!item || !pui) return;
    QString displayName;
    if (theApp.m_bDoTest) pui->GetAttedNick(displayName);
    else displayName = pui->GetScreenName();
    const int statusImage = GetStatusImage(pui);
    const int avatarImage = iconMode ? AddToImageList(pui) : -1;
    item->setText(displayName);
    item->setData(CMemberList::StatusImageRole, statusImage);
    item->setData(CMemberList::StateImageRole,
                  iconMode ? statusImage + 1 : 0);
    item->setData(CMemberList::AvatarImageRole, avatarImage);
    item->setData(CMemberList::IconModeRole, iconMode);
    item->setIcon(GetMemberIcon(pui, iconMode));
}

CChatDoc* DocumentForMemberList(const CMemberList* memberList)
{
    for (CChatDoc* document : g_docs) {
        if (document && document->m_memberList == memberList) return document;
    }
    return GetChatDoc();
}

bool MemberCommandEnabled(const QString& command, CChatDoc* document)
{
    if (!document) return false;
    if (command == QLatin1String("ID_MEMBER_GETINFO"))
        return document->OnUpdateMemberGetinfo();
    if (command == QLatin1String("ID_MEMBER_IGNORE"))
        return document->OnUpdateMemberIgnore();
    if (command == QLatin1String("ID_ADDTONOTIFICATIONS"))
        return document->OnUpdateAddToNotifs();
    if (command == QLatin1String("ID_GETIDENTITY")
        || command == QLatin1String("ID_GET_VERSION")
        || command == QLatin1String("ID_PING_USER")
        || command == QLatin1String("ID_GET_LOCALTIME")) {
        return document->OnUpdateGetidentity();
    }
    if (command == QLatin1String("ID_SEND_EMAIL"))
        return document->OnUpdateComicUserNotSelf();
    if (command == QLatin1String("ID_WHISPERBOX_MLIST"))
        return document->OnUpdate1SelectionNotSelf();
    if (command == QLatin1String("ID_SEND_FILE"))
        return document->OnUpdate1SelectionNotSelf();
    if (command == QLatin1String("ID_VISIT_HOMEPAGE"))
        return document->OnUpdateVisitHomepage();
    if (command == QLatin1String("ID_MEMBER_GETCHAR"))
        return document->OnUpdateGetComicCharacter();
    if (command == QLatin1String("ID_ADMINISTRATOR_KICK"))
        return document->OnUpdate1SelectionNotSelf();
    if (command == QLatin1String("ID_ADMIN_BAN"))
        return document->OnUpdateAdminBan();
    if (command == QLatin1String("ID_MAKEADMIN"))
        return document->OnUpdateMakeadmin();
    if (command == QLatin1String("ID_MAKESPEAKER"))
        return document->OnUpdateMakespeaker();
    if (command == QLatin1String("ID_MAKESPECTATOR"))
        return document->OnUpdateMakespectator();
    if (command == QLatin1String("ID_VIEW_ICON"))
        return document->OnUpdateViewIcon();
    if (command == QLatin1String("ID_VIEW_LIST"))
        return document->OnUpdateViewList();
    if (command == QLatin1String("ID_DEFINE_MACRO"))
        return true;
    if (command.startsWith(QLatin1String("ID_MACRO_A"))) {
        bool valid = false;
        const INT macro = command.mid(10).toInt(&valid);
        return valid && macro >= 0 && macro < NMACROS
            && document->OnUpdateMacro(ID_MACRO_A0 + macro);
    }
    // The CB32/NetMeeting path is excluded from the Modern build.
    if (command == QLatin1String("ID_START_NETMEETING")) {
        return false;
    }
    return false;
}

void ExecuteMemberCommand(const QString& command, CChatDoc* document)
{
    if (!document || !MemberCommandEnabled(command, document)) return;
    if (command == QLatin1String("ID_MEMBER_GETINFO"))
        document->OnMemberGetinfo();
    else if (command == QLatin1String("ID_MEMBER_IGNORE"))
        document->OnMemberIgnore();
    else if (command == QLatin1String("ID_ADDTONOTIFICATIONS"))
        document->OnAddToNotifs();
    else if (command == QLatin1String("ID_GETIDENTITY"))
        document->OnGetidentity();
    else if (command == QLatin1String("ID_MEMBER_GETCHAR"))
        document->OnGetComicCharacter();
    else if (command == QLatin1String("ID_GET_VERSION"))
        document->OnGetVersion();
    else if (command == QLatin1String("ID_PING_USER"))
        document->OnPingUser();
    else if (command == QLatin1String("ID_GET_LOCALTIME"))
        document->OnGetLocaltime();
    else if (command == QLatin1String("ID_WHISPERBOX_MLIST"))
        document->OnWhisperboxMlist();
    else if (command == QLatin1String("ID_SEND_FILE"))
        document->OnSendFile();
    else if (command == QLatin1String("ID_SEND_EMAIL"))
        document->OnSendEmail();
    else if (command == QLatin1String("ID_VISIT_HOMEPAGE"))
        document->OnVisitHomepage();
    else if (command == QLatin1String("ID_ADMINISTRATOR_KICK"))
        document->OnAdministratorKick();
    else if (command == QLatin1String("ID_ADMIN_BAN"))
        document->OnAdminBan();
    else if (command == QLatin1String("ID_MAKEADMIN"))
        document->OnMakeadmin();
    else if (command == QLatin1String("ID_MAKESPEAKER"))
        document->OnMakespeaker();
    else if (command == QLatin1String("ID_MAKESPECTATOR"))
        document->OnMakespectator();
    else if (command == QLatin1String("ID_VIEW_LIST"))
        document->OnViewList();
    else if (command == QLatin1String("ID_VIEW_ICON"))
        document->OnViewIcon();
    else if (command == QLatin1String("ID_DEFINE_MACRO"))
        theApp.OnDefineMacro();
    else if (command.startsWith(QLatin1String("ID_MACRO_A"))) {
        bool valid = false;
        const INT macro = command.mid(10).toInt(&valid);
        if (valid && macro >= 0 && macro < NMACROS)
            document->OnMacro(ID_MACRO_A0 + macro);
    }
}

void ConfigureMemberAction(QAction* action, const QString& command,
                           CChatDoc* document, QMenu& menu)
{
    action->setData(command);
    action->setStatusTip(originalResourceString(command)
                             .section(QLatin1Char('\n'), 0, 0));
    const bool enabled = MemberCommandEnabled(command, document);
    action->setEnabled(enabled);
    if (command == QLatin1String("ID_MEMBER_IGNORE")) {
        BOOL allIgnored = FALSE;
        if (document)
            document->OnUpdateMemberIgnore(&allIgnored);
        action->setCheckable(true);
        action->setChecked(allIgnored);
    } else if (command == QLatin1String("ID_VIEW_ICON")) {
        BOOL iconView = FALSE;
        if (document) document->OnUpdateViewIcon(&iconView);
        action->setCheckable(true);
        action->setChecked(iconView);
    } else if (command == QLatin1String("ID_VIEW_LIST")) {
        BOOL listView = FALSE;
        if (document) document->OnUpdateViewList(FALSE, &listView);
        action->setCheckable(true);
        action->setChecked(listView);
    } else if (command == QLatin1String("ID_MAKEADMIN")) {
        BOOL isAdmin = FALSE;
        if (document) document->OnUpdateMakeadmin(&isAdmin);
        action->setCheckable(true);
        action->setChecked(isAdmin);
    } else if (command == QLatin1String("ID_MAKESPEAKER")) {
        BOOL isSpeaker = FALSE;
        if (document) document->OnUpdateMakespeaker(&isSpeaker);
        action->setCheckable(true);
        action->setChecked(isSpeaker);
    } else if (command == QLatin1String("ID_MAKESPECTATOR")) {
        BOOL isSpectator = FALSE;
        if (document)
            document->OnUpdateMakespectator(&isSpectator);
        action->setCheckable(true);
        action->setChecked(isSpectator);
    }
    if (enabled) {
        QObject::connect(action, &QAction::triggered, &menu,
                         [command, document] {
                             ExecuteMemberCommand(command, document);
                         });
    }
}

void AppendMemberMenu(QMenu& menu, const QList<OriginalMenuItem>& items,
                      CChatDoc* document)
{
    for (const OriginalMenuItem& item : items) {
        if (item.type == OriginalMenuItemType::Separator) {
            menu.addSeparator();
            continue;
        }
        if (item.type == OriginalMenuItemType::Popup) {
            QMenu* popup = menu.addMenu(item.text);
            AppendMemberMenu(*popup, item.children, document);
            continue;
        }

        QAction* action = menu.addAction(item.text);
        ConfigureMemberAction(action, item.commandIdentifier, document, menu);
    }

    for (const QStringList& commands : {
             QStringList{QStringLiteral("ID_VIEW_LIST"),
                         QStringLiteral("ID_VIEW_ICON")},
             QStringList{QStringLiteral("ID_MAKEADMIN"),
                         QStringLiteral("ID_MAKESPEAKER"),
                         QStringLiteral("ID_MAKESPECTATOR")}}) {
        auto* group = new QActionGroup(&menu);
        group->setExclusive(true);
        for (QAction* action : menu.actions()) {
            if (commands.contains(action->data().toString()))
                group->addAction(action);
        }
        if (group->actions().size() < 2) delete group;
    }
}

void LoadMemberMenu(QMenu& menu, const QString& resource,
                    CChatDoc* document)
{
    const QList<OriginalMenuItem> roots = originalMenuResource(resource);
    if (roots.size() == 1
        && roots.first().type == OriginalMenuItemType::Popup) {
        AppendMemberMenu(menu, roots.first().children, document);
    } else {
        AppendMemberMenu(menu, roots, document);
    }
}
}

CMemberListCtrl::CMemberListCtrl(CMemberList* owner)
    : QListWidget(owner)
    , m_owner(owner)
{
}

void CMemberListCtrl::keyPressEvent(QKeyEvent* event)
{
    if (!event) return;
    CChatDoc* document = DocumentForMemberList(m_owner);
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        if (document) {
            const BOOL backward = event->key() == Qt::Key_Backtab
                || event->modifiers().testFlag(Qt::ShiftModifier);
            document->CycleFocus(CHATFOCUS_MEMBERLIST, backward);
        }
        event->accept();
        return;
    }

    if (!event->text().isEmpty() && document && document->m_sayWnd) {
        document->SetFocusToSayWnd();
        QWidget* target = QApplication::focusWidget();
        if (target && target != this) {
            QKeyEvent forwarded(QEvent::KeyPress, event->key(),
                                event->modifiers(), event->text(),
                                event->isAutoRepeat(), event->count());
            QApplication::sendEvent(target, &forwarded);
        }
        event->accept();
        return;
    }

    QListWidget::keyPressEvent(event);
}

void AddMacroMenu(QMenu& contextMenu)
{
    CChatDoc* document = GetChatDoc();
    if (!document) return;

    const INT macroPosition = bCanViewUnrated() && document->m_bComicView
        ? MACROSUBMENUCOMIC : MACROSUBMENUTEXT;
    const QList<QAction*> contextActions = contextMenu.actions();
    QMenu* macroMenu = macroPosition >= 0
            && macroPosition < contextActions.size()
        ? contextActions.at(macroPosition)->menu() : nullptr;
    if (!macroMenu) return;

    macroMenu->clear();
    QAction* define = macroMenu->addAction(originalMenuItemText(
        QStringLiteral("ID_DEFINE_MACRO")));
    ConfigureMemberAction(define, QStringLiteral("ID_DEFINE_MACRO"),
                          document, *macroMenu);

    BOOL macroPresent = FALSE;
    for (INT macro = 0; macro < NMACROS; ++macro) {
        if (!theApp.m_macros[macro].m_bDefined) continue;
        if (!macroPresent) {
            macroMenu->addSeparator();
            macroPresent = TRUE;
        }
        const QString command = QStringLiteral("ID_MACRO_A%1").arg(macro);
        QAction* action = macroMenu->addAction(QStringLiteral("%1\tAlt+%2")
            .arg(theApp.m_macros[macro].m_strName).arg(macro));
        ConfigureMemberAction(action, command, document, *macroMenu);
    }
}

void ShowMemberContext(int x, int y)
{
    CChatDoc* document = GetChatDoc();
    if (!mousedPui || !document) return;
    QMenu menu;
    const bool administrator = g_puiSelf && g_puiSelf->IsOperator();
    LoadMemberMenu(menu,
                   administrator ? QStringLiteral("IDR_MEMBERADMIN")
                                 : QStringLiteral("IDR_IRC_MEMBER"),
                   document);
    document->UpdateComicCharacterMenu(&menu);
    AddMacroMenu(menu);
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->ConfigureContextMenu(&menu);
    if (!menu.actions().isEmpty()) menu.exec(QPoint(x, y));
}

CMemberList::CMemberList(QWidget* parent)
    : QWidget(parent)
    , m_MemberListBox(new CMemberListCtrl(this))
    , m_list(m_MemberListBox)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setMovement(QListView::Static);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setWrapping(true);
    m_list->setWordWrap(false);
    QPalette listPalette = m_list->palette();
    listPalette.setColor(QPalette::Base, QColor(255, 255, 255));
    listPalette.setColor(QPalette::Text, QColor(0, 0, 0));
    m_list->setPalette(listPalette);
    m_list->viewport()->installEventFilter(this);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) {
        CChatDoc* document = DocumentForMemberList(this);
        CUserInfo* pui = document
            ? document->GetSingleSelectedMember() : nullptr;
        if (!document || !document->m_proto || !pui) return;
        if (pui->IsComicUser()) {
            document->m_proto->ChatGetInfo(pui);
        } else {
            QString message = originalResourceString(
                QStringLiteral("IDS_NOTCOMICSUSER"));
            message.replace(QStringLiteral("%1"), pui->GetScreenName());
            QMessageBox::information(this, QString(), message);
        }
    });
}

void CMemberList::AddUser(CUserInfo* pui)
{
    if (!pui) {
        return;
    }
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).value<void*>() == pui) {
            UpdateListItem(m_list->item(i), pui, m_iconMode);
            Sort();
            return;
        }
    }
    auto* item = new QListWidgetItem(pui->GetScreenName());
    item->setData(UserPointerRole,
                  QVariant::fromValue(static_cast<void*>(pui)));
    UpdateListItem(item, pui, m_iconMode);
    m_list->addItem(item);
    Sort();
}

void CMemberList::RemoveUser(CUserInfo* pui)
{
    if (!pui) return;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).value<void*>() == pui) {
            delete m_list->takeItem(i);
            return;
        }
    }
}

void CMemberList::Clear()
{
    m_list->clear();
}

void CMemberList::Sort()
{
    QSet<CUserInfo*> selected;
    for (QListWidgetItem* item : m_list->selectedItems()) {
        selected.insert(static_cast<CUserInfo*>(
            item->data(Qt::UserRole).value<void*>()));
    }
    CUserInfo* current = currentUser();
    QList<QListWidgetItem*> items;
    while (m_list->count() > 0) {
        items.append(m_list->takeItem(0));
    }
    std::stable_sort(items.begin(), items.end(), [](QListWidgetItem* a, QListWidgetItem* b) {
        auto* pa = static_cast<CUserInfo*>(a->data(Qt::UserRole).value<void*>());
        auto* pb = static_cast<CUserInfo*>(b->data(Qt::UserRole).value<void*>());
        if (!pa || !pb) {
            return a->text() < b->text();
        }
        const int sa = GetSort(pa);
        const int sb = GetSort(pb);
        if (sa != sb) {
            return sa < sb;
        }
        return SortName(pa).compare(SortName(pb), Qt::CaseInsensitive) < 0;
    });
    for (QListWidgetItem* item : items) {
        auto* pui = static_cast<CUserInfo*>(item->data(Qt::UserRole).value<void*>());
        if (pui) {
            UpdateListItem(item, pui, m_iconMode);
        }
        m_list->addItem(item);
        item->setSelected(selected.contains(pui));
        if (pui == current)
            m_list->setCurrentItem(item, QItemSelectionModel::NoUpdate);
    }
}

void CMemberList::SetIconMode(bool iconMode)
{
    m_iconMode = iconMode;
    m_list->setViewMode(iconMode ? QListView::IconMode : QListView::ListMode);
    m_list->setIconSize(iconMode
        ? QSize(MEMBER_STATUS_SIZE + MEMBER_ICON_GAP + MEMBER_ICON_SIZE,
                MEMBER_ICON_SIZE)
        : QSize(MEMBER_STATUS_SIZE, MEMBER_STATUS_SIZE));
    for (int index = 0; index < m_list->count(); ++index) {
        QListWidgetItem* item = m_list->item(index);
        auto* pui = static_cast<CUserInfo*>(
            item->data(Qt::UserRole).value<void*>());
        UpdateListItem(item, pui, m_iconMode);
    }
}

int CMemberList::count() const
{
    return m_list->count();
}

CUserInfo* CMemberList::currentUser() const
{
    QListWidgetItem* item = m_list->currentItem();
    return item ? static_cast<CUserInfo*>(item->data(Qt::UserRole).value<void*>()) : nullptr;
}

QList<CUserInfo*> CMemberList::selectedUsers() const
{
    QList<CUserInfo*> selections;
    const QList<QListWidgetItem*> selected = m_list->selectedItems();
    for (QListWidgetItem* item : selected) {
        auto* pui = static_cast<CUserInfo*>(item->data(Qt::UserRole).value<void*>());
        if (pui && pui != g_puiSelf) {
            selections.append(pui);
        }
    }
    return selections;
}

CUserInfo* CMemberList::GetNextSelectedMember(int& index) const
{
    for (++index; index < m_list->count(); ++index) {
        QListWidgetItem* item = m_list->item(index);
        if (item && item->isSelected()) {
            return static_cast<CUserInfo*>(
                item->data(Qt::UserRole).value<void*>());
        }
    }
    return nullptr;
}

int CMemberList::SelectedMemberCount() const
{
    return m_list->selectedItems().size();
}

QWidget* CMemberList::FocusWidget() const
{
    return m_list;
}

void CMemberList::EnsureFocusItem()
{
    if (m_list->currentRow() < 0 && m_list->count() > 0) m_list->setCurrentRow(0);
}

void CMemberList::MakeVisible(CUserInfo* pui)
{
    if (!pui) return;
    for (int index = 0; index < m_list->count(); ++index) {
        QListWidgetItem* item = m_list->item(index);
        if (item && item->data(Qt::UserRole).value<void*>() == pui) {
            m_list->scrollToItem(item, QAbstractItemView::EnsureVisible);
            return;
        }
    }
}

void CMemberList::OnContextMenu(const QPoint& listPoint,
                                const QPoint& globalPoint, BOOL keyboard)
{
    CChatDoc* document = DocumentForMemberList(this);
    if (!document) return;
    if (keyboard && SelectedMemberCount() > 1) {
        QApplication::beep();
        return;
    }

    QListWidgetItem* item = nullptr;
    if (keyboard) {
        for (INT index = 0; index < m_list->count(); ++index) {
            if (m_list->item(index)->isSelected()) {
                item = m_list->item(index);
                break;
            }
        }
    } else {
        item = m_list->itemAt(listPoint);
    }
    QMenu menu(this);
    if (!item) {
        if (!document->m_bComicView) return;
        LoadMemberMenu(menu, QStringLiteral("IDR_MEMBERCONTEXT"), document);
    } else {
        mousedPui = static_cast<CUserInfo*>(
            item->data(Qt::UserRole).value<void*>());
        ShowMemberContext(globalPoint.x(), globalPoint.y());
        return;
    }
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->ConfigureContextMenu(&menu);
    if (!menu.actions().isEmpty()) menu.exec(globalPoint);
}

void CMemberList::contextMenuEvent(QContextMenuEvent* event)
{
    const BOOL keyboard = event->reason() == QContextMenuEvent::Keyboard;
    const QPoint globalPoint = keyboard
        ? mapToGlobal(rect().center()) : event->globalPos();
    const QPoint listPoint = m_list->viewport()->mapFromGlobal(globalPoint);
    OnContextMenu(listPoint, globalPoint, keyboard);
    event->accept();
}

bool CMemberList::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_list->viewport()
        && event->type() == QEvent::ContextMenu) {
        auto* contextEvent = static_cast<QContextMenuEvent*>(event);
        const BOOL keyboard = contextEvent->reason()
            == QContextMenuEvent::Keyboard;
        const QPoint globalPoint = keyboard
            ? mapToGlobal(rect().center()) : contextEvent->globalPos();
        OnContextMenu(contextEvent->pos(), globalPoint, keyboard);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ForwardToSayWnd(unsigned int character)
{
    CChatDoc* document = GetChatDoc();
    if (!document || !document->m_sayWnd) return;
    document->SetFocusToSayWnd();
    QWidget* focus = QApplication::focusWidget();
    if (!focus || character > 0xffffU) return;
    const QString text(QChar(static_cast<ushort>(character)));
    QKeyEvent keyEvent(QEvent::KeyPress, 0, Qt::NoModifier, text);
    QApplication::sendEvent(focus, &keyEvent);
}

void GetSelectedPuis(QList<CUserInfo*>& selections)
{
    selections.clear();
    CChatDoc* document = GetChatDoc();
    if (!document || !document->m_memberList) return;
    auto* memberList = qobject_cast<QListWidget*>(
        document->m_memberList->FocusWidget());
    if (!memberList) return;

    INT items = 0;
    for (INT index = 0; index < memberList->count(); ++index) {
        QListWidgetItem* item = memberList->item(index);
        if (!item || !item->isSelected()) continue;
        auto* pui = item ? static_cast<CUserInfo*>(
            item->data(CMemberList::UserPointerRole).value<void*>()) : nullptr;
        if (pui && pui != g_puiSelf) selections.append(pui);
        if (!(items++ < 10)) break;
    }
}

void UpdateSpectators(CChatDoc* doc, BOOL moderated)
{
    if (!doc || !doc->m_memberList || !doc->m_memberList->m_list) return;
    QListWidget* members = doc->m_memberList->m_list;
    for (int index = 0; index < members->count(); ++index) {
        QListWidgetItem* item = members->item(index);
        auto* pui = static_cast<CUserInfo*>(
            item->data(Qt::UserRole).value<void*>());
        if (!pui) continue;
        pui->SetFlag(UF_SPECTATOR,
                     !pui->IsOperator() && moderated
                         && !pui->CheckFlag(UF_HASVOICE));
        UpdateListItem(item, pui, doc->m_memberList->m_iconMode);
    }
    members->viewport()->update();
}
