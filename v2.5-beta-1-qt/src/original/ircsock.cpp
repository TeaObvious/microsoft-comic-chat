// Ported from v2.5-beta-1-modern/ircsock.cpp.

#include "ircsock.h"

#include "chat.h"
#include "chatdoc.h"
#include "chatsrv.h"
#include "ccommon.h"
#include "histent.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "memblst.h"
#include "motd.h"
#include "notif.h"
#include "originalassets.h"
#include "protsupp.h"
#include "roomlist.h"
#include "setupdlg.h"
#include "status.h"
#include "userlist.h"
#include "userinfo.h"

#include <QHostInfo>
#include <QHostAddress>
#include <QMessageBox>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <cctype>

#define SET_CMD(command) command, int(sizeof(command) - 1)

const PRIRCCMD g_rgIrcCmd[cmdidMax] = {
    {SET_CMD("ACCESS"), 0x03, 0x00},
    {SET_CMD("ACTION"), 0x02, 0x00},
    {SET_CMD("AUTH"), 0x03, 0x00},
    {SET_CMD("AWAY"), 0x02, 0x00},
    {SET_CMD("CLONE"), 0x03, 0x00},
    {SET_CMD("CREATE"), 0x02, 0x00},
    {SET_CMD("DATA"), 0x02, 0x00},
    {SET_CMD("ERROR"), 0x02, 0x00},
    {SET_CMD("INFO"), 0x03, 0x00},
    {SET_CMD("INVITE"), 0x02, 0x02},
    {SET_CMD("ISON"), 0x03, 0x01},
    {SET_CMD("JOIN"), 0x02, 0x00},
    {SET_CMD("KICK"), 0x02, 0x02},
    {SET_CMD("KILL"), 0x02, 0x01},
    {SET_CMD("KILLED"), 0x02, 0x00},
    {SET_CMD("KLINE"), 0x03, 0x00},
    {SET_CMD("KNOCK"), 0x03, 0x00},
    {SET_CMD("LIST"), 0x02, 0x00},
    {SET_CMD("LISTX"), 0x03, 0x00},
    {SET_CMD("LUSERS"), 0x03, 0x00},
    {SET_CMD("ME"), 0x02, 0x00},
    {SET_CMD("MODE"), 0x02, 0x01},
    {SET_CMD("MSG"), 0x02, 0x00},
    {SET_CMD("NAMES"), 0x03, 0x00},
    {SET_CMD("NICK"), 0x02, 0x01},
    {SET_CMD("NOTICE"), 0x02, 0x00},
    {SET_CMD("PART"), 0x02, 0x00},
    {SET_CMD("PASS"), 0x02, 0x00},
    {SET_CMD("PING"), 0x03, 0x00},
    {SET_CMD("PONG"), 0x02, 0x00},
    {SET_CMD("PRIVMSG"), 0x02, 0x00},
    {SET_CMD("PROP"), 0x02, 0x00},
    {SET_CMD("QUIT"), 0x02, 0x00},
    {SET_CMD("QUOTE"), 0x03, 0x00},
    {SET_CMD("RAW"), 0x03, 0x00},
    {SET_CMD("REPLY"), 0x02, 0x00},
    {SET_CMD("REQUEST"), 0x02, 0x00},
    {SET_CMD("SERVER"), 0x01, 0x00},
    {SET_CMD("SOUND"), 0x02, 0x00},
    {SET_CMD("THINK"), 0x02, 0x00},
    {SET_CMD("TOPIC"), 0x03, 0x01},
    {SET_CMD("UNKLINE"), 0x03, 0x00},
    {SET_CMD("USER"), 0x03, 0x00},
    {SET_CMD("USERHOST"), 0x03, 0x01},
    {SET_CMD("WHISPER"), 0x02, 0x00},
    {SET_CMD("WHO"), 0x02, 0x00},
    {SET_CMD("WHOIS"), 0x03, 0x01}
};

const SYNTAX g_rgSyntax[g_uSyntaxCount] = {
    {cmdidCreate, 0, 4, {AT_CHANNEL, AT_CHANNELFLAGS,
                          AT_MAXMEMBER | AT_OPTIONAL,
                          AT_PASSWORD | AT_OPTIONAL, AT_NONE, AT_NONE}},
    {cmdidInvite, 0, 2, {AT_NICKNAME, AT_CHANNEL, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidIsOn, 0, 1, {AT_NICKNAME | AT_SPACEMULTIPLE, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidJoin, 0, 2, {AT_CHANNEL, AT_PASSWORD | AT_OPTIONAL, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidList, 0, 1, {AT_CHANNEL | AT_OPTIONAL | AT_COMMAMULTIPLE, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidKick, IDS_KICKMSG_SYNTAX, 3, {AT_CHANNEL, AT_NICKNAME,
                                        AT_REASON | AT_OPTIONAL | AT_COLON,
                                        AT_NONE, AT_NONE, AT_NONE}},
    {cmdidKill, IDS_KILLMSG_SYNTAX, 2, {AT_CHANNEL | AT_NICKNAME,
                                        AT_REASON | AT_OPTIONAL | AT_COLON,
                                        AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidMe, IDS_ME_SYNTAX, 1, {AT_MESSAGE, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidMsg, 0, 2, {AT_CHANNEL | AT_NICKNAME | AT_COMMAMULTIPLE,
                       AT_MESSAGE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidMode, 0, 6, {AT_CHANNEL, AT_CHANNELFLAGS | AT_OPTIONAL,
                        AT_MAXMEMBER | AT_OPTIONAL,
                        AT_NICKNAME | AT_OPTIONAL,
                        AT_NICKMASK | AT_OPTIONAL,
                        AT_PASSWORD | AT_OPTIONAL}},
    {cmdidMode, 0, 2, {AT_NICKNAME, AT_USERFLAGS | AT_OPTIONAL,
                        AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidNames, 0, 1, {AT_CHANNEL | AT_OPTIONAL | AT_COMMAMULTIPLE,
                         AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidNick, 0, 1, {AT_NICKNAME, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidPart, 0, 1, {AT_CHANNEL | AT_OPTIONAL, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidPrivMsg, IDS_PRIVMSG_SYNTAX, 2,
        {AT_CHANNEL | AT_NICKNAME | AT_COMMAMULTIPLE,
         AT_MESSAGE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidProp, IDS_PROPGET_SYNTAX, 2,
        {AT_CHANNEL, AT_PROPNAME | AT_COMMAMULTIPLE,
         AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidProp, IDS_PROPSET_SYNTAX, 3,
        {AT_CHANNEL, AT_PROPNAME,
         AT_PROPVALUE | AT_SHOWCOLON | AT_COLON | AT_OPTIONAL,
         AT_NONE, AT_NONE, AT_NONE}},
    {cmdidServer, 0, 1, {AT_SERVER | AT_NETWORK, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidSound, IDS_SOUND_SYNTAX, 3,
        {AT_CHANNEL | AT_NICKNAME, AT_SOUND,
         AT_MESSAGE | AT_OPTIONAL, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidThink, 0, 1, {AT_MESSAGE, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidTopic, IDS_SETTOPIC_SYNTAX, 2,
        {AT_CHANNEL, AT_COLON | AT_TOPIC, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidTopic, IDS_GETTOPIC_SYNTAX, 1,
        {AT_CHANNEL, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidUserHost, 0, 1,
        {AT_NICKNAME | AT_SPACEMULTIPLE, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}},
    {cmdidWhoIs, 0, 1,
        {AT_NICKMASK | AT_COMMAMULTIPLE, AT_NONE, AT_NONE, AT_NONE, AT_NONE, AT_NONE}}
};

#undef SET_CMD

SHORT NGetCmd(const QString& command)
{
    SHORT start = 0;
    SHORT end = cmdidMax - 1;
    do {
        const SHORT middle = static_cast<SHORT>((end - start) / 2 + start);
        const int comparison = command.compare(
            QString::fromLatin1(g_rgIrcCmd[middle].szCmd),
            Qt::CaseInsensitive);
        if (comparison == 0) return middle;
        if (start == end) break;
        if (comparison < 0) {
            end = middle;
        } else if (middle != start) {
            start = middle;
        } else {
            start = end;
        }
    } while (true);
    return -1;
}

void GetBanString(const QString& userName, const QString& hostName,
                  QString& ban)
{
    CIrcProto* protocol = GetIrcProto();
    if (!protocol || !protocol->IsIRCX()
        || userName.startsWith(QLatin1Char('~'))) {
        ban = QStringLiteral("*!*@%1").arg(hostName);
    } else {
        ban = QStringLiteral("*!%1@%2").arg(userName, hostName);
    }
}

BOOL CIrcSocket::bFreeModeCell(const QString* channel,
                              const QString* nickname)
{
    int userIndex = -1;
    int channelIndex = -1;
    LONG userRank = 0;
    LONG channelRank = 0;
    CCQuery* userQuery = nullptr;
    CCQuery* channelQuery = nullptr;

    if (nickname || (!nickname && !channel)) {
        userQuery = m_queries.FindQuery(ctSetUserMode, &userIndex,
                                        &userRank);
    }
    if (channel || (!nickname && !channel)) {
        channelQuery = m_queries.FindQuery(ctSetChannelMode, &channelIndex,
                                           &channelRank);
    }

    if (!userQuery || userQuery->GetQueryPurpose() != qpComSetUserMode)
        userRank = 0;
    if (!channelQuery
        || channelQuery->GetQueryPurpose() != qpComSetChannelMode) {
        channelRank = 0;
    }

    if (userRank && (!channelRank || userRank < channelRank)) {
        return m_queries.FreeRemoveAt(userIndex);
    }
    if (channelRank && (!userRank || channelRank < userRank)) {
        return m_queries.FreeRemoveAt(channelIndex);
    }
    return FALSE;
}

void CIrcSocket::HandleCommand(QString& displayLine,
                               const QString& sourceLine,
                               IRCPARSE* parse, CIrcPrint* ircPrint)
{
    if (!parse || !ircPrint || parse->args.isEmpty()) return;
    const SHORT command = NGetCmd(parse->args[0]);
    switch (command) {
    case cmdidReply:
    case cmdidRequest:
        break;
    case cmdidAuth:
        // The source handles this through SSPI. Auth 2/3 remains disabled at
        // this named platform boundary and must not report local success.
        if (parse->nArgs >= 3) ircPrint->SetFormat(PT_NONE);
        break;
    case cmdidData:
        ircPrint->SetFormat(PT_NONE);
        break;
    case cmdidClone:
        ircPrint->SetFormat(PT_OFFSET, sourceLine, RGB(0, 0, 0), 1, TRUE);
        break;
    case cmdidKnock:
        ircPrint->SetFormat(PT_WHOLESTRING, sourceLine,
                            RGB(0, 0, 0), 0, TRUE);
        break;
    case cmdidPong:
        ircPrint->SetFormat(PT_OFFSET, sourceLine, RGB(0, 0, 0), 1, TRUE);
        break;
    case cmdidKilled:
        ircPrint->SetFormat(PT_WHOLESTRING, sourceLine,
                            RGB(0, 0, 255), 0, TRUE);
        break;
    case cmdidNotice:
    case cmdidPrivMsg:
        ircPrint->SetFormat(PT_NONE);
        if (parse->nick.isEmpty() && parse->user.isEmpty()) {
            ircPrint->SetFormat(PT_LASTSTRING, sourceLine,
                                RGB(128, 0, 128));
        }
        break;
    case cmdidProp:
        ircPrint->SetFormat(PT_OFFSET, sourceLine,
                            RGB(0, 0, 0), 2);
        break;
    default:
        break;
    }
    Q_UNUSED(displayLine);
}

void CIrcSocket::HandleResultCode(QString& displayLine,
                                  const QString& sourceLine,
                                  IRCPARSE* parse, CIrcPrint* ircPrint)
{
    if (!parse || !ircPrint || !parse->uCode) return;
    switch (parse->uCode) {
    case RPL_YOURHOST:
    case RPL_CREATED:
        ircPrint->SetFormat(PT_LASTSTRING, sourceLine, RGB(255, 0, 0));
        break;
    case RPL_MYINFO:
    case RPL_FOOFORNOW:
        ircPrint->SetFormat(PT_OFFSET, sourceLine,
                            RGB(255, 0, 0), 3, TRUE);
        break;
    case RPL_TRACELINK:
    case RPL_TRACECONNECTING:
    case RPL_TRACEHANDSHAKE:
    case RPL_TRACEUNKNOWN:
    case RPL_TRACEOPERATOR:
    case RPL_TRACEUSER:
    case RPL_TRACESERVER:
    case RPL_TRACENEWTYPE:
    case RPL_TRACELOG:
    case RPL_STATSLINKINFO:
    case RPL_STATSCOMMANDS:
    case RPL_STATSCLINE:
    case RPL_STATSNLINE:
    case RPL_STATSILINE:
    case RPL_STATSKLINE:
    case RPL_STATSYLINE:
    case RPL_ENDOFSTATS:
    case RPL_STATSLLINE:
    case RPL_STATSUPTIME:
    case RPL_STATSOLINE:
    case RPL_STATSHLINE:
    case RPL_ADMINME:
        ircPrint->SetFormat(PT_OFFSET, sourceLine, RGB(0, 0, 0), 3,
                            parse->uCode == RPL_ENDOFSTATS);
        break;
    case RPL_ADMINLOC1:
    case RPL_ADMINLOC2:
    case RPL_ADMINEMAIL:
        ircPrint->SetFormat(PT_LASTSTRING, sourceLine,
                            RGB(0, 0, 0), 0, FALSE);
        break;
    case RPL_USERHOST:
        displayLine = originalResourceString(
            QStringLiteral("IDS_USERHOST_PREFIX"));
        displayLine.replace(QStringLiteral("%s"), parse->lastString);
        ircPrint->SetFormat(PT_WHOLESTRING, displayLine,
                            RGB(128, 0, 128), 0, TRUE);
        break;
    case RPL_ISON:
        displayLine = originalResourceString(
            QStringLiteral("IDS_ISON_PREFIX"));
        displayLine.replace(QStringLiteral("%s"), parse->lastString);
        ircPrint->SetFormat(PT_WHOLESTRING, displayLine,
                            RGB(0, 0, 255), 0, TRUE);
        break;
    case RPL_WHOISUSER:
        if (parse->nArgs >= 5) {
            ircPrint->SetFormat(m_queries.FindQuery(ctWhoIs)
                                    ? PT_NONE : PT_OFFSET,
                                sourceLine, RGB(0, 0, 128), 3);
        }
        break;
    case RPL_WHOISSERVER:
    case RPL_WHOISOPERATOR:
    case RPL_WHOISIDLE:
    case RPL_WHOISCHANNELS:
    case RPL_WHOISIP:
        ircPrint->SetFormat(m_queries.FindQuery(ctWhoIs)
                                ? PT_NONE : PT_OFFSET,
                            sourceLine, RGB(0, 0, 128), 3);
        break;
    case RPL_ENDOFWHOIS:
        if (parse->nArgs >= 3) {
            ircPrint->SetFormat(m_queries.FindQuery(ctWhoIs)
                                    ? PT_NONE : PT_OFFSET,
                                sourceLine, RGB(0, 0, 128), 3, TRUE);
        }
        break;
    case RPL_WHOWASUSER:
    case RPL_ENDOFWHOWAS:
        ircPrint->SetFormat(PT_OFFSET, sourceLine, RGB(0, 0, 128), 3,
                            parse->uCode == RPL_ENDOFWHOWAS);
        break;
    case RPL_LINKS:
    case RPL_ENDOFLINKS:
        ircPrint->SetFormat(PT_OFFSET, sourceLine, RGB(0, 0, 0), 3,
                            parse->uCode == RPL_ENDOFLINKS);
        break;
    case RPL_INFO:
    case RPL_ENDOFINFO:
    case RPL_VERSION:
    case RPL_TIME:
        ircPrint->SetFormat(PT_OFFSET, sourceLine, RGB(128, 0, 0), 3,
                            parse->uCode != RPL_INFO);
        break;
    case RPL_YOUREOPER:
    case RPL_YOUREADMIN:
        ircPrint->SetFormat(PT_OFFSET, sourceLine,
                            RGB(0, 0, 255), 2, TRUE);
        break;
    case RPL_CHANNELMODEIS:
        if (parse->nArgs >= 4) {
            ircPrint->SetFormat(m_queries.FindQuery(ctGetChannelMode)
                                    ? PT_NONE : PT_OFFSET,
                                sourceLine, RGB(0, 0, 0), 3);
        }
        break;
    case RPL_NOTOPIC:
        ircPrint->SetFormat(m_queries.FindQuery(ctTopic)
                                ? PT_NONE : PT_OFFSET,
                            sourceLine, RGB(0, 0, 128), 3, TRUE);
        break;
    case RPL_NAMEREPLY:
        ircPrint->SetFormat(m_queries.FindQuery(ctNames)
                                ? PT_NONE : PT_OFFSET,
                            sourceLine, RGB(128, 128, 0), 4);
        break;
    case RPL_ENDOFNAMES:
        ircPrint->SetFormat(m_queries.FindQuery(ctNames)
                                ? PT_NONE : PT_OFFSET,
                            sourceLine, RGB(128, 128, 0), 3, TRUE);
        break;
    case RPL_WHOREPLY:
        if (parse->nArgs >= 8) {
            ircPrint->SetFormat(m_queries.FindQuery(ctWho)
                                    ? PT_NONE : PT_OFFSET,
                                sourceLine, RGB(0, 128, 128), 3);
        }
        break;
    case RPL_ENDOFWHO:
        ircPrint->SetFormat(m_queries.FindQuery(ctWho)
                                ? PT_NONE : PT_OFFSET,
                            sourceLine, RGB(0, 128, 128), 3, TRUE);
        break;
    case RPL_IRCX:
        ircPrint->SetFormat((m_queries.FindQuery(ctModeIsIrcX)
                             || m_queries.FindQuery(ctIrcX))
                                ? PT_NONE : PT_OFFSET,
                            sourceLine, RGB(0, 0, 0), 3, TRUE);
        break;
    case RPL_ACCESSADD:
    case RPL_ACCESSDELETE:
    case RPL_ACCESSSTART:
    case RPL_ACCESSLIST:
    case RPL_ACCESSEND:
    case RPL_EVENTADD:
    case RPL_EVENTDEL:
    case RPL_EVENTSTART:
    case RPL_EVENTLIST:
    case RPL_EVENTEND:
        ircPrint->SetFormat(PT_OFFSET, sourceLine, RGB(0, 0, 0), 3,
                            parse->uCode != RPL_ACCESSLIST
                                && parse->uCode == RPL_EVENTLIST);
        break;
    case RPL_PROPLIST:
        if (parse->nArgs >= 4) {
            ircPrint->SetFormat(m_queries.FindQuery(ctPropGet)
                                    ? PT_NONE : PT_OFFSET,
                                sourceLine, RGB(0, 0, 0), 3, TRUE);
        }
        break;
    case RPL_PROPEND:
        if (parse->nArgs >= 3)
            ircPrint->SetFormat(PT_NONE);
        break;
    default:
        break;
    }
}

namespace {
void showIrcSourceMessage(const QString& message)
{
    QMessageBox::information(
        theApp.m_pMainWnd.data(),
        originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
        message);
}

QString formattedIrcResource(const QString& identifier,
                             const QString& value)
{
    QString message = originalResourceString(identifier);
    message.replace(QStringLiteral("%s"), value);
    return message;
}
}

void CIrcSocket::HandleErrorCode(const QString& sourceLine,
                                 IRCPARSE* parse, CIrcPrint* ircPrint)
{
    if (!parse || !ircPrint || !parse->uCode) return;
    bool displayErrorInStatusWindow = false;
    bool hasChannelName = false;
    QString channelName;
    int roomIndex = -1;
    CRoomInfo* enterInfo = nullptr;

    switch (parse->uCode) {
    default:
        displayErrorInStatusWindow = true;
        break;
    case ERR_NOSUCHNICK: {
        const QString object = parse->args.value(2);
        const bool isChannel = !object.isEmpty()
            && (object.front() == QLatin1Char('#')
                || object.front() == QLatin1Char('%')
                || object.front() == QLatin1Char('&'));
        if (isChannel) {
            showIrcSourceMessage(formattedIrcResource(
                QStringLiteral("IDS_ERR_NOSUCHCHANNEL"),
                DecodeChan(object)));
        } else {
            showIrcSourceMessage(formattedIrcResource(
                QStringLiteral("IDS_ERR_NOSUCHNICK"),
                DecodeNick(object)));
            bFreeModeCell(nullptr, &object);
        }
        break;
    }
    case ERR_NOSUCHCHANNEL: {
        const QString object = parse->args.value(2);
        enterInfo = theApp.GetRoomInfoFromName(object, &roomIndex,
                                               false, true);
        if (enterInfo) {
            ShowBadChannelName(object);
        } else {
            int queryIndex = -1;
            CCQuery* query = m_queries.FindQuery(ctTopic, &queryIndex);
            QString message;
            roomIndex = 0;
            if (query && query->GetQueryPurpose() == qpListMembers
                && query->GetChannelName() == object) {
                m_queries.FreeRemoveAt(queryIndex);
                message = formattedIrcResource(
                    QStringLiteral("IDS_ERR_NOSUCHCHANNELANYMORE"),
                    DecodeChan(object));
                if (theApp.m_pRoomList)
                    theApp.m_pRoomList->ReenableListMembers();
            } else {
                message = formattedIrcResource(
                    QStringLiteral("IDS_ERR_NOSUCHCHANNEL"),
                    DecodeChan(object));
                bFreeModeCell(&object, &object);
            }
            showIrcSourceMessage(message);
        }
        break;
    }
    case ERR_TOOMANYCHANNELS:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_ERR_TOOMANYCHANNELS")));
        break;
    case ERR_NOMOTD: {
        const QString message = originalResourceString(IDS_ERR_NOMOTD);
        CIrcPrint motdPrint;
        motdPrint.SetFormat(PT_WHOLESTRING, message,
                            RGB(0, 0, 255), 0, TRUE);
        AddToStatus(motdPrint, message);
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(ctLUsersMOTD, &queryIndex);
        if (query) {
            if ((query->GetQueryPurpose() == qpLUsersMOTD
                 || (theApp.m_flags1 & F1_SHOWMOTD))
                && !m_strLUSER.isEmpty()) {
                ShowMOTD(m_strLUSER, QString());
            }
            m_queries.FreeRemoveAt(queryIndex);
        }
        m_strLUSER.clear();
        theApp.m_bDisableMOTD = false;
        break;
    }
    case ERR_NONICKNAMEGIVEN:
    case ERR_ERRONEUSNICKNAME:
    case ERR_NICKNAMEINUSE: {
        const int argumentIndex = parse->uCode == ERR_NICKNAMEINUSE ? 2 : 1;
        const QString badNickname = parse->nArgs >= argumentIndex + 1
            ? parse->args.value(argumentIndex) : QStringLiteral("");
        if (CIrcProto* protocol = m_proto ? m_proto : GetIrcProto()) {
            protocol->TryNewNick(
                parse->uCode == ERR_NICKNAMEINUSE
                    ? ID_ERR_DUPED_NICK : ID_ERR_BAD_NICK,
                m_bIrcXServer ? DecodeNick(badNickname) : badNickname);
        }
        break;
    }
    case ERR_NICKCOLLISION:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_ERR_NICKCOLLISION")));
        break;
    case ERR_NICKTOOFAST:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_ERR_NICKTOOFAST")));
        break;
    case ERR_NICKNOCHANGE:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_ERR_NICKNOCHANGE")));
        break;
    case ERR_NOTONCHANNEL: {
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(ctTopic, &queryIndex);
        if (query && query->GetQueryPurpose() == qpListMembers) {
            const QString encodedRoom = query->GetChannelName();
            const QString prettyRoom = query->GetData()
                ? *static_cast<QString*>(query->GetData()) : QString();
            m_queries.FreeRemoveAt(queryIndex);
            OnUserListAux(QString(), encodedRoom, prettyRoom);
        } else {
            const QString object = parse->args.value(2);
            bFreeModeCell(&object, nullptr);
            displayErrorInStatusWindow = true;
        }
        break;
    }
    case ERR_NOTREGISTERED:
        HrModeIsIrcXFailure();
        break;
    case ERR_NEEDMOREPARAMS:
        bFreeModeCell(nullptr, nullptr);
        displayErrorInStatusWindow = true;
        break;
    case ERR_PASSWDMISMATCH:
        HrIrcSetOper(m_pszUserName);
        break;
    case ERR_YOUREBANNEDCREEP:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_ERR_YOUREBANNEDCREEP")));
        break;
    case ERR_YOUWILLBEBANNED:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_ERR_YOUWILLBEBANNED")));
        break;
    case ERR_KEYSET: {
        const QString object = parse->args.value(2);
        bFreeModeCell(&object, nullptr);
        displayErrorInStatusWindow = true;
        break;
    }
    case ERR_CHANNELISFULL:
        showIrcSourceMessage(formattedIrcResource(
            QStringLiteral("ID_ERR_CHANNELISFULL"),
            DecodeChan(parse->args.value(2))));
        break;
    case ERR_UNKNOWNMODE:
        bFreeModeCell(nullptr, nullptr);
        displayErrorInStatusWindow = true;
        break;
    case ERR_INVITEONLYCHAN:
        showIrcSourceMessage(formattedIrcResource(
            QStringLiteral("ID_ERR_INVITEONLY"),
            DecodeChan(parse->args.value(2))));
        break;
    case ERR_BANNEDFROMCHAN:
        showIrcSourceMessage(formattedIrcResource(
            QStringLiteral("ID_ERR_BANNEDFROMCHAN"),
            DecodeChan(parse->args.value(2))));
        break;
    case ERR_BADCHANNELKEY:
        enterInfo = theApp.GetRoomInfoFromName(parse->args.value(2),
                                               &roomIndex);
        if (enterInfo) OnBadChannelPassword(*enterInfo);
        break;
    case ERR_CHANOPRIVSNEEDED: {
        const QString object = parse->args.value(2);
        if (!bFreeModeCell(&object, nullptr)) {
            int queryIndex = -1;
            CCQuery* query = m_queries.FindQuery(ctTopic, &queryIndex);
            if (query && query->GetQueryPurpose() == qpSetTopic)
                m_queries.FreeRemoveAt(queryIndex);
        }
        displayErrorInStatusWindow = true;
        break;
    }
    case ERR_UMODEUNKNOWNFLAG:
    case ERR_USERSDONTMATCH: {
        const QString emptyNickname;
        bFreeModeCell(nullptr, &emptyNickname);
        displayErrorInStatusWindow = true;
        break;
    }
    case ERR_NOJOINDYNAMIC:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_ERR_NOJOINDYNAMIC")));
        break;
    case ERR_NODYNAMICCHANNELS:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_ERR_NODYNAMICCHANNELS")));
        break;
    case ERR_AUTHONLY:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_ERR_AUTHONLY")));
        break;
    case ERR_BADFUNCTION:
        if (!m_bIrcXServer) {
            showIrcSourceMessage(originalResourceString(
                QStringLiteral("IDS_ERR_NODYNAMICCHANNELS")));
        }
        break;
    case ERR_BADTAG:
        if (!m_bIrcXServer) {
            showIrcSourceMessage(originalResourceString(
                QStringLiteral("IDS_ERR_AUTHONLY")));
        }
        break;
    case ERR_BADPROPERTY:
        if (!m_bIrcXServer) {
            showIrcSourceMessage(originalResourceString(
                QStringLiteral("IDS_ERR_NICKNOCHANGE")));
        }
        break;
    case ERR_RESOURCE:
        if (!m_bIrcXServer) {
            showIrcSourceMessage(originalResourceString(
                QStringLiteral("IDS_ERR_NOJOINDYNAMIC")));
        }
        break;
    case ERR_AUTHENTICATIONFAILED:
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("ID_ERR_BADUSERINFO")));
        m_bAuthFailed = TRUE;
        // HrIrcXLogin's SSPI package retry remains disabled at the same
        // platform boundary as AUTH; this does not report local success.
        break;
    case ERR_UNKNOWNPACKAGE:
        // The rejected package itself remains unavailable, but the original
        // package-order state and eventual ANON/failure branch still apply.
        HrIrcXLogin(TRUE);
        break;
    case ERR_NOSUCHOBJECT: {
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(ctPropGet, &queryIndex);
        if (query) {
            if ((query->GetQueryPurpose() == qpJoinPics
                 || query->GetQueryPurpose() == qpCreatePics)
                && query->GetChannelName() == parse->args.value(2)) {
                if (bCanViewUnrated(TRUE)) {
                    enterInfo = theApp.GetRoomInfoFromName(
                        parse->args.value(2));
                    if (m_proto && enterInfo) {
                        if (query->GetQueryPurpose() == qpJoinPics)
                            m_proto->ChatJoinAux(*enterInfo);
                        else
                            m_proto->ChatCreateAux(*enterInfo);
                    }
                }
                m_queries.FreeRemoveAt(queryIndex);
            }
        } else {
            displayErrorInStatusWindow = true;
        }
        break;
    }
    }

    switch (parse->uCode) {
    case ERR_USERONCHANNEL:
        channelName = parse->args.value(3);
        hasChannelName = true;
        break;
    case ERR_NOSUCHNICK:
    case ERR_INVITEONLYCHAN:
    case ERR_CHANNELISFULL:
    case ERR_NOSUCHCHANNEL:
    case ERR_BANNEDFROMCHAN:
    case ERR_BADCHANNELKEY:
    case ERR_TOOMANYCHANNELS:
        channelName = parse->args.value(2);
        hasChannelName = true;
        break;
    default:
        break;
    }

    if (m_bIrcXServer) {
        switch (parse->uCode) {
        case ERR_NOJOINDYNAMIC:
        case ERR_NODYNAMICCHANNELS:
        case ERR_AUTHONLY:
        case ERR_CHANNELEXIST:
            channelName = parse->args.value(2);
            hasChannelName = true;
            break;
        case ERR_NOACCESS:
            if (parse->args.value(2) != QLatin1String("*")) {
                channelName = parse->args.value(2);
                hasChannelName = true;
            }
            break;
        default:
            break;
        }
    } else {
        switch (parse->uCode) {
        case ERR_CANNOTJOINMICONLY:
        case ERR_CANNOTJOINFROMREMOTE:
        case ERR_CANNOTCREATEDYNAMIC:
        case ERR_ONLYAUTHCANJOIN:
        case ERR_CANNOTJOINDYNAMIC:
            channelName = parse->args.value(2);
            hasChannelName = true;
            break;
        default:
            break;
        }
    }

    if (hasChannelName) {
        if (roomIndex == -1) {
            enterInfo = theApp.GetRoomInfoFromName(channelName, &roomIndex,
                                                   false);
        }
        if (enterInfo && roomIndex > 0)
            theApp.RemoveRoomInfo(roomIndex);
    }

    if (displayErrorInStatusWindow) {
        ircPrint->SetFormat(PT_OFFSET, sourceLine,
                            RGB(255, 0, 0), 3, TRUE);
    } else {
        ircPrint->SetFormat(PT_NONE);
    }
}

namespace {
QString g_strBan;
QStringList g_arrayBans;

QString sourceCodePageCarrier(QStringView text)
{
    QByteArray bytes;
    if (!bWideToCodePage(text, GetACP(), &bytes))
        bytes = text.toString().toLatin1();
    return QString::fromLatin1(bytes);
}

bool channelPrefix(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    const QChar ch = value.front();
    return ch == QLatin1Char('#') || ch == QLatin1Char('%') || ch == QLatin1Char('&');
}

QString identFromParse(const IRCPARSE& parse)
{
    if (parse.user.isEmpty() || parse.machine.isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1@%2").arg(parse.user, parse.machine);
}

bool isAppearsAsMessage(const QString& message)
{
    return message.startsWith(QStringLiteral("# Appears as "));
}

QString setRoomTopic(CChatDoc* doc, const QString& controlFullTopic,
                     CDWordArray* copiedFormatting = nullptr)
{
    QByteArray bytes = controlFullTopic.toUtf8();
    CDWordArray formatting;
    char* controlLess = SzControlLess(bytes.data(), &formatting);
    const QString topic = controlLess
        ? QString::fromUtf8(controlLess) : QString();
    if (copiedFormatting) {
        copiedFormatting->RemoveAll();
        for (int index = 0; index < formatting.GetSize(); ++index)
            copiedFormatting->Add(formatting.GetAt(index));
    }
    if (doc && doc->m_proto) {
        FreeAndNullFormatting(&doc->m_proto->m_prgdwTopicFormatting);
        doc->m_proto->m_prgdwTopicFormatting = CopyFormatting(&formatting);
        doc->m_proto->m_strTopic = topic;
    }
    return topic;
}

void ParseChannelMode(CChatDoc* doc, const QString& flags,
                      const QString& arg2, const QString& arg3,
                      CRoomInfo* enterRoom = nullptr)
{
    if (!doc || !doc->m_proto) return;
    bool add = true;
    DWORD delta = 0;
    DWORD addFlags = 0;
    DWORD subFlags = 0;
    for (QChar flag : flags) {
        switch (flag.toLatin1()) {
        case '+':
            add = true;
            delta = 0;
            break;
        case '-':
            add = false;
            delta = 0;
            break;
        case 'p': delta |= CM_PRIVATE; break;
        case 's': delta |= CM_HIDDEN; break;
        case 'i': delta |= CM_INVITEONLY; break;
        case 't': delta |= CM_TOPICHOST; break;
        case 'n': delta |= CM_NOEXTERN; break;
        case 'm': delta |= CM_MODERATED; break;
        case 'l':
            delta |= CM_USERLIMIT;
            doc->m_proto->m_dwMaxUsers = add ? arg2.toULong() : 0;
            break;
        case 'k':
            delta |= CM_CHANNELKEY;
            doc->m_proto->m_strPassword = add
                ? (arg3.isEmpty() ? arg2 : arg3) : QString();
            break;
        case 'q':
            ChatChangeAdmin(doc, arg2,
                            add ? UF_OWNER | UF_OPERATOR : 0,
                            add ? 0 : UF_OWNER);
            break;
        case 'o':
            ChatChangeAdmin(doc, arg2, add ? UF_OPERATOR : 0,
                            add ? 0 : UF_OPERATOR);
            break;
        case 'v':
            ChatChangeAdmin(doc, arg2, add ? UF_HASVOICE : 0,
                            add ? 0 : UF_HASVOICE);
            break;
        case 'f':
            if (CRoomInfo* defaultProto = GetDefaultProto();
                defaultProto && defaultProto->IsIRCX()) {
                delta |= CM_NOFORMAT;
                if (add) {
                    theApp.m_bSaveViewMode = false;
                    doc->OnViewText();
                }
            }
            break;
        case 'y':
            delta |= CM_MIC;
            if (add && enterRoom) FixMICChannelName(doc, enterRoom);
            break;
        default: break;
        }
        if (add) addFlags |= delta;
        else subFlags |= delta;
    }
    doc->m_proto->m_dwModes |= addFlags;
    doc->m_proto->m_dwModes &= ~subFlags;
    if ((addFlags | subFlags) & CM_MODERATED) {
        UpdateSpectators(doc,
                         (doc->m_proto->m_dwModes & CM_MODERATED) != 0);
    }
    if (theApp.m_pMainWnd
        && theApp.m_pMainWnd->GetActiveDocument() == doc) {
        theApp.m_pMainWnd->RefreshCommandUi();
    }
}
}

void CSInString(QString* string, const QString& channelName, CChatDoc* doc)
{
    if (!string) return;
    int encoding = channelName.startsWith(QLatin1Char('%'))
        ? ENC_UTF8 : ENC_DBCS;
    if (doc && doc->m_proto && (doc->m_proto->m_dwModes & CM_MIC)) {
        encoding = ENC_DBCS;
    }
    if (string->isEmpty()) return;
    *string = DecodeString(string->toLatin1(), encoding);
}

void CSInPlace(QString* nickname)
{
    if (!nickname || nickname->isEmpty()) return;
    CSInString(nickname);
}

CIrcSocket::CIrcSocket(CIrcProto* proto)
    : m_proto(proto)
    , m_ircXTimer(new QTimer)
{
    bindSocket(new QTcpSocket);
    m_ircXTimer->setSingleShot(true);
    m_ircXTimer->setInterval(50000);
    QObject::connect(m_ircXTimer, &QTimer::timeout, m_ircXTimer,
                     [this] { HrModeIsIrcXFailure(); });
}

CIrcSocket::~CIrcSocket()
{
    delete m_pendingListXRoom;
    if (m_socket) QObject::disconnect(m_socket, nullptr, nullptr, nullptr);
    delete m_socket;
    delete m_ircXTimer;
}

void CIrcSocket::bindSocket(QTcpSocket* socket)
{
    m_socket = socket;
    if (!m_socket) return;
    QObject::connect(m_socket, &QTcpSocket::connected, m_socket,
                     [this] { handleConnected(); });
    QObject::connect(m_socket, &QTcpSocket::readyRead, m_socket,
                     [this] { handleReadyRead(); });
    QObject::connect(m_socket, &QTcpSocket::disconnected, m_socket,
                     [this] { handleDisconnected(); });
    QObject::connect(
        m_socket, &QTcpSocket::errorOccurred, m_socket,
        [this](QAbstractSocket::SocketError) {
            if (m_socket) theApp.SetStatusPaneString(
                0, m_socket->errorString());
        });
}

void CIrcSocket::AttachProtocol(CIrcProto* proto)
{
    // The original socket resolves callbacks through the one process-wide
    // default IRC protocol. Keep the first attached protocol as that target;
    // room protocols still share this socket and are selected by LookupDoc.
    if (!m_proto) m_proto = proto;
}

void CIrcSocket::DetachProtocol(CIrcProto* proto)
{
    if (m_proto == proto) m_proto = nullptr;
}

bool CIrcSocket::Connect(const QString& server, quint16 port)
{
    m_bRegistered = false;
    if (!m_socket) bindSocket(new QTcpSocket);
    m_socket->connectToHost(server, port);
    return true;
}

void CIrcSocket::AdoptSocket(QTcpSocket* socket)
{
    if (!socket || socket == m_socket) return;
    if (m_socket) {
        QObject::disconnect(m_socket, nullptr, nullptr, nullptr);
        m_socket->abort();
        delete m_socket;
    }
    m_bRegistered = false;
    m_bDisconnectRequested = false;
    bindSocket(socket);
}

void CIrcSocket::Disconnect()
{
    theApp.m_bInSearch = false;
    delete m_pendingListXRoom;
    m_pendingListXRoom = nullptr;
    m_bDisconnectRequested = true;
    m_socket->disconnectFromHost();
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        m_bDisconnectRequested = false;
    }
}

void CIrcSocket::Reset()
{
    m_nSecuPackIndex = -1;
    m_bIrcXServer = false;
    m_bRegistered = false;
    m_bAnonAllowed = FALSE;
    m_bAuthFailed = FALSE;
    m_bJustSentModeIsIrcX = false;
    m_rgszSvrSecuPack.clear();
    m_ircXTimer->stop();
    m_pending.clear();
}

void CIrcSocket::SendRaw(const QString& raw)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(raw.toLatin1());
    }
}

void CIrcSocket::SendRaw(const QByteArray& raw)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(raw);
    }
}

quint32 CIrcSocket::LocalIPv4Address() const
{
    if (!m_socket) return 0;
    bool ok = false;
    const quint32 address = m_socket->localAddress().toIPv4Address(&ok);
    return ok ? address : 0;
}

void CIrcSocket::handleConnected()
{
    OnConnect();
}

void CIrcSocket::OnConnect()
{
    if (m_proto
        && m_proto->bExecuteQuery(qpIsIrcX, ctModeIsIrcX, dtMax, nullptr,
                                  QString(), QString())) {
        m_bJustSentModeIsIrcX = true;
        m_ircXTimer->start();
    }
}

void CIrcSocket::handleDisconnected()
{
    theApp.m_bInSearch = false;
    delete m_pendingListXRoom;
    m_pendingListXRoom = nullptr;
    const bool requested = m_bDisconnectRequested;
    m_bDisconnectRequested = false;
    if (requested) {
        return;
    }

    if (theApp.m_SrvConnector.IsConnecting()
        && theApp.m_SrvConnector.GetNumServers() > 1) {
        ChatServerDisconnect(FALSE, TRUE);
        theApp.ResumeConnection();
        return;
    }

    ChatServerDisconnect(TRUE, FALSE);
    if (theApp.m_pMainWnd) {
        QMessageBox::information(
            theApp.m_pMainWnd.data(),
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
            originalResourceString(
                QStringLiteral("IDS_CONNECTION_DROPPED")));
    }
}

BOOL CIrcSocket::PromptForPassword(const QString& userName,
                                   BOOL saveInSettings)
{
    CChatServerGroup* group =
        theApp.m_SrvConnector.GetConnectingServerGroup();
    CChatServer* server = theApp.m_SrvConnector.GetConnectingServer();
    const BOOL remember = server ? server->m_bRememberPassword : FALSE;
    CChatPasswordDialog dialog(QString::fromUtf8(GetMyPhysicalServer()),
                               userName, remember,
                               theApp.m_pMainWnd.data());
    if (dialog.exec() != QDialog::Accepted) return FALSE;
    m_pszPassword = dialog.m_strPassword;
    if (saveInSettings && server && group) {
        server->m_pszPassword = m_pszPassword;
        server->m_bRememberPassword = dialog.m_bRememberPassword;
        server->m_nAuthenticationType =
            CChatServer::authtypePlainText;
        server->WriteToRegistry();
    }
    return TRUE;
}

void CIrcSocket::SetAuthentication(UINT type, const QString& userName,
                                   const QString& password,
                                   const QString& customPackages)
{
    m_rgszUsrSecuPack.clear();
    m_nAuthenticationType = type;
    m_bAnonAllowed = FALSE;
    m_pszUserName = userName.isEmpty() ? QString() : userName;
    m_pszPassword = password.isEmpty() ? QString() : password;
    if (!customPackages.isEmpty()
        && m_nAuthenticationType
            == CChatServer::authtypeCustomPackages) {
        m_rgszUsrSecuPack = customPackages.split(
            QLatin1Char(','), Qt::KeepEmptyParts);
    }
}

bool CIrcSocket::HrIrcLogin(bool ircX, const QString& nickname,
                            const QString& userName, const QString& realName,
                            const QString& password,
                            bool promptForPassword)
{
    const QString nick = nickname.isNull() ? QString::fromUtf8(GetMyName()) : nickname;
    if (nick.isEmpty()) {
        return false;
    }
    const QString real = realName.isNull() ? QString::fromUtf8(GetMyRealName()) : realName;
    QString user = userName.isNull()
        ? (!m_pszUserName.isNull() ? m_pszUserName
                                   : QString::fromUtf8(GetMyUserName()))
        : userName;
    if (user.isEmpty()) {
        user = nick;
    }
    user.remove(QLatin1Char(' '));

    QString loginPassword = password.isNull() ? m_pszPassword : password;
    if (loginPassword.isEmpty() && promptForPassword
        && m_nAuthenticationType != CChatServer::authtypeNone) {
        PromptForPassword(user, TRUE);
        loginPassword = m_pszPassword;
    }

    if (!loginPassword.isEmpty()) {
        const QString pass = QStringLiteral("PASS %1\r\n").arg(
            sourceCodePageCarrier(QStringView(loginPassword)));
        if (m_proto) m_proto->SendMessageText(pass);
        else SendRaw(pass);
    }

    if (m_proto) {
        if (!m_proto->ChatChangeNick(nick)) {
            return false;
        }
    } else {
        SendRaw(QStringLiteral("NICK %1\r\n").arg(
            sourceCodePageCarrier(QStringView(nick))));
    }

    if (!m_bRegistered) {
        QString machineName = QHostInfo::localHostName();
        if (machineName.isEmpty()) {
            machineName = QString::fromLatin1(g_szNoMachine);
        }
        const QString registration = QStringLiteral("USER %1 %2 . :%3\r\n")
            .arg(sourceCodePageCarrier(QStringView(user)),
                 sourceCodePageCarrier(QStringView(machineName)),
                 sourceCodePageCarrier(QStringView(real)));
        if (m_proto) m_proto->SendMessageText(registration);
        else SendRaw(registration);
        m_bRegistered = true;
    }
    if (!ircX
        && m_nAuthenticationType == CChatServer::authtypePlainText
        && !m_pszUserName.isEmpty() && !m_pszPassword.isEmpty()) {
        HrIrcSetOper(m_pszUserName, m_pszPassword);
    }
    return true;
}

bool CIrcSocket::HrIrcSetOper(const QString& userName,
                              const QString& password)
{
    QString operPassword = password;
    if (operPassword.isEmpty()) {
        if (!PromptForPassword(userName, FALSE)) {
            return false;
        }
        operPassword = m_pszPassword;
    }

    const QString oper = QStringLiteral("OPER %1 %2\r\n")
        .arg(sourceCodePageCarrier(QStringView(userName)),
             sourceCodePageCarrier(QStringView(operPassword)));
    if (m_proto) m_proto->SendMessageText(oper);
    else SendRaw(oper);
    return true;
}

bool CIrcSocket::HrIrcXLogin(BOOL forceNextPackage)
{
    if (m_bAnonAllowed
        && (m_nAuthenticationType == CChatServer::authtypeNone
            || m_nAuthenticationType
                == CChatServer::authtypePlainText)) {
        return HrIrcLogin(true);
    }

    if (forceNextPackage) ++m_nSecuPackIndex;

    if (m_nAuthenticationType
        == CChatServer::authtypeCustomPackages) {
        while (m_nSecuPackIndex < m_rgszUsrSecuPack.size()) {
            const QString securityPackage =
                m_rgszUsrSecuPack.at(m_nSecuPackIndex);
            if (securityPackage.compare(QString::fromLatin1(g_szAnon),
                                        Qt::CaseInsensitive) == 0) {
                if (m_bAnonAllowed) return HrIrcLogin(true);
            }
            // HrAuthenticate is the Windows SSPI boundary. An unavailable
            // package follows the source failure path to the next package.
            ++m_nSecuPackIndex;
        }
    } else {
        // Server-package authentication is the same unavailable SSPI
        // boundary. Preserve source ordering, then use ANON only where the
        // server advertised it.
        m_nSecuPackIndex = static_cast<short>(m_rgszSvrSecuPack.size());
        if (m_bAnonAllowed) return HrIrcLogin(true);
    }

    const bool resumeConnection = theApp.m_SrvConnector.IsConnecting()
        && theApp.m_SrvConnector.GetNumServers() > 1;
    Disconnect();
    if (resumeConnection) {
        ChatServerDisconnect(FALSE, TRUE);
        theApp.ResumeConnection();
    } else {
        ChatServerDisconnect(TRUE, FALSE);
        showIrcSourceMessage(originalResourceString(
            QStringLiteral("IDS_CONNECTION_DROPPED")));
    }
    showIrcSourceMessage(originalResourceString(
        QStringLiteral("ID_ERR_NOAUTH")));
    if (theApp.m_pMainWnd) theApp.m_pMainWnd->CreateNewDocument();
    return false;
}

void CIrcSocket::HrModeIsIrcXFailure()
{
    if (!m_bJustSentModeIsIrcX) {
        return;
    }
    int index = -1;
    if (m_queries.FindQuery(ctModeIsIrcX, &index)) {
        m_queries.FreeRemoveAt(index);
    }
    m_bIrcXServer = false;
    HrIrcLogin(false);
    m_bJustSentModeIsIrcX = false;
    m_ircXTimer->stop();
}

void CIrcSocket::handleReadyRead()
{
    // Original OnReceive rechecks its shared input after ProcessMessage
    // because processing can receive more bytes reentrantly. readyRead is not
    // guaranteed to be emitted recursively, so drain bytes that arrived while
    // this slot was processing the preceding block.
    do {
        m_pending += m_socket->readAll();
        int index = -1;
        while ((index = m_pending.indexOf('\n')) >= 0) {
            QByteArray line = m_pending.left(index);
            m_pending.remove(0, index + 1);
            if (line.endsWith('\r')) {
                line.chop(1);
            }
            if (!line.isEmpty()) {
                ProcessMessageBytes(line);
            }
        }
    } while (m_socket->bytesAvailable() > 0);
}

namespace {
void ParseItBytes(const QByteArray& source, IRCPARSE* parse,
                  BOOL doubleQuotes, bool rawByteCarrier)
{
    if (!parse) return;
    *parse = IRCPARSE{};

    const qsizetype sourceSize = source.size();
    qsizetype body = 0;

    const auto isSpace = [&source](qsizetype index) {
        return index < source.size()
            && std::isspace(static_cast<unsigned char>(source.at(index)));
    };
    const auto decodeField = [rawByteCarrier](const QByteArray& bytes,
                                               qsizetype maximum,
                                               bool extendedIdentifier) {
        const QByteArray bounded = bytes.left(maximum);
        if (!rawByteCarrier) return QString::fromUtf8(bounded);
        Q_UNUSED(extendedIdentifier);
        return QString::fromLatin1(bounded);
    };

    if (sourceSize > 0 && source.at(0) == ':') {
        parse->bHasPrefix = TRUE;
        body = source.indexOf(' ', 1);
        if (body < 0) body = sourceSize;
        const QByteArray prefix = source.mid(1, std::min<qsizetype>(
            body - 1, 299));
        qsizetype separator = -1;
        if (!prefix.isEmpty()
            && prefix.at(0) != '#' && prefix.at(0) != '%'
            && prefix.at(0) != '&') {
            const qsizetype bang = prefix.indexOf('!');
            const qsizetype at = prefix.indexOf('@');
            if (bang >= 0 && at >= 0) separator = std::min(bang, at);
            else separator = std::max(bang, at);
        }
        if (separator >= 0) {
            parse->nick = decodeField(prefix.left(separator), 49, true);
            if (prefix.at(separator) == '!') {
                const qsizetype at = prefix.indexOf('@', separator + 1);
                if (at >= 0) {
                    parse->user = decodeField(
                        prefix.mid(separator + 1, at - separator - 1),
                        49, false);
                    parse->machine = decodeField(prefix.mid(at + 1), 49,
                                                 false);
                }
            }
        } else if (prefix.size() < 50) {
            parse->nick = decodeField(prefix, 49, true);
        }
    }

    const auto finishLastString = [&] {
        qsizetype end = body;
        while (end < sourceSize && source.at(end) != '\r'
               && source.at(end) != '\n') {
            ++end;
        }
        parse->lastString = decodeField(source.mid(body, end - body),
                                        end - body, false);
        parse->bHasLastString = TRUE;
    };

    while (true) {
        while (body < sourceSize && isSpace(body)) ++body;
        if (body >= sourceSize) break;
        if (source.at(body) == ':') {
            ++body;
            finishLastString();
            break;
        }

        qsizetype tokenStart = body;
        qsizetype tokenEnd = body;
        QByteArray token;
        if (doubleQuotes && source.at(body) == '"') {
            tokenEnd = body + 1;
            while (tokenEnd < sourceSize && source.at(tokenEnd) != '"'
                   && source.at(tokenEnd) != '\r'
                   && source.at(tokenEnd) != '\n') {
                ++tokenEnd;
            }
            token = source.mid(tokenStart,
                               std::min<qsizetype>(tokenEnd - tokenStart,
                                                   MAX_TOKEN - 1));
            body = tokenEnd;
            if (body < sourceSize && source.at(body) == '"') {
                ++body;
                if (token.size() < MAX_TOKEN - 1) token.append('"');
            }
        } else {
            while (tokenStart < sourceSize
                   && (isSpace(tokenStart) || source.at(tokenStart) == ' '
                       || source.at(tokenStart) == '\r'
                       || source.at(tokenStart) == '\n')) {
                ++tokenStart;
            }
            if (tokenStart >= sourceSize) break;
            tokenEnd = tokenStart;
            while (tokenEnd < sourceSize && !isSpace(tokenEnd)
                   && source.at(tokenEnd) != ' '
                   && source.at(tokenEnd) != '\r'
                   && source.at(tokenEnd) != '\n') {
                ++tokenEnd;
            }
            token = source.mid(tokenStart,
                               std::min<qsizetype>(tokenEnd - tokenStart,
                                                   MAX_TOKEN - 1));
            body = tokenEnd;
        }

        parse->nOffsets[parse->nArgs] = static_cast<SHORT>(tokenStart);
        parse->args.append(decodeField(token, token.size(), true));
        ++parse->nArgs;
        if (parse->nArgs == MAXARGS) {
            finishLastString();
            break;
        }
    }

    if (!parse->args.isEmpty()) {
        parse->command = parse->args.first().toUpper();
        const QByteArray first = parse->args.first().toLatin1();
        qsizetype index = 0;
        while (index < first.size()
               && std::isdigit(static_cast<unsigned char>(first.at(index)))) {
            parse->uCode *= 10;
            parse->uCode += first.at(index) - '0';
            ++index;
        }
    }
}
}

void ParseIt(const QString& message, IRCPARSE* parse, BOOL doubleQuotes)
{
    ParseItBytes(message.toUtf8(), parse, doubleQuotes, false);
}

void ParseIt(const QByteArray& message, IRCPARSE* parse, BOOL doubleQuotes)
{
    ParseItBytes(message, parse, doubleQuotes, true);
}

void CIrcSocket::ProcessMessage(const QString& line)
{
    ProcessMessageBytes(line.toUtf8());
}

void CIrcSocket::ProcessMessageBytes(const QByteArray& rawLine)
{
    const QString line = QString::fromLatin1(rawLine);
    IRCPARSE parse;
    ParseIt(rawLine, &parse);
    CIrcPrint ircPrint;
    struct StatusAtReturn {
        CIrcPrint& print;
        const QString& source;
        ~StatusAtReturn() { AddToStatus(print, source); }
    } statusAtReturn{ircPrint, line};

    if (parse.command == QLatin1String("PING")) {
        const QByteArray payload = !parse.lastString.isEmpty()
            ? parse.lastString.toLatin1()
            : (parse.args.size() > 1 ? parse.args[1].toLatin1()
                                     : QByteArray());
        SendRaw(QByteArrayLiteral("PONG :") + payload
                + QByteArrayLiteral("\r\n"));
        ircPrint.SetFormat(PT_NONE);
        return;
    }

    if (parse.command == QLatin1String("ERROR")) {
        if (parse.bHasLastString) {
            CSInString(&parse.lastString);
            if (!theApp.m_SrvConnector.IsConnecting()
                || ((!theApp.m_SrvConnector.GetNumServers()) == 1)) {
                if (parse.lastString.contains(
                        QStringLiteral("No IRC clients"))) {
                    showIrcSourceMessage(originalResourceString(
                        QStringLiteral("IDS_MICONLY")));
                } else {
                    showIrcSourceMessage(parse.lastString);
                }
                if (theApp.m_pMainWnd)
                    theApp.m_pMainWnd->CreateNewDocument();
            } else if (m_bJustSentModeIsIrcX) {
                ircPrint.SetFormat(PT_NONE);
                HrModeIsIrcXFailure();
            }
        }
        return;
    }

    if (parse.uCode == RPL_WELCOME) {
        theApp.CompleteConnection();
        if (parse.args.size() >= 2) {
            SetMyNameNick(parse.args[1]);
        }
        ircPrint.SetFormat(PT_LASTSTRING, line, RGB(255, 0, 0));
        AddToStatus(ircPrint, line);
        ircPrint.SetFormat(PT_NONE);

        if (m_proto) {
            m_proto->SetConnectionStatus(CX_NOCHANNEL);
            QString server = QString::fromUtf8(GetMyServer());
            QString identity = parse.args.value(1);
            QString channel;
            QString eventMessage;
            theApp.m_dynaRules.bMatchAndApplyRules(
                eOnConnect, nullptr, nullptr, server, identity,
                channel, eventMessage);
            if (m_proto->GetConnectionStatus() != CX_DISCONNECTED) {
                m_queries.bAddQuery(new CCQuery(
                    qpInitialLUsersMOTD, ctLUsersMOTD, dtMax, nullptr,
                    QString(), QString()));
                m_proto->OnLogin();
            }
        }
        return;
    }

    if (parse.uCode == RPL_LUSERCLIENT
        || parse.uCode == RPL_LUSEROP
        || parse.uCode == RPL_LUSERUNKNOWN
        || parse.uCode == RPL_LUSERCHANNELS
        || parse.uCode == RPL_LUSERME
        || parse.uCode == RPL_LOCALUSERS
        || parse.uCode == RPL_GLOBALUSERS) {
        if (parse.bHasLastString) {
            CSInString(&parse.lastString);
            QString display;
            if (parse.nArgs >= 3) {
                display = parse.args[2] + QLatin1Char(' ');
            }
            display += parse.lastString;
            CCQuery* query = m_queries.FindQuery(ctLUsersMOTD);
            if (query) m_strLUSER += display + QLatin1Char('\n');
            if (query && query->GetQueryPurpose() == qpLUsersMOTD) {
                ircPrint.SetFormat(PT_NONE);
            } else {
                ircPrint.SetFormat(PT_WHOLESTRING, display,
                                   RGB(0, 0, 255), 0,
                                   parse.uCode == RPL_GLOBALUSERS);
            }
        }
        return;
    }

    if (parse.uCode == RPL_MOTDSTART) {
        ircPrint.SetFormat(PT_NONE);
        return;
    }

    if (parse.uCode == RPL_MOTD || parse.uCode == RPL_MOTD2) {
        QString motd;
        if (parse.bHasLastString) {
            motd = parse.lastString;
            if (motd.startsWith(QStringLiteral("- "))) motd.remove(0, 2);
            if (motd == QLatin1String("-")) motd.clear();
            m_strMOTD += motd + QStringLiteral("\r\n");
        }
        CCQuery* query = m_queries.FindQuery(ctLUsersMOTD);
        if (query && query->GetQueryPurpose() == qpLUsersMOTD) {
            ircPrint.SetFormat(PT_NONE);
        } else {
            ircPrint.SetFormat(PT_WHOLESTRING, motd + QLatin1Char('\r'),
                               RGB(0, 128, 0));
        }
        return;
    }

    if (parse.uCode == RPL_ENDOFMOTD) {
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(ctLUsersMOTD, &queryIndex);
        const BOOL newLine = !query
            || query->GetQueryPurpose() == qpInitialLUsersMOTD;
        if (query) {
            if ((query->GetQueryPurpose() == qpLUsersMOTD
                 || (theApp.m_flags1 & F1_SHOWMOTD))
                && (!m_strMOTD.isEmpty() || !m_strLUSER.isEmpty())) {
                ShowMOTD(m_strLUSER, m_strMOTD);
            }
            m_queries.FreeRemoveAt(queryIndex);
        }
        m_strMOTD.clear();
        m_strLUSER.clear();
        ircPrint.SetFormat(PT_NONE, QString(), RGB(0, 128, 0), 0,
                           newLine);
        theApp.m_bDisableMOTD = false;
        return;
    }

    if (parse.uCode == ERR_NOMOTD) {
        const QString message = originalResourceString(IDS_ERR_NOMOTD);
        ircPrint.SetFormat(PT_WHOLESTRING, message,
                           RGB(0, 0, 255), 0, TRUE);
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(ctLUsersMOTD, &queryIndex);
        if (query) {
            if ((query->GetQueryPurpose() == qpLUsersMOTD
                 || (theApp.m_flags1 & F1_SHOWMOTD))
                && !m_strLUSER.isEmpty()) {
                ShowMOTD(m_strLUSER, QString());
            }
            m_queries.FreeRemoveAt(queryIndex);
        }
        m_strLUSER.clear();
        theApp.m_bDisableMOTD = false;
        return;
    }

    if (parse.uCode == 451) {
        HrModeIsIrcXFailure();
        return;
    }

    if (parse.uCode == RPL_IRCX && parse.args.size() >= 3) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(ctModeIsIrcX, &queryIndex);
        if (!query) {
            query = m_queries.FindQuery(ctIrcX, &queryIndex);
        }
        if (!query) {
            return;
        }
        m_queries.FreeRemoveAt(queryIndex);
        if (parse.args[2] == QLatin1String("0")) {
            if (parse.nArgs >= 7) {
                const QStringList packages = parse.args[4].split(
                    QLatin1Char(','), Qt::KeepEmptyParts);
                for (const QString& securityPackage : packages) {
                    if (securityPackage.compare(
                            QString::fromLatin1(g_szAnon),
                            Qt::CaseInsensitive) == 0) {
                        m_bAnonAllowed = TRUE;
                    } else {
                        m_rgszSvrSecuPack.append(securityPackage);
                    }
                }
                bool validMaximum = false;
                const int maximum = parse.args[parse.nArgs - 2]
                    .toInt(&validMaximum);
                if (validMaximum && maximum > m_nMaxMsgLength) {
                    m_nMaxMsgLength = static_cast<short>(maximum);
                }
            }
            m_bIrcXServer = true;
            m_bJustSentModeIsIrcX = false;
            m_ircXTimer->stop();
            if (m_proto) {
                m_proto->bExecuteQuery(qpIrcX, ctIrcX, dtMax, nullptr,
                                       QString(), QString());
            }
        } else {
            HrIrcXLogin(TRUE);
        }
        return;
    }

    if (parse.uCode == RPL_PROPLIST && parse.nArgs >= 4) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        CCQuery* query = m_queries.FindQuery(ctPropGet);
        if (!query) return;
        switch (query->GetQueryPurpose()) {
        case qpJoinPics:
        case qpCreatePics:
            query->SetQueryPurpose(qpMax);
            if (bPassesRatings(parse.lastString, TRUE)) {
                CRoomInfo* enterInfo = theApp.GetRoomInfoFromName(
                    parse.args[2]);
                if (m_proto && enterInfo) {
                    // Preserve source order: the query purpose has already
                    // changed to qpMax before the comparison.
                    if (query->GetQueryPurpose() == qpJoinPics)
                        m_proto->ChatJoinAux(*enterInfo);
                    else
                        m_proto->ChatCreateAux(*enterInfo);
                }
            }
            break;
        case qpJoinBackUrl:
            if (m_proto) m_proto->HandleClientDataChange(parse.lastString);
            break;
        default:
            break;
        }
        return;
    }

    if (parse.uCode == RPL_PROPEND && parse.nArgs >= 3) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        int queryIndex = -1;
        if (CCQuery* query = m_queries.FindQuery(ctPropGet, &queryIndex)) {
            query = m_queries.RemoveAt(queryIndex);
            switch (query->GetQueryPurpose()) {
            case qpJoinPics:
            case qpCreatePics:
                if (bCanViewUnrated(TRUE)) {
                    CRoomInfo* enterInfo = theApp.GetRoomInfoFromName(
                        parse.args[2]);
                    if (m_proto && enterInfo) {
                        if (query->GetQueryPurpose() == qpJoinPics)
                            m_proto->ChatJoinAux(*enterInfo);
                        else
                            m_proto->ChatCreateAux(*enterInfo);
                    }
                }
                break;
            case qpJoinBackUrl:
            case qpMax:
                break;
            default:
                break;
            }
            delete query;
        }
        return;
    }

    if (parse.uCode == RPL_LISTSTART
        || parse.uCode == RPL_LISTXSTART) {
        const enumCommandType command = parse.uCode == RPL_LISTSTART
            ? ctList : ctListX;
        CCQuery* query = m_queries.FindQuery(command);
        if (query) {
            if (query->GetQueryPurpose() == qpRoomListDlg) {
                delete m_pendingListXRoom;
                m_pendingListXRoom = nullptr;
                m_pendingListXAdd = FALSE;
                g_bCanViewUnrated = bCanViewUnrated();
                StartRoomList();
            }
            if (query->GetQueryPurpose() == qpRoomListDlg
                || query->GetQueryPurpose() == qpOnNewRoomEvent) {
                ircPrint.SetFormat(PT_NONE);
            } else {
                ircPrint.SetFormat(PT_OFFSET, line, RGB(128, 0, 128), 3);
            }
        } else {
            ircPrint.SetFormat(PT_OFFSET, line, RGB(128, 0, 128), 3);
        }
        return;
    }

    if (parse.uCode == RPL_LIST && parse.args.size() >= 4
        && parse.bHasLastString) {
        CCQuery* query = m_queries.FindQuery(ctList);
        if (query) {
            if (query->GetQueryPurpose() == qpOnNewRoomEvent) {
                auto* rule = static_cast<CCRule*>(query->GetData());
                if (rule && rule->bActive() && !rule->bStopped()
                    && rule->GetDaemonExt()) {
                    rule->GetDaemonExt()->bAddChannelToCurrentList(
                        parse.args.at(2));
                }
                ircPrint.SetFormat(PT_NONE);
            } else if (query->GetQueryPurpose() == qpRoomListDlg) {
                CSInString(&parse.lastString);
                if (parse.args.at(2) != QLatin1String("*")) {
                    auto* room = new CRoom;
                    room->m_name = parse.args.at(2);
                    room->m_prettyName = DecodeChan(parse.args.at(2));
                    room->m_nUsers = parse.args.at(3).toUInt();
                    room->m_descr = parse.lastString;
                    room->m_byteRegistered = FALSE;
                    AddToRoomList(room);
                }
                ircPrint.SetFormat(PT_NONE);
            } else {
                ircPrint.SetFormat(PT_OFFSET, line, RGB(128, 0, 128), 3);
            }
        } else {
            ircPrint.SetFormat(PT_OFFSET, line, RGB(128, 0, 128), 3);
        }
        return;
    }

    if (parse.uCode == RPL_LISTXLIST) {
        CCQuery* query = m_queries.FindQuery(ctListX);
        if (query) {
            if (query->GetQueryPurpose() == qpOnNewRoomEvent) {
                auto* rule = static_cast<CCRule*>(query->GetData());
                if (rule && rule->bActive() && !rule->bStopped()
                    && rule->GetDaemonExt() && parse.args.size() >= 3) {
                    rule->GetDaemonExt()->bAddChannelToCurrentList(
                        parse.args.at(2));
                }
                ircPrint.SetFormat(PT_NONE);
            } else if (query->GetQueryPurpose() == qpRoomListDlg) {
                if (m_pendingListXRoom) {
                    AddToRoomList(m_pendingListXRoom, m_pendingListXAdd);
                    m_pendingListXRoom = nullptr;
                }
                if (parse.args.size() >= 6 && parse.bHasLastString) {
                    auto* room = new CRoom;
                    const QString roomName = parse.args.at(2);
                    const BOOL mic = parse.args.at(3).contains(QLatin1Char('y'));
                    CSInString(&parse.lastString,
                               mic ? QString() : roomName);
                    room->m_name = roomName;
                    room->m_prettyName = DecodeChan(roomName, mic);
                    room->m_nUsers = parse.args.at(4).toUInt();
                    QByteArray topic = parse.lastString.toUtf8();
                    room->m_descr = QString::fromUtf8(
                        SzControlLess(topic.data(), nullptr));
                    room->m_byteRegistered = parse.args.at(3).contains(
                        QLatin1Char('r'));
                    m_pendingListXRoom = room;
                    m_pendingListXAdd = g_bCanViewUnrated;
                }
                ircPrint.SetFormat(PT_NONE);
            } else {
                ircPrint.SetFormat(PT_OFFSET, line, RGB(128, 0, 128), 3);
            }
        } else {
            ircPrint.SetFormat(PT_OFFSET, line, RGB(128, 0, 128), 3);
        }
        return;
    }

    if (parse.uCode == RPL_LISTXPICS) {
        m_pendingListXAdd = bPassesRatings(parse.lastString);
        if (m_queries.FindQuery(ctListX)) {
            ircPrint.SetFormat(PT_NONE);
        } else {
            ircPrint.SetFormat(PT_OFFSET, line, RGB(128, 0, 128), 3);
        }
        return;
    }

    if (parse.uCode == RPL_LISTEND
        || parse.uCode == RPL_LISTXTRUNC
        || parse.uCode == RPL_LISTXEND) {
        const enumCommandType command = parse.uCode == RPL_LISTEND
            ? ctList : ctListX;
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(command, &queryIndex);
        if (query) {
            query = m_queries.RemoveAt(queryIndex);
            if (query->GetQueryPurpose() == qpRoomListDlg) {
                if (m_pendingListXRoom) {
                    AddToRoomList(m_pendingListXRoom, m_pendingListXAdd);
                    m_pendingListXRoom = nullptr;
                }
                EndRoomList();
            } else if (query->GetQueryPurpose() == qpOnNewRoomEvent) {
                auto* rule = static_cast<CCRule*>(query->GetData());
                if (rule && rule->bActive() && !rule->bStopped()
                    && rule->GetDaemonExt()) {
                    rule->GetDaemonExt()->bOnEndOfListing(
                        &theApp.m_dynaRules, rule,
                        query->GetQueryPurpose());
                }
            }
            delete query;
            ircPrint.SetFormat(PT_NONE);
        } else {
            ircPrint.SetFormat(PT_OFFSET, line, RGB(128, 0, 128), 3, TRUE);
        }
        return;
    }

    if (parse.command == QLatin1String("INVITE")
        && parse.bHasLastString) {
        CSInPlace(&parse.user);
        OnInvite(parse.nick,
                 parse.user + QLatin1Char('@') + parse.machine,
                 parse.lastString);
        ircPrint.SetFormat(PT_NONE);
        return;
    }

    if (parse.command == QLatin1String("JOIN")) {
        ircPrint.SetFormat(PT_NONE);
        QString channel = parse.lastString;
        if (channel.isEmpty() && parse.args.size() >= 2) {
            channel = parse.args[1];
        }
        CSInPlace(&parse.user);
        const QString ident = parse.user + QLatin1Char('@') + parse.machine;

        if (parse.nick.compare(theApp.m_myNick, Qt::CaseInsensitive) != 0) {
            CChatDoc* doc = LookupDoc(channel);
            if (doc) {
                enumActions actionIDs[2] = {
                    static_cast<enumActions>(1), aHighlightMessage
                };
                QString server = QString::fromUtf8(GetMyServer());
                QString identity = parse.nick + QLatin1Char('!') + ident;
                QString eventChannel = channel;
                QString eventMessage;
                theApp.m_dynaRules.bMatchAndApplyRules(
                    eOnJoin, actionIDs, nullptr, server, identity,
                    eventChannel, eventMessage);
                char highlightType = -1;
                if (theApp.m_dynaRules.GetFlags() & g_wHighlight) {
                    highlightType = static_cast<char>(
                        theApp.m_dynaRules.GetFlags() >> 8);
                }
                auto* pui = new CUserInfo(parse.nick, ident);
                AddAndExecute(new JoinEntry(
                    pui, FALSE, highlightType), doc);
                theApp.m_dynaRules.bMatchAndApplyRules(
                    eOnJoin, nullptr, actionIDs, server, identity,
                    eventChannel, eventMessage);
            }
        } else if (m_proto
                   && bProcessAddChannel(channel, NewDefaultProto(nullptr),
                                         &g_nCXKeepServer, &g_bCXPrompt)) {
            if (!theApp.m_nMyIdentLength) {
                theApp.m_nMyIdentLength = static_cast<short>(ident.size() + 2);
                SetMyIdent(ident);
            }
            CCQuery* query = new CCQuery(qpInitialNames, ctNames, dtMax, nullptr, channel, QString());
            m_queries.bAddQuery(query);
            query = new CCQuery(qpInitialTopic, ctTopic, dtMax, nullptr, channel, QString());
            m_queries.bAddQuery(query);
            m_proto->bExecuteQuery(qpInitialMode, ctGetChannelMode, dtMax, nullptr, channel, QString());
            m_proto->bExecuteQuery(qpInitialWho, ctWho, dtMax, nullptr, channel, QString());
            if (m_bIrcXServer) {
                m_proto->bExecuteQuery(qpJoinBackUrl, ctPropGet, dtMax,
                                       nullptr, channel, QString());
            }
        }
        return;
    }

    if (parse.command == QLatin1String("CREATE") && parse.nArgs >= 3) {
        ircPrint.SetFormat(PT_NONE);
        const QString channel = parse.args[1];
        if (m_proto
            && bProcessAddChannel(channel, NewDefaultProto(nullptr),
                                  &g_nCXKeepServer, &g_bCXPrompt)) {
            CCQuery* query = new CCQuery(qpInitialNames, ctNames, dtMax,
                                         nullptr, channel, QString());
            m_queries.bAddQuery(query);
            query = new CCQuery(qpInitialTopic, ctTopic, dtMax,
                                nullptr, channel, QString());
            m_queries.bAddQuery(query);
            m_proto->bExecuteQuery(qpInitialMode, ctGetChannelMode, dtMax,
                                   nullptr, channel, QString());
            m_proto->bExecuteQuery(qpInitialWho, ctWho, dtMax,
                                   nullptr, channel, QString());
            if (m_bIrcXServer) {
                m_proto->bExecuteQuery(qpJoinBackUrl, ctPropGet, dtMax,
                                       nullptr, channel, QString());
            }
        }
        return;
    }

    if (parse.command == QLatin1String("MODE") && parse.args.size() >= 3) {
        constexpr BYTE modeCacheNone = 0;
        constexpr BYTE modeCacheHostLost = 1;
        constexpr BYTE modeCacheOwnerLost = 2;
        static QString lostChannel;
        static QString lostNickname;
        static BYTE lostStatus = modeCacheNone;

        CChatDoc* doc = LookupDoc(parse.args[1]);
        if (doc) {
            ParseChannelMode(doc, parse.args[2],
                             parse.args.size() >= 4 ? parse.args[3] : QString(),
                             parse.args.size() >= 5 ? parse.args[4] : QString());
        }
        int queryIndex = -1;
        if (CCQuery* query = m_queries.FindQuery(ctSetChannelMode, &queryIndex)) {
            if (query->GetQueryPurpose() == qpComSetChannelMode
                && query->GetChannelName().compare(parse.args[1],
                                                    Qt::CaseInsensitive) == 0) {
                m_queries.FreeRemoveAt(queryIndex);
                ircPrint.SetFormat(PT_OFFSET, line, RGB(0, 0, 0), 1, TRUE);
                const QString flags = parse.args[2];
                const bool hostLost = flags.contains(QStringLiteral("-o"));
                const bool ownerLost = flags.contains(QStringLiteral("-q"));
                if (hostLost || ownerLost) {
                    lostChannel = parse.args[1];
                    lostNickname = parse.args.value(3);
                    lostStatus = hostLost
                        ? modeCacheHostLost : modeCacheOwnerLost;
                } else {
                    lostStatus = modeCacheNone;
                }
            }
        } else {
            const QString flags = parse.args[2];
            const QString modeNickname = parse.args.value(3);
            if (lostStatus != modeCacheNone
                && lostChannel == parse.args[1]
                && lostNickname == modeNickname) {
                const bool display =
                    (lostStatus == modeCacheHostLost
                     && flags.contains(QStringLiteral("+q")))
                    || (lostStatus == modeCacheOwnerLost
                        && flags.contains(QStringLiteral("+o")))
                    || (lostStatus == modeCacheOwnerLost
                        && flags.contains(QStringLiteral("-o")));
                ircPrint.SetFormat(display ? PT_OFFSET : PT_NONE,
                                   line, RGB(0, 0, 0), 1, TRUE);
                lostStatus = modeCacheNone;
            } else {
                ircPrint.SetFormat(PT_NONE);
            }
        }
        return;
    }

    if (parse.command == QLatin1String("MODE") && parse.nArgs == 2
        && parse.bHasLastString
        && parse.args[1].compare(QString::fromUtf8(GetMyNickName()),
                                 Qt::CaseInsensitive) == 0) {
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(ctSetUserMode, &queryIndex);
        if (!query) {
            ircPrint.SetFormat(PT_OFFSET, line, RGB(0, 0, 0), 1, TRUE);
            return;
        }
        BOOL display = TRUE;
        switch (query->GetQueryPurpose()) {
        case qpSetInvisible:
        case qpSetVisible:
        case qpComSetUserMode:
        {
            bool set = false;
            bool removeQuery = query->GetQueryPurpose() == qpComSetUserMode;
            for (QChar mode : parse.lastString) {
                switch (mode.toLatin1()) {
                case '-':
                    set = false;
                    break;
                case '+':
                    set = true;
                    break;
                case 'i':
                    if (set) theApp.m_flags1 &= ~DWORD(F1_USERVISIBLE);
                    else theApp.m_flags1 |= F1_USERVISIBLE;
                    removeQuery = true;
                    if (query->GetQueryPurpose() != qpComSetUserMode)
                        display = FALSE;
                    break;
                default:
                    break;
                }
            }
            if (removeQuery) m_queries.FreeRemoveAt(queryIndex);
            ircPrint.SetFormat(display ? PT_OFFSET : PT_NONE,
                               line, RGB(0, 0, 0), 1, TRUE);
            break;
        }
        default:
            break;
        }
        return;
    }

    if (parse.uCode == RPL_UMODEIS
        ) {
        ircPrint.SetFormat(PT_OFFSET, line, RGB(0, 0, 0), 2, TRUE);
        return;
    }

    if (parse.uCode == RPL_AWAY && parse.nArgs > 2
        && parse.bHasLastString) {
        QString message = originalResourceString(QStringLiteral("IDS_AWAYREPORT"));
        message.replace(QStringLiteral("%1"),
                        DecodeNickForScreen(parse.args[2]));
        message.replace(QStringLiteral("%2"), parse.lastString);
        ircPrint.SetFormat(PT_WHOLESTRING, message,
                           RGB(0, 128, 128), 0, TRUE);
        return;
    }

    if (parse.uCode == RPL_UNAWAY || parse.uCode == RPL_NOWAWAY) {
        ircPrint.SetFormat(PT_LASTSTRING, line,
                           RGB(0, 128, 128), 0, TRUE);
        return;
    }

    if (parse.uCode == RPL_CHANNELMODEIS && parse.args.size() >= 4) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        CChatDoc* doc = LookupDoc(parse.args[2]);
        if (doc) {
            int roomInfoIndex = -1;
            CRoomInfo* enterInfo = theApp.GetRoomInfoFromName(
                parse.args[2], &roomInfoIndex,
                parse.args[3].contains(QLatin1Char('e')), false);
            doc->m_proto->m_strPassword.clear();
            doc->m_proto->m_dwModes = 0;
            ParseChannelMode(doc, parse.args[3],
                             parse.args.size() >= 5 ? parse.args[4] : QString(),
                             parse.args.size() >= 6 ? parse.args[5] : QString(),
                             enterInfo);
            if (currentRoom && enterInfo && enterInfo->m_bSetMode
                && parse.args[2].compare(currentRoom->m_strChannel,
                                         Qt::CaseInsensitive) == 0) {
                doc->m_proto->ChatSetMode(enterInfo->m_dwModes,
                                          enterInfo->m_dwMaxUsers,
                                          enterInfo->m_strPassword);
                if (!enterInfo->m_strTopic.isEmpty()) {
                    QString controlFull = enterInfo->m_strTopic;
                    if (enterInfo->m_prgdwTopicFormatting) {
                        const QByteArray topic = enterInfo->m_strTopic.toUtf8();
                        if (char* encoded = SzControlFull(
                                topic.constData(),
                                enterInfo->m_prgdwTopicFormatting)) {
                            controlFull = QString::fromUtf8(encoded);
                            delete[] encoded;
                        }
                    }
                    doc->m_proto->ChatSetTopic(controlFull);
                }
            }
            if (roomInfoIndex > 0) {
                theApp.RemoveRoomInfo(roomInfoIndex);
            } else if (enterInfo) {
                bInitEnterInfo(*enterInfo, QStringLiteral(""), QString(),
                               QString(), 0L, FALSE);
            }
        }
        int queryIndex = -1;
        if (CCQuery* query = m_queries.FindQuery(ctGetChannelMode, &queryIndex)) {
            if (query->GetQueryPurpose() == qpInitialMode
                && query->GetChannelName().compare(parse.args[2],
                                                    Qt::CaseInsensitive) == 0) {
                m_queries.FreeRemoveAt(queryIndex);
            }
        }
        return;
    }

    if (parse.command == QLatin1String("TOPIC") && parse.args.size() >= 2
        && parse.bHasLastString) {
        CChatDoc* doc = LookupDoc(parse.args[1]);
        CSInString(&parse.lastString, parse.args[1], doc);
        CDWordArray formatting;
        const QString controlLessTopic = setRoomTopic(
            doc, parse.lastString, &formatting);
        int queryIndex = -1;
        if (CCQuery* query = m_queries.FindQuery(ctTopic, &queryIndex)) {
            Q_UNUSED(query);
            m_queries.FreeRemoveAt(queryIndex);
            ircPrint.SetFormat(PT_NONE);
        } else {
            const QString statusLine = parse.args[1]
                + QStringLiteral(" :") + controlLessTopic;
            PushFormattingOffsets(
                &formatting,
                static_cast<SHORT>(parse.args[1].toUtf8().size() + 2));
            ircPrint.SetFormat(PT_WHOLESTRING, statusLine,
                               RGB(0, 0, 128), 0, TRUE);
            AddToStatus(ircPrint, statusLine, &formatting);
            ircPrint.SetFormat(PT_NONE);
        }
        return;
    }

    if (parse.uCode == RPL_TOPIC && parse.args.size() >= 3) {
        CChatDoc* doc = LookupDoc(parse.args[2]);
        if (parse.bHasLastString) {
            CSInString(&parse.lastString, parse.args[2], doc);
        }
        CDWordArray formatting;
        const QString controlLessTopic = setRoomTopic(
            doc, parse.lastString, &formatting);
        int queryIndex = -1;
        bool listMembers = false;
        QString encodedRoom;
        QString prettyRoom;
        CCQuery* query = m_queries.FindQuery(ctTopic, &queryIndex);
        if (query) {
            if (query->GetQueryPurpose() == qpListMembers
                && query->GetChannelName().compare(parse.args[2],
                                                   Qt::CaseInsensitive) == 0) {
                encodedRoom = query->GetChannelName();
                prettyRoom = query->GetData()
                    ? *static_cast<QString*>(query->GetData()) : QString();
                listMembers = true;
            }
            if (query->GetQueryPurpose() == qpInitialTopic
                && query->GetChannelName().compare(parse.args[2],
                                                   Qt::CaseInsensitive) == 0) {
                ircPrint.SetFormat(PT_NONE);
            }
            m_queries.FreeRemoveAt(queryIndex);
            ircPrint.SetFormat(PT_NONE);
        } else {
            const QString statusLine = parse.args[2]
                + QStringLiteral(" :") + controlLessTopic;
            PushFormattingOffsets(
                &formatting,
                static_cast<SHORT>(parse.args[2].toUtf8().size() + 2));
            ircPrint.SetFormat(PT_WHOLESTRING, statusLine,
                               RGB(0, 0, 128), 0, TRUE);
            AddToStatus(ircPrint, statusLine, &formatting);
            ircPrint.SetFormat(PT_NONE);
        }
        if (listMembers)
            OnUserListAux(QString(), encodedRoom, prettyRoom);
        return;
    }

    if (parse.uCode == RPL_NOTOPIC && parse.args.size() >= 3) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(ctTopic, &queryIndex);
        if (query && query->GetQueryPurpose() == qpListMembers
            && query->GetChannelName().compare(parse.args[2],
                                               Qt::CaseInsensitive) == 0) {
            const QString encodedRoom = query->GetChannelName();
            const QString prettyRoom = query->GetData()
                ? *static_cast<QString*>(query->GetData()) : QString();
            m_queries.FreeRemoveAt(queryIndex);
            ircPrint.SetFormat(PT_NONE);
            OnUserListAux(QString(), encodedRoom, prettyRoom);
        }
        return;
    }

    if (parse.uCode == RPL_NAMEREPLY) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        int topicIndex = -1;
        if (CCQuery* topicQuery = m_queries.FindQuery(ctTopic, &topicIndex)) {
            if (topicQuery->GetQueryPurpose() == qpInitialTopic) {
                m_queries.FreeRemoveAt(topicIndex);
            }
        }

        if (CCQuery* namesQuery = m_queries.FindQuery(ctNames)) {
            const QString replyChannel = parse.args.size() >= 4
                ? parse.args[3] : QString();
            if (namesQuery->GetQueryPurpose() == qpInitialNames
                && namesQuery->GetChannelName().compare(replyChannel,
                                                         Qt::CaseInsensitive) == 0) {
                CChatDoc* doc = LookupDoc(replyChannel);
                if (doc && !parse.lastString.isEmpty()) {
                    if (bForEachWord(parse.lastString, bSingleJoin, doc, 0,
                                     QStringLiteral(" "))) {
                        QString server = QString::fromUtf8(GetMyServer());
                        QString identity = QString::fromUtf8(GetMyNickName())
                            + QLatin1Char('!')
                            + QString::fromUtf8(GetMyIdent());
                        QString eventChannel = doc->m_proto->m_strChannel;
                        QString eventMessage;
                        theApp.m_dynaRules.bMatchAndApplyRules(
                            eOnJoin, nullptr, nullptr, server, identity,
                            eventChannel, eventMessage);
                    }
                }
            }
        }
        return;
    }

    if (parse.uCode == RPL_ENDOFNAMES) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        int index = -1;
        bool initialEnumerationEnded = false;
        if (CCQuery* namesQuery = m_queries.FindQuery(ctNames, &index)) {
            const QString replyChannel = parse.args.size() >= 3
                ? parse.args[2] : QString();
            if (namesQuery->GetQueryPurpose() == qpInitialNames
                && namesQuery->GetChannelName().compare(replyChannel,
                                                         Qt::CaseInsensitive) == 0) {
                m_queries.FreeRemoveAt(index);
                initialEnumerationEnded = true;
            }
        }
        if (initialEnumerationEnded) {
            const QString replyChannel = parse.args.size() >= 3
                ? parse.args[2] : QString();
            ProcessEndEnumeration(LookupDoc(replyChannel));
        }
        return;
    }

    if (parse.uCode == RPL_WHOREPLY && parse.args.size() >= 8) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        if (CCQuery* query = m_queries.FindQuery(ctWho)) {
            if (query->GetQueryPurpose() == qpOnConnectEvent
                || query->GetQueryPurpose() == qpOnDisconnectEvent
                || query->GetQueryPurpose() == qpOnNotification) {
                PPRUSERMATCH match = query->GetPrUserMatch();
                const QByteArray nickname = parse.args[6].toLatin1();
                const QByteArray userName = parse.args[3].toLatin1();
                const QByteArray hostName = parse.args[4].toLatin1();
                if (match && bIsMatch(match, nickname.constData(),
                                      userName.constData(),
                                      hostName.constData())) {
                    auto* rule = query->GetQueryPurpose() == qpOnNotification
                        ? nullptr : static_cast<CCRule*>(query->GetData());
                    auto* notif = query->GetQueryPurpose() == qpOnNotification
                        ? static_cast<CCNotif*>(query->GetData()) : nullptr;
                    CCDaemonExt* daemonExt = rule ? rule->GetDaemonExt()
                        : notif ? notif->GetDaemonExt() : nullptr;
                    if (((rule && rule->bActive() && !rule->bStopped())
                         || (notif && notif->bActive()))
                        && daemonExt) {
                        if (CUser* user = CreateUserFromWhoReply(&parse)) {
                            if (!daemonExt->bAddUserToCurrentList(user)) {
                                user->Release();
                            }
                        }
                    }
                }
                ircPrint.SetFormat(PT_NONE);
            } else if (query->GetQueryPurpose() == qpInitialWho
                && query->GetChannelName().compare(parse.args[2],
                                                    Qt::CaseInsensitive) == 0) {
                CSInString(&parse.args[3]);
                UpdateIgnoreOnEntry(parse.args[2], parse.args[6],
                                    parse.args[3], parse.args[4]);
                ircPrint.SetFormat(PT_NONE);
            } else if (query->GetQueryPurpose() == qpUserListDlg) {
                CSInString(&parse.args[3]);
                if (CUser* user = CreateUserFromWhoReply(&parse))
                    AddToUserList(user);
                ircPrint.SetFormat(PT_NONE);
            }
        }
        return;
    }

    if (parse.uCode == RPL_ENDOFWHO) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        int queryIndex = -1;
        if (CCQuery* query = m_queries.FindQuery(ctWho, &queryIndex)) {
            if (query->GetQueryPurpose() == qpOnConnectEvent
                || query->GetQueryPurpose() == qpOnDisconnectEvent
                || query->GetQueryPurpose() == qpOnNotification) {
                query = m_queries.RemoveAt(queryIndex);
                auto* rule = query->GetQueryPurpose() == qpOnNotification
                    ? nullptr : static_cast<CCRule*>(query->GetData());
                auto* notif = query->GetQueryPurpose() == qpOnNotification
                    ? static_cast<CCNotif*>(query->GetData()) : nullptr;
                CCDaemonExt* daemonExt = rule ? rule->GetDaemonExt()
                    : notif ? notif->GetDaemonExt() : nullptr;
                if (rule && rule->bActive() && !rule->bStopped()
                    && daemonExt) {
                    daemonExt->bOnEndOfListing(
                        &theApp.m_dynaRules, rule, query->GetQueryPurpose());
                } else if (notif && notif->bActive() && daemonExt) {
                    daemonExt->bOnEndOfListing(
                        &theApp.m_dynaNotifs, notif);
                }
                delete query;
                ircPrint.SetFormat(PT_NONE);
            } else if (query->GetQueryPurpose() == qpInitialWho
                || query->GetQueryPurpose() == qpUserListDlg) {
                if (query->GetQueryPurpose() == qpUserListDlg)
                    EndUserList();
                m_queries.FreeRemoveAt(queryIndex);
                ircPrint.SetFormat(PT_NONE);
            }
        }
        return;
    }

    if ((parse.uCode == ERR_NOTONCHANNEL
         || parse.uCode == ERR_NOSUCHCHANNEL)
        && parse.args.size() >= 3) {
        int queryIndex = -1;
        CCQuery* query = m_queries.FindQuery(ctTopic, &queryIndex);
        if (query && query->GetQueryPurpose() == qpListMembers
            && query->GetChannelName().compare(parse.args[2],
                                               Qt::CaseInsensitive) == 0) {
            const QString encodedRoom = query->GetChannelName();
            const QString prettyRoom = query->GetData()
                ? *static_cast<QString*>(query->GetData()) : QString();
            m_queries.FreeRemoveAt(queryIndex);
            ircPrint.SetFormat(PT_NONE);
            if (parse.uCode == ERR_NOTONCHANNEL) {
                OnUserListAux(QString(), encodedRoom, prettyRoom);
            } else {
                QString message = originalResourceString(
                    QStringLiteral("IDS_ERR_NOSUCHCHANNELANYMORE"));
                message.replace(QStringLiteral("%s"),
                                DecodeChan(parse.args[2]));
                QMessageBox::information(
                    theApp.m_pMainWnd.data(),
                    originalResourceString(
                        QStringLiteral("AFX_IDS_APP_TITLE")),
                    message);
                if (theApp.m_pRoomList)
                    theApp.m_pRoomList->ReenableListMembers();
            }
            return;
        }
    }

    if (parse.uCode == RPL_WHOISUSER && parse.args.size() >= 5) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        CSInString(&parse.args[3]);
        CCQuery* query = m_queries.FindQuery(ctWhoIs);
        if (query
            && query->GetNicknameMask().compare(parse.args[2],
                                                Qt::CaseInsensitive) == 0) {
            switch (query->GetQueryPurpose()) {
            case qpBanDlg:
            case qpKickDlg:
            {
                QString ban;
                GetBanString(parse.args[3], parse.args[4], ban);
                if (currentRoom) {
                    if (query->GetQueryPurpose() == qpKickDlg) {
                        currentRoom->DoKickDlg(parse.args[2], ban);
                    } else {
                        g_strBan = ban;
                        currentRoom->SendMessageText(
                            QStringLiteral("MODE %1 +b\r\n")
                                .arg(query->GetChannelName()));
                    }
                }
                break;
            }
            case qpGetIdent:
                ShowIdentity(parse.args[2], parse.args[3], parse.args[4]);
                break;
            case qpIgnoreIdent:
            {
                const WORD flags = static_cast<WORD>(
                    reinterpret_cast<quintptr>(query->GetData()));
                IgnoreUser(parse.args[2],
                           parse.args[3] + QLatin1Char('@') + parse.args[4],
                           flags & g_wIgnoreIdent,
                           flags & g_wAutoIgnoreIdent);
                break;
            }
            default:
                break;
            }
            ircPrint.SetFormat(PT_NONE);
        }
        return;
    }

    if (parse.uCode == RPL_INVITING && parse.args.size() >= 4) {
        AcknowledgeInvite(DecodeNick(parse.args[2]),
                          DecodeChan(parse.args[3]));
        ircPrint.SetFormat(PT_NONE);
        return;
    }

    if (parse.uCode == RPL_BANLIST && parse.args.size() >= 4) {
        g_arrayBans.append(DecodeNick(parse.args[3]));
        ircPrint.SetFormat(PT_NONE);
        return;
    }

    if (parse.uCode == RPL_ENDOFBANLIST && parse.args.size() >= 3) {
        int queryIndex = -1;
        if (CCQuery* query = m_queries.FindQuery(ctSetChannelMode,
                                                 &queryIndex)) {
            if (query->GetQueryPurpose() == qpComSetChannelMode
                && query->GetChannelName().compare(
                       parse.args[2], Qt::CaseInsensitive) == 0) {
                m_queries.FreeRemoveAt(queryIndex);
            }
        }
        ircPrint.SetFormat(PT_NONE);
        DoBanDlg(parse.args[2], g_strBan, g_arrayBans);
        g_strBan.clear();
        g_arrayBans.clear();
        return;
    }

    if (parse.uCode == RPL_ENDOFWHOIS && parse.args.size() >= 3) {
        QString displayLine;
        HandleResultCode(displayLine, line, &parse, &ircPrint);
        int queryIndex = -1;
        if (CCQuery* query = m_queries.FindQuery(ctWhoIs, &queryIndex)) {
            if (query->GetNicknameMask().compare(parse.args[2],
                                                 Qt::CaseInsensitive) == 0) {
                m_queries.FreeRemoveAt(queryIndex);
                ircPrint.SetFormat(PT_NONE);
            }
        }
        return;
    }

    if (parse.command == QLatin1String("DATA") && parse.args.size() >= 3) {
        ircPrint.SetFormat(PT_NONE);
        const QString target = parse.args[1];
        if (parse.bHasLastString && parse.lastString.startsWith(QLatin1Char('#'))
            && parse.args[2] == QLatin1String("CCUDI1")
            && !parse.nick.isEmpty() && !parse.user.isEmpty()) {
            CChatDoc* doc = nullptr;
            unsigned char msgType = MT_PRIVATEMSG | MT_DATA;
            if (channelPrefix(target)) {
                doc = LookupDoc(target);
                msgType = MT_CHANNELSEND | MT_DATA;
            }
            if ((msgType & MT_PRIVATEMSG) || doc) {
                const QString ident = identFromParse(parse);
                if (!doc && isAppearsAsMessage(parse.lastString)) {
                    for (CChatDoc* candidate : g_docs) {
                        CUserInfo* pui = LookupPui(parse.nick, candidate);
                        if (pui && !pui->IsDeparted()) {
                            OnDataMsg(candidate, parse.nick, ident,
                                      parse.lastString, msgType);
                        }
                    }
                } else {
                    OnDataMsg(doc, parse.nick, ident, parse.lastString,
                              msgType);
                }
            }
        }
        return;
    }

    if ((parse.command == QLatin1String("PRIVMSG")
         || parse.command == QLatin1String("NOTICE"))
        && parse.args.size() >= 2 && parse.bHasLastString) {
        QString displayLine;
        HandleCommand(displayLine, line, &parse, &ircPrint);
        if (!parse.nick.isEmpty() && !parse.user.isEmpty()) {
            const QString target = parse.args[1];
            CChatDoc* doc = nullptr;
            unsigned char msgType = MT_PRIVATEMSG;
            if (channelPrefix(target)) {
                doc = LookupDoc(target);
                msgType = MT_CHANNELSEND;
            }
            msgType = static_cast<unsigned char>(msgType | (parse.command == QLatin1String("NOTICE") ? MT_NOTICE : MT_PRVMSG));
            if ((msgType & MT_PRIVATEMSG) || doc) {
                const QString ident = identFromParse(parse);
                if (!doc && isAppearsAsMessage(parse.lastString)) {
                    for (CChatDoc* candidate : g_docs) {
                        CUserInfo* pui = LookupPui(parse.nick, candidate);
                        if (pui && !pui->IsDeparted()) {
                            OnTextMsg(candidate, parse.nick, ident,
                                      parse.lastString, msgType);
                        }
                    }
                } else {
                    CSInString(&parse.lastString, target, doc);
                    OnTextMsg(doc, parse.nick, ident, parse.lastString,
                              msgType);
                }
            }
        }
        return;
    }

    if (parse.command == QLatin1String("NICK")) {
        ircPrint.SetFormat(PT_NONE);
        const bool displayNewNick =
            parse.nick == QString::fromUtf8(GetMyNickName());
        const QString newNick = parse.bHasLastString
            ? parse.lastString : QString();
        if (!newNick.isEmpty()) {
            bool setName = false;
            for (CChatDoc* doc : g_docs) {
                if (!doc || doc->GetConnectionStatus() != CX_INCHANNEL) {
                    continue;
                }
                CUserInfo* pui = LookupPui(parse.nick, doc);
                if (pui && !pui->IsDeparted()) {
                    AddAndExecute(new NickEntry(parse.nick, newNick), doc);
                    setName = true;
                }
            }
            if (!setName) SetMyNameNick(newNick);
            if (displayNewNick) {
                QString message = originalResourceString(
                    QStringLiteral("IDS_NOWKNOWNAS"));
                message.replace(QStringLiteral("%s"),
                                QString::fromUtf8(GetMyScreenName()));
                ircPrint.SetFormat(PT_WHOLESTRING, message,
                                   RGB(0, 0, 255), 0, TRUE);
            }
        }
        return;
    }

    if (parse.command == QLatin1String("KICK") && parse.args.size() >= 3
        && parse.bHasLastString) {
        ircPrint.SetFormat(PT_NONE);
        CChatDoc* doc = LookupDoc(parse.args[1]);
        CSInString(&parse.lastString, parse.args[1], doc);
        OnKick(doc, parse.nick, parse.args[2], parse.lastString);
        return;
    }

    if (parse.command == QLatin1String("PART")) {
        ircPrint.SetFormat(PT_NONE);
        CChatDoc* doc = parse.args.size() >= 2
            ? LookupDoc(parse.args[1]) : nullptr;
        if (parse.nick.compare(QString::fromUtf8(GetMyNickName()),
                               Qt::CaseInsensitive) == 0) {
            GotPartChannel(doc);
            theApp.m_pExitingDoc = nullptr;
        } else if (doc) {
                enumActions actionIDs[2] = {
                    static_cast<enumActions>(1), aHighlightMessage
                };
                QString server = QString::fromUtf8(GetMyServer());
                QString identity = parse.nick + QLatin1Char('!')
                    + parse.user + QLatin1Char('@') + parse.machine;
                QString channel = parse.args[1];
                QString eventMessage;
                theApp.m_dynaRules.bMatchAndApplyRules(
                    eOnLeave, actionIDs, nullptr, server, identity,
                    channel, eventMessage);
                char highlightType = -1;
                if (theApp.m_dynaRules.GetFlags() & g_wHighlight) {
                    highlightType = static_cast<char>(
                        theApp.m_dynaRules.GetFlags() >> 8);
                }
                AddAndExecute(new PartEntry(
                    parse.nick, highlightType), doc);
                theApp.m_dynaRules.bMatchAndApplyRules(
                    eOnLeave, nullptr, actionIDs, server, identity,
                    channel, eventMessage);
        }
        return;
    }

    if (parse.command == QLatin1String("PROP")) {
        QString displayLine;
        HandleCommand(displayLine, line, &parse, &ircPrint);
        if (parse.nArgs == 3 && parse.bHasLastString
            && channelPrefix(parse.args[1])) {
            CChatDoc* doc = LookupDoc(parse.args[1]);
            if (doc && doc->m_proto && doc->m_proto->IsIRCX()) {
                if (parse.args[2] == QLatin1String("CLIENT")) {
                    int queryIndex = -1;
                    CCQuery* query = m_queries.FindQuery(ctPropSet,
                                                        &queryIndex);
                    if (query && query->GetQueryPurpose() == qpSetClient) {
                        m_queries.FreeRemoveAt(queryIndex);
                        ircPrint.SetFormat(PT_NONE);
                    }
                    doc->m_proto->HandleClientDataChange(parse.lastString);
                }
                if (parse.args[2] == QLatin1String("TOPIC")) {
                    CSInString(&parse.lastString, parse.args[1], doc);
                    setRoomTopic(doc, parse.lastString);
                }
            }
        }
        return;
    }

    if (parse.command == QLatin1String("QUIT")
        || parse.command == QLatin1String("KILL")) {
        ircPrint.SetFormat(PT_NONE);
        const QString quittingNick = parse.command == QLatin1String("QUIT")
            ? parse.nick : (parse.args.size() >= 2 ? parse.args[1] : QString());
        if (!quittingNick.isEmpty()) {
            for (CChatDoc* doc : g_docs) {
                if (!doc || doc->GetConnectionStatus() != CX_INCHANNEL) break;
                CUserInfo* pui = LookupPui(quittingNick, doc);
                if (pui && !pui->IsDeparted()) {
                    AddAndExecute(new PartEntry(quittingNick), doc);
                }
            }
        }
        return;
    }

    if (parse.command == QLatin1String("WHISPER")
        && parse.args.size() >= 3 && parse.bHasLastString) {
        ircPrint.SetFormat(PT_NONE);
        CChatDoc* doc = LookupDoc(parse.args[1]);
        if (doc && !parse.nick.isEmpty()) {
            QList<CUserInfo*> talkTos;
            GetTalkTos(doc, &talkTos, parse.args[2]);
            CSInString(&parse.lastString, parse.args[1], doc);
            OnTextMsg(doc, parse.nick, QStringLiteral("X"),
                      parse.lastString, MT_PRIVATEMSG | MT_WHISPER,
                      &talkTos);
        }
        return;
    }

    QString displayLine;
    if (parse.uCode) {
        if (bIsErrorCode(parse.uCode)) {
            HandleErrorCode(line, &parse, &ircPrint);
        } else {
            HandleResultCode(displayLine, line, &parse, &ircPrint);
        }
    } else {
        HandleCommand(displayLine, line, &parse, &ircPrint);
    }
}
