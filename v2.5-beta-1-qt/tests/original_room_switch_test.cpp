#include "chat.h"
#include "chatdoc.h"
#include "defines.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "protsupp.h"

#include <QApplication>

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
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.CleanRoomInfos();
    serverConn.m_queries.FreeRemoveAll();

    const QString channel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString password = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString creationModes = originalResourceString(
        QStringLiteral("IDS_AT_CHANNELFLAGS"));
    const QString topic = originalResourceString(
        QStringLiteral("IDS_CHANPROP_ADMIN"));
    REQUIRE(!channel.isEmpty() && !password.isEmpty()
            && !creationModes.isEmpty() && !topic.isEmpty());

    REQUIRE(bInitEnterInfo(g_enterInfo, channel, password,
                           creationModes, MAX_TOPICLEN, FALSE));
    REQUIRE(g_enterInfo.m_strChannel == channel);
    REQUIRE(g_enterInfo.m_strPassword == password);
    REQUIRE(g_enterInfo.m_strCreationModes == creationModes);
    REQUIRE(g_enterInfo.m_dwMaxUsers == MAX_TOPICLEN);
    REQUIRE(g_enterInfo.m_dwModes == (CM_NOEXTERN | CM_TOPICHOST));
    REQUIRE(!g_enterInfo.m_bSetMode);

    auto* stored = new CRoomInfo;
    stored->m_strChannel = channel;
    const int storedIndex = theApp.AddRoomInfo(stored);
    REQUIRE(storedIndex == 1);
    int foundIndex = -1;
    REQUIRE(theApp.GetRoomInfoFromName(channel, &foundIndex) == stored);
    REQUIRE(foundIndex == storedIndex);
    const QString cloneChannel = channel + QString::number(MAX_CHANNELPWD);
    REQUIRE(theApp.GetRoomInfoFromName(cloneChannel, &foundIndex,
                                       true, false) == stored);
    REQUIRE(foundIndex == storedIndex);
    theApp.RemoveRoomInfo(storedIndex);
    REQUIRE(theApp.m_enterInfos.size() == 1);

    CChatDoc document;
    delete document.m_proto;
    auto* protocol = new CapturingIrcProto;
    document.m_proto = protocol;
    protocol->m_doc = &document;
    protocol->m_strChannel = channel;
    protocol->SetConnectionStatus(CX_INCHANNEL);
    currentRoom = protocol;

    REQUIRE(bInitEnterInfo(g_enterInfo, channel, password,
                           creationModes, MAX_TOPICLEN, FALSE));
    g_enterInfo.m_dwModes = CM_HIDDEN | CM_NOEXTERN | CM_MODERATED
        | CM_USERLIMIT | CM_CHANNELKEY;
    g_enterInfo.m_strTopic = topic;
    g_enterInfo.m_bSetMode = TRUE;
    g_enterInfo.m_prgdwTopicFormatting = new CDWordArray;
    g_enterInfo.m_prgdwTopicFormatting->Add(wBold);
    g_enterInfo.m_prgdwTopicFormatting->Add(
        (static_cast<DWORD>(topic.toUtf8().size()) << 16));

    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    serverConn.ProcessMessage(QStringLiteral(":%1 324 %2 %3 +nt")
                                  .arg(server, password, channel));

    REQUIRE(protocol->sent.size() == 3);
    REQUIRE(protocol->sent.at(0)
            == QStringLiteral("MODE %1 -t \r\n").arg(channel));
    REQUIRE(protocol->sent.at(1)
            == QStringLiteral("MODE %1 +smlk %2 %3\r\n")
                   .arg(channel, QString::number(MAX_TOPICLEN), password));
    const QString formattedTopic = QChar::fromLatin1(chCtlBold) + topic
        + QChar::fromLatin1(chCtlBold);
    REQUIRE(protocol->sent.at(2)
            == QStringLiteral("TOPIC %1 :%2\r\n")
                   .arg(channel, formattedTopic));
    REQUIRE(!g_enterInfo.m_bSetMode);
    REQUIRE(g_enterInfo.m_strChannel.isEmpty());
    REQUIRE(g_enterInfo.m_strTopic.isEmpty());
    REQUIRE(g_enterInfo.m_prgdwTopicFormatting == nullptr);
    REQUIRE(document.m_allChannelPuis.isEmpty());
    REQUIRE(document.m_puiSelf == nullptr);

    currentRoom = nullptr;
    serverConn.m_queries.FreeRemoveAll();
    theApp.CleanRoomInfos();
    return EXIT_SUCCESS;
}
