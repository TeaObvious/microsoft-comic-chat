#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "ircproto.h"
#include "ircsock.h"
#include "memblst.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "protsupp.h"
#include "rules.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>
#include <QListWidget>

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

CCRule* addHighlightRule(CCRuleSet* ruleSet, enumEvents event,
                         enumKeyEventParam identityKey,
                         int oneBasedHighlightType)
{
    auto* rule = new CCRule(&theApp.m_dynaRules);
    rule->SetEvent(theApp.m_rulesData.GetEvent(event));
    rule->SetEventKeyParam(0, identityKey);
    rule->SetEventParam(0,
        theApp.m_rulesData.GetKeyEventParam(identityKey));
    rule->SetEventKeyParam(1, kepMyActivatedRoom);
    rule->SetEventParam(1,
        theApp.m_rulesData.GetKeyEventParam(kepMyActivatedRoom));
    rule->SetAction(theApp.m_rulesData.GetAction(aHighlightMessage));
    QString highlight = originalResourceString(
        QStringLiteral("IDS_HIGHLIGHT_TYPE"));
    highlight.replace(QStringLiteral("%d"),
                      QString::number(oneBasedHighlightType));
    rule->SetActionKeyParam(0, kapMax);
    rule->SetActionParam(0, highlight);
    rule->SetFlags(g_wActive);
    REQUIRE(ruleSet->bAddRule(rule));
    return rule;
}

}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    theApp.InitVals();
    REQUIRE(theApp.m_rulesData.bInitAlloc());
    REQUIRE(theApp.m_rulesData.bLoadStrings());
    theApp.m_bComicView = true;
    theApp.InitializeComicsFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont, theApp.m_comicsColor));
    InitializeAvatars();

    QString firstSourceName;
    QString secondSourceName;
    QString thirdSourceName;
    GetNextAvatarName(firstSourceName);
    GetNextAvatarName(secondSourceName);
    GetNextAvatarName(thirdSourceName);
    REQUIRE(!firstSourceName.isEmpty());
    REQUIRE(!secondSourceName.isEmpty());
    REQUIRE(!thirdSourceName.isEmpty());
    REQUIRE(firstSourceName.compare(secondSourceName, Qt::CaseInsensitive) != 0);
    REQUIRE(firstSourceName.compare(thirdSourceName, Qt::CaseInsensitive) != 0);
    REQUIRE(secondSourceName.compare(thirdSourceName, Qt::CaseInsensitive) != 0);
    theApp.m_myCharacterName = firstSourceName;

    REQUIRE(CommunicationInits());
    CChatDoc document;
    SetChatDoc(&document);
    CPageView view;
    document.m_view = &view;
    CMemberList members;
    document.m_memberList = &members;
    members.SetIconMode(true);

    const QString nick = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
    const QString channel = originalResourceString(QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString user = QString::fromUtf8(GetMyUserName());
    serverConn.ProcessMessage(
        QStringLiteral(":%1!%2@NoMachine JOIN :%3").arg(nick, user, channel));
    REQUIRE(document.m_proto != nullptr);
    REQUIRE(document.m_proto != GetIrcProto());
    CIrcProto& protocol = *document.m_proto;
    REQUIRE(document.GetConnectionStatus() == CX_INCHANNEL);
    REQUIRE(theApp.m_nMyIdentLength == user.size() + QStringLiteral("@NoMachine").size() + 2);
    REQUIRE(QString::fromUtf8(GetMyIdent()) == user + QStringLiteral("@NoMachine"));
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctNames) != nullptr);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctTopic) != nullptr);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctGetChannelMode) != nullptr);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctWho) != nullptr);

    protocol.SetVisibility(false);
    CCQuery* visibilityQuery = protocol.m_pSock->m_queries.FindQuery(
        ctSetUserMode);
    REQUIRE(visibilityQuery != nullptr);
    REQUIRE(visibilityQuery->GetQueryPurpose() == qpSetInvisible);
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":NoMachine MODE %1 :+i").arg(nick));
    REQUIRE((theApp.m_flags1 & F1_USERVISIBLE) == 0);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctSetUserMode) == nullptr);

    protocol.SetVisibility(true);
    visibilityQuery = protocol.m_pSock->m_queries.FindQuery(ctSetUserMode);
    REQUIRE(visibilityQuery != nullptr);
    REQUIRE(visibilityQuery->GetQueryPurpose() == qpSetVisible);
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":NoMachine MODE %1 :-i").arg(nick));
    REQUIRE(theApp.m_flags1 & F1_USERVISIBLE);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctSetUserMode) == nullptr);

    protocol.m_pSock->ProcessMessage(
        QStringLiteral("324 %1 %2 +mnl 5").arg(nick, channel));
    REQUIRE((protocol.m_dwModes & (CM_MODERATED | CM_NOEXTERN | CM_USERLIMIT))
            == (CM_MODERATED | CM_NOEXTERN | CM_USERLIMIT));
    REQUIRE(protocol.m_dwMaxUsers == 5);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctGetChannelMode) == nullptr);

    const QString sourceTopic = originalResourceString(QStringLiteral("ID_STARRING"));
    protocol.m_pSock->ProcessMessage(
        QStringLiteral("332 %1 %2 :%3").arg(nick, channel, sourceTopic));
    REQUIRE(protocol.m_strTopic == sourceTopic);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctTopic) == nullptr);

    auto* eventRuleSet = new CCRuleSet(&theApp.m_dynaRules);
    eventRuleSet->SetName(originalResourceString(
        QStringLiteral("IDS_GENERAL_RULESET")));
    eventRuleSet->Activate();
    addHighlightRule(eventRuleSet, eOnJoin, kepMe, 4);
    addHighlightRule(eventRuleSet, eOnJoin, kepAnyoneButMe, 1);
    addHighlightRule(eventRuleSet, eOnLeave, kepAnyoneButMe, 2);
    addHighlightRule(eventRuleSet, eOnLeave, kepMe, 4);
    addHighlightRule(eventRuleSet, eOnNewHost, kepAnyoneButMe, 3);
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(eventRuleSet));

    protocol.m_pSock->ProcessMessage(
        QStringLiteral("353 %1 = %2 :%1 %3").arg(nick, channel, firstSourceName));
    REQUIRE(members.count() == 2);
    REQUIRE(theApp.m_dynaRules.GetFlags() & g_wHighlight);
    REQUIRE((theApp.m_dynaRules.GetFlags() >> 8) == 3);
    CUserInfo* other = LookupPui(firstSourceName);
    REQUIRE(other != nullptr);
    REQUIRE(g_puiSelf != nullptr);
    REQUIRE(other->IsSpectator());
    QListWidget* memberWidget = members.findChild<QListWidget*>();
    REQUIRE(memberWidget != nullptr);
    REQUIRE(memberWidget->count() == 2);
    REQUIRE(memberWidget->iconSize() == QSize(58, 40));
    for (int index = 0; index < memberWidget->count(); ++index) {
        QListWidgetItem* item = memberWidget->item(index);
        REQUIRE(!memberWidget->item(index)->icon().isNull());
        auto* member = static_cast<CUserInfo*>(
            item->data(Qt::UserRole).value<void*>());
        REQUIRE(member != nullptr);
        CAvatarX* avatar = GetAvatar(member->GetAvatarID());
        REQUIRE(avatar != nullptr);
        CPose* iconPose = avatar->GetIconPose();
        REQUIRE(iconPose != nullptr);
        CDIB* drawing = iconPose->GetDrawing();
        REQUIRE(drawing != nullptr);
        const QImage sourceIcon = drawing->Image();
        REQUIRE(!sourceIcon.isNull());
        REQUIRE(sourceIcon.width() <= 40);
        REQUIRE(sourceIcon.height() <= 40);
        const QImage displayed = item->icon().pixmap(QSize(58, 40)).toImage();
        REQUIRE(displayed.size() == QSize(58, 40));
        for (int y = 0; y < sourceIcon.height(); ++y) {
            for (int x = 0; x < sourceIcon.width(); ++x) {
                REQUIRE(displayed.pixelColor(x + 18, y).rgb()
                        == sourceIcon.pixelColor(x, y).rgb());
            }
        }
    }

    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":NoMachine MODE %1 -m").arg(channel));
    REQUIRE((protocol.m_dwModes & CM_MODERATED) == 0);
    REQUIRE(!other->IsSpectator());
    REQUIRE(!g_puiSelf->IsSpectator());
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":NoMachine MODE %1 +m").arg(channel));
    REQUIRE(protocol.m_dwModes & CM_MODERATED);
    REQUIRE(other->IsSpectator());

    protocol.m_pSock->ProcessMessage(
        QStringLiteral("352 %1 %2 %3 NoMachine NoMachine %3 H :0 %4")
            .arg(nick, channel, firstSourceName,
                 originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME"))));
    REQUIRE(other->GetFullName() == firstSourceName + QStringLiteral("@NoMachine"));
    protocol.m_pSock->ProcessMessage(QStringLiteral("315 %1 %2").arg(nick, channel));
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctWho) == nullptr);

    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":NoMachine MODE %1 +o %2").arg(channel, firstSourceName));
    REQUIRE(other->IsOperator());
    REQUIRE(theApp.m_dynaRules.GetFlags() & g_wHighlight);
    REQUIRE((theApp.m_dynaRules.GetFlags() >> 8) == 2);
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":NoMachine MODE %1 +v %2").arg(channel, firstSourceName));
    REQUIRE(other->CheckFlag(UF_HASVOICE));

    protocol.m_pSock->ProcessMessage(QStringLiteral("366 %1 %2").arg(nick, channel));
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctNames) == nullptr);
    REQUIRE(!document.m_pages.isEmpty());
    REQUIRE(document.m_pages.first()->m_panels.first()->m_elements.size() >= 4);

    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%1@NoMachine NICK :%2")
            .arg(firstSourceName, secondSourceName));
    REQUIRE(LookupPui(firstSourceName) == nullptr);
    REQUIRE(LookupPui(secondSourceName.toUpper()) == other);
    REQUIRE(other->GetName() == secondSourceName);

    CAvatarX* selfAvatar = GetAvatar(g_puiSelf->GetAvatarID());
    CAvatarX* otherAvatar = GetAvatar(other->GetAvatarID());
    auto* kickRuleSet = new CCRuleSet(&theApp.m_dynaRules);
    kickRuleSet->SetName(originalResourceString(
        QStringLiteral("IDS_GENERAL_RULESET")));
    kickRuleSet->Activate();
    auto* kickRule = new CCRule(&theApp.m_dynaRules);
    kickRule->SetEvent(theApp.m_rulesData.GetEvent(eOnKick));
    kickRule->SetEventKeyParam(0, kepAnyoneButMe);
    kickRule->SetEventParam(0, theApp.m_rulesData.GetKeyEventParam(
        kepAnyoneButMe));
    kickRule->SetEventKeyParam(1, kepMyActivatedRoom);
    kickRule->SetEventParam(1, theApp.m_rulesData.GetKeyEventParam(
        kepMyActivatedRoom));
    kickRule->SetAction(theApp.m_rulesData.GetAction(aHighlightMessage));
    QString kickHighlight = originalResourceString(
        QStringLiteral("IDS_HIGHLIGHT_TYPE"));
    kickHighlight.replace(QStringLiteral("%d"), QStringLiteral("2"));
    kickRule->SetActionKeyParam(0, kapMax);
    kickRule->SetActionParam(0, kickHighlight);
    kickRule->SetFlags(g_wActive);
    REQUIRE(kickRuleSet->bAddRule(kickRule));
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(kickRuleSet));
    const int historyBeforeKick = document.m_history.size();
    QString expectedKick = originalResourceString(
        QStringLiteral("ID_KICK_NO_MESG"));
    expectedKick.replace(QStringLiteral("%1"), g_puiSelf->GetScreenName());
    expectedKick.replace(QStringLiteral("%2"), other->GetScreenName());
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%2@NoMachine KICK %3 %4 :")
            .arg(nick, user, channel, secondSourceName));
    REQUIRE(document.m_history.size() == historyBeforeKick + 2);
    auto* kickAction = dynamic_cast<SayEntry*>(
        document.m_history.at(historyBeforeKick));
    auto* kickPart = dynamic_cast<PartEntry*>(
        document.m_history.at(historyBeforeKick + 1));
    REQUIRE(kickAction != nullptr);
    REQUIRE(kickPart != nullptr);
    REQUIRE(kickAction->m_mesg == expectedKick);
    REQUIRE(kickAction->m_udi.m_uModes == BM_ACTION);
    REQUIRE(kickAction->m_udi.m_bbCooked == 0);
    REQUIRE(kickAction->m_udi.m_bbReq == 1);
    REQUIRE(kickAction->m_udi.m_talkTos.size() == 1);
    REQUIRE(kickAction->m_udi.m_talkTos.first() == other);
    REQUIRE(kickAction->m_cHighlightType == 1);
    REQUIRE(kickPart->m_name == secondSourceName);
    REQUIRE(kickPart->m_cHighlightType == 1);
    REQUIRE(other->IsDeparted());
    REQUIRE(members.count() == 1);
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(kickRuleSet));

    const int historyBeforeJoin = document.m_history.size();
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%2@NoMachine JOIN :%3")
            .arg(thirdSourceName, user, channel));
    REQUIRE(document.m_history.size() == historyBeforeJoin + 1);
    auto* liveJoin = dynamic_cast<JoinEntry*>(document.m_history.last());
    REQUIRE(liveJoin != nullptr);
    REQUIRE(liveJoin->m_name == thirdSourceName);
    REQUIRE(liveJoin->m_cHighlightType == 0);
    REQUIRE(members.count() == 2);

    const int historyBeforePart = document.m_history.size();
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%2@NoMachine PART %3")
            .arg(thirdSourceName, user, channel));
    REQUIRE(document.m_history.size() == historyBeforePart + 1);
    auto* livePart = dynamic_cast<PartEntry*>(document.m_history.last());
    REQUIRE(livePart != nullptr);
    REQUIRE(livePart->m_name == thirdSourceName);
    REQUIRE(livePart->m_cHighlightType == 1);
    REQUIRE(members.count() == 1);

    protocol.m_pSock->m_bIrcXServer = true;
    theApp.m_bSaveViewMode = true;
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":NoMachine MODE %1 +f").arg(channel));
    REQUIRE(protocol.m_dwModes & CM_NOFORMAT);
    REQUIRE(!document.m_bComicView);
    REQUIRE(!theApp.m_bSaveViewMode);

    protocol.ChatPartChannel(&document, false);
    REQUIRE(document.GetConnectionStatus() == CX_NOCHANNEL);
    REQUIRE(theApp.m_dynaRules.GetFlags() & g_wHighlight);
    REQUIRE((theApp.m_dynaRules.GetFlags() >> 8) == 3);
    protocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%2@NoMachine PART %3").arg(nick, user, channel));
    REQUIRE(document.GetConnectionStatus() == CX_NOCHANNEL);
    REQUIRE(members.count() == 0);
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(eventRuleSet));

    // bInitEnterInfo gives a newly created room CM_NOEXTERN|CM_TOPICHOST;
    // the corresponding original CREATE channel flags are +nt.
    serverConn.m_bIrcXServer = false;
    serverConn.ProcessMessage(
        QStringLiteral(":NoMachine CREATE %1 +nt").arg(channel));
    REQUIRE(document.GetConnectionStatus() == CX_INCHANNEL);
    REQUIRE(document.m_proto != nullptr);
    REQUIRE(document.m_proto->m_pSock == &serverConn);
    REQUIRE(serverConn.m_queries.FindQuery(ctNames) != nullptr);
    REQUIRE(serverConn.m_queries.FindQuery(ctTopic) != nullptr);
    REQUIRE(serverConn.m_queries.FindQuery(ctGetChannelMode) != nullptr);
    REQUIRE(serverConn.m_queries.FindQuery(ctWho) != nullptr);

    if (selfAvatar) selfAvatar->m_userInfo = nullptr;
    if (otherAvatar) otherAvatar->m_userInfo = nullptr;
    g_puiSelf = nullptr;
    g_mapNickToPtr->clear();
    document.m_puiSelf = nullptr;
    document.m_memberList = nullptr;
    SetChatDoc(nullptr);
    CommunicationCleanup();
    CUnitPanelPage::DestroyFonts();
    DestroyAvatars();
    return 0;
}
