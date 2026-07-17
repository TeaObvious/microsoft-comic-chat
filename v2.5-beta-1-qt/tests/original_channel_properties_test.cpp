#include "chanprop.h"
#include "chat.h"
#include "defines.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "protsupp.h"
#include "userinfo.h"

#include <QApplication>
#include <QCheckBox>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

const OriginalDialogControl* resourceControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

QRect resourceRect(const OriginalDialogControl& control, const QFont& font)
{
    const QFontMetrics metrics(font);
    const QString alphabet = QStringLiteral(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    const int baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
    const int baseY = qMax(1, metrics.height());
    const auto x = [baseX](int dlu) { return (dlu * baseX + 2) / 4; };
    const auto y = [baseY](int dlu) { return (dlu * baseY + 4) / 8; };
    return {x(control.x), y(control.y),
            x(control.width), y(control.height)};
}

void requireResourceGeometry(CChannelProp& dialog,
                             const QString& resourceIdentifier)
{
    const OriginalDialogResource resource = originalDialogResource(
        resourceIdentifier);
    REQUIRE(dialog.layout() == nullptr);
    for (const OriginalDialogControl& control : resource.controls) {
        if (control.identifier == QLatin1String("IDC_STATIC")) continue;
        QWidget* widget = dialog.findChild<QWidget*>(control.identifier);
        REQUIRE(widget != nullptr);
        REQUIRE(widget->geometry() == resourceRect(control, dialog.font()));
        REQUIRE(widget->isVisible() == control.visible);
    }
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();

    const QString selfNick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString channel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString oldPassword = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString newPassword = selfNick;
    const QString topic = originalResourceString(
        QStringLiteral("IDS_CHANPROP_ADMIN"));
    REQUIRE(!selfNick.isEmpty() && !channel.isEmpty()
            && !oldPassword.isEmpty() && !topic.isEmpty());

    CUserInfo host(QLatin1Char('@') + selfNick);
    REQUIRE(host.IsOperator());
    g_puiSelf = &host;
    {
        CChannelProp properties;
        properties.m_bSecret = TRUE;
        properties.m_bInviteOnly = TRUE;
        properties.m_bModerated = TRUE;
        properties.m_bTopicAnyone = FALSE;
        properties.m_bSetMax = TRUE;
        properties.m_uMaxParticipants = MAX_TOPICLEN;
        properties.m_bSetPassword = TRUE;
        properties.m_strPassword = oldPassword;
        properties.m_rtfTopic.m_strText = topic;
        properties.show();
        application.processEvents();

        REQUIRE(properties.windowTitle()
                == originalDialogCaption(QStringLiteral("IDD_CHANNELPROP")));
        requireResourceGeometry(properties, QStringLiteral("IDD_CHANNELPROP"));
        auto* message = properties.findChild<QLabel*>(
            QStringLiteral("IDC_CHANPROP_MESG"));
        REQUIRE(message != nullptr);
        REQUIRE(message->text() == originalResourceString(
            QStringLiteral("IDS_CHANPROP_ADMIN")));
        REQUIRE(!properties.m_rtfTopic.isReadOnly());
        REQUIRE(properties.m_rtfTopic.toPlainText() == topic);
        REQUIRE(properties.findChild<QCheckBox*>(
                    QStringLiteral("IDC_HIDDEN"))->isChecked());
        REQUIRE(properties.findChild<QCheckBox*>(
                    QStringLiteral("IDC_INVITEONLY"))->isChecked());
        REQUIRE(properties.findChild<QCheckBox*>(
                    QStringLiteral("IDC_MODERATED"))->isChecked());
        REQUIRE(!properties.findChild<QCheckBox*>(
                    QStringLiteral("IDC_AUDITORIUM"))->isVisible());
        REQUIRE(!properties.findChild<QCheckBox*>(
                    QStringLiteral("IDC_NOWHISPERS"))->isVisible());
    }

    CUserInfo participant(selfNick);
    REQUIRE(!participant.IsOperator());
    g_puiSelf = &participant;
    {
        CChannelProp topicOnly;
        topicOnly.m_bTopicAnyone = TRUE;
        topicOnly.show();
        application.processEvents();
        REQUIRE(topicOnly.findChild<QLabel*>(
                    QStringLiteral("IDC_CHANPROP_MESG"))->text()
                == originalResourceString(
                    QStringLiteral("IDS_CHANPROP_TOPICONLY")));
        REQUIRE(!topicOnly.m_rtfTopic.isReadOnly());
        REQUIRE(!topicOnly.findChild<QCheckBox*>(
                    QStringLiteral("IDC_MODERATED"))->isEnabled());
    }
    {
        CChannelProp readOnly;
        readOnly.m_bTopicAnyone = FALSE;
        readOnly.show();
        application.processEvents();
        REQUIRE(readOnly.findChild<QLabel*>(
                    QStringLiteral("IDC_CHANPROP_MESG"))->text()
                == originalResourceString(
                    QStringLiteral("IDS_CHANPROP_NOTADMIN")));
        REQUIRE(readOnly.m_rtfTopic.isReadOnly());
    }

    g_puiSelf = &host;
    {
        CChannelCreateDlg create;
        create.m_bIsIRCX = FALSE;
        create.m_strChannelName = channel;
        create.show();
        application.processEvents();
        requireResourceGeometry(create, QStringLiteral("IDD_CHANNELCREATE"));

        auto* channelEdit = create.findChild<QLineEdit*>(
            QStringLiteral("IDC_CHANNELNAME"));
        auto* hidden = create.findChild<QCheckBox*>(
            QStringLiteral("IDC_HIDDEN"));
        auto* privateRoom = create.findChild<QCheckBox*>(
            QStringLiteral("IDC_PRIVATE"));
        auto* setMaximum = create.findChild<QCheckBox*>(
            QStringLiteral("IDC_SETMAX"));
        auto* maximum = create.findChild<QLineEdit*>(
            QStringLiteral("IDC_MAXPARTICIPANTS"));
        auto* passwordGiven = create.findChild<QCheckBox*>(
            QStringLiteral("IDC_PASSWORD_GIVEN"));
        auto* password = create.findChild<QLineEdit*>(
            QStringLiteral("IDC_PASSWORD"));
        auto* ok = create.findChild<QPushButton*>(QStringLiteral("IDOK"));
        REQUIRE(channelEdit && hidden && privateRoom && setMaximum && maximum
                && passwordGiven && password && ok);
        REQUIRE(channelEdit->text() == channel);
        REQUIRE(channelEdit->maxLength() == MAX_IRCCHANNAME);
        REQUIRE(ok->isEnabled());

        privateRoom->setChecked(true);
        hidden->click();
        REQUIRE(hidden->isChecked() && !privateRoom->isChecked());
        setMaximum->setChecked(true);
        maximum->setText(QString::number(MAX_CHANNELPWD));
        passwordGiven->setChecked(true);
        password->setText(newPassword);
        create.m_rtfTopic.setPlainText(topic);
        create.accept();
        REQUIRE(create.result() == QDialog::Accepted);
        REQUIRE(create.m_strChannelName == channel);
        REQUIRE(create.m_bSecret && !create.m_bPrivate);
        REQUIRE(create.m_bSetMax
                && create.m_uMaxParticipants == MAX_CHANNELPWD);
        REQUIRE(create.m_bSetPassword
                && create.m_strPassword == newPassword);
        REQUIRE(create.m_rtfTopic.m_strText == topic);
    }

    serverConn.m_queries.FreeRemoveAll();
    CapturingIrcProto protocol;
    protocol.SetConnectionStatus(CX_INCHANNEL);
    protocol.m_strChannel = channel;
    REQUIRE(protocol.ChatSetTopic(topic));
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("TOPIC %1 :%2\r\n").arg(channel, topic));
    CCQuery* topicQuery = serverConn.m_queries.FindQuery(ctTopic);
    REQUIRE(topicQuery != nullptr
            && topicQuery->GetQueryPurpose() == qpSetTopic);
    serverConn.m_queries.FreeRemoveAll();

    char modeBuffer[20];
    GetModeChars(CM_PRIVATE | CM_HIDDEN | CM_INVITEONLY | CM_TOPICHOST
                     | CM_NOEXTERN | CM_MODERATED | CM_USERLIMIT
                     | CM_CHANNELKEY,
                 modeBuffer);
    REQUIRE(std::strcmp(modeBuffer, "psitnmlk") == 0);

    protocol.sent.clear();
    protocol.m_dwModes = CM_PRIVATE | CM_NOEXTERN
        | CM_USERLIMIT | CM_CHANNELKEY;
    protocol.m_dwMaxUsers = MAX_CHANNELPWD;
    protocol.m_strPassword = oldPassword;
    const DWORD newModes = CM_HIDDEN | CM_INVITEONLY | CM_NOEXTERN
        | CM_MODERATED | CM_USERLIMIT | CM_CHANNELKEY;
    REQUIRE(protocol.ChatSetMode(newModes, MAX_TOPICLEN, newPassword));
    REQUIRE(protocol.sent.size() == 2);
    REQUIRE(protocol.sent[0]
            == QStringLiteral("MODE %1 -pk %2\r\n")
                   .arg(channel, oldPassword));
    REQUIRE(protocol.sent[1]
            == QStringLiteral("MODE %1 +simlk %2 %3\r\n")
                   .arg(channel, QString::number(MAX_TOPICLEN), newPassword));

    g_puiSelf = nullptr;
    serverConn.m_queries.FreeRemoveAll();
    return EXIT_SUCCESS;
}
