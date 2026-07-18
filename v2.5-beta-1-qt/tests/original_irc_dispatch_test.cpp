#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "protsupp.h"
#include "query.h"
#include "setupdlg.h"
#include "status.h"

#include <QApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <utility>

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

IRCPARSE parsed(const QString& line)
{
    IRCPARSE parse;
    ParseIt(line, &parse);
    return parse;
}

void requirePrint(const CIrcPrint& print, UINT type, COLORREF color,
                  BYTE offset, BOOL newLine)
{
    REQUIRE(print.m_iType == type);
    REQUIRE(print.m_crTextColor == color);
    REQUIRE(print.m_offset == offset);
    REQUIRE(print.m_bNewLine == newLine);
}

QString formattedResource(const QString& identifier, const QString& value)
{
    QString result = originalResourceString(identifier);
    result.replace(QStringLiteral("%s"), value);
    return result;
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

const OriginalDialogControl* dialogControl(
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
    const int baseX = qMax(1,
        (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
    const int baseY = qMax(1, metrics.height());
    const auto x = [baseX](int dlu) { return (dlu * baseX + 2) / 4; };
    const auto y = [baseY](int dlu) { return (dlu * baseY + 4) / 8; };
    return {x(control.x), y(control.y),
            x(control.width), y(control.height)};
}

class CapturingIrcProto final : public CIrcProto {
public:
    void SendMessageText(const QString& raw) override
    {
        sent.append(raw);
    }

    QStringList sent;
};
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();

    REQUIRE(RPL_YOURHOST == 2);
    REQUIRE(RPL_ENDOFSTATS == 219);
    REQUIRE(RPL_USERHOST == 302);
    REQUIRE(RPL_EVENTLIST == 809);
    REQUIRE(ERR_UNKNOWNCOMMAND == 421);
    REQUIRE(ERR_AUTHENTICATIONFAILED == 910);
    REQUIRE(bIsErrorCode(ERR_NOSUCHNICK));
    REQUIRE(bIsErrorCode(ERR_NOJOINDYNAMIC));
    REQUIRE(bIsErrorCode(ERR_INTERNALERROR));
    REQUIRE(!bIsErrorCode(RPL_EVENTLIST));

    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString nick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString channel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString payload = originalResourceString(
        QStringLiteral("IDS_ERR_NOMOTD"));

    {
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_NICKNAME"));
        CNicknameDlg dialog;
        dialog.m_label = payload;
        dialog.m_strNickname = nick;
        dialog.m_bSpacesAllowed = FALSE;
        dialog.show();
        application.processEvents();
        auto* label = dialog.findChild<QLabel*>(
            QStringLiteral("IDC_STATICNICKNAME"));
        auto* edit = dialog.findChild<QLineEdit*>(
            QStringLiteral("IDC_NEWNICK"));
        REQUIRE(dialog.windowTitle() == resource.caption);
        REQUIRE(label && label->text() == payload);
        REQUIRE(edit && edit->text() == nick);
        REQUIRE(edit->maxLength() == MAX_NICKINPUT);
        REQUIRE(edit->geometry() == resourceRect(
            *dialogControl(resource, QStringLiteral("IDC_NEWNICK")),
            dialog.font()));
        QString spaced = nick + QLatin1Char(' ') + nick;
        int position = 0;
        REQUIRE(edit->validator()->validate(spaced, position)
                == QValidator::Invalid);
        dialog.hide();
    }

    {
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_CHANPASSWORD"));
        CPasswordDlg dialog;
        dialog.m_strMessage = payload;
        dialog.m_strPassword = nick;
        dialog.show();
        application.processEvents();
        auto* message = dialog.findChild<QLabel*>(
            QStringLiteral("IDC_PASSWORD_MESG"));
        auto* password = dialog.findChild<QLineEdit*>(
            QStringLiteral("IDC_PASSWORD"));
        REQUIRE(dialog.windowTitle() == resource.caption);
        REQUIRE(message && message->text() == payload);
        REQUIRE(password && password->text() == nick);
        REQUIRE(password->echoMode() == QLineEdit::Password);
        REQUIRE(password->maxLength() == MAX_CHANNELPWD);
        REQUIRE(password->geometry() == resourceRect(
            *dialogControl(resource, QStringLiteral("IDC_PASSWORD")),
            dialog.font()));
        dialog.hide();
    }

    {
        CIrcProto protocol;
        QString selectedNickname;
        QTimer::singleShot(0, [nick] {
            auto* dialog = dynamic_cast<CNicknameDlg*>(
                QApplication::activeModalWidget());
            REQUIRE(dialog);
            auto* edit = dialog->findChild<QLineEdit*>(
                QStringLiteral("IDC_NEWNICK"));
            REQUIRE(edit);
            edit->setText(nick);
            dialog->accept();
        });
        protocol.TryNewNick(ID_ERR_BAD_NICK, nick, FALSE,
                            &selectedNickname);
        REQUIRE(selectedNickname == nick);

        CIrcSocket routedSocket(&protocol);
        const QString line = QStringLiteral(":%1 433 %2 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QTimer::singleShot(0, [] {
            auto* dialog = dynamic_cast<CNicknameDlg*>(
                QApplication::activeModalWidget());
            REQUIRE(dialog);
            dialog->reject();
        });
        routedSocket.HandleErrorCode(line, &parse, &print);
        REQUIRE(print.m_iType == PT_NONE);
    }

    {
        CRoomInfo enterInfo;
        enterInfo.m_strChannel = channel;
        enterInfo.m_strPassword = nick;
        QString prompt = originalResourceString(ID_PASSWORD_PROMPT);
        prompt.replace(QStringLiteral("%1"), DecodeChan(channel));
        QTimer::singleShot(0, [prompt] {
            auto* message = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            REQUIRE(message);
            REQUIRE(message->text()
                    == originalResourceString(IDS_BAD_PASSWORD));
            message->accept();
            QTimer::singleShot(0, [prompt] {
                auto* dialog = dynamic_cast<CPasswordDlg*>(
                    QApplication::activeModalWidget());
                REQUIRE(dialog);
                auto* label = dialog->findChild<QLabel*>(
                    QStringLiteral("IDC_PASSWORD_MESG"));
                REQUIRE(label && label->text() == prompt);
                dialog->reject();
            });
        });
        OnBadChannelPassword(enterInfo);
        REQUIRE(enterInfo.m_strPassword.isEmpty());
    }

    {
        CapturingIrcProto protocol;
        CIrcSocket operSocket(&protocol);
        REQUIRE(operSocket.HrIrcSetOper(nick, payload));
        REQUIRE(protocol.sent == QStringList{
            QStringLiteral("OPER %1 %2\r\n").arg(nick, payload)});

        protocol.sent.clear();
        QTimer::singleShot(0, [payload] {
            auto* dialog = dynamic_cast<CChatPasswordDialog*>(
                QApplication::activeModalWidget());
            REQUIRE(dialog);
            auto* edit = dialog->findChild<QLineEdit*>(
                QStringLiteral("IDC_PASSWORD"));
            auto* ok = dialog->findChild<QPushButton*>(
                QStringLiteral("IDOK"));
            REQUIRE(edit);
            REQUIRE(ok);
            edit->setText(payload);
            ok->click();
        });
        REQUIRE(operSocket.HrIrcSetOper(nick));
        REQUIRE(protocol.sent == QStringList{
            QStringLiteral("OPER %1 %2\r\n").arg(nick, payload)});

        protocol.sent.clear();
        operSocket.SetAuthentication(CChatServer::authtypePlainText,
                                     nick, payload, QString());
        const QString line = QStringLiteral(":%1 464 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QTimer::singleShot(0, [payload] {
            auto* dialog = dynamic_cast<CChatPasswordDialog*>(
                QApplication::activeModalWidget());
            REQUIRE(dialog);
            auto* edit = dialog->findChild<QLineEdit*>(
                QStringLiteral("IDC_PASSWORD"));
            auto* ok = dialog->findChild<QPushButton*>(
                QStringLiteral("IDOK"));
            REQUIRE(edit);
            REQUIRE(ok);
            edit->setText(payload);
            ok->click();
        });
        operSocket.HandleErrorCode(line, &parse, &print);
        REQUIRE(print.m_iType == PT_NONE);
        REQUIRE(protocol.sent == QStringList{
            QStringLiteral("OPER %1 %2\r\n").arg(nick, payload)});
    }

    {
        CapturingIrcProto protocol;
        CIrcSocket ircXSocket(&protocol);
        protocol.m_pSock = &ircXSocket;
        protocol.SetConnectionStatus(CX_CONNECTING);
        ircXSocket.SetAuthentication(CChatServer::authtypeNone,
                                     QString(), QString(), QString());
        ircXSocket.m_nMaxMsgLength = g_nDefaultIOBuff / 2;
        REQUIRE(ircXSocket.m_queries.bAddQuery(new CCQuery(
            qpIsIrcX, ctModeIsIrcX, dtMax, nullptr,
            QString(), QString())));
        ircXSocket.ProcessMessage(
            QStringLiteral(":%1 800 * 0 3 NTLM,ANON 512 *")
                .arg(server));
        REQUIRE(ircXSocket.m_bIrcXServer);
        REQUIRE(ircXSocket.m_bAnonAllowed);
        REQUIRE(ircXSocket.m_rgszSvrSecuPack
                == QStringList{QStringLiteral("NTLM")});
        REQUIRE(ircXSocket.m_nMaxMsgLength == g_nDefaultIOBuff);
        REQUIRE(ircXSocket.m_queries.FindQuery(ctModeIsIrcX) == nullptr);
        REQUIRE(ircXSocket.m_queries.FindQuery(ctIrcX) != nullptr);
        REQUIRE(protocol.sent == QStringList{QStringLiteral("IRCX\r\n")});
        ircXSocket.m_queries.FreeRemoveAll();
    }

    {
        theApp.CleanRoomInfos();
        auto* enterInfo = new CRoomInfo;
        enterInfo->m_strChannel = channel;
        REQUIRE(theApp.AddRoomInfo(enterInfo) > 0);

        CapturingIrcProto protocol;
        CIrcSocket propSocket(&protocol);
        protocol.m_pSock = &propSocket;
        REQUIRE(propSocket.m_queries.bAddQuery(new CCQuery(
            qpJoinPics, ctPropGet, dtMax, nullptr,
            channel, QString())));
        propSocket.ProcessMessage(
            QStringLiteral(":%1 819 %2 %3 :%4")
                .arg(server, nick, channel, payload));
        REQUIRE(propSocket.m_queries.FindQuery(ctPropGet) == nullptr);
        REQUIRE(protocol.sent == QStringList{
            QStringLiteral("JOIN %1\r\n").arg(channel)});
        theApp.CleanRoomInfos();
    }

    CIrcSocket socket;

    {
        const QString line = QStringLiteral(":%1 002 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        requirePrint(print, PT_LASTSTRING, RGB(255, 0, 0), 0, FALSE);
        REQUIRE(print.m_szMessage == line);
    }

    {
        const QString line = QStringLiteral(":%1 004 %2 %1 %3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        requirePrint(print, PT_OFFSET, RGB(255, 0, 0), 3, TRUE);
        REQUIRE(print.m_szMessage == line);
    }

    {
        const QString line = QStringLiteral(":%1 219 %2 %3 :%4")
            .arg(server, nick, channel, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        requirePrint(print, PT_OFFSET, RGB(0, 0, 0), 3, TRUE);
    }

    {
        const QString line = QStringLiteral(":%1 257 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        requirePrint(print, PT_LASTSTRING, RGB(0, 0, 0), 0, FALSE);
    }

    {
        const QString line = QStringLiteral(":%1 302 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        requirePrint(print, PT_WHOLESTRING, RGB(128, 0, 128), 0, TRUE);
        REQUIRE(print.m_szMessage == formattedResource(
            QStringLiteral("IDS_USERHOST_PREFIX"), payload));
    }

    {
        const QString line = QStringLiteral(":%1 303 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        requirePrint(print, PT_WHOLESTRING, RGB(0, 0, 255), 0, TRUE);
        REQUIRE(print.m_szMessage == formattedResource(
            QStringLiteral("IDS_ISON_PREFIX"), payload));
    }

    {
        const QString line = QStringLiteral(":%1 312 %2 %2 %1 :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        requirePrint(print, PT_OFFSET, RGB(0, 0, 128), 3, FALSE);

        REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
            qpGetIdent, ctWhoIs, dtMax, nullptr, channel, nick)));
        print = CIrcPrint{};
        socket.HandleResultCode(display, line, &parse, &print);
        REQUIRE(print.m_iType == PT_NONE);
        socket.m_queries.FreeRemoveAll();
    }

    {
        const QString line = QStringLiteral(":%1 369 %2 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        requirePrint(print, PT_OFFSET, RGB(0, 0, 128), 3, TRUE);
    }

    {
        const QString infoLine = QStringLiteral(":%1 371 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE info = parsed(infoLine);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, infoLine, &info, &print);
        requirePrint(print, PT_OFFSET, RGB(128, 0, 0), 3, FALSE);

        const QString endLine = QStringLiteral(":%1 374 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE end = parsed(endLine);
        print = CIrcPrint{};
        socket.HandleResultCode(display, endLine, &end, &print);
        requirePrint(print, PT_OFFSET, RGB(128, 0, 0), 3, TRUE);
    }

    {
        const QString accessLine = QStringLiteral(":%1 804 %2 %3 :%4")
            .arg(server, nick, channel, payload);
        IRCPARSE access = parsed(accessLine);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, accessLine, &access, &print);
        requirePrint(print, PT_OFFSET, RGB(0, 0, 0), 3, FALSE);

        const QString eventLine = QStringLiteral(":%1 809 %2 %3 :%4")
            .arg(server, nick, channel, payload);
        IRCPARSE event = parsed(eventLine);
        print = CIrcPrint{};
        socket.HandleResultCode(display, eventLine, &event, &print);
        requirePrint(print, PT_OFFSET, RGB(0, 0, 0), 3, TRUE);
    }

    const auto requireResultFallback = [&](const QString& line,
                                           COLORREF color, BYTE offset,
                                           BOOL newLine) {
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        requirePrint(print, PT_OFFSET, color, offset, newLine);
        REQUIRE(print.m_szMessage == line);
    };

    const QString whoIsUser = QStringLiteral(":%1 311 %2 %2 %3 %1 * :%4")
        .arg(server, nick, payload);
    const QString endWhoIs = QStringLiteral(":%1 318 %2 %2 :%3")
        .arg(server, nick, payload);
    const QString channelMode = QStringLiteral(":%1 324 %2 %3 +nt")
        .arg(server, nick, channel);
    const QString noTopic = QStringLiteral(":%1 331 %2 %3 :%4")
        .arg(server, nick, channel, payload);
    const QString namesReply = QStringLiteral(":%1 353 %2 = %3 :%2")
        .arg(server, nick, channel);
    const QString endNames = QStringLiteral(":%1 366 %2 %3 :%4")
        .arg(server, nick, channel, payload);
    const QString whoReply = QStringLiteral(
        ":%1 352 %2 %3 %2 %1 %1 %2 H :%4")
        .arg(server, nick, channel, payload);
    const QString endWho = QStringLiteral(":%1 315 %2 %3 :%4")
        .arg(server, nick, channel, payload);
    const QString ircXReply = QStringLiteral(
        ":%1 800 * 0 3 ANON 512 *")
        .arg(server);
    const QString propList = QStringLiteral(
        ":%1 818 %2 %3 PICS :%4")
        .arg(server, nick, channel, payload);

    requireResultFallback(whoIsUser, RGB(0, 0, 128), 3, FALSE);
    requireResultFallback(endWhoIs, RGB(0, 0, 128), 3, TRUE);
    requireResultFallback(channelMode, RGB(0, 0, 0), 3, FALSE);
    requireResultFallback(noTopic, RGB(0, 0, 128), 3, TRUE);
    requireResultFallback(namesReply, RGB(128, 128, 0), 4, FALSE);
    requireResultFallback(endNames, RGB(128, 128, 0), 3, TRUE);
    requireResultFallback(whoReply, RGB(0, 128, 128), 3, FALSE);
    requireResultFallback(endWho, RGB(0, 128, 128), 3, TRUE);
    requireResultFallback(ircXReply, RGB(0, 0, 0), 3, TRUE);
    requireResultFallback(propList, RGB(0, 0, 0), 3, TRUE);

    const auto requireQuerySuppression = [&](const QString& line,
                                             enumQueryPurpose purpose,
                                             enumCommandType command) {
        socket.m_queries.FreeRemoveAll();
        REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
            purpose, command, dtMax, nullptr, channel, nick)));
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleResultCode(display, line, &parse, &print);
        REQUIRE(print.m_iType == PT_NONE);
    };
    requireQuerySuppression(whoIsUser, qpGetIdent, ctWhoIs);
    requireQuerySuppression(channelMode, qpInitialMode, ctGetChannelMode);
    requireQuerySuppression(noTopic, qpListMembers, ctTopic);
    requireQuerySuppression(namesReply, qpInitialNames, ctNames);
    requireQuerySuppression(whoReply, qpInitialWho, ctWho);
    requireQuerySuppression(ircXReply, qpIsIrcX, ctModeIsIrcX);
    requireQuerySuppression(propList, qpJoinPics, ctPropGet);
    socket.m_queries.FreeRemoveAll();

    {
        CChatDoc statusDocument;
        CStatusView statusView(&statusDocument);
        statusDocument.m_textView = &statusView;
        statusDocument.m_bStatusView = true;
        const QString statusLine = channel + QStringLiteral(" :") + payload;

        socket.ProcessMessage(
            QStringLiteral(":%1 TOPIC %2 :%3")
                .arg(server, channel, payload));
        REQUIRE(statusView.m_pRichEdit->toPlainText() == statusLine);

        socket.ProcessMessage(
            QStringLiteral(":%1 332 %2 %3 :%4")
                .arg(server, nick, channel, payload));
        QString expected = statusLine + QStringLiteral("\n\n") + statusLine;
        REQUIRE(statusView.m_pRichEdit->toPlainText() == expected);

        QString nowKnown = originalResourceString(
            QStringLiteral("IDS_NOWKNOWNAS"));
        nowKnown.replace(QStringLiteral("%s"), nick);
        socket.ProcessMessage(
            QStringLiteral(":%1!%1@%2 NICK :%1")
                .arg(nick, server));
        expected += QStringLiteral("\n\n") + nowKnown;
        REQUIRE(statusView.m_pRichEdit->toPlainText() == expected);

        REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
            qpComSetChannelMode, ctSetChannelMode, dtMax, nullptr,
            channel, QString())));
        socket.ProcessMessage(
            QStringLiteral(":%1 MODE %2 -o %3")
                .arg(server, channel, nick));
        expected += QStringLiteral("\n\nMODE %1 -o %2")
            .arg(channel, nick);
        REQUIRE(statusView.m_pRichEdit->toPlainText() == expected);
        REQUIRE(socket.m_queries.FindQuery(ctSetChannelMode) == nullptr);

        socket.ProcessMessage(
            QStringLiteral(":%1 MODE %2 +q %3")
                .arg(server, channel, nick));
        expected += QStringLiteral("\n\nMODE %1 +q %2")
            .arg(channel, nick);
        REQUIRE(statusView.m_pRichEdit->toPlainText() == expected);
        statusDocument.m_textView = nullptr;
        statusDocument.m_bStatusView = false;
    }

    for (const auto& command : {
             std::pair<QString, UINT>(QStringLiteral("CLONE"), PT_OFFSET),
             std::pair<QString, UINT>(QStringLiteral("KNOCK"), PT_WHOLESTRING),
             std::pair<QString, UINT>(QStringLiteral("PONG"), PT_OFFSET),
             std::pair<QString, UINT>(QStringLiteral("KILLED"), PT_WHOLESTRING)}) {
        const QString line = QStringLiteral(":%1 %2 %3 :%4")
            .arg(server, command.first, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleCommand(display, line, &parse, &print);
        REQUIRE(print.m_iType == command.second);
        REQUIRE(print.m_szMessage == line);
        REQUIRE(print.m_bNewLine);
        if (command.first == QLatin1String("CLONE")
            || command.first == QLatin1String("PONG")) {
            REQUIRE(print.m_offset == 1);
            REQUIRE(print.m_crTextColor == RGB(0, 0, 0));
        } else if (command.first == QLatin1String("KNOCK")) {
            REQUIRE(print.m_crTextColor == RGB(0, 0, 0));
        } else {
            REQUIRE(print.m_crTextColor == RGB(0, 0, 255));
        }
    }

    {
        const QString line = QStringLiteral(":%1 DATA %2 CCUDI1 :#")
            .arg(server, channel);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleCommand(display, line, &parse, &print);
        REQUIRE(print.m_iType == PT_NONE);
    }

    {
        const QString line = QStringLiteral(":%1 PROP %2 TOPIC :%3")
            .arg(server, channel, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleCommand(display, line, &parse, &print);
        requirePrint(print, PT_OFFSET, RGB(0, 0, 0), 2, FALSE);
    }

    for (const QString& command : {QStringLiteral("NOTICE"),
                                   QStringLiteral("PRIVMSG")}) {
        const QString line = QStringLiteral("%1 %2 :%3")
            .arg(command, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleCommand(display, line, &parse, &print);
        requirePrint(print, PT_LASTSTRING, RGB(128, 0, 128), 0, FALSE);
    }

    for (const QString& command : {QStringLiteral("REPLY"),
                                   QStringLiteral("REQUEST")}) {
        const QString line = QStringLiteral(":%1 %2 %3 :%4")
            .arg(server, command, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        QString display;
        socket.HandleCommand(display, line, &parse, &print);
        REQUIRE(print.m_iType == PT_NOTINIT);
    }

    {
        const QString line = QStringLiteral(":%1 421 %2 %3 :%4")
            .arg(server, nick, QStringLiteral("REPLY"), payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        socket.HandleErrorCode(line, &parse, &print);
        requirePrint(print, PT_OFFSET, RGB(255, 0, 0), 3, TRUE);
    }

    const auto requireModalError = [&](int code, const QString& expected) {
        const QString line = QStringLiteral(":%1 %2 %3 %4 :%5")
            .arg(server).arg(code).arg(nick, channel, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        closeInformationMessage(expected);
        socket.HandleErrorCode(line, &parse, &print);
        REQUIRE(print.m_iType == PT_NONE);
    };

    for (const auto& error : {
             std::pair<int, QString>(
                 ERR_TOOMANYCHANNELS,
                 originalResourceString(
                     QStringLiteral("IDS_ERR_TOOMANYCHANNELS"))),
             std::pair<int, QString>(
                 ERR_NICKCOLLISION,
                 originalResourceString(
                     QStringLiteral("IDS_ERR_NICKCOLLISION"))),
             std::pair<int, QString>(
                 ERR_NICKTOOFAST,
                 originalResourceString(
                     QStringLiteral("IDS_ERR_NICKTOOFAST"))),
             std::pair<int, QString>(
                 ERR_NICKNOCHANGE,
                 originalResourceString(
                     QStringLiteral("IDS_ERR_NICKNOCHANGE"))),
             std::pair<int, QString>(
                 ERR_YOUREBANNEDCREEP,
                 originalResourceString(
                     QStringLiteral("IDS_ERR_YOUREBANNEDCREEP"))),
             std::pair<int, QString>(
                 ERR_YOUWILLBEBANNED,
                 originalResourceString(
                     QStringLiteral("IDS_ERR_YOUWILLBEBANNED"))),
             std::pair<int, QString>(
                 ERR_NOJOINDYNAMIC,
                 originalResourceString(
                     QStringLiteral("IDS_ERR_NOJOINDYNAMIC"))),
             std::pair<int, QString>(
                 ERR_NODYNAMICCHANNELS,
                 originalResourceString(
                     QStringLiteral("IDS_ERR_NODYNAMICCHANNELS"))),
             std::pair<int, QString>(
                 ERR_AUTHONLY,
                 originalResourceString(
                     QStringLiteral("IDS_ERR_AUTHONLY")))}) {
        requireModalError(error.first, error.second);
    }

    for (const auto& error : {
             std::pair<int, QString>(
                 ERR_CHANNELISFULL,
                 formattedResource(QStringLiteral("ID_ERR_CHANNELISFULL"),
                                   DecodeChan(channel))),
             std::pair<int, QString>(
                 ERR_INVITEONLYCHAN,
                 formattedResource(QStringLiteral("ID_ERR_INVITEONLY"),
                                   DecodeChan(channel))),
             std::pair<int, QString>(
                 ERR_BANNEDFROMCHAN,
                 formattedResource(QStringLiteral("ID_ERR_BANNEDFROMCHAN"),
                                   DecodeChan(channel)))}) {
        requireModalError(error.first, error.second);
    }

    theApp.CleanRoomInfos();
    auto* pendingRoom = new CRoomInfo;
    pendingRoom->m_strChannel = channel;
    REQUIRE(theApp.AddRoomInfo(pendingRoom) == 1);
    REQUIRE(theApp.m_enterInfos.size() == 2);
    requireModalError(
        ERR_CHANNELISFULL,
        formattedResource(QStringLiteral("ID_ERR_CHANNELISFULL"),
                          DecodeChan(channel)));
    REQUIRE(theApp.m_enterInfos.size() == 1);

    {
        REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
            qpComSetUserMode, ctSetUserMode, dtMax, nullptr,
            QString(), nick)));
        const QString line = QStringLiteral(":%1 401 %2 %2 :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        closeInformationMessage(formattedResource(
            QStringLiteral("IDS_ERR_NOSUCHNICK"), DecodeNick(nick)));
        socket.HandleErrorCode(line, &parse, &print);
        REQUIRE(print.m_iType == PT_NONE);
        REQUIRE(socket.m_queries.FindQuery(ctSetUserMode) == nullptr);
    }

    {
        const QString line = QStringLiteral(":%1 401 %2 %3 :%4")
            .arg(server, nick, channel, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        closeInformationMessage(formattedResource(
            QStringLiteral("IDS_ERR_NOSUCHCHANNEL"),
            DecodeChan(channel)));
        socket.HandleErrorCode(line, &parse, &print);
        REQUIRE(print.m_iType == PT_NONE);
    }

    const auto requireChannelModeError = [&](int code) {
        socket.m_queries.FreeRemoveAll();
        REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
            qpComSetChannelMode, ctSetChannelMode, dtMax, nullptr,
            channel, QString())));
        const QString line = QStringLiteral(":%1 %2 %3 %4 :%5")
            .arg(server).arg(code).arg(nick, channel, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        socket.HandleErrorCode(line, &parse, &print);
        requirePrint(print, PT_OFFSET, RGB(255, 0, 0), 3, TRUE);
        REQUIRE(socket.m_queries.FindQuery(ctSetChannelMode) == nullptr);
    };
    requireChannelModeError(ERR_KEYSET);
    requireChannelModeError(ERR_CHANOPRIVSNEEDED);

    for (int code : {ERR_UMODEUNKNOWNFLAG, ERR_USERSDONTMATCH}) {
        socket.m_queries.FreeRemoveAll();
        REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
            qpComSetUserMode, ctSetUserMode, dtMax, nullptr,
            QString(), nick)));
        const QString line = QStringLiteral(":%1 %2 %3 :%4")
            .arg(server).arg(code).arg(nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        socket.HandleErrorCode(line, &parse, &print);
        requirePrint(print, PT_OFFSET, RGB(255, 0, 0), 3, TRUE);
        REQUIRE(socket.m_queries.FindQuery(ctSetUserMode) == nullptr);
    }

    socket.m_queries.FreeRemoveAll();
    REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
        qpComSetChannelMode, ctSetChannelMode, dtMax, nullptr,
        channel, QString())));
    REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
        qpComSetUserMode, ctSetUserMode, dtMax, nullptr,
        QString(), nick)));
    {
        const QString line = QStringLiteral(":%1 461 %2 MODE :%3")
            .arg(server, nick, payload);
        IRCPARSE parse = parsed(line);
        CIrcPrint print;
        socket.HandleErrorCode(line, &parse, &print);
        requirePrint(print, PT_OFFSET, RGB(255, 0, 0), 3, TRUE);
        REQUIRE(socket.m_queries.FindQuery(ctSetChannelMode) == nullptr);
        REQUIRE(socket.m_queries.FindQuery(ctSetUserMode) != nullptr);
    }
    socket.m_queries.FreeRemoveAll();

    {
        CQueryPtrList queries;
        REQUIRE(queries.bAddQuery(new CCQuery(
            qpInitialWho, ctWho, dtMax, nullptr, channel, nick)));
        REQUIRE(queries.bAddQuery(new CCQuery(
            qpComSetChannelMode, ctSetChannelMode, dtMax, nullptr,
            channel, QString())));
        REQUIRE(queries.bAddQuery(new CCQuery(
            qpComSetUserMode, ctSetUserMode, dtMax, nullptr,
            QString(), nick)));
        LONG rank = 0;
        REQUIRE(queries.FindQuery(ctWho, nullptr, &rank) != nullptr);
        REQUIRE(rank == 1);
        REQUIRE(queries.FindQuery(ctSetChannelMode, nullptr, &rank) != nullptr);
        REQUIRE(rank == 2);
        REQUIRE(queries.FindQuery(ctSetUserMode, nullptr, &rank) != nullptr);
        REQUIRE(rank == 3);
    }

    socket.m_queries.FreeRemoveAll();
    REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
        qpInitialWho, ctWho, dtMax, nullptr, channel, nick)));
    REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
        qpComSetChannelMode, ctSetChannelMode, dtMax, nullptr,
        channel, QString())));
    REQUIRE(socket.m_queries.bAddQuery(new CCQuery(
        qpComSetUserMode, ctSetUserMode, dtMax, nullptr,
        QString(), nick)));
    REQUIRE(socket.bFreeModeCell(&channel, &nick));
    REQUIRE(socket.m_queries.FindQuery(ctSetChannelMode) == nullptr);
    REQUIRE(socket.m_queries.FindQuery(ctSetUserMode) != nullptr);
    REQUIRE(socket.bFreeModeCell(&channel, &nick));
    REQUIRE(socket.m_queries.FindQuery(ctSetUserMode) == nullptr);
    REQUIRE(!socket.bFreeModeCell(&channel, &nick));
    socket.m_queries.FreeRemoveAll();

    return 0;
}
