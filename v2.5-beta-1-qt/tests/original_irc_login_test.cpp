#include "chat.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "protsupp.h"
#include "rules.h"

#include <QCoreApplication>
#include <QHostInfo>

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
    void SendMessageText(const QString& raw) override
    {
        sent.append(raw.toUtf8());
    }

    QByteArray sent;
};
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    theApp.InitVals();
    REQUIRE(theApp.m_rulesData.bInitAlloc());
    REQUIRE(theApp.m_rulesData.bLoadStrings());

    REQUIRE(theApp.m_myName == originalResourceString(QStringLiteral("IDS_DEFAULT_NICK")));
    REQUIRE(theApp.m_myNick == theApp.m_myName);
    REQUIRE(theApp.m_myRealName
            == originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME")));
    REQUIRE(theApp.m_myChannel
            == originalResourceString(QStringLiteral("IDS_DEFAULT_CHANNEL")));

    CapturingIrcProto protocol;
    protocol.SetConnectionStatus(CX_CONNECTING);
    protocol.m_pSock->OnConnect();
    REQUIRE(protocol.sent == QByteArray("MODE ISIRCX\r\n"));

    protocol.m_pSock->HrModeIsIrcXFailure();
    QString machineName = QHostInfo::localHostName();
    if (machineName.isEmpty()) machineName = QStringLiteral("NoMachine");
    const QByteArray expected = QByteArray("MODE ISIRCX\r\n")
        + QStringLiteral("NICK %1\r\n").arg(theApp.m_myName).toUtf8()
        + QStringLiteral("USER %1 %2 . :%3\r\n")
              .arg(theApp.m_myName, machineName, theApp.m_myRealName).toUtf8();
    REQUIRE(protocol.sent == expected);

    protocol.SetVisibility(false);
    REQUIRE(protocol.sent == expected
        + QStringLiteral("MODE %1 +i\r\n").arg(theApp.m_myNick).toUtf8());
    protocol.SetVisibility(true);
    REQUIRE(protocol.sent == expected
        + QStringLiteral("MODE %1 +i\r\nMODE %1 -i\r\n")
              .arg(theApp.m_myNick).toUtf8());
    REQUIRE(CommunicationInits());

    const auto ruleSetName = [](const QString& resource) {
        const QString stored = originalResourceString(resource);
        return stored.mid(stored.indexOf(QLatin1Char('|')) + 1);
    };
    auto* targetSet = new CCRuleSet(&theApp.m_dynaRules);
    targetSet->SetName(ruleSetName(QStringLiteral("IDS_SAMPLES_RULESET")));
    auto* connectSet = new CCRuleSet(&theApp.m_dynaRules);
    connectSet->SetName(ruleSetName(QStringLiteral("IDS_GENERAL_RULESET")));
    connectSet->Activate();
    auto* connectRule = new CCRule(&theApp.m_dynaRules);
    connectRule->SetEvent(theApp.m_rulesData.GetEvent(eOnConnect));
    connectRule->SetEventKeyParam(0, kepMe);
    connectRule->SetEventParam(0,
        theApp.m_rulesData.GetKeyEventParam(kepMe));
    connectRule->SetEventKeyParam(1, kepAny);
    connectRule->SetEventParam(1,
        theApp.m_rulesData.GetKeyEventParam(kepAny));
    connectRule->SetAction(
        theApp.m_rulesData.GetAction(aActivateRuleSet));
    connectRule->SetActionKeyParam(0, kapMax);
    connectRule->SetActionParam(0, targetSet->GetName());
    connectRule->SetActionKeyParam(1, kapYes);
    connectRule->SetActionParam(1,
        theApp.m_rulesData.GetKeyActionParam(kapYes));
    connectRule->SetFlags(g_wActive);
    REQUIRE(connectSet->bAddRule(connectRule));
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(targetSet));
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(connectSet));

    protocol.SetConnectionStatus(CX_CONNECTING);
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":NoMachine 001 %1 :%2")
            .arg(theApp.m_myNick,
                 originalResourceString(QStringLiteral("IDS_CONNECTION"))));
    REQUIRE(targetSet->bActive());
    REQUIRE(protocol.GetConnectionStatus() == CX_NOCHANNEL);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctLUsersMOTD) != nullptr);
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(connectSet));
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(targetSet));

    auto* disconnectTargetSet = new CCRuleSet(&theApp.m_dynaRules);
    disconnectTargetSet->SetName(
        ruleSetName(QStringLiteral("IDS_SAMPLES_RULESET")));
    auto* disconnectSet = new CCRuleSet(&theApp.m_dynaRules);
    disconnectSet->SetName(
        ruleSetName(QStringLiteral("IDS_GENERAL_RULESET")));
    disconnectSet->Activate();
    auto* disconnectRule = new CCRule(&theApp.m_dynaRules);
    disconnectRule->SetEvent(theApp.m_rulesData.GetEvent(eOnDisconnect));
    disconnectRule->SetEventKeyParam(0, kepMe);
    disconnectRule->SetEventParam(0,
        theApp.m_rulesData.GetKeyEventParam(kepMe));
    disconnectRule->SetEventKeyParam(1, kepAny);
    disconnectRule->SetEventParam(1,
        theApp.m_rulesData.GetKeyEventParam(kepAny));
    disconnectRule->SetAction(
        theApp.m_rulesData.GetAction(aActivateRuleSet));
    disconnectRule->SetActionKeyParam(0, kapMax);
    disconnectRule->SetActionParam(0, disconnectTargetSet->GetName());
    disconnectRule->SetActionKeyParam(1, kapYes);
    disconnectRule->SetActionParam(1,
        theApp.m_rulesData.GetKeyActionParam(kapYes));
    disconnectRule->SetFlags(g_wActive);
    REQUIRE(disconnectSet->bAddRule(disconnectRule));
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(disconnectTargetSet));
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(disconnectSet));

    CIrcProto* defaultProtocol = GetIrcProto();
    REQUIRE(defaultProtocol != nullptr);
    defaultProtocol->SetConnectionStatus(CX_NOCHANNEL);
    ChatServerDisconnect(TRUE, FALSE);
    REQUIRE(disconnectTargetSet->bActive());
    REQUIRE(defaultProtocol->GetConnectionStatus() == CX_DISCONNECTED);
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(disconnectSet));
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(disconnectTargetSet));
    CommunicationCleanup();
    return 0;
}
