#include "chat.h"
#include "ircproto.h"
#include "originalassets.h"

#include <QCoreApplication>
#include <QStringList>

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
    QStringList sent;
};
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    theApp.InitVals();

    const QString channel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString nickname = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString away = originalResourceString(
        QStringLiteral("IDS_DFLTAWAYMSG"));
    REQUIRE(!channel.isEmpty());
    REQUIRE(!nickname.isEmpty());
    REQUIRE(!away.isEmpty());

    CapturingIrcProto protocol;
    protocol.SetConnectionStatus(CX_INCHANNEL);
    protocol.m_strChannel = channel;

    REQUIRE(protocol.ProcessSlashCommand(QStringLiteral("/NAMES"), nullptr,
                                         BM_SAY));
    REQUIRE(protocol.sent.takeLast() == QStringLiteral("NAMES\r\n"));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/INVITE %1 %2").arg(nickname, channel), nullptr,
        BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("INVITE %1 %2\r\n").arg(nickname, channel));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/ISON %1").arg(nickname), nullptr, BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("ISON %1\r\n").arg(nickname));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/KICK %1 %2").arg(channel, nickname), nullptr,
        BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("KICK %1 %2\r\n").arg(channel, nickname));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/MODE %1").arg(channel), nullptr, BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("MODE %1\r\n").arg(channel));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/MODE %1 +m").arg(channel), nullptr, BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("MODE %1 +m\r\n").arg(channel));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/MODE %1 +i").arg(nickname), nullptr, BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("MODE %1  +i\r\n").arg(nickname));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/PROP %1 CLIENT").arg(channel), nullptr, BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PROP %1 CLIENT\r\n").arg(channel));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/PROP %1 CLIENT :CLIENT").arg(channel), nullptr,
        BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PROP %1 CLIENT :CLIENT\r\n").arg(channel));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/NICK \"%1\"").arg(nickname), nullptr, BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("NICK %1\r\n").arg(nickname));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/RAW WHOIS %1").arg(nickname), nullptr, BM_SAY));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("WHOIS %1\r\n").arg(nickname));

    REQUIRE(protocol.ProcessSlashCommand(
        QStringLiteral("/AWAY %1").arg(away), nullptr, BM_SAY));
    REQUIRE(theApp.m_bAway);
    REQUIRE(theApp.m_bAwayPrompt);
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("AWAY :%1\r\n").arg(away));
    REQUIRE(protocol.ProcessSlashCommand(QStringLiteral("/AWAY"), nullptr,
                                         BM_SAY));
    REQUIRE(!theApp.m_bAway);
    REQUIRE(!theApp.m_bAwayPrompt);
    REQUIRE(protocol.sent.takeLast() == QStringLiteral("AWAY\r\n"));

    const QString prettyChannel = channel.mid(1);
    const QString encodedChannel = EncodeChan(prettyChannel);
    REQUIRE(encodedChannel == QStringLiteral("%#") + prettyChannel);
    REQUIRE(DecodeChan(encodedChannel) == prettyChannel);
    REQUIRE(EncodeChan(channel) == channel);
    REQUIRE(!bExtendedNickname(nickname));

    const QString syntax = protocol.StrSyntaxMessage(cmdidKick);
    REQUIRE(syntax.startsWith(originalResourceString(IDS_SYNTAXPREFIX)));
    REQUIRE(syntax.contains(QString::fromLatin1(g_rgIrcCmd[cmdidKick].szCmd)));
    REQUIRE(syntax.contains(originalResourceString(IDS_KICKMSG_SYNTAX)));

    REQUIRE(protocol.sent.isEmpty());
    return 0;
}
