//=--------------------------------------------------------------------------=
// Actions.Cpp -- Qt port of v2.5-beta-1-modern/actions.cpp
//=--------------------------------------------------------------------------=

#include "actions.h"

#include "chat.h"
#include "chatdoc.h"
#include "ccommon.h"
#include "ircproto.h"
#include "ircsock.h"
#include "mainfrm.h"
#include "notif.h"
#include "notipage.h"
#include "originalassets.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "status.h"
#include "userinfo.h"
#include "whisprbx.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPointer>
#include <QScreen>
#include <QThread>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {
QPointer<CNotificationUsers> g_notificationBox;

QByteArray sourceAcpBytes(QStringView text)
{
    QByteArray bytes;
    if (!bWideToCodePage(text, GetACP(), &bytes)) {
        bytes = text.toString().toLatin1();
    }
    return bytes;
}

QRect makeRectVisibleOnScreen(QRect rect)
{
    QScreen* screen = QGuiApplication::screenAt(rect.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return rect;
    const QRect available = screen->availableGeometry();
    rect.setWidth(qMin(rect.width(), available.width()));
    rect.setHeight(qMin(rect.height(), available.height()));
    rect.moveLeft(qBound(available.left(), rect.left(),
                         available.right() - rect.width() + 1));
    rect.moveTop(qBound(available.top(), rect.top(),
                        available.bottom() - rect.height() + 1));
    return rect;
}

char* nextStart(char* string)
{
    while (string && (*string == ' ' || *string == '\t'
                      || *string == '\r' || *string == '\n')) {
        ++string;
    }
    return string;
}

bool messageDelimiter(const QChar character)
{
    return character.isSpace() || character.isPunct()
        || character.category() == QChar::Other_Control;
}

bool containsMessage(const QString& value, const QString& filter,
                     bool wholeWord, bool matchCase)
{
    if (value.isEmpty() || filter.isEmpty() || value.size() < filter.size()) {
        return false;
    }
    const Qt::CaseSensitivity sensitivity = matchCase
        ? Qt::CaseSensitive : Qt::CaseInsensitive;
    qsizetype from = 0;
    while ((from = value.indexOf(filter, from, sensitivity)) >= 0) {
        if (!wholeWord
            || ((from == 0 || messageDelimiter(value.at(from - 1)))
                && (from + filter.size() == value.size()
                    || messageDelimiter(value.at(from + filter.size()))))) {
            return true;
        }
        ++from;
    }
    return false;
}

bool pointerAlreadyPresent(const QList<CUserInfo*>& users, CUserInfo* user)
{
    return users.contains(user);
}

BOOL sendToChannel(CCActionContext* context, USHORT modes)
{
    if (!context) return FALSE;
    const QString channel = context->GetFinalActionParam(0);
    const QString message = context->GetFinalActionParam(1);
    if (channel.isEmpty() || message.isEmpty()) return FALSE;
    g_rgpuiWhisperees.clear();
    const QString encodedChannel = EncodeChan(channel);
    return bChatSendText(message, modes, TRUE,
                         context->GetFinalMsgFormatting(), &encodedChannel,
                         TRUE);
}

BOOL whisperToUser(CIrcProto* protocol, CCActionContext* context)
{
    if (!protocol || !context) return FALSE;
    QString users = context->GetFinalActionParam(0);
    const QString message = context->GetFinalActionParam(1);
    if (users.isEmpty() || message.isEmpty()) return FALSE;
    QList<CUserInfo*> handled;
    while (!users.isEmpty()) {
        const QString fullName = GetNextToken(users, ';', TRUE);
        const QString decodedNickname = StrExtractNickname(fullName);
        const QString ident = StrExtractIdent(fullName);
        if (decodedNickname.isEmpty()) continue;
        const QString encodedNick = protocol->IsIRCX()
            && bExtendedNickname(decodedNickname)
            ? EncodeNick(decodedNickname) : decodedNickname;
        CChatDoc* document = nullptr;
        CUserInfo* user = PuiFromDocNickIdent(
            &document, encodedNick, ident, TRUE, TRUE);
        if (user && (!document || user != document->m_puiSelf)
            && !pointerAlreadyPresent(handled, user)) {
            handled.append(user);
            WhisperBox(user, FALSE, FALSE);
            bWhisperInBox(QString(), message,
                          context->GetFinalMsgFormatting(), BM_WHISPER);
        }
    }
    return TRUE;
}

BOOL whisperToUserInChannel(CIrcProto* protocol, CCActionContext* context)
{
    if (!protocol || !context) return FALSE;
    QString identities = context->GetFinalActionParam(0);
    const QString channel = context->GetFinalActionParam(1);
    const QString message = context->GetFinalActionParam(2);
    if (channel.isEmpty() || message.isEmpty()) return FALSE;
    CChatDoc* document = LookupDoc(EncodeChan(channel));
    if (!document || document->GetConnectionStatus() != CX_INCHANNEL) return TRUE;
    if (!document->m_bObscured) g_rgpuiWhisperees.clear();
    QList<CUserInfo*> handled;
    while (!identities.isEmpty()) {
        const QString decodedNickname = StrExtractNickname(
            GetNextToken(identities, ';', TRUE));
        if (decodedNickname.isEmpty()) continue;
        const QString encodedNick = protocol->IsIRCX()
            && bExtendedNickname(decodedNickname)
            ? EncodeNick(decodedNickname) : decodedNickname;
        CUserInfo* user = LookupPui(encodedNick, document);
        if (user && user != document->m_puiSelf && !user->IsDeparted()
            && !pointerAlreadyPresent(handled, user)) {
            handled.append(user);
            if (document->m_bObscured) {
                WhisperBox(user, FALSE, FALSE);
                bWhisperInBox(QString(), message,
                              context->GetFinalMsgFormatting(), BM_WHISPER);
            } else {
                g_rgpuiWhisperees.append(user);
            }
        }
    }
    if (!document->m_bObscured && !g_rgpuiWhisperees.isEmpty()) {
        const QString encodedChannel = document->m_proto->m_strChannel;
        bChatSendText(message, BM_WHISPER, TRUE,
                      context->GetFinalMsgFormatting(), &encodedChannel, TRUE);
    }
    return TRUE;
}
}

BOOL bGetNextRange(char** string, UINT* minimum, UINT* maximum)
{
    if (!string || !*string || !minimum || !maximum) return FALSE;
    char* current = nextStart(*string);
    char* end = nullptr;
    if (!std::isdigit(static_cast<unsigned char>(*current))) goto failure;
    *minimum = static_cast<UINT>(std::strtoul(current, &end, 10));
    current = nextStart(end);
    switch (*current) {
    case ',':
        ++current;
        *maximum = *minimum;
        break;
    case '\0':
        *maximum = *minimum;
        break;
    case '-':
        current = nextStart(current + 1);
        if (!std::isdigit(static_cast<unsigned char>(*current))) goto failure;
        *maximum = static_cast<UINT>(std::strtoul(current, &end, 10));
        current = nextStart(end);
        if (*current == ',') ++current;
        else if (*current != '\0') goto failure;
        break;
    default:
        goto failure;
    }
    *string = current;
    if (*minimum > *maximum) std::swap(*minimum, *maximum);
    return TRUE;

failure:
    *minimum = *maximum = 0;
    return FALSE;
}

void TrimQuotes(QString& input)
{
    if (input.size() >= 2 && input.front() == QLatin1Char('"')
        && input.back() == QLatin1Char('"')) {
        input = input.mid(1, input.size() - 2);
    }
}

QString GetNextToken(QString& tokens, CHAR separator, BOOL trim)
{
    QString token;
    while (!tokens.isEmpty()) {
        if (trim) tokens = tokens.trimmed();
        const qsizetype index = tokens.indexOf(QLatin1Char(separator));
        if (index >= 0) {
            token = tokens.left(index);
            if (trim) token = token.trimmed();
            tokens = tokens.mid(index + 1);
            if (!token.isEmpty()) {
                while (!tokens.isEmpty()
                       && ((trim && tokens.front().isSpace())
                           || tokens.front() == QLatin1Char(separator))) {
                    tokens.remove(0, 1);
                }
                TrimQuotes(token);
                return token;
            }
        } else {
            token = trim ? tokens.trimmed() : tokens;
            TrimQuotes(token);
            tokens.clear();
            return token;
        }
    }
    return token;
}

QString StrExtractNickname(QString identity)
{
    const qsizetype bang = identity.indexOf(QLatin1Char('!'));
    return bang >= 0 && identity.indexOf(QLatin1Char('@')) >= 0
        ? identity.left(bang) : identity;
}

QString StrExtractIdent(QString identity)
{
    const qsizetype bang = identity.indexOf(QLatin1Char('!'));
    return bang >= 0 && identity.indexOf(QLatin1Char('@')) >= 0
        ? identity.mid(bang + 1) : QString();
}

BOOL bKeyEventParam(QString& parameter, enumKeyEventParam key)
{
    switch (key) {
    case kepMyActivatedRoom:
    case kepMyInactivatedRooms: {
        CChatDoc* document = LookupDoc(parameter);
        BOOL result = currentRoom && currentRoom->m_strChannel == parameter;
        if (key == kepMyInactivatedRooms) result = document && !result;
        return result;
    }
    case kepAnyone:
    case kepAny:
        return TRUE;
    case kepAnyOfMyRooms:
        return LookupDoc(parameter) != nullptr;
    case kepMe:
    case kepAnyoneButMe: {
        const QString nickname = StrExtractNickname(parameter);
        BOOL result = nickname.compare(QString::fromUtf8(GetMyNickName()),
                                       Qt::CaseInsensitive) == 0;
        return key == kepAnyoneButMe ? !result : result;
    }
    default:
        return FALSE;
    }
}

BOOL bNetValid(QString netParameter)
{
    const QString currentServer = QString::fromUtf8(GetMyServer());
    const QString service = netParameter;
    if (service.compare(currentServer, Qt::CaseInsensitive) == 0) return TRUE;
    if (!currentServer.startsWith(QLatin1Char('/'))) return FALSE;
    if (service.startsWith(QLatin1Char('/'))) {
        return currentServer.size() > service.size()
            && currentServer.at(service.size()) == QLatin1Char('/')
            && currentServer.left(service.size()).compare(
                   service, Qt::CaseInsensitive) == 0;
    }
    return currentServer.size() > service.size() + 3
        && currentServer.at(currentServer.size() - service.size() - 1)
            == QLatin1Char('/')
        && currentServer.right(service.size()).compare(
               service, Qt::CaseInsensitive) == 0;
}

BOOL bRndEventParam(QString& value, QString& filter,
                    PPRUSERMATCH userMatch, WORD flags,
                    enumParamType type)
{
    switch (type) {
    case ptNickname: {
        const qsizetype bang = value.indexOf(QLatin1Char('!'));
        const qsizetype at = value.indexOf(QLatin1Char('@'));
        QString decodedNickname;
        QString userName;
        QString hostName;
        if (bang >= 0 && at >= 0) {
            decodedNickname = DecodeNickForScreen(value.left(bang));
            userName = value.mid(bang + 1, at - bang - 1);
            hostName = value.mid(at + 1);
        } else {
            decodedNickname = DecodeNickForScreen(value);
        }
        if (userMatch && !decodedNickname.isEmpty() && !userName.isEmpty()
            && !hostName.isEmpty()) {
            QByteArray nick = sourceAcpBytes(QStringView(decodedNickname));
            const QByteArray user = sourceAcpBytes(QStringView(userName));
            const QByteArray host = sourceAcpBytes(QStringView(hostName));
            BOOL result = bIsMatch(userMatch, nick.constData(),
                                   user.constData(), host.constData());
            if (!result && bang >= 0) {
                nick = sourceAcpBytes(QStringView(
                    DecodeNick(value.left(bang))));
                result = bIsMatch(userMatch, nick.constData(),
                                  user.constData(), host.constData());
            }
            return result;
        }
        return decodedNickname.compare(filter, Qt::CaseInsensitive) == 0;
    }
    case ptMessage:
        return containsMessage(value, filter, flags & g_wMatchWord,
                               flags & g_wMatchCase);
    case ptRoomName:
        return value.compare(EncodeChan(filter), Qt::CaseInsensitive) == 0;
    case ptServerName:
        return bNetValid(filter);
    default:
        return FALSE;
    }
}

QString StrGetKeyEventParam(enumParamType type)
{
    switch (type) {
    case ptNickname:
        return QString::fromUtf8(GetMyScreenName());
    case ptServerName:
        return QString::fromUtf8(GetMyServer());
    case ptRoomName:
        return currentRoom ? currentRoom->m_strPrettyChannel : QString();
    default:
        return {};
    }
}

QString StrGetKeyActionParam(enumKeyActionParam key,
                             QString& eventServer, QString& eventIdentity,
                             QString& eventChannel, QString& eventRecipients,
                             QString& eventControlLessMessage)
{
    switch (key) {
    case kapMyActivatedRoom:
        return currentRoom ? currentRoom->m_strPrettyChannel : QString();
    case kapAll:
        return QString::fromLatin1(g_szAllLines);
    case kapEventMessage:
        return eventControlLessMessage;
    case kapEventNickname:
        return DecodeNickForScreen(StrExtractNickname(eventIdentity));
    case kapEventRecipients:
        return eventRecipients;
    case kapEventRoom:
        return DecodeChan(eventChannel);
    case kapEventServer:
        return eventServer;
    case kapRandom:
        return QString::fromLatin1(g_szRandomLine);
    case kapMe:
        return QString::fromUtf8(GetMyScreenName());
    default:
        return {};
    }
}

BOOL bExecuteAction(CCDynaRules* dynaRules, CCRule* rule,
                    CCActionContext* context)
{
    if (!context) return FALSE;
    CIrcProto* protocol = GetIrcProto();
    if (!protocol) return FALSE;
    const QString eventNickname = StrExtractNickname(context->GetCachedIdentity());
    CChatDoc* document = nullptr;
    CUserInfo* user = nullptr;
    switch (context->GetActionID()) {
    case aBan:
    case aGetLagTime:
    case aGetLocalTime:
    case aGetProfile:
    case aGetVersion:
    case aKick:
    case aLeaveRoom:
    case aMakeHost:
        document = LookupDoc(context->GetCachedChannel());
        if (!document) return TRUE;
        break;
    default:
        break;
    }
    switch (context->GetActionID()) {
    case aBan:
    case aGetLagTime:
    case aGetProfile:
    case aGetVersion:
    case aMakeHost:
        user = LookupPui(eventNickname, document);
        break;
    default:
        break;
    }

    switch (context->GetActionID()) {
    case aActivateRuleSet: {
        CCDynaRules* target = dynaRules ? dynaRules : &theApp.m_dynaRules;
        CCRuleSet* ruleSet = target->GetRuleSetFromName(
            context->GetFinalActionParam(0));
        const enumKeyActionParam key = context->GetActionKeyParam(1);
        if (ruleSet && key == kapYes) ruleSet->Activate();
        else if (ruleSet && key == kapNo) ruleSet->Desactivate();
        return key == kapYes || key == kapNo;
    }
    case aBan:
        if (document->m_puiSelf && document->m_puiSelf->IsOperator()
            && eventNickname.compare(QString::fromUtf8(GetMyNickName()),
                                     Qt::CaseInsensitive) != 0) {
            const qsizetype bang = context->GetCachedIdentity().indexOf('!');
            const qsizetype at = context->GetCachedIdentity().indexOf('@');
            if (bang >= 0 && at >= 0) {
                QString ban;
                GetBanString(context->GetCachedIdentity().mid(
                                 bang + 1, at - bang - 1),
                             context->GetCachedIdentity().mid(at + 1), ban);
                document->m_proto->ChatBanUser(ban, TRUE);
            } else if (user) {
                document->m_proto->ChatBanUser(user);
            }
        }
        return TRUE;
    case aBeep: {
        int count = context->GetFinalActionParam(0).toInt();
        while (count-- > 0) {
            QApplication::beep();
            if (count > 0) QThread::msleep(100);
        }
        return TRUE;
    }
    case aDoNotDisplay:
        if (!dynaRules) return FALSE;
        dynaRules->AddFlag(g_wDoNotDisplay);
        return TRUE;
    case aExecuteMacro: {
        CChatDoc* macroDocument = nullptr;
        CUserInfo* macroUser = PuiFromDocNickIdent(
            &macroDocument, eventNickname,
            StrExtractIdent(context->GetCachedIdentity()),
            FALSE, FALSE);
        const QString macroName = context->GetFinalActionParam(0);
        for (INT macro = 0; macro < NMACROS; ++macro) {
            if (theApp.m_macros[macro].m_bDefined
                && theApp.m_macros[macro].m_strName == macroName) {
                const QString channel = context->GetCachedChannel();
                theApp.m_macros[macro].Invoke(
                    channel.isEmpty() ? nullptr : &channel,
                    macroUser, TRUE);
                break;
            }
        }
        return TRUE;
    }
    case aGetIdentity:
        protocol->ChatGetIdentity(nullptr, eventNickname);
        return TRUE;
    case aGetLagTime:
        if (user) protocol->ChatPingUser(user);
        return TRUE;
    case aGetLocalTime: {
        const QString decoded = context->GetFinalActionParam(0);
        const QString encoded = protocol->IsIRCX() && bExtendedNickname(decoded)
            ? EncodeNick(decoded) : decoded;
        if (CUserInfo* found = LookupPui(encoded, document)) {
            protocol->ChatGetLocalTime(found);
        }
        return TRUE;
    }
    case aGetProfile:
        if (user && (context->GetEventID() == eOnJoin || user->IsComicUser())) {
            protocol->ChatGetInfo(user);
        }
        return TRUE;
    case aGetVersion:
        if (user) protocol->ChatGetVersion(user);
        return TRUE;
    case aHighlightMessage:
        if (!rule || !dynaRules) return FALSE;
        dynaRules->AddFlag(g_wHighlight);
        dynaRules->AddFlag(static_cast<WORD>(
            rule->iGetHighlightTypeIndex(context->GetFinalActionParam(0)) << 8));
        return TRUE;
    case aIgnore:
        if (eventNickname.compare(QString::fromUtf8(GetMyNickName()),
                                  Qt::CaseInsensitive) != 0) {
            protocol->DoIgnoreUser(user, TRUE, FALSE, eventNickname);
        }
        return TRUE;
    case aInvite: {
        if (eventNickname.compare(QString::fromUtf8(GetMyNickName()),
                                  Qt::CaseInsensitive) == 0) return TRUE;
        CChatDoc* room = LookupDoc(EncodeChan(context->GetFinalActionParam(0)));
        return !room || room->m_proto->ChatSendInvitation(eventNickname);
    }
    case aJoinRoom:
        g_bEnterOnCreate = FALSE;
        return bSwitchToRoom(context->GetFinalActionParam(0), QString(), QString(),
                             0L, TRUE, TRUE);
    case aKick:
        if (document->m_puiSelf && document->m_puiSelf->IsOperator()
            && eventNickname.compare(QString::fromUtf8(GetMyNickName()),
                                     Qt::CaseInsensitive) != 0) {
            document->m_proto->ChatKickUser(
                eventNickname, context->GetFinalActionParam(0));
        }
        return TRUE;
    case aLeaveRoom:
        document->OnLeave();
        return TRUE;
    case aMakeHost:
        if (user && document->m_puiSelf && document->m_puiSelf->IsOperator()
            && eventNickname.compare(QString::fromUtf8(GetMyNickName()),
                                     Qt::CaseInsensitive) != 0) {
            document->m_proto->ChatSetOperator(user, UM_HOST);
        }
        return TRUE;
    case aNotifyDialog:
        QMessageBox::information(theApp.m_pMainWnd.data(),
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
            context->GetFinalActionParam(0));
        return TRUE;
    case aReplaceMessage:
        return rule && dynaRules ? dynaRules->bReplaceMessage(rule) : FALSE;
    case aSendAction:
        return sendToChannel(context, BM_ACTION);
    case aSendMessage:
        return sendToChannel(context, BM_SAY);
    case aSendThought:
        return sendToChannel(context, BM_THINK);
    case aSendWhisper:
        return whisperToUser(protocol, context);
    case aSendWhisperInRoom:
        return whisperToUserInChannel(protocol, context);
    case aDisconnect:
        ChatServerDisconnect(TRUE, FALSE);
        return TRUE;
    case aConnect:
        if (!GetDefaultProto()
            || GetDefaultProto()->GetConnectionStatus() == CX_DISCONNECTED) {
            const BOOL prompt = g_bCXPrompt;
            g_bCXPrompt = FALSE;
            ReconnectToServer(context->GetFinalActionParam(0),
                              context->GetFinalActionParam(1));
            g_bCXPrompt = prompt;
        }
        return TRUE;

    // These effects remain at their original, not-yet-ported module borders.
    case aPlaySound:          // sounddlg.* / mcithrd.*
    case aSendFileLine:       // filesend/text-file action path
    case aSendSound:          // sounddlg.* / mcithrd.*
    case aWhisperFileLine:    // filesend/text-file action path
        return FALSE;
    default:
        return FALSE;
    }
}

BOOL bRuleDaemonQuery(CCRule* rule)
{
    CIrcProto* protocol = GetIrcProto();
    if (!protocol || !rule || !rule->GetEvent()) return FALSE;
    if (protocol->GetConnectionStatus() == CX_DISCONNECTED
        || protocol->GetConnectionStatus() == CX_CONNECTING) {
        return TRUE;
    }
    switch (rule->GetEvent()->GetID()) {
    case eOnConnect:
        return protocol->bExecuteQuery(
            qpOnConnectEvent, ctWho, dtRule, rule, QString(),
            rule->GetEventParam(0));
    case eOnDisconnect:
        return protocol->bExecuteQuery(
            qpOnDisconnectEvent, ctWho, dtRule, rule, QString(),
            rule->GetEventParam(0));
    case eOnNewRoom:
        return protocol->bExecuteQuery(
            qpOnNewRoomEvent, protocol->IsIRCX() ? ctListX : ctList,
            dtRule, rule, EncodeChan(rule->GetEventParam(0)), QString());
    default:
        return FALSE;
    }
}

QString StrAddWildcards(QString input, UCHAR op, BOOL isNickname)
{
    Q_UNUSED(isNickname);
    switch (op) {
    case g_uAny:
        return QStringLiteral("*");
    case g_uEquals:
        return input;
    case g_uContains:
        if (input.startsWith(QLatin1Char('\'')))
            return QStringLiteral("'*") + input.mid(1) + QLatin1Char('*');
        return QLatin1Char('*') + input + QLatin1Char('*');
    case g_uStartsWith:
        return input + QLatin1Char('*');
    case g_uEndsWith:
        if (input.startsWith(QLatin1Char('\'')))
            return QStringLiteral("'*") + input.mid(1);
        return QLatin1Char('*') + input;
    default:
        return input;
    }
}

BOOL bNotifDaemonQuery(CCNotif* notif)
{
    CIrcProto* protocol = GetIrcProto();
    if (!protocol || !notif) return FALSE;
    if (protocol->GetConnectionStatus() == CX_DISCONNECTED
        || protocol->GetConnectionStatus() == CX_CONNECTING) {
        return TRUE;
    }
    if (!notif->bActive()) return FALSE;

    QString decodedNickname = notif->GetParam(g_uNickname);
    TrimQuotes(decodedNickname);
    const QString encodedNickname = protocol->IsIRCX()
        && !decodedNickname.isEmpty()
        && bExtendedNickname(decodedNickname)
        ? EncodeNick(decodedNickname) : decodedNickname;

    const QString nicknameMask =
        StrAddWildcards(encodedNickname,
                        notif->GetOperator(g_uNickname), TRUE)
        + QLatin1Char('!')
        + StrAddWildcards(notif->GetParam(g_uUserName),
                          notif->GetOperator(g_uUserName), FALSE)
        + QLatin1Char('@')
        + StrAddWildcards(notif->GetParam(g_uHostName),
                          notif->GetOperator(g_uHostName), FALSE);
    return protocol->bExecuteQuery(qpOnNotification, ctWho, dtNotif,
                                   notif, QString(), nicknameMask);
}

CNotificationUsers* GetNotifBox()
{
    return g_notificationBox.data();
}

CNotificationUsers* CreateNotificationBox()
{
    if (g_notificationBox) return g_notificationBox.data();
    auto* notificationBox = new CNotificationUsers(nullptr);
    g_notificationBox = notificationBox;
    notificationBox->SetPostCreate(TRUE);
    if (!theApp.m_rectNotifs.isEmpty()) {
        notificationBox->setGeometry(
            makeRectVisibleOnScreen(theApp.m_rectNotifs));
    } else {
        const QSize size = notificationBox->size();
        notificationBox->resize(size.width() * 3 / 2,
                                size.height() * 2);
    }
    notificationBox->showNormal();
    return notificationBox;
}

void DestroyNotificationBox()
{
    if (!g_notificationBox) return;
    CNotificationUsers* notificationBox = g_notificationBox.data();
    g_notificationBox.clear();
    delete notificationBox;
}

BOOL bDisplayNotifications(CCDynaNotifs* dynaNotifs)
{
    if (!dynaNotifs) return FALSE;
    CNotificationUsers* notificationBox = GetNotifBox();
    if (dynaNotifs->GetModifiedUsersCount()) {
        const BOOL boxAlreadyExisted = notificationBox != nullptr;
        if (!notificationBox) notificationBox = CreateNotificationBox();
        if (notificationBox) {
            notificationBox->bFillList(
                dynaNotifs->GetNotifUsersArray(),
                dynaNotifs->GetModifiedUsersCount());
            if (boxAlreadyExisted) notificationBox->bSignalNewEntries();
        }
    }
    dynaNotifs->ResetModifiedUsersCount();
    return notificationBox != nullptr;
}

BOOL bSignalNewUpdate(CCDynaNotifs* dynaNotifs)
{
    if (!dynaNotifs) return FALSE;
    CNotificationUsers* notificationBox = GetNotifBox();
    if (!notificationBox) return FALSE;
    return notificationBox->bSignalNewUpdate();
}

BOOL bReportRuleFailure(CCRuleSet* ruleSet, CCRule* rule, UINT errorCode)
{
    if (!rule || !ruleSet) return FALSE;
    QString display = originalResourceString(
        QStringLiteral("IDS_ERR_RULEDISPLAY"));
    display.replace(display.indexOf(QStringLiteral("%s")), 2,
                    rule->StrGetEventDisplay());
    display.replace(display.indexOf(QStringLiteral("%s")), 2,
                    rule->StrGetActionDisplay());
    display.replace(display.indexOf(QStringLiteral("%s")), 2,
                    ruleSet->GetName());
    QString report = originalResourceString(
        errorCode == g_uErrFlooding
            ? QStringLiteral("IDS_ERR_FLOODING")
            : QStringLiteral("IDS_ERR_FLOODING"));
    report.replace(report.indexOf(QStringLiteral("%s")), 2, display);
    CIrcPrint ircPrint;
    ircPrint.SetFormat(PT_NOTINIT, QString(), RGB(255, 0, 0), 0, TRUE);
    AddToStatus(ircPrint, report);
    return TRUE;
}
