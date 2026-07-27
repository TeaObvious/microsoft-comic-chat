// Ported from v2.5-beta-1-modern/ircproto.cpp.

#include "ircproto.h"

#include "chat.h"
#include "chatdoc.h"
#include "ccommon.h"
#include "format.h"
#include "histent.h"
#include "intl.h"
#include "ircsock.h"
#include "mainfrm.h"
#include "notif.h"
#include "originalassets.h"
#include "protsupp.h"
#include "roomlist.h"
#include "setupdlg.h"
#include "userlist.h"
#include "userinfo.h"

#include <QByteArray>
#include <QLineEdit>
#include <QRegularExpression>

CIrcSocket serverConn;
static CIrcProto* g_defaultIrcProto = nullptr;

long GetMyIP()
{
    return static_cast<long>(serverConn.LocalIPv4Address());
}

namespace {
constexpr auto kCcudi1 = "CCUDI1";
constexpr char kActionId[] = "\001ACTION";
constexpr short kActionLength = 7;
constexpr short kSoundLength = 6;
constexpr short kAwayLength = 5;
constexpr short kHeresInfoLength = 12;

QString maybeString(void* pvData)
{
    return pvData ? QString::fromLatin1(static_cast<const char*>(pvData))
                  : QString();
}

QByteArray sourceCodePageBytes(QStringView text, UINT codePage = GetACP())
{
    QByteArray bytes;
    if (!bWideToCodePage(text, codePage, &bytes)) {
        return text.toString().toLatin1();
    }
    return bytes;
}

QString sourceCodePageText(const QByteArray& bytes, UINT codePage = GetACP())
{
    QString text;
    if (!bCodePageToWide(bytes, codePage, &text)) {
        return QString::fromLatin1(bytes);
    }
    return text;
}

QString sourceCodePageRoundTrip(QStringView text, UINT codePage = GetACP())
{
    return sourceCodePageText(sourceCodePageBytes(text, codePage), codePage);
}

QString sourceCodePageCarrier(QStringView text, UINT codePage = GetACP())
{
    return QString::fromLatin1(sourceCodePageBytes(text, codePage));
}

bool originalSpace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'
        || ch == '\v' || ch == '\f';
}

const char* nextEncodedCharacter(int encoding, const char* position,
                                 const char* end)
{
    if (position >= end) return end;
    if (encoding == ENC_DBCS) {
        const char* next = CharNextEx(GetACP(), position);
        return qMin(next, end);
    }
    if (encoding != ENC_UTF8) return position + 1;
    return qMin(SzNextUTF8Char(position), end);
}

QByteArray lowLevelQuote(const QByteArray& source)
{
    QByteArray result;
    BOOL freeResult = FALSE;
    bLowLevelQuoting(g_chLLQuoteCTCP, TRUE, source, &result, &freeResult);
    return result;
}

short nGetBreakingPoint(int encoding, const char* body, short bodyLength,
                        short maxLength, WORD formatBegin,
                        char* formatBeginBytes, WORD* formatEnd)
{
    const short formatBeginLength = nFillFormatting(
        formatBeginBytes, 0, formatBegin, *body);
    maxLength = static_cast<short>(maxLength - formatBeginLength);
    *formatEnd = 0;
    if (bodyLength <= maxLength) return bodyLength;
    if (maxLength <= 2) return 0;

    const char* scan = body;
    const char* end = body + bodyLength;
    const char* furthestSpaceStart = nullptr;
    const char* furthestFormattingStart = nullptr;
    const char* validSpaceStart = body + static_cast<int>(maxLength * 0.8);
    bool inSpaces = false;
    WORD lastFullFormat = formatBegin;
    const char* limit = qMin(body + maxLength - 2, end);
    do {
        switch (*scan) {
        case chCtlColor:
        case chCtlBold:
        case chCtlItalic:
        case chCtlFixedPitchFont:
        case chCtlUnderline:
        case chCtlSymbol:
            if (!furthestFormattingStart) furthestFormattingStart = scan;
            scan = SzSkipOneFormat(scan, &formatBegin);
            break;
        default:
            furthestFormattingStart = nullptr;
            lastFullFormat = formatBegin;
            if (originalSpace(*scan)) {
                if (!inSpaces) {
                    *formatEnd = formatBegin;
                    furthestSpaceStart = scan;
                    inSpaces = true;
                }
            } else {
                inSpaces = false;
            }
            scan = nextEncodedCharacter(encoding, scan, end);
            break;
        }
    } while (scan < limit);

    if (furthestSpaceStart && furthestSpaceStart >= validSpaceStart) {
        return static_cast<short>(furthestSpaceStart - body);
    }
    *formatEnd = lastFullFormat;
    if (furthestFormattingStart) {
        return static_cast<short>(furthestFormattingStart - body - 1);
    }
    return static_cast<short>(scan - body);
}

void sendWire(CIrcProto* protocol, const QByteArray& bytes)
{
    protocol->SendMessageBytes(bytes);
}

QString trimOuterQuotes(QString value)
{
    if (value.size() >= 2 && value.front() == QLatin1Char('"')
        && value.back() == QLatin1Char('"')) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}
}

bool bExtendedNickname(const QString& nickname)
{
    return bExtendedWideNickname(QStringView(nickname));
}

QString EncodeNick(const QString& nickname, bool escapeWildcards)
{
    QByteArray encoded;
    const QString sourceText = sourceCodePageRoundTrip(QStringView(nickname));
    if (!bConvertWideStringToUTF8(QStringView(sourceText), 0, &encoded, nullptr,
                                  TRUE, FALSE, TRUE, escapeWildcards)) {
        return nickname;
    }
    return QString::fromLatin1(encoded);
}

QString DecodeNick(const QString& nickname)
{
    if (!nickname.startsWith(QLatin1Char('\''))) return nickname;
    QString decoded;
    if (!bConvertUTF8StringToWide(nickname.toLatin1(), 0, &decoded, nullptr,
                                  TRUE, FALSE, TRUE)) {
        return nickname;
    }
    return sourceCodePageRoundTrip(QStringView(decoded));
}

QString DecodeNickForScreen(const QString& nickname)
{
    const QString decoded = DecodeNick(nickname);
    for (QChar character : decoded) {
        if (character.isSpace()
            || character.category() == QChar::Other_Control) {
            return QLatin1Char('"') + decoded + QLatin1Char('"');
        }
    }
    return decoded;
}

QString EncodeChan(const QString& channel)
{
    if (channel.isEmpty()) {
        return channel;
    }
    if (channel.startsWith(QLatin1Char('#'))
        || channel.startsWith(QLatin1Char('&'))) {
        const QByteArray local = sourceCodePageBytes(QStringView(channel));
        if (theApp.m_charSet == ANSI_CHARSET)
            return QString::fromLatin1(local);
        QByteArray converted;
        BOOL changed = FALSE;
        if (!bConvertString(FALSE, theApp.m_charSet, local, &converted,
                            &changed)) {
            return channel;
        }
        return QString::fromLatin1(converted);
    }
    QByteArray encoded;
    const QString sourceText = sourceCodePageRoundTrip(QStringView(channel));
    if (!bConvertWideStringToUTF8(QStringView(sourceText), 0, &encoded, nullptr,
                                  FALSE, TRUE, TRUE, FALSE)) {
        return channel;
    }
    return QString::fromLatin1(encoded);
}

QString DecodeChan(const QString& channel, bool forceDbcs)
{
    if (channel.isEmpty() || (!channel.startsWith(QLatin1Char('%'))
                              && !channel.startsWith(QLatin1Char('#'))
                              && !channel.startsWith(QLatin1Char('&')))) {
        return channel;
    }

    const QChar firstCharacter = channel.front();
    QByteArray local;
    UINT localCodePage = GetACP();
    QString decoded;
    int prefixLength = 0;
    if (firstCharacter == QLatin1Char('%')) {
        if (!bConvertUTF8StringToWide(channel.toLatin1(), 0, &decoded,
                                      nullptr, FALSE, TRUE, TRUE)) {
            return sourceCodePageText(channel.toLatin1());
        }
        localCodePage = forceDbcs ? 1252U : GetACP();
        local = sourceCodePageBytes(QStringView(decoded), localCodePage);
        if (local.size() > 1 && local.front() == '%') {
            prefixLength = 2;
        }
    } else {
        local = channel.toLatin1();
    }

    if (firstCharacter == QLatin1Char('#')
        || firstCharacter == QLatin1Char('&') || forceDbcs) {
        if (theApp.m_charSet != ANSI_CHARSET) {
            QByteArray converted;
            BOOL changed = FALSE;
            if (!bConvertString(TRUE, theApp.m_charSet, local, &converted,
                                &changed)) {
                return sourceCodePageText(local, localCodePage).mid(prefixLength);
            }
            local = converted;
            localCodePage = GetACP();
        }
    }

    return sourceCodePageText(local, localCodePage).mid(prefixLength);
}

QString DecodeString(const QByteArray& string, int encoding)
{
    if (string.isEmpty()) return QString();
    if (encoding == ENC_DBCS) {
        QByteArray local = string;
        if (theApp.m_charSet != ANSI_CHARSET) {
            QByteArray converted;
            BOOL changed = FALSE;
            if (!bConvertString(TRUE, theApp.m_charSet, string, &converted,
                                &changed)) {
                return sourceCodePageText(string);
            }
            local = converted;
        }
        return sourceCodePageText(local);
    }

    QString decoded;
    if (!bConvertUTF8StringToWide(string, 0, &decoded, nullptr,
                                  FALSE, FALSE, FALSE)) {
        return sourceCodePageText(string);
    }
    return sourceCodePageRoundTrip(QStringView(decoded));
}

void GetModeChars(DWORD flags, char* buffer)
{
    if (!buffer) return;
    char* output = buffer;
    if (flags & CM_PRIVATE) *output++ = 'p';
    if (flags & CM_HIDDEN) *output++ = 's';
    if (flags & CM_INVITEONLY) *output++ = 'i';
    if (flags & CM_TOPICHOST) *output++ = 't';
    if (flags & CM_NOEXTERN) *output++ = 'n';
    if (flags & CM_MODERATED) *output++ = 'm';
    if (flags & CM_USERLIMIT) *output++ = 'l';
    if (flags & CM_CHANNELKEY) *output++ = 'k';
    *output = '\0';
}

void ChatFillRoomList(CRoomList* roomList)
{
    if (!roomList || !roomList->m_persist || !GetIrcProto()) return;
    const QString query = roomList->m_persist->m_strQuery;
    QString parameter;
    enumCommandType command = ctMax;
    if (!query.isEmpty()) {
        const QStringList tokens = query.split(
            QRegularExpression(QStringLiteral("\\s+")),
            Qt::SkipEmptyParts);
        if (!tokens.isEmpty()) {
            command = tokens.first().compare(QStringLiteral("LIST"),
                                              Qt::CaseInsensitive) == 0
                ? ctList : ctListX;
            if (tokens.size() > 1) parameter = tokens.at(1);
        }
    }
    if (serverConn.m_bIrcXServer) {
        g_bCanViewUnrated = bCanViewUnrated();
        if (query.isEmpty()) command = ctListX;
    } else if (query.isEmpty()) {
        command = ctList;
    }
    if (command != ctMax) {
        GetIrcProto()->bExecuteQuery(qpRoomListDlg, command, dtMax,
                                     nullptr, parameter, QString());
    }
}

void ChatFillUserList(CUserList* userList)
{
    if (!userList || !userList->m_persist || !GetIrcProto()) return;
    QString user;
    QString channel;
    if (!userList->m_persist->m_strQuery.isEmpty()) {
        QString tail = userList->m_persist->m_strQuery.mid(3);
        while (!tail.isEmpty() && tail.front().isSpace()) tail.remove(0, 1);
        user = tail.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
    } else if (userList->m_persist->m_searchType == USERSEARCH_ALL) {
        user.clear();
    } else if (userList->m_persist->m_searchType == USERSEARCH_NICK
               || userList->m_persist->m_searchType == USERSEARCH_ID) {
        user = userList->m_user->text();
        while (!user.isEmpty() && user.front().isSpace()) user.remove(0, 1);
        userList->m_persist->m_strUserFilter = user;
        if (!user.isEmpty()) {
            if (userList->m_persist->m_searchType == USERSEARCH_NICK
                && serverConn.m_bIrcXServer) {
                user = QStringLiteral("'*") + EncodeNick(user, true).mid(1);
            } else {
                user.prepend(QLatin1Char('*'));
            }
            user.append(QLatin1Char('*'));
        }
    } else if (userList->m_persist->m_searchType == USERSEARCH_ROOM) {
        if (!userList->m_persist->m_strEncRoom.isEmpty()) {
            channel = userList->m_persist->m_strEncRoom;
        } else {
            channel = userList->m_ctlRoom->text();
            while (!channel.isEmpty() && channel.front().isSpace())
                channel.remove(0, 1);
            channel = EncodeChan(channel);
        }
    }
    GetIrcProto()->bExecuteQuery(qpUserListDlg, ctWho, dtMax, nullptr,
                                 channel, user);
}

void FixMICChannelName(CChatDoc* doc, CRoomInfo* enterRoom)
{
    if (!doc || !doc->m_proto || !enterRoom) return;
    doc->m_proto->m_strPrettyChannel = DecodeChan(
        doc->m_proto->m_strChannel, true);
    ChatSetChannel(DecodeChan(enterRoom->m_strChannel, true));
    doc->SetLegalPath(doc->m_proto->m_strPrettyChannel);
    doc->m_proto->SetConnectionStatus(doc->GetConnectionStatus());
}

CIrcProto::CIrcProto()
{
    m_pSock = &serverConn;
    serverConn.AttachProtocol(this);
    currentRoom = this;
}

CIrcProto::~CIrcProto()
{
    serverConn.DetachProtocol(this);
}

CRoomInfo* NewDefaultProto(CChatDoc* doc)
{
    auto* proto = new CIrcProto;
    proto->m_pSock = &serverConn;
    proto->m_doc = doc;
    return proto;
}

CIrcProto* GetIrcProto()
{
    return g_defaultIrcProto;
}

bool CommunicationInits()
{
    if (g_defaultIrcProto) return true;
    g_defaultIrcProto = static_cast<CIrcProto*>(NewDefaultProto(nullptr));
    return g_defaultIrcProto != nullptr;
}

void CommunicationCleanup()
{
    delete g_defaultIrcProto;
    g_defaultIrcProto = nullptr;
}

bool CIrcProto::ConnectToServer(const QString& server, const QString& nick,
                                const QString& realName, const QString& channel,
                                int onConnectAction)
{
    ChatSetServer(server);
    SetMyName(nick);
    SetMyRealName(realName);
    ChatSetChannel(channel);
    theApp.m_iOnConnectAction = onConnectAction;
    g_enterInfo.m_strChannel = channel;
    g_enterInfo.m_strPassword.clear();
    return bChatServerConnect(QString::fromUtf8(GetMyServer()));
}

void CIrcProto::Disconnect()
{
    ChatServerDisconnect(TRUE, FALSE);
}

void CIrcProto::OnLogin()
{
    CChatService* AddToServerList(const QString& service);

    SetConnectionStatus(CX_NOCHANNEL);
    theApp.m_bInSearch = false;
    if (theApp.m_dynaRules.bDaemonNeeded()) {
        theApp.m_dynaRules.bStartRulesDaemon(
            g_uRulesDaemonShortElapse, TRUE);
    }
    if (theApp.m_dynaNotifs.bDaemonNeeded()) {
        theApp.m_dynaNotifs.bStartNotifsDaemon(
            g_uNotifsDaemonShortElapse, TRUE);
    }
    AddToServerList(QString::fromUtf8(GetMyServer()));
    SetVisibility((theApp.m_flags1 & F1_USERVISIBLE) != 0);
    int onConnectAction = theApp.m_iOnConnectAction;
    if (theApp.m_bLoadURL) {
        onConnectAction = CA_JOINROOM;
        theApp.m_bLoadURL = false;
    }
    if (onConnectAction == CA_JOINROOM) {
        ChatJoinChannel(g_enterInfo);
    } else if (onConnectAction == CA_ROOMLIST) {
        theApp.OnChatroomList();
    }
}

void CIrcProto::ChatJoinChannel(CRoomInfo& enterInfo)
{
    ChatJoinAux(enterInfo);
}

void CIrcProto::ChatJoinAux(CRoomInfo& enterInfo)
{
    Q_ASSERT(!enterInfo.m_strChannel.isEmpty());
    if (enterInfo.m_strPassword.isEmpty()) {
        SendMessageText(QStringLiteral("JOIN %1\r\n").arg(enterInfo.m_strChannel));
    } else {
        const int encoding = enterInfo.m_strChannel.startsWith(QLatin1Char('#'))
                || enterInfo.m_strChannel.startsWith(QLatin1Char('&'))
            ? ENC_DBCS : ENC_UTF8;
        SendMessageText(QStringLiteral("JOIN %1 %2\r\n").arg(
            enterInfo.m_strChannel,
            EncodeString(enterInfo.m_strPassword, encoding)));
    }
}

void CIrcProto::ChatCreateChannel(CRoomInfo& enterInfo)
{
    ChatCreateAux(enterInfo);
}

void CIrcProto::ChatCreateAux(CRoomInfo& enterInfo)
{
    if (enterInfo.m_strChannel.isEmpty()) return;
    QString parameters;
    if (!enterInfo.m_strCreationModes.isEmpty()) {
        parameters += QLatin1Char(' ') + enterInfo.m_strCreationModes;
    }
    if (enterInfo.m_dwMaxUsers) {
        parameters += QLatin1Char(' ')
            + QString::number(enterInfo.m_dwMaxUsers);
    }
    if (!enterInfo.m_strPassword.isEmpty()) {
        const int encoding = enterInfo.m_strChannel.startsWith(QLatin1Char('#'))
                || enterInfo.m_strChannel.startsWith(QLatin1Char('&'))
            ? ENC_DBCS : ENC_UTF8;
        parameters += QLatin1Char(' ')
            + EncodeString(enterInfo.m_strPassword, encoding);
    }
    SendMessageText(QStringLiteral("CREATE %1%2\r\n")
                        .arg(enterInfo.m_strChannel, parameters));
}

void CIrcProto::ChatPartChannel(CChatDoc* doc, bool)
{
    if (m_bInRoom && !m_strChannel.isEmpty()) {
        SendMessageText(QStringLiteral("PART %1\r\n").arg(m_strChannel));
    }

    if (GetConnectionStatus() == CX_INCHANNEL) {
        SetConnectionStatus(CX_NOCHANNEL);
        if (doc && doc->m_proto && !m_strChannel.isEmpty()
            && !doc->m_bStatusView) {
            QString server = QString::fromUtf8(GetMyServer());
            QString identity = theApp.m_myNick + QLatin1Char('!')
                + theApp.m_myIdent;
            QString channel = m_strChannel;
            QString eventMessage;
            theApp.m_dynaRules.bMatchAndApplyRules(
                eOnLeave, nullptr, nullptr, server, identity,
                channel, eventMessage);
        }
    }
}

void CIrcProto::ChatSendSay(const QString& text)
{
    bChatSendToChannel(QString(), text, nullptr, BM_SAY);
}

void CIrcProto::ChatSendAction(const QString& text)
{
    QString msg = QStringLiteral("\001ACTION %1\001").arg(text);
    bChatSendToChannel(QString(), msg, nullptr, BM_ACTION);
}

bool CIrcProto::bChatSendToTarget(const QString& addressee, const QString& annotations,
                                  const QString& message, unsigned short modes,
                                  bool asNotice)
{
    const QString target = addressee.isEmpty() ? m_strChannel : addressee;
    if (target.isEmpty()) return false;

    const int encoding = addressee.isEmpty() ? EncodingType() : ENC_DBCS;
    const QByteArray targetBytes = target.toLatin1();
    const QByteArray annotationBytes = annotations.toLatin1();
    const QByteArray messageBytes = lowLevelQuote(
        EncodeStringBytes(message, encoding));
    const QByteArray command = asNotice ? QByteArrayLiteral("NOTICE")
                                        : QByteArrayLiteral("PRIVMSG");

    int receivingPrefixLength = 2
        + sourceCodePageBytes(QStringView(GetMyNickName())).size();
    if (theApp.m_nMyIdentLength) {
        receivingPrefixLength += theApp.m_nMyIdentLength;
    } else {
        GetMyUserName();
        receivingPrefixLength += sourceCodePageBytes(
            QStringView(theApp.m_strUserName)).size() + 32;
    }
    const int maximum = m_pSock ? m_pSock->m_nMaxMsgLength : g_nDefaultIOBuff;
    const int receivedLength = 12 + targetBytes.size() + annotationBytes.size()
        + messageBytes.size() + receivingPrefixLength;

    if (receivedLength <= maximum) {
        if (!annotationBytes.isEmpty() && IsIRCX()) {
            sendWire(this, QByteArrayLiteral("DATA ") + targetBytes + ' '
                     + kCcudi1 + QByteArrayLiteral(" :") + annotationBytes
                     + QByteArrayLiteral("\r\n"));
            if (!messageBytes.isEmpty()) {
                sendWire(this, command + ' ' + targetBytes + QByteArrayLiteral(" :")
                         + messageBytes + QByteArrayLiteral("\r\n"));
            }
        } else {
            sendWire(this, command + ' ' + targetBytes + QByteArrayLiteral(" :")
                     + annotationBytes + messageBytes + QByteArrayLiteral("\r\n"));
        }
        return true;
    }

    if (messageBytes.isEmpty()) return false;
    short prefixLength = 0;
    short suffixLength = 1;
    switch (modes) {
    case BM_ACTION:
        prefixLength = kActionLength + 1;
        break;
    case BM_SOUND:
        prefixLength = kSoundLength + 1;
        break;
    case BM_AWAY:
        prefixLength = kAwayLength + 1;
        break;
    case BM_HERESINFO:
        prefixLength = kHeresInfoLength + 1;
        suffixLength = 0;
        break;
    case BM_SAY:
    case BM_THINK:
    case BM_WHISPER:
        suffixLength = 0;
        break;
    default:
        return false;
    }
    if (prefixLength > messageBytes.size()) return false;

    QByteArray prefix = messageBytes.left(prefixLength);
    QByteArray body = messageBytes.mid(prefixLength);
    WORD formatBegin = 0;
    unsigned short chunkModes = modes;
    const bool onlySendOneChunk = encoding == ENC_DBCS && GetACP() == 932;
    do {
        char formatBeginBytes[11]{};
        WORD formatEnd = 0;
        const int available = maximum - receivingPrefixLength - 12
            - targetBytes.size() - annotationBytes.size() - prefixLength
            - suffixLength;
        const short breakingPoint = nGetBreakingPoint(
            encoding, body.constData(), static_cast<short>(body.size()),
            static_cast<short>(available), formatBegin, formatBeginBytes,
            &formatEnd);
        if (breakingPoint <= 0) return false;

        QByteArray chunkBody = body.left(breakingPoint);
        if (suffixLength) chunkBody += '\001';
        const QByteArray continuedFormat(formatBeginBytes);
        if (!annotationBytes.isEmpty() && IsIRCX()) {
            sendWire(this, QByteArrayLiteral("DATA ") + targetBytes + ' '
                     + kCcudi1 + QByteArrayLiteral(" :") + annotationBytes
                     + QByteArrayLiteral("\r\n"));
            sendWire(this, command + ' ' + targetBytes + QByteArrayLiteral(" :")
                     + prefix + continuedFormat + chunkBody
                     + QByteArrayLiteral("\r\n"));
        } else {
            sendWire(this, command + ' ' + targetBytes + QByteArrayLiteral(" :")
                     + annotationBytes + prefix + continuedFormat + chunkBody
                     + QByteArrayLiteral("\r\n"));
        }

        body.remove(0, breakingPoint);
        while (!body.isEmpty() && originalSpace(body.front())) body.remove(0, 1);
        formatBegin = formatEnd;
        if (chunkModes == BM_SOUND) {
            chunkModes = BM_ACTION;
            prefixLength = kActionLength + 1;
            prefix = QByteArray(kActionId, kActionLength) + ' ';
        }
    } while (!body.isEmpty() && !onlySendOneChunk);
    return true;
}

int CIrcProto::EncodingType() const
{
    return m_strChannel.startsWith(QLatin1Char('%')) && !(m_dwModes & CM_MIC)
        ? ENC_UTF8 : ENC_DBCS;
}

QString CIrcProto::EncodeString(const QString& string, int encoding) const
{
    const QByteArray encoded = EncodeStringBytes(string, encoding);
    if (encoding == ENC_CHANNEL) encoding = EncodingType();
    return QString::fromLatin1(encoded);
}

QByteArray CIrcProto::EncodeStringBytes(const QString& string,
                                        int encoding) const
{
    if (encoding == ENC_CHANNEL) encoding = EncodingType();
    if (encoding == ENC_DBCS) {
        const QByteArray local = sourceCodePageBytes(QStringView(string));
        if (theApp.m_charSet == ANSI_CHARSET) return local;
        QByteArray converted;
        BOOL changed = FALSE;
        if (!bConvertString(FALSE, theApp.m_charSet, local, &converted,
                            &changed)) {
            return local;
        }
        return converted;
    }
    QByteArray encoded;
    const QString sourceText = sourceCodePageRoundTrip(QStringView(string));
    if (!bConvertWideStringToUTF8(QStringView(sourceText), 0, &encoded, nullptr,
                                  FALSE, FALSE, FALSE, FALSE)) {
        return sourceCodePageBytes(QStringView(string));
    }
    return encoded;
}

QString CIrcProto::StrEncodeCommandParam(DWORD argumentType, int* encoding,
                                         QString parameter) const
{
    if (!encoding) return parameter;
    DWORD type = argumentType
        & (AT_CHANNEL | AT_NICKNAME | AT_NICKMASK | AT_REASON | AT_PASSWORD
           | AT_TOPIC | AT_MESSAGE | AT_PROPVALUE);
    QString encodedChannel;

    if (!type) return parameter;

    if ((type & AT_CHANNEL) && (type & AT_NICKNAME)) {
        if (parameter.startsWith(QLatin1Char('%'))
            || parameter.startsWith(QLatin1Char('#'))
            || parameter.startsWith(QLatin1Char('&'))) {
            type &= ~AT_NICKNAME;
        } else {
            encodedChannel = EncodeChan(parameter);
            if (LookupDoc(encodedChannel)) {
                type &= ~AT_NICKNAME;
            } else {
                type &= ~AT_CHANNEL;
                encodedChannel.clear();
            }
        }
    }

    if (!encodedChannel.isEmpty()) {
        *encoding = ENC_UTF8;
        return encodedChannel;
    }

    switch (type) {
    case AT_CHANNEL:
        *encoding = parameter.startsWith(QLatin1Char('&'))
                || parameter.startsWith(QLatin1Char('#'))
            ? ENC_DBCS : ENC_UTF8;
        return EncodeChan(parameter);

    case AT_NICKNAME | AT_NICKMASK:
    case AT_NICKNAME:
    case AT_NICKMASK: {
        QString nickPortion = parameter;
        QString nickname = trimOuterQuotes(parameter);
        if (type & AT_NICKMASK) {
            const qsizetype bang = parameter.indexOf(QLatin1Char('!'));
            if (bang >= 0) {
                qsizetype begin = 0;
                while (begin < bang
                       && (parameter.at(begin) == QLatin1Char('?')
                           || parameter.at(begin) == QLatin1Char('*'))) {
                    ++begin;
                }
                nickPortion = parameter.mid(begin, bang - begin);
            }
        }
        if (IsIRCX() && bExtendedNickname(nickPortion))
            return EncodeNick(nickname);
        return QString::fromLatin1(sourceCodePageBytes(QStringView(nickname)));
    }

    default:
        return EncodeString(parameter, *encoding);
    }
}

bool CIrcProto::bChatSendToChannel(const QString& annotations, const QString& message,
                                   QString*, unsigned short modes)
{
    return bChatSendToTarget(QString(), annotations, message, modes, false);
}

bool CIrcProto::bChatSendPrivMesg(const QString& addressee, const QString& annotations,
                                  const QString& message, QString*, bool asNotice,
                                  unsigned short modes)
{
    return bChatSendToTarget(addressee, annotations, message, modes, asNotice);
}

bool CIrcProto::ChatChangeNick(const QString& newNick)
{
    if (newNick.isEmpty()) {
        return false;
    }
    const QString wireNickname = IsIRCX() && bExtendedNickname(newNick)
        ? EncodeNick(newNick) : sourceCodePageCarrier(QStringView(newNick));
    SendMessageText(QStringLiteral("NICK %1\r\n").arg(wireNickname));
    return true;
}

bool CIrcProto::ChatKickUser(const QString& nickname,
                             const QString& reason)
{
    const QString wireReason = reason.isEmpty()
        ? QString() : EncodeString(reason);
    SendMessageText(QStringLiteral("KICK %1 %2 :%3\r\n")
                        .arg(m_strChannel, nickname, wireReason));
    return true;
}

void CIrcProto::ChatKickUser(CUserInfo* pui)
{
    if (!pui) return;
    bExecuteQuery(qpKickDlg, ctWhoIs, dtMax, nullptr,
                  m_strChannel, pui->GetName());
}

void CIrcProto::ChatBanUser(CUserInfo* pui)
{
    if (pui) {
        bExecuteQuery(qpBanDlg, ctWhoIs, dtMax, nullptr,
                      m_strChannel, pui->GetName());
    } else {
        SendMessageText(QStringLiteral("MODE %1 +b\r\n").arg(m_strChannel));
    }
}

bool CIrcProto::ChatBanUser(const QString& banPattern, BOOL ban,
                            const QString& encodedChannel)
{
    QString wirePattern = banPattern;
    const qsizetype bang = banPattern.indexOf(QLatin1Char('!'));
    QString nickname = bang >= 0 ? banPattern.left(bang) : banPattern;
    while (!nickname.isEmpty()
           && (nickname.front() == QLatin1Char('?')
               || nickname.front() == QLatin1Char('*'))) {
        nickname.remove(0, 1);
    }
    if (IsIRCX() && bExtendedNickname(nickname))
        wirePattern = EncodeNick(banPattern);

    const QString channel = encodedChannel.isEmpty()
        ? m_strChannel : encodedChannel;
    SendMessageText(QStringLiteral("MODE %1 %2b %3\r\n")
                        .arg(channel, ban ? QStringLiteral("+")
                                          : QStringLiteral("-"),
                             wirePattern));
    return true;
}

bool CIrcProto::ChatSendInvitation(const QString& nickname)
{
    SendMessageText(QStringLiteral("INVITE %1 %2\r\n")
                        .arg(nickname, m_strChannel));
    return true;
}

void CIrcProto::ChatSetNick(const QString& nickname)
{
    QString newNickname = nickname;
    const QString oldNickname = QString::fromUtf8(GetMyName());
    if (GetConnectionStatus() != CX_DISCONNECTED) {
        ChatChangeNick(newNickname);
    } else if (newNickname != oldNickname) {
        AddAndExecute(new NickEntry(oldNickname, newNickname));
    }
}

bool CIrcProto::ChatSetTopic(const QString& topic)
{
    const QByteArray encoded = EncodeStringBytes(topic);
    return bExecuteQuery(qpSetTopic, ctTopic, dtMax,
                         const_cast<char*>(encoded.constData()),
                         m_strChannel, QString());
}

bool CIrcProto::ChatSetMode(DWORD newMode, DWORD newMaxUsers,
                            const QString& newPassword)
{
    char maximumUsers[7] = "";
    DWORD newSets = newMode & ~m_dwModes;
    DWORD newUnsets = m_dwModes & ~newMode;

    if ((newMode & CM_USERLIMIT) && newMaxUsers != m_dwMaxUsers) {
        newSets |= CM_USERLIMIT;
        qsnprintf(maximumUsers, sizeof(maximumUsers), "%lu",
                  static_cast<unsigned long>(newMaxUsers));
    } else {
        newSets &= ~DWORD(CM_USERLIMIT);
    }

    if ((newMode & CM_CHANNELKEY) && newPassword != m_strPassword) {
        newSets |= CM_CHANNELKEY;
        if (!m_strPassword.isEmpty()) newUnsets |= CM_CHANNELKEY;
    } else {
        newSets &= ~DWORD(CM_CHANNELKEY);
    }

    char modeBuffer[20];
    GetModeChars(newUnsets, modeBuffer);
    if (*modeBuffer) {
        const QString key = (newUnsets & CM_CHANNELKEY)
            ? EncodeString(m_strPassword) : QString();
        SendMessageText(QStringLiteral("MODE %1 -%2 %3\r\n")
                            .arg(m_strChannel,
                                 QString::fromLatin1(modeBuffer), key));
    }

    GetModeChars(newSets, modeBuffer);
    if (*modeBuffer) {
        const QString key = (newSets & CM_CHANNELKEY)
            ? EncodeString(newPassword) : QString();
        SendMessageText(QStringLiteral("MODE %1 +%2 %3 %4\r\n")
                            .arg(m_strChannel,
                                 QString::fromLatin1(modeBuffer),
                                 QString::fromLatin1(maximumUsers), key));
    }
    return true;
}

bool CIrcProto::bChatShowMOTD()
{
    return bExecuteQuery(qpLUsersMOTD, ctLUsersMOTD, dtMax, nullptr,
                         QString(), QString());
}

bool CIrcProto::ChatSetClientData(const QString& clientData)
{
    if (!IsIRCX() || !m_pSock) return false;
    const QByteArray encoded = clientData.isEmpty()
        ? QByteArray() : EncodeStringBytes(clientData);
    return bExecuteQuery(qpSetClient, ctPropSet, dtMax,
                         const_cast<char*>(encoded.constData()),
                         m_strChannel, QString());
}

void CIrcProto::HandleClientDataChange(const QString& newClientData)
{
    QString property;
    QString value;
    for (int pass = 0; pass < 2; ++pass) {
        QString remaining = pass == 0 ? newClientData : m_strClientData;
        const QString& other = pass == 0 ? m_strClientData : newClientData;
        while (EnumKeyString(remaining, property, value)) {
            QString otherValue;
            if (!GetValueFromKeyString(other, property, otherValue)) {
                if (pass == 0) OnPropertyChange(property, &value);
                else OnPropertyChange(property, nullptr);
            } else if (pass == 0 && otherValue != value) {
                OnPropertyChange(property, &value);
            }
        }
    }
    m_strClientData = newClientData;
}

bool CIrcProto::ChangeProperty(CUserInfo* puiSelf, const QString& property,
                               const QString* value)
{
    if (!IsIRCX() || !puiSelf || !puiSelf->IsOwner()) return false;
    if (!ChangeKeyString(m_strClientData, property, value, 255)) return false;
    return ChatSetClientData(m_strClientData);
}

void CIrcProto::ChatSetAway(bool away, const QString& message,
                            CUserInfo* pui, bool protoNotify)
{
    if (!protoNotify) {
        CRoomInfo::ChatSetAway(away, message, pui, false);
        return;
    }

    SendMessageText(away ? QStringLiteral("AWAY :%1\r\n").arg(
                               sourceCodePageCarrier(QStringView(message)))
                         : QStringLiteral("AWAY\r\n"));
    for (CChatDoc* document : g_docs) {
        if (document && document->m_proto
            && document->m_proto->GetConnectionStatus() == CX_INCHANNEL) {
            document->m_proto->CRoomInfo::ChatSetAway(
                away, message, nullptr, false);
        }
    }
}

void CIrcProto::DoIgnoreUser(CUserInfo* pui, bool ignore, bool autoIgnore,
                             const QString& nickname)
{
    if (pui && pui->Ignored() == ignore) return;
    if (!pui && nickname.isEmpty()) return;
    if (!pui || pui->GetFullName().isEmpty()) {
        const QString nick = pui ? pui->GetName() : nickname;
        WORD flags = ignore ? g_wIgnoreIdent : 0;
        if (autoIgnore) flags = static_cast<WORD>(flags + g_wAutoIgnoreIdent);
        bExecuteQuery(qpIgnoreIdent, ctWhoIs, dtFlags,
                      reinterpret_cast<void*>(static_cast<quintptr>(flags)),
                      QString(), nick);
        return;
    }
    IgnoreUser(pui->GetName(), pui->GetFullName(), ignore, autoIgnore);
}

void CIrcProto::ChatGetIdentity(CUserInfo* pui, const QString& nickname)
{
    if (!pui || pui->GetFullName().isEmpty()) {
        const QString nick = pui ? pui->GetName() : nickname;
        if (!nick.isEmpty()) {
            bExecuteQuery(qpGetIdent, ctWhoIs, dtMax, nullptr,
                          QString(), nick);
        }
        return;
    }

    const QString identity = pui->GetFullName();
    const qsizetype separator = identity.indexOf(QLatin1Char('@'));
    const QString user = separator >= 0 ? identity.left(separator) : identity;
    const QString host = separator >= 0 ? identity.mid(separator + 1)
                                        : QString();
    ShowIdentity(pui->GetName(), user, host);
}

bool CIrcProto::bExecuteQuery(enumQueryPurpose qp, enumCommandType ct, enumDataType dt, void* pvData,
                              const QString& channelName, const QString& nicknameMask)
{
    if (GetConnectionStatus() == CX_DISCONNECTED) {
        return false;
    }
    if (ct == ctSetUserMode
        && qp != qpSetVisible
        && qp != qpSetInvisible
        && qp != qpComSetUserMode) {
        return false;
    }
    auto* query = new CCQuery(
        qp, ct, dt, pvData, channelName, nicknameMask,
        !nicknameMask.isEmpty() && ct == ctWho);
    if (!m_pSock->m_queries.bAddQuery(query)) {
        delete query;
        return false;
    }

    switch (ct) {
    case ctWho: {
        QString filter;
        if (PPRUSERMATCH userMatch = query->GetPrUserMatch()) {
            const char* selected = nullptr;
            UINT selectedLength = 0;
            if (userMatch->cbNickname) {
                selected = userMatch->szNickname;
                selectedLength = userMatch->cbNickname;
            }
            if (userMatch->cbUserName > selectedLength) {
                selected = userMatch->szUserName;
                selectedLength = userMatch->cbUserName;
            }
            if (userMatch->cbIPAddress > selectedLength) {
                selected = userMatch->szIPAddress;
                selectedLength = userMatch->cbIPAddress;
            }
            if (selected) {
                filter = QString::fromLatin1(
                    selected, static_cast<qsizetype>(selectedLength));
            }
        }
        if (!filter.isEmpty()) {
            SendMessageText(QStringLiteral("WHO %1\r\n").arg(filter));
        } else {
            SendMessageText(channelName.isEmpty()
                ? QStringLiteral("WHO\r\n")
                : QStringLiteral("WHO %1\r\n").arg(channelName));
        }
        break;
    }
    case ctWhoIs:
        SendMessageText(QStringLiteral("WHOIS %1\r\n").arg(nicknameMask));
        break;
    case ctTopic:
        if (qp == qpSetTopic) {
            SendMessageText(QStringLiteral("TOPIC %1 :%2\r\n").arg(channelName, maybeString(pvData)));
        } else if (qp == qpListMembers) {
            SendMessageText(QStringLiteral("TOPIC %1\r\n").arg(channelName));
        }
        break;
    case ctList:
        SendMessageText(channelName.isEmpty() ? QStringLiteral("LIST\r\n")
                                              : QStringLiteral("LIST %1\r\n").arg(channelName));
        break;
    case ctListX:
        SendMessageText(channelName.isEmpty() ? QStringLiteral("LISTX\r\n")
                                              : QStringLiteral("LISTX N=%1\r\n").arg(channelName));
        break;
    case ctIrcX:
        SendMessageText(QStringLiteral("IRCX\r\n"));
        break;
    case ctGetChannelMode:
        SendMessageText(QStringLiteral("MODE %1\r\n").arg(channelName));
        break;
    case ctSetChannelMode:
        SendMessageText(QStringLiteral("MODE %1%2\r\n").arg(channelName, maybeString(pvData)));
        break;
    case ctSetUserMode:
    {
        QString modes;
        switch (qp) {
        case qpSetVisible:
            modes = QStringLiteral("-i");
            break;
        case qpSetInvisible:
            modes = QStringLiteral("+i");
            break;
        case qpComSetUserMode:
            modes = maybeString(pvData);
            break;
        default:
            break;
        }
        SendMessageText(QStringLiteral("MODE %1 %2\r\n")
                            .arg(nicknameMask, modes));
        break;
    }
    case ctLUsersMOTD:
        SendMessageText(QStringLiteral("LUSERS\r\nMOTD\r\n"));
        break;
    case ctModeIsIrcX:
        SendMessageText(QStringLiteral("MODE ISIRCX\r\n"));
        break;
    case ctPropGet:
        SendMessageText(QStringLiteral("PROP %1 %2\r\n")
                            .arg(channelName, qp == qpJoinPics ? QStringLiteral("PICS") : QStringLiteral("CLIENT")));
        break;
    case ctPropSet:
        SendMessageText(QStringLiteral("PROP %1 CLIENT :%2\r\n").arg(channelName, maybeString(pvData)));
        break;
    case ctNames:
        // Original initial-join path only queues ctNames and consumes server 353/366.
        break;
    default:
        break;
    }
    return true;
}

void CIrcProto::SetVisibility(bool visible)
{
    bExecuteQuery(visible ? qpSetVisible : qpSetInvisible,
                  ctSetUserMode, dtMax, nullptr,
                  QString(), GetMyNickName());
}

bool CIrcProto::bRegisterMode(const QString& message)
{
    const qsizetype separator = message.indexOf(QLatin1Char(' '));
    const QString target = separator < 0 ? message : message.left(separator);
    const QString parameters = separator < 0 ? QString() : message.mid(separator);
    const QByteArray parameterBytes = sourceCodePageBytes(
        QStringView(parameters));
    if (target.startsWith(QLatin1Char('#'))
        || target.startsWith(QLatin1Char('%'))
        || target.startsWith(QLatin1Char('&'))) {
        return bExecuteQuery(qpComSetChannelMode, ctSetChannelMode, dtMax,
                             const_cast<char*>(parameterBytes.constData()),
                             target, QString());
    }
    return bExecuteQuery(qpComSetUserMode, ctSetUserMode, dtMax,
                         const_cast<char*>(parameterBytes.constData()),
                         QString(), target);
}

void CIrcProto::SendMessageText(const QString& raw)
{
    if (m_pSock) {
        m_pSock->SendRaw(raw.toLatin1());
    }
}

void CIrcProto::SendMessageBytes(const QByteArray& raw)
{
    if (m_pSock) {
        m_pSock->SendRaw(raw);
    } else {
        // Keep existing protocol test adapters observable without adding a
        // second test-only send path. Latin-1 preserves each source byte.
        SendMessageText(QString::fromLatin1(raw));
    }
}

QString CIrcProto::GetMyNickName() const
{
    return theApp.m_myNick;
}

void CIrcProto::SetConnectionStatus(ConnectionStatus status)
{
    CRoomInfo::SetConnectionStatus(status);
    if (m_pSock) {
        switch (status) {
        case CX_DISCONNECTED:
        case CX_CONNECTING:
            m_pSock->m_iConnected = status;
            break;
        case CX_INCHANNEL:
            m_pSock->m_iConnected = CX_CONNECTED;
            m_bInRoom = true;
            break;
        case CX_NOCHANNEL:
            m_pSock->m_iConnected = CX_CONNECTED;
            m_bInRoom = false;
            break;
        default:
            break;
        }
    }
    if (CChatDoc* doc = m_doc) {
        QString statusText;
        switch (status) {
        case CX_DISCONNECTED:
            statusText = originalResourceString(QStringLiteral("ID_DISCONNECTED"));
            break;
        case CX_CONNECTING:
            statusText = originalResourceString(QStringLiteral("ID_CONNECTING"));
            break;
        case CX_NOCHANNEL:
            statusText = originalResourceString(QStringLiteral("ID_NOCHANNEL"));
            statusText.replace(
                QStringLiteral("%1"), GetMyServerPrettyName());
            break;
        case CX_INCHANNEL:
            statusText = originalResourceString(QStringLiteral("ID_CONNECTED"));
            statusText.replace(QStringLiteral("%1"), m_strPrettyChannel);
            statusText.replace(
                QStringLiteral("%2"), GetMyServerPrettyName());
            break;
        default:
            break;
        }
        if (!statusText.isEmpty()) {
            doc->SaveConnectStatus(statusText);
        }
        doc->ResetStatus(true, false);
    }
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->RefreshCommandUi();
}

ConnectionStatus CIrcProto::GetConnectionStatus() const
{
    if (!m_pSock) return CRoomInfo::GetConnectionStatus();
    if (m_pSock->m_iConnected == CX_DISCONNECTED
        || m_pSock->m_iConnected == CX_CONNECTING) {
        return m_pSock->m_iConnected;
    }
    return m_bInRoom ? CX_INCHANNEL : CX_NOCHANNEL;
}
