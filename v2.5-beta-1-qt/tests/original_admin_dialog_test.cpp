#include "admindlg.h"
#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "panel.h"
#include "protsupp.h"
#include "resource.h"
#include "rules.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>
#include <QCheckBox>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
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
    const int baseX = qMax(1,
        (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
    const int baseY = qMax(1, metrics.height());
    const auto x = [baseX](int dlu) { return (dlu * baseX + 2) / 4; };
    const auto y = [baseY](int dlu) { return (dlu * baseY + 4) / 8; };
    return {x(control.x), y(control.y),
            x(control.width), y(control.height)};
}

void closeInformationMessage(const QString& expected)
{
    QTimer::singleShot(0, [expected] {
        auto* box = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        REQUIRE(box);
        REQUIRE(box->text() == expected);
        box->accept();
    });
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    theApp.InitVals();
    REQUIRE(theApp.m_rulesData.bInitAlloc());
    REQUIRE(theApp.m_rulesData.bLoadStrings());
    theApp.InitializeFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont,
                                     theApp.m_comicsColor));
    REQUIRE(CommunicationInits());

    const QString defaultNick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString selectedNick = originalResourceString(
        QStringLiteral("IDS_SAMPLE_NICK"));
    const QString channel = EncodeChan(originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL")));
    const QString userName = QString::fromUtf8(GetMyUserName());
    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString realName = originalResourceString(
        QStringLiteral("IDS_DEFAULT_REALNAME"));

    {
        CKickDialog dialog;
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_KICK"));
        REQUIRE(resource.width == 186);
        REQUIRE(resource.height == 89);
        REQUIRE(dialog.layout() == nullptr);
        REQUIRE(dialog.m_reasonEdit->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_REASON")),
            dialog.font()));
        REQUIRE(dialog.m_banToo->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_BANTOO")),
            dialog.font()));
        REQUIRE(dialog.m_banPattern->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_BANTOO_NAME")),
            dialog.font()));
        dialog.show();
        application.processEvents();
        REQUIRE(!dialog.m_banPattern->isEnabled());
        dialog.m_banToo->click();
        REQUIRE(dialog.m_banPattern->isEnabled());
        dialog.hide();
    }

    {
        CBanDlg dialog;
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_BAN"));
        REQUIRE(resource.width == 186);
        REQUIRE(resource.height == 170);
        REQUIRE(dialog.layout() == nullptr);
        REQUIRE(dialog.m_ctlBans->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_BANNED_USERS")),
            dialog.font()));
        REQUIRE(dialog.m_banButton->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDBAN")),
            dialog.font()));
        REQUIRE(dialog.m_unbanButton->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDUNBAN")),
            dialog.font()));
    }

    {
        CInviteDlg dialog;
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_INVITE"));
        REQUIRE(resource.width == 186);
        REQUIRE(resource.height == 89);
        REQUIRE(dialog.layout() == nullptr);
        REQUIRE(dialog.m_invitees->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_INVITEES")),
            dialog.font()));
        dialog.show();
        application.processEvents();
        const QString sourceText = originalResourceString(
            QStringLiteral("IDS_KICKREASON"));
        dialog.m_invitees->setPlainText(sourceText.repeated(64));
        REQUIRE(dialog.m_invitees->toPlainText().size() == 255);
        dialog.hide();
    }

    {
        CInvitationDlg dialog;
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_INVITATION"));
        REQUIRE(resource.width == 186);
        REQUIRE(resource.height == 93);
        REQUIRE(dialog.layout() == nullptr);
        REQUIRE(dialog.m_message->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_MESSAGE")),
            dialog.font()));
        REQUIRE(dialog.m_ignore->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_IGNORE")),
            dialog.font()));
    }

    CapturingIrcProto protocol;
    protocol.m_strChannel = channel;
    protocol.SetConnectionStatus(CX_INCHANNEL);
    currentRoom = &protocol;
    serverConn.m_bIrcXServer = false;

    CUserInfo selected(selectedNick,
                       userName + QLatin1Char('@') + server);
    QString banPattern;
    GetBanString(userName, server, banPattern);
    REQUIRE(banPattern == QStringLiteral("*!*@%1").arg(server));

    REQUIRE(protocol.ChatKickUser(selectedNick, QString()));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("KICK %1 %2 :\r\n")
                   .arg(channel, selectedNick));
    REQUIRE(protocol.ChatBanUser(banPattern, TRUE));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("MODE %1 +b %2\r\n")
                   .arg(channel, banPattern));
    REQUIRE(protocol.ChatBanUser(banPattern, FALSE));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("MODE %1 -b %2\r\n")
                   .arg(channel, banPattern));
    REQUIRE(protocol.ChatSendInvitation(selectedNick));
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("INVITE %1 %2\r\n")
                   .arg(selectedNick, channel));

    protocol.sent.clear();
    protocol.m_dwModes = CM_MODERATED;
    protocol.ChatSetOperator(&selected, UM_HOST);
    REQUIRE((protocol.sent == QStringList{
        QStringLiteral("MODE %1 +o %2\r\n").arg(channel, selectedNick)}));
    selected.SetOperator(true);
    protocol.sent.clear();
    protocol.ChatSetOperator(&selected, UM_SPEAKER);
    REQUIRE((protocol.sent == QStringList{
        QStringLiteral("MODE %1 -o %2\r\n").arg(channel, selectedNick),
        QStringLiteral("MODE %1 +v %2\r\n").arg(channel, selectedNick)}));
    protocol.sent.clear();
    protocol.ChatSetOperator(&selected, UM_SPECTATOR);
    REQUIRE((protocol.sent == QStringList{
        QStringLiteral("MODE %1 -o %2\r\n").arg(channel, selectedNick),
        QStringLiteral("MODE %1 -v %2\r\n").arg(channel, selectedNick)}));
    selected.SetOperator(false);

    protocol.sent.clear();
    protocol.ChatKickUser(&selected);
    REQUIRE((protocol.sent == QStringList{
        QStringLiteral("WHOIS %1\r\n").arg(selectedNick)}));
    QTimer::singleShot(0, [banPattern, selectedNick] {
        auto* dialog = dynamic_cast<CKickDialog*>(
            QApplication::activeModalWidget());
        REQUIRE(dialog);
        QString prompt = originalResourceString(
            QStringLiteral("IDS_KICKREASON"));
        prompt.replace(QStringLiteral("%1"), DecodeNick(selectedNick));
        REQUIRE(dialog->m_Kick->text() == prompt);
        REQUIRE(dialog->m_banPattern->text() == banPattern);
        dialog->m_banToo->click();
        dialog->accept();
    });
    serverConn.ProcessMessage(
        QStringLiteral(":%1 311 %2 %3 %4 %5 * :%6")
            .arg(server, defaultNick, selectedNick,
                 userName, server, realName));
    REQUIRE((protocol.sent == QStringList{
        QStringLiteral("WHOIS %1\r\n").arg(selectedNick),
        QStringLiteral("MODE %1 +b %2\r\n").arg(channel, banPattern),
        QStringLiteral("KICK %1 %2 :\r\n").arg(channel, selectedNick)}));
    serverConn.ProcessMessage(
        QStringLiteral(":%1 318 %2 %3 :End of WHOIS")
            .arg(server, defaultNick, selectedNick));
    REQUIRE(serverConn.m_queries.FindQuery(ctWhoIs) == nullptr);

    protocol.sent.clear();
    protocol.ChatBanUser(&selected);
    REQUIRE((protocol.sent == QStringList{
        QStringLiteral("WHOIS %1\r\n").arg(selectedNick)}));
    serverConn.ProcessMessage(
        QStringLiteral(":%1 311 %2 %3 %4 %5 * :%6")
            .arg(server, defaultNick, selectedNick,
                 userName, server, realName));
    REQUIRE(protocol.sent.last()
            == QStringLiteral("MODE %1 +b\r\n").arg(channel));
    serverConn.ProcessMessage(
        QStringLiteral(":%1 318 %2 %3 :End of WHOIS")
            .arg(server, defaultNick, selectedNick));
    serverConn.ProcessMessage(
        QStringLiteral(":%1 367 %2 %3 %4")
            .arg(server, defaultNick, channel, banPattern));
    QTimer::singleShot(0, [banPattern] {
        auto* dialog = dynamic_cast<CBanDlg*>(
            QApplication::activeModalWidget());
        REQUIRE(dialog);
        REQUIRE(dialog->m_banEdit->text() == banPattern);
        REQUIRE(dialog->m_banList->count() == 1);
        dialog->m_banList->setCurrentRow(0);
        REQUIRE(!dialog->m_banButton->isEnabled());
        REQUIRE(dialog->m_unbanButton->isEnabled());
        dialog->OnUnban();
        dialog->accept();
    });
    serverConn.ProcessMessage(
        QStringLiteral(":%1 368 %2 %3 :End of channel ban list")
            .arg(server, defaultNick, channel));
    REQUIRE(protocol.sent.last()
            == QStringLiteral("MODE %1 -b %2\r\n")
                   .arg(channel, banPattern));

    protocol.sent.clear();
    g_puiSelf = &selected;
    QTimer::singleShot(0, [defaultNick, selectedNick] {
        auto* dialog = dynamic_cast<CInviteDlg*>(
            QApplication::activeModalWidget());
        REQUIRE(dialog);
        dialog->m_invitees->setPlainText(
            defaultNick + QStringLiteral(",\r\n") + selectedNick);
        dialog->accept();
    });
    protocol.ChatInvite();
    REQUIRE((protocol.sent == QStringList{
        QStringLiteral("INVITE %1 %2\r\n").arg(defaultNick, channel),
        QStringLiteral("INVITE %1 %2\r\n").arg(selectedNick, channel)}));
    g_puiSelf = nullptr;

    theApp.m_bAllowInvites = true;
    g_bCanViewUnrated = TRUE;
    theApp.m_ignores.clear();
    const auto ruleSetName = [](const QString& resource) {
        const QString stored = originalResourceString(resource);
        return stored.mid(stored.indexOf(QLatin1Char('|')) + 1);
    };
    auto* invitationTarget = new CCRuleSet(&theApp.m_dynaRules);
    invitationTarget->SetName(ruleSetName(
        QStringLiteral("IDS_SAMPLES_RULESET")));
    auto* invitationRules = new CCRuleSet(&theApp.m_dynaRules);
    invitationRules->SetName(ruleSetName(
        QStringLiteral("IDS_GENERAL_RULESET")));
    invitationRules->Activate();
    auto* invitationRule = new CCRule(&theApp.m_dynaRules);
    invitationRule->SetEvent(
        theApp.m_rulesData.GetEvent(eOnInvitation));
    invitationRule->SetEventKeyParam(0, kepAnyoneButMe);
    invitationRule->SetEventParam(0,
        theApp.m_rulesData.GetKeyEventParam(kepAnyoneButMe));
    invitationRule->SetEventKeyParam(1, kepAny);
    invitationRule->SetEventParam(1,
        theApp.m_rulesData.GetKeyEventParam(kepAny));
    invitationRule->SetAction(
        theApp.m_rulesData.GetAction(aActivateRuleSet));
    invitationRule->SetActionKeyParam(0, kapMax);
    invitationRule->SetActionParam(0, invitationTarget->GetName());
    invitationRule->SetActionKeyParam(1, kapYes);
    invitationRule->SetActionParam(1,
        theApp.m_rulesData.GetKeyActionParam(kapYes));
    invitationRule->SetFlags(g_wActive);
    REQUIRE(invitationRules->bAddRule(invitationRule));
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(invitationTarget));
    REQUIRE(theApp.m_dynaRules.bAddRuleSet(invitationRules));
    QTimer::singleShot(0, [selectedNick, channel] {
        auto* dialog = dynamic_cast<CInvitationDlg*>(
            QApplication::activeModalWidget());
        REQUIRE(dialog);
        QString offer = originalResourceString(
            QStringLiteral("ID_JOIN_OFFER"));
        offer.replace(QStringLiteral("%1"), DecodeNick(selectedNick));
        offer.replace(QStringLiteral("%2"), DecodeChan(channel));
        REQUIRE(dialog->m_message->text() == offer);
        dialog->OnNo();
    });
    serverConn.ProcessMessage(
        QStringLiteral(":%1!%2@%3 INVITE %4 :%5")
            .arg(selectedNick, userName, server, defaultNick, channel));
    REQUIRE(invitationTarget->bActive());
    REQUIRE(theApp.m_ignores.isEmpty());
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(invitationRules));
    REQUIRE(theApp.m_dynaRules.bRemoveRuleSet(invitationTarget));

    {
        CChatDoc document;
        CUserInfo self(defaultNick,
                       userName + QLatin1Char('@') + server);
        document.m_puiSelf = &self;
        document.m_proto->m_strChannel = channel;
        document.m_proto->SetConnectionStatus(CX_INCHANNEL);
        SetChatDoc(&document);
        REQUIRE(bCanInvite());
        document.m_proto->m_dwModes = CM_INVITEONLY;
        REQUIRE(!bCanInvite());
        self.SetOperator(true);
        REQUIRE(bCanInvite());

        g_bEnterOnCreate = TRUE;
        QTimer::singleShot(0, [] {
            auto* dialog = dynamic_cast<CInvitationDlg*>(
                QApplication::activeModalWidget());
            REQUIRE(dialog);
            dialog->OnYes();
        });
        serverConn.ProcessMessage(
            QStringLiteral(":%1!%2@%3 INVITE %4 :%5")
                .arg(selectedNick, userName, server,
                     defaultNick, channel));
        REQUIRE(!g_bEnterOnCreate);

        QString confirmation = originalResourceString(
            QStringLiteral("IDS_INVITE_CONF"));
        confirmation.replace(QStringLiteral("%1"), selectedNick);
        confirmation.replace(QStringLiteral("%2"), DecodeChan(channel));
        closeInformationMessage(confirmation);
        serverConn.ProcessMessage(
            QStringLiteral(":%1 341 %2 %3 %4")
                .arg(server, defaultNick, selectedNick, channel));
        SetChatDoc(nullptr);
        document.m_puiSelf = nullptr;
    }

    currentRoom = nullptr;
    serverConn.m_queries.FreeRemoveAll();
    CommunicationCleanup();
    CUnitPanelPage::DestroyFonts();
    return 0;
}
