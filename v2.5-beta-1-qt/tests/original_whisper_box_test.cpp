#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "ircproto.h"
#include "originalassets.h"
#include "panel.h"
#include "protsupp.h"
#include "rules.h"
#include "setupdlg.h"
#include "userinfo.h"
#include "userlist.h"
#include "whisprbx.h"

#include <QApplication>
#include <QCheckBox>
#include <QFontMetrics>
#include <QPushButton>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidgetItem>

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

class DialogUnits {
public:
    explicit DialogUnits(const QFont& font)
    {
        const QFontMetrics metrics(font);
        const QString alphabet = QStringLiteral(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
        baseY = qMax(1, metrics.height());
    }
    int x(int value) const { return (value * baseX + 2) / 4; }
    int y(int value) const { return (value * baseY + 4) / 8; }
    QRect rect(const OriginalDialogControl& control) const
    {
        return {x(control.x), y(control.y),
                x(control.width), y(control.height)};
    }
private:
    int baseX = 1;
    int baseY = 1;
};

QString commandName(enumCmdId command)
{
    return QString::fromLatin1(g_rgIrcCmd[command].szCmd);
}

CCRule* addIncomingRule(CCRuleSet* ruleSet, enumEvents event,
                        enumActions action, const QString& eventMessage,
                        const QString& actionParameter = QString())
{
    CCRulesData& data = theApp.m_rulesData;
    auto* rule = new CCRule(&theApp.m_dynaRules);
    rule->SetEvent(data.GetEvent(event));
    rule->SetAction(data.GetAction(action));
    rule->SetFlags(g_wActive | g_wMatchCase);

    rule->SetEventKeyParam(0, kepAnyoneButMe);
    rule->SetEventParam(0, data.GetKeyEventParam(kepAnyoneButMe));
    if (event == eOnWhisper) {
        rule->SetEventKeyParam(1, kepMax);
        rule->SetEventParam(1, eventMessage);
    } else {
        rule->SetEventKeyParam(1, kepMyActivatedRoom);
        rule->SetEventParam(1, data.GetKeyEventParam(kepMyActivatedRoom));
        rule->SetEventKeyParam(2, kepMax);
        rule->SetEventParam(2, eventMessage);
    }

    if (action == aReplaceMessage || action == aHighlightMessage) {
        rule->SetActionKeyParam(0, kapMax);
        rule->SetActionParam(0, actionParameter);
    }
    REQUIRE(ruleSet->bAddRule(rule));
    return rule;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.InitializeFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont,
                                     theApp.m_comicsColor));
    REQUIRE(CommunicationInits());
    REQUIRE(theApp.m_rulesData.bInitAlloc());
    REQUIRE(theApp.m_rulesData.bLoadStrings());

    CChatDoc document;
    SetChatDoc(&document);
    delete document.m_proto;
    document.m_proto = nullptr;
    CapturingIrcProto protocol;
    document.m_proto = &protocol;
    protocol.m_doc = &document;
    protocol.m_strChannel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    protocol.m_strPrettyChannel = protocol.m_strChannel;
    protocol.m_pSock->m_bIrcXServer = false;
    protocol.SetConnectionStatus(CX_INCHANNEL);
    document.m_bComicView = false;

    const QString identity = QString::fromUtf8(GetMyUserName())
        + QLatin1Char('@')
        + originalResourceString(QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString firstNick = commandName(cmdidList);
    const QString secondNick = commandName(cmdidWho);
    const QString roomNick = commandName(cmdidMode);
    REQUIRE(!identity.isEmpty() && !firstNick.isEmpty()
            && !secondNick.isEmpty() && !roomNick.isEmpty());

    auto* self = new CUserInfo(
        originalResourceString(QStringLiteral("IDS_DEFAULT_NICK")), identity);
    auto* first = new CUserInfo(firstNick, identity);
    auto* second = new CUserInfo(secondNick, identity);
    auto* roomUser = new CUserInfo(roomNick, identity);
    document.m_allChannelPuis = {self, first, second, roomUser};
    for (CUserInfo* pui : document.m_allChannelPuis)
        document.m_mapNickToPtr.insert(pui->GetName(), pui);
    document.m_puiSelf = self;
    g_puiSelf = self;

    CWhisperBox* box = CreateWhisperBox();
    REQUIRE(box != nullptr);
    application.processEvents();
    const OriginalDialogResource resource = originalDialogResource(
        QStringLiteral("IDD_WHISPERBOX"));
    const DialogUnits units(box->font());
    REQUIRE(resource.width == 334 && resource.height == 196);
    REQUIRE(box->windowTitle() == resource.caption);
    REQUIRE(box->layout() == nullptr);
    REQUIRE(box->minimumSize() == QSize(267, 175));
    REQUIRE(!box->windowIcon().isNull());
    REQUIRE(box->m_leaves.isEmpty());
    REQUIRE(box->m_currentIndex == -1);
    REQUIRE(box->m_sayWnd->m_dwButtons
            == (SB_WHISPER | SB_WACTION | SB_WSOUND));
    REQUIRE(box->m_sayWnd->m_cntBalloons == 3);

    QWidget* hiddenSayPosition = box->findChild<QWidget*>(
        QStringLiteral("IDC_SAYPOSITION"));
    REQUIRE(hiddenSayPosition && !hiddenSayPosition->isVisible());
    const OriginalDialogControl* sayControl = resourceControl(
        resource, QStringLiteral("IDC_SAYPOSITION"));
    const OriginalDialogControl* deleteControl = resourceControl(
        resource, QStringLiteral("IDC_DELETE_TAB"));
    const OriginalDialogControl* ignoreControl = resourceControl(
        resource, QStringLiteral("IDC_IGNORE_WBOX"));
    REQUIRE(sayControl && deleteControl && ignoreControl);
    QRect sourceSayRect = units.rect(*sayControl);
    sourceSayRect.adjust(4, 0, -5, 0);
    REQUIRE(box->m_sayWnd->height() == sourceSayRect.height());
    REQUIRE(box->m_deleteButton->size() == units.rect(*deleteControl).size());
    REQUIRE(box->m_ignoreButton->size() == units.rect(*ignoreControl).size());
    REQUIRE(box->m_deleteButton->x()
            == box->width() - box->m_deleteButton->width() - units.x(7));
    REQUIRE(box->m_deleteButton->y()
            == box->height() - box->m_deleteButton->height() - units.y(3));

    // Source insertion is case-insensitive alphabetic, while lookup is the
    // exact stored nickname. Both users are real CUserInfo instances here.
    WhisperBox(second, FALSE);
    WhisperBox(first, FALSE);
    application.processEvents();
    REQUIRE(box->m_leaves.size() == 2);
    REQUIRE(box->m_leaves[0]->m_label.compare(
                box->m_leaves[1]->m_label, Qt::CaseInsensitive) <= 0);
    REQUIRE(box->GetTab(first->GetName()) >= 0);
    REQUIRE(box->GetTab(first->GetName().toLower())
            == (first->GetName() == first->GetName().toLower()
                    ? box->GetTab(first->GetName()) : -1));
    for (CWhisperLeaf* leaf : box->m_leaves) {
        REQUIRE(leaf->m_richView->isReadOnly());
        REQUIRE(leaf->m_richCore->dwGetTextViewBufferMaxSize() == 65536);
    }

    const int firstTab = box->GetTab(first->GetName());
    const int secondTab = box->GetTab(second->GetName());
    box->SwitchToTab(firstTab);
    box->SetModified(secondTab, TRUE);
    REQUIRE(box->m_leaves[secondTab]->m_bModified);
    bool foundMarkedButton = false;
    for (QToolButton* button : box->findChildren<QToolButton*>())
        foundMarkedButton = foundMarkedButton
            || button->text() == box->m_leaves[secondTab]->m_label
                + QStringLiteral(" *");
    REQUIRE(foundMarkedButton);
    box->SwitchToTab(secondTab);
    REQUIRE(!box->m_leaves[secondTab]->m_bModified);

    // A private in-room message without an existing tab stays in the room.
    const QString roomMessage = originalResourceString(
        QStringLiteral("ID_THINK_PREFIX"));
    const int historyBeforeRoom = document.m_history.size();
    roomUser->m_udi.Reset();
    roomUser->m_bbValidUDI = 0;
    ProcessSay(&document, roomUser, roomMessage, MT_PRIVATEMSG);
    REQUIRE(document.m_history.size() == historyBeforeRoom + 1);
    REQUIRE(box->GetTab(roomUser->GetName()) == -1);

    // Once the real member has a tab, the same source path is intercepted.
    WhisperBox(roomUser, FALSE);
    const int roomTab = box->GetTab(roomUser->GetName());
    const int historyBeforeIntercept = document.m_history.size();
    roomUser->m_udi.Reset();
    roomUser->m_bbValidUDI = 0;
    ProcessSay(&document, roomUser, roomMessage, MT_PRIVATEMSG);
    REQUIRE(document.m_history.size() == historyBeforeIntercept);
    REQUIRE(box->m_leaves[roomTab]->m_richView->toPlainText()
            .contains(roomMessage));

    const QString sourceUrl = originalResourceString(
        QStringLiteral("IDS_URL_MSPREFIX"));
    REQUIRE(sourceUrl.endsWith(QLatin1Char('?')));
    const QString linkedSourceUrl = sourceUrl.left(sourceUrl.size() - 1);
    roomUser->m_udi.Reset();
    roomUser->m_bbValidUDI = 0;
    ProcessSay(&document, roomUser, sourceUrl, MT_PRIVATEMSG);
    QTextEdit* roomWhisperView = box->m_leaves[roomTab]->m_richView;
    const int whisperUrlStart = roomWhisperView->toPlainText().lastIndexOf(
        sourceUrl);
    REQUIRE(whisperUrlStart >= 0);
    QTextCursor whisperUrl(roomWhisperView->document());
    whisperUrl.setPosition(whisperUrlStart);
    whisperUrl.movePosition(QTextCursor::NextCharacter,
                            QTextCursor::KeepAnchor);
    REQUIRE(whisperUrl.charFormat().isAnchor());
    REQUIRE(whisperUrl.charFormat().anchorHref() == linkedSourceUrl);
    QTextCursor whisperTerminator(roomWhisperView->document());
    whisperTerminator.setPosition(whisperUrlStart + linkedSourceUrl.size());
    whisperTerminator.movePosition(QTextCursor::NextCharacter,
                                   QTextCursor::KeepAnchor);
    REQUIRE(!whisperTerminator.charFormat().isAnchor());

    // A real external PUI creates a tab without being redirected to a room.
    const QString externalNick = commandName(cmdidAway);
    CUserInfo* external = ExternalPui(externalNick, identity, true);
    REQUIRE(external && external->IsExternal());
    const QString externalMessage = originalResourceString(
        QStringLiteral("IDS_DEFAULTGREETING"));
    external->m_udi.Reset();
    external->m_bbValidUDI = 0;
    ProcessSay(nullptr, external, externalMessage, MT_PRIVATEMSG);
    const int externalTab = box->GetTab(external->GetName());
    REQUIRE(externalTab >= 0);
    REQUIRE(box->m_leaves[externalTab]->m_richView->toPlainText()
            .contains(externalMessage));

    // The source applies ReplaceMessage and HighlightMessage before creating
    // the room SayEntry. Both resulting fields are retained by HistoryEntry.
    auto* ruleSet = new CCRuleSet(&theApp.m_dynaRules);
    ruleSet->SetName(originalResourceString(
        QStringLiteral("IDS_GENERAL_RULESET")));
    ruleSet->Activate();
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(ruleSet));
    const QString roomRuleMessage = originalResourceString(
        QStringLiteral("IDS_CONNECTION"));
    const QString roomReplacement = originalResourceString(
        QStringLiteral("IDS_DFLTAWAYMSG"));
    REQUIRE(roomRuleMessage != roomReplacement);
    addIncomingRule(ruleSet, eOnMessage, aReplaceMessage,
                    roomRuleMessage, roomReplacement);
    QString highlightParameter = originalResourceString(
        QStringLiteral("IDS_HIGHLIGHT_TYPE"));
    highlightParameter.replace(QStringLiteral("%d"), QStringLiteral("1"));
    addIncomingRule(ruleSet, eOnMessage, aHighlightMessage,
                    roomRuleMessage, highlightParameter);
    const int historyBeforeRules = document.m_history.size();
    roomUser->m_udi.Reset();
    roomUser->m_bbValidUDI = 0;
    ProcessSay(&document, roomUser, roomRuleMessage, MT_CHANNELSEND);
    REQUIRE(document.m_history.size() == historyBeforeRules + 1);
    auto* ruledSay = dynamic_cast<SayEntry*>(document.m_history.last());
    REQUIRE(ruledSay != nullptr);
    REQUIRE(ruledSay->m_mesg == roomReplacement);
    REQUIRE(ruledSay->m_cHighlightType == 0);

    // Although ReplaceMessage is approved here too, the original
    // bAddToWhisperBox renders szMesg, not GetCFFinalMessage(). Preserve that
    // observable source behavior rather than inferring an intended variant.
    const QString whisperRuleMessage = originalResourceString(
        QStringLiteral("IDS_INVITE_CONF"));
    const QString whisperReplacement = originalResourceString(
        QStringLiteral("IDS_CONNECTION_DROPPED"));
    REQUIRE(whisperRuleMessage != whisperReplacement);
    addIncomingRule(ruleSet, eOnWhisper, aReplaceMessage,
                    whisperRuleMessage, whisperReplacement);
    CUserInfo ruleExternal(commandName(cmdidWhoIs), identity);
    ruleExternal.SetExternal(true);
    REQUIRE(bAddToWhisperBox(
        &ruleExternal, BM_WHISPER, whisperRuleMessage));
    const int ruleExternalTab = box->GetTab(ruleExternal.GetName());
    REQUIRE(ruleExternalTab >= 0);
    QTextEdit* ruleExternalView =
        box->m_leaves[ruleExternalTab]->m_richView;
    REQUIRE(ruleExternalView->toPlainText().contains(whisperRuleMessage));
    REQUIRE(!ruleExternalView->toPlainText().contains(whisperReplacement));

    // DoNotDisplay still consumes an external whisper but creates no tab.
    CUserInfo hiddenExternal(commandName(cmdidUserHost), identity);
    hiddenExternal.SetExternal(true);
    const QString hiddenMessage = originalResourceString(
        QStringLiteral("IDS_CONNECTION_FAILED"));
    addIncomingRule(ruleSet, eOnWhisper, aDoNotDisplay, hiddenMessage);
    const int leavesBeforeHidden = box->m_leaves.size();
    REQUIRE(bAddToWhisperBox(&hiddenExternal, BM_WHISPER, hiddenMessage));
    REQUIRE(box->m_leaves.size() == leavesBeforeHidden);
    REQUIRE(box->GetTab(hiddenExternal.GetName()) == -1);
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(ruleSet));

    // Outgoing text uses the selected leaf and does not echo into room history.
    const int selectedRoomTab = box->GetTab(roomUser->GetName());
    REQUIRE(selectedRoomTab >= 0);
    box->SwitchToTab(selectedRoomTab);
    protocol.sent.clear();
    const QString outgoing = originalResourceString(
        QStringLiteral("IDS_DFLTAWAYMSG"));
    const int historyBeforeSend = document.m_history.size();
    REQUIRE(bWhisperInBox(QString(), outgoing, nullptr, BM_WHISPER));
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent.first()
            == QStringLiteral("PRIVMSG %1 :%2\r\n")
                .arg(roomUser->GetName(), outgoing));
    REQUIRE(document.m_history.size() == historyBeforeSend);
    REQUIRE(box->m_leaves[selectedRoomTab]->m_richView->toPlainText()
            .contains(outgoing));

    // /ME remains a whisper action; /THINK becomes a regular whisper.
    protocol.sent.clear();
    const QString slashMe = QLatin1Char('/') + commandName(cmdidMe)
        + QLatin1Char(' ') + outgoing;
    REQUIRE(bWhisperInBox(QString(), slashMe, nullptr, BM_WHISPER));
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent.first()
            == QStringLiteral("PRIVMSG %1 :\001ACTION %2\001\r\n")
                .arg(roomUser->GetName(), outgoing));

    protocol.sent.clear();
    const QString slashThink = QLatin1Char('/') + commandName(cmdidThink)
        + QLatin1Char(' ') + outgoing;
    REQUIRE(bWhisperInBox(QString(), slashThink, nullptr, BM_WHISPER));
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent.first()
            == QStringLiteral("PRIVMSG %1 :%2\r\n")
                .arg(roomUser->GetName(), outgoing));

    // /MSG from the box switches to the real target tab before sending.
    protocol.sent.clear();
    const QString slashMsg = QLatin1Char('/') + commandName(cmdidMsg)
        + QLatin1Char(' ') + second->GetName()
        + QLatin1Char(' ') + outgoing;
    REQUIRE(bWhisperInBox(QString(), slashMsg, nullptr, BM_WHISPER));
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent.first()
            == QStringLiteral("PRIVMSG %1 :%2\r\n")
                .arg(second->GetName(), outgoing));
    REQUIRE(box->m_currentIndex == box->GetTab(second->GetName()));

    IgnoreUser(second->GetName(), second->GetFullName(), true, false);
    REQUIRE(box->m_leaves[box->GetTab(second->GetName())]->m_bIgnore);
    REQUIRE(box->m_ignoreButton->isChecked());

    // User-list enablement is tied only to one real non-self WHO result.
    {
        CUserListPersist persist;
        persist.m_cachedServer = QString::fromUtf8(GetMyServer());
        auto* listed = new CUser;
        listed->m_strNickname = first->GetName();
        listed->m_strIdentity = first->GetFullName();
        persist.AddUser(listed);
        CUserList dialog(&persist);
        dialog.show();
        application.processEvents();
        QTreeWidgetItem* item = dialog.m_userListCtrl->topLevelItem(0);
        REQUIRE(item != nullptr);
        dialog.m_userListCtrl->setCurrentItem(item);
        item->setSelected(true);
        dialog.OnItemchangedUserlist();
        REQUIRE(dialog.m_message->isEnabled());
    }

    InitializeWhisperCores(FALSE);
    while (!box->m_leaves.isEmpty()) box->OnDeleteTab();
    REQUIRE(!box->isVisible());
    DestroyWhisperBox();
    REQUIRE(GetWhisperBox() == nullptr);
    DestroyExternalUserInfos();
    g_rgpuiWhisperees.clear();
    document.m_proto = nullptr;
    currentRoom = nullptr;
    SetChatDoc(nullptr);
    CommunicationCleanup();
    return 0;
}
