#include "chat.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "setupdlg.h"

#include <QCoreApplication>
#include <QTcpSocket>

#include <cstring>
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

class ReentrantReadSocket final : public QTcpSocket {
public:
    ReentrantReadSocket()
    {
        setOpenMode(QIODevice::ReadWrite);
        setSocketState(QAbstractSocket::ConnectedState);
    }

    void SetInput(const QByteArray& firstInput,
                  const QByteArray& secondInput)
    {
        m_input = firstInput;
        m_secondInput = secondInput;
    }

    void StartRead() { emit readyRead(); }

    bool InjectedInput() const { return m_injectedInput; }
    const QByteArray& Output() const { return m_output; }
    qint64 bytesAvailable() const override { return m_input.size(); }

protected:
    qint64 readData(char* data, qint64 maximum) override
    {
        if (m_input.isEmpty()) return 0;
        const qint64 count = qMin(maximum,
                                  static_cast<qint64>(m_input.size()));
        std::memcpy(data, m_input.constData(), static_cast<std::size_t>(count));
        m_input.remove(0, count);
        return count;
    }

    qint64 writeData(const char* data, qint64 length) override
    {
        m_output.append(data, length);
        if (!m_injectedInput
            && QByteArrayView(data, length).startsWith("PONG :")) {
            m_injectedInput = true;
            // Input becomes readable reentrantly while ProcessMessageBytes is
            // still handling the first line. No second signal is emitted.
            m_input += m_secondInput;
        }
        return length;
    }

private:
    QByteArray m_input;
    QByteArray m_secondInput;
    QByteArray m_output;
    bool m_injectedInput = false;
};
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    theApp.InitVals();

    const QString nick = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
    const QString user = QString::fromUtf8(GetMyUserName());
    const QString channel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString trailing = originalResourceString(
        QStringLiteral("ID_KICK_NO_MESG"));

    const QString encodedNick = EncodeNick(nick);
    REQUIRE(DecodeNick(encodedNick) == nick);
    REQUIRE(DecodeNickForScreen(encodedNick) == nick);
    const QString spacedSource = originalResourceString(
        QStringLiteral("IDS_DEFAULT_REALNAME"));
    REQUIRE(DecodeNickForScreen(EncodeNick(spacedSource))
            == QLatin1Char('"') + spacedSource + QLatin1Char('"'));

    const QString message = QStringLiteral(":%1!%2@NoMachine PRIVMSG %3 :%4\r\n")
                                .arg(nick, user, channel, trailing);
    IRCPARSE parse;
    ParseIt(message, &parse);
    REQUIRE(parse.bHasPrefix);
    REQUIRE(parse.nick == nick);
    REQUIRE(parse.user == user);
    REQUIRE(parse.machine == QStringLiteral("NoMachine"));
    REQUIRE(parse.nArgs == 2);
    REQUIRE(parse.args.size() == 2);
    REQUIRE(parse.command == QStringLiteral("PRIVMSG"));
    REQUIRE(parse.args.at(1) == channel);
    REQUIRE(parse.nOffsets.at(0) == message.toUtf8().indexOf("PRIVMSG"));
    REQUIRE(parse.nOffsets.at(1) == message.toUtf8().indexOf(channel.toUtf8()));
    REQUIRE(parse.bHasLastString);
    REQUIRE(parse.lastString == trailing);
    REQUIRE(parse.uCode == 0);
    REQUIRE(NGetCmd(parse.command) == cmdidPrivMsg);

    ParseIt(QStringLiteral(":NoMachine 001 %1 :%2")
                .arg(nick, trailing), &parse);
    REQUIRE(parse.nick == QStringLiteral("NoMachine"));
    REQUIRE(parse.user.isEmpty());
    REQUIRE(parse.machine.isEmpty());
    REQUIRE(parse.uCode == RPL_WELCOME);

    ParseIt(QStringLiteral(":%1 MODE %1 +m").arg(channel), &parse);
    REQUIRE(parse.nick == channel);
    REQUIRE(parse.user.isEmpty());
    REQUIRE(parse.machine.isEmpty());

    ParseIt(QStringLiteral(":%1@NoMachine NICK :%1").arg(nick), &parse);
    REQUIRE(parse.nick == nick);
    REQUIRE(parse.user.isEmpty());
    REQUIRE(parse.machine.isEmpty());

    ParseIt(QStringLiteral("ACTION \"%1\"").arg(trailing), &parse, TRUE);
    REQUIRE(parse.nArgs == 2);
    REQUIRE(parse.args.at(1) == QStringLiteral("\"") + trailing
                                  + QStringLiteral("\""));
    REQUIRE(parse.nOffsets.at(1) == int(sizeof("ACTION ") - 1));

    QStringList tokens;
    for (int index = 0; index < 11; ++index) {
        tokens.append(QString::fromLatin1(g_rgIrcCmd[index].szCmd));
    }
    const QString overfull = tokens.join(QLatin1Char(' '));
    ParseIt(overfull, &parse);
    REQUIRE(parse.nArgs == MAXARGS);
    REQUIRE(parse.args.last() == tokens.at(MAXARGS - 1));
    REQUIRE(parse.bHasLastString);
    REQUIRE(parse.lastString == QLatin1Char(' ') + tokens.at(MAXARGS));
    for (int index = 0; index < cmdidMax; ++index) {
        REQUIRE(NGetCmd(QString::fromLatin1(g_rgIrcCmd[index].szCmd).toLower())
                == index);
    }

    const QByteArray jisUser = QByteArray::fromHex(
        "1b244224221b2842");
    const QByteArray utfTrailing = QByteArray::fromHex("c2a2e282ac");
    const QByteArray rawMessage = QByteArrayLiteral(":") + nick.toUtf8()
        + QByteArrayLiteral("!") + jisUser + QByteArrayLiteral("@")
        + QByteArray(g_szNoMachine) + QByteArrayLiteral(" PRIVMSG ")
        + channel.toUtf8() + QByteArrayLiteral(" :") + utfTrailing;
    ParseIt(rawMessage, &parse);
    REQUIRE(parse.nick == nick);
    REQUIRE(parse.user.toLatin1() == jisUser);
    REQUIRE(parse.machine == QString::fromLatin1(g_szNoMachine));
    REQUIRE(parse.command == QStringLiteral("PRIVMSG"));
    REQUIRE(parse.lastString.toLatin1() == utfTrailing);
    REQUIRE(parse.nOffsets.at(0) == rawMessage.indexOf("PRIVMSG"));

    const QString extendedNick = EncodeNick(spacedSource);
    const QByteArray extendedPrefix = QByteArrayLiteral(":")
        + extendedNick.toUtf8() + QByteArrayLiteral("@")
        + QByteArray(g_szNoMachine) + QByteArrayLiteral(" NICK :")
        + encodedNick.toUtf8();
    ParseIt(extendedPrefix, &parse);
    REQUIRE(parse.nick == extendedNick);
    REQUIRE(parse.lastString.toLatin1() == encodedNick.toLatin1());

    // Original CIrcSocket::OnReceive clears a complete line before
    // ProcessMessage and then explicitly rechecks its shared input buffer
    // because message processing can receive more bytes reentrantly. Qt does
    // not emit readyRead recursively, so handleReadyRead must drain bytes that
    // arrive while the slot is active.
    auto* client = new ReentrantReadSocket;
    const QByteArray firstPayload = nick.toUtf8();
    const QByteArray secondPayload = channel.toUtf8();
    const QByteArray firstInput = QByteArrayLiteral("PING :")
        + firstPayload + QByteArrayLiteral("\r\n");
    const QByteArray secondInput = QByteArrayLiteral("PING :")
        + secondPayload + QByteArrayLiteral("\r\n");
    const QByteArray expectedOutput = QByteArrayLiteral("PONG :")
        + firstPayload + QByteArrayLiteral("\r\nPONG :")
        + secondPayload + QByteArrayLiteral("\r\n");

    {
        CIrcSocket socket;
        socket.AdoptSocket(client);
        client->SetInput(firstInput, secondInput);
        client->StartRead();
        REQUIRE(client->InjectedInput());
        REQUIRE(client->Output() == expectedOutput);
    }

    return 0;
}
