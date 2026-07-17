#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "ircproto.h"
#include "ircsock.h"
#include "memblst.h"
#include "originalassets.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>
#include <QListWidget>

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
    QList<QString> sent;
};

QListWidgetItem* itemForUser(QListWidget* list, CUserInfo* pui)
{
    for (int index = 0; index < list->count(); ++index) {
        QListWidgetItem* item = list->item(index);
        if (item->data(Qt::UserRole).value<void*>() == pui) return item;
    }
    return nullptr;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    REQUIRE(CommunicationInits());
    InitializeAvatars();

    QString otherNick;
    GetNextAvatarName(otherNick);
    REQUIRE(!otherNick.isEmpty());
    const QString selfNick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString channel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString identity = otherNick + QLatin1Char('@') + server;

    CChatDoc document;
    document.m_bComicView = false;
    SetChatDoc(&document);
    delete document.m_proto;
    document.m_proto = nullptr;
    CapturingIrcProto protocol;
    document.m_proto = &protocol;
    protocol.m_doc = &document;
    protocol.m_strChannel = channel;
    protocol.SetConnectionStatus(CX_INCHANNEL);

    CMemberList memberList;
    document.m_memberList = &memberList;
    CUserInfo self(selfNick, QString::fromUtf8(GetMyUserName()));
    self.ComicUser(true);
    CUserInfo other(otherNick, identity);
    other.ComicUser(true);
    document.m_puiSelf = &self;
    g_puiSelf = &self;
    document.m_mapNickToPtr.insert(selfNick.toLower(), &self);
    document.m_mapNickToPtr.insert(otherNick.toLower(), &other);
    document.m_allChannelPuis.append(&self);
    document.m_allChannelPuis.append(&other);
    memberList.AddUser(&self);
    memberList.AddUser(&other);

    auto* list = qobject_cast<QListWidget*>(memberList.FocusWidget());
    REQUIRE(list != nullptr);
    QListWidgetItem* otherItem = itemForUser(list, &other);
    REQUIRE(otherItem != nullptr);
    list->setCurrentItem(otherItem);
    otherItem->setSelected(true);
    REQUIRE(document.SelectedMemberCount() == 1);
    REQUIRE(document.GetSingleSelectedMember() == &other);

    document.OnMemberGetinfo();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :# GetInfo\r\n").arg(otherNick));
    REQUIRE(other.IsRequestInfo(RF_PROFILE));

    const qsizetype historyBeforeIdentity = document.m_history.size();
    const qsizetype sentBeforeIdentity = protocol.sent.size();
    document.OnGetidentity();
    REQUIRE(protocol.sent.size() == sentBeforeIdentity);
    REQUIRE(document.m_history.size() == historyBeforeIdentity + 1);
    auto* identityEntry = dynamic_cast<GetInfoEntry*>(document.m_history.last());
    REQUIRE(identityEntry != nullptr);
    QString expectedIdentity = originalResourceString(
        QStringLiteral("IDS_REPORT_IDENT2"));
    expectedIdentity.replace(QStringLiteral("%1"), other.GetScreenName());
    expectedIdentity.replace(QStringLiteral("%2"), otherNick);
    expectedIdentity.replace(QStringLiteral("%3"), server);
    REQUIRE(identityEntry->m_info == expectedIdentity);

    other.SetFullName(QString());
    document.OnGetidentity();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("WHOIS %1\r\n").arg(otherNick));
    REQUIRE(serverConn.m_queries.FindQuery(ctWhoIs) != nullptr);
    serverConn.ProcessMessage(
        QStringLiteral("311 %1 %2 %2 %3 :%4")
            .arg(selfNick, otherNick, server,
                 originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME"))));
    identityEntry = dynamic_cast<GetInfoEntry*>(document.m_history.last());
    REQUIRE(identityEntry != nullptr);
    REQUIRE(identityEntry->m_info == expectedIdentity);
    serverConn.ProcessMessage(
        QStringLiteral("318 %1 %2").arg(selfNick, otherNick));
    REQUIRE(serverConn.m_queries.FindQuery(ctWhoIs) == nullptr);
    other.SetFullName(identity);

    document.OnGetVersion();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :\001VERSION\001\r\n")
                   .arg(otherNick));
    document.OnGetLocaltime();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :\001TIME\001\r\n")
                   .arg(otherNick));
    document.OnSendEmail();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :\001EMAIL\001\r\n")
                   .arg(otherNick));
    document.OnVisitHomepage();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :\001URL\001\r\n")
                   .arg(otherNick));
    document.OnPingUser();
    REQUIRE(protocol.sent.takeLast().startsWith(
        QStringLiteral("PRIVMSG %1 :\001PING ").arg(otherNick)));
    REQUIRE(other.CheckFlag(UF_REQUESTPING));

    document.OnMemberIgnore();
    REQUIRE(other.Ignored());
    REQUIRE(IsIgnored(identity));
    document.OnMemberIgnore();
    REQUIRE(!other.Ignored());
    REQUIRE(!IsIgnored(identity));

    const QList<OriginalMenuItem> memberMenu = originalMenuResource(
        QStringLiteral("IDR_IRC_MEMBER"));
    REQUIRE(memberMenu.size() == 1);
    REQUIRE(memberMenu.first().type == OriginalMenuItemType::Popup);
    REQUIRE(memberMenu.first().children.first().commandIdentifier
            == QStringLiteral("ID_MEMBER_GETINFO"));
    REQUIRE(memberMenu.first().children.first().text
            == originalMenuItemText(QStringLiteral("ID_MEMBER_GETINFO")));

    document.m_memberList = nullptr;
    document.m_puiSelf = nullptr;
    document.m_mapNickToPtr.clear();
    document.m_allChannelPuis.clear();
    g_puiSelf = nullptr;
    SetChatDoc(nullptr);
    document.m_proto = nullptr;
    DestroyAvatars();
    CommunicationCleanup();
    return EXIT_SUCCESS;
}
