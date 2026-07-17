#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "panel.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>

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

class CapturingIrcProto final : public CIrcProto {
public:
    void SendMessageText(const QString& raw) override { sent.append(raw); }
    void SendMessageBytes(const QByteArray& raw) override
    {
        sent.append(QString::fromUtf8(raw));
    }
    QList<QString> sent;
};

QString wirePayload(const QString& wire)
{
    const qsizetype marker = wire.indexOf(QStringLiteral(" :"));
    if (marker < 0) return QString();
    QString payload = wire.mid(marker + 2);
    if (payload.endsWith(QStringLiteral("\r\n"))) payload.chop(2);
    return payload;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    theApp.InitVals();
    theApp.m_bComicView = true;
    theApp.InitializeComicsFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont, theApp.m_comicsColor));
    InitializeAvatars();

    QString selfAvatarName;
    QString otherAvatarName;
    GetNextAvatarName(selfAvatarName);
    GetNextAvatarName(otherAvatarName);
    REQUIRE(!selfAvatarName.isEmpty());
    REQUIRE(!otherAvatarName.isEmpty());
    REQUIRE(selfAvatarName.compare(otherAvatarName, Qt::CaseInsensitive) != 0);
    theApp.m_myCharacterName = selfAvatarName;

    const QString selfNick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString channel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString identity = otherAvatarName + QLatin1Char('@') + server;

    CChatDoc document;
    SetChatDoc(&document);
    delete document.m_proto;
    document.m_proto = nullptr;
    CapturingIrcProto protocol;
    document.m_proto = &protocol;
    protocol.m_doc = &document;
    protocol.m_strChannel = channel;
    protocol.m_strPrettyChannel = channel;
    protocol.SetConnectionStatus(CX_INCHANNEL);
    document.InitHistory();

    auto* self = new CUserInfo(QLatin1Char('@') + selfNick,
                               QString::fromUtf8(GetMyUserName()));
    AddAndExecute(new JoinEntry(self), &document);
    REQUIRE(g_puiSelf == self);
    REQUIRE(self->IsOperator());

    theApp.m_iGreetingType = AGT_SAY;
    auto* other = new CUserInfo(otherAvatarName, identity);
    const qsizetype sentBeforeGreeting = protocol.sent.size();
    AddAndExecute(new JoinEntry(other, FALSE), &document);
    REQUIRE(protocol.sent.size() == sentBeforeGreeting + 1);
    QString greeting = originalResourceString(QStringLiteral("IDS_DEFAULTGREETING"));
    greeting.replace(QStringLiteral("%1"), other->GetScreenName());
    greeting.replace(QStringLiteral("%2"), channel);
    REQUIRE(protocol.sent.last().startsWith(
        QStringLiteral("PRIVMSG %1 :").arg(channel)));
    REQUIRE(protocol.sent.last().endsWith(greeting + QStringLiteral("\r\n")));

    theApp.m_iGreetingType = AGT_NONE;
    for (int occurrence = 0; occurrence < theApp.m_uFloodCount - 1;
         ++occurrence) {
        REQUIRE(!other->IsFlooding());
    }
    REQUIRE(other->IsFlooding());
    REQUIRE(other->Ignored());
    REQUIRE(IsIgnored(identity));

    protocol.DoIgnoreUser(other, false, false);
    REQUIRE(!other->Ignored());
    REQUIRE(!IsIgnored(identity));

    other->SetFullName(QString());
    const qsizetype sentBeforeWhois = protocol.sent.size();
    protocol.DoIgnoreUser(other, true, true);
    REQUIRE(protocol.sent.size() == sentBeforeWhois + 1);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("WHOIS %1\r\n").arg(otherAvatarName));
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctWhoIs) != nullptr);
    protocol.m_pSock->ProcessMessage(
        QStringLiteral("311 %1 %2 %2 %3 :%4")
            .arg(selfNick, otherAvatarName, server,
                 originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME"))));
    REQUIRE(other->Ignored());
    REQUIRE(IsIgnored(identity));
    protocol.m_pSock->ProcessMessage(
        QStringLiteral("318 %1 %2").arg(selfNick, otherAvatarName));
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctWhoIs) == nullptr);

    protocol.DoIgnoreUser(other, false, false);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctWhoIs) != nullptr);
    protocol.m_pSock->ProcessMessage(
        QStringLiteral("311 %1 %2 %2 %3 :%4")
            .arg(selfNick, otherAvatarName, server,
                 originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME"))));
    protocol.m_pSock->ProcessMessage(
        QStringLiteral("318 %1 %2").arg(selfNick, otherAvatarName));
    REQUIRE(!other->Ignored());
    REQUIRE(!IsIgnored(identity));
    const QString awayMessage = originalResourceString(
        QStringLiteral("IDS_DFLTAWAYMSG"));
    protocol.ChatSetAway(true, awayMessage, nullptr, false);
    REQUIRE(self->CheckFlag(UF_AWAY));
    REQUIRE(protocol.sent.last()
            == QStringLiteral("PRIVMSG %1 :\001AWAY %2\001\r\n")
                   .arg(channel, awayMessage));
    protocol.ChatSetAway(false, QString(), nullptr, false);
    REQUIRE(!self->CheckFlag(UF_AWAY));
    REQUIRE(protocol.sent.last()
            == QStringLiteral("PRIVMSG %1 :\001AWAY\001\r\n").arg(channel));

    theApp.m_uFloodFlags = 0;
    const qsizetype historyBeforeReaction = document.m_history.size();
    ProcessSay(&document, other, QStringLiteral("<Chr>"), MT_CHANNELSEND);
    REQUIRE(document.m_history.size() == historyBeforeReaction + 1);

    const qsizetype sentBeforeVersionRequest = protocol.sent.size();
    protocol.ChatGetVersion(other);
    REQUIRE(protocol.sent.size() == sentBeforeVersionRequest + 1);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("PRIVMSG %1 :\001VERSION\001\r\n")
                   .arg(otherAvatarName));
    REQUIRE(other->IsRequestInfo(RF_VERSION));
    protocol.ReplyVersion(other);
    const QString versionReply = wirePayload(protocol.sent.last());
    REQUIRE(versionReply.startsWith(QStringLiteral("\001VERSION ")));
    OnTextMsg(&document, otherAvatarName, identity, versionReply,
              MT_PRIVATEMSG | MT_NOTICE);
    REQUIRE(!other->IsRequestInfo(RF_VERSION));

    const qsizetype sentBeforePing = protocol.sent.size();
    protocol.ChatPingUser(other);
    REQUIRE(protocol.sent.size() == sentBeforePing + 1);
    REQUIRE(other->CheckFlag(UF_REQUESTPING));
    QString pingReply = wirePayload(protocol.sent.last());
    REQUIRE(pingReply.startsWith(QStringLiteral("\001PING ")));
    OnTextMsg(&document, otherAvatarName, identity, pingReply,
              MT_PRIVATEMSG | MT_NOTICE);
    REQUIRE(!other->CheckFlag(UF_REQUESTPING));

    const qsizetype sentBeforeClientInfo = protocol.sent.size();
    OnTextMsg(&document, otherAvatarName, identity,
              QStringLiteral("\001CLIENTINFO\001"),
              MT_PRIVATEMSG | MT_PRVMSG);
    REQUIRE(protocol.sent.size() == sentBeforeClientInfo + 1);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("NOTICE %1 :\001CLIENTINFO ACTION AWAY CLIENTINFO DCC EMAIL NETMEET PING SOUND TIME USERINFO URL VERSION\001\r\n")
                   .arg(otherAvatarName));

    for (CUserInfo* pui : document.m_allChannelPuis) {
        if (pui) {
            if (CAvatarX* avatar = GetAvatar(pui->GetAvatarID())) {
                if (avatar->m_userInfo == pui) avatar->m_userInfo = nullptr;
            }
        }
    }
    g_puiSelf = nullptr;
    g_mapNickToPtr->clear();
    document.m_puiSelf = nullptr;
    SetChatDoc(nullptr);
    document.m_proto = nullptr;
    CUnitPanelPage::DestroyFonts();
    DestroyAvatars();
    return 0;
}
