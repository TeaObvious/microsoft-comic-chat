#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "filesend.h"
#include "histent.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "panel.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHostAddress>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <functional>

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
    void SendMessageText(const QString& raw) override { sent.append(raw); }
    void SendMessageBytes(const QByteArray& raw) override
    {
        sent.append(QString::fromUtf8(raw));
    }
    QList<QString> sent;
};

QString wirePayload(const QString& wire)
{
    const qsizetype marker = wire.indexOf(QStringLiteral(" :"));
    if (marker < 0) return QString();
    QString payload = wire.mid(marker + 2);
    if (payload.endsWith(QStringLiteral("\r\n"))) payload.chop(2);
    return payload;
}

template<typename Predicate>
bool waitUntil(Predicate predicate, int timeoutMilliseconds = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

void pumpEventsFor(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

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
    const int baseX = qMax(
        1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
    const int baseY = qMax(1, metrics.height());
    const auto x = [baseX](int dlu) { return (dlu * baseX + 2) / 4; };
    const auto y = [baseY](int dlu) { return (dlu * baseY + 4) / 8; };
    return {x(control.x), y(control.y),
            x(control.width), y(control.height)};
}

quint16 unusedLocalPort()
{
    QTcpServer reservation;
    REQUIRE(reservation.listen(QHostAddress::LocalHost, 0));
    const quint16 port = reservation.serverPort();
    reservation.close();
    return port;
}

QByteArray cumulativeAck(quint32 bytes)
{
    QByteArray ack(4, '\0');
    ack[0] = static_cast<char>((bytes >> 24) & 0xffU);
    ack[1] = static_cast<char>((bytes >> 16) & 0xffU);
    ack[2] = static_cast<char>((bytes >> 8) & 0xffU);
    ack[3] = static_cast<char>(bytes & 0xffU);
    return ack;
}

quint32 ackValue(const QByteArray& bytes, qsizetype offset)
{
    REQUIRE(offset >= 0 && bytes.size() >= offset + 4);
    const auto octet = [&bytes](qsizetype index) {
        return static_cast<quint32>(
            static_cast<unsigned char>(bytes.at(index)));
    };
    return (octet(offset) << 24)
        | (octet(offset + 1) << 16)
        | (octet(offset + 2) << 8)
        | octet(offset + 3);
}

QByteArray sourceDerivedPayload(qsizetype size)
{
    QByteArray seed = originalResourceString(
        QStringLiteral("IDS_DEFAULTGREETING")).toLatin1();
    REQUIRE(!seed.isEmpty());
    QByteArray payload;
    payload.reserve(size);
    while (payload.size() < size) payload.append(seed);
    payload.truncate(size);
    return payload;
}

QString transferTitle(const QString& identifier, int percent,
                      const QString& fileName, const QString& otherGuy)
{
    QString title = originalResourceString(identifier);
    title.replace(QStringLiteral("%1"), QString::number(percent));
    title.replace(QStringLiteral("%2"), fileName);
    title.replace(QStringLiteral("%3"), otherGuy);
    return title;
}

QList<CFileProgress*> fileProgressWindows()
{
    QList<CFileProgress*> result;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto* progress = dynamic_cast<CFileProgress*>(widget))
            result.append(progress);
    }
    return result;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    theApp.InitVals();
    theApp.m_bComicView = true;
    theApp.InitializeComicsFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont, theApp.m_comicsColor));
    InitializeAvatars();

    QString selfAvatarName;
    QString otherAvatarName;
    GetNextAvatarName(selfAvatarName);
    GetNextAvatarName(otherAvatarName);
    REQUIRE(!selfAvatarName.isEmpty());
    REQUIRE(!otherAvatarName.isEmpty());
    REQUIRE(selfAvatarName.compare(otherAvatarName, Qt::CaseInsensitive) != 0);
    theApp.m_myCharacterName = selfAvatarName;

    const QString selfNick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString channel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString identity = otherAvatarName + QLatin1Char('@') + server;

    {
        const QString quotedSource =
            selfNick + QLatin1Char(' ') + QLatin1Char('\n')
            + QLatin1Char('\r') + QLatin1Char('\001')
            + QLatin1Char('\\');
        const QString quoted = CTCPQuoteString(quotedSource);
        REQUIRE(quoted
                == selfNick + QStringLiteral("\\@\\n\\r\\1\\\\"));
        QString unquoted = quoted;
        REQUIRE(CTCPUnQuoteString(&unquoted));
        REQUIRE(unquoted == quotedSource);

        // CTCPUnQuoteString's preliminary source scan deliberately does not
        // count a doubled backslash as a quoted character by itself.
        QString backslashOnly = QStringLiteral("\\\\");
        REQUIRE(!CTCPUnQuoteString(&backslashOnly));
        REQUIRE(backslashOnly == QStringLiteral("\\\\"));

        const QString sourceExtended =
            QString::fromLatin1("R\xC9" "GIS");
        REQUIRE(CTCPQuoteString(sourceExtended) == sourceExtended);
        const QString sourceTab =
            selfNick + QLatin1Char('\t') + selfNick;
        REQUIRE(CTCPQuoteString(sourceTab) == sourceTab);
        QString nulTerminated = QStringLiteral("x y");
        nulTerminated.append(QChar());
        nulTerminated.append(QStringLiteral("ignored"));
        REQUIRE(CTCPQuoteString(nulTerminated)
                == QStringLiteral("x\\@y"));
        QString nulUnquoted = QStringLiteral("x\\@y");
        nulUnquoted.append(QChar());
        nulUnquoted.append(QStringLiteral("ignored"));
        REQUIRE(CTCPUnQuoteString(&nulUnquoted));
        REQUIRE(nulUnquoted == QStringLiteral("x y"));
        QString nulPlain = QStringLiteral("plain");
        nulPlain.append(QChar());
        nulPlain.append(QStringLiteral("\\@ignored"));
        REQUIRE(!CTCPUnQuoteString(&nulPlain));
        REQUIRE(nulPlain == QStringLiteral("plain"));

        constexpr quint32 loopbackHost = 0x7f000001U;
        constexpr quint16 sourceStartPort = 7011;
        const QString offeredName = selfNick + QLatin1Char(' ') + selfNick;
        const QString wireOffer = BuildDCCSendOffer(
            offeredName, loopbackHost, sourceStartPort, 0);
        REQUIRE(wireOffer
                == QStringLiteral("\001DCC SEND %1\\@%1 2130706433 7011 0\001")
                       .arg(selfNick));

        DCCSendOffer offer;
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SeNd %1\\@%1 2130706433 7011 1\001")
                .arg(selfNick),
            &offer));
        REQUIRE(offer.fileName == offeredName);
        REQUIRE(offer.hostAddress == loopbackHost);
        REQUIRE(offer.port == sourceStartPort);
        REQUIRE(offer.fileSizeKnown);
        REQUIRE(offer.fileSize == 1);

        // The Win32 source uses atol/atoi without host/port validation and
        // then stores the port in sixteen bits.
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 invalid 65537 1\001").arg(selfNick),
            &offer));
        REQUIRE(offer.hostAddress == 0);
        REQUIRE(offer.port == 1);
        REQUIRE(offer.fileSize == 1);
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral(
                "SEND file 999999999999999999999 "
                "999999999999999999999 1\001"),
            &offer));
        REQUIRE(offer.hostAddress == 0x7fffffffU);
        REQUIRE(offer.port == 0xffffU);
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral(
                "SEND file -999999999999999999999 "
                "-999999999999999999999 1\001"),
            &offer));
        REQUIRE(offer.hostAddress == 0x80000000U);
        REQUIRE(offer.port == 0);
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND file 1 invalid 1\001"), &offer));
        REQUIRE(offer.hostAddress == 1);
        REQUIRE(offer.port == 0);
        // GetToken2 has the source's four-byte whitespace boundary, but the
        // MS-CRT atol/atoi step still skips leading vertical tab/form feed.
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND file \v1 \f2 \v3\001"), &offer));
        REQUIRE(offer.hostAddress == 1);
        REQUIRE(offer.port == 2);
        REQUIRE(offer.fileSize == 3);

        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011\001").arg(selfNick),
            &offer));
        REQUIRE(!offer.fileSizeKnown);
        REQUIRE(offer.fileSize == -1);
        REQUIRE(!ParseDCCSendOffer(
            QStringLiteral("CHAT %1 2130706433 7011 1\001").arg(selfNick),
            &offer));
        REQUIRE(!ParseDCCSendOffer(
            QString::fromUtf8("\xC5\xBF" "END file 2130706433 7011 1\001"),
            &offer));
        REQUIRE(!ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 0\001").arg(selfNick),
            &offer));
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 2147483648\001")
                .arg(selfNick),
            &offer));
        REQUIRE(offer.fileSize == 2147483647);
        REQUIRE(!ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 invalid\001")
                .arg(selfNick),
            &offer));
        REQUIRE(!ParseDCCSendOffer(
            QStringLiteral("SEND\v%1 2130706433 7011 1\001")
                .arg(selfNick),
            &offer));
        REQUIRE(!ParseDCCSendOffer(
            QStringLiteral("SEND\f%1 2130706433 7011 1\001")
                .arg(selfNick),
            &offer));

        const QString oversizedToken(240, QLatin1Char('x'));
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 1\001")
                .arg(oversizedToken),
            &offer));
        REQUIRE(offer.fileName == QString(200, QLatin1Char('x')));

        const QString splitQuote =
            QString(199, QLatin1Char('x')) + QStringLiteral("\\@");
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 1\001")
                .arg(splitQuote),
            &offer));
        REQUIRE(offer.fileName
                == QString(199, QLatin1Char('x')) + QLatin1Char('\\'));

        const QString cp1252Token(150, QChar(0x00e9));
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 1\001")
                .arg(cp1252Token),
            &offer));
        REQUIRE(offer.fileName == cp1252Token);
        const QString oversizedCp1252Token(240, QChar(0x00e9));
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 1\001")
                .arg(oversizedCp1252Token),
            &offer));
        REQUIRE(offer.fileName == QString(200, QChar(0x00e9)));

        const QLocale savedLocale;
        QLocale::setDefault(QLocale(QLocale::Japanese,
                                    QLocale::Japan));
        const QString cp932Token(100, QChar(0x3042));
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 1\001")
                .arg(cp932Token),
            &offer));
        REQUIRE(offer.fileName == cp932Token);
        const QString oversizedCp932Token(101, QChar(0x3042));
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 1\001")
                .arg(oversizedCp932Token),
            &offer));
        REQUIRE(offer.fileName == cp932Token);
        const QString splitCp932Token =
            QString(199, QLatin1Char('x')) + QChar(0x3042);
        REQUIRE(ParseDCCSendOffer(
            QStringLiteral("SEND %1 2130706433 7011 1\001")
                .arg(splitCp932Token),
            &offer));
        REQUIRE(offer.fileName
                == QString(199, QLatin1Char('x')) + QChar(0x0082));
        QLocale::setDefault(savedLocale);

        QString nulArguments = QStringLiteral("SEND file");
        nulArguments.append(QChar());
        nulArguments.append(QStringLiteral(" 2130706433 7011 1\001"));
        REQUIRE(!ParseDCCSendOffer(nulArguments, &offer));
    }

    {
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_FILE_TRANSFER"));
        REQUIRE(resource.caption == QStringLiteral("File Send"));
        REQUIRE(resource.fontFamily == QStringLiteral("MS Sans Serif"));
        REQUIRE(resource.fontPointSize == 8);
        REQUIRE(resource.width == 186);
        REQUIRE(resource.height == 93);
        REQUIRE(resource.controls.size() == 9);
        REQUIRE(originalDialogControlText(
                    QStringLiteral("IDD_FILE_TRANSFER"),
                    QStringLiteral("IDC_STATIC_NXFERRED"))
                == QStringLiteral("?:"));
        REQUIRE(originalDialogControlText(
                    QStringLiteral("IDD_FILE_TRANSFER"),
                    QStringLiteral("IDC_BYTES_SENT"))
                == QStringLiteral("0"));
        REQUIRE(originalDialogControlText(
                    QStringLiteral("IDD_FILE_TRANSFER"),
                    QStringLiteral("IDC_BYTES_TOTAL"))
                == QStringLiteral("0"));
        REQUIRE(originalDialogControlText(
                    QStringLiteral("IDD_FILE_TRANSFER"),
                    QStringLiteral("IDC_CX_STATUS"))
                == QStringLiteral("Waiting for connection"));

        CFileProgress dialog;
        REQUIRE(dialog.objectName() == QStringLiteral("IDD_FILE_TRANSFER"));
        REQUIRE(dialog.layout() == nullptr);
        REQUIRE(dialog.windowTitle() == resource.caption);
        REQUIRE(dialog.font().family() == resource.fontFamily);
        REQUIRE(dialog.font().pointSize() == resource.fontPointSize);
        REQUIRE(dialog.windowFlags() & Qt::WindowMinimizeButtonHint);
        REQUIRE(!(dialog.windowFlags() & Qt::WindowMaximizeButtonHint));

        OriginalDialogControl bounds;
        bounds.x = 0;
        bounds.y = 0;
        bounds.width = resource.width;
        bounds.height = resource.height;
        const QSize resourceSize = resourceRect(bounds, dialog.font()).size();
        REQUIRE(dialog.size() == resourceSize);
        REQUIRE(dialog.minimumSize() == resourceSize);
        REQUIRE(dialog.maximumSize() == resourceSize);

        for (const QString& identifier : {
                 QStringLiteral("IDOK"),
                 QStringLiteral("IDCANCEL"),
                 QStringLiteral("IDC_FILEPROGRESS"),
                 QStringLiteral("IDC_STATIC_NXFERRED"),
                 QStringLiteral("IDC_BYTES_SENT"),
                 QStringLiteral("IDC_BYTES_TOTAL"),
                 QStringLiteral("IDC_CX_STATUS")}) {
            QWidget* widget = dialog.findChild<QWidget*>(identifier);
            const OriginalDialogControl* source =
                resourceControl(resource, identifier);
            REQUIRE(widget && source);
            REQUIRE(widget->geometry() == resourceRect(
                *source, dialog.font()));
        }

        auto* ok = dialog.findChild<QPushButton*>(QStringLiteral("IDOK"));
        auto* cancel = dialog.findChild<QPushButton*>(
            QStringLiteral("IDCANCEL"));
        auto* progress = dialog.findChild<QProgressBar*>(
            QStringLiteral("IDC_FILEPROGRESS"));
        auto* xferred = dialog.findChild<QLabel*>(
            QStringLiteral("IDC_STATIC_NXFERRED"));
        auto* bytesSent = dialog.findChild<QLabel*>(
            QStringLiteral("IDC_BYTES_SENT"));
        auto* bytesTotal = dialog.findChild<QLabel*>(
            QStringLiteral("IDC_BYTES_TOTAL"));
        auto* status = dialog.findChild<QLabel*>(
            QStringLiteral("IDC_CX_STATUS"));
        REQUIRE(ok && cancel && progress && xferred);
        REQUIRE(bytesSent && bytesTotal && status);
        REQUIRE(ok->text() == originalDialogControlText(
            QStringLiteral("IDD_FILE_TRANSFER"), QStringLiteral("IDOK")));
        REQUIRE(cancel->text() == originalDialogControlText(
            QStringLiteral("IDD_FILE_TRANSFER"),
            QStringLiteral("IDCANCEL")));
        REQUIRE(!progress->isTextVisible());
        REQUIRE(progress->minimum() == 0);
        REQUIRE(progress->maximum() == 100);
        REQUIRE(progress->value() == 0);
        bool foundBytesTotalCaption = false;
        bool foundStatusCaption = false;
        for (QLabel* label : dialog.findChildren<QLabel*>()) {
            if (label->text() == QStringLiteral("Bytes total:")) {
                foundBytesTotalCaption = true;
            }
            if (label->text() == QStringLiteral("Status:")) {
                foundStatusCaption = true;
            }
        }
        REQUIRE(foundBytesTotalCaption);
        REQUIRE(foundStatusCaption);
        REQUIRE(originalResourceString(QStringLiteral("IDS_FILETIMEOUT"))
                == QStringLiteral(
                    "Transfer timed out.  File transfer failed."));

        const QString fileName = selfNick + QLatin1Char(' ') + channel;
        dialog.Configure(CFileProgress::Direction::Sending, fileName,
                         otherAvatarName, 2050);
        REQUIRE(dialog.TransferDirection()
                == CFileProgress::Direction::Sending);
        REQUIRE(dialog.IsSending());
        REQUIRE(!dialog.IsActive());
        REQUIRE(!dialog.WasCanceled());
        REQUIRE(dialog.BytesTransferred() == 0);
        REQUIRE(dialog.BytesTotal() == 2050);
        REQUIRE(xferred->text() == originalResourceString(
            QStringLiteral("IDS_BYTES_SENT")));
        REQUIRE(bytesSent->text() == QStringLiteral("0"));
        REQUIRE(bytesTotal->text() == QStringLiteral("2050"));
        QString awaiting = originalResourceString(
            QStringLiteral("IDS_AWAITING_ACCEPT"));
        awaiting.replace(QStringLiteral("%1"), otherAvatarName);
        REQUIRE(dialog.StatusText() == awaiting);
        REQUIRE(status->text() == awaiting);

        REQUIRE(dialog.windowTitle() == transferTitle(
            QStringLiteral("IDS_FILESEND_TITLE"), 0, fileName,
            otherAvatarName));

        dialog.Configure(CFileProgress::Direction::Receiving, fileName,
                         otherAvatarName, 8200);
        REQUIRE(dialog.TransferDirection()
                == CFileProgress::Direction::Receiving);
        REQUIRE(!dialog.IsSending());
        REQUIRE(xferred->text() == originalResourceString(
            QStringLiteral("IDS_BYTES_RECEIVED")));
        QString connecting = originalResourceString(
            QStringLiteral("IDS_FILE_CONNECTING"));
        connecting.replace(QStringLiteral("%1"), otherAvatarName);
        REQUIRE(dialog.StatusText() == connecting);
        REQUIRE(status->text() == connecting);

        CFileProgress placeholderDialog;
        placeholderDialog.Configure(
            CFileProgress::Direction::Sending,
            QStringLiteral("%3"), QStringLiteral("%2"), 1);
        QString placeholderTitle = originalResourceString(
            QStringLiteral("IDS_FILESEND_TITLE"));
        REQUIRE(ReplaceToken(
            placeholderTitle, QStringLiteral("%1"), QStringLiteral("0")));
        REQUIRE(ReplaceToken(
            placeholderTitle, QStringLiteral("%2"), QStringLiteral("%3")));
        REQUIRE(ReplaceToken(
            placeholderTitle, QStringLiteral("%3"), QStringLiteral("%2")));
        REQUIRE(placeholderTitle
                == QStringLiteral("0% - Sending %2 to %3"));
        REQUIRE(placeholderDialog.windowTitle() == placeholderTitle);
    }

    {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("timeout.bin"));
        QFile source(path);
        REQUIRE(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(source.write(sourceDerivedPayload(17)) == 17);
        source.close();

        CFileProgress sender;
        sender.Configure(CFileProgress::Direction::Sending,
                         QStringLiteral("timeout.bin"), otherAvatarName, 17);
        REQUIRE(sender.StartSending(path, unusedLocalPort()));
        auto* transferTimer = sender.findChild<QTimer*>();
        REQUIRE(transferTimer);
        REQUIRE(transferTimer->interval() == 120000);
        transferTimer->start(1);
        REQUIRE(waitUntil([&sender] { return !sender.IsActive(); }));
        REQUIRE(sender.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILETIMEOUT")));

        const quint16 closingPort = unusedLocalPort();
        sender.Configure(CFileProgress::Direction::Sending,
                         QStringLiteral("timeout.bin"), otherAvatarName, 17);
        REQUIRE(sender.StartSending(path, closingPort));
        QTcpSocket closingPeer;
        closingPeer.connectToHost(QHostAddress::LocalHost, closingPort);
        REQUIRE(waitUntil([&closingPeer] {
            return closingPeer.bytesAvailable() >= 17;
        }));
        REQUIRE(closingPeer.readAll() == sourceDerivedPayload(17));
        closingPeer.disconnectFromHost();
        REQUIRE(waitUntil([&closingPeer] {
            return closingPeer.state()
                == QAbstractSocket::UnconnectedState;
        }));
        REQUIRE(sender.IsActive());
        REQUIRE(sender.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILE_CONNECT")));
        auto* closeAcknowledgementTimer = sender.findChild<QTimer*>();
        REQUIRE(closeAcknowledgementTimer);
        REQUIRE(closeAcknowledgementTimer->isActive());
        REQUIRE(closeAcknowledgementTimer->interval() == 60000);
        closeAcknowledgementTimer->start(1);
        REQUIRE(waitUntil([&sender] { return !sender.IsActive(); }));
        REQUIRE(sender.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILETIMEOUT")));

        sender.Configure(CFileProgress::Direction::Sending,
                         QStringLiteral("timeout.bin"), otherAvatarName, 17);
        REQUIRE(sender.StartSending(path, unusedLocalPort()));
        sender.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        sender.accept();
        REQUIRE(sender.IsActive());
        REQUIRE(!sender.isVisible());
        REQUIRE(!sender.WasCanceled());
        sender.reject();
        REQUIRE(!sender.IsActive());
        REQUIRE(sender.WasCanceled());

        // Once a peer has accepted the first block, the same source timer is
        // the 60-second cumulative-ACK timeout rather than the accept timer.
        const quint16 acknowledgementPort = unusedLocalPort();
        sender.Configure(CFileProgress::Direction::Sending,
                         QStringLiteral("timeout.bin"), otherAvatarName, 17);
        REQUIRE(sender.StartSending(path, acknowledgementPort));
        QTcpSocket unacknowledgingPeer;
        unacknowledgingPeer.connectToHost(
            QHostAddress::LocalHost, acknowledgementPort);
        REQUIRE(waitUntil([&unacknowledgingPeer] {
            return unacknowledgingPeer.bytesAvailable() >= 17;
        }));
        REQUIRE(unacknowledgingPeer.readAll()
                == sourceDerivedPayload(17));
        auto* acknowledgementTimer = sender.findChild<QTimer*>();
        REQUIRE(acknowledgementTimer);
        REQUIRE(acknowledgementTimer->isActive());
        REQUIRE(acknowledgementTimer->interval() == 60000);
        acknowledgementTimer->start(1);
        REQUIRE(waitUntil([&sender] { return !sender.IsActive(); }));
        REQUIRE(sender.BytesTransferred() == 0);
        REQUIRE(sender.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILETIMEOUT")));
    }

    {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const QByteArray payload = sourceDerivedPayload(2050);
        const QString path = directory.filePath(QStringLiteral("send.bin"));
        QFile source(path);
        REQUIRE(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(source.write(payload) == payload.size());
        source.close();

        const quint16 port = unusedLocalPort();
        CFileProgress sender;
        sender.Configure(CFileProgress::Direction::Sending,
                         QStringLiteral("send.bin"), otherAvatarName,
                         payload.size());
        REQUIRE(sender.StartSending(path, port));
        REQUIRE(sender.IsActive());
        auto* listeningServer = sender.findChild<QTcpServer*>();
        REQUIRE(listeningServer);
        REQUIRE(listeningServer->listenBacklogSize() == 1);
        REQUIRE(listeningServer->maxPendingConnections() == 1);

        QTcpSocket peer;
        peer.connectToHost(QHostAddress::LocalHost, port);
        REQUIRE(waitUntil([&peer] {
            return peer.state() == QAbstractSocket::ConnectedState;
        }));
        REQUIRE(waitUntil([&peer] {
            return peer.bytesAvailable() >= 1024;
        }));
        auto* acknowledgementTimer = sender.findChild<QTimer*>();
        REQUIRE(acknowledgementTimer);
        REQUIRE(acknowledgementTimer->interval() == 60000);
        REQUIRE(sender.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILE_CONNECT")));

        const QByteArray first = peer.read(1024);
        REQUIRE(first == payload.first(1024));
        REQUIRE(peer.bytesAvailable() == 0);

        const QByteArray negativeAck = cumulativeAck(0xffffffffU);
        REQUIRE(peer.write(negativeAck) == negativeAck.size());
        peer.flush();
        REQUIRE(waitUntil([&sender] {
            return sender.BytesTransferred() == -1;
        }));
        REQUIRE(sender.IsActive());
        REQUIRE(peer.bytesAvailable() == 0);

        const QByteArray firstAck = cumulativeAck(1024);
        REQUIRE(peer.write(firstAck.constData(), 2) == 2);
        peer.flush();
        pumpEventsFor(75);
        REQUIRE(peer.bytesAvailable() == 0);
        REQUIRE(sender.BytesTransferred() == -1);

        REQUIRE(peer.write(firstAck.constData() + 2, 2) == 2);
        peer.flush();
        REQUIRE(waitUntil([&peer, &sender] {
            return peer.bytesAvailable() >= 1024
                && sender.BytesTransferred() >= 1024;
        }));
        const QByteArray second = peer.read(1024);
        REQUIRE(second == payload.mid(1024, 1024));

        const QByteArray secondAck = cumulativeAck(2048);
        REQUIRE(peer.write(secondAck) == secondAck.size());
        peer.flush();
        REQUIRE(waitUntil([&peer] {
            return peer.bytesAvailable() >= 2;
        }));
        REQUIRE(peer.read(2) == payload.last(2));

        const QByteArray finalAck = cumulativeAck(2050);
        REQUIRE(peer.write(finalAck) == finalAck.size());
        peer.flush();
        REQUIRE(waitUntil([&sender] {
            return !sender.IsActive();
        }));
        REQUIRE(sender.BytesTransferred() == payload.size());
        REQUIRE(sender.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILE_SENT")));
        REQUIRE(sender.windowTitle() == transferTitle(
            QStringLiteral("IDS_FILESEND_TITLE"), 100,
            QStringLiteral("send.bin"), otherAvatarName));
    }

    {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("large-ack.bin"));
        QFile source(path);
        REQUIRE(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(source.write("x", 1) == 1);
        source.close();

        const quint16 port = unusedLocalPort();
        CFileProgress sender;
        sender.Configure(CFileProgress::Direction::Sending,
                         QStringLiteral("large-ack.bin"), otherAvatarName, 1);
        REQUIRE(sender.StartSending(path, port));
        QTcpSocket peer;
        peer.connectToHost(QHostAddress::LocalHost, port);
        REQUIRE(waitUntil([&peer] { return peer.bytesAvailable() == 1; }));
        REQUIRE(peer.read(1) == QByteArray("x", 1));

        const QByteArray largestPositiveAck =
            cumulativeAck(0x7fffffffU);
        REQUIRE(peer.write(largestPositiveAck)
                == largestPositiveAck.size());
        peer.flush();
        REQUIRE(waitUntil([&sender] { return !sender.IsActive(); }));
        REQUIRE(sender.BytesTransferred() == INT_MAX);
        REQUIRE(sender.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILE_SENT")));
        REQUIRE(sender.windowTitle() == transferTitle(
            QStringLiteral("IDS_FILESEND_TITLE"), INT_MAX,
            QStringLiteral("large-ack.bin"), otherAvatarName));
    }

    {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("changed-size.bin"));
        QFile source(path);
        REQUIRE(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(source.write("x", 1) == 1);
        source.close();

        CFileProgress sender;
        sender.Configure(CFileProgress::Direction::Sending,
                         QStringLiteral("changed-size.bin"),
                         otherAvatarName, 1);
        REQUIRE(source.open(QIODevice::WriteOnly | QIODevice::Append));
        REQUIRE(source.write("y", 1) == 1);
        source.close();

        const quint16 port = unusedLocalPort();
        REQUIRE(sender.StartSending(path, port));
        REQUIRE(sender.BytesTotal() == 1);
        QTcpSocket peer;
        peer.connectToHost(QHostAddress::LocalHost, port);
        REQUIRE(waitUntil([&peer] { return peer.bytesAvailable() == 2; }));
        REQUIRE(peer.read(2) == QByteArray("xy", 2));
        const QByteArray actualBytesAck = cumulativeAck(2);
        REQUIRE(peer.write(actualBytesAck) == actualBytesAck.size());
        peer.flush();
        REQUIRE(waitUntil([&sender] { return !sender.IsActive(); }));
        REQUIRE(sender.BytesTotal() == 1);
        REQUIRE(sender.BytesTransferred() == 2);
        REQUIRE(sender.windowTitle() == transferTitle(
            QStringLiteral("IDS_FILESEND_TITLE"), 200,
            QStringLiteral("changed-size.bin"), otherAvatarName));
    }

    {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        constexpr qsizetype declaredSize = 8200;
        const QByteArray payload = sourceDerivedPayload(9000);
        const QString path = directory.filePath(
            QStringLiteral("receive.bin"));

        QTcpServer listener;
        REQUIRE(listener.listen(QHostAddress::LocalHost, 0));

        CFileProgress receiver;
        receiver.Configure(CFileProgress::Direction::Receiving,
                           QStringLiteral("receive.bin"), otherAvatarName,
                           declaredSize);
        REQUIRE(receiver.StartReceiving(
            path, 0x7f000001U, listener.serverPort(), declaredSize));
        REQUIRE(receiver.IsActive());
        REQUIRE(waitUntil([&listener] {
            return listener.hasPendingConnections();
        }));
        QTcpSocket* peer = listener.nextPendingConnection();
        REQUIRE(peer);
        REQUIRE(waitUntil([peer] {
            return peer->state() == QAbstractSocket::ConnectedState;
        }));

        QByteArray acknowledgementBytes;
        QList<quint32> acknowledgements;
        const auto collectAcknowledgements = [&] {
            acknowledgementBytes += peer->readAll();
            while (acknowledgementBytes.size() >= 4) {
                acknowledgements.append(
                    ackValue(acknowledgementBytes, 0));
                acknowledgementBytes.remove(0, 4);
            }
        };
        const auto receivedAtLeast = [&](quint32 amount) {
            collectAcknowledgements();
            return !acknowledgements.isEmpty()
                && acknowledgements.constLast() >= amount;
        };

        REQUIRE(peer->write(payload.constData(), 8192) == 8192);
        peer->flush();
        REQUIRE(waitUntil([&] {
            return receiver.BytesTransferred() >= 8192
                && receivedAtLeast(8192);
        }));
        REQUIRE(acknowledgements.constLast() == 8192);
        REQUIRE(receiver.IsActive());
        auto* bytesReceived = receiver.findChild<QLabel*>(
            QStringLiteral("IDC_BYTES_SENT"));
        REQUIRE(bytesReceived);
        bool displayedAmountOk = false;
        const qint64 displayedAmount =
            bytesReceived->text().toLongLong(&displayedAmountOk);
        REQUIRE(displayedAmountOk);
        REQUIRE(displayedAmount > 0);
        REQUIRE(displayedAmount <= 8192);

        REQUIRE(peer->write(payload.constData() + 8192,
                            payload.size() - 8192)
                == payload.size() - 8192);
        peer->flush();
        REQUIRE(waitUntil([&] {
            collectAcknowledgements();
            return !receiver.IsActive()
                && !acknowledgements.isEmpty()
                && acknowledgements.constLast() >= declaredSize;
        }));
        collectAcknowledgements();
        REQUIRE(acknowledgementBytes.isEmpty());
        quint32 previousAcknowledgement = 0;
        for (quint32 acknowledgement : acknowledgements) {
            REQUIRE(acknowledgement > previousAcknowledgement);
            REQUIRE(acknowledgement - previousAcknowledgement <= 8192);
            previousAcknowledgement = acknowledgement;
        }
        const quint32 finalAcknowledgement =
            acknowledgements.constLast();
        REQUIRE(finalAcknowledgement >= declaredSize);
        REQUIRE(finalAcknowledgement <= payload.size());
        REQUIRE(receiver.BytesTransferred() == finalAcknowledgement);
        REQUIRE(receiver.BytesTotal() == declaredSize);
        REQUIRE(receiver.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILE_RECEIVED")));
        REQUIRE(receiver.windowTitle() == transferTitle(
            QStringLiteral("IDS_FILEGET_TITLE"),
            static_cast<int>(
                static_cast<double>(finalAcknowledgement)
                / static_cast<double>(declaredSize) * 100.0),
            QStringLiteral("receive.bin"), otherAvatarName));
        auto* progress = receiver.findChild<QProgressBar*>(
            QStringLiteral("IDC_FILEPROGRESS"));
        REQUIRE(progress && progress->value() == 100);

        QFile received(path);
        REQUIRE(received.open(QIODevice::ReadOnly));
        REQUIRE(received.readAll() == payload.first(declaredSize));
    }

    {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());

        QTcpServer silentListener;
        REQUIRE(silentListener.listen(QHostAddress::LocalHost, 0));
        const QString timeoutPath = directory.filePath(
            QStringLiteral("receive-timeout.bin"));
        CFileProgress timeoutReceiver;
        timeoutReceiver.Configure(CFileProgress::Direction::Receiving,
                                  QStringLiteral("receive-timeout.bin"),
                                  otherAvatarName, 17);
        REQUIRE(timeoutReceiver.StartReceiving(
            timeoutPath, 0x7f000001U, silentListener.serverPort(), 17));
        REQUIRE(waitUntil([&] {
            return silentListener.hasPendingConnections()
                && timeoutReceiver.StatusText()
                    == originalResourceString(
                        QStringLiteral("IDS_FILE_CONNECT"));
        }));
        QTcpSocket* silentPeer = silentListener.nextPendingConnection();
        REQUIRE(silentPeer);
        auto* receiveTimer = timeoutReceiver.findChild<QTimer*>();
        REQUIRE(receiveTimer);
        REQUIRE(receiveTimer->isActive());
        REQUIRE(receiveTimer->interval() == 60000);
        receiveTimer->start(1);
        REQUIRE(waitUntil([&timeoutReceiver] {
            return !timeoutReceiver.IsActive();
        }));
        REQUIRE(timeoutReceiver.BytesTransferred() == 0);
        REQUIRE(timeoutReceiver.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILETIMEOUT")));

        QTcpServer cancelListener;
        REQUIRE(cancelListener.listen(QHostAddress::LocalHost, 0));
        const QString cancelPath = directory.filePath(
            QStringLiteral("receive-cancel.bin"));
        CFileProgress canceledReceiver;
        canceledReceiver.Configure(CFileProgress::Direction::Receiving,
                                   QStringLiteral("receive-cancel.bin"),
                                   otherAvatarName, 17);
        REQUIRE(canceledReceiver.StartReceiving(
            cancelPath, 0x7f000001U, cancelListener.serverPort(), 17));
        REQUIRE(waitUntil([&] {
            return cancelListener.hasPendingConnections()
                && canceledReceiver.StatusText()
                    == originalResourceString(
                        QStringLiteral("IDS_FILE_CONNECT"));
        }));
        QTcpSocket* canceledPeer = cancelListener.nextPendingConnection();
        REQUIRE(canceledPeer);
        canceledReceiver.reject();
        REQUIRE(!canceledReceiver.IsActive());
        REQUIRE(canceledReceiver.WasCanceled());
        REQUIRE(waitUntil([canceledPeer] {
            return canceledPeer->state()
                == QAbstractSocket::UnconnectedState;
        }));
    }

    {
        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("existing.bin"));
        const QByteArray original = sourceDerivedPayload(257);
        QFile existing(path);
        REQUIRE(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(existing.write(original) == original.size());
        existing.close();

        CFileProgress unknownSize;
        unknownSize.Configure(CFileProgress::Direction::Receiving,
                              QStringLiteral("existing.bin"),
                              otherAvatarName, -1);
        REQUIRE(!unknownSize.StartReceiving(
            path, 0x7f000001U, 7011, -1));
        REQUIRE(!unknownSize.IsActive());
        QFile unknownPreserved(path);
        REQUIRE(unknownPreserved.open(QIODevice::ReadOnly));
        REQUIRE(unknownPreserved.readAll() == original);
        unknownPreserved.close();

        CFileProgress receiver;
        receiver.Configure(CFileProgress::Direction::Receiving,
                           QStringLiteral("existing.bin"), otherAvatarName,
                           original.size());
        REQUIRE(receiver.StartReceiving(
            path, 0x7f000001U, unusedLocalPort(), original.size()));
        REQUIRE(waitUntil([&receiver] {
            return !receiver.IsActive()
                && receiver.StatusText() == originalResourceString(
                    QStringLiteral("IDS_CONNECTION_FAILED"));
        }));
        QFile preserved(path);
        REQUIRE(preserved.open(QIODevice::ReadOnly));
        REQUIRE(preserved.readAll() == original);

        QTcpServer openFailureListener;
        REQUIRE(openFailureListener.listen(QHostAddress::LocalHost, 0));
        QString expectedOpenFailure = originalResourceString(
            QStringLiteral("ID_ERR_SAVE"));
        REQUIRE(ReplaceToken(expectedOpenFailure, QStringLiteral("%1"),
                             directory.path()));
        bool sawOpenFailure = false;
        QTimer openFailureCloser;
        openFailureCloser.setInterval(1);
        QObject::connect(&openFailureCloser, &QTimer::timeout, [&] {
            auto* message = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            if (!message) return;
            REQUIRE(message->icon() == QMessageBox::NoIcon);
            REQUIRE(message->text() == expectedOpenFailure);
            REQUIRE(message->standardButtons() == QMessageBox::Ok);
            sawOpenFailure = true;
            openFailureCloser.stop();
            message->done(QMessageBox::Ok);
        });
        openFailureCloser.start();
        CFileProgress openFailure;
        openFailure.Configure(CFileProgress::Direction::Receiving,
                              QStringLiteral("cannot-open"),
                              otherAvatarName, 17);
        REQUIRE(openFailure.StartReceiving(
            directory.path(), 0x7f000001U,
            openFailureListener.serverPort(), 17));
        REQUIRE(waitUntil([&] {
            return sawOpenFailure && !openFailure.IsActive();
        }));
        REQUIRE(openFailure.StatusText() == originalResourceString(
            QStringLiteral("IDS_FILE_CONNECT")));
    }

    CChatDoc document;
    SetChatDoc(&document);
    delete document.m_proto;
    document.m_proto = nullptr;
    CapturingIrcProto protocol;
    document.m_proto = &protocol;
    protocol.m_doc = &document;
    protocol.m_strChannel = channel;
    protocol.m_strPrettyChannel = channel;
    protocol.SetConnectionStatus(CX_INCHANNEL);
    document.InitHistory();

    {
        CUserInfo dccSender(otherAvatarName, identity);
        const QString offeredFile =
            QStringLiteral("%1 file.bin").arg(otherAvatarName);
        const QString dccMessage =
            QStringLiteral("\001dCc sEnD %1\\@file.bin 2130706433 7011 17\001")
                .arg(otherAvatarName);
        QString expectedPrompt = originalResourceString(
            QStringLiteral("IDS_ACCEPT_FILE_MESG"));
        expectedPrompt.replace(QStringLiteral("%1"),
                               dccSender.GetScreenName());
        expectedPrompt.replace(QStringLiteral("%2"), offeredFile);
        QString fileSize = originalResourceString(
            QStringLiteral("IDS_FILESIZE_FORMAT"));
        fileSize.replace(QStringLiteral("%1"), QStringLiteral("17"));
        expectedPrompt.replace(QStringLiteral("%3"), fileSize);

        bool sawPrompt = false;
        QTimer::singleShot(0, [&] {
            auto* prompt = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            REQUIRE(prompt);
            REQUIRE(prompt->icon() == QMessageBox::NoIcon);
            REQUIRE(prompt->text() == expectedPrompt);
            sawPrompt = true;
            prompt->done(QMessageBox::No);
        });
        const qsizetype historyBeforeDcc = document.m_history.size();
        ProcessSay(&document, &dccSender, dccMessage,
                   MT_PRIVATEMSG | MT_PRVMSG);
        REQUIRE(sawPrompt);
        REQUIRE(document.m_history.size() == historyBeforeDcc);

        // DCC text is suppressed even when the receive call is gated.
        const bool savedAllowFileTransfer = theApp.m_bAllowFileTX;
        theApp.m_bAllowFileTX = false;
        ProcessSay(&document, &dccSender, dccMessage,
                   MT_PRIVATEMSG | MT_PRVMSG);
        theApp.m_bAllowFileTX = savedAllowFileTransfer;
        ProcessSay(&document, &dccSender,
                   QStringLiteral("\001DCCSEND x 1 1 1\001"),
                   MT_PRIVATEMSG | MT_PRVMSG);
        bool acceptedNonSourceWhitespace = false;
        QTimer::singleShot(0, [&] {
            if (auto* prompt = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget())) {
                acceptedNonSourceWhitespace = true;
                prompt->done(QMessageBox::No);
            }
        });
        ProcessSay(&document, &dccSender,
                   QStringLiteral("\001DCC\vSEND x 1 1 1\001"),
                   MT_PRIVATEMSG | MT_PRVMSG);
        pumpEventsFor(10);
        REQUIRE(!acceptedNonSourceWhitespace);
        dccSender.Ignore(true);
        ProcessSay(&document, &dccSender, dccMessage,
                   MT_PRIVATEMSG | MT_PRVMSG);
        dccSender.Ignore(false);

        const UCHAR savedFloodFlags = theApp.m_uFloodFlags;
        theApp.m_uFloodFlags = FLOOD_IGNORE;
        CUserInfo floodingSender(
            QStringLiteral("%1Flood").arg(otherAvatarName),
            QStringLiteral("Flood@%1").arg(server));
        for (int occurrence = 0;
             occurrence < theApp.m_uFloodCount - 1; ++occurrence) {
            REQUIRE(!floodingSender.IsFlooding());
        }
        REQUIRE(floodingSender.IsFlooding());
        floodingSender.Ignore(false);
        ProcessSay(&document, &floodingSender, dccMessage,
                   MT_PRIVATEMSG | MT_PRVMSG);
        protocol.DoIgnoreUser(&floodingSender, false, false);
        theApp.m_uFloodFlags = savedFloodFlags;
        REQUIRE(QApplication::activeModalWidget() == nullptr);
        REQUIRE(document.m_history.size() == historyBeforeDcc);
    }

    {
        CleanupFileProgressStore(TRUE);
        REQUIRE(fileProgressWindows().isEmpty());

        const bool savedAllowFileTransfer = theApp.m_bAllowFileTX;
        const QString savedTransferDirectory = theApp.m_strFileTXDir;
        theApp.m_bAllowFileTX = true;

        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const QString initialDirectory =
            directory.filePath(QStringLiteral("initial"));
        const QString chosenDirectory =
            directory.filePath(QStringLiteral("chosen"));
        REQUIRE(QDir().mkpath(initialDirectory));
        REQUIRE(QDir().mkpath(chosenDirectory));
        theApp.m_strFileTXDir = initialDirectory;

        CUserInfo sender(
            QStringLiteral("%1Receive").arg(otherAvatarName), identity);
        const QString offeredFile = QStringLiteral("offered receive.bin");
        const QString targetPath =
            QDir(chosenDirectory).filePath(QStringLiteral("accepted.bin"));
        QTcpServer fileSource;
        REQUIRE(fileSource.listen(QHostAddress::LocalHost, 0));
        const QString knownOffer =
            QStringLiteral("SEND offered\\@receive.bin 2130706433 %1 17\001")
                .arg(fileSource.serverPort());

        QString expectedPrompt = originalResourceString(
            QStringLiteral("IDS_ACCEPT_FILE_MESG"));
        expectedPrompt.replace(QStringLiteral("%1"),
                               sender.GetScreenName());
        expectedPrompt.replace(QStringLiteral("%2"), offeredFile);
        QString fileSize = originalResourceString(
            QStringLiteral("IDS_FILESIZE_FORMAT"));
        fileSize.replace(QStringLiteral("%1"), QStringLiteral("17"));
        expectedPrompt.replace(QStringLiteral("%3"), fileSize);

        bool sawPrompt = false;
        bool sawSaveDialog = false;
        QTimer::singleShot(0, [&] {
            auto* prompt = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            REQUIRE(prompt);
            REQUIRE(prompt->icon() == QMessageBox::NoIcon);
            REQUIRE(prompt->text() == expectedPrompt);
            REQUIRE(prompt->defaultButton());
            REQUIRE(prompt->standardButton(prompt->defaultButton())
                    == QMessageBox::Yes);
            sawPrompt = true;

            QTimer::singleShot(0, [&] {
                auto* fileDialog = qobject_cast<QFileDialog*>(
                    QApplication::activeModalWidget());
                REQUIRE(fileDialog);
                QString title = originalResourceString(
                    QStringLiteral("IDS_TITLE_FILEDLG_RCV"));
                title.replace(QStringLiteral("%1"),
                              sender.GetScreenName());
                REQUIRE(fileDialog->windowTitle() == title);
                REQUIRE(fileDialog->acceptMode()
                        == QFileDialog::AcceptSave);
                REQUIRE(fileDialog->fileMode() == QFileDialog::AnyFile);
                REQUIRE(!fileDialog->testOption(
                    QFileDialog::DontConfirmOverwrite));
                REQUIRE(fileDialog->nameFilters().value(0)
                        == originalResourceString(
                            QStringLiteral("IDS_ALL_FILES"))
                               .section(QLatin1Char('\n'), 0, 0));
                REQUIRE(QDir(fileDialog->directory().absolutePath())
                        .absolutePath()
                        == QDir(initialDirectory).absolutePath());
                REQUIRE(QFileInfo(
                            fileDialog->selectedFiles().value(0)).fileName()
                        == offeredFile);
                sawSaveDialog = true;
                fileDialog->setDirectory(chosenDirectory);
                fileDialog->selectFile(QFileInfo(targetPath).fileName());
                static_cast<QDialog*>(fileDialog)->accept();
            });
            prompt->done(QMessageBox::Yes);
        });

        ChatReceiveFile(&sender, knownOffer);
        REQUIRE(sawPrompt);
        REQUIRE(sawSaveDialog);
        REQUIRE(theApp.m_strFileTXDir
                == QFileInfo(targetPath).absolutePath());
        QList<CFileProgress*> progressWindows = fileProgressWindows();
        REQUIRE(progressWindows.size() == 1);
        CFileProgress* progress = progressWindows.first();
        REQUIRE(progress->TransferDirection()
                == CFileProgress::Direction::Receiving);
        REQUIRE(progress->BytesTotal() == 17);
        REQUIRE(waitUntil([&] {
            return fileSource.hasPendingConnections()
                && progress->StatusText()
                    == originalResourceString(
                        QStringLiteral("IDS_FILE_CONNECT"));
        }));
        QTcpSocket* sourcePeer = fileSource.nextPendingConnection();
        REQUIRE(sourcePeer);

        // Application shutdown uses the inclusive cleanup path even while a
        // visible transfer is active; destruction must cancel its socket and
        // remove the modeless progress window.
        QPointer<CFileProgress> progressGuard(progress);
        CleanupFileProgressStore(TRUE);
        REQUIRE(progressGuard.isNull());
        REQUIRE(fileProgressWindows().isEmpty());
        REQUIRE(waitUntil([sourcePeer] {
            return sourcePeer->state()
                == QAbstractSocket::UnconnectedState;
        }));

        theApp.m_strFileTXDir = initialDirectory;
        bool sawCanceledSaveDialog = false;
        QTimer::singleShot(0, [&] {
            auto* prompt = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            REQUIRE(prompt);
            QTimer::singleShot(0, [&] {
                auto* fileDialog = qobject_cast<QFileDialog*>(
                    QApplication::activeModalWidget());
                REQUIRE(fileDialog);
                sawCanceledSaveDialog = true;
                static_cast<QDialog*>(fileDialog)->reject();
            });
            prompt->done(QMessageBox::Yes);
        });
        ChatReceiveFile(&sender, knownOffer);
        REQUIRE(sawCanceledSaveDialog);
        REQUIRE(theApp.m_strFileTXDir == initialDirectory);
        REQUIRE(fileProgressWindows().isEmpty());
        REQUIRE(!fileSource.hasPendingConnections());

        const QString unknownTarget =
            QDir(chosenDirectory).filePath(QStringLiteral("unknown.bin"));
        QFile::remove(unknownTarget);
        QTcpServer unknownSource;
        REQUIRE(unknownSource.listen(QHostAddress::LocalHost, 0));
        const QString unknownOffer =
            QStringLiteral("SEND unknown\\@size.bin 2130706433 %1\001")
                .arg(unknownSource.serverPort());
        const QString unknownFile = QStringLiteral("unknown size.bin");
        QString unknownPrompt = originalResourceString(
            QStringLiteral("IDS_ACCEPT_FILE_MESG"));
        unknownPrompt.replace(QStringLiteral("%1"),
                              sender.GetScreenName());
        unknownPrompt.replace(QStringLiteral("%2"), unknownFile);
        unknownPrompt.replace(QStringLiteral("%3"),
            originalResourceString(QStringLiteral("IDS_FILESIZE_UNKNOWN")));

        theApp.m_strFileTXDir = initialDirectory;
        bool sawUnknownPrompt = false;
        bool sawUnknownSaveDialog = false;
        QTimer::singleShot(0, [&] {
            auto* prompt = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
            REQUIRE(prompt);
            REQUIRE(prompt->icon() == QMessageBox::NoIcon);
            REQUIRE(prompt->text() == unknownPrompt);
            sawUnknownPrompt = true;
            QTimer::singleShot(0, [&] {
                auto* fileDialog = qobject_cast<QFileDialog*>(
                    QApplication::activeModalWidget());
                REQUIRE(fileDialog);
                REQUIRE(QDir(fileDialog->directory().absolutePath())
                        .absolutePath()
                        == QDir(initialDirectory).absolutePath());
                REQUIRE(QFileInfo(
                            fileDialog->selectedFiles().value(0)).fileName()
                        == unknownFile);
                sawUnknownSaveDialog = true;
                fileDialog->setDirectory(chosenDirectory);
                fileDialog->selectFile(
                    QFileInfo(unknownTarget).fileName());
                static_cast<QDialog*>(fileDialog)->accept();
            });
            prompt->done(QMessageBox::Yes);
        });

        ChatReceiveFile(&sender, unknownOffer);
        REQUIRE(sawUnknownPrompt);
        REQUIRE(sawUnknownSaveDialog);
        REQUIRE(theApp.m_strFileTXDir
                == QFileInfo(unknownTarget).absolutePath());
        pumpEventsFor(50);
        REQUIRE(fileProgressWindows().isEmpty());
        REQUIRE(!unknownSource.hasPendingConnections());
        REQUIRE(!QFileInfo::exists(unknownTarget));

        // The four-dialog cap counts currently nested confirmation dialogs,
        // not completed progress windows. A fifth re-entrant offer must
        // return without replacing the fourth active prompt.
        const QString limitedOffer =
            QStringLiteral("SEND limit.bin 2130706433 7011 1\001");
        int promptsSeen = 0;
        int blockedFifthOffer = 0;
        std::function<void(int)> invokeNestedOffer;
        invokeNestedOffer = [&](int depth) {
            QTimer::singleShot(0, [&, depth] {
                auto* prompt = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget());
                REQUIRE(prompt);
                REQUIRE(prompt->icon() == QMessageBox::NoIcon);
                ++promptsSeen;
                if (depth < 4) {
                    invokeNestedOffer(depth + 1);
                } else {
                    QWidget* fourthPrompt = prompt;
                    ChatReceiveFile(&sender, limitedOffer);
                    REQUIRE(QApplication::activeModalWidget()
                            == fourthPrompt);
                    ++blockedFifthOffer;
                }
                prompt->done(QMessageBox::No);
            });
            ChatReceiveFile(&sender, limitedOffer);
        };
        invokeNestedOffer(1);
        REQUIRE(promptsSeen == 4);
        REQUIRE(blockedFifthOffer == 1);
        REQUIRE(QApplication::activeModalWidget() == nullptr);
        REQUIRE(fileProgressWindows().isEmpty());

        theApp.m_bAllowFileTX = savedAllowFileTransfer;
        theApp.m_strFileTXDir = savedTransferDirectory;
    }

    {
        QTcpServer ircListener;
        REQUIRE(ircListener.listen(QHostAddress::LocalHost, 0));
        auto* ircClient = new QTcpSocket;
        ircClient->connectToHost(
            QHostAddress::LocalHost, ircListener.serverPort());
        REQUIRE(waitUntil([&] {
            return ircClient->state() == QAbstractSocket::ConnectedState
                && ircListener.hasPendingConnections();
        }));
        QTcpSocket* ircPeer = ircListener.nextPendingConnection();
        REQUIRE(ircPeer);
        serverConn.AdoptSocket(ircClient);
        REQUIRE(static_cast<quint32>(GetMyIP()) == 0x7f000001U);

        QTemporaryDir directory;
        REQUIRE(directory.isValid());
        const QString path =
            directory.filePath(QStringLiteral("dcc offer.bin"));
        const QByteArray payload = sourceDerivedPayload(33);
        QFile source(path);
        REQUIRE(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(source.write(payload) == payload.size());
        source.close();

        CUserInfo target(
            QStringLiteral("%1Dcc").arg(otherAvatarName), identity);
        const QString currentDirectory = QDir::currentPath();
        const auto sendOffer = [&](quint16 expectedPort) {
            bool sawDialog = false;
            QTimer::singleShot(0, [&] {
                auto* fileDialog = qobject_cast<QFileDialog*>(
                    QApplication::activeModalWidget());
                REQUIRE(fileDialog);
                QString title = originalResourceString(
                    QStringLiteral("IDS_TITLE_FILEDLG_SEND"));
                title.replace(QStringLiteral("%1"),
                              target.GetScreenName());
                REQUIRE(fileDialog->windowTitle() == title);
                REQUIRE(fileDialog->fileMode() == QFileDialog::ExistingFile);
                REQUIRE(fileDialog->nameFilters().value(0)
                        == originalResourceString(
                            QStringLiteral("IDS_ALL_FILES"))
                               .section(QLatin1Char('\n'), 0, 0));
                sawDialog = true;
                fileDialog->selectFile(path);
                static_cast<QDialog*>(fileDialog)->accept();
            });

            const qsizetype sentBefore = protocol.sent.size();
            protocol.ChatSendFile(&target);
            REQUIRE(sawDialog);
            REQUIRE(QDir::currentPath() == currentDirectory);
            REQUIRE(protocol.sent.size() == sentBefore + 1);
            REQUIRE(protocol.sent.last()
                    == QStringLiteral(
                           "PRIVMSG %1 :\001DCC SEND dcc\\@offer.bin "
                           "2130706433 %2 33\001\r\n")
                           .arg(target.GetName())
                           .arg(expectedPort));

            CFileProgress* progress = nullptr;
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                if (auto* candidate =
                        dynamic_cast<CFileProgress*>(widget)) {
                    progress = candidate;
                    break;
                }
            }
            REQUIRE(progress);
            REQUIRE(progress->IsActive());
            REQUIRE(progress->IsSending());
            REQUIRE(progress->BytesTotal() == payload.size());
            QString awaiting = originalResourceString(
                QStringLiteral("IDS_AWAITING_ACCEPT"));
            awaiting.replace(QStringLiteral("%1"),
                             target.GetScreenName());
            REQUIRE(progress->StatusText() == awaiting);
            progress->reject();
            REQUIRE(progress->WasCanceled());
            CleanupFileProgressStore(FALSE);
        };

        sendOffer(7011);
        sendOffer(7012);
        serverConn.AdoptSocket(new QTcpSocket);
        ircPeer->deleteLater();
    }

    auto* self = new CUserInfo(QLatin1Char('@') + selfNick,
                               QString::fromUtf8(GetMyUserName()));
    AddAndExecute(new JoinEntry(self), &document);
    REQUIRE(g_puiSelf == self);
    REQUIRE(self->IsOperator());

    theApp.m_iGreetingType = AGT_SAY;
    auto* other = new CUserInfo(otherAvatarName, identity);
    const qsizetype sentBeforeGreeting = protocol.sent.size();
    AddAndExecute(new JoinEntry(other, FALSE), &document);
    REQUIRE(protocol.sent.size() == sentBeforeGreeting + 1);
    QString greeting = originalResourceString(QStringLiteral("IDS_DEFAULTGREETING"));
    greeting.replace(QStringLiteral("%1"), other->GetScreenName());
    greeting.replace(QStringLiteral("%2"), channel);
    REQUIRE(protocol.sent.last().startsWith(
        QStringLiteral("PRIVMSG %1 :").arg(channel)));
    REQUIRE(protocol.sent.last().endsWith(greeting + QStringLiteral("\r\n")));

    theApp.m_iGreetingType = AGT_NONE;
    for (int occurrence = 0; occurrence < theApp.m_uFloodCount - 1;
         ++occurrence) {
        REQUIRE(!other->IsFlooding());
    }
    REQUIRE(other->IsFlooding());
    REQUIRE(other->Ignored());
    REQUIRE(IsIgnored(identity));

    protocol.DoIgnoreUser(other, false, false);
    REQUIRE(!other->Ignored());
    REQUIRE(!IsIgnored(identity));

    other->SetFullName(QString());
    const qsizetype sentBeforeWhois = protocol.sent.size();
    protocol.DoIgnoreUser(other, true, true);
    REQUIRE(protocol.sent.size() == sentBeforeWhois + 1);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("WHOIS %1\r\n").arg(otherAvatarName));
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctWhoIs) != nullptr);
    protocol.m_pSock->ProcessMessage(
        QStringLiteral("311 %1 %2 %2 %3 :%4")
            .arg(selfNick, otherAvatarName, server,
                 originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME"))));
    REQUIRE(other->Ignored());
    REQUIRE(IsIgnored(identity));
    protocol.m_pSock->ProcessMessage(
        QStringLiteral("318 %1 %2").arg(selfNick, otherAvatarName));
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctWhoIs) == nullptr);

    protocol.DoIgnoreUser(other, false, false);
    REQUIRE(protocol.m_pSock->m_queries.FindQuery(ctWhoIs) != nullptr);
    protocol.m_pSock->ProcessMessage(
        QStringLiteral("311 %1 %2 %2 %3 :%4")
            .arg(selfNick, otherAvatarName, server,
                 originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME"))));
    protocol.m_pSock->ProcessMessage(
        QStringLiteral("318 %1 %2").arg(selfNick, otherAvatarName));
    REQUIRE(!other->Ignored());
    REQUIRE(!IsIgnored(identity));
    const QString awayMessage = originalResourceString(
        QStringLiteral("IDS_DFLTAWAYMSG"));
    protocol.ChatSetAway(true, awayMessage, nullptr, false);
    REQUIRE(self->CheckFlag(UF_AWAY));
    REQUIRE(protocol.sent.last()
            == QStringLiteral("PRIVMSG %1 :\001AWAY %2\001\r\n")
                   .arg(channel, awayMessage));
    protocol.ChatSetAway(false, QString(), nullptr, false);
    REQUIRE(!self->CheckFlag(UF_AWAY));
    REQUIRE(protocol.sent.last()
            == QStringLiteral("PRIVMSG %1 :\001AWAY\001\r\n").arg(channel));

    theApp.m_uFloodFlags = 0;
    const qsizetype historyBeforeReaction = document.m_history.size();
    ProcessSay(&document, other, QStringLiteral("<Chr>"), MT_CHANNELSEND);
    REQUIRE(document.m_history.size() == historyBeforeReaction + 1);

    const qsizetype sentBeforeVersionRequest = protocol.sent.size();
    protocol.ChatGetVersion(other);
    REQUIRE(protocol.sent.size() == sentBeforeVersionRequest + 1);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("PRIVMSG %1 :\001VERSION\001\r\n")
                   .arg(otherAvatarName));
    REQUIRE(other->IsRequestInfo(RF_VERSION));
    protocol.ReplyVersion(other);
    const QString versionReply = wirePayload(protocol.sent.last());
    REQUIRE(versionReply.startsWith(QStringLiteral("\001VERSION ")));
    OnTextMsg(&document, otherAvatarName, identity, versionReply,
              MT_PRIVATEMSG | MT_NOTICE);
    REQUIRE(!other->IsRequestInfo(RF_VERSION));

    const qsizetype sentBeforePing = protocol.sent.size();
    protocol.ChatPingUser(other);
    REQUIRE(protocol.sent.size() == sentBeforePing + 1);
    REQUIRE(other->CheckFlag(UF_REQUESTPING));
    QString pingReply = wirePayload(protocol.sent.last());
    REQUIRE(pingReply.startsWith(QStringLiteral("\001PING ")));
    OnTextMsg(&document, otherAvatarName, identity, pingReply,
              MT_PRIVATEMSG | MT_NOTICE);
    REQUIRE(!other->CheckFlag(UF_REQUESTPING));

    const qsizetype sentBeforeClientInfo = protocol.sent.size();
    OnTextMsg(&document, otherAvatarName, identity,
              QStringLiteral("\001CLIENTINFO\001"),
              MT_PRIVATEMSG | MT_PRVMSG);
    REQUIRE(protocol.sent.size() == sentBeforeClientInfo + 1);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("NOTICE %1 :\001CLIENTINFO ACTION AWAY CLIENTINFO DCC EMAIL NETMEET PING SOUND TIME USERINFO URL VERSION\001\r\n")
                   .arg(otherAvatarName));

    protocol.ReplyEmail(other);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("NOTICE %1 :\001EMAIL %2\001\r\n")
                   .arg(otherAvatarName, QString::fromUtf8(GetMyEmail())));

    const QString sourceHomePage = originalResourceString(
        QStringLiteral("IDS_URL_MSPREFIX"));
    REQUIRE(!sourceHomePage.isEmpty());
    const QString savedHomePage = QString::fromUtf8(GetMyHomePage());
    SetMyHomePage(sourceHomePage);
    protocol.ReplyHomePage(other);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("NOTICE %1 :\001URL %2\001\r\n")
                   .arg(otherAvatarName, sourceHomePage));

    const qsizetype sentBeforeHomePageRequest = protocol.sent.size();
    protocol.ChatGetHomePage(other);
    REQUIRE(protocol.sent.size() == sentBeforeHomePageRequest + 1);
    REQUIRE(protocol.sent.last()
            == QStringLiteral("PRIVMSG %1 :\001URL\001\r\n")
                   .arg(otherAvatarName));
    REQUIRE(other->IsRequestInfo(RF_HOMEPAGE));
    ProcessSay(&document, other,
               QStringLiteral("\001URL %1\001").arg(sourceHomePage),
               MT_PRIVATEMSG | MT_NOTICE);
    REQUIRE(!other->IsRequestInfo(RF_HOMEPAGE));
    ProcessSay(&document, other,
               QStringLiteral("\001URL %1\001").arg(sourceHomePage),
               MT_PRIVATEMSG | MT_NOTICE);
    REQUIRE(!other->IsRequestInfo(RF_HOMEPAGE));
    SetMyHomePage(savedHomePage);

    for (CUserInfo* pui : document.m_allChannelPuis) {
        if (pui) {
            if (CAvatarX* avatar = GetAvatar(pui->GetAvatarID())) {
                if (avatar->m_userInfo == pui) avatar->m_userInfo = nullptr;
            }
        }
    }
    g_puiSelf = nullptr;
    g_mapNickToPtr->clear();
    document.m_puiSelf = nullptr;
    SetChatDoc(nullptr);
    document.m_proto = nullptr;
    CUnitPanelPage::DestroyFonts();
    DestroyAvatars();
    return 0;
}
