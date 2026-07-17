#include "chat.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "protsupp.h"
#include "rules.h"
#include "setupdlg.h"

#include <QCoreApplication>

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

QString ruleSetName(const QString& resource)
{
    const QString stored = originalResourceString(resource);
    return stored.mid(stored.indexOf(QLatin1Char('|')) + 1);
}

CCRule* activationRule(enumEvents event, const QString& firstParameter,
                       const QString& serverParameter,
                       CCRuleSet* target)
{
    auto* rule = new CCRule(&theApp.m_dynaRules);
    rule->SetEvent(theApp.m_rulesData.GetEvent(event));
    rule->SetEventKeyParam(0, kepMax);
    rule->SetEventParam(0, firstParameter);
    if (event == eOnConnect || event == eOnDisconnect) {
        rule->SetEventKeyParam(1, kepMax);
        rule->SetEventParam(1, serverParameter);
    }
    rule->SetAction(theApp.m_rulesData.GetAction(aActivateRuleSet));
    rule->SetActionKeyParam(0, kapMax);
    rule->SetActionParam(0, target->GetName());
    rule->SetActionKeyParam(1, kapYes);
    rule->SetActionParam(1,
        theApp.m_rulesData.GetKeyActionParam(kapYes));
    rule->SetFlags(g_wActive);
    return rule;
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    theApp.InitVals();
    REQUIRE(theApp.m_rulesData.bInitAlloc());
    REQUIRE(theApp.m_rulesData.bLoadStrings());
    REQUIRE(CommunicationInits());

    const QStringList sample = originalResourceString(
        QStringLiteral("IDS_SAMPLES_RULE2")).split(QLatin1Char('|'));
    REQUIRE(sample.size() >= 5);
    const QString identityMask = sample.at(3);
    const QString serverParameter = sample.at(4);
    const qsizetype at = identityMask.indexOf(QLatin1Char('@'));
    REQUIRE(at >= 0 && at + 1 < identityMask.size());
    const QString host = identityMask.mid(at + 1);
    const QString wireServer = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString nickname = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString userName = QString::fromUtf8(GetMyUserName());
    const QString realName = originalResourceString(
        QStringLiteral("IDS_DEFAULT_REALNAME"));
    const QString room = EncodeChan(originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL")));
    const QString listText = originalResourceString(
        QStringLiteral("ID_RL_DESCR_LABEL"));
    REQUIRE(!identityMask.isEmpty() && !serverParameter.isEmpty()
            && !wireServer.isEmpty()
            && !host.isEmpty() && !nickname.isEmpty()
            && !userName.isEmpty() && !realName.isEmpty()
            && !room.isEmpty() && !listText.isEmpty());

    ChatSetServer(serverParameter);
    CIrcProto* defaultProtocol = GetIrcProto();
    REQUIRE(defaultProtocol != nullptr);
    defaultProtocol->SetConnectionStatus(CX_NOCHANNEL);

    auto* target = new CCRuleSet(&theApp.m_dynaRules);
    target->SetName(ruleSetName(QStringLiteral("IDS_SAMPLES_RULESET")));
    auto* rules = new CCRuleSet(&theApp.m_dynaRules);
    rules->SetName(ruleSetName(QStringLiteral("IDS_GENERAL_RULESET")));
    rules->Activate();
    CCRule* connectRule = activationRule(
        eOnConnect, identityMask, serverParameter, target);
    CCRule* disconnectRule = activationRule(
        eOnDisconnect, identityMask, serverParameter, target);
    CCRule* roomRule = activationRule(
        eOnNewRoom, DecodeChan(room), QString(), target);
    REQUIRE(rules->bAddRule(connectRule));
    REQUIRE(rules->bAddRule(disconnectRule));
    REQUIRE(rules->bAddRule(roomRule));
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(target));
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(rules));
    REQUIRE(theApp.m_dynaRules.bUpdateRuleSetsDaemonExt(TRUE));
    REQUIRE(connectRule->GetDaemonExt()
            && disconnectRule->GetDaemonExt()
            && roomRule->GetDaemonExt());

    REQUIRE(theApp.m_dynaRules.bStartRulesDaemon(
        g_uRulesDaemonShortElapse, TRUE));
    theApp.m_dynaRules.OnRulesDaemonTimer();
    CCQuery* timerWho = serverConn.m_queries.FindQuery(ctWho);
    CCQuery* timerList = serverConn.m_queries.FindQuery(ctList);
    REQUIRE(timerWho != nullptr && timerList != nullptr);
    REQUIRE(timerWho->GetQueryPurpose() == qpOnConnectEvent);
    REQUIRE(timerList->GetQueryPurpose() == qpOnNewRoomEvent);
    serverConn.m_queries.FreeRemoveAll();
    REQUIRE(theApp.m_dynaRules.bStopRulesDaemon());

    CapturingIrcProto protocol;
    protocol.SetConnectionStatus(CX_NOCHANNEL);
    REQUIRE(protocol.bExecuteQuery(qpOnConnectEvent, ctWho, dtRule,
                                   connectRule, QString(), identityMask));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("WHO %1\r\n").arg(host));
    const QString whoReply = QStringLiteral(
        ":%1 352 %2 %3 %4 %5 %1 %2 H :0 %6")
        .arg(wireServer, nickname, room, userName, host, realName);
    const QString endWho = QStringLiteral(":%1 315 %2 :%3")
        .arg(wireServer, nickname, listText);
    serverConn.ProcessMessage(whoReply);
    serverConn.ProcessMessage(endWho);
    REQUIRE(target->bActive());

    target->Desactivate();
    REQUIRE(protocol.bExecuteQuery(qpOnConnectEvent, ctWho, dtRule,
                                   connectRule, QString(), identityMask));
    serverConn.ProcessMessage(whoReply);
    serverConn.ProcessMessage(endWho);
    REQUIRE(!target->bActive());

    REQUIRE(protocol.bExecuteQuery(qpOnDisconnectEvent, ctWho, dtRule,
                                   disconnectRule, QString(), identityMask));
    serverConn.ProcessMessage(whoReply);
    serverConn.ProcessMessage(endWho);
    REQUIRE(!target->bActive());
    REQUIRE(protocol.bExecuteQuery(qpOnDisconnectEvent, ctWho, dtRule,
                                   disconnectRule, QString(), identityMask));
    serverConn.ProcessMessage(endWho);
    REQUIRE(target->bActive());

    target->Desactivate();
    REQUIRE(protocol.bExecuteQuery(qpOnNewRoomEvent, ctList, dtRule,
                                   roomRule, room, QString()));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("LIST %1\r\n").arg(room));
    serverConn.ProcessMessage(QStringLiteral(":%1 321 %2 :%3")
                                  .arg(wireServer, nickname, listText));
    serverConn.ProcessMessage(QStringLiteral(":%1 322 %2 %3 1 :%4")
                                  .arg(wireServer, nickname, room,
                                       listText));
    serverConn.ProcessMessage(QStringLiteral(":%1 323 %2 :%3")
                                  .arg(wireServer, nickname, listText));
    REQUIRE(target->bActive());

    target->Desactivate();
    REQUIRE(protocol.bExecuteQuery(qpOnNewRoomEvent, ctList, dtRule,
                                   roomRule, room, QString()));
    serverConn.ProcessMessage(QStringLiteral(":%1 321 %2 :%3")
                                  .arg(wireServer, nickname, listText));
    serverConn.ProcessMessage(QStringLiteral(":%1 322 %2 %3 1 :%4")
                                  .arg(wireServer, nickname, room,
                                       listText));
    serverConn.ProcessMessage(QStringLiteral(":%1 323 %2 :%3")
                                  .arg(wireServer, nickname, listText));
    REQUIRE(!target->bActive());

    serverConn.m_queries.FreeRemoveAll();
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(rules));
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(target));
    CommunicationCleanup();
    return 0;
}
