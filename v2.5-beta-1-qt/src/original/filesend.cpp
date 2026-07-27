// Ported from v2.5-beta-1-modern/filesend.cpp.
// Qt replaces the Win32 file, socket, worker-thread, and modeless-dialog
// mechanics while retaining the source protocol and UI state transitions.

#include "filesend.h"

#include "chat.h"
#include "chatprot.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "originalassets.h"
#include "protsupp.h"
#include "userinfo.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHostAddress>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <climits>
#include <cstring>
#include <limits>

namespace {
constexpr int kFirstDccPort = 7011;
constexpr int kAcceptTimeoutMs = 120000;
constexpr int kReceiveTimeoutMs = 60000;
constexpr int kProgressPostLimitMs = 100;
constexpr qint64 kSendBlockSize = 1024;
constexpr qint64 kReceiveBlockSize = 8192;
constexpr int kSocketBufferSize = 4096;
constexpr int kSendSocketBufferSize = 1024;
constexpr int kReceiveDialogLimit = 4;
constexpr qsizetype kSourceMaxTokenBytes = 200;

quint16 g_nextDccPort = kFirstDccPort;
int g_receiveDialogCount = 0;
QList<QPointer<CFileProgress>> g_fileProgressStore;

class DialogUnitMapper {
public:
    explicit DialogUnitMapper(const QFont& font)
    {
        const QFontMetrics metrics(font);
        const QString alphabet = QStringLiteral(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        m_baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
        m_baseY = qMax(1, metrics.height());
    }

    int x(int dlu) const { return (dlu * m_baseX + 2) / 4; }
    int y(int dlu) const { return (dlu * m_baseY + 4) / 8; }
    QRect rect(const OriginalDialogControl& control) const
    {
        return {x(control.x), y(control.y),
                x(control.width), y(control.height)};
    }

private:
    int m_baseX = 1;
    int m_baseY = 1;
};

const OriginalDialogControl* findControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

void placeControl(QWidget* widget, const OriginalDialogResource& dialog,
                  const DialogUnitMapper& mapper,
                  const QString& identifier, int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = findControl(
            dialog, identifier, occurrence)) {
        widget->setGeometry(mapper.rect(*control));
        widget->setVisible(control->visible);
    }
}

QFont resourceFont(const OriginalDialogResource& dialog)
{
    QFont font(dialog.fontFamily);
    if (dialog.fontPointSize > 0) font.setPointSize(dialog.fontPointSize);
    return font;
}

QString sourceAllFilesFilter()
{
    const QString source =
        originalResourceString(QStringLiteral("IDS_ALL_FILES"));
    const QString label = source.section(QLatin1Char('\n'), 0, 0);
    return label.isEmpty() ? source : label;
}

QString sourceFileSizeText(const DCCSendOffer& offer)
{
    if (!offer.fileSizeKnown) {
        return originalResourceString(QStringLiteral("IDS_FILESIZE_UNKNOWN"));
    }
    QString result =
        originalResourceString(QStringLiteral("IDS_FILESIZE_FORMAT"));
    ReplaceToken(result, QStringLiteral("%1"),
                 QString::number(offer.fileSize));
    return result;
}

bool sourceAsciiCaseInsensitiveEqual(QStringView left, QStringView right)
{
    if (left.size() != right.size()) return false;
    for (qsizetype index = 0; index < left.size(); ++index) {
        ushort leftCharacter = left.at(index).unicode();
        ushort rightCharacter = right.at(index).unicode();
        if (leftCharacter >= 'A' && leftCharacter <= 'Z')
            leftCharacter += 'a' - 'A';
        if (rightCharacter >= 'A' && rightCharacter <= 'Z')
            rightCharacter += 'a' - 'A';
        if (leftCharacter != rightCharacter) return false;
    }
    return true;
}

QByteArray sourceCodePageBytes(QStringView text)
{
    QByteArray bytes;
    if (!bWideToCodePage(text, GetACP(), &bytes))
        bytes = text.toString().toLatin1();
    return bytes;
}

QString sourceCodePageText(const QByteArray& bytes)
{
    QString text;
    if (!bCodePageToWide(bytes, GetACP(), &text))
        text = QString::fromLatin1(bytes);
    return text;
}

bool sourceSpace(QChar character)
{
    return character == QLatin1Char(' ')
        || character == QLatin1Char('\t')
        || character == QLatin1Char('\n')
        || character == QLatin1Char('\r');
}

qint64 sourceLongPrefix(const QString& token)
{
    qsizetype index = 0;
    // GetToken2 recognizes only the source's four whitespace bytes, while
    // the subsequent MS-CRT atol/atoi conversion additionally skips VT/FF.
    while (index < token.size()
           && (sourceSpace(token.at(index))
               || token.at(index) == QLatin1Char('\v')
               || token.at(index) == QLatin1Char('\f'))) {
        ++index;
    }
    bool negative = false;
    if (index < token.size()
        && (token.at(index) == QLatin1Char('+')
            || token.at(index) == QLatin1Char('-'))) {
        negative = token.at(index) == QLatin1Char('-');
        ++index;
    }

    bool anyDigit = false;
    quint64 magnitude = 0;
    while (index < token.size()) {
        const ushort character = token.at(index).unicode();
        if (character < '0' || character > '9') break;
        anyDigit = true;
        const quint64 digit = character - '0';
        if (magnitude
            > (static_cast<quint64>(INT_MAX) + (negative ? 1u : 0u)
               - digit) / 10u) {
            magnitude = static_cast<quint64>(INT_MAX)
                + (negative ? 1u : 0u);
            while (++index < token.size()
                   && token.at(index) >= QLatin1Char('0')
                   && token.at(index) <= QLatin1Char('9')) {
            }
            break;
        }
        magnitude = magnitude * 10u + digit;
        ++index;
    }
    if (!anyDigit) return 0;
    if (negative) {
        if (magnitude >= static_cast<quint64>(INT_MAX) + 1u)
            return INT_MIN;
        return -static_cast<qint64>(magnitude);
    }
    return static_cast<qint64>(qMin<quint64>(
        magnitude, static_cast<quint64>(INT_MAX)));
}

bool takeSourceToken(const QString& source, qsizetype* offset,
                     QString* token)
{
    if (!offset || !token) return false;
    qsizetype begin = *offset;
    while (begin < source.size() && sourceSpace(source.at(begin))) ++begin;
    if (begin >= source.size()) {
        *offset = begin;
        token->clear();
        return false;
    }
    qsizetype end = begin;
    while (end < source.size() && !sourceSpace(source.at(end))) ++end;
    // GetToken2 advances over the complete token but copies no more than
    // MAX_TOKEN - 1 bytes into its static result buffer.
    QByteArray sourceBytes = sourceCodePageBytes(
        QStringView(source).mid(begin, end - begin));
    if (sourceBytes.size() > kSourceMaxTokenBytes)
        sourceBytes.truncate(kSourceMaxTokenBytes);
    *token = sourceCodePageText(sourceBytes);
    *offset = end;
    return true;
}

QWidget* dialogParent()
{
    return theApp.m_pMainWnd.data();
}

void showSourceOkMessage(const QString& text)
{
    QMessageBox message(
        QMessageBox::NoIcon,
        originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
        text, QMessageBox::Ok, dialogParent());
    message.exec();
}

void addToFileProgressStore(CFileProgress* progress)
{
    if (!progress) return;
    CleanupFileProgressStore(FALSE);
    g_fileProgressStore.append(progress);
}

class ReceiveDialogCounter {
public:
    ReceiveDialogCounter() { ++g_receiveDialogCount; }
    ~ReceiveDialogCounter() { --g_receiveDialogCount; }
};
}

QString CTCPQuoteString(const QString& value)
{
    const qsizetype nul = value.indexOf(QChar());
    const QByteArray source =
        (nul < 0 ? value : value.left(nul)).toUtf8();
    QByteArray result;
    result.reserve(source.size() * 2);
    for (char character : source) {
        switch (character) {
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case ' ':
            result += "\\@";
            break;
        case '\001':
            result += "\\1";
            break;
        case '\\':
            result += "\\\\";
            break;
        default:
            result += character;
            break;
        }
    }
    return QString::fromUtf8(result);
}

BOOL CTCPUnQuoteString(QString* value)
{
    if (!value) return FALSE;
    const qsizetype nul = value->indexOf(QChar());
    if (nul >= 0) value->truncate(nul);
    const QByteArray source = value->toUtf8();
    bool quotedCharacter = false;
    bool quotedString = true;

    for (qsizetype index = 0; index < source.size(); ++index) {
        if (source.at(index) != '\\') continue;
        if (index + 1 >= source.size()) {
            quotedString = false;
            break;
        }
        switch (source.at(index + 1)) {
        case '1':
        case '@':
        case 'n':
        case 'r':
            quotedCharacter = true;
            ++index;
            break;
        case '\\':
            ++index;
            break;
        default:
            quotedString = false;
            break;
        }
        if (!quotedString) break;
    }

    // This slightly surprising source behavior means a string containing
    // only doubled backslashes is left unchanged.
    if (!quotedCharacter || !quotedString) return FALSE;

    QByteArray result;
    result.reserve(source.size());
    for (qsizetype index = 0; index < source.size(); ++index) {
        const char character = source.at(index);
        if (character != '\\') {
            result += character;
            continue;
        }
        const char quoted = source.at(++index);
        switch (quoted) {
        case 'n':
            result += '\n';
            break;
        case 'r':
            result += '\r';
            break;
        case '1':
            result += '\001';
            break;
        case '@':
            result += ' ';
            break;
        case '\\':
            result += '\\';
            break;
        default:
            return FALSE;
        }
    }
    *value = QString::fromUtf8(result);
    return TRUE;
}

QString BuildDCCSendOffer(const QString& fileName, quint32 hostAddress,
                          quint16 port, qint64 fileSize)
{
    return QStringLiteral("\001DCC SEND %1 %2 %3 %4\001")
        .arg(CTCPQuoteString(fileName),
             QString::number(hostAddress),
             QString::number(port),
             QString::number(fileSize));
}

BOOL ParseDCCSendOffer(const QString& arguments, DCCSendOffer* offer)
{
    if (!offer) return FALSE;
    const qsizetype nul = arguments.indexOf(QChar());
    const QString source =
        nul < 0 ? arguments : arguments.left(nul);
    qsizetype offset = 0;
    QString token;
    if (!takeSourceToken(source, &offset, &token)
        || !sourceAsciiCaseInsensitiveEqual(
            token, QStringLiteral("SEND"))) {
        return FALSE;
    }

    DCCSendOffer parsed;
    if (!takeSourceToken(source, &offset, &parsed.fileName)) return FALSE;
    CTCPUnQuoteString(&parsed.fileName);

    if (!takeSourceToken(source, &offset, &token)) return FALSE;
    parsed.hostAddress =
        static_cast<quint32>(static_cast<qint32>(sourceLongPrefix(token)));

    if (!takeSourceToken(source, &offset, &token)) return FALSE;
    parsed.port =
        static_cast<quint16>(static_cast<qint16>(sourceLongPrefix(token)));

    if (!takeSourceToken(source, &offset, &token)) {
        parsed.fileSize = -1;
        parsed.fileSizeKnown = FALSE;
    } else {
        parsed.fileSize = sourceLongPrefix(token);
        if (parsed.fileSize < 1) return FALSE;
        parsed.fileSizeKnown = TRUE;
    }

    *offer = parsed;
    return TRUE;
}

struct CFileProgress::TransferState {
    Direction direction = Direction::Sending;
    QString path;
    QString fileName;
    QString otherGuy;
    qint64 bytesTotal = 0;
    qint64 bytesTransferred = 0;
    qint64 rawBytesReceived = 0;
    qint64 totalSent = 0;
    bool active = false;
    bool connected = false;
    bool canceled = false;
    bool waitingForAcknowledgement = false;
    bool closeGracefully = false;
    std::unique_ptr<QFile> file;
    QTcpServer* server = nullptr;
    QTcpSocket* socket = nullptr;
    QTimer* timer = nullptr;
    QByteArray sendBlock;
    qsizetype sendOffset = 0;
    QByteArray acknowledgementBytes;
    QElapsedTimer lastProgressPost;
};

CFileProgress::CFileProgress(QWidget* parent)
    : QDialog(parent)
    , m_state(std::make_unique<TransferState>())
{
    buildDialog();
    m_state->timer = new QTimer(this);
    m_state->timer->setSingleShot(true);
    connect(m_state->timer, &QTimer::timeout,
            this, &CFileProgress::handleTimeout);
}

CFileProgress::~CFileProgress()
{
    cancelTransfer();
}

void CFileProgress::buildDialog()
{
    const QString resource = QStringLiteral("IDD_FILE_TRANSFER");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setObjectName(resource);
    setFont(font);
    setWindowTitle(dialog.caption);
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowMinimizeButtonHint
                   | Qt::MSWindowsFixedSizeDialogHint);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    placeControl(m_progress, dialog, mapper,
                 QStringLiteral("IDC_FILEPROGRESS"));

    m_xferredLabel = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_STATIC_NXFERRED")), this);
    placeControl(m_xferredLabel, dialog, mapper,
                 QStringLiteral("IDC_STATIC_NXFERRED"));

    auto* totalLabel = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_STATIC"), 0), this);
    placeControl(totalLabel, dialog, mapper, QStringLiteral("IDC_STATIC"), 0);

    auto* statusLabel = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_STATIC"), 1), this);
    placeControl(statusLabel, dialog, mapper, QStringLiteral("IDC_STATIC"), 1);

    m_bytesSent = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_BYTES_SENT")), this);
    placeControl(m_bytesSent, dialog, mapper,
                 QStringLiteral("IDC_BYTES_SENT"));

    m_bytesTotal = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_BYTES_TOTAL")), this);
    placeControl(m_bytesTotal, dialog, mapper,
                 QStringLiteral("IDC_BYTES_TOTAL"));

    m_status = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_CX_STATUS")), this);
    m_status->setWordWrap(true);
    placeControl(m_status, dialog, mapper, QStringLiteral("IDC_CX_STATUS"));

    m_ok = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDOK")), this);
    m_ok->setDefault(true);
    placeControl(m_ok, dialog, mapper, QStringLiteral("IDOK"));

    m_cancel = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDCANCEL")), this);
    placeControl(m_cancel, dialog, mapper, QStringLiteral("IDCANCEL"));

    connect(m_ok, &QPushButton::clicked, this, &CFileProgress::accept);
    connect(m_cancel, &QPushButton::clicked, this, &CFileProgress::reject);
}

void CFileProgress::Configure(Direction direction, const QString& fileName,
                              const QString& otherGuy, qint64 bytesTotal)
{
    if (m_state->active) cancelTransfer();
    m_state->direction = direction;
    m_state->fileName = fileName;
    m_state->otherGuy = otherGuy;
    m_state->bytesTotal = bytesTotal;
    m_state->bytesTransferred = 0;
    m_state->rawBytesReceived = 0;
    m_state->totalSent = 0;
    m_state->canceled = false;
    m_state->closeGracefully = false;
    m_progress->setValue(0);
    m_progress->setEnabled(
        direction == Direction::Sending || bytesTotal > 0);
    m_xferredLabel->setText(originalResourceString(
        direction == Direction::Sending
            ? QStringLiteral("IDS_BYTES_SENT")
            : QStringLiteral("IDS_BYTES_RECEIVED")));
    m_bytesSent->setText(QStringLiteral("0"));
    m_bytesTotal->setText(bytesTotal >= 0
        ? QString::number(bytesTotal)
        : originalResourceString(QStringLiteral("IDS_FILESIZE_UNKNOWN")));

    QString status = originalResourceString(
        direction == Direction::Sending
            ? QStringLiteral("IDS_AWAITING_ACCEPT")
            : QStringLiteral("IDS_FILE_CONNECTING"));
    ReplaceToken(status, QStringLiteral("%1"), otherGuy);
    m_status->setText(status);
    setWindowTitle(originalDialogCaption(
        QStringLiteral("IDD_FILE_TRANSFER")));
    postByteProgress(0, TRUE);
    // The synchronous initial zero display precedes the source worker's
    // independent 100 ms posting clock, so it must not throttle its first
    // real progress update.
    m_state->lastProgressPost.invalidate();
}

BOOL CFileProgress::StartSending(const QString& path, quint16 port)
{
    auto source = std::make_unique<QFile>(path);
    if (!source->open(QIODevice::ReadOnly)) return FALSE;
    return StartSending(std::move(source), path, port);
}

BOOL CFileProgress::StartSending(std::unique_ptr<QFile> sourceFile,
                                 const QString& path, quint16 port)
{
    if (!sourceFile || !sourceFile->isOpen()
        || !(sourceFile->openMode() & QIODevice::ReadOnly)) {
        return FALSE;
    }
    // Modern advertises and displays the one pre-open _stat size even if the
    // opened file changes before/during transfer. Configure owns that cached
    // value; the worker still reads the actual open handle through EOF.
    if (m_state->bytesTotal < 0 || m_state->bytesTotal > INT_MAX)
        return FALSE;

    resetTransport();
    m_state->direction = Direction::Sending;
    m_state->path = path;
    m_state->bytesTransferred = 0;
    m_state->totalSent = 0;
    m_state->canceled = false;
    m_state->file = std::move(sourceFile);
    m_state->server = new QTcpServer(this);
    m_state->server->setListenBacklogSize(1);
    m_state->server->setMaxPendingConnections(1);
    connect(m_state->server, &QTcpServer::newConnection,
            this, &CFileProgress::handleNewConnection);
    connect(m_state->server, &QTcpServer::acceptError, this,
            [this](QAbstractSocket::SocketError) {
                if (m_state->active) finishSilently();
            });
    if (!m_state->server->listen(QHostAddress::AnyIPv4, port)) {
        resetTransport();
        return FALSE;
    }
    m_state->active = true;
    m_state->timer->start(kAcceptTimeoutMs);
    return TRUE;
}

BOOL CFileProgress::StartReceiving(const QString& path, quint32 hostAddress,
                                   quint16 port, qint64 fileSize)
{
    // The source accepts an omitted size in its prompt, but its receive loop
    // then turns the first positive read into a negative write length and
    // declares success. There is no source-defined safe EOF contract to port.
    if (fileSize < 1 || fileSize > INT_MAX) return FALSE;

    resetTransport();
    m_state->direction = Direction::Receiving;
    m_state->path = path;
    m_state->bytesTotal = fileSize;
    m_state->bytesTransferred = 0;
    m_state->rawBytesReceived = 0;
    m_state->canceled = false;
    m_state->socket = new QTcpSocket(this);
    connect(m_state->socket, &QTcpSocket::connected,
            this, &CFileProgress::handleConnected);
    connect(m_state->socket, &QTcpSocket::readyRead,
            this, &CFileProgress::handleReadyRead);
    connect(m_state->socket, &QTcpSocket::bytesWritten,
            this, &CFileProgress::handleBytesWritten);
    connect(m_state->socket, &QTcpSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError error) {
                handleSocketError(error);
            });
    connect(m_state->socket, &QTcpSocket::disconnected, this, [this] {
        if (m_state->active) finishSilently();
    });
    m_state->active = true;
    m_state->socket->connectToHost(QHostAddress(hostAddress), port);
    return TRUE;
}

qint64 CFileProgress::BytesTransferred() const
{
    return m_state->bytesTransferred;
}

qint64 CFileProgress::BytesTotal() const
{
    return m_state->bytesTotal;
}

QString CFileProgress::StatusText() const
{
    return m_status ? m_status->text() : QString();
}

CFileProgress::Direction CFileProgress::TransferDirection() const
{
    return m_state->direction;
}

BOOL CFileProgress::IsActive() const
{
    return m_state->active;
}

BOOL CFileProgress::IsSending() const
{
    return m_state->direction == Direction::Sending;
}

BOOL CFileProgress::WasCanceled() const
{
    return m_state->canceled;
}

void CFileProgress::accept()
{
    // IDOK hides the modeless source window but does not stop its worker.
    QDialog::accept();
}

void CFileProgress::reject()
{
    cancelTransfer();
    QDialog::reject();
}

void CFileProgress::resetTransport()
{
    if (!m_state) return;
    if (m_state->timer) m_state->timer->stop();

    if (m_state->server) {
        m_state->server->disconnect(this);
        m_state->server->close();
        m_state->server->deleteLater();
        m_state->server = nullptr;
    }
    if (m_state->socket) {
        m_state->socket->disconnect(this);
        if (m_state->closeGracefully
            && m_state->socket->state()
                == QAbstractSocket::ConnectedState) {
            m_state->socket->flush();
            m_state->socket->disconnectFromHost();
        } else {
            m_state->socket->abort();
        }
        m_state->socket->deleteLater();
        m_state->socket = nullptr;
    }
    if (m_state->file) {
        m_state->file->close();
        m_state->file.reset();
    }
    m_state->sendBlock.clear();
    m_state->acknowledgementBytes.clear();
    m_state->sendOffset = 0;
    m_state->waitingForAcknowledgement = false;
    m_state->connected = false;
    m_state->active = false;
    m_state->closeGracefully = false;
}

void CFileProgress::handleNewConnection()
{
    if (!m_state->active || !m_state->server) return;
    QTcpSocket* accepted = m_state->server->nextPendingConnection();
    if (!accepted) return;
    m_state->timer->stop();
    m_state->server->close();
    m_state->server->deleteLater();
    m_state->server = nullptr;

    accepted->setParent(this);
    m_state->socket = accepted;
    m_state->socket->setSocketOption(
        QAbstractSocket::SendBufferSizeSocketOption,
        kSendSocketBufferSize);
    connect(m_state->socket, &QTcpSocket::readyRead,
            this, &CFileProgress::handleReadyRead);
    connect(m_state->socket, &QTcpSocket::bytesWritten,
            this, &CFileProgress::handleBytesWritten);
    connect(m_state->socket, &QTcpSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError error) {
                handleSocketError(error);
            });
    connect(m_state->socket, &QTcpSocket::disconnected, this, [this] {
        if (!m_state->active) return;
        if (m_state->waitingForAcknowledgement
            && m_state->timer->isActive()) {
            return;
        }
        finishSilently();
    });
    m_state->connected = true;
    setStatusResource(QStringLiteral("IDS_FILE_CONNECT"));
    queueNextSendBlock();
}

void CFileProgress::handleConnected()
{
    if (!m_state->active || !m_state->socket
        || m_state->direction != Direction::Receiving) {
        return;
    }
    m_state->connected = true;
    m_state->socket->setSocketOption(
        QAbstractSocket::ReceiveBufferSizeSocketOption,
        kSocketBufferSize);
    setStatusResource(QStringLiteral("IDS_FILE_CONNECT"));

    m_state->file = std::make_unique<QFile>(m_state->path);
    if (!m_state->file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QString message =
            originalResourceString(QStringLiteral("ID_ERR_SAVE"));
        ReplaceToken(message, QStringLiteral("%1"), m_state->path);
        showSourceOkMessage(message);
        finishSilently();
        return;
    }
    m_state->timer->start(kReceiveTimeoutMs);
    if (m_state->socket->bytesAvailable() > 0) processReceivedBytes();
}

void CFileProgress::handleReadyRead()
{
    if (!m_state->active || !m_state->socket) return;
    if (m_state->direction == Direction::Sending)
        processAcknowledgements();
    else if (m_state->connected)
        processReceivedBytes();
}

void CFileProgress::handleBytesWritten()
{
    if (!m_state->active || !m_state->socket
        || m_state->direction != Direction::Sending
        || m_state->waitingForAcknowledgement) {
        return;
    }
    continueCurrentSendBlock();
}

void CFileProgress::handleSocketError(
    QAbstractSocket::SocketError error)
{
    if (!m_state->active) return;
    if (m_state->direction == Direction::Sending
        && m_state->waitingForAcknowledgement
        && error == QAbstractSocket::RemoteHostClosedError) {
        // recv() returns zero after an orderly close; the source keeps
        // waiting for its four-byte ACK until the receive timeout.
        return;
    }
    if (m_state->direction == Direction::Receiving
        && !m_state->connected) {
        setStatusResource(QStringLiteral("IDS_CONNECTION_FAILED"));
    }
    finishSilently();
}

void CFileProgress::handleTimeout()
{
    if (!m_state->active) return;
    setStatusResource(QStringLiteral("IDS_FILETIMEOUT"));
    finishSilently();
}

void CFileProgress::queueNextSendBlock()
{
    if (!m_state->active || !m_state->file || !m_state->socket) return;
    const QByteArray block = m_state->file->read(kSendBlockSize);
    if (block.isEmpty()) {
        if (m_state->file->error() == QFileDevice::NoError)
            finishSuccessfully();
        else
            finishSilently();
        return;
    }
    m_state->sendBlock = block;
    m_state->sendOffset = 0;
    m_state->waitingForAcknowledgement = false;
    continueCurrentSendBlock();
}

void CFileProgress::continueCurrentSendBlock()
{
    if (!m_state->active || !m_state->socket
        || m_state->sendOffset >= m_state->sendBlock.size()) {
        return;
    }
    const qint64 written = m_state->socket->write(
        m_state->sendBlock.constData() + m_state->sendOffset,
        m_state->sendBlock.size() - m_state->sendOffset);
    if (written < 0) {
        finishSilently();
        return;
    }
    if (written == 0) return;
    m_state->sendOffset += static_cast<qsizetype>(written);
    if (m_state->sendOffset < m_state->sendBlock.size()) return;

    m_state->totalSent += m_state->sendBlock.size();
    m_state->waitingForAcknowledgement = true;
    m_state->timer->start(kReceiveTimeoutMs);
    if (!m_state->acknowledgementBytes.isEmpty())
        processAcknowledgements();
}

void CFileProgress::processAcknowledgements()
{
    if (!m_state->active || !m_state->socket) return;
    m_state->acknowledgementBytes += m_state->socket->readAll();
    while (m_state->active && m_state->waitingForAcknowledgement
           && m_state->acknowledgementBytes.size()
               >= static_cast<qsizetype>(sizeof(quint32))) {
        const qint32 acknowledged = qFromBigEndian<qint32>(
            reinterpret_cast<const uchar*>(
                m_state->acknowledgementBytes.constData()));
        m_state->acknowledgementBytes.remove(
            0, static_cast<qsizetype>(sizeof(quint32)));
        postByteProgress(acknowledged,
                         acknowledged >= m_state->totalSent);
        if (acknowledged < m_state->totalSent) {
            // The source gives each complete cumulative ACK another receive
            // interval while it waits for the current block total.
            m_state->timer->start(kReceiveTimeoutMs);
            continue;
        }
        m_state->timer->stop();
        m_state->waitingForAcknowledgement = false;
        m_state->sendBlock.clear();
        m_state->sendOffset = 0;
        queueNextSendBlock();
    }
}

void CFileProgress::processReceivedBytes()
{
    while (m_state->active && m_state->socket
           && m_state->socket->bytesAvailable() > 0) {
        const qint64 amount = qMin<qint64>(
            kReceiveBlockSize, m_state->socket->bytesAvailable());
        const QByteArray block = m_state->socket->read(amount);
        if (block.isEmpty()) {
            finishSilently();
            return;
        }

        m_state->rawBytesReceived += block.size();
        const quint32 acknowledged =
            static_cast<quint32>(m_state->rawBytesReceived);
        char ackBytes[sizeof(quint32)];
        qToBigEndian<quint32>(
            acknowledged, reinterpret_cast<uchar*>(ackBytes));
        m_state->socket->write(ackBytes, sizeof(ackBytes));
        m_state->socket->flush();

        const qint64 alreadyStored =
            qMin(m_state->bytesTransferred, m_state->bytesTotal);
        const qint64 writeAmount = qMax<qint64>(
            0, qMin<qint64>(block.size(),
                            m_state->bytesTotal - alreadyStored));
        qint64 totalWritten = 0;
        while (totalWritten < writeAmount) {
            const qint64 written = m_state->file->write(
                block.constData() + totalWritten,
                writeAmount - totalWritten);
            if (written <= 0) {
                showSourceOkMessage(
                    QStringLiteral("Could not completely save file."));
                finishSilently();
                return;
            }
            totalWritten += written;
        }

        postByteProgress(
            m_state->rawBytesReceived,
            m_state->rawBytesReceived >= m_state->bytesTotal);
        if (m_state->rawBytesReceived >= m_state->bytesTotal) {
            finishSuccessfully();
            return;
        }
        m_state->timer->start(kReceiveTimeoutMs);
    }
}

void CFileProgress::setStatusResource(const QString& identifier)
{
    if (m_status) m_status->setText(originalResourceString(identifier));
}

void CFileProgress::postByteProgress(qint64 bytes, BOOL force)
{
    m_state->bytesTransferred = bytes;
    if (!force && m_state->lastProgressPost.isValid()
        && m_state->lastProgressPost.elapsed() < kProgressPostLimitMs) {
        return;
    }
    m_state->lastProgressPost.restart();
    if (m_bytesSent) m_bytesSent->setText(QString::number(bytes));
    if (m_state->bytesTotal <= 0) return;

    const double percent = static_cast<double>(bytes)
        / static_cast<double>(m_state->bytesTotal);
    const double sourceProgress = percent * 100.0;
    // The source narrows an untrusted signed cumulative ACK to int for both
    // the progress position and title. Preserve every representable result,
    // but make the otherwise undefined out-of-range conversion explicit.
    const int progress = sourceProgress >= INT_MAX
        ? INT_MAX
        : sourceProgress <= INT_MIN
            ? INT_MIN
            : static_cast<int>(sourceProgress);
    if (m_progress) m_progress->setValue(qBound(0, progress, 100));
    QString title = originalResourceString(
        m_state->direction == Direction::Sending
            ? QStringLiteral("IDS_FILESEND_TITLE")
            : QStringLiteral("IDS_FILEGET_TITLE"));
    ReplaceToken(title, QStringLiteral("%1"), QString::number(progress));
    ReplaceToken(title, QStringLiteral("%2"), m_state->fileName);
    ReplaceToken(title, QStringLiteral("%3"), m_state->otherGuy);
    setWindowTitle(title);
}

void CFileProgress::finishSuccessfully()
{
    setStatusResource(
        m_state->direction == Direction::Sending
            ? QStringLiteral("IDS_FILE_SENT")
            : QStringLiteral("IDS_FILE_RECEIVED"));
    m_state->closeGracefully = true;
    resetTransport();
}

void CFileProgress::finishSilently()
{
    resetTransport();
}

void CFileProgress::cancelTransfer()
{
    if (!m_state) return;
    m_state->canceled = true;
    m_state->closeGracefully = false;
    resetTransport();
}

void CRoomInfo::ChatSendFile(CUserInfo* pui)
{
    if (!pui || pui->IsDeparted()) return;

    QFileDialog dialog(dialogParent());
    dialog.setObjectName(QStringLiteral("CFileDialog"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontResolveSymlinks, false);
    dialog.setNameFilter(sourceAllFilesFilter());
    QString title =
        originalResourceString(QStringLiteral("IDS_TITLE_FILEDLG_SEND"));
    ReplaceToken(title, QStringLiteral("%1"), pui->GetScreenName());
    dialog.setWindowTitle(title);
    if (dialog.exec() != QDialog::Accepted) return;

    // Source consumes the port as soon as the accepted file dialog returns,
    // even if address lookup, stat, or open subsequently fails.
    // The source stores each selected port in sixteen bits. Keep that
    // wraparound without retaining its eventual signed-int overflow.
    const quint16 port = g_nextDccPort++;
    const quint32 hostAddress = static_cast<quint32>(GetMyIP());
    if (hostAddress == 0) return;

    const QString path = dialog.selectedFiles().value(0);
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return;
    }
    const qint64 fileSize = fileInfo.size();
    if (fileSize < 0 || fileSize > INT_MAX) return;
    auto sourceFile = std::make_unique<QFile>(path);
    if (!sourceFile->open(QIODevice::ReadOnly)) return;

    const QString fileName = fileInfo.fileName();
    const QString offer = BuildDCCSendOffer(
        fileName, hostAddress, port, fileSize);
    bChatSendPrivMesg(pui->GetName(), QString(), offer);

    auto* progress = new CFileProgress;
    progress->Configure(CFileProgress::Direction::Sending,
                        fileName, pui->GetScreenName(), fileSize);
    progress->show();
    addToFileProgressStore(progress);
    progress->StartSending(std::move(sourceFile), path, port);
}

void ChatReceiveFile(CUserInfo* pui, const QString& message)
{
    if (!pui || !theApp.m_bAllowFileTX || !bCanViewUnrated()) return;

    DCCSendOffer offer;
    if (!ParseDCCSendOffer(message, &offer)) return;
    if (g_receiveDialogCount >= kReceiveDialogLimit) return;
    ReceiveDialogCounter dialogCounter;

    QString prompt =
        originalResourceString(QStringLiteral("IDS_ACCEPT_FILE_MESG"));
    ReplaceToken(prompt, QStringLiteral("%1"), pui->GetScreenName());
    ReplaceToken(prompt, QStringLiteral("%2"), offer.fileName);
    ReplaceToken(prompt, QStringLiteral("%3"), sourceFileSizeText(offer));
    // The source uses MB_YESNO without an icon flag.
    QMessageBox question(
        QMessageBox::NoIcon,
        originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
        prompt, QMessageBox::Yes | QMessageBox::No, dialogParent());
    question.setDefaultButton(QMessageBox::Yes);
    if (question.exec() != QMessageBox::Yes) return;

    QFileDialog dialog(dialogParent());
    dialog.setObjectName(QStringLiteral("CFileReceiveDialog"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setOption(QFileDialog::DontConfirmOverwrite, false);
    dialog.setNameFilter(sourceAllFilesFilter());
    QString title =
        originalResourceString(QStringLiteral("IDS_TITLE_FILEDLG_RCV"));
    ReplaceToken(title, QStringLiteral("%1"), pui->GetScreenName());
    dialog.setWindowTitle(title);
    if (!theApp.m_strFileTXDir.isEmpty())
        dialog.setDirectory(theApp.m_strFileTXDir);
    dialog.selectFile(offer.fileName);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString path = dialog.selectedFiles().value(0);
    theApp.m_strFileTXDir = QFileInfo(path).absolutePath();
    if (!offer.fileSizeKnown) {
        // See StartReceiving: without a positive source size there is no safe,
        // source-defined completion/write rule.
        return;
    }

    auto* progress = new CFileProgress;
    progress->Configure(CFileProgress::Direction::Receiving,
                        QFileInfo(path).fileName(),
                        pui->GetScreenName(), offer.fileSize);
    progress->show();
    addToFileProgressStore(progress);
    progress->StartReceiving(
        path, offer.hostAddress, offer.port, offer.fileSize);
}

void CleanupFileProgressStore(BOOL includeShowing)
{
    for (qsizetype index = g_fileProgressStore.size(); index-- > 0;) {
        CFileProgress* progress = g_fileProgressStore.at(index).data();
        if (!progress) {
            g_fileProgressStore.removeAt(index);
            continue;
        }
        if (includeShowing
            || (!progress->isVisible() && !progress->IsActive())) {
            g_fileProgressStore.removeAt(index);
            delete progress;
        }
    }
}
