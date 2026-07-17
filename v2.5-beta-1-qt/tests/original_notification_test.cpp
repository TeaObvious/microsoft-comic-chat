#include "actions.h"
#include "chat.h"
#include "ircproto.h"
#include "ircsock.h"
#include "notif.h"
#include "originalassets.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "userlist.h"

#include <QCoreApplication>

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

INT displayCalls = 0;
INT signalCalls = 0;
QVector<UINT> displayedChanges;

BOOL displayNotifications(CCDynaNotifs* notifications)
{
    REQUIRE(notifications != nullptr);
    ++displayCalls;
    displayedChanges.append(notifications->GetModifiedUsersCount());
    // CNotificationUsers::bFillList clears this flag, while the original
    // bDisplayNotifications callback clears the count after filling.
    notifications->bRemoveFlagsFromAllUsers(g_wAltered);
    notifications->ResetModifiedUsersCount();
    return TRUE;
}

BOOL signalNewUpdate(CCDynaNotifs* notifications)
{
    REQUIRE(notifications != nullptr);
    ++signalCalls;
    return TRUE;
}

CCNotif* makeNotification(const QString& nickname,
                          const QString& host,
                          const QString& network)
{
    auto* notification = new CCNotif;
    notification->SetOperator(g_uNickname, g_uEquals);
    notification->SetOperator(g_uUserName, g_uAny);
    notification->SetOperator(g_uHostName, g_uEquals);
    notification->SetParam(g_uNickname, nickname);
    notification->SetParam(g_uUserName, QString());
    notification->SetParam(g_uHostName, host);
    notification->SetParam(g_uNetName, network);
    notification->Activate();
    REQUIRE(notification->bUpdateDaemonExt(TRUE));
    return notification;
}

QString whoReply(const QString& server, const QString& target,
                 const QString& room, const QString& user,
                 const QString& host, const QString& realName)
{
    return QStringLiteral(":%1 352 %2 %3 %4 %5 %1 %2 H :0 %6")
        .arg(server, target, room, user, host, realName);
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    theApp.InitVals();
    REQUIRE(CommunicationInits());

    const QString nickname = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString host = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString realName = originalResourceString(
        QStringLiteral("IDS_DEFAULT_REALNAME"));
    const QString room = EncodeChan(originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL")));
    const QString userName = QString::fromUtf8(GetMyUserName());
    const QString any = originalResourceString(IDS_KEY_EVENT_PARAM0 + kepAny);
    const QString listText = originalResourceString(ID_RL_DESCR_LABEL);
    const QStringList roomRule = originalResourceString(
        IDS_SAMPLES_RULE5).split(QLatin1Char('|'));
    REQUIRE(roomRule.size() >= 5);
    const QString changedRoom = EncodeChan(roomRule.at(4));
    REQUIRE(!nickname.isEmpty() && !host.isEmpty() && !realName.isEmpty()
            && !room.isEmpty() && !userName.isEmpty() && !any.isEmpty()
            && !listText.isEmpty() && !changedRoom.isEmpty());

    ChatSetServer(host);
    CIrcProto* protocol = GetIrcProto();
    REQUIRE(protocol != nullptr);
    protocol->SetConnectionStatus(CX_NOCHANNEL);

    CCNotif* notification = makeNotification(nickname, host, any);

    std::array<char, g_uMaxSerializedNotif> serialized{};
    const INT serializedLength = notification->Serialize(
        serialized.data(), serialized.size());
    REQUIRE(serializedLength > 8);

    QByteArray expected;
    expected.append(static_cast<char>(g_wVersion));
    expected.append(static_cast<char>(g_wActive));
    expected.append('\0');
    expected.append(static_cast<char>(g_uNotifParamNum - 1));
    expected.append('\1');
    expected.append(static_cast<char>(g_uEquals));
    expected.append(nickname.toLatin1());
    expected.append('\0');
    expected.append(static_cast<char>(g_uAny));
    expected.append(static_cast<char>(g_uEquals));
    expected.append(host.toLatin1());
    expected.append('\0');
    expected.append('\0');
    REQUIRE(serializedLength == expected.size());
    REQUIRE(QByteArray(serialized.data(), serializedLength) == expected);

    CCNotif restored;
    REQUIRE(restored.UnSerialize(
                reinterpret_cast<const BYTE*>(serialized.data()),
                serializedLength) == serializedLength);
    REQUIRE(restored == *notification);
    REQUIRE(restored.bActive());
    REQUIRE(restored.GetDaemonExt() != nullptr);
    REQUIRE(restored.GetParam(g_uNetName) == any);

    QByteArray invalid(serialized.data(), serializedLength);
    invalid[0] = static_cast<char>(g_wVersion + 1);
    CCNotif wrongVersion;
    REQUIRE(wrongVersion.UnSerialize(
                reinterpret_cast<const BYTE*>(invalid.constData()),
                invalid.size()) == 0);
    invalid = QByteArray(serialized.data(), serializedLength);
    invalid[1] = static_cast<char>(g_wNoSubsequent);
    CCNotif wrongFlags;
    REQUIRE(wrongFlags.UnSerialize(
                reinterpret_cast<const BYTE*>(invalid.constData()),
                invalid.size()) == 0);
    invalid = QByteArray(serialized.data(), serializedLength);
    invalid[5] = static_cast<char>(g_uEndsWith + 1);
    CCNotif wrongOperator;
    REQUIRE(wrongOperator.UnSerialize(
                reinterpret_cast<const BYTE*>(invalid.constData()),
                invalid.size()) == 0);
    CCNotif truncated;
    REQUIRE(truncated.UnSerialize(
                reinterpret_cast<const BYTE*>(serialized.data()),
                serializedLength - 1) == 0);

    CCNotif caseVariant(notification);
    caseVariant.SetParam(g_uNickname, nickname.toUpper());
    caseVariant.SetParam(g_uHostName, host.toUpper());
    caseVariant.SetParam(g_uNetName, any.toUpper());
    REQUIRE(caseVariant == *notification);

    REQUIRE(StrAddWildcards(nickname, g_uAny, TRUE)
            == QStringLiteral("*"));
    REQUIRE(StrAddWildcards(nickname, g_uEquals, TRUE) == nickname);
    REQUIRE(StrAddWildcards(nickname, g_uContains, TRUE)
            == QLatin1Char('*') + nickname + QLatin1Char('*'));
    REQUIRE(StrAddWildcards(nickname, g_uStartsWith, TRUE)
            == nickname + QLatin1Char('*'));
    REQUIRE(StrAddWildcards(nickname, g_uEndsWith, TRUE)
            == QLatin1Char('*') + nickname);
    const QString quotedNickname = QLatin1Char('\'') + nickname;
    REQUIRE(StrAddWildcards(quotedNickname, g_uContains, TRUE)
            == QStringLiteral("'*") + nickname + QLatin1Char('*'));
    REQUIRE(StrAddWildcards(quotedNickname, g_uEndsWith, TRUE)
            == QStringLiteral("'*") + nickname);

    CCDynaNotifs sorted;
    CCNotif* first = makeNotification(nickname, host, any);
    CCNotif* second = makeNotification(realName, host, any);
    REQUIRE(sorted.bAddNotif(first));
    REQUIRE(sorted.bAddNotif(second));
    sorted.SetFlags(0);
    REQUIRE(sorted.bSortNotifs());
    REQUIRE(sorted.GetNotifsArray().at(0)->GetParam(g_uNickname).compare(
                sorted.GetNotifsArray().at(1)->GetParam(g_uNickname),
                Qt::CaseInsensitive) <= 0);
    sorted.SetFlags(g_wSortDescending);
    REQUIRE(sorted.bSortNotifs());
    REQUIRE(sorted.GetNotifsArray().at(0)->GetParam(g_uNickname).compare(
                sorted.GetNotifsArray().at(1)->GetParam(g_uNickname),
                Qt::CaseInsensitive) >= 0);

    REQUIRE(theApp.m_dynaNotifs.bAddNotif(notification));
    theApp.m_dynaNotifs.SetDisplayNotificationsFunction(
        displayNotifications);
    theApp.m_dynaNotifs.SetSignalNewUpdateFunction(signalNewUpdate);
    REQUIRE(theApp.m_dynaNotifs.bUpdateNotifsDaemonExt(TRUE));
    REQUIRE(theApp.m_dynaNotifs.bStartNotifsDaemon(
        g_uNotifsDaemonShortElapse, TRUE));

    theApp.m_dynaNotifs.OnNotifsDaemonTimer();
    REQUIRE(theApp.m_dynaNotifs.GetWhosCount() == 1);
    CCQuery* query = serverConn.m_queries.FindQuery(ctWho);
    REQUIRE(query != nullptr);
    REQUIRE(query->GetQueryPurpose() == qpOnNotification);
    REQUIRE(query->GetDataType() == dtNotif);
    const QString expectedMask = nickname + QStringLiteral("!*@") + host;
    REQUIRE(query->GetNicknameMask() == expectedMask);

    const QString endWho = QStringLiteral(":%1 315 %2 :%3")
        .arg(host, nickname, listText);
    serverConn.ProcessMessage(whoReply(
        host, nickname, room, userName, host, realName));
    serverConn.ProcessMessage(endWho);
    REQUIRE(theApp.m_dynaNotifs.GetWhosCount() == 0);
    REQUIRE(displayCalls == 1);
    REQUIRE(displayedChanges == QVector<UINT>{1});
    REQUIRE(theApp.m_dynaNotifs.GetNotifUsersArray()->GetSize() == 1);
    CUser* stored = static_cast<CUser*>(
        theApp.m_dynaNotifs.GetNotifUsersArray()->GetAt(0));
    REQUIRE(stored != nullptr);
    REQUIRE((stored->GetFlags() & (g_wVisible | g_wConnected | g_wNew))
            == (g_wVisible | g_wConnected | g_wNew));
    REQUIRE(!(stored->GetFlags() & g_wAltered));
    REQUIRE(stored->m_strRoom == room);

    theApp.m_dynaNotifs.OnNotifsDaemonTimer();
    serverConn.ProcessMessage(whoReply(
        host, nickname, room, userName, host, realName));
    serverConn.ProcessMessage(endWho);
    REQUIRE(displayCalls == 2);
    REQUIRE(displayedChanges.last() == 0);
    REQUIRE(theApp.m_dynaNotifs.GetNotifUsersArray()->GetSize() == 1);

    theApp.m_dynaNotifs.OnNotifsDaemonTimer();
    serverConn.ProcessMessage(whoReply(
        host, nickname, changedRoom, userName, host, realName));
    serverConn.ProcessMessage(endWho);
    REQUIRE(displayCalls == 3);
    REQUIRE(displayedChanges.last() == 1);
    stored = static_cast<CUser*>(
        theApp.m_dynaNotifs.GetNotifUsersArray()->GetAt(0));
    REQUIRE(stored != nullptr);
    REQUIRE(stored->GetFlags() & g_wConnected);
    REQUIRE(stored->m_strRoom == changedRoom);
    REQUIRE(stored->m_strPrettyRoom == DecodeChan(changedRoom));

    theApp.m_dynaNotifs.OnNotifsDaemonTimer();
    REQUIRE(theApp.m_dynaNotifs.GetWhosCount() == 1);
    REQUIRE(!theApp.m_dynaNotifs.bUpdateNotifs());
    REQUIRE(theApp.m_dynaNotifs.GetUpdateCount() == 1);
    serverConn.ProcessMessage(endWho);
    REQUIRE(signalCalls == 1);
    REQUIRE(theApp.m_dynaNotifs.GetUpdateCount() == 0);
    REQUIRE(theApp.m_dynaNotifs.GetWhosCount() == 1);
    REQUIRE(theApp.m_dynaNotifs.GetNotifUsersArray()->GetSize() == 0);
    serverConn.ProcessMessage(endWho);
    REQUIRE(theApp.m_dynaNotifs.GetWhosCount() == 0);

    CCDynaNotifs copied;
    copied = theApp.m_dynaNotifs;
    REQUIRE(copied.GetNotifsArray().size() == 1);
    REQUIRE(copied.GetNotifUsersArray()->GetSize() == 0);
    REQUIRE(copied.GetWhosCount() == 0);
    REQUIRE(copied.GetUpdateCount() == 0);
    REQUIRE(copied.GetModifiedUsersCount() == 0);

    REQUIRE(theApp.m_dynaNotifs.bStopNotifsDaemon());
    serverConn.m_queries.FreeRemoveAll();
    REQUIRE(theApp.m_dynaNotifs.bRemoveNotif(notification));
    REQUIRE(theApp.m_dynaNotifs.bRemoveAllUsers());
    CommunicationCleanup();
    return 0;
}
