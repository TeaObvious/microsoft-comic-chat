#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "protsupp.h"
#include "roomlist.h"
#include "setupdlg.h"
#include "userinfo.h"
#include "userlist.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFontMetrics>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

class CapturingIrcProto final : public CIrcProto {
public:
    void SendMessageText(const QString& raw) override { sent.append(raw); }
    void SendMessageBytes(const QByteArray& raw) override
    {
        sent.append(QString::fromUtf8(raw));
    }
    QStringList sent;
};

const OriginalDialogControl* resourceControl(
    const OriginalDialogResource& dialog, const QString& identifier)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier == identifier) return &control;
    }
    return nullptr;
}

QRect resourceRect(const OriginalDialogControl& control, const QFont& font)
{
    const QFontMetrics metrics(font);
    const QString alphabet = QStringLiteral(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    const int baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
    const int baseY = qMax(1, metrics.height());
    const auto x = [baseX](int dlu) { return (dlu * baseX + 2) / 4; };
    const auto y = [baseY](int dlu) { return (dlu * baseY + 4) / 8; };
    return {x(control.x), y(control.y),
            x(control.width), y(control.height)};
}

QString sourceCommand(enumCmdId command)
{
    return QString::fromLatin1(g_rgIrcCmd[command].szCmd);
}

CRoom* sourceRoom(const QString& name, UINT users, const QString& description,
                  BOOL registered = FALSE)
{
    auto* room = new CRoom;
    room->m_name = name;
    room->m_prettyName = name;
    room->m_nUsers = users;
    room->m_descr = description;
    room->m_byteRegistered = registered;
    room->CalculateSortByte();
    return room;
}

CUser* sourceUser(const QString& nickname, const QString& identity,
                  const QString& fullName, const QString& room)
{
    auto* user = new CUser;
    user->m_strNickname = nickname;
    user->m_strIdentity = identity;
    user->m_strFullName = fullName;
    user->m_strRoom = room;
    user->m_strPrettyRoom = room;
    return user;
}

void removeQuery(enumCommandType command)
{
    int index = -1;
    REQUIRE(serverConn.m_queries.FindQuery(command, &index) != nullptr);
    REQUIRE(index >= 0);
    REQUIRE(serverConn.m_queries.FreeRemoveAt(index));
}

class EnterInfoState {
public:
    EnterInfoState()
        : channel(g_enterInfo.m_strChannel)
        , prettyChannel(g_enterInfo.m_strPrettyChannel)
        , password(g_enterInfo.m_strPassword)
        , creationModes(g_enterInfo.m_strCreationModes)
        , topic(g_enterInfo.m_strTopic)
        , formatting(CopyFormatting(g_enterInfo.m_prgdwTopicFormatting))
        , modes(g_enterInfo.m_dwModes)
        , maximumUsers(g_enterInfo.m_dwMaxUsers)
        , setMode(g_enterInfo.m_bSetMode)
        , keepServer(g_nCXKeepServer)
        , prompt(g_bCXPrompt)
        , enterOnCreate(g_bEnterOnCreate)
    {
    }

    ~EnterInfoState()
    {
        g_enterInfo.m_strChannel = channel;
        g_enterInfo.m_strPrettyChannel = prettyChannel;
        g_enterInfo.m_strPassword = password;
        g_enterInfo.m_strCreationModes = creationModes;
        g_enterInfo.m_strTopic = topic;
        FreeAndNullFormatting(&g_enterInfo.m_prgdwTopicFormatting);
        g_enterInfo.m_prgdwTopicFormatting = formatting;
        formatting = nullptr;
        g_enterInfo.m_dwModes = modes;
        g_enterInfo.m_dwMaxUsers = maximumUsers;
        g_enterInfo.m_bSetMode = setMode;
        g_nCXKeepServer = keepServer;
        g_bCXPrompt = prompt;
        g_bEnterOnCreate = enterOnCreate;
    }

private:
    QString channel;
    QString prettyChannel;
    QString password;
    QString creationModes;
    QString topic;
    CDWordArray* formatting = nullptr;
    unsigned long modes = 0;
    unsigned long maximumUsers = 0;
    bool setMode = false;
    SHORT keepServer = 0;
    BOOL prompt = FALSE;
    BOOL enterOnCreate = FALSE;
};
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    REQUIRE(CommunicationInits());
    GetIrcProto()->SetConnectionStatus(CX_NOCHANNEL);

    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString nick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString identity = QString::fromUtf8(GetMyUserName());
    const QString fullName = originalResourceString(
        QStringLiteral("IDS_DEFAULT_REALNAME"));
    const QString encodedRoom = EncodeChan(originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL")));
    const QString topic = originalResourceString(
        QStringLiteral("ID_RL_DESCR_LABEL"));
    const QString listCommand = sourceCommand(cmdidList);
    const QString listXCommand = sourceCommand(cmdidListX);
    const QString topicCommand = sourceCommand(cmdidTopic);
    const QString whoCommand = sourceCommand(cmdidWho);
    REQUIRE(!server.isEmpty() && !nick.isEmpty() && !identity.isEmpty()
            && !fullName.isEmpty() && !encodedRoom.isEmpty()
            && !topic.isEmpty() && !listCommand.isEmpty()
            && !listXCommand.isEmpty() && !topicCommand.isEmpty()
            && !whoCommand.isEmpty());

    {
        const BOOL savedIrcX = serverConn.m_bIrcXServer;
        serverConn.m_bIrcXServer = FALSE;
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        persist.m_bRegisteredOnly = TRUE;
        CRoomList dialog(&persist);
        dialog.show();
        dialog.activateWindow();
        application.processEvents();
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_ROOMLIST"));
        REQUIRE(dialog.layout() == nullptr);
        REQUIRE(resource.width == 400 && resource.height == 255);
        REQUIRE(dialog.windowTitle() == resource.caption);
        REQUIRE(dialog.m_roomList->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_ROOMLIST")),
            dialog.font()));
        REQUIRE(dialog.m_topicEdit->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_TOPIC_EDIT")),
            dialog.font()));
        REQUIRE(dialog.m_roomList->headerItem()->text(0)
                == originalResourceString(QStringLiteral("ID_RL_ROOM_LABEL")));
        REQUIRE(dialog.m_roomList->columnWidth(0)
                == originalResourceString(
                    QStringLiteral("ID_RL_ROOM_WIDTH")).toInt());
        REQUIRE(dialog.m_roomList->columnWidth(1)
                == originalResourceString(
                    QStringLiteral("ID_RL_NUSERS_WIDTH")).toInt());
        REQUIRE(dialog.m_roomList->columnWidth(2)
                == originalResourceString(
                    QStringLiteral("ID_RL_DESCR_WIDTH")).toInt());
        REQUIRE(dialog.m_roomList->selectionMode()
                == QAbstractItemView::SingleSelection);
        REQUIRE(dialog.m_roomList->selectionBehavior()
                == QAbstractItemView::SelectRows);
        REQUIRE(dialog.m_goto->isDefault());
        REQUIRE(!dialog.m_registeredOnly->isEnabled());
        REQUIRE(!dialog.m_registeredOnly->isChecked());
        REQUIRE(!dialog.m_goto->isEnabled());
        REQUIRE(!dialog.m_listMembers->isEnabled());

        QWidget* minSpin = dialog.findChild<QWidget*>(
            QStringLiteral("IDC_SPIN_MIN"));
        QWidget* maxSpin = dialog.findChild<QWidget*>(
            QStringLiteral("IDC_SPIN_MAX"));
        REQUIRE(minSpin && maxSpin);
        const QList<QToolButton*> spinButtons =
            minSpin->findChildren<QToolButton*>()
            + maxSpin->findChildren<QToolButton*>();
        REQUIRE(spinButtons.size() == 4);
        for (QToolButton* spinButton : spinButtons)
            REQUIRE(spinButton->focusPolicy() == Qt::NoFocus);

        dialog.m_topicEdit->clear();
        dialog.m_topicEdit->setFocus();
        QKeyEvent topicSpace(
            QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier,
            QStringLiteral(" "));
        QApplication::sendEvent(dialog.m_topicEdit, &topicSpace);
        QKeyEvent topicComma(
            QEvent::KeyPress, Qt::Key_Comma, Qt::NoModifier,
            QStringLiteral(","));
        QApplication::sendEvent(dialog.m_topicEdit, &topicComma);
        REQUIRE(dialog.m_topicEdit->text().isEmpty());

        dialog.m_minMembersEdit->setText(QString::number(1));
        QKeyEvent spinUp(
            QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QApplication::sendEvent(dialog.m_minMembersEdit, &spinUp);
        REQUIRE(dialog.m_minMembersEdit->text() == QString::number(2));
        QKeyEvent spinDown(
            QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(dialog.m_minMembersEdit, &spinDown);
        REQUIRE(dialog.m_minMembersEdit->text() == QString::number(1));
        dialog.m_maxMembersEdit->setText(QString::number(3));
        QApplication::sendEvent(dialog.m_maxMembersEdit, &spinUp);
        REQUIRE(dialog.m_maxMembersEdit->text() == QString::number(4));
        QApplication::sendEvent(dialog.m_maxMembersEdit, &spinDown);
        REQUIRE(dialog.m_maxMembersEdit->text() == QString::number(3));
        serverConn.m_bIrcXServer = savedIrcX;
    }

    {
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CUserList dialog(&persist);
        dialog.show();
        dialog.activateWindow();
        application.processEvents();
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_USERLIST"));
        REQUIRE(dialog.layout() == nullptr);
        REQUIRE(resource.width == 395 && resource.height == 263);
        REQUIRE(dialog.windowTitle() == resource.caption);
        REQUIRE(dialog.m_userListCtrl->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_USERLIST")),
            dialog.font()));
        REQUIRE(dialog.m_user->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_SEARCH_EDIT")),
            dialog.font()));
        REQUIRE(dialog.m_userListCtrl->headerItem()->text(1)
                == originalResourceString(QStringLiteral("ID_UL_IDENT_LABEL")));
        REQUIRE(dialog.m_userListCtrl->columnWidth(0)
                == originalResourceString(
                    QStringLiteral("ID_UL_NICK_WIDTH")).toInt());
        REQUIRE(dialog.m_userListCtrl->columnWidth(1)
                == originalResourceString(
                    QStringLiteral("ID_UL_IDENT_WIDTH")).toInt());
        REQUIRE(dialog.m_userListCtrl->columnWidth(2)
                == originalResourceString(
                    QStringLiteral("ID_UL_REALNAME_WIDTH")).toInt());
        REQUIRE(dialog.m_userListCtrl->columnWidth(3)
                == originalResourceString(
                    QStringLiteral("ID_UL_ROOM_WIDTH")).toInt());
        REQUIRE(dialog.m_userListCtrl->selectionMode()
                == QAbstractItemView::SingleSelection);
        REQUIRE(dialog.m_userListCtrl->selectionBehavior()
                == QAbstractItemView::SelectRows);
        REQUIRE(dialog.m_reset->isDefault());
        REQUIRE(dialog.m_user->isVisible());
        REQUIRE(dialog.m_ctlRoom->isVisible());
        REQUIRE(dialog.m_user->isEnabled());
        REQUIRE(!dialog.m_ctlRoom->isEnabled());
        REQUIRE(dialog.m_user->hasFocus());
        REQUIRE(!dialog.m_message->isEnabled());
    }

    {
        CRoomListPersist persist;
        CRoom* ircxAlpha = sourceRoom(listCommand, 1, topic);
        CRoom* ircxOther = sourceRoom(
            originalResourceString(QStringLiteral("ID_RL_ROOM_WIDTH"))
                + listCommand,
            1, topic);
        CRoom* ircAlpha = sourceRoom(
            QLatin1Char('#') + whoCommand, 1, topic);
        CRoom* ircOther = sourceRoom(
            QLatin1Char('#')
                + originalResourceString(QStringLiteral("ID_RL_ROOM_WIDTH"))
                + whoCommand,
            1, topic);
        persist.AddRoom(ircOther);
        persist.AddRoom(ircAlpha);
        persist.AddRoom(ircxOther);
        persist.AddRoom(ircxAlpha);

        persist.m_sortColumn = 0;
        persist.m_sortAscending = TRUE;
        persist.SortRooms();
        REQUIRE(persist.m_rooms
                == QList<CRoom*>({ircxAlpha, ircxOther, ircAlpha, ircOther}));
        persist.m_sortAscending = FALSE;
        persist.SortRooms();
        REQUIRE(persist.m_rooms
                == QList<CRoom*>({ircOther, ircAlpha, ircxOther, ircxAlpha}));

        ircxAlpha->m_nUsers = 1;
        ircxOther->m_nUsers = 1;
        ircAlpha->m_nUsers = 2;
        ircOther->m_nUsers = 3;
        ircxAlpha->m_prettyName = listCommand;
        ircxOther->m_prettyName = topicCommand;
        ircAlpha->m_prettyName = whoCommand;
        ircOther->m_prettyName = whoCommand + topicCommand;
        persist.m_sortColumn = 1;
        persist.m_sortAscending = TRUE;
        persist.SortRooms();
        REQUIRE(persist.m_rooms
                == QList<CRoom*>({ircxAlpha, ircxOther, ircAlpha, ircOther}));
        persist.m_sortAscending = FALSE;
        persist.SortRooms();
        REQUIRE(persist.m_rooms
                == QList<CRoom*>({ircOther, ircAlpha, ircxAlpha, ircxOther}));

        ircxAlpha->m_descr = topic;
        ircxOther->m_descr = topic;
        ircAlpha->m_descr = topic;
        ircOther->m_descr = topic;
        ircxAlpha->CalculateSortByte();
        ircxOther->CalculateSortByte();
        ircAlpha->CalculateSortByte();
        ircOther->CalculateSortByte();
        persist.m_sortColumn = 2;
        persist.m_sortAscending = TRUE;
        persist.SortRooms();
        const QList<CRoom*> descriptionTieOrder = persist.m_rooms;
        persist.m_sortAscending = FALSE;
        persist.SortRooms();
        REQUIRE(persist.m_rooms == descriptionTieOrder);
    }

    {
        CUserListPersist persist;
        CUser* listUser = sourceUser(
            listCommand, whoCommand, topicCommand, whoCommand);
        listUser->SetPrettyNick(
            QLatin1Char('"') + listCommand + QLatin1Char('"'));
        CUser* whoUser = sourceUser(
            whoCommand, listCommand, whoCommand, listCommand);
        CUser* topicUser = sourceUser(
            topicCommand, listCommand, listCommand, listCommand);
        persist.AddUser(whoUser);
        persist.AddUser(listUser);
        persist.AddUser(topicUser);

        const QList<QList<CUser*>> ascendingOrders = {
            {listUser, topicUser, whoUser},
            {topicUser, whoUser, listUser},
            {topicUser, listUser, whoUser},
            {topicUser, whoUser, listUser}};
        const QList<QList<CUser*>> descendingOrders = {
            {whoUser, topicUser, listUser},
            {listUser, whoUser, topicUser},
            {whoUser, listUser, topicUser},
            {listUser, whoUser, topicUser}};
        for (int column = 0; column < 4; ++column) {
            persist.m_sortColumn = column;
            persist.m_sortAscending = TRUE;
            persist.Sort();
            REQUIRE(persist.m_users == ascendingOrders.at(column));
            persist.m_sortAscending = FALSE;
            persist.Sort();
            REQUIRE(persist.m_users == descendingOrders.at(column));
        }
    }

    {
        const BOOL savedIrcX = serverConn.m_bIrcXServer;
        const BOOL savedRegistered = theApp.m_bListRegistered;
        serverConn.m_bIrcXServer = TRUE;
        theApp.m_bListRegistered = FALSE;

        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoom* nameMatch = sourceRoom(
            topic.toLower() + listCommand, 1, fullName);
        CRoom* descriptionMatch = sourceRoom(
            listCommand, 2, topic.toUpper(), TRUE);
        CRoom* noMatch = sourceRoom(whoCommand, 3, fullName);
        persist.AddRoom(noMatch);
        persist.AddRoom(descriptionMatch);
        persist.AddRoom(nameMatch);
        CRoomList dialog(&persist);
        dialog.show();
        application.processEvents();
        REQUIRE(dialog.m_registeredOnly->isEnabled());
        REQUIRE(dialog.m_roomList->topLevelItemCount() == 3);

        dialog.m_roomList->clearSelection();
        dialog.m_roomList->setCurrentItem(nullptr);
        dialog.m_roomList->setFocus(Qt::OtherFocusReason);
        application.processEvents();
        REQUIRE(dialog.m_roomList->currentItem()
                == dialog.m_roomList->topLevelItem(0));
        REQUIRE(dialog.m_roomList->selectedItems().isEmpty());
        REQUIRE(!dialog.m_goto->isEnabled());
        REQUIRE(!dialog.m_listMembers->isEnabled());

        QTreeWidgetItem* focused = dialog.m_roomList->topLevelItem(0);
        QTreeWidgetItem* selected = dialog.m_roomList->topLevelItem(1);
        dialog.m_roomList->setCurrentItem(
            focused, 0, QItemSelectionModel::NoUpdate);
        selected->setSelected(true);
        REQUIRE(dialog.GetSelectedRoom()
                == persist.m_rooms.at(focused->data(
                    0, Qt::UserRole).toInt()));
        REQUIRE(dialog.m_goto->isEnabled());
        REQUIRE(dialog.m_listMembers->isEnabled());

        dialog.m_roomList->header()->sectionClicked(2);
        REQUIRE(persist.m_sortColumn == 2);
        REQUIRE(persist.m_sortAscending);
        dialog.m_roomList->header()->sectionClicked(2);
        REQUIRE(!persist.m_sortAscending);
        REQUIRE(!dialog.m_goto->isEnabled());
        REQUIRE(!dialog.m_listMembers->isEnabled());

        dialog.m_topicEdit->setText(topic.toUpper());
        REQUIRE(dialog.m_roomList->topLevelItemCount() == 1);
        REQUIRE(dialog.m_roomList->topLevelItem(0)->text(0)
                == nameMatch->m_prettyName);
        dialog.m_ctrlSearchDescrs->click();
        REQUIRE(dialog.m_roomList->topLevelItemCount() == 2);
        dialog.m_registeredOnly->click();
        REQUIRE(theApp.m_bListRegistered);
        REQUIRE(dialog.m_roomList->topLevelItemCount() == 1);
        REQUIRE(dialog.m_roomList->topLevelItem(0)->text(0)
                == descriptionMatch->m_prettyName);

        dialog.m_registeredOnly->click();
        dialog.m_topicEdit->clear();
        dialog.m_maxMembersEdit->setText(QString::number(1));
        REQUIRE(dialog.m_roomList->topLevelItemCount() == 1);
        REQUIRE(dialog.m_roomList->topLevelItem(0)->text(0)
                == nameMatch->m_prettyName);
        dialog.m_minMembersEdit->setText(QString::number(2));
        REQUIRE(dialog.m_minMembersEdit->text() == QString::number(2));
        REQUIRE(dialog.m_maxMembersEdit->text() == QString::number(2));
        REQUIRE(dialog.m_roomList->topLevelItemCount() == 1);
        REQUIRE(dialog.m_roomList->topLevelItem(0)->text(0)
                == descriptionMatch->m_prettyName);
        dialog.m_maxMembersEdit->setText(QString::number(1));
        REQUIRE(dialog.m_minMembersEdit->text() == QString::number(1));
        REQUIRE(dialog.m_maxMembersEdit->text() == QString::number(1));
        REQUIRE(dialog.m_roomList->topLevelItemCount() == 1);

        QString roomCount = originalResourceString(
            QStringLiteral("IDS_NUM_ROOMS"));
        roomCount.replace(QStringLiteral("%1"), QString::number(1));
        REQUIRE(dialog.m_ctlCaption->text() == roomCount);

        serverConn.m_bIrcXServer = savedIrcX;
        theApp.m_bListRegistered = savedRegistered;
    }

    {
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        persist.AddUser(sourceUser(
            whoCommand, identity, fullName, encodedRoom));
        persist.AddUser(sourceUser(
            listCommand, identity, fullName, encodedRoom));
        CUserList dialog(&persist);
        dialog.show();
        application.processEvents();
        dialog.m_userListCtrl->clearSelection();
        dialog.m_userListCtrl->setCurrentItem(nullptr);
        dialog.m_userListCtrl->setFocus(Qt::OtherFocusReason);
        application.processEvents();
        REQUIRE(dialog.m_userListCtrl->currentItem()
                == dialog.m_userListCtrl->topLevelItem(0));
        REQUIRE(dialog.m_userListCtrl->selectedItems().isEmpty());
        REQUIRE(!dialog.m_invite->isEnabled());
        REQUIRE(!dialog.m_message->isEnabled());
        REQUIRE(!dialog.m_join->isEnabled());

        dialog.m_userListCtrl->header()->sectionClicked(3);
        REQUIRE(persist.m_sortColumn == 3);
        REQUIRE(persist.m_sortAscending);
        dialog.m_userListCtrl->header()->sectionClicked(3);
        REQUIRE(!persist.m_sortAscending);
    }

    {
        const BOOL savedIrcX = serverConn.m_bIrcXServer;
        const BOOL savedRegistered = theApp.m_bListRegistered;
        serverConn.m_bIrcXServer = TRUE;
        theApp.m_bListRegistered = FALSE;

        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoom* retained = sourceRoom(encodedRoom, 2, topic, TRUE);
        persist.AddRoom(retained);
        CRoomList dialog(&persist);
        QTimer::singleShot(0, [&] {
            dialog.m_topicEdit->setText(topic);
            dialog.m_ctrlSearchDescrs->click();
            dialog.m_registeredOnly->click();
            dialog.m_minMembersEdit->setText(QString::number(2));
            dialog.m_maxMembersEdit->setText(QString::number(2));
            dialog.reject();
        });
        REQUIRE(dialog.DoModal() == QDialog::Rejected);
        REQUIRE(persist.m_strTopicFilter == topic);
        REQUIRE(persist.m_bSearchDescrs);
        REQUIRE(persist.m_bRegisteredOnly);
        REQUIRE(persist.m_minMembers == 2);
        REQUIRE(persist.m_maxMembers == 2);
        REQUIRE(persist.m_nRooms == 1);
        REQUIRE(persist.m_rooms.first() == retained);

        serverConn.m_bIrcXServer = savedIrcX;
        theApp.m_bListRegistered = savedRegistered;
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        CapturingIrcProto protocol;
        protocol.SetConnectionStatus(CX_NOCHANNEL);
        protocol.m_strChannel = encodedRoom;
        REQUIRE(protocol.bExecuteQuery(qpRoomListDlg, ctList, dtMax,
                                       nullptr, encodedRoom, QString()));
        REQUIRE(protocol.sent.takeLast()
                == QStringLiteral("LIST %1\r\n").arg(encodedRoom));
        serverConn.m_queries.FreeRemoveAll();
        REQUIRE(protocol.bExecuteQuery(qpRoomListDlg, ctListX, dtMax,
                                       nullptr, encodedRoom, QString()));
        REQUIRE(protocol.sent.takeLast()
                == QStringLiteral("LISTX N=%1\r\n").arg(encodedRoom));
        serverConn.m_queries.FreeRemoveAll();
        const QString mask = QLatin1Char('*') + nick + QLatin1Char('*');
        REQUIRE(protocol.bExecuteQuery(qpUserListDlg, ctWho, dtMax,
                                       nullptr, QString(), mask));
        REQUIRE(protocol.sent.takeLast()
                == QStringLiteral("WHO %1\r\n").arg(mask));
        serverConn.m_queries.FreeRemoveAll();
        QString prettyRoom = DecodeChan(encodedRoom);
        REQUIRE(protocol.bExecuteQuery(qpListMembers, ctTopic, dtMax,
                                       &prettyRoom, encodedRoom, QString()));
        REQUIRE(protocol.sent.takeLast()
                == QStringLiteral("TOPIC %1\r\n").arg(encodedRoom));
        serverConn.m_queries.FreeRemoveAll();
        REQUIRE(protocol.ChatSendInvitation(nick));
        REQUIRE(protocol.sent.takeLast()
                == QStringLiteral("INVITE %1 %2\r\n")
                       .arg(nick, encodedRoom));
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        const BOOL savedIrcX = serverConn.m_bIrcXServer;
        serverConn.m_bIrcXServer = FALSE;
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        persist.m_searchType = USERSEARCH_ALL;
        CUserList dialog(&persist);
        theApp.m_pUserList = &dialog;
        dialog.show();
        application.processEvents();

        const auto completeSearch = [&](const QString& channel,
                                        const QString& mask) {
            CCQuery* query = serverConn.m_queries.FindQuery(ctWho);
            REQUIRE(query != nullptr);
            REQUIRE(query->GetQueryPurpose() == qpUserListDlg);
            REQUIRE(query->GetChannelName() == channel);
            REQUIRE(query->GetNicknameMask() == mask);
            removeQuery(ctWho);
            EndUserList();
            REQUIRE(dialog.m_reset->isEnabled());
            REQUIRE(!theApp.m_bInSearch);
        };

        dialog.OnUsersearchAll();
        dialog.OnResetList();
        completeSearch(QString(), QString());

        dialog.m_user->setText(QLatin1Char(' ') + nick);
        dialog.OnUsersearchNick();
        REQUIRE(dialog.m_user->isEnabled());
        REQUIRE(!dialog.m_ctlRoom->isEnabled());
        dialog.OnResetList();
        REQUIRE(persist.m_strUserFilter == nick);
        completeSearch(QString(),
                       QLatin1Char('*') + nick + QLatin1Char('*'));

        dialog.m_user->setText(QLatin1Char(' ') + identity);
        dialog.OnUsersearchIdentity();
        dialog.OnResetList();
        REQUIRE(persist.m_strUserFilter == identity);
        completeSearch(QString(),
                       QLatin1Char('*') + identity + QLatin1Char('*'));

        dialog.m_ctlRoom->setText(
            QLatin1Char(' ') + DecodeChan(encodedRoom));
        dialog.OnUsersearchRoom();
        REQUIRE(!dialog.m_user->isEnabled());
        REQUIRE(dialog.m_ctlRoom->isEnabled());
        dialog.OnResetList();
        completeSearch(encodedRoom, QString());

        dialog.m_ctlRoom->setText(whoCommand);
        persist.m_strEncRoom = encodedRoom;
        dialog.OnResetList();
        completeSearch(encodedRoom, QString());

        serverConn.m_bIrcXServer = TRUE;
        dialog.OnUsersearchNick();
        dialog.m_user->setText(nick);
        dialog.OnResetList();
        completeSearch(
            QString(),
            QStringLiteral("'*") + EncodeNick(nick, true).mid(1)
                + QLatin1Char('*'));

        theApp.m_pUserList = nullptr;
        serverConn.m_bIrcXServer = savedIrcX;
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        persist.m_strQuery = whoCommand + QLatin1Char(' ') + nick
            + QStringLiteral("  ");
        CUserList dialog(&persist);
        theApp.m_pUserList = &dialog;
        dialog.show();
        application.processEvents();
        QString expectedTitle = originalDialogResource(
            QStringLiteral("IDD_USERLIST")).caption
            + QStringLiteral(": ") + persist.m_strQuery;
        while (!expectedTitle.isEmpty()
               && expectedTitle.back().isSpace()) {
            expectedTitle.chop(1);
        }
        REQUIRE(dialog.windowTitle() == expectedTitle);
        auto* searchAll = dialog.findChild<QRadioButton*>(
            QStringLiteral("IDC_USERSEARCH_ALL"));
        REQUIRE(searchAll != nullptr);
        REQUIRE(!searchAll->isEnabled());
        REQUIRE(!dialog.m_user->isEnabled());
        REQUIRE(!dialog.m_ctlRoom->isEnabled());
        CCQuery* query = serverConn.m_queries.FindQuery(ctWho);
        REQUIRE(query != nullptr);
        REQUIRE(query->GetQueryPurpose() == qpUserListDlg);
        REQUIRE(query->GetNicknameMask() == nick);
        removeQuery(ctWho);
        EndUserList();
        dialog.OnCloseDialog();
        theApp.m_pUserList = nullptr;
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        const BOOL savedIrcX = serverConn.m_bIrcXServer;
        serverConn.m_bIrcXServer = FALSE;
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        persist.AddRoom(sourceRoom(encodedRoom, 1, topic));
        CRoomList dialog(&persist);
        theApp.m_pRoomList = &dialog;
        dialog.show();
        application.processEvents();

        QKeyEvent repeatPress(
            QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier,
            QString(), TRUE, 2);
        QApplication::sendEvent(dialog.m_topicEdit, &repeatPress);
        application.processEvents();
        REQUIRE(serverConn.m_queries.FindQuery(ctList) == nullptr);
        REQUIRE(dialog.m_reset->isEnabled());

        QKeyEvent release(
            QEvent::KeyRelease, Qt::Key_F5, Qt::NoModifier,
            QString(), FALSE, 1);
        QApplication::sendEvent(dialog.m_topicEdit, &release);
        application.processEvents();
        REQUIRE(serverConn.m_queries.FindQuery(ctList) == nullptr);

        QKeyEvent refresh(
            QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier,
            QString(), FALSE, 1);
        QApplication::sendEvent(dialog.m_topicEdit, &refresh);
        REQUIRE(serverConn.m_queries.FindQuery(ctList) == nullptr);
        application.processEvents();
        CCQuery* query = serverConn.m_queries.FindQuery(ctList);
        REQUIRE(query != nullptr);
        REQUIRE(query->GetQueryPurpose() == qpRoomListDlg);
        REQUIRE(!dialog.m_reset->isEnabled());
        REQUIRE(dialog.m_roomList->topLevelItemCount() == 0);
        REQUIRE(!persist.m_searchTime.contains(QStringLiteral("%1")));
        removeQuery(ctList);
        EndRoomList();
        REQUIRE(dialog.m_reset->isEnabled());
        REQUIRE(!theApp.m_bInSearch);
        theApp.m_pRoomList = nullptr;
        serverConn.m_bIrcXServer = savedIrcX;
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        persist.m_searchType = USERSEARCH_ALL;
        persist.AddUser(sourceUser(
            whoCommand, identity, fullName, encodedRoom));
        CUserList dialog(&persist);
        theApp.m_pUserList = &dialog;
        dialog.show();
        application.processEvents();

        QKeyEvent repeatPress(
            QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier,
            QString(), TRUE, 2);
        QApplication::sendEvent(dialog.m_userListCtrl->viewport(),
                                &repeatPress);
        application.processEvents();
        REQUIRE(serverConn.m_queries.FindQuery(ctWho) == nullptr);

        QKeyEvent refresh(
            QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier,
            QString(), FALSE, 1);
        QApplication::sendEvent(dialog.m_userListCtrl->viewport(),
                                &refresh);
        REQUIRE(serverConn.m_queries.FindQuery(ctWho) == nullptr);
        application.processEvents();
        CCQuery* query = serverConn.m_queries.FindQuery(ctWho);
        REQUIRE(query != nullptr);
        REQUIRE(query->GetQueryPurpose() == qpUserListDlg);
        REQUIRE(query->GetChannelName().isEmpty());
        REQUIRE(query->GetNicknameMask().isEmpty());
        REQUIRE(!dialog.m_reset->isEnabled());
        REQUIRE(dialog.m_userListCtrl->topLevelItemCount() == 0);
        removeQuery(ctWho);
        EndUserList();
        REQUIRE(dialog.m_reset->isEnabled());
        REQUIRE(!theApp.m_bInSearch);
        theApp.m_pUserList = nullptr;
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoomList dialog(&persist);
        dialog.show();
        application.processEvents();
        theApp.m_pRoomList = &dialog;
        dialog.m_reset->setFocus(Qt::OtherFocusReason);
        application.processEvents();
        REQUIRE(dialog.m_reset->hasFocus());
        dialog.OnResetList();
        REQUIRE(dialog.m_bResetHadFocus);
        REQUIRE(!dialog.m_reset->isEnabled());
        CCQuery* query = serverConn.m_queries.FindQuery(ctList);
        REQUIRE(query != nullptr);
        REQUIRE(query->GetQueryPurpose() == qpRoomListDlg);
        serverConn.ProcessMessage(QStringLiteral(":%1 321 %2 :%3")
                                      .arg(server, nick, topic));
        serverConn.ProcessMessage(QStringLiteral(":%1 322 %2 %3 1 :%4")
                                      .arg(server, nick, encodedRoom, topic));
        serverConn.ProcessMessage(QStringLiteral(":%1 323 %2 :%3")
                                      .arg(server, nick, topic));
        REQUIRE(persist.m_nRooms == 1);
        REQUIRE(persist.m_rooms.first()->m_name == encodedRoom);
        REQUIRE(persist.m_rooms.first()->m_descr == topic);
        REQUIRE(serverConn.m_queries.FindQuery(ctList) == nullptr);
        REQUIRE(!theApp.m_bInSearch);
        REQUIRE(dialog.m_reset->isEnabled());
        REQUIRE(dialog.m_reset->hasFocus());
        QString roomCount = originalResourceString(
            QStringLiteral("IDS_NUM_ROOMS"));
        roomCount.replace(QStringLiteral("%1"), QString::number(1));
        REQUIRE(dialog.m_ctlCaption->text() == roomCount);
        REQUIRE(dialog.m_searchTime->text() == persist.m_searchTime);
        theApp.m_pRoomList = nullptr;
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        const BOOL savedIrcX = serverConn.m_bIrcXServer;
        serverConn.m_bIrcXServer = TRUE;
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoomList dialog(&persist);
        dialog.show();
        application.processEvents();
        theApp.m_pRoomList = &dialog;
        dialog.OnResetList();
        CCQuery* query = serverConn.m_queries.FindQuery(ctListX);
        REQUIRE(query != nullptr);
        REQUIRE(query->GetQueryPurpose() == qpRoomListDlg);
        serverConn.ProcessMessage(QStringLiteral(":%1 811 %2 :%3")
                                      .arg(server, nick, topic));
        serverConn.ProcessMessage(
            QStringLiteral(":%1 812 %2 %3 r 1 0 :%4")
                .arg(server, nick, encodedRoom, topic));
        serverConn.ProcessMessage(QStringLiteral(":%1 817 %2 :%3")
                                      .arg(server, nick, topic));
        REQUIRE(persist.m_nRooms == 1);
        REQUIRE(persist.m_rooms.first()->m_byteRegistered);
        REQUIRE(serverConn.m_queries.FindQuery(ctListX) == nullptr);
        REQUIRE(!theApp.m_bInSearch);
        REQUIRE(dialog.m_reset->isEnabled());
        theApp.m_pRoomList = nullptr;
        serverConn.m_bIrcXServer = savedIrcX;
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        persist.m_searchType = USERSEARCH_ALL;
        CUserList dialog(&persist);
        dialog.show();
        application.processEvents();
        theApp.m_pUserList = &dialog;
        dialog.m_reset->setFocus(Qt::OtherFocusReason);
        application.processEvents();
        REQUIRE(dialog.m_reset->hasFocus());
        dialog.OnResetList();
        REQUIRE(dialog.m_bResetHadFocus);
        REQUIRE(!dialog.m_reset->isEnabled());
        CCQuery* query = serverConn.m_queries.FindQuery(ctWho);
        REQUIRE(query != nullptr);
        REQUIRE(query->GetQueryPurpose() == qpUserListDlg);
        serverConn.ProcessMessage(
            QStringLiteral(":%1 352 %2 %3 %4 NoMachine %1 %2 H :0 %5")
                .arg(server, nick, encodedRoom, identity, fullName));
        serverConn.ProcessMessage(QStringLiteral(":%1 315 %2 :%3")
                                      .arg(server, nick, topic));
        REQUIRE(persist.m_nUsers == 1);
        REQUIRE(persist.m_users.first()->m_strNickname == nick);
        REQUIRE(persist.m_users.first()->m_strIdentity
                == identity + QStringLiteral("@NoMachine"));
        REQUIRE(persist.m_users.first()->m_strFullName == fullName);
        REQUIRE(serverConn.m_queries.FindQuery(ctWho) == nullptr);
        REQUIRE(!theApp.m_bInSearch);
        REQUIRE(dialog.m_reset->isEnabled());
        REQUIRE(dialog.m_reset->hasFocus());
        QString userCount = originalResourceString(
            QStringLiteral("IDS_NUM_USERS"));
        userCount.replace(QStringLiteral("%d"), QString::number(1));
        REQUIRE(dialog.m_ctlCaption->text() == userCount);
        REQUIRE(dialog.m_searchTime->text() == persist.m_searchTime);
        theApp.m_pUserList = nullptr;
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        EnterInfoState enterInfoState;
        const QString otherRoom = QLatin1Char('#') + whoCommand;
        REQUIRE(otherRoom != encodedRoom);
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoom* room = sourceRoom(otherRoom, 1, topic);
        persist.AddRoom(room);
        CRoomList dialog(&persist);
        dialog.show();
        application.processEvents();
        QTreeWidgetItem* item = dialog.m_roomList->topLevelItem(0);
        dialog.m_roomList->setCurrentItem(item);
        item->setSelected(true);
        g_bEnterOnCreate = TRUE;
        dialog.m_roomList->itemDoubleClicked(item, 0);
        REQUIRE(dialog.result() == 0);
        REQUIRE(!g_bEnterOnCreate);
        REQUIRE(g_enterInfo.m_strChannel == otherRoom);
    }

    {
        EnterInfoState enterInfoState;
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoomList dialog(&persist);
        dialog.show();
        application.processEvents();
        BOOL sawCreateDialog = FALSE;
        dialog.OnCreateRoom();
        QTimer::singleShot(0, [&] {
            auto* modal = qobject_cast<QDialog*>(
                application.activeModalWidget());
            REQUIRE(modal != nullptr);
            REQUIRE(modal->windowTitle() == originalDialogResource(
                QStringLiteral("IDD_CHANNELCREATE")).caption);
            sawCreateDialog = TRUE;
            modal->reject();
        });
        application.processEvents();
        REQUIRE(sawCreateDialog);
        REQUIRE(dialog.result() == 0);
    }

    {
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoomList dialog(&persist);
        dialog.show();
        application.processEvents();
        dialog.OnCloseDialog();
        REQUIRE(dialog.result() == 0);
        REQUIRE(persist.m_nRooms == 0);
    }

    {
        CChatDoc* precedingDocument = GetChatDoc();
        const QString savedNick = theApp.m_myNick;
        theApp.m_myNick = nick;
        CChatDoc document;
        document.m_bComicView = FALSE;
        SetChatDoc(&document);
        delete document.m_proto;
        document.m_proto = nullptr;
        CapturingIrcProto protocol;
        document.m_proto = &protocol;
        protocol.m_doc = &document;
        protocol.m_strChannel = encodedRoom;
        protocol.SetConnectionStatus(CX_INCHANNEL);
        document.LoadDocData();

        CUserInfo self(nick, identity);
        CUserInfo present(listCommand, identity);
        document.m_puiSelf = &self;
        g_puiSelf = &self;

        const QString otherRoom = QLatin1Char('#') + whoCommand;
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CUser* selfUser = sourceUser(
            nick, identity, fullName, encodedRoom);
        CUser* otherUser = sourceUser(
            listCommand, identity, fullName, otherRoom);
        persist.AddUser(selfUser);
        persist.AddUser(otherUser);
        CUserList dialog(&persist);
        dialog.show();
        application.processEvents();
        REQUIRE(!dialog.m_invite->isEnabled());
        REQUIRE(!dialog.m_message->isEnabled());
        REQUIRE(!dialog.m_join->isEnabled());

        const auto selectUser = [&](int index) {
            dialog.m_userListCtrl->clearSelection();
            QTreeWidgetItem* item =
                dialog.m_userListCtrl->topLevelItem(index);
            dialog.m_userListCtrl->setCurrentItem(item);
            item->setSelected(true);
            dialog.OnItemchangedUserlist();
        };

        selectUser(0);
        REQUIRE(!dialog.m_invite->isEnabled());
        REQUIRE(!dialog.m_message->isEnabled());
        REQUIRE(!dialog.m_join->isEnabled());

        selectUser(1);
        REQUIRE(dialog.m_invite->isEnabled());
        REQUIRE(dialog.m_message->isEnabled());
        REQUIRE(dialog.m_join->isEnabled());

        document.m_mapNickToPtr.insert(listCommand, &present);
        dialog.OnItemchangedUserlist();
        REQUIRE(!dialog.m_invite->isEnabled());
        present.SetDeparted(TRUE);
        dialog.OnItemchangedUserlist();
        REQUIRE(dialog.m_invite->isEnabled());

        otherUser->m_strRoom = encodedRoom;
        dialog.OnItemchangedUserlist();
        REQUIRE(!dialog.m_join->isEnabled());
        otherUser->m_strRoom = otherRoom;
        dialog.OnItemchangedUserlist();
        REQUIRE(dialog.m_join->isEnabled());

        protocol.sent.clear();
        dialog.OnInviteFromList();
        REQUIRE(protocol.sent
                == QStringList({QStringLiteral("INVITE %1 %2\r\n")
                                    .arg(listCommand, encodedRoom)}));

        dialog.OnMessageFromList();
        REQUIRE(dialog.result() == LAUNCH_WHISPERBOX);
        REQUIRE(dialog.m_selUser != nullptr);
        REQUIRE(dialog.m_selUser->GetName() == listCommand);
        REQUIRE(dialog.m_selUser->GetFullName() == identity);

        {
            EnterInfoState enterInfoState;
            CUserListPersist joinPersist;
            joinPersist.m_cachedServer = QString::fromUtf8(GetMyServer());
            joinPersist.AddUser(sourceUser(
                listCommand, identity, fullName, otherRoom));
            CUserList joinDialog(&joinPersist);
            joinDialog.show();
            application.processEvents();
            QTreeWidgetItem* item =
                joinDialog.m_userListCtrl->topLevelItem(0);
            joinDialog.m_userListCtrl->setCurrentItem(item);
            item->setSelected(true);
            g_bEnterOnCreate = TRUE;
            joinDialog.OnJoinRoom();
            REQUIRE(joinDialog.result() == 0);
            REQUIRE(!g_bEnterOnCreate);
            REQUIRE(g_enterInfo.m_strChannel == otherRoom);
        }

        document.m_mapNickToPtr.clear();
        document.m_puiSelf = nullptr;
        g_puiSelf = nullptr;
        document.m_proto = nullptr;
        if (precedingDocument) SetChatDoc(precedingDocument);
        else SetChatDoc(nullptr);
        theApp.m_myNick = savedNick;
    }

    {
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        persist.m_strUserFilter = nick;
        CUser* retained = sourceUser(
            listCommand, identity, fullName, encodedRoom);
        persist.AddUser(retained);
        CUserList dialog(&persist);
        QTimer::singleShot(0, [&] {
            dialog.m_user->setText(topic);
            dialog.OnCloseDialog();
        });
        REQUIRE(dialog.DoModal() == 0);
        REQUIRE(persist.m_strUserFilter == nick);
        REQUIRE(persist.m_nUsers == 1);
        REQUIRE(persist.m_users.first() == retained);
        REQUIRE(dialog.m_selUser == nullptr);
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        auto* room = new CRoom;
        room->m_name = encodedRoom;
        room->m_prettyName = DecodeChan(encodedRoom);
        room->CalculateSortByte();
        persist.AddRoom(room);
        CRoomList dialog(&persist);
        dialog.show();
        application.processEvents();
        QTreeWidgetItem* item = dialog.m_roomList->topLevelItem(0);
        dialog.m_roomList->setCurrentItem(item);
        item->setSelected(true);
        theApp.m_pRoomList = &dialog;
        dialog.OnListmembers();
        REQUIRE(!dialog.m_listMembers->isEnabled());
        CCQuery* topicQuery = serverConn.m_queries.FindQuery(ctTopic);
        REQUIRE(topicQuery
                && topicQuery->GetQueryPurpose() == qpListMembers);
        BOOL sawUserList = FALSE;
        QTimer::singleShot(0, [&] {
            auto* userDialog = dynamic_cast<CUserList*>(
                application.activeModalWidget());
            REQUIRE(userDialog != nullptr);
            REQUIRE(userDialog->m_persist->m_strEncRoom == encodedRoom);
            sawUserList = TRUE;
            userDialog->done(0);
        });
        serverConn.ProcessMessage(QStringLiteral(":%1 331 %2 %3 :%4")
                                      .arg(server, nick, encodedRoom, topic));
        REQUIRE(sawUserList);
        REQUIRE(serverConn.m_queries.FindQuery(ctTopic) == nullptr);
        CCQuery* whoQuery = serverConn.m_queries.FindQuery(ctWho);
        REQUIRE(whoQuery
                && whoQuery->GetQueryPurpose() == qpUserListDlg
                && whoQuery->GetChannelName() == encodedRoom);
        serverConn.ProcessMessage(QStringLiteral(":%1 315 %2 :%3")
                                      .arg(server, nick, topic));
        REQUIRE(serverConn.m_queries.FindQuery(ctWho) == nullptr);
        REQUIRE(dialog.m_listMembers->isEnabled());
        dialog.activateWindow();
        application.processEvents();
        REQUIRE(dialog.m_listMembers->hasFocus());
        theApp.m_pRoomList = nullptr;
    }

    serverConn.m_queries.FreeRemoveAll();
    CommunicationCleanup();
    return EXIT_SUCCESS;
}
