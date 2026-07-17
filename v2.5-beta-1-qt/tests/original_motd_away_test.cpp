#include "chat.h"
#include "defines.h"
#include "ircproto.h"
#include "ircsock.h"
#include "motd.h"
#include "originalassets.h"

#include <QApplication>
#include <QCheckBox>
#include <QFontMetrics>
#include <QPushButton>
#include <QTextCursor>
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
    QList<QString> sent;
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
    const int baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
    const int baseY = qMax(1, metrics.height());
    const auto x = [baseX](int dlu) { return (dlu * baseX + 2) / 4; };
    const auto y = [baseY](int dlu) { return (dlu * baseY + 4) / 8; };
    return {x(control.x), y(control.y),
            x(control.width), y(control.height)};
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.m_strAwayMessage.clear();

    const QString awayText = originalResourceString(
        QStringLiteral("IDS_DFLTAWAYMSG"));
    const QString motdText = originalResourceString(
        QStringLiteral("IDS_ERR_NOMOTD"));
    QString luserText = originalResourceString(
        QStringLiteral("ID_USER_PLURAL"));
    luserText.replace(QStringLiteral("%1"),
                      QString::number(MAX_CHANNELPWD));
    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString nick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    REQUIRE(!awayText.isEmpty() && !motdText.isEmpty()
            && !luserText.isEmpty() && !server.isEmpty() && !nick.isEmpty());

    {
        CAwayDlg away;
        away.m_rtfAwayMsg.m_strText = awayText;
        away.show();
        application.processEvents();
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_AWAYDLG"));
        REQUIRE(away.layout() == nullptr);
        REQUIRE(away.windowTitle() == resource.caption);
        REQUIRE(away.m_rtfAwayMsg.geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_AWAYMSG")),
            away.font()));
        auto* ok = away.findChild<QPushButton*>(QStringLiteral("IDOK"));
        REQUIRE(ok && ok->isEnabled());
        away.m_rtfAwayMsg.setPlainText(QStringLiteral("   "));
        REQUIRE(!ok->isEnabled());
        away.m_rtfAwayMsg.setPlainText(awayText);
        away.accept();
        REQUIRE(away.result() == QDialog::Accepted);
        REQUIRE(away.m_rtfAwayMsg.m_strText == awayText);
    }

    {
        CMOTD motd;
        motd.m_strLUSER = luserText;
        motd.m_strMOTD = motdText;
        motd.m_bShowMOTD = FALSE;
        motd.show();
        application.processEvents();
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_MOTD"));
        REQUIRE(motd.layout() == nullptr);
        REQUIRE(motd.windowTitle() == resource.caption);
        REQUIRE(motd.m_edit.geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_EDITPOS")),
            motd.font()));
        REQUIRE(motd.m_edit.isReadOnly());
        REQUIRE(motd.m_edit.toPlainText()
                == luserText + QLatin1Char('\n') + motdText);
        QTextCursor cursor(motd.m_edit.document());
        cursor.setPosition(0);
        cursor.movePosition(QTextCursor::NextCharacter,
                            QTextCursor::KeepAnchor);
        REQUIRE(cursor.charFormat().foreground().color()
                == QColor(0, 0, 255));
        cursor.setPosition(luserText.size() + 1);
        cursor.movePosition(QTextCursor::NextCharacter,
                            QTextCursor::KeepAnchor);
        REQUIRE(cursor.charFormat().foreground().color()
                == QColor(0, 0, 0));
        auto* show = motd.findChild<QCheckBox*>(
            QStringLiteral("IDC_SHOW_MOTD"));
        REQUIRE(show && !show->isChecked());
        show->setChecked(true);
        motd.accept();
        REQUIRE(motd.m_bShowMOTD);
    }

    serverConn.m_queries.FreeRemoveAll();
    CapturingIrcProto protocol;
    protocol.SetConnectionStatus(CX_NOCHANNEL);
    REQUIRE(protocol.bChatShowMOTD());
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent.first() == QStringLiteral("LUSERS\r\nMOTD\r\n"));
    CCQuery* query = serverConn.m_queries.FindQuery(ctLUsersMOTD);
    REQUIRE(query && query->GetQueryPurpose() == qpLUsersMOTD);
    theApp.m_bDisableMOTD = true;
    theApp.m_flags1 &= ~DWORD(F1_SHOWMOTD);

    serverConn.ProcessMessage(QStringLiteral(":%1 251 %2 :%3")
                                  .arg(server, nick, luserText));
    serverConn.ProcessMessage(QStringLiteral(":%1 375 %2 :- %3")
                                  .arg(server, nick, motdText));
    serverConn.ProcessMessage(QStringLiteral(":%1 372 %2 :- %3")
                                  .arg(server, nick, motdText));
    BOOL sawDialog = FALSE;
    QTimer::singleShot(0, [&] {
        auto* dialog = dynamic_cast<CMOTD*>(application.activeModalWidget());
        REQUIRE(dialog != nullptr);
        REQUIRE(dialog->m_strLUSER == luserText + QLatin1Char('\n'));
        REQUIRE(dialog->m_strMOTD == motdText + QStringLiteral("\r\n"));
        sawDialog = TRUE;
        dialog->accept();
    });
    serverConn.ProcessMessage(QStringLiteral(":%1 376 %2 :%3")
                                  .arg(server, nick, motdText));
    REQUIRE(sawDialog);
    REQUIRE(serverConn.m_queries.FindQuery(ctLUsersMOTD) == nullptr);
    REQUIRE(serverConn.m_strLUSER.isEmpty());
    REQUIRE(serverConn.m_strMOTD.isEmpty());
    REQUIRE(!theApp.m_bDisableMOTD);

    REQUIRE(CommunicationInits());
    GetIrcProto()->SetConnectionStatus(CX_NOCHANNEL);
    QTimer::singleShot(0, [&] {
        auto* dialog = dynamic_cast<CAwayDlg*>(application.activeModalWidget());
        REQUIRE(dialog != nullptr);
        REQUIRE(dialog->m_rtfAwayMsg.toPlainText() == awayText);
        dialog->accept();
    });
    theApp.OnAwayToggle();
    REQUIRE(theApp.m_bAway && theApp.m_bAwayPrompt);
    REQUIRE(theApp.m_strAwayMessage == awayText);
    theApp.OnAwayToggle();
    REQUIRE(!theApp.m_bAway && !theApp.m_bAwayPrompt);

    CapturingIrcProto awayProtocol;
    awayProtocol.ChatSetAway(true, awayText);
    awayProtocol.ChatSetAway(false, awayText);
    REQUIRE(awayProtocol.sent.size() == 2);
    REQUIRE(awayProtocol.sent.at(0)
            == QStringLiteral("AWAY :%1\r\n").arg(awayText));
    REQUIRE(awayProtocol.sent.at(1) == QStringLiteral("AWAY\r\n"));

    CommunicationCleanup();
    serverConn.m_queries.FreeRemoveAll();
    return EXIT_SUCCESS;
}
