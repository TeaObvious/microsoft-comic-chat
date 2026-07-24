// Ported from v2.5-beta-1-modern/roomlist.cpp.
// Qt widgets replace the MFC controls; data flow, filtering and sorting remain
// in the original module and use only chat.rc resources.

#include "roomlist.h"

#include "chat.h"
#include "ircproto.h"
#include "originalassets.h"
#include "protsupp.h"
#include "setupdlg.h"

#include <QCheckBox>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QIntValidator>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShowEvent>
#include <QTime>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <functional>

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

int compareRoomNames(const CRoom* left, const CRoom* right)
{
    if (left->m_byteSort < right->m_byteSort) return -1;
    if (left->m_byteSort > right->m_byteSort) return 1;
    return compareNoCase(left->m_prettyName, right->m_prettyName);
}

QWidget* createSpinButtons(QWidget* parent,
                           const OriginalDialogResource& dialog,
                           const DialogUnitMapper& mapper,
                           const QString& identifier,
                           const std::function<void(int)>& step)
{
    auto* holder = new QWidget(parent);
    placeControl(holder, dialog, mapper, identifier);
    auto* up = new QToolButton(holder);
    auto* down = new QToolButton(holder);
    up->setArrowType(Qt::UpArrow);
    down->setArrowType(Qt::DownArrow);
    up->setAutoRepeat(true);
    down->setAutoRepeat(true);
    up->setFocusPolicy(Qt::NoFocus);
    down->setFocusPolicy(Qt::NoFocus);
    const int half = qMax(1, holder->height() / 2);
    up->setGeometry(0, 0, holder->width(), half);
    down->setGeometry(0, half, holder->width(), holder->height() - half);
    QObject::connect(up, &QToolButton::clicked, holder, [step] { step(1); });
    QObject::connect(down, &QToolButton::clicked, holder, [step] { step(-1); });
    return holder;
}
}

CRoomListPersist::CRoomListPersist()
{
    m_rooms.reserve(2000);
}

CRoomListPersist::~CRoomListPersist()
{
    MakeEmpty();
}

void CRoomListPersist::MakeEmpty()
{
    qDeleteAll(m_rooms);
    m_rooms.clear();
    m_nRooms = 0;
    m_roomsSize = 0;
}

void CRoomListPersist::Reset()
{
    MakeEmpty();
    m_cachedServer.clear();
    m_strTopicFilter.clear();
    m_bSearchDescrs = FALSE;
    m_bRegisteredOnly = theApp.m_bListRegistered;
    m_minMembers = 0;
    m_maxMembers = 9999;
    m_sortAscending = TRUE;
    m_sortColumn = 0;
    m_searchTime.clear();
}

int CRoomListPersist::AddRoom(CRoom* room)
{
    if (!room) return -1;
    if (m_nRooms >= m_roomsSize) {
        m_roomsSize += 2000;
        m_rooms.reserve(m_roomsSize);
    }
    const int result = m_nRooms;
    m_rooms.append(room);
    ++m_nRooms;
    return result;
}

void CRoomListPersist::SortRooms()
{
    const int column = m_sortColumn;
    const BOOL ascending = m_sortAscending;
    std::sort(m_rooms.begin(), m_rooms.end(),
              [column, ascending](const CRoom* left, const CRoom* right) {
        int result = 0;
        if (column == 0) {
            result = compareRoomNames(left, right);
        } else if (column == 1) {
            if (left->m_nUsers < right->m_nUsers) result = -1;
            else if (left->m_nUsers > right->m_nUsers) result = 1;
            if (!ascending) result = -result;
            if (!result) {
                result = compareNoCase(left->m_prettyName,
                                       right->m_prettyName);
            }
            return result < 0;
        } else if (column == 2) {
            result = compareNoCase(left->m_descr, right->m_descr);
        }
        if (!ascending) result = -result;
        if (!result) result = compareRoomNames(left, right);
        return result < 0;
    });
}

void CRoom::CalculateSortByte()
{
    int result = 0;
    QString name = m_prettyName;
    if (!name.isEmpty()
        && (name.front() == QLatin1Char('#')
            || name.front() == QLatin1Char('&'))) {
        result = 2;
        name.remove(0, 1);
    }
    if (!name.isEmpty()) {
        const ushort value = name.front().unicode();
        if (value < 128
            && !((value >= 'A' && value <= 'Z')
                 || (value >= 'a' && value <= 'z'))) {
            ++result;
        }
    }
    m_byteSort = static_cast<BYTE>(result);
}

CRoomListCtrl::CRoomListCtrl(CRoomList* parent)
    : QTreeWidget(parent)
    , m_roomList(parent)
{
    setColumnCount(3);
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
        CRoomListPersist* persist = m_roomList->m_persist;
        if (persist->m_sortColumn == column) {
            persist->m_sortAscending = !persist->m_sortAscending;
        } else {
            persist->m_sortColumn = column;
            persist->m_sortAscending = TRUE;
        }
        m_roomList->SortRooms(TRUE);
    });
}

CRoom* CRoomListCtrl::GetSelectedRoom() const
{
    const QList<QTreeWidgetItem*> selected = selectedItems();
    if (selected.size() != 1 || !m_roomList || !m_roomList->m_persist)
        return nullptr;
    QTreeWidgetItem* focused = currentItem();
    if (!focused) return nullptr;
    const int index = focused->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= m_roomList->m_persist->m_rooms.size())
        return nullptr;
    return m_roomList->m_persist->m_rooms.at(index);
}

void CRoomListCtrl::focusInEvent(QFocusEvent* event)
{
    if (!currentItem() && topLevelItemCount() > 0) {
        setCurrentItem(topLevelItem(0), 0,
                       QItemSelectionModel::NoUpdate);
    }
    QTreeWidget::focusInEvent(event);
}

CRoomList::CRoomList(CRoomListPersist* persist, QWidget* parent)
    : QDialog(parent)
    , m_persist(persist)
{
    const QString resourceName = QStringLiteral("IDD_ROOMLIST");
    const OriginalDialogResource dialog = originalDialogResource(resourceName);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.type != QLatin1String("LTEXT")
            && control.type != QLatin1String("RTEXT")
            && control.type != QLatin1String("CTEXT")) {
            continue;
        }
        if (control.identifier == QLatin1String("IDC_ROOM_CAPTION")
            || control.identifier == QLatin1String("IDC_SEARCH_TIME")) {
            continue;
        }
        auto* label = new QLabel(control.text, this);
        label->setObjectName(control.identifier);
        label->setGeometry(mapper.rect(control));
        if (control.type == QLatin1String("RTEXT"))
            label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        else if (control.type == QLatin1String("CTEXT"))
            label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    }

    m_topicEdit = new QLineEdit(this);
    placeControl(m_topicEdit, dialog, mapper, QStringLiteral("IDC_TOPIC_EDIT"));
    // The original control is CFilterEdit with its default NOSPC and comma
    // filters. Invalid typed or pasted input leaves the preceding value.
    m_topicEdit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[^\\s,]*")), m_topicEdit));
    m_ctrlSearchDescrs = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SEARCH_DESCRS")), this);
    placeControl(m_ctrlSearchDescrs, dialog, mapper,
                 QStringLiteral("IDC_SEARCH_DESCRS"));
    m_registeredOnly = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_REGISTERED_ONLY")), this);
    placeControl(m_registeredOnly, dialog, mapper,
                 QStringLiteral("IDC_REGISTERED_ONLY"));

    m_minMembersEdit = new QLineEdit(this);
    m_maxMembersEdit = new QLineEdit(this);
    placeControl(m_minMembersEdit, dialog, mapper,
                 QStringLiteral("IDC_MIN_MEMBERS"));
    placeControl(m_maxMembersEdit, dialog, mapper,
                 QStringLiteral("IDC_MAX_MEMBERS"));
    m_minMembersEdit->setMaxLength(4);
    m_maxMembersEdit->setMaxLength(4);
    m_minMembersEdit->setValidator(new QIntValidator(0, 9999,
                                                     m_minMembersEdit));
    m_maxMembersEdit->setValidator(new QIntValidator(0, 9999,
                                                     m_maxMembersEdit));
    createSpinButtons(this, dialog, mapper, QStringLiteral("IDC_SPIN_MIN"),
                      [this](int delta) {
        const int value = qBound(0, m_minMembersEdit->text().toInt() + delta,
                                 9999);
        m_minMembersEdit->setText(QString::number(value));
    });
    createSpinButtons(this, dialog, mapper, QStringLiteral("IDC_SPIN_MAX"),
                      [this](int delta) {
        const int value = qBound(0, m_maxMembersEdit->text().toInt() + delta,
                                 9999);
        m_maxMembersEdit->setText(QString::number(value));
    });

    m_roomList = new CRoomListCtrl(this);
    placeControl(m_roomList, dialog, mapper, QStringLiteral("IDC_ROOMLIST"));
    m_roomList->setHeaderLabels({
        originalResourceString(QStringLiteral("ID_RL_ROOM_LABEL")),
        originalResourceString(QStringLiteral("ID_RL_NUSERS_LABEL")),
        originalResourceString(QStringLiteral("ID_RL_DESCR_LABEL"))});
    m_roomList->setColumnWidth(0, originalResourceString(
        QStringLiteral("ID_RL_ROOM_WIDTH")).toInt());
    m_roomList->setColumnWidth(1, originalResourceString(
        QStringLiteral("ID_RL_NUSERS_WIDTH")).toInt());
    m_roomList->setColumnWidth(2, originalResourceString(
        QStringLiteral("ID_RL_DESCR_WIDTH")).toInt());

    const auto button = [&](const QString& identifier) {
        auto* result = new QPushButton(originalDialogControlText(
            resourceName, identifier), this);
        placeControl(result, dialog, mapper, identifier);
        return result;
    };
    m_reset = button(QStringLiteral("IDC_RESET_LIST"));
    m_listMembers = button(QStringLiteral("IDC_LISTMEMBERS"));
    m_goto = button(QStringLiteral("IDC_GOTO"));
    m_goto->setDefault(true);
    auto* createRoom = button(QStringLiteral("IDC_NEWROOM"));
    auto* close = button(QStringLiteral("IDC_CLOSE_ROOMLIST"));
    m_ctlCaption = new QLabel(this);
    placeControl(m_ctlCaption, dialog, mapper,
                 QStringLiteral("IDC_ROOM_CAPTION"));
    m_searchTime = new QLabel(this);
    m_searchTime->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    placeControl(m_searchTime, dialog, mapper,
                 QStringLiteral("IDC_SEARCH_TIME"));

    connect(m_reset, &QPushButton::clicked, this, &CRoomList::OnResetList);
    connect(m_listMembers, &QPushButton::clicked,
            this, &CRoomList::OnListmembers);
    connect(m_goto, &QPushButton::clicked, this, &CRoomList::OnGoto);
    connect(createRoom, &QPushButton::clicked,
            this, &CRoomList::OnCreateRoom);
    connect(close, &QPushButton::clicked, this, &CRoomList::OnCloseDialog);
    connect(m_ctrlSearchDescrs, &QCheckBox::clicked,
            this, &CRoomList::OnSearchDescrs);
    connect(m_registeredOnly, &QCheckBox::clicked,
            this, &CRoomList::OnRegisteredOnly);
    connect(m_topicEdit, &QLineEdit::textChanged,
            this, &CRoomList::OnChangeTopicEdit);
    connect(m_minMembersEdit, &QLineEdit::textChanged,
            this, &CRoomList::OnChangeMinMembers);
    connect(m_maxMembersEdit, &QLineEdit::textChanged,
            this, &CRoomList::OnChangeMaxMembers);
    connect(m_roomList, &QTreeWidget::itemDoubleClicked,
            this, [this] { OnGoto(); });
    connect(m_roomList, &QTreeWidget::itemSelectionChanged, this, [this] {
        const bool selected = m_roomList->selectedItems().size() == 1;
        m_goto->setEnabled(selected);
        m_listMembers->setEnabled(selected);
    });

    installEventFilter(this);
    for (QWidget* control : findChildren<QWidget*>())
        control->installEventFilter(this);
}

void CRoomList::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) initializeDialog();
    QDialog::showEvent(event);
}

void CRoomList::initializeDialog()
{
    m_bInitialized = true;
    const QString server = QString::fromUtf8(GetMyServer());
    if (m_persist->m_cachedServer != server) m_persist->Reset();

    m_strTopic = m_persist->m_strTopicFilter;
    m_bSearchDescrs = m_persist->m_bSearchDescrs;
    m_bRegisteredOnly = m_persist->m_bRegisteredOnly
        && serverConn.m_bIrcXServer;
    m_minMembers = m_persist->m_minMembers;
    m_maxMembers = m_persist->m_maxMembers;

    m_loadingControls = true;
    m_topicEdit->setText(m_strTopic);
    m_ctrlSearchDescrs->setChecked(m_bSearchDescrs);
    m_registeredOnly->setChecked(m_bRegisteredOnly);
    m_minMembersEdit->setText(QString::number(m_minMembers));
    m_maxMembersEdit->setText(QString::number(m_maxMembers));
    m_loadingControls = false;
    m_registeredOnly->setEnabled(serverConn.m_bIrcXServer);

    if (m_persist->m_cachedServer != server) {
        OnResetList();
        m_persist->m_cachedServer = server;
    } else {
        LoadRooms(FALSE);
    }

    if (!m_persist->m_strQuery.isEmpty()) {
        setWindowTitle(windowTitle() + QStringLiteral(": ")
                       + m_persist->m_strQuery.trimmed());
    }
}

int CRoomList::DoModal()
{
    const int result = exec();
    storeFilterState();
    return result;
}

void CRoomList::storeFilterState()
{
    m_persist->m_strTopicFilter = m_strTopic;
    m_persist->m_bSearchDescrs = m_bSearchDescrs;
    m_persist->m_bRegisteredOnly = m_bRegisteredOnly;
    m_persist->m_minMembers = m_minMembers;
    m_persist->m_maxMembers = m_maxMembers;
}

void CRoomList::OnResetList()
{
    m_persist->m_searchTime = originalResourceString(
        QStringLiteral("IDS_SEARCH_TIME"));
    m_persist->m_searchTime.replace(
        QStringLiteral("%1"),
        QLocale().toString(QTime::currentTime(), QLocale::ShortFormat));
    m_roomList->clear();
    m_persist->MakeEmpty();
    theApp.m_bInSearch = TRUE;
    m_bResetHadFocus = m_reset->hasFocus();
    m_reset->setEnabled(false);
    m_goto->setEnabled(false);
    m_listMembers->setEnabled(false);
    ChatFillRoomList(this);
}

void CRoomList::ClearRoomList()
{
    m_roomList->clear();
}

void CRoomList::AddToRoomList(int roomIndex)
{
    if (roomIndex < 0 || roomIndex >= m_persist->m_rooms.size()) return;
    CRoom* room = m_persist->m_rooms.at(roomIndex);
    if (!MatchesTopicFilter(room, m_strTopic, m_bSearchDescrs)) return;
    auto* item = new QTreeWidgetItem(m_roomList);
    item->setText(0, room->m_prettyName);
    item->setText(1, QString::number(room->m_nUsers));
    item->setText(2, room->m_descr);
    item->setData(0, Qt::UserRole, roomIndex);
}

void CRoomList::LoadRooms(BOOL resetList)
{
    if (resetList) m_roomList->clear();
    for (int index = 0; index < m_persist->m_nRooms; ++index)
        AddToRoomList(index);
    AnnounceCount();
    AnnounceTime();
    m_goto->setEnabled(false);
    m_listMembers->setEnabled(false);
}

void CRoomList::SortRooms(BOOL resetList)
{
    m_persist->SortRooms();
    LoadRooms(resetList);
}

void CRoomList::FilterByTopic(const QString& topic)
{
    m_strTopic = topic;
    LoadRooms(TRUE);
}

BOOL CRoomList::MatchesTopicFilter(CRoom* room,
                                   const QString& searchTopic,
                                   BOOL searchDescrs) const
{
    if (!room) return FALSE;
    if (room->m_nUsers > m_maxMembers || room->m_nUsers < m_minMembers)
        return FALSE;
    if (m_bRegisteredOnly && !room->m_byteRegistered) return FALSE;
    if (m_strTopic.isEmpty()) return TRUE;
    if (room->m_prettyName.contains(searchTopic, Qt::CaseInsensitive))
        return TRUE;
    return searchDescrs
        && room->m_descr.contains(searchTopic, Qt::CaseInsensitive);
}

void CRoomList::AnnounceCount()
{
    QString label = originalResourceString(QStringLiteral("IDS_NUM_ROOMS"));
    label.replace(QStringLiteral("%1"),
                  QString::number(m_roomList->topLevelItemCount()));
    m_ctlCaption->setText(label);
}

void CRoomList::AnnounceTime()
{
    m_searchTime->setText(m_persist->m_searchTime);
}

CRoom* CRoomList::GetSelectedRoom() const
{
    return m_roomList->GetSelectedRoom();
}

void CRoomList::OnGoto()
{
    CRoom* room = GetSelectedRoom();
    if (!room) return;
    const QString name = room->m_name;
    done(0);
    g_bEnterOnCreate = FALSE;
    bSwitchToRoom(name);
}

void CRoomList::OnListmembers()
{
    CRoom* room = GetSelectedRoom();
    if (!room) return;
    m_listMembers->setEnabled(false);
    ListMembers(room->m_name, room->m_prettyName);
}

void CRoomList::ReenableListMembers()
{
    m_listMembers->setEnabled(true);
    m_reset->setFocus();
    focusNextChild();
}

void CRoomList::OnCreateRoom()
{
    done(0);
    QTimer::singleShot(0, [] { ChatCreateRoom(g_enterInfo); });
}

void CRoomList::OnCloseDialog()
{
    done(0);
}

void CRoomList::OnSearchDescrs()
{
    m_bSearchDescrs = m_ctrlSearchDescrs->isChecked();
    if (!m_strTopic.isEmpty()) FilterByTopic(m_strTopic);
}

void CRoomList::OnChangeTopicEdit()
{
    if (m_loadingControls) return;
    FilterByTopic(m_topicEdit->text());
}

void CRoomList::OnChangeMinMembers()
{
    if (!m_bInitialized || m_loadingControls) return;
    m_minMembers = m_minMembersEdit->text().toUInt();
    if (m_minMembers > m_maxMembers)
        m_maxMembersEdit->setText(m_minMembersEdit->text());
    OnChangeTopicEdit();
}

void CRoomList::OnChangeMaxMembers()
{
    if (!m_bInitialized || m_loadingControls) return;
    m_maxMembers = m_maxMembersEdit->text().toUInt();
    if (m_minMembers > m_maxMembers)
        m_minMembersEdit->setText(m_maxMembersEdit->text());
    OnChangeTopicEdit();
}

void CRoomList::OnRegisteredOnly()
{
    m_bRegisteredOnly = m_registeredOnly->isChecked();
    theApp.m_bListRegistered = m_bRegisteredOnly;
    OnChangeTopicEdit();
}

bool CRoomList::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress
        && (watched == m_minMembersEdit || watched == m_maxMembersEdit)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up
            || keyEvent->key() == Qt::Key_Down) {
            auto* edit = static_cast<QLineEdit*>(watched);
            const int delta = keyEvent->key() == Qt::Key_Up ? 1 : -1;
            const int value = qBound(0, edit->text().toInt() + delta, 9999);
            edit->setText(QString::number(value));
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress
        || event->type() == QEvent::KeyRelease) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_F5) {
            if (event->type() == QEvent::KeyPress
                && !keyEvent->isAutoRepeat()) {
                QTimer::singleShot(0, this, &CRoomList::OnResetList);
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
