// Ported from v2.5-beta-1-modern/filesend.h and filesend.cpp.
// Qt replaces only the Win32 dialog, socket, thread, and file-handle mechanics.

#pragma once

#include "wincompat.h"

#include <QAbstractSocket>
#include <QDialog>
#include <QString>
#include <QtGlobal>

#include <memory>

class CUserInfo;
class QFile;
class QLabel;
class QProgressBar;
class QPushButton;
class QTcpServer;
class QTcpSocket;
class QTimer;
class QWidget;

struct DCCSendOffer {
    QString fileName;
    quint32 hostAddress = 0;
    quint16 port = 0;
    qint64 fileSize = -1;
    BOOL fileSizeKnown = FALSE;
};

// Quotes strings according to the CTCP draft of February 2, 1997. CTCP's DCC
// filename quoting is distinct from the lower-level 0x10 quoting applied by
// CIrcProto to the complete private-message payload. Nulls are not quoted
// because the source treats them as string terminators.
QString CTCPQuoteString(const QString& value);
BOOL CTCPUnQuoteString(QString* value);

QString BuildDCCSendOffer(const QString& fileName, quint32 hostAddress,
                          quint16 port, qint64 fileSize);
BOOL ParseDCCSendOffer(const QString& arguments, DCCSendOffer* offer);

class CFileProgress : public QDialog {
public:
    enum class Direction {
        Sending,
        Receiving
    };

    explicit CFileProgress(QWidget* parent = nullptr);
    ~CFileProgress() override;

    void Configure(Direction direction, const QString& fileName,
                   const QString& otherGuy, qint64 bytesTotal);

    // The ordinary overload is the deterministic loopback/test seam. The
    // owning overload lets ChatSendFile retain the source-open-before-offer
    // ordering without reopening a path after the invitation was advertised.
    BOOL StartSending(const QString& path, quint16 port);
    BOOL StartSending(std::unique_ptr<QFile> sourceFile,
                      const QString& path, quint16 port);
    BOOL StartReceiving(const QString& path, quint32 hostAddress,
                        quint16 port, qint64 fileSize);

    qint64 BytesTransferred() const;
    qint64 BytesTotal() const;
    QString StatusText() const;
    Direction TransferDirection() const;
    BOOL IsActive() const;
    BOOL IsSending() const;
    BOOL WasCanceled() const;

    void accept() override;
    void reject() override;

private:
    struct TransferState;

    void buildDialog();
    void resetTransport();
    void handleNewConnection();
    void handleConnected();
    void handleReadyRead();
    void handleBytesWritten();
    void handleSocketError(QAbstractSocket::SocketError error);
    void handleTimeout();
    void queueNextSendBlock();
    void continueCurrentSendBlock();
    void processAcknowledgements();
    void processReceivedBytes();
    void setStatusResource(const QString& identifier);
    void postByteProgress(qint64 bytes, BOOL force);
    void finishSuccessfully();
    void finishSilently();
    void cancelTransfer();

    std::unique_ptr<TransferState> m_state;
    QLabel* m_xferredLabel = nullptr;
    QLabel* m_bytesSent = nullptr;
    QLabel* m_bytesTotal = nullptr;
    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_ok = nullptr;
    QPushButton* m_cancel = nullptr;
};

void ChatReceiveFile(CUserInfo* pui, const QString& message);
void CleanupFileProgressStore(BOOL includeShowing);
