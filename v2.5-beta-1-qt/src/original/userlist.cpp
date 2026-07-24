// Ported from v2.5-beta-1-modern/userlist.cpp.
// Qt widgets replace the MFC controls; WHO result ownership, filtering,
// sorting and action conditions remain in the original module.

#include "userlist.h"

#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "originalassets.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QFocusEvent>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QShowEvent>
#include <QTime>
#include <QTimer>

#include <algorithm>

namespace {
class DialogUnitMapper {
public:
    explicit DialogUnitMapper(const QFont& font)
    {
        const QFontMetrics metrics(font);
        const QString alphabet = QStringLiteral(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        m_baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
        m_baseY = qMax(1, metrics.height());
    }

    int x(int dlu) const { return (dlu * m_baseX + 2) / 4; }
    int y(int dlu) const { return (dlu * m_baseY + 4) / 8; }
    QRect rect(const OriginalDialogControl& control) const
    {
        return {x(control.x), y(control.y),
                x(control.width), y(control.height)};
    }

private:
    int m_baseX = 1;
    int m_baseY = 1;
};

const OriginalDialogControl* findControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

void placeControl(QWidget* widget, const OriginalDialogResource& dialog,
                  const DialogUnitMapper& mapper, const QString& identifier,
                  int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = findControl(
            dialog, identifier, occurrence)) {
        widget->setGeometry(mapper.rect(*control));
        widget->setVisible(control->visible);
    }
}

QFont resourceFont(const OriginalDialogResource& dialog)
{
    QFont font(dialog.fontFamily);
    if (dialog.fontPointSize > 0) font.setPointSize(dialog.fontPointSize);
    return font;
}

int compareNoCase(const QString& left, const QString& right)
{
    const int value = QString::compare(left, right, Qt::CaseInsensitive);
    return value < 0 ? -1 : value > 0 ? 1 : 0;
}

QString trimQuotes(QString value)
{
    if (value.size() >= 2 && value.front() == QLatin1Char('"')
        && value.back() == QLatin1Char('"')) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}
}

void CUser::Release()
{
    if (m_nRefCount <= 0) return;
    if (--m_nRefCount == 0) delete this;
}

CUserListPersist::CUserListPersist()
{
    m_users.reserve(2000);
}

CUserListPersist::~CUserListPersist()
{
    MakeEmpty();
}

void CUserListPersist::MakeEmpty()
{
    for (CUser* user : m_users) user->Release();
    m_users.clear();
    m_nUsers = 0;
    m_usersSize = 0;
}

void CUserListPersist::Reset()
{
    MakeEmpty();
    m_cachedServer.clear();
    m_strUserFilter.clear();
    m_strRoomFilter.clear();
    m_strEncRoom.clear();
    m_searchType = USERSEARCH_NICK;
    m_sortAscending = TRUE;
    m_sortColumn = 0;
    m_searchTime.clear();
}

int CUserListPersist::AddUser(CUser* user)
{
    if (!user) return -1;
    if (m_nUsers >= m_usersSize) {
        m_usersSize += 2000;
        m_users.reserve(m_usersSize);
    }
    const int result = m_nUsers;
    m_users.append(user);
    ++m_nUsers;
    return result;
}

void CUserListPersist::Sort()
{
    const int column = m_sortColumn;
    const BOOL ascending = m_sortAscending;
    std::sort(m_users.begin(), m_users.end(),
              [column, ascending](const CUser* left, const CUser* right) {
        const QString leftNick = trimQuotes(left->GetPrettyNick());
        const QString rightNick = trimQuotes(right->GetPrettyNick());
        int result = 0;
        if (column == 0) result = compareNoCase(leftNick, rightNick);
        else if (column == 1)
            result = compareNoCase(left->m_strIdentity, right->m_strIdentity);
        else if (column == 2)
            result = compareNoCase(left->m_strFullName, right->m_strFullName);
        else if (column == 3)
            result = compareNoCase(left->m_strPrettyRoom,
                                   right->m_strPrettyRoom);
        if (!result && column != 0)
            result = compareNoCase(leftNick, rightNick);
        if (!ascending) result = -result;
        return result < 0;
    });
}

CUserListCtrl::CUserListCtrl(CUserList* parent)
    : QTreeWidget(parent)
    , m_userList(parent)
{
    setColumnCount(4);
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSortingEnabled(false);
    header()->setSectionsClickable(true);
    header()->setStretchLastSection(false);
    QObject::connect(header(), &QHeaderView::sectionClicked, this,
                     [this](int column) {
        CUserListPersist* persist = m_userList->m_persist;
        if (persist->m_sortColumn == column) {
            persist->m_sortAscending = !persist->m_sortAscending;
        } else {
            persist->m_sortColumn = column;
            persist->m_sortAscending = TRUE;
        }
        m_userList->Sort(TRUE);
    });
}

CUser* CUserListCtrl::GetSelectedUser() const
{
    const QList<QTreeWidgetItem*> selected = selectedItems();
    if (selected.size() != 1 || !m_userList || !m_userList->m_persist)
        return nullptr;
    const int index = selected.first()->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= m_userList->m_persist->m_users.size())
        return nullptr;
    return m_userList->m_persist->m_users.at(index);
}

QString CUserListCtrl::GetSelectedNickname() const
{
    CUser* user = GetSelectedUser();
    return user ? user->m_strNickname : QString();
}

void CUserListCtrl::focusInEvent(QFocusEvent* event)
{
    if (!currentItem() && topLevelItemCount() > 0) {
        setCurrentItem(topLevelItem(0), 0,
                       QItemSelectionModel::NoUpdate);
    }
    QTreeWidget::focusInEvent(event);
}

CUserList::CUserList(CUserListPersist* persist, QWidget* parent)
    : QDialog(parent)
    , m_persist(persist)
    , m_strUser(persist ? persist->m_strUserFilter : QString())
    , m_strRoom(persist ? persist->m_strRoomFilter : QString())
{
    const QString resourceName = QStringLiteral("IDD_USERLIST");
    const OriginalDialogResource dialog = originalDialogResource(resourceName);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    m_group = new QGroupBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_GROUP0")), this);
    placeControl(m_group, dialog, mapper, QStringLiteral("IDC_GROUP0"));

    const auto radio = [&](const QString& identifier) {
        auto* result = new QRadioButton(originalDialogControlText(
            resourceName, identifier), this);
        placeControl(result, dialog, mapper, identifier);
        return result;
    };
    m_searchAll = radio(QStringLiteral("IDC_USERSEARCH_ALL"));
    m_searchNick = radio(QStringLiteral("IDC_USERSEARCH_NICK"));
    m_searchIdentity = radio(QStringLiteral("IDC_USERSEARCH_IDENTITY"));
    m_searchRoom = radio(QStringLiteral("IDC_USERSEARCH_ROOM"));

    m_searchLabel = new QLabel(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SEARCH_LABEL")), this);
    placeControl(m_searchLabel, dialog, mapper,
                 QStringLiteral("IDC_SEARCH_LABEL"));
    m_roomLabel = new QLabel(originalDialogControlText(
        resourceName, QStringLiteral("IDC_ROOM_LABEL")), this);
    placeControl(m_roomLabel, dialog, mapper, QStringLiteral("IDC_ROOM_LABEL"));
    m_user = new QLineEdit(this);
    m_ctlRoom = new QLineEdit(this);
    placeControl(m_user, dialog, mapper, QStringLiteral("IDC_SEARCH_EDIT"));
    placeControl(m_ctlRoom, dialog, mapper, QStringLiteral("IDC_ROOM_EDIT"));
    m_user->setMaxLength(100);
    m_ctlRoom->setMaxLength(MAX_IRCXCHANNAME);
    m_user->setText(m_strUser);
    m_ctlRoom->setText(m_strRoom);

    m_userListCtrl = new CUserListCtrl(this);
    placeControl(m_userListCtrl, dialog, mapper,
                 QStringLiteral("IDC_USERLIST"));
    m_userListCtrl->setHeaderLabels({
        originalResourceString(QStringLiteral("ID_UL_NICK_LABEL")),
        originalResourceString(QStringLiteral("ID_UL_IDENT_LABEL")),
        originalResourceString(QStringLiteral("ID_UL_REALNAME_LABEL")),
        originalResourceString(QStringLiteral("ID_UL_ROOM_LABEL"))});
    m_userListCtrl->setColumnWidth(0, originalResourceString(
        QStringLiteral("ID_UL_NICK_WIDTH")).toInt());
    m_userListCtrl->setColumnWidth(1, originalResourceString(
        QStringLiteral("ID_UL_IDENT_WIDTH")).toInt());
    m_userListCtrl->setColumnWidth(2, originalResourceString(
        QStringLiteral("ID_UL_REALNAME_WIDTH")).toInt());
    m_userListCtrl->setColumnWidth(3, originalResourceString(
        QStringLiteral("ID_UL_ROOM_WIDTH")).toInt());

    const auto button = [&](const QString& identifier) {
        auto* result = new QPushButton(originalDialogControlText(
            resourceName, identifier), this);
        placeControl(result, dialog, mapper, identifier);
        return result;
    };
    m_reset = button(QStringLiteral("IDC_RESET_LIST"));
    m_reset->setDefault(true);
    m_invite = button(QStringLiteral("IDC_INVITE_FROM_LIST"));
    m_message = button(QStringLiteral("IDC_MESSAGE_FROM_LIST"));
    m_join = button(QStringLiteral("IDC_JOINROOM"));
    auto* close = button(QStringLiteral("IDC_CLOSE_USERLIST"));
    m_ctlCaption = new QLabel(this);
    placeControl(m_ctlCaption, dialog, mapper,
                 QStringLiteral("IDC_ROOM_CAPTION"));
    m_searchTime = new QLabel(this);
    m_searchTime->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    placeControl(m_searchTime, dialog, mapper,
                 QStringLiteral("IDC_SEARCH_TIME"));

    connect(m_reset, &QPushButton::clicked, this, &CUserList::OnResetList);
    connect(m_invite, &QPushButton::clicked,
            this, &CUserList::OnInviteFromList);
    connect(m_message, &QPushButton::clicked,
            this, &CUserList::OnMessageFromList);
    connect(m_join, &QPushButton::clicked, this, &CUserList::OnJoinRoom);
    connect(close, &QPushButton::clicked, this, &CUserList::OnCloseDialog);
    connect(m_searchAll, &QRadioButton::clicked,
            this, &CUserList::OnUsersearchAll);
    connect(m_searchNick, &QRadioButton::clicked,
            this, &CUserList::OnUsersearchNick);
    connect(m_searchIdentity, &QRadioButton::clicked,
            this, &CUserList::OnUsersearchIdentity);
    connect(m_searchRoom, &QRadioButton::clicked,
            this, &CUserList::OnUsersearchRoom);
    connect(m_ctlRoom, &QLineEdit::textChanged,
            this, &CUserList::OnChangeRoomEdit);
    connect(m_userListCtrl, &QTreeWidget::itemSelectionChanged,
            this, &CUserList::OnItemchangedUserlist);

    installEventFilter(this);
    for (QWidget* control : findChildren<QWidget*>())
        control->installEventFilter(this);
}

CUserList::~CUserList()
{
    delete m_selUser;
}

void CUserList::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) initializeDialog();
    QDialog::showEvent(event);
}

void CUserList::initializeDialog()
{
    m_bInitialized = true;
    const QString server = QString::fromUtf8(GetMyServer());
    if (!m_persist->m_strQuery.isEmpty()
        || !m_persist->m_strEncRoom.isEmpty()) {
        OnResetList();
    } else if (m_persist->m_cachedServer != server) {
        m_persist->Reset();
    }
    m_persist->m_cachedServer = server;
    if (!m_persist->m_strQuery.isEmpty()) {
        setWindowTitle(windowTitle() + QStringLiteral(": ")
                       + m_persist->m_strQuery.trimmed());
    }
    Load(FALSE);
    if (m_persist->m_searchType == USERSEARCH_NICK
        || m_persist->m_searchType == USERSEARCH_ID) {
        m_user->setFocus();
    } else if (m_persist->m_searchType == USERSEARCH_ROOM) {
        m_ctlRoom->setFocus();
    }
}

int CUserList::DoModal()
{
    return exec();
}

void CUserList::OnResetList()
{
    m_persist->m_searchTime = originalResourceString(
        QStringLiteral("IDS_SEARCH_TIME"));
    m_persist->m_searchTime.replace(
        QStringLiteral("%1"),
        QLocale().toString(QTime::currentTime(), QLocale::ShortFormat));
    m_userListCtrl->clear();
    m_persist->MakeEmpty();
    theApp.m_bInSearch = TRUE;
    m_bResetHadFocus = m_reset->hasFocus();
    m_reset->setEnabled(false);
    ChatFillUserList(this);
}

void CUserList::Load(BOOL resetList)
{
    if (resetList) m_userListCtrl->clear();
    m_invite->setEnabled(false);
    m_message->setEnabled(false);
    m_join->setEnabled(false);

    m_searchAll->setChecked(m_persist->m_searchType == USERSEARCH_ALL);
    m_searchNick->setChecked(m_persist->m_searchType == USERSEARCH_NICK);
    m_searchIdentity->setChecked(m_persist->m_searchType == USERSEARCH_ID);
    m_searchRoom->setChecked(m_persist->m_searchType == USERSEARCH_ROOM);

    const BOOL showEdit1 = m_persist->m_searchType == USERSEARCH_NICK
        || m_persist->m_searchType == USERSEARCH_ID;
    const BOOL showEdit2 = m_persist->m_searchType == USERSEARCH_ROOM;
    const BOOL allowChoice = m_persist->m_strQuery.isEmpty();
    ShowAndEnableControl(QStringLiteral("IDC_SEARCH_LABEL"),
                         showEdit1 && allowChoice);
    ShowAndEnableControl(QStringLiteral("IDC_SEARCH_EDIT"),
                         showEdit1 && allowChoice);
    ShowAndEnableControl(QStringLiteral("IDC_ROOM_LABEL"),
                         showEdit2 && allowChoice);
    ShowAndEnableControl(QStringLiteral("IDC_ROOM_EDIT"),
                         showEdit2 && allowChoice);
    m_searchIdentity->setEnabled(allowChoice);
    m_searchNick->setEnabled(allowChoice);
    m_searchAll->setEnabled(allowChoice);
    m_searchRoom->setEnabled(allowChoice);
    m_group->setEnabled(allowChoice);

    for (int index = 0; index < m_persist->m_nUsers; ++index)
        AddToUserList(index);
    AnnounceCount();
    AnnounceTime();
}

void CUserList::ShowAndEnableControl(const QString& identifier,
                                     BOOL showAndEnable)
{
    if (QWidget* control = findChild<QWidget*>(identifier))
        control->setEnabled(showAndEnable);
}

void CUserList::AddToUserList(int index)
{
    if (index < 0 || index >= m_persist->m_users.size()) return;
    CUser* user = m_persist->m_users.at(index);
    auto* item = new QTreeWidgetItem(m_userListCtrl);
    item->setText(0, user->GetPrettyNick());
    item->setText(1, user->m_strIdentity);
    item->setText(2, user->m_strFullName);
    item->setText(3, user->m_strPrettyRoom);
    item->setData(0, Qt::UserRole, index);
}

void CUserList::Sort(BOOL resetList)
{
    m_persist->Sort();
    Load(resetList);
}

void CUserList::AnnounceCount()
{
    QString value = originalResourceString(QStringLiteral("IDS_NUM_USERS"));
    value.replace(QStringLiteral("%d"),
                  QString::number(m_userListCtrl->topLevelItemCount()));
    m_ctlCaption->setText(value);
}

void CUserList::AnnounceTime()
{
    m_searchTime->setText(m_persist->m_searchTime);
}

void CUserList::OnItemchangedUserlist()
{
    CUser* user = m_userListCtrl->GetSelectedUser();
    const QString selectedNick = user ? user->m_strNickname : QString();
    const BOOL isOther = !selectedNick.isEmpty()
        && selectedNick != QString::fromUtf8(GetMyNickName());

    BOOL inCurrentRoom = FALSE;
    CChatDoc* doc = GetChatDoc();
    if (isOther && doc && doc->GetConnectionStatus() == CX_INCHANNEL) {
        CUserInfo* pui = LookupPui(selectedNick, doc);
        inCurrentRoom = pui && !pui->IsDeparted();
    }
    m_invite->setEnabled(isOther && bCanInvite() && !inCurrentRoom);

    m_message->setEnabled(isOther);

    BOOL canJoin = FALSE;
    if (isOther && user && !user->m_strRoom.isEmpty()
        && CHANNELPREFIX(user->m_strRoom.front().toLatin1())) {
        doc = LookupDoc(user->m_strRoom);
        canJoin = !doc || doc->GetConnectionStatus() != CX_INCHANNEL
            || doc != GetChatDoc();
    }
    m_join->setEnabled(canJoin);
}

void CUserList::OnInviteFromList()
{
    if (!currentRoom || currentRoom->GetConnectionStatus() != CX_INCHANNEL) {
        QMessageBox::information(
            this,
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
            originalResourceString(QStringLiteral("IDS_OUTCHANNEL_INVITE")));
        return;
    }
    const QString nick = m_userListCtrl->GetSelectedNickname();
    if (!nick.isEmpty()) currentRoom->ChatSendInvitation(nick);
}

void CUserList::setSearchType(int searchType)
{
    m_persist->m_searchType = searchType;
    const BOOL edit1 = searchType == USERSEARCH_NICK
        || searchType == USERSEARCH_ID;
    const BOOL edit2 = searchType == USERSEARCH_ROOM;
    ShowAndEnableControl(QStringLiteral("IDC_SEARCH_LABEL"), edit1);
    ShowAndEnableControl(QStringLiteral("IDC_SEARCH_EDIT"), edit1);
    ShowAndEnableControl(QStringLiteral("IDC_ROOM_LABEL"), edit2);
    ShowAndEnableControl(QStringLiteral("IDC_ROOM_EDIT"), edit2);
}

void CUserList::OnUsersearchAll() { setSearchType(USERSEARCH_ALL); }
void CUserList::OnUsersearchIdentity() { setSearchType(USERSEARCH_ID); }
void CUserList::OnUsersearchNick() { setSearchType(USERSEARCH_NICK); }
void CUserList::OnUsersearchRoom() { setSearchType(USERSEARCH_ROOM); }

void CUserList::OnMessageFromList()
{
    CUser* user = m_userListCtrl->GetSelectedUser();
    if (user && !m_selUser)
        m_selUser = new CUserInfo(user->m_strNickname, user->m_strIdentity);
    done(LAUNCH_WHISPERBOX);
}

void CUserList::OnJoinRoom()
{
    CUser* user = m_userListCtrl->GetSelectedUser();
    if (!user) return;
    const QString room = user->m_strRoom;
    done(0);
    g_bEnterOnCreate = FALSE;
    bSwitchToRoom(room);
}

void CUserList::OnCloseDialog()
{
    done(0);
}

void CUserList::OnChangeRoomEdit()
{
    m_persist->m_strEncRoom.clear();
}

bool CUserList::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress
        || event->type() == QEvent::KeyRelease) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_F5) {
            if (event->type() == QEvent::KeyPress
                && !keyEvent->isAutoRepeat()) {
                QTimer::singleShot(0, this, &CUserList::OnResetList);
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
