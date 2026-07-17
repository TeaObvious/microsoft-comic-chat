#include "chat.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "protsupp.h"
#include "roomlist.h"
#include "setupdlg.h"
#include "userlist.h"

#include <QApplication>
#include <QFontMetrics>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>

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
    REQUIRE(!server.isEmpty() && !nick.isEmpty() && !identity.isEmpty()
            && !fullName.isEmpty() && !encodedRoom.isEmpty()
            && !topic.isEmpty());

    {
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoomList dialog(&persist);
        dialog.show();
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
        REQUIRE(!dialog.m_goto->isEnabled());
        REQUIRE(!dialog.m_listMembers->isEnabled());
    }

    {
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CUserList dialog(&persist);
        dialog.show();
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
        REQUIRE(!dialog.m_message->isEnabled());
    }

    {
        CRoomListPersist persist;
        auto* ircxRoom = new CRoom;
        ircxRoom->m_name = QString::fromLatin1(
            g_rgIrcCmd[cmdidList].szCmd);
        ircxRoom->m_prettyName = ircxRoom->m_name;
        ircxRoom->CalculateSortByte();
        auto* ircRoom = new CRoom;
        ircRoom->m_name = QLatin1Char('#') + QString::fromLatin1(
            g_rgIrcCmd[cmdidWho].szCmd);
        ircRoom->m_prettyName = ircRoom->m_name;
        ircRoom->CalculateSortByte();
        persist.AddRoom(ircRoom);
        persist.AddRoom(ircxRoom);
        persist.SortRooms();
        REQUIRE(persist.m_rooms.first() == ircxRoom);

        CUserListPersist users;
        auto* second = new CUser;
        second->m_strNickname = QString::fromLatin1(
            g_rgIrcCmd[cmdidWho].szCmd);
        auto* first = new CUser;
        first->m_strNickname = QString::fromLatin1(
            g_rgIrcCmd[cmdidList].szCmd);
        users.AddUser(second);
        users.AddUser(first);
        users.Sort();
        REQUIRE(users.m_users.first() == first);
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
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoomList dialog(&persist);
        dialog.show();
        application.processEvents();
        theApp.m_pRoomList = &dialog;
        theApp.m_bInSearch = TRUE;
        REQUIRE(serverConn.m_queries.bAddQuery(new CCQuery(
            qpRoomListDlg, ctList, dtMax, nullptr, QString(), QString())));
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
        theApp.m_pRoomList = nullptr;
    }

    serverConn.m_queries.FreeRemoveAll();
    {
        CRoomListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        CRoomList dialog(&persist);
        dialog.show();
        application.processEvents();
        theApp.m_pRoomList = &dialog;
        theApp.m_bInSearch = TRUE;
        REQUIRE(serverConn.m_queries.bAddQuery(new CCQuery(
            qpRoomListDlg, ctListX, dtMax, nullptr, QString(), QString())));
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
        theApp.m_pRoomList = nullptr;
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
        theApp.m_bInSearch = TRUE;
        REQUIRE(serverConn.m_queries.bAddQuery(new CCQuery(
            qpUserListDlg, ctWho, dtMax, nullptr, QString(), QString())));
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
        theApp.m_pUserList = nullptr;
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
        dialog.m_roomList->topLevelItem(0)->setSelected(true);
        theApp.m_pRoomList = &dialog;
        QString prettyRoom = room->m_prettyName;
        ListMembers(encodedRoom, prettyRoom);
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
        theApp.m_pRoomList = nullptr;
    }

    serverConn.m_queries.FreeRemoveAll();
    CommunicationCleanup();
    return EXIT_SUCCESS;
}
