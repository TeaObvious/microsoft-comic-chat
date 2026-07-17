// Ported from v2.5-beta-1-modern/ircsock.h.
// CIrcSocket uses QTcpSocket but keeps original parser/reply responsibilities.

#pragma once

#include "query.h"
#include "resource.h"
#include "wincompat.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <array>

class CIrcProto;
class CChatDoc;
class CRoom;
class QTcpSocket;
class QTimer;

constexpr int RPL_WELCOME = 1;
constexpr int RPL_UMODEIS = 221;
constexpr int RPL_LUSERCLIENT = 251;
constexpr int RPL_LUSEROP = 252;
constexpr int RPL_LUSERUNKNOWN = 253;
constexpr int RPL_LUSERCHANNELS = 254;
constexpr int RPL_LUSERME = 255;
constexpr int RPL_LOCALUSERS = 265;
constexpr int RPL_GLOBALUSERS = 266;
constexpr int RPL_AWAY = 301;
constexpr int RPL_UNAWAY = 305;
constexpr int RPL_NOWAWAY = 306;
constexpr int RPL_WHOISUSER = 311;
constexpr int RPL_ENDOFWHOIS = 318;
constexpr int RPL_CHANNELMODEIS = 324;
constexpr int RPL_LISTSTART = 321;
constexpr int RPL_LIST = 322;
constexpr int RPL_LISTEND = 323;
constexpr int RPL_NOTOPIC = 331;
constexpr int RPL_TOPIC = 332;
constexpr int RPL_INVITING = 341;
constexpr int RPL_WHOREPLY = 352;
constexpr int RPL_NAMEREPLY = 353;
constexpr int RPL_ENDOFWHO = 315;
constexpr int RPL_ENDOFNAMES = 366;
constexpr int RPL_BANLIST = 367;
constexpr int RPL_ENDOFBANLIST = 368;
constexpr int RPL_MOTD = 372;
constexpr int RPL_MOTDSTART = 375;
constexpr int RPL_ENDOFMOTD = 376;
constexpr int RPL_MOTD2 = 377;
constexpr int RPL_PROPLIST = 818;
constexpr int RPL_PROPEND = 819;
constexpr int RPL_LISTXSTART = 811;
constexpr int RPL_LISTXLIST = 812;
constexpr int RPL_LISTXPICS = 813;
constexpr int RPL_LISTXTRUNC = 816;
constexpr int RPL_LISTXEND = 817;
constexpr int ERR_NOSUCHCHANNEL = 403;
constexpr int ERR_NOTONCHANNEL = 442;
constexpr int ERR_NOSUCHOBJECT = 924;
constexpr int ERR_NOMOTD = 422;
constexpr int MAXARGS = 10;

constexpr UINT PT_NOTINIT = 0;
constexpr UINT PT_WHOLESTRING = 1;
constexpr UINT PT_LASTSTRING = 2;
constexpr UINT PT_NONE = 3;
constexpr UINT PT_OFFSET = 4;

constexpr UCHAR CMD_SHOWSTATUSWINDOW = 0x01;
constexpr UCHAR CMD_MUSTBECONNECTED = 0x02;

constexpr DWORD AT_NONE = 0x00000000;
constexpr DWORD AT_NICKNAME = 0x00000001;
constexpr DWORD AT_NICKMASK = 0x00000002;
constexpr DWORD AT_CHANNEL = 0x00000004;
constexpr DWORD AT_TOPIC = 0x00000008;
constexpr DWORD AT_REASON = 0x00000010;
constexpr DWORD AT_MESSAGE = 0x00000020;
constexpr DWORD AT_MAXMEMBER = 0x00000040;
constexpr DWORD AT_CHANNELFLAGS = 0x00000080;
constexpr DWORD AT_PASSWORD = 0x00000100;
constexpr DWORD AT_SERVER = 0x00000200;
constexpr DWORD AT_NETWORK = 0x00000400;
constexpr DWORD AT_SOUND = 0x00000800;
constexpr DWORD AT_USERFLAGS = 0x00001000;
constexpr DWORD AT_PROPNAME = 0x00002000;
constexpr DWORD AT_PROPVALUE = 0x00004000;
constexpr DWORD AT_OPTIONAL = 0x08000000;
constexpr DWORD AT_SPACEMULTIPLE = 0x10000000;
constexpr DWORD AT_COMMAMULTIPLE = 0x20000000;
constexpr DWORD AT_SHOWCOLON = 0x40000000;
constexpr DWORD AT_COLON = 0x80000000;
constexpr UINT AT_COUNT = 15;

enum enumCmdId {
    cmdidAccess,
    cmdidAction,
    cmdidAuth,
    cmdidAway,
    cmdidClone,
    cmdidCreate,
    cmdidData,
    cmdidError,
    cmdidInfo,
    cmdidInvite,
    cmdidIsOn,
    cmdidJoin,
    cmdidKick,
    cmdidKill,
    cmdidKilled,
    cmdidKLine,
    cmdidKnock,
    cmdidList,
    cmdidListX,
    cmdidLUsers,
    cmdidMe,
    cmdidMode,
    cmdidMsg,
    cmdidNames,
    cmdidNick,
    cmdidNotice,
    cmdidPart,
    cmdidPass,
    cmdidPing,
    cmdidPong,
    cmdidPrivMsg,
    cmdidProp,
    cmdidQuit,
    cmdidQuote,
    cmdidRaw,
    cmdidReply,
    cmdidRequest,
    cmdidServer,
    cmdidSound,
    cmdidThink,
    cmdidTopic,
    cmdidUnKLine,
    cmdidUser,
    cmdidUserHost,
    cmdidWhisper,
    cmdidWho,
    cmdidWhoIs,
    cmdidMax
};

struct SYNTAX {
    enumCmdId cmdid = cmdidMax;
    UINT uIDSComment = 0;
    UCHAR uArgNum = 0;
    std::array<DWORD, 6> dwArgType{};
};

constexpr UINT g_uSyntaxCount = 24;
extern const SYNTAX g_rgSyntax[g_uSyntaxCount];

struct PRIRCCMD {
    const char* szCmd = nullptr;
    int cb = 0;
    UCHAR uFlags = 0;
    UCHAR uMinArg = 0;
};

extern const PRIRCCMD g_rgIrcCmd[cmdidMax];
SHORT NGetCmd(const QString& command);

struct IRCPARSE {
    BOOL bHasPrefix = FALSE;
    QString nick;
    QString user;
    QString machine;
    QString command;
    QStringList args;
    SHORT nArgs = 0;
    std::array<SHORT, MAXARGS> nOffsets{};
    QString lastString;
    BOOL bHasLastString = FALSE;
    int uCode = 0;
};

void ParseIt(const QString& message, IRCPARSE* parse,
             BOOL doubleQuotes = FALSE);
void ParseIt(const QByteArray& message, IRCPARSE* parse,
             BOOL doubleQuotes = FALSE);
void CSInString(QString* string,
                const QString& channelName = QString(),
                CChatDoc* doc = nullptr);
void CSInPlace(QString* nickname);
void GetBanString(const QString& userName, const QString& hostName,
                  QString& ban);

class CIrcPrint {
public:
    UINT m_iType = PT_NOTINIT;
    QString m_szMessage;
    COLORREF m_crTextColor = RGB(0, 0, 0);
    BYTE m_offset = 0;
    BOOL m_bNewLine = FALSE;

    void SetFormat(UINT type, const QString& message = QString(),
                   COLORREF color = RGB(0, 0, 0), int offset = 0,
                   BOOL newLine = FALSE)
    {
        m_iType = type;
        m_szMessage = message;
        m_crTextColor = color;
        m_offset = static_cast<BYTE>(offset);
        m_bNewLine = newLine;
    }
};

class CIrcSocket {
public:
    explicit CIrcSocket(CIrcProto* proto = nullptr);
    ~CIrcSocket();

    void AttachProtocol(CIrcProto* proto);
    void DetachProtocol(CIrcProto* proto);

    bool Connect(const QString& server, quint16 port);
    void AdoptSocket(QTcpSocket* socket);
    void Disconnect();
    void Reset();
    void SendRaw(const QString& raw);
    void SendRaw(const QByteArray& raw);
    void ProcessMessage(const QString& line);
    void ProcessMessageBytes(const QByteArray& line);
    void OnConnect();
    bool HrIrcLogin(bool ircX, const QString& nickname = QString(),
                    const QString& userName = QString(),
                    const QString& realName = QString(),
                    const QString& password = QString(),
                    bool promptForPassword = true);
    void HrModeIsIrcXFailure();
    BOOL PromptForPassword(const QString& userName,
                           BOOL saveInSettings);
    void SetAuthentication(UINT type, const QString& userName,
                           const QString& password,
                           const QString& customPackages);

    CQueryPtrList m_queries;
    short m_nMaxMsgLength = 512;
    int m_iConnected = 0;
    bool m_bIrcXServer = false;
    QString m_strLUSER;
    QString m_strMOTD;

private:
    void bindSocket(QTcpSocket* socket);
    void handleReadyRead();
    void handleConnected();
    void handleDisconnected();

    CIrcProto* m_proto = nullptr;
    QTcpSocket* m_socket = nullptr;
    QTimer* m_ircXTimer = nullptr;
    QByteArray m_pending;
    bool m_bRegistered = false;
    bool m_bJustSentModeIsIrcX = false;
    bool m_bDisconnectRequested = false;
    CRoom* m_pendingListXRoom = nullptr;
    BOOL m_pendingListXAdd = FALSE;
    UINT m_nAuthenticationType = 0;
    QString m_pszUserName;
    QString m_pszPassword;
    QString m_pszSecurityPackages;
};
