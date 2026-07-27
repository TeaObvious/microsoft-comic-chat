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

constexpr int RPL_IRCSTART = 1;
constexpr int RPL_WELCOME = 1;
constexpr int RPL_YOURHOST = 2;
constexpr int RPL_CREATED = 3;
constexpr int RPL_MYINFO = 4;
constexpr int RPL_FOOFORNOW = 5;
constexpr int RPL_TRACELINK = 200;
constexpr int RPL_TRACECONNECTING = 201;
constexpr int RPL_TRACEHANDSHAKE = 202;
constexpr int RPL_TRACEUNKNOWN = 203;
constexpr int RPL_TRACEOPERATOR = 204;
constexpr int RPL_TRACEUSER = 205;
constexpr int RPL_TRACESERVER = 206;
constexpr int RPL_TRACENEWTYPE = 208;
constexpr int RPL_STATSLINKINFO = 211;
constexpr int RPL_STATSCOMMANDS = 212;
constexpr int RPL_STATSCLINE = 213;
constexpr int RPL_STATSNLINE = 214;
constexpr int RPL_STATSILINE = 215;
constexpr int RPL_STATSKLINE = 216;
constexpr int RPL_STATSYLINE = 218;
constexpr int RPL_ENDOFSTATS = 219;
constexpr int RPL_UMODEIS = 221;
constexpr int RPL_STATSLLINE = 241;
constexpr int RPL_STATSUPTIME = 242;
constexpr int RPL_STATSOLINE = 243;
constexpr int RPL_STATSHLINE = 244;
constexpr int RPL_LUSERCLIENT = 251;
constexpr int RPL_LUSEROP = 252;
constexpr int RPL_LUSERUNKNOWN = 253;
constexpr int RPL_LUSERCHANNELS = 254;
constexpr int RPL_LUSERME = 255;
constexpr int RPL_ADMINME = 256;
constexpr int RPL_ADMINLOC1 = 257;
constexpr int RPL_ADMINLOC2 = 258;
constexpr int RPL_ADMINEMAIL = 259;
constexpr int RPL_TRACELOG = 261;
constexpr int RPL_LOCALUSERS = 265;
constexpr int RPL_GLOBALUSERS = 266;
constexpr int RPL_AWAY = 301;
constexpr int RPL_USERHOST = 302;
constexpr int RPL_ISON = 303;
constexpr int RPL_UNAWAY = 305;
constexpr int RPL_NOWAWAY = 306;
constexpr int RPL_WHOISUSER = 311;
constexpr int RPL_WHOISSERVER = 312;
constexpr int RPL_WHOISOPERATOR = 313;
constexpr int RPL_WHOWASUSER = 314;
constexpr int RPL_ENDOFWHO = 315;
constexpr int RPL_WHOISIDLE = 317;
constexpr int RPL_ENDOFWHOIS = 318;
constexpr int RPL_WHOISCHANNELS = 319;
constexpr int RPL_WHOISIP = 320;
constexpr int RPL_LISTSTART = 321;
constexpr int RPL_LIST = 322;
constexpr int RPL_LISTEND = 323;
constexpr int RPL_CHANNELMODEIS = 324;
constexpr int RPL_NOTOPIC = 331;
constexpr int RPL_TOPIC = 332;
constexpr int RPL_INVITING = 341;
constexpr int RPL_VERSION = 351;
constexpr int RPL_WHOREPLY = 352;
constexpr int RPL_NAMEREPLY = 353;
constexpr int RPL_LINKS = 364;
constexpr int RPL_ENDOFLINKS = 365;
constexpr int RPL_ENDOFNAMES = 366;
constexpr int RPL_BANLIST = 367;
constexpr int RPL_ENDOFBANLIST = 368;
constexpr int RPL_ENDOFWHOWAS = 369;
constexpr int RPL_INFO = 371;
constexpr int RPL_MOTD = 372;
constexpr int RPL_ENDOFINFO = 374;
constexpr int RPL_MOTDSTART = 375;
constexpr int RPL_ENDOFMOTD = 376;
constexpr int RPL_MOTD2 = 377;
constexpr int RPL_YOUREOPER = 381;
constexpr int RPL_YOUREADMIN = 386;
constexpr int RPL_TIME = 391;
constexpr int RPL_IRCEND = 399;

constexpr int ERR_IRCSTART = 401;
constexpr int ERR_NOSUCHNICK = 401;
constexpr int ERR_NOSUCHSERVER = 402;
constexpr int ERR_NOSUCHCHANNEL = 403;
constexpr int ERR_CANNOTSENDTOCHAN = 404;
constexpr int ERR_TOOMANYCHANNELS = 405;
constexpr int ERR_TOOMANYTARGETS = 407;
constexpr int ERR_NOORIGIN = 409;
constexpr int ERR_NORECIPIENT = 411;
constexpr int ERR_UNKNOWNCOMMAND = 421;
constexpr int ERR_NOMOTD = 422;
constexpr int ERR_NONICKNAMEGIVEN = 431;
constexpr int ERR_ERRONEUSNICKNAME = 432;
constexpr int ERR_NICKNAMEINUSE = 433;
constexpr int ERR_NICKCOLLISION = 436;
constexpr int ERR_NICKTOOFAST = 438;
constexpr int ERR_NICKNOCHANGE = 439;
constexpr int ERR_USERNOTINCHANNEL = 441;
constexpr int ERR_NOTONCHANNEL = 442;
constexpr int ERR_USERONCHANNEL = 443;
constexpr int ERR_NOTREGISTERED = 451;
constexpr int ERR_NEEDMOREPARAMS = 461;
constexpr int ERR_ALREADYREGISTERED = 462;
constexpr int ERR_PASSWDMISMATCH = 464;
constexpr int ERR_YOUREBANNEDCREEP = 465;
constexpr int ERR_YOUWILLBEBANNED = 466;
constexpr int ERR_KEYSET = 467;
constexpr int ERR_CHANNELISFULL = 471;
constexpr int ERR_UNKNOWNMODE = 472;
constexpr int ERR_INVITEONLYCHAN = 473;
constexpr int ERR_BANNEDFROMCHAN = 474;
constexpr int ERR_BADCHANNELKEY = 475;
constexpr int ERR_NOPRIVILEGES = 481;
constexpr int ERR_CHANOPRIVSNEEDED = 482;
constexpr int ERR_CHANOWNPRIVNEEDED = 485;
constexpr int ERR_UMODEUNKNOWNFLAG = 501;
constexpr int ERR_USERSDONTMATCH = 502;
constexpr int ERR_IRCEND = 502;

constexpr int RPL_IRCXSTART = 800;
constexpr int RPL_IRCX = 800;
constexpr int RPL_ACCESSADD = 801;
constexpr int RPL_ACCESSDELETE = 802;
constexpr int RPL_ACCESSSTART = 803;
constexpr int RPL_ACCESSLIST = 804;
constexpr int RPL_ACCESSEND = 805;
constexpr int RPL_EVENTADD = 806;
constexpr int RPL_EVENTDEL = 807;
constexpr int RPL_EVENTSTART = 808;
constexpr int RPL_EVENTLIST = 809;
constexpr int RPL_EVENTEND = 810;
constexpr int RPL_LISTXSTART = 811;
constexpr int RPL_LISTXLIST = 812;
constexpr int RPL_LISTXPICS = 813;
constexpr int RPL_LISTXTRUNC = 816;
constexpr int RPL_LISTXEND = 817;
constexpr int RPL_PROPLIST = 818;
constexpr int RPL_PROPEND = 819;
constexpr int RPL_IRCXEND = 899;

constexpr int ERR_IRCXSTART1 = 503;
constexpr int ERR_NOJOINDYNAMIC = 552;
constexpr int ERR_NODYNAMICCHANNELS = 553;
constexpr int ERR_AUTHONLY = 556;
constexpr int ERR_OVERFLOWABORT = 557;
constexpr int ERR_IRCXEND1 = 557;
constexpr int ERR_IRCXSTART2 = 900;
constexpr int ERR_BADCOMMAND = 900;
constexpr int ERR_TOOMANYARGUMENTS = 901;
constexpr int ERR_BADFUNCTION = 902;
constexpr int ERR_BADLEVEL = 903;
constexpr int ERR_BADTAG = 904;
constexpr int ERR_BADPROPERTY = 905;
constexpr int ERR_BADVALUE = 906;
constexpr int ERR_RESOURCE = 907;
constexpr int ERR_SECURITY = 908;
constexpr int ERR_ALREADYAUTHENTICATED = 909;
constexpr int ERR_AUTHENTICATIONFAILED = 910;
constexpr int ERR_AUTHENTICATIONSUSPENDED = 911;
constexpr int ERR_UNKNOWNPACKAGE = 912;
constexpr int ERR_NOACCESS = 913;
constexpr int ERR_NOWHISPER = 923;
constexpr int ERR_NOSUCHOBJECT = 924;
constexpr int ERR_NOTSUPPORTED = 925;
constexpr int ERR_CHANNELEXIST = 926;
constexpr int ERR_INTERNALERROR = 999;

constexpr int ERR_CANNOTJOINMICONLY = 900;
constexpr int ERR_CANNOTJOINFROMREMOTE = 901;
constexpr int ERR_CANNOTCREATEDYNAMIC = 902;
constexpr int ERR_COMMANDNOTSUPPORTED = 903;
constexpr int ERR_ONLYAUTHCANJOIN = 904;
constexpr int ERR_CANNOTCHANGENICK = 905;
constexpr int ERR_CANNOTMAKEHOST = 906;
constexpr int ERR_CANNOTJOINDYNAMIC = 907;
constexpr int ERR_UNKNOWNERROR = 999;
constexpr int ERR_IRCXEND2 = 999;

constexpr bool bIsErrorCode(int code)
{
    return (code >= ERR_IRCSTART && code <= ERR_IRCEND)
        || (code >= ERR_IRCXSTART1 && code <= ERR_IRCXEND1)
        || (code >= ERR_IRCXSTART2 && code <= ERR_IRCXEND2);
}
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
    quint32 LocalIPv4Address() const;
    void ProcessMessage(const QString& line);
    void ProcessMessageBytes(const QByteArray& line);
    void HandleCommand(QString& displayLine, const QString& sourceLine,
                       IRCPARSE* parse, CIrcPrint* ircPrint);
    void HandleResultCode(QString& displayLine, const QString& sourceLine,
                          IRCPARSE* parse, CIrcPrint* ircPrint);
    void HandleErrorCode(const QString& sourceLine, IRCPARSE* parse,
                         CIrcPrint* ircPrint);
    BOOL bFreeModeCell(const QString* channel, const QString* nickname);
    void OnConnect();
    bool HrIrcLogin(bool ircX, const QString& nickname = QString(),
                    const QString& userName = QString(),
                    const QString& realName = QString(),
                    const QString& password = QString(),
                    bool promptForPassword = true);
    bool HrIrcSetOper(const QString& userName,
                      const QString& password = QString());
    bool HrIrcXLogin(BOOL forceNextPackage);
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
    QStringList m_rgszSvrSecuPack;
    QStringList m_rgszUsrSecuPack;
    BOOL m_bAnonAllowed = FALSE;
    BOOL m_bAuthFailed = FALSE;
    short m_nSecuPackIndex = -1;

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
};
