// Ported from v2.5-beta-1-modern/protsupp.cpp.
// The implementation keeps Join/353/366/member/starring semantics; no local dummy users.

#include "protsupp.h"

#include "admindlg.h"
#include "avatar.h"
#include "avatario.h"
#include "chat.h"
#include "chatdoc.h"
#include "chanprop.h"
#include "histent.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "memblst.h"
#include "notif.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "proppage.h"
#include "roomlist.h"
#include "resource.h"
#include "setupdlg.h"
#include "textview.h"
#include "userlist.h"
#include "userinfo.h"
#include "whisprbx.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <QByteArray>
#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>

QMap<QString, CUserInfo*>* g_mapNickToPtr = nullptr;
QList<CUserInfo*> g_rgpuiWhisperees;
CRoomInfo g_enterInfo;
CRoomInfo* currentRoom = nullptr;
SHORT g_nCXKeepServer = 0;
BOOL g_bCXPrompt = TRUE;
BOOL g_bEnterOnCreate = FALSE;
BOOL g_bCanViewUnrated = TRUE;
static QList<CUserInfo*> externalPuis;

namespace {
bool g_bSendComicsData = true;
QString g_lastBackdropName;

QString lowLevelUnquoteCtcp(const QString& value)
{
    const QByteArray source = value.toUtf8();
    bool quoted = false;
    for (qsizetype index = 0; index < source.size(); ++index) {
        if (source.at(index) != 0x10) continue;
        if (++index >= source.size()) return value;
        const char escaped = source.at(index);
        if (escaped != 'n' && escaped != 'r' && escaped != 0x10) return value;
        quoted = true;
    }
    if (!quoted) return value;

    QByteArray result;
    result.reserve(source.size());
    for (qsizetype index = 0; index < source.size(); ++index) {
        const char character = source.at(index);
        if (character != 0x10) {
            result.append(character);
            continue;
        }
        const char escaped = source.at(++index);
        result.append(escaped == 'n' ? '\n' : escaped == 'r' ? '\r' : 0x10);
    }
    return QString::fromUtf8(result);
}

QString sourceTail(const QString& source, SHORT byteOffset)
{
    const QByteArray bytes = source.toUtf8();
    if (byteOffset < 0 || byteOffset > bytes.size()) return {};
    return QString::fromUtf8(bytes.constData() + byteOffset,
                             bytes.size() - byteOffset);
}

QString controlFullTail(const QString& source, SHORT byteOffset,
                        CDWordArray* formatting, bool* success = nullptr)
{
    if (success) *success = true;
    const QString tail = sourceTail(source, byteOffset);
    CDWordArray* pulled = PullFormattingOffsets(formatting, byteOffset);
    if (!pulled) return tail;

    const QByteArray plain = tail.toUtf8();
    char* controlFull = SzControlFull(plain.constData(), pulled);
    FreeAndNullFormatting(&pulled);
    if (!controlFull) {
        if (success) *success = false;
        return {};
    }
    const QString result = QString::fromUtf8(controlFull);
    delete[] controlFull;
    return result;
}

QString trimOuterQuotes(QString value)
{
    if (value.size() >= 2 && value.front() == QLatin1Char('"')
        && value.back() == QLatin1Char('"')) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

QStringList commandWords(const QString& source)
{
    QStringList words;
    qsizetype position = 0;
    while (position < source.size()) {
        while (position < source.size()
               && (source.at(position).isSpace()
                   || source.at(position) == QLatin1Char(','))) {
            ++position;
        }
        if (position >= source.size()) break;

        const qsizetype start = position;
        if (source.at(position) == QLatin1Char('"')) {
            ++position;
            while (position < source.size()
                   && source.at(position) != QLatin1Char('"')
                   && source.at(position) != QLatin1Char(',')) {
                ++position;
            }
            if (position < source.size()
                && source.at(position) == QLatin1Char('"')) {
                ++position;
            }
        } else {
            while (position < source.size()
                   && !source.at(position).isSpace()
                   && source.at(position) != QLatin1Char(',')) {
                ++position;
            }
        }
        const QString word = source.mid(start, position - start);
        if (!word.isEmpty()) words.append(word);
    }
    return words;
}

void showOriginalMessage(const QString& message)
{
    QMessageBox::information(
        nullptr,
        originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
        message);
}

void showOriginalMessage(int identifier)
{
    showOriginalMessage(originalResourceString(identifier));
}
}

BOOL ReplaceToken(QString& value, const QString& token,
                  const QString& replacement)
{
    const qsizetype index = value.indexOf(token);
    if (index < 0) return FALSE;
    value = value.left(index) + replacement
        + value.mid(index + token.size());
    return TRUE;
}

void CIrcProto::TryNewNick(int messageId, const QString& showNick,
                           BOOL registerNick, QString* newNick)
{
    CNicknameDlg dialog(theApp.m_pMainWnd.data());
    dialog.m_label = originalResourceString(messageId);
    dialog.m_strNickname = showNick.isNull()
        ? QString::fromUtf8(GetMyName()) : showNick;
    dialog.m_bSpacesAllowed = IsIRCX();
    if (dialog.exec() == QDialog::Rejected) {
        if (GetConnectionStatus() == CX_CONNECTING)
            Disconnect();
        return;
    }

    if (dialog.m_strNickname.isEmpty()) {
        dialog.m_strNickname = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK"));
    }
    if (CPersonalPage* page = GetPersonalPage())
        page->SetNickname(dialog.m_strNickname);
    if (newNick) *newNick = dialog.m_strNickname;

    if (registerNick) {
        if (GetConnectionStatus() != CX_DISCONNECTED)
            ChatSetNick(dialog.m_strNickname);
        else
            SetMyName(dialog.m_strNickname);
    }
}

BOOL bReplaceMacroTokens(QString& message, BOOL in)
{
    QString replacement = originalResourceString(
        QStringLiteral("IDS_USERVARIABLE"));
    if (replacement.isEmpty()) return FALSE;
    const QString userToken = QString::fromLatin1(szUserToken);
    if (in) {
        if (!replacement.contains(userToken)) {
            while (ReplaceToken(message, userToken, replacement)) {}
        }
    } else if (!userToken.contains(replacement)) {
        while (ReplaceToken(message, replacement, userToken)) {}
    }

    replacement = originalResourceString(QStringLiteral("IDS_ROOMVARIABLE"));
    if (replacement.isEmpty()) return FALSE;
    const QString roomToken = QString::fromLatin1(szRoomToken);
    if (in) {
        if (!replacement.contains(roomToken)) {
            while (ReplaceToken(message, roomToken, replacement)) {}
        }
    } else if (!roomToken.contains(replacement)) {
        while (ReplaceToken(message, replacement, roomToken)) {}
    }
    return TRUE;
}

bool ToggleSendComicsData()
{
    g_bSendComicsData = !g_bSendComicsData;
    CChatDoc* document = GetChatDoc();
    if (g_bSendComicsData && document && document->m_proto) {
        document->m_proto->ChatAnnounceNewAvatar(
            QString::fromUtf8(GetMyCharacter()), QString::fromUtf8(MyAvatarURL()));
    }
    return g_bSendComicsData;
}

void SetSendComicsData(bool sendComicsData)
{
    g_bSendComicsData = sendComicsData;
}

bool GetSendComicsData()
{
    return g_bSendComicsData;
}

BOOL bCanViewUnrated(BOOL)
{
    // v2.5-beta-1-modern explicitly allows access when the Windows Ratings
    // DLL cannot be loaded. Qt/Linux has no equivalent provider attached, so
    // this is that source branch, not a new rating policy.
    return TRUE;
}

BOOL bPassesRatings(const QString& rating, BOOL)
{
    if (rating.isEmpty() && g_bCanViewUnrated) return TRUE;
    // Non-empty PICS requires RatingCheckUserAccess. Without an equivalent
    // provider the port must not fabricate an allow/deny decision.
    return FALSE;
}

void ListMembers(const QString& room, const QString& prettyRoom)
{
    if (CIrcProto* protocol = GetIrcProto()) {
        protocol->bExecuteQuery(qpListMembers, ctTopic, dtMax,
                                const_cast<QString*>(&prettyRoom),
                                room, QString());
    }
}

void StartRoomList()
{
    CRoomList* roomList = theApp.m_pRoomList;
    if (roomList) {
        roomList->ClearRoomList();
        roomList->m_reset->setEnabled(false);
        roomList->m_persist->MakeEmpty();
    }
}

void EndRoomList()
{
    CRoomList* roomList = theApp.m_pRoomList;
    if (roomList) {
        roomList->SortRooms(TRUE);
        roomList->AnnounceCount();
        roomList->AnnounceTime();
        roomList->m_reset->setEnabled(true);
        if (roomList->m_bResetHadFocus) roomList->m_reset->setFocus();
    }
    theApp.m_bInSearch = FALSE;
}

void EndUserList()
{
    CUserList* userList = theApp.m_pUserList;
    if (userList) {
        userList->Sort(TRUE);
        userList->AnnounceCount();
        userList->m_reset->setEnabled(true);
        if (userList->m_bResetHadFocus) userList->m_reset->setFocus();
    }
    theApp.m_bInSearch = FALSE;
}

void AddToRoomList(CRoom* room, BOOL addIt)
{
    CRoomList* roomList = theApp.m_pRoomList;
    if (roomList && addIt && room) {
        room->CalculateSortByte();
        const int index = roomList->m_persist->AddRoom(room);
        roomList->AddToRoomList(index);
        roomList->AnnounceCount();
    } else {
        delete room;
    }
}

void AddToUserList(CUser* user)
{
    CUserList* userList = theApp.m_pUserList;
    if (!userList || !user) {
        if (user) user->Release();
        return;
    }
    const QString filter = userList->m_persist->m_strUserFilter;
    const int searchType = userList->m_persist->m_searchType;
    const bool matches = searchType == USERSEARCH_ALL
        || (searchType == USERSEARCH_NICK
            && user->GetPrettyNick().contains(filter, Qt::CaseInsensitive))
        || (searchType == USERSEARCH_ID
            && user->m_strIdentity.contains(filter, Qt::CaseInsensitive))
        || searchType == USERSEARCH_ROOM;
    if (!matches) {
        user->Release();
        return;
    }
    const int index = userList->m_persist->AddUser(user);
    userList->AddToUserList(index);
    userList->AnnounceCount();
}

CUser* CreateUserFromWhoReply(IRCPARSE* parse)
{
    if (!parse || parse->args.size() < 8) return nullptr;
    auto* user = new CUser;
    user->m_strNickname = parse->args.at(6);
    user->m_strIdentity = parse->args.at(3) + QLatin1Char('@')
        + parse->args.at(4);
    user->m_strRoom = parse->args.at(2);
    user->m_strPrettyRoom = DecodeChan(parse->args.at(2));
    if (user->m_strNickname.startsWith(QLatin1Char('\'')))
        user->SetPrettyNick(DecodeNickForScreen(user->m_strNickname));
    if (parse->bHasLastString) {
        const qsizetype firstSpace = parse->lastString.indexOf(QLatin1Char(' '));
        if (firstSpace >= 0) {
            qsizetype start = firstSpace;
            while (start < parse->lastString.size()
                   && parse->lastString.at(start).isSpace()) {
                ++start;
            }
            if (start < parse->lastString.size())
                user->m_strFullName = parse->lastString.mid(start);
        }
    }
    return user;
}

CRoomInfo* GetDefaultProto()
{
    if (CIrcProto* protocol = GetIrcProto()) return protocol;
    if (theApp.m_pDoc && theApp.m_pDoc->m_proto) return theApp.m_pDoc->m_proto;
    CChatDoc* document = GetChatDoc();
    return document ? document->m_proto : currentRoom;
}

bool bCanDance()
{
    static std::time_t lastDance = 0;
    const std::time_t currentTime = std::time(nullptr);
    if (std::llabs(static_cast<long long>(lastDance - currentTime)) > 2) {
        lastDance = currentTime;
        return true;
    }
    return false;
}

bool CRoomInfo::bSendWhispers(const QString& annotations, const QString& message,
                              QString* nmText, unsigned short modes,
                              bool* justToMe)
{
    if (justToMe) {
        *justToMe = true;
    }
    for (CUserInfo* pui : g_rgpuiWhisperees) {
        if (!pui) {
            continue;
        }
        if (justToMe
            && pui->GetName().compare(QString::fromUtf8(GetMyNickName()),
                                      Qt::CaseInsensitive) != 0) {
            *justToMe = false;
        }
        if (!bChatSendPrivMesg(pui->GetName(), annotations, message, nmText,
                               false, modes)) {
            return false;
        }
    }
    return true;
}

void CRoomInfo::ChatAnnounceNewAvatar(const QString& avatarName, const QString& url,
                                      const QString& addressee, bool force)
{
    if (GetConnectionStatus() != CX_INCHANNEL) {
        return;
    }
    if (!(IsIRCX() || g_bSendComicsData || force)) {
        return;
    }
    const QString effectiveName = avatarName.isEmpty()
        ? QStringLiteral("NONE") : avatarName;
    QString annotations = QStringLiteral("# Appears as ") + effectiveName;
    if (!url.isEmpty()) {
        annotations += QLatin1Char('.');
        annotations += url;
    }
    if (addressee.isEmpty()) {
        bChatSendToChannel(annotations, QString());
    } else {
        bChatSendPrivMesg(addressee, annotations, QString());
    }
}

void CRoomInfo::ChatGetInfo(CUserInfo* pui)
{
    if (!pui) return;
    pui->IncrementRequestInfo(RF_PROFILE);
    bChatSendPrivMesg(pui->GetName(), QStringLiteral("# GetInfo"), QString());
}

void CRoomInfo::ChatGetAvatarInfo(CUserInfo* pui, bool interactive)
{
    if (!pui) return;
    pui->SetFlag(interactive ? UF_INTERACTIVEDOWNLOAD : UF_AUTODOWNLOAD, true);
    bChatSendPrivMesg(pui->GetName(), QStringLiteral("# GetCharInfo"), QString());
}

void CRoomInfo::ChatSyncBackDrop(CChatDoc* document, const QString& backdrop,
                                 const QString& url)
{
    if (backdrop.isEmpty() || GetConnectionStatus() != CX_INCHANNEL) return;
    bChatSendToChannel(QStringLiteral("# BDrop2: %1,%2").arg(backdrop, url),
                       QString());
    QString oldName = backdrop;
    const qsizetype dot = oldName.indexOf(QLatin1Char('.'));
    if (dot >= 0) oldName.truncate(dot);
    bChatSendToChannel(QStringLiteral("# BDrop:  %1").arg(oldName), QString());
    const QString propertyValue = backdrop + QLatin1Char(',') + url;
    ChangeProperty(document ? document->m_puiSelf : nullptr,
                   QStringLiteral("bk"), &propertyValue);
}

void CRoomInfo::OnPropertyChange(const QString& property,
                                 const QString* value)
{
    if (property.compare(QStringLiteral("bk"), Qt::CaseInsensitive) != 0
        || !value) {
        return;
    }

    qsizetype position = 0;
    while (position < value->size()
           && (value->at(position).isSpace()
               || value->at(position) == QLatin1Char(','))) {
        ++position;
    }
    const qsizetype nameStart = position;
    while (position < value->size()
           && !value->at(position).isSpace()
           && value->at(position) != QLatin1Char(',')) {
        ++position;
    }
    const QString backdropName = value->mid(nameStart, position - nameStart);
    if (backdropName.isEmpty()) return;

    while (position < value->size()
           && (value->at(position).isSpace()
               || value->at(position) == QLatin1Char(','))) {
        ++position;
    }
    const qsizetype urlStart = position;
    while (position < value->size()
           && !value->at(position).isSpace()
           && value->at(position) != QLatin1Char(',')
           && value->at(position) != QLatin1Char(')')) {
        ++position;
    }
    const QString backdropUrl = value->mid(urlStart, position - urlStart);
    AddAndExecute(new ChangeBackDropEntry(backdropName, backdropUrl), m_doc);
}

void DoUserAway(CChatDoc* document, CUserInfo* pui, bool away)
{
    if (!pui) return;
    pui->SetFlag(UF_AWAY, away);
    if (document && document->m_memberList) {
        document->m_memberList->AddUser(pui);
        document->m_memberList->Sort();
    }
}

void ShowAway(CUserInfo* pui, QString awayMessage, CChatDoc* document)
{
    if (!pui) return;
    const qsizetype end = awayMessage.indexOf(QLatin1Char('\001'));
    if (end >= 0) awayMessage.truncate(end);
    const bool away = !awayMessage.isEmpty();
    QString display = originalResourceString(
        away ? QStringLiteral("IDS_AWAYREPORT")
             : QStringLiteral("IDS_BACKREPORT"));
    display.replace(QStringLiteral("%1"), pui->GetScreenName());
    if (away) display.replace(QStringLiteral("%2"), awayMessage);
    DoUserAway(document, pui, away);
    AddAndExecute(new GetInfoEntry(pui, display), document);
}

void ShowIdentity(const QString& nick, const QString& user,
                  const QString& host)
{
    CUserInfo* pui = LookupPui(nick);
    if (!pui) return;
    QString message = originalResourceString(
        QStringLiteral("IDS_REPORT_IDENT2"));
    message.replace(QStringLiteral("%1"), pui->GetScreenName());
    message.replace(QStringLiteral("%2"), user);
    message.replace(QStringLiteral("%3"), host);
    AddAndExecute(new GetInfoEntry(pui, message), GetChatDoc());
}

void CRoomInfo::ChatSetAway(bool away, const QString& message,
                            CUserInfo* pui, bool)
{
    CChatDoc* document = m_doc;
    QString wire = QString::fromLatin1("\001AWAY");
    if (away) wire += QLatin1Char(' ') + message;
    wire += QLatin1Char('\001');
    if (pui) {
        bChatSendPrivMesg(pui->GetName(), QString(), wire, nullptr, false,
                          BM_AWAY);
        return;
    }
    if (document && document->m_puiSelf) {
        ShowAway(document->m_puiSelf, wire.mid(6), document);
    }
    bChatSendToChannel(QString(), wire, nullptr, BM_AWAY);
}

void CRoomInfo::DoIgnoreUser(CUserInfo* pui, bool ignore, bool autoIgnore,
                             const QString& nickname)
{
    if (!pui && nickname.isEmpty()) return;
    const QString nick = pui ? pui->GetName() : nickname;
    const QString identity = pui ? pui->GetFullName() : QString();
    if (!identity.isEmpty()) IgnoreUser(nick, identity, ignore, autoIgnore);
}

bool ExpandVariables(QString& message, CChatDoc* document, CUserInfo* pui,
                     bool invokedByRule)
{
    const QString userVariable = originalResourceString(
        QStringLiteral("IDS_USERVARIABLE"));
    if (userVariable.isNull()) return false;
    if (!document) document = GetChatDoc();
    if (!document) return false;

    if (message.contains(userVariable)) {
        if (!pui) {
            if (invokedByRule) return false;
            pui = document->GetSingleSelectedMember();
        }
        if (!pui) return false;
        const QString screenName = pui->GetScreenName();
        if (!screenName.contains(userVariable)) message.replace(userVariable, screenName);
    }

    const QString roomVariable = originalResourceString(
        QStringLiteral("IDS_ROOMVARIABLE"));
    if (roomVariable.isNull() || !document->m_proto) return false;
    if (!document->m_proto->m_strPrettyChannel.contains(roomVariable)) {
        message.replace(roomVariable, document->m_proto->m_strPrettyChannel);
    }
    return true;
}

void AutoGreet(const QString& nickname)
{
    if (!theApp.m_iGreetingType || !g_puiSelf || !g_puiSelf->IsOperator()) {
        return;
    }
    CUserInfo* pui = LookupPui(nickname);
    if (!pui) return;
    QString controlFull = theApp.m_strGreetingMesg;
    if (!ExpandVariables(controlFull, GetChatDoc(), pui)) return;

    g_puiSelf->m_udi.m_talkTos.clear();
    g_puiSelf->m_udi.m_talkTos.append(pui);
    QByteArray controlBytes = controlFull.toUtf8();
    CDWordArray formatting;
    const QString controlLess = QString::fromUtf8(
        SzControlLess(controlBytes.data(), &formatting));
    switch (theApp.m_iGreetingType) {
    case AGT_SAY:
        bChatSendText(controlLess, BM_SAY, true, &formatting);
        break;
    case AGT_WHISPER:
        g_rgpuiWhisperees.clear();
        g_rgpuiWhisperees.append(pui);
        bChatSendText(controlLess, BM_WHISPER, true, &formatting);
        break;
    default:
        break;
    }
}

void CRoomInfo::ReplyVersion(CUserInfo* pui)
{
    if (!pui) return;
    QString version;
    GetVersionString(version);
    CChatDoc* document = GetChatDoc();
    const QString mode = originalResourceString(
        document && document->m_bComicView
            ? QStringLiteral("IDS_COMICS_MODE")
            : QStringLiteral("IDS_TEXT_MODE"));
    const QString wire = QStringLiteral("\001VERSION %1 %2\001")
                             .arg(version, mode);
    bChatSendPrivMesg(pui->GetName(), QString(), wire, nullptr, true);
}

void CRoomInfo::ReplyPing(CUserInfo* pui, const QString& message)
{
    if (!pui) return;
    bChatSendPrivMesg(pui->GetName(), QString(),
                      QStringLiteral("\001PING %1\001").arg(message),
                      nullptr, true);
}

void CRoomInfo::ReplyTime(CUserInfo* pui)
{
    if (!pui) return;
    const QLocale locale = QLocale::system();
    const QDateTime now = QDateTime::currentDateTime();
    const QString value = locale.toString(now.date(), QLocale::ShortFormat)
        + QStringLiteral(", ")
        + locale.toString(now.time(), QLocale::ShortFormat);
    bChatSendPrivMesg(pui->GetName(), QString(),
                      QStringLiteral("\001TIME %1\001").arg(value),
                      nullptr, true);
}

void CRoomInfo::ReplyEmail(CUserInfo* pui)
{
    if (!pui) return;
    bChatSendPrivMesg(pui->GetName(), QString(),
                      QStringLiteral("\001EMAIL %1\001")
                          .arg(QString::fromUtf8(GetMyEmail())),
                      nullptr, true);
}

void CRoomInfo::ReplyHomePage(CUserInfo* pui)
{
    if (!pui) return;
    bChatSendPrivMesg(pui->GetName(), QString(),
                      QStringLiteral("\001URL %1\001")
                          .arg(QString::fromUtf8(GetMyHomePage())),
                      nullptr, true);
}

void CRoomInfo::ChatGetVersion(CUserInfo* pui)
{
    if (!pui || pui->IsDeparted()) return;
    pui->IncrementRequestInfo(RF_VERSION);
    bChatSendPrivMesg(pui->GetName(), QString(),
                      QStringLiteral("\001VERSION\001"));
}

void CRoomInfo::ChatPingUser(CUserInfo* pui)
{
    if (!pui || pui->IsDeparted()) return;
    pui->SetFlag(UF_REQUESTPING, true);
    bChatSendPrivMesg(
        pui->GetName(), QString(),
        QStringLiteral("\001PING %1\001")
            .arg(static_cast<qlonglong>(std::time(nullptr))));
}

void CRoomInfo::ChatGetLocalTime(CUserInfo* pui)
{
    if (!pui || pui->IsDeparted()) return;
    pui->IncrementRequestInfo(RF_TIME);
    bChatSendPrivMesg(pui->GetName(), QString(),
                      QStringLiteral("\001TIME\001"));
}

void CRoomInfo::ChatGetEmail(CUserInfo* pui)
{
    if (!pui || pui->IsDeparted()) return;
    pui->IncrementRequestInfo(RF_EMAIL);
    bChatSendPrivMesg(pui->GetName(), QString(),
                      QStringLiteral("\001EMAIL\001"));
}

void CRoomInfo::ChatGetHomePage(CUserInfo* pui)
{
    if (!pui || pui->IsDeparted()) return;
    pui->IncrementRequestInfo(RF_HOMEPAGE);
    bChatSendPrivMesg(pui->GetName(), QString(),
                      QStringLiteral("\001URL\001"));
}

void CRoomInfo::DoChannelDialog()
{
    CChannelProp properties;
    properties.m_bIsIRCX = GetDefaultProto()
        && GetDefaultProto()->IsIRCX();
    properties.m_rtfTopic.DefineDefaultCharFormat();
    properties.m_rtfTopic.m_strText = m_strTopic;
    properties.m_rtfTopic.m_prgdwFormatting =
        CopyFormatting(m_prgdwTopicFormatting);

    if (m_dwModes & CM_USERLIMIT) {
        properties.m_uMaxParticipants = static_cast<UINT>(m_dwMaxUsers);
        properties.m_bSetMax = m_dwMaxUsers > 0;
    }
    if (m_dwModes & CM_CHANNELKEY) {
        properties.m_strPassword = m_strPassword;
        properties.m_bSetPassword = !m_strPassword.isEmpty();
    }
    properties.m_bSecret = (m_dwModes & CM_HIDDEN) != 0;
    properties.m_bPrivate = (m_dwModes & CM_PRIVATE) != 0;
    properties.m_bInviteOnly = (m_dwModes & CM_INVITEONLY) != 0;
    properties.m_bModerated = (m_dwModes & CM_MODERATED) != 0;
    properties.m_bTopicAnyone = (m_dwModes & CM_TOPICHOST) == 0;

    if (properties.exec() == QDialog::Accepted) {
        if (properties.m_rtfTopic.m_strText != m_strTopic
            || !bFormattingsEqual(properties.m_rtfTopic.m_prgdwFormatting,
                                  m_prgdwTopicFormatting)) {
            const QByteArray plain = properties.m_rtfTopic.m_strText.toUtf8();
            char* controlFull = properties.m_rtfTopic.m_prgdwFormatting
                ? SzControlFull(plain.constData(),
                                properties.m_rtfTopic.m_prgdwFormatting)
                : nullptr;
            ChatSetTopic(controlFull
                ? QString::fromUtf8(controlFull)
                : properties.m_rtfTopic.m_strText);
            delete[] controlFull;
        }

        DWORD newMode = CM_NOEXTERN;
        DWORD newMaxUsers = 0;
        if (properties.m_bSetMax) {
            newMaxUsers = properties.m_uMaxParticipants;
            if (newMaxUsers > 0) newMode |= CM_USERLIMIT;
        }
        if (properties.m_bSecret) newMode |= CM_HIDDEN;
        if (properties.m_bPrivate) newMode |= CM_PRIVATE;
        if (properties.m_bInviteOnly) newMode |= CM_INVITEONLY;
        if (properties.m_bModerated) newMode |= CM_MODERATED;
        if (!properties.m_bTopicAnyone) newMode |= CM_TOPICHOST;
        if (properties.m_bSetPassword) newMode |= CM_CHANNELKEY;
        ChatSetMode(newMode, newMaxUsers, properties.m_strPassword);
    }

    CChatDoc* document = GetChatDoc();
    if (document) document->SetFocusToSayWnd();
}

void CRoomInfo::ChatSetOperator(CUserInfo* pui, int mode)
{
    if (!pui) return;
    if (mode == UM_HOST) {
        SendMessageText(QStringLiteral("MODE %1 +o %2\r\n")
                            .arg(m_strChannel, pui->GetName()));
        return;
    }

    if (pui->IsOperator()) {
        SendMessageText(QStringLiteral("MODE %1 -o %2\r\n")
                            .arg(m_strChannel, pui->GetName()));
    }
    if (mode == UM_SPEAKER && currentRoom
        && (currentRoom->m_dwModes & CM_MODERATED)) {
        SendMessageText(QStringLiteral("MODE %1 +v %2\r\n")
                            .arg(m_strChannel, pui->GetName()));
    }
    if (mode == UM_SPECTATOR) {
        SendMessageText(QStringLiteral("MODE %1 -v %2\r\n")
                            .arg(m_strChannel, pui->GetName()));
    }
}

void CRoomInfo::DoKickDlg(const QString& nick, const QString& banPattern)
{
    CKickDialog dialog(theApp.m_pMainWnd.data());
    dialog.m_strKick = originalResourceString(
        QStringLiteral("IDS_KICKREASON"));
    dialog.m_strKick.replace(QStringLiteral("%1"), DecodeNick(nick));
    dialog.m_strBanPattern = banPattern;
    if (dialog.exec() == IDOK) {
        if (dialog.m_bBanToo) {
            QString pattern = dialog.m_strBanPattern;
            while (!pattern.isEmpty() && pattern.front().isSpace())
                pattern.remove(0, 1);
            if (!pattern.isEmpty()) ChatBanUser(pattern, TRUE);
        }
        ChatKickUser(nick, dialog.m_reason.trimmed());
    }
    if (CChatDoc* document = GetChatDoc()) document->SetFocusToSayWnd();
}

void DoBanDlg(const QString& encodedChannelName, const QString& ban,
              QStringList& banArray)
{
    CBanDlg dialog(theApp.m_pMainWnd.data());
    dialog.m_strBanPattern = ban;
    dialog.m_strMesg = originalResourceString(QStringLiteral("IDS_BANMESG"));
    BOOL mic = FALSE;
    if (currentRoom
        && currentRoom->m_strChannel == encodedChannelName) {
        mic = (currentRoom->m_dwModes & CM_MIC) != 0;
    } else {
        dialog.m_szEncodedChannel = encodedChannelName;
    }
    dialog.m_strMesg.replace(QStringLiteral("%1"),
                             DecodeChan(encodedChannelName, mic));
    dialog.m_banArray = &banArray;
    dialog.exec();
    banArray.clear();
    if (CChatDoc* document = GetChatDoc()) document->SetFocusToSayWnd();
}

bool bCanInvite()
{
    CChatDoc* document = GetChatDoc();
    return document
        && document->GetConnectionStatus() == CX_INCHANNEL
        && document->m_puiSelf
        && (document->m_puiSelf->IsOperator()
            || !(document->m_proto->m_dwModes & CM_INVITEONLY));
}

bool bDoInvite(const QString& invitee, void* protocol, unsigned long)
{
    auto* room = static_cast<CRoomInfo*>(protocol);
    if (!room) return false;
    QString encodedNick = invitee;
    if (room->IsIRCX() && bExtendedNickname(invitee))
        encodedNick = EncodeNick(invitee);
    return room->ChatSendInvitation(encodedNick);
}

void CRoomInfo::ChatInvite()
{
    if (GetConnectionStatus() != CX_INCHANNEL || !g_puiSelf) return;
    CInviteDlg dialog(theApp.m_pMainWnd.data());
    if (dialog.exec() == IDOK) {
        bForEachWord(dialog.m_strInvitees, bDoInvite, this, 0L,
                     QStringLiteral(" ,\r\n"));
    }
    if (CChatDoc* document = GetChatDoc()) document->SetFocusToSayWnd();
}

void OnInvite(const QString& sender, const QString& fullName,
              const QString& room)
{
    static BOOL inInvite = FALSE;
    if (!theApp.m_bAllowInvites || !bCanViewUnrated()) return;
    if (IsIgnored(fullName) || inInvite) return;
    inInvite = TRUE;

    QString server = QString::fromUtf8(GetMyServer());
    QString identity = sender + QLatin1Char('!') + fullName;
    QString channel = room;
    QString eventMessage;
    theApp.m_dynaRules.bMatchAndApplyRules(
        eOnInvitation, nullptr, nullptr, server, identity,
        channel, eventMessage);

    CInvitationDlg dialog(theApp.m_pMainWnd.data());
    dialog.m_strMessage = originalResourceString(
        QStringLiteral("ID_JOIN_OFFER"));
    dialog.m_strMessage.replace(QStringLiteral("%1"), DecodeNick(sender));
    dialog.m_strMessage.replace(QStringLiteral("%2"), DecodeChan(room));
    const int result = dialog.exec();
    if (result == IDYES) {
        g_bEnterOnCreate = FALSE;
        bSwitchToRoom(room);
    }
    if (result != IDCANCEL && dialog.m_bIgnore)
        IgnoreUser(sender, fullName, TRUE, FALSE);
    inInvite = FALSE;
}

void AcknowledgeInvite(const QString& nickname, const QString& room)
{
    QString confirmation = originalResourceString(
        QStringLiteral("IDS_INVITE_CONF"));
    confirmation.replace(QStringLiteral("%1"), nickname);
    confirmation.replace(QStringLiteral("%2"), room);
    QMessageBox::information(
        theApp.m_pMainWnd.data(),
        originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
        confirmation);
}

void ShowVersion(CUserInfo* pui, QString message)
{
    if (!pui || !pui->IsRequestInfo(RF_VERSION)) return;
    QString display = originalResourceString(QStringLiteral("IDS_VERSION_PREFIX"));
    const qsizetype end = message.indexOf(QLatin1Char('\001'));
    if (end >= 0) message.truncate(end);
    while (!message.isEmpty() && message.front().isSpace()) message.remove(0, 1);
    display.replace(QStringLiteral("%1"), pui->GetScreenName());
    display.replace(QStringLiteral("%2"), message);
    AddAndExecute(new GetInfoEntry(pui, display));
    pui->DecrementRequestInfo(RF_VERSION);
}

void ShowPing(CUserInfo* pui, const QString& message)
{
    if (!pui || !pui->CheckFlag(UF_REQUESTPING)) return;
    const QRegularExpressionMatch match = QRegularExpression(
        QStringLiteral("^\\s*([+-]?\\d+)")).match(message);
    bool valid = false;
    const qlonglong oldTime = match.hasMatch()
        ? match.captured(1).toLongLong(&valid) : 0;
    if (!valid) return;
    QString display = originalResourceString(QStringLiteral("IDS_PING_MESSAGE"));
    display.replace(QStringLiteral("%1"), pui->GetScreenName());
    display.replace(QStringLiteral("%2"), QString::number(
        static_cast<qlonglong>(std::time(nullptr)) - oldTime));
    AddAndExecute(new GetInfoEntry(pui, display));
    pui->SetFlag(UF_REQUESTPING, false);
}

void ShowTime(CUserInfo* pui, QString message)
{
    if (!pui || !pui->IsRequestInfo(RF_TIME)) return;
    const qsizetype end = message.indexOf(QLatin1Char('\001'));
    if (end >= 0) message.truncate(end);
    QString display = originalResourceString(
        QStringLiteral("IDS_LOCALTIME_MESSAGE"));
    display.replace(QStringLiteral("%1"), pui->GetScreenName());
    display.replace(QStringLiteral("%2"), message);
    AddAndExecute(new GetInfoEntry(pui, display));
    pui->DecrementRequestInfo(RF_TIME);
}

void ShowEmail(CUserInfo* pui, QString address)
{
    if (!pui || !pui->IsRequestInfo(RF_EMAIL)) return;
    const qsizetype end = address.indexOf(QLatin1Char('\001'));
    if (end >= 0) address.truncate(end);
    while (!address.isEmpty() && address.front().isSpace()) address.remove(0, 1);
    if (address.isEmpty()) {
        QMessageBox::information(nullptr,
            originalResourceString(QStringLiteral("ID_APP_TITLE")),
            originalResourceString(QStringLiteral("IDS_NO_EMAIL_ADDRESS")));
    } else {
        QDesktopServices::openUrl(QUrl(QStringLiteral("mailto:") + address));
    }
    pui->DecrementRequestInfo(RF_EMAIL);
}

void ShowHomePage(CUserInfo* pui, QString url)
{
    if (!pui || !pui->IsRequestInfo(RF_HOMEPAGE)) return;
    const qsizetype end = url.indexOf(QLatin1Char('\001'));
    if (end >= 0) url.truncate(end);
    while (!url.isEmpty() && url.front().isSpace()) url.remove(0, 1);
    if (url.isEmpty()) {
        QMessageBox::information(nullptr,
            originalResourceString(QStringLiteral("ID_APP_TITLE")),
            originalResourceString(QStringLiteral("IDS_NO_HOMEPAGE")));
    } else {
        if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)) {
            url.prepend(QStringLiteral("http://"));
        }
        QDesktopServices::openUrl(QUrl(url));
    }
    pui->DecrementRequestInfo(RF_HOMEPAGE);
}

QString PrepareTextAction(CUserInfo* pui, const QString& message,
                          unsigned short& modes)
{
    if (!pui) return QString();
    QString result = pui->GetScreenName() + message.mid(g_nActionLen);
    const qsizetype end = result.indexOf(QLatin1Char('\001'));
    if (end >= 0) result.truncate(end);
    modes &= static_cast<unsigned short>(~BM_SAY);
    modes |= BM_ACTION;
    return result;
}

QString PrepareComicsAction(CUserInfo* pui, const QString& message)
{
    return pui ? pui->GetScreenName() + QLatin1Char(' ') + message
               : QString();
}

namespace {
QString ctcpUnquote(const QString& source)
{
    bool quoted = false;
    for (qsizetype index = 0; index < source.size(); ++index) {
        if (source.at(index) != QLatin1Char('\\')) continue;
        if (++index >= source.size()) return source;
        const QChar escaped = source.at(index);
        if (escaped != QLatin1Char('1') && escaped != QLatin1Char('@')
            && escaped != QLatin1Char('n') && escaped != QLatin1Char('r')
            && escaped != QLatin1Char('\\')) return source;
        quoted = true;
    }
    if (!quoted) return source;

    QString result;
    result.reserve(source.size());
    for (qsizetype index = 0; index < source.size(); ++index) {
        if (source.at(index) != QLatin1Char('\\')) {
            result += source.at(index);
            continue;
        }
        const QChar escaped = source.at(++index);
        if (escaped == QLatin1Char('1')) result += QLatin1Char('\001');
        else if (escaped == QLatin1Char('@')) result += QLatin1Char(' ');
        else if (escaped == QLatin1Char('n')) result += QLatin1Char('\n');
        else if (escaped == QLatin1Char('r')) result += QLatin1Char('\r');
        else result += QLatin1Char('\\');
    }
    return result;
}
}

QString PrepareSound(CUserInfo* pui, const QString& message,
                     unsigned short& modes)
{
    if (!pui) return QString();
    qsizetype start = g_nSoundLen;
    while (start < message.size() && message.at(start).isSpace()) ++start;
    if (start >= message.size()) return QString();

    qsizetype end = -1;
    const bool quotedName = message.at(start) == QLatin1Char('"');
    if (quotedName) {
        ++start;
        end = message.indexOf(QLatin1Char('"'), start);
        if (end < 0) return QString();
    } else {
        end = message.indexOf(QLatin1Char(' '), start + 1);
        if (end < 0) end = message.indexOf(QLatin1Char('\001'), start);
        if (end < 0) end = message.size();
    }

    QString file = message.mid(start, end - start);
    if (!quotedName) file = ctcpUnquote(file);
    qsizetype tailStart = quotedName ? end + 1 : end;
    QString tail = message.mid(tailStart);
    const qsizetype ctcpEnd = tail.indexOf(QLatin1Char('\001'));
    if (ctcpEnd >= 0) tail.truncate(ctcpEnd);

    QByteArray tailBytes = tail.toUtf8();
    char reset[MAX_FORMATTINGPERBYTE]{};
    QString result = pui->GetScreenName() + tail;
    if (nResettingSequence(tailBytes.constData(), reset)) {
        result += QString::fromLatin1(reset);
    }
    result += QStringLiteral(" (") + file + QLatin1Char(')');
    // Playback remains assigned to mcithrd/sounddlg; no substitute sound or
    // search path is introduced here.
    modes &= static_cast<unsigned short>(~BM_SAY);
    modes |= BM_ACTION;
    return result;
}

void IdentifyWhispers(CChatDoc* document, CUserInfo* pui,
                      unsigned char msgType, unsigned short& modes,
                      const QList<CUserInfo*>* talkTos)
{
    if (!pui || !(msgType & MT_PRIVATEMSG)) return;
    modes &= static_cast<unsigned short>(~BM_SAY);
    modes |= BM_WHISPER;
    pui->m_udi.m_talkTos.clear();
    if (talkTos) {
        pui->m_udi.m_talkTos = *talkTos;
    } else if (document && document->m_puiSelf) {
        pui->m_udi.m_talkTos.append(document->m_puiSelf);
    } else if (CUserInfo* self = ExternalPui(
                   QString::fromUtf8(GetMyNickName()), QString(), true)) {
        pui->m_udi.m_talkTos.append(self);
    }
}

bool AcceptWhispers()
{
    return theApp.m_bAcceptWhispers;
}

unsigned char IndexToByte(unsigned char byteIn)
{
    return static_cast<unsigned char>(byteIn + '0');
}

unsigned char ByteToIndex(unsigned char byteIn)
{
    return static_cast<unsigned char>(byteIn - '0');
}

unsigned short SM2BM(unsigned char byteMode)
{
    switch (byteMode) {
    case SM_WHISPER:
        return BM_WHISPER;
    case SM_THINK:
        return BM_THINK;
    case SM_ACTION:
        return BM_ACTION;
    default:
        return BM_SAY;
    }
}

unsigned char BM2SM(unsigned short modes)
{
    if ((modes & BM_ACTION) || (modes & BM_SOUND)) {
        return SM_ACTION;
    }
    if (modes & BM_WHISPER) {
        return SM_WHISPER;
    }
    if (modes & BM_THINK) {
        return SM_THINK;
    }
    return SM_SAY;
}

void GetTalkTos(CChatDoc* doc, QList<CUserInfo*>* talkTos,
                const QString& names)
{
    if (!talkTos) return;
    qsizetype position = 0;
    while (position < names.size()) {
        while (position < names.size()
               && (names.at(position).isSpace()
                   || names.at(position) == QLatin1Char(','))) {
            ++position;
        }
        if (position >= names.size()) return;
        const qsizetype start = position;
        while (position < names.size()
               && !names.at(position).isSpace()
               && names.at(position) != QLatin1Char(',')) {
            ++position;
        }
        if (CUserInfo* pui = LookupPui(names.mid(start, position - start), doc)) {
            talkTos->append(pui);
        }
    }
}

namespace {
bool g_bInEnumeration = false;

bool channelPrefix(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    const QChar ch = value.front();
    return ch == QLatin1Char('#') || ch == QLatin1Char('%') || ch == QLatin1Char('&');
}

void parseUDITail(CUserInfo* pui, const QString& data, int start, bool privateMessage)
{
    if (!pui) {
        return;
    }

    int pos = start;
    if (pos < data.size() && data[pos] == QLatin1Char(CGESTUREPREFIX)) {
        ++pos;
        if (pos < data.size()) pui->m_udi.m_chGest = static_cast<signed char>(ByteToIndex(static_cast<unsigned char>(data[pos++].toLatin1())));
        if (pos < data.size()) pui->m_udi.m_chGestE = static_cast<signed char>(ByteToIndex(static_cast<unsigned char>(data[pos++].toLatin1())));
        if (pos < data.size()) pui->m_udi.m_chGestI = static_cast<signed char>(ByteToIndex(static_cast<unsigned char>(data[pos++].toLatin1())));
    }

    if (pos < data.size() && data[pos] == QLatin1Char(CEXPRESSIONPREFIX)) {
        ++pos;
        if (pos < data.size()) pui->m_udi.m_chExpr = static_cast<signed char>(ByteToIndex(static_cast<unsigned char>(data[pos++].toLatin1())));
        if (pos < data.size()) pui->m_udi.m_chExprE = static_cast<signed char>(ByteToIndex(static_cast<unsigned char>(data[pos++].toLatin1())));
        if (pos < data.size()) pui->m_udi.m_chExprI = static_cast<signed char>(ByteToIndex(static_cast<unsigned char>(data[pos++].toLatin1())));
    }

    if (pos < data.size() && data[pos] == QLatin1Char(CREQUESTEDPREFIX)) {
        ++pos;
        pui->m_udi.m_bbReq = 1;
    }

    if (pos < data.size() && data[pos] == QLatin1Char(CMODEPREFIX)) {
        ++pos;
        if (pos < data.size()) {
            pui->m_udi.m_uModes = SM2BM(ByteToIndex(static_cast<unsigned char>(data[pos++].toLatin1())));
        }
        if (privateMessage) {
            pui->m_udi.m_uModes &= static_cast<unsigned short>(~(BM_SAY | BM_THINK));
            pui->m_udi.m_uModes |= BM_WHISPER;
        }
    }

    if (pos < data.size() && data[pos] == QLatin1Char(CTALKTOPREFIX)) {
        pui->m_udi.m_talkTos.clear();
        const QStringList names = data.mid(pos + 1).split(QLatin1Char(','),
                                                            Qt::SkipEmptyParts);
        for (const QString& name : names) {
            if (CUserInfo* talkTo = LookupPui(name.trimmed())) {
                pui->m_udi.m_talkTos.append(talkTo);
            }
        }
    }

    if (pui->m_udi.m_chGestI != -1 && pui->m_udi.m_chExprI != -1) {
        pui->m_udi.m_bbCooked = 1;
    }
}

bool ProcessComment(CChatDoc* document, CUserInfo* pui, const QString& message,
                    unsigned char msgType)
{
    if (!pui || !message.startsWith(QLatin1Char('#'))) {
        return false;
    }

    const QString unquoted = lowLevelUnquoteCtcp(message);
    CRoomInfo* proto = document ? document->m_proto : currentRoom;

    if (unquoted.startsWith(QStringLiteral("# Appears as "))) {
        QString value = unquoted.mid(QStringLiteral("# Appears as ").size());
        while (!value.isEmpty()
               && (value.front().isSpace()
                   || QStringLiteral(",.)").contains(value.front()))) {
            value.remove(0, 1);
        }
        qsizetype nameEnd = 0;
        while (nameEnd < value.size() && !value.at(nameEnd).isSpace()
               && !QStringLiteral(",.)").contains(value.at(nameEnd))) {
            ++nameEnd;
        }
        const QString avatarName = value.left(nameEnd);
        QString remainder = value.mid(nameEnd);
        while (!remainder.isEmpty()
               && (remainder.front().isSpace()
                   || QStringLiteral(".,)").contains(remainder.front()))) {
            remainder.remove(0, 1);
        }
        qsizetype urlEnd = 0;
        while (urlEnd < remainder.size() && !remainder.at(urlEnd).isSpace()
               && !QStringLiteral(",)").contains(remainder.at(urlEnd))) {
            ++urlEnd;
        }
        QString avatarUrl;
        if (urlEnd > 0) avatarUrl = remainder.left(urlEnd);
        if (avatarName.isEmpty()) return false;

        if (proto && !pui->Ignored() && !pui->IsFlooding()) {
            if (!pui->IsComicUser()) {
                if (msgType & MT_CHANNELSEND) {
                    proto->ChatAnnounceNewAvatar(
                        QString::fromUtf8(GetMyCharacter()),
                        MyAvatarURL() ? QStringLiteral("?") : QString(),
                        pui->GetName(), true);
                }
                pui->ComicUser(true);
            }
            if (pui->NeedsDownload()
                && !pui->GetAvatarRealName().isNull()
                && avatarName.compare(pui->GetAvatarRealName(),
                                      Qt::CaseInsensitive) == 0) {
                if (!avatarUrl.isEmpty() && avatarUrl != QLatin1String("?")) {
                    SetUserAvatarRealInfo(pui, avatarName, avatarUrl, document);
                } else {
                    pui->SetFlag(UF_AUTODOWNLOAD | UF_INTERACTIVEDOWNLOAD,
                                 false);
                }
            } else {
                AddAndExecute(new ChangeAvatarEntry(pui, avatarName, avatarUrl),
                              document);
            }
        }
        return true;
    }

    if (unquoted.startsWith(QStringLiteral("# GetInfo"))) {
        if (proto && !pui->Ignored() && !pui->IsFlooding()) {
            const QString profile = theApp.m_myProfile.isEmpty()
                ? originalResourceString(QStringLiteral("ID_DEFAULT_PROFILE"))
                : theApp.m_myProfile;
            proto->bChatSendPrivMesg(
                pui->GetName(), QString(),
                QStringLiteral("# HeresInfo: ") + profile, nullptr, false,
                BM_HERESINFO);
        }
        return true;
    }

    if (unquoted.startsWith(QStringLiteral("# GetCharInfo"))) {
        if (proto && !pui->Ignored() && !pui->IsFlooding()) {
            proto->ChatAnnounceNewAvatar(
                QString::fromUtf8(GetMyCharacter()),
                QString::fromUtf8(MyAvatarURL() ? MyAvatarURL() : ""),
                pui->GetName(), true);
        }
        return true;
    }

    if (unquoted.startsWith(QStringLiteral("# HeresInfo: "))) {
        if (proto && pui->IsRequestInfo(RF_PROFILE)) {
            QString fullMessage = originalResourceString(
                QStringLiteral("IDS_SHOWINFO_PREFIX"));
            fullMessage.replace(QStringLiteral("%1"), pui->GetScreenName());
            fullMessage.replace(QStringLiteral("%2"), unquoted.mid(
                QStringLiteral("# HeresInfo: ").size()));
            AddAndExecute(new GetInfoEntry(pui, fullMessage), document);
            pui->DecrementRequestInfo(RF_PROFILE);
        } else if (proto) {
            pui->IsFlooding();
        }
        return true;
    }

    if (unquoted.startsWith(QStringLiteral("# BDrop: "))) {
        if (proto && !pui->Ignored() && !pui->IsFlooding()
            && pui->IsOperator()) {
            const QString backdrop = unquoted.mid(
                QStringLiteral("# BDrop: ").size()).trimmed();
            if (!backdrop.isEmpty()
                && backdrop.compare(g_lastBackdropName,
                                    Qt::CaseInsensitive) != 0) {
                AddAndExecute(new ChangeBackDropEntry(backdrop), document);
            }
        }
        return true;
    }

    if (unquoted.startsWith(QStringLiteral("# BDrop2: "))) {
        if (proto && !pui->Ignored() && !pui->IsFlooding()
            && pui->IsOperator()) {
            QString value = unquoted.mid(QStringLiteral("# BDrop2: ").size());
            while (!value.isEmpty()
                   && (value.front().isSpace() || value.front() == QLatin1Char(','))) {
                value.remove(0, 1);
            }
            qsizetype nameEnd = 0;
            while (nameEnd < value.size() && !value.at(nameEnd).isSpace()
                   && value.at(nameEnd) != QLatin1Char(',')) {
                ++nameEnd;
            }
            const QString backdrop = value.left(nameEnd);
            QString remainder = value.mid(nameEnd);
            while (!remainder.isEmpty()
                   && (remainder.front().isSpace()
                       || remainder.front() == QLatin1Char(','))) {
                remainder.remove(0, 1);
            }
            qsizetype urlEnd = 0;
            while (urlEnd < remainder.size() && !remainder.at(urlEnd).isSpace()
                   && remainder.at(urlEnd) != QLatin1Char(',')
                   && remainder.at(urlEnd) != QLatin1Char(')')) {
                ++urlEnd;
            }
            QString backdropUrl;
            if (urlEnd > 0) backdropUrl = remainder.left(urlEnd);
            if (!backdrop.isEmpty()) {
                g_lastBackdropName = backdrop;
                const qsizetype dot = g_lastBackdropName.indexOf(QLatin1Char('.'));
                if (dot >= 0) g_lastBackdropName.truncate(dot);
                AddAndExecute(new ChangeBackDropEntry(backdrop, backdropUrl),
                              document);
            }
        }
        return true;
    }
    return false;
}

void ProcessUDIData(CChatDoc*, CUserInfo* pui, const QString& data)
{
    if (!pui || !data.startsWith(QLatin1Char('#'))) {
        return;
    }
    pui->m_udi.Reset();
    pui->m_bbValidUDI = 0;
    if (theApp.m_bVIPMode && !pui->IsOperator()) return;
    parseUDITail(pui, data, 1, false);
    pui->m_bbValidUDI = 1;
}

void ProcessSayAux(CChatDoc* doc, CUserInfo* pui, QString message,
                   unsigned char msgType, const QList<CUserInfo*>* talkTos)
{
    if (!pui) {
        return;
    }

    CRoomInfo* proto = doc ? doc->m_proto : currentRoom;
    bool floodChecked = false;
    if ((msgType & MT_PRIVATEMSG) && !AcceptWhispers()) {
        pui->IsFlooding();
        return;
    }
    if (theApp.m_bVIPMode && !pui->IsOperator()) {
        pui->m_bbValidUDI = 0;
        pui->IsFlooding();
        return;
    }

    message = lowLevelUnquoteCtcp(message);

    if (message.startsWith(QStringLiteral("(#"))) {
        const int end = message.indexOf(QStringLiteral(") "), 2);
        if (end >= 0) {
            pui->m_udi.m_talkTos.clear();
            parseUDITail(pui, message.mid(2, end - 2), 0, msgType & MT_PRIVATEMSG);
            if (pui->m_udi.m_bbCooked) {
                message = message.mid(end + 2);
            }
        }
    } else if (!pui->m_bbValidUDI) {
        pui->m_udi.Reset();
        if (msgType & MT_PRIVATEMSG) {
            pui->m_udi.m_uModes &= static_cast<unsigned short>(~BM_SAY);
            pui->m_udi.m_uModes |= BM_WHISPER;
        }
    }

    pui->m_bbValidUDI = 0;

    const QString action = QString::fromLatin1(actionID, g_nActionLen);
    const QString sound = QString::fromLatin1(soundID, g_nSoundLen);
    const QString version = QString::fromLatin1(versionID, g_nVersionLen);
    const QString ping = QString::fromLatin1(pingID, g_nPingLen);
    const QString time = QString::fromLatin1(timeID, g_nTimeLen);
    const QString fileDcc = QString::fromLatin1(fileDCCID, g_nFileDCCLen);
    const QString email = QString::fromLatin1(emailID, g_nEmailLen);
    const QString url = QString::fromLatin1(urlID, g_nUrlLen);
    const QString netMeeting = QString::fromLatin1(netMeetingID, g_nNetMeetLen);
    const QString away = QString::fromLatin1(awayID, g_nAwayLen);
    const QString clientInfo = QString::fromLatin1(clientInfoID,
                                                    g_nClientInfoLen);
    const QString xvchat = QString::fromLatin1(xvchatID, g_nXVChatLen);
    const auto startsCaseInsensitive = [&message](const QString& prefix) {
        return message.startsWith(prefix, Qt::CaseInsensitive);
    };
    const auto countMalformed = [pui] { pui->IsFlooding(); };

    if (pui->m_udi.m_uModes & BM_ACTION) {
        if (pui->Ignored() || pui->IsFlooding()) return;
        message = message.startsWith(action)
            ? PrepareTextAction(pui, message, pui->m_udi.m_uModes)
            : PrepareComicsAction(pui, message);
        floodChecked = true;
    } else if (message.startsWith(action)) {
        if (pui->Ignored() || pui->IsFlooding()) return;
        message = PrepareTextAction(pui, message, pui->m_udi.m_uModes);
        floodChecked = true;
    } else if (message.startsWith(sound)) {
        if (pui->Ignored() || pui->IsFlooding()) return;
        message = PrepareSound(pui, message, pui->m_udi.m_uModes);
        floodChecked = true;
    } else if (startsCaseInsensitive(version)) {
        const QString offset = message.mid(g_nVersionLen);
        if (offset.startsWith(QLatin1Char('\001'))) {
            if (!(msgType & MT_NOTICE) && !pui->Ignored()
                && !pui->IsFlooding() && proto) proto->ReplyVersion(pui);
        } else if (!offset.isEmpty() && offset.front().isSpace()) {
            ShowVersion(pui, offset.mid(1));
        } else {
            countMalformed();
        }
        return;
    } else if (startsCaseInsensitive(ping)) {
        QString offset = message.mid(g_nPingLen);
        if (msgType & MT_PRVMSG) {
            if (!pui->Ignored() && !pui->IsFlooding() && proto
                && !offset.isEmpty()) {
                if (offset.endsWith(QLatin1Char('\001'))) offset.chop(1);
                proto->ReplyPing(pui, offset.mid(1));
            }
        } else if (!offset.isEmpty() && offset.front().isSpace()) {
            ShowPing(pui, offset.mid(1));
        } else {
            countMalformed();
        }
        return;
    } else if (startsCaseInsensitive(time)) {
        const QString offset = message.mid(g_nTimeLen);
        if (offset.startsWith(QLatin1Char('\001'))) {
            if (!(msgType & MT_NOTICE) && !pui->Ignored()
                && !pui->IsFlooding() && proto) proto->ReplyTime(pui);
        } else if (!offset.isEmpty() && offset.front().isSpace()) {
            ShowTime(pui, offset.mid(1));
        } else {
            countMalformed();
        }
        return;
    } else if (startsCaseInsensitive(fileDcc)) {
        const QString offset = message.mid(g_nFileDCCLen);
        if (!pui->Ignored() && !pui->IsFlooding()
            && !offset.isEmpty() && offset.front().isSpace()) {
            // ChatReceiveFile remains in the original filesend.* boundary.
        }
        return;
    } else if (startsCaseInsensitive(email)) {
        const QString offset = message.mid(g_nEmailLen);
        if (offset.startsWith(QLatin1Char('\001'))) {
            if (!(msgType & MT_NOTICE) && !pui->Ignored()
                && !pui->IsFlooding() && proto) proto->ReplyEmail(pui);
        } else if (!offset.isEmpty() && offset.front().isSpace()) {
            ShowEmail(pui, offset.mid(1));
        } else {
            countMalformed();
        }
        return;
    } else if (startsCaseInsensitive(url)) {
        const QString offset = message.mid(g_nUrlLen);
        if (offset.startsWith(QLatin1Char('\001'))) {
            if (!pui->Ignored() && !pui->IsFlooding() && proto) {
                proto->ReplyHomePage(pui);
            }
        } else if (!offset.isEmpty() && offset.front().isSpace()) {
            ShowHomePage(pui, offset.mid(1));
        } else {
            countMalformed();
        }
        return;
    } else if (startsCaseInsensitive(netMeeting)) {
        const QString offset = message.mid(g_nNetMeetLen);
        if (!offset.isEmpty() && offset.front().isSpace()) {
            if (!pui->Ignored()) pui->IsFlooding();
            // DoNetMeetingCX remains excluded with the original CB32 path.
        } else {
            countMalformed();
        }
        return;
    } else if (startsCaseInsensitive(away)) {
        QString offset = message.mid(g_nAwayLen + 1);
        if (!pui->Ignored() && !pui->IsFlooding()) {
            ShowAway(pui, offset, doc);
        } else {
            const qsizetype end = offset.indexOf(QLatin1Char('\001'));
            if (end >= 0) offset.truncate(end);
            pui->SetFlag(UF_AWAY, !offset.isEmpty());
        }
        return;
    } else if (startsCaseInsensitive(clientInfo)) {
        const QString offset = message.mid(g_nClientInfoLen);
        if (offset.startsWith(QLatin1Char('\001'))) {
            if (!pui->Ignored() && !pui->IsFlooding() && proto) {
                proto->bChatSendPrivMesg(
                    pui->GetName(), QString(),
                    QStringLiteral("\001CLIENTINFO ACTION AWAY CLIENTINFO DCC EMAIL NETMEET PING SOUND TIME USERINFO URL VERSION\001"),
                    nullptr, true);
            }
        } else {
            countMalformed();
        }
        return;
    } else if (startsCaseInsensitive(xvchat)) {
        return;
    } else if (message.size() >= 2 && message.at(0) == QLatin1Char('\001')
               && message.at(1) == QLatin1Char('*')) {
        const QString oldReply = message.mid(2);
        const auto oldPayload = [&oldReply](const QString& prefix,
                                            QString* payload) {
            const QString offset = oldReply.mid(prefix.size());
            if (offset.isEmpty() || !offset.front().isSpace()) return false;
            *payload = offset.mid(1);
            return true;
        };
        QString payload;
        if (oldReply.startsWith(version.mid(1), Qt::CaseInsensitive)) {
            if (!oldPayload(version.mid(1), &payload)) countMalformed();
            else ShowVersion(pui, payload);
            return;
        }
        if (oldReply.startsWith(time.mid(1), Qt::CaseInsensitive)) {
            if (!oldPayload(time.mid(1), &payload)) countMalformed();
            else ShowTime(pui, payload);
            return;
        }
        if (oldReply.startsWith(ping.mid(1), Qt::CaseInsensitive)) {
            if (!oldPayload(ping.mid(1), &payload)) countMalformed();
            else ShowPing(pui, payload);
            return;
        }
        if (oldReply.startsWith(email.mid(1), Qt::CaseInsensitive)) {
            if (!oldPayload(email.mid(1), &payload)) countMalformed();
            else ShowEmail(pui, payload);
            return;
        }
        if (oldReply.startsWith(url.mid(1), Qt::CaseInsensitive)) {
            if (!oldPayload(url.mid(1), &payload)) countMalformed();
            else ShowHomePage(pui, payload);
            return;
        }
        if (oldReply.startsWith(netMeeting.mid(1), Qt::CaseInsensitive)) {
            if (!oldPayload(netMeeting.mid(1), &payload)) countMalformed();
            return;
        }
    } else if (message.startsWith(QLatin1Char('\001'))) {
        countMalformed();
        return;
    }

    if (!floodChecked && (pui->Ignored() || pui->IsFlooding())) return;
    if (!pui->m_udi.m_bbCooked || pui->m_udi.m_talkTos.isEmpty()) {
        IdentifyWhispers(doc, pui, msgType, pui->m_udi.m_uModes, talkTos);
    }
    if (proto && (proto->m_dwModes & CM_NOFORMAT)) {
        pui->m_udi.m_uModes |= BM_NOFORMAT;
    }
    if (message.isEmpty()) return;

    if (bAddToWhisperBox(pui, pui->m_udi.m_uModes, message)) return;
    if (!doc || !doc->m_proto) return;

    enumActions actionIDs[4] = {
        static_cast<enumActions>(3),
        aDoNotDisplay,
        aHighlightMessage,
        aReplaceMessage
    };
    QString senderIdentity = pui->GetName();
    if (!pui->GetFullName().isEmpty()) {
        senderIdentity += QLatin1Char('!') + pui->GetFullName();
    }
    const QString recipients = GetAddressees(
        pui, QStringLiteral("; "), false);
    QString server = QString::fromUtf8(GetMyServer());
    QString channel = doc->m_proto->m_strChannel;
    QString originalMessage = message;
    const enumEvents event = (msgType & MT_CHANNELSEND)
        ? eOnMessage : eOnWhisperInRoom;

    theApp.m_dynaRules.SetCachRecipients(recipients);
    theApp.m_dynaRules.bMatchAndApplyRules(
        event, actionIDs, nullptr, server, senderIdentity,
        channel, originalMessage);
    if (!(theApp.m_dynaRules.GetFlags() & g_wDoNotDisplay)) {
        const QString displayed = (theApp.m_dynaRules.GetFlags() & g_wReplace)
            ? theApp.m_dynaRules.GetCFFinalMessage() : message;
        char highlightType = -1;
        if (theApp.m_dynaRules.GetFlags() & g_wHighlight) {
            highlightType = static_cast<char>(
                theApp.m_dynaRules.GetFlags() >> 8);
        }
        AddAndExecute(new SayEntry(
            pui, displayed, nullptr, highlightType), doc);
    }
    theApp.m_dynaRules.bMatchAndApplyRules(
        event, nullptr, actionIDs, server, senderIdentity,
        channel, originalMessage);
    theApp.m_dynaRules.SetCachRecipients(QString());
}

void ProcessNonComicsMsg(QString& str, unsigned short& modes)
{
    QString prefix;
    if (modes & BM_THINK) {
        prefix = originalResourceString(QStringLiteral("ID_THINK_PREFIX"));
        prefix.replace(QStringLiteral("%1"), QString());
        while (!prefix.isEmpty() && prefix.front().isSpace()) {
            prefix.remove(0, 1);
        }
        str.prepend(prefix);
        modes &= ~BM_THINK;
        modes |= BM_ACTION;
    }
    if (modes & BM_ACTION) {
        prefix = QString::fromLatin1("\001ACTION");
        prefix += QLatin1Char(' ');
        str.prepend(prefix);
        str += QLatin1Char('\001');
    }
}

QString GetAddresseesAux(CUserInfo* pui, const QString& separator, bool useNick)
{
    QString result;
    if (!pui) {
        return result;
    }
    const int count = qMin(pui->m_udi.m_talkTos.size(), 5);
    for (int i = 0; i < count; ++i) {
        CUserInfo* addressee = pui->m_udi.m_talkTos.at(i);
        if (!addressee) {
            continue;
        }
        if (!result.isEmpty()) {
            result += separator;
        }
        result += useNick ? addressee->GetName() : addressee->GetScreenName();
    }
    return result;
}

QString GetWhisperedAddressees(const QString& separator)
{
    QString result;
    const int count = qMin(g_rgpuiWhisperees.size(), 5);
    for (int i = 0; i < count; ++i) {
        CUserInfo* addressee = g_rgpuiWhisperees.at(i);
        if (!addressee) {
            continue;
        }
        if (!result.isEmpty()) {
            result += separator;
        }
        result += addressee->GetName();
    }
    return result;
}

bool bInsertAnnotations(CUserInfo* puiSelf, QString& buffer,
                        unsigned short modes, bool includeParenthesis)
{
    CAvatarX* avatar = MyAvatar();
    if (!avatar || !puiSelf) {
        return true;
    }

    CHAR faceIndex = 0;
    CHAR torsoIndex = 0;
    BYTE requested = 0;
    CEmotion face;
    CEmotion torso;
    BYTE faceEmotion = 0;
    BYTE faceIntensity = 0;
    BYTE torsoEmotion = 0;
    BYTE torsoIntensity = 0;
    avatar->GetIndices(faceIndex, torsoIndex, requested);
    avatar->GetEmotions(face, torso);
    EmotionToBytes(face, faceEmotion, faceIntensity);
    EmotionToBytes(torso, torsoEmotion, torsoIntensity);

    if (includeParenthesis) buffer += QLatin1Char('(');
    buffer += QLatin1Char('#');
    buffer += QLatin1Char(CGESTUREPREFIX);
    buffer += QLatin1Char(static_cast<char>(IndexToByte(static_cast<BYTE>(torsoIndex))));
    buffer += QLatin1Char(static_cast<char>(torsoEmotion));
    buffer += QLatin1Char(static_cast<char>(torsoIntensity));
    buffer += QLatin1Char(CEXPRESSIONPREFIX);
    buffer += QLatin1Char(static_cast<char>(IndexToByte(static_cast<BYTE>(faceIndex))));
    buffer += QLatin1Char(static_cast<char>(faceEmotion));
    buffer += QLatin1Char(static_cast<char>(faceIntensity));
    if (requested) buffer += QLatin1Char(CREQUESTEDPREFIX);
    buffer += QLatin1Char(CMODEPREFIX);
    buffer += QLatin1Char(static_cast<char>(IndexToByte(BM2SM(modes))));

    if ((modes != BM_WHISPER && !puiSelf->m_udi.m_talkTos.isEmpty())
        || (modes == BM_WHISPER && !g_rgpuiWhisperees.isEmpty())) {
        buffer += QLatin1Char(CTALKTOPREFIX);
        buffer += modes == BM_WHISPER
            ? GetWhisperedAddressees(QStringLiteral(","))
            : GetAddresseesAux(puiSelf, QStringLiteral(","), true);
    }
    if (includeParenthesis) buffer += QStringLiteral(") ");
    return true;
}

void ShowSayAux(CChatDoc* document, CUserInfo* pui, const QString& text,
                CDWordArray* formatting, BYTE cooked, unsigned short modes)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!pui || !target || pui->Ignored()) {
        return;
    }

    pui->m_udi.m_bbCooked = cooked;
    pui->m_udi.m_uModes = modes;
    pui->m_udi.m_chExprE = 0;
    pui->m_udi.m_chGestE = 0;
    pui->m_udi.m_chExprI = 0;
    pui->m_udi.m_chGestI = 0;

    if (target->m_bComicView) {
        CAvatarX* avatar = GetAvatar(pui->GetAvatarID());
        if (!avatar) {
            return;
        }
        CHAR expressionIndex = 0;
        CHAR gestureIndex = 0;
        BYTE requested = 0;
        avatar->GetIndices(expressionIndex, gestureIndex, requested);
        pui->m_udi.m_chExpr = static_cast<signed char>(expressionIndex);
        pui->m_udi.m_chGest = static_cast<signed char>(gestureIndex);
        pui->m_udi.m_bbReq = requested;
    } else {
        pui->m_udi.m_bbReq = 0;
    }
    AddAndExecute(new SayEntry(pui, text,
                               formatting ? formatting
                                          : NoFormattingSentinel()),
                  target);
}
}

CUserInfo* ExternalPui(const QString& nickname, const QString& fullName,
                       bool addIfNotThere)
{
    for (CUserInfo* pui : externalPuis) {
        if (!pui
            || pui->GetName().compare(nickname, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString existingFullName = pui->GetFullName();
        if (fullName.isNull() || fullName.isEmpty()
            || existingFullName.isEmpty()
            || existingFullName.compare(fullName, Qt::CaseInsensitive) == 0) {
            if (existingFullName.isEmpty() && !fullName.isEmpty()) {
                pui->SetFullName(fullName);
            }
            return pui;
        }
    }
    if (!addIfNotThere) return nullptr;

    auto* pui = new CUserInfo(nickname, fullName);
    pui->SetExternal(true);
    externalPuis.prepend(pui);
    return pui;
}

void DestroyExternalUserInfos()
{
    for (CUserInfo* pui : externalPuis) delete pui;
    externalPuis.clear();
}

CUserInfo* PuiFromDocNickIdent(CChatDoc** doc, const QString& nickname,
                               const QString& userIdent,
                               bool skipObscuredChannels,
                               bool addExternalIfNotThere)
{
    if (!doc) return nullptr;
    CUserInfo* pui = nullptr;
    if (*doc) {
        pui = LookupPui(nickname, *doc);
        if (pui && pui->IsDeparted()) pui = nullptr;
    } else {
        CChatDoc* active = GetChatDoc();
        if (active && active->GetConnectionStatus() == CX_INCHANNEL) {
            pui = LookupPui(nickname, active);
            if (pui && pui->IsDeparted()) pui = nullptr;
            if (pui) *doc = active;
        }
    }

    if (!pui) {
        for (CChatDoc* candidate : g_docs) {
            if (!candidate || candidate->GetConnectionStatus() != CX_INCHANNEL
                || (candidate->m_bObscured && skipObscuredChannels)) {
                continue;
            }
            CUserInfo* candidatePui = LookupPui(nickname, candidate);
            if (candidatePui && !candidatePui->IsDeparted()) {
                pui = candidatePui;
                *doc = candidate;
                break;
            }
        }
    }

    return pui ? pui : ExternalPui(nickname, userIdent,
                                   addExternalIfNotThere);
}

void ProcessSay(CChatDoc* doc, CUserInfo* pui, QString message,
                unsigned char msgType, const QList<CUserInfo*>* talkTos)
{
    ProcessSayAux(doc, pui, message, msgType, talkTos);
}

void OnKick(CChatDoc* doc, const QString& kicker, const QString& kickee,
            const QString& message)
{
    if (!doc) return;

    CUserInfo* kickeePui = LookupPui(kickee, doc);
    CUserInfo* kickerPui = LookupPui(kicker, doc);
    if (!kickeePui) return;

    QString boxMessage = originalResourceString(
        message.isEmpty() ? QStringLiteral("ID_KICK_NO_MESG")
                          : QStringLiteral("ID_KICK_MESG"));
    if (!message.isEmpty()) boxMessage.replace(QStringLiteral("%3"), message);
    boxMessage.replace(QStringLiteral("%1"),
                       kickerPui ? kickerPui->GetScreenName() : kicker);
    boxMessage.replace(QStringLiteral("%2"), kickeePui->GetScreenName());

    QByteArray controlFull = boxMessage.toUtf8();
    CDWordArray formatting;
    char* controlLessBytes = SzControlLess(controlFull.data(), &formatting);
    const QString controlLess = QString::fromUtf8(controlLessBytes);
    if (formatting.GetSize() && controlLessBytes) {
        const std::size_t length = std::strlen(controlLessBytes);
        formatting.Add(MAKELONG(0, static_cast<WORD>(length >= 2 ? length - 2 : 0)));
    }

    enumActions actionIDs[2] = {
        static_cast<enumActions>(1), aHighlightMessage
    };
    QString kickeeIdentity = kickee;
    if (!kickeePui->GetFullName().isEmpty()) {
        kickeeIdentity += QLatin1Char('!') + kickeePui->GetFullName();
    }
    QString server = QString::fromUtf8(GetMyServer());
    QString channel = doc->m_proto ? doc->m_proto->m_strChannel : QString();
    QString eventMessage;
    theApp.m_dynaRules.bMatchAndApplyRules(
        eOnKick, actionIDs, nullptr, server, kickeeIdentity,
        channel, eventMessage);

    char highlightType = -1;
    if (kickerPui) {
        kickerPui->m_udi.m_chGest = 0;
        kickerPui->m_udi.m_chExpr = 0;
        kickerPui->m_udi.m_chGestE = 0;
        kickerPui->m_udi.m_chGestI = 0;
        kickerPui->m_udi.m_chExprE = 0;
        kickerPui->m_udi.m_chExprI = 0;
        kickerPui->m_udi.m_bbCooked = 0;
        kickerPui->m_udi.m_bbReq = 1;
        kickerPui->m_udi.m_uModes = BM_ACTION;
        kickerPui->m_udi.m_talkTos.clear();
        kickerPui->m_udi.m_talkTos.append(kickeePui);
        if (theApp.m_dynaRules.GetFlags() & g_wHighlight) {
            highlightType = static_cast<char>(
                theApp.m_dynaRules.GetFlags() >> 8);
        }
        AddAndExecute(new SayEntry(kickerPui, controlLess, &formatting,
                                   highlightType), doc);
    }

    AddAndExecute(new PartEntry(kickee, highlightType), doc);
    if (kickee.compare(QString::fromUtf8(GetMyNickName()),
                       Qt::CaseInsensitive) == 0) {
        GotPartChannel(doc);
        QMessageBox::information(
            nullptr, originalResourceString(QStringLiteral("ID_APP_TITLE")),
            controlLess);
    }

    theApp.m_dynaRules.bMatchAndApplyRules(
        eOnKick, nullptr, actionIDs, server, kickeeIdentity,
        channel, eventMessage);
}

QString GetAddressees(CUserInfo* pui, const QString& separator, bool useNick)
{
    return GetAddresseesAux(pui, separator, useNick);
}

void ShowSay(CChatDoc* document, CUserInfo* pui, const QString& text,
             CDWordArray* formatting, BYTE cooked, unsigned short modes)
{
    ShowSayAux(document, pui, text, formatting, cooked, modes);
}

bool bForEachWord(const QString& line, bool (*pfn)(const QString&, void*, unsigned long),
                  void* clientData, unsigned long data, const QString& separators,
                  bool doubleQuotes)
{
    bool ret = false;
    qsizetype position = 0;
    while (position < line.size()) {
        QString word;
        if (doubleQuotes && line.at(position) == QLatin1Char('"')) {
            const qsizetype start = position++;
            while (position < line.size()
                   && line.at(position) != QLatin1Char('"')) {
                ++position;
            }
            if (position < line.size()) ++position;
            word = line.mid(start, position - start);
        } else {
            while (position < line.size()
                   && (line.at(position).isSpace()
                       || separators.contains(line.at(position)))) {
                ++position;
            }
            if (position >= line.size()) break;
            const qsizetype start = position;
            while (position < line.size()
                   && !line.at(position).isSpace()
                   && !separators.contains(line.at(position))) {
                ++position;
            }
            word = line.mid(start, position - start);
        }
        if (word.isEmpty()) continue;
        ret = pfn(word, clientData, data) || ret;
    }
    return ret;
}

void ChatEmptyMemberList(CChatDoc* doc)
{
    if (!doc) {
        doc = GetChatDoc();
    }
    if (doc && doc->m_memberList) {
        doc->m_memberList->Clear();
    }
}

void AddToMembersList(CUserInfo* pui, CChatDoc* doc)
{
    if (!doc) {
        doc = GetChatDoc();
    }
    if (!doc || !pui || doc->GetConnectionStatus() != CX_INCHANNEL) {
        return;
    }

    if (doc->m_bComicView) {
        AddToImageList(pui);
    }

    if (doc->m_memberList) {
        doc->m_memberList->AddUser(pui);
    }

    if (!g_bInEnumeration && doc->m_bComicView) {
        UpdateTitle(doc);
        if (doc->m_bIconMembers && doc->m_memberList) {
            doc->m_memberList->Sort();
        }
    }
    doc->ResetStatus(false, true);
}

void CIUserPart(const QString& nickname, CChatDoc* doc)
{
    if (!doc) doc = GetChatDoc();
    CUserInfo* pui = LookupPui(nickname, doc);
    if (!doc || !pui) return;
    pui->SetDeparted(true);
    if (doc->m_memberList) doc->m_memberList->RemoveUser(pui);
    if (doc->m_bComicView) UpdateTitle(doc);
    doc->ResetStatus(false, true);
}

void ProcessNick(CChatDoc* doc, const QString& oldNick, const QString& newNick,
                 bool updateMemberList)
{
    if (!doc || newNick.isEmpty()) return;
    CUserInfo* pui = LookupPui(oldNick, doc);
    if (!pui) {
        if (oldNick.compare(QString::fromUtf8(GetMyName()), Qt::CaseInsensitive) == 0) {
            SetMyName(newNick);
        }
        return;
    }
    if (oldNick == newNick) return;

    QString oldKey;
    auto& users = doc->m_mapNickToPtr;
    for (auto it = users.cbegin(); it != users.cend(); ++it) {
        if (it.value() == pui) {
            oldKey = it.key();
            break;
        }
    }
    if (!oldKey.isEmpty()) users.remove(oldKey);
    pui->SetName(newNick);
    if (pui == g_puiSelf) SetMyNameNick(newNick);
    users.insert(newNick, pui);
    if (updateMemberList && doc->m_memberList) {
        doc->m_memberList->AddUser(pui);
        doc->m_memberList->Sort();
    }
    if (updateMemberList && doc->m_bComicView) UpdateTitle(doc);
}

void ReinstallPui(CUserInfo* pui, const QString& newNick)
{
    if (!pui || newNick.isEmpty()) return;
    QString oldKey;
    if (!g_mapNickToPtr) return;
    for (auto it = g_mapNickToPtr->cbegin(); it != g_mapNickToPtr->cend(); ++it) {
        if (it.value() == pui) {
            oldKey = it.key();
            break;
        }
    }
    if (!oldKey.isEmpty()) g_mapNickToPtr->remove(oldKey);
    QString nickname = newNick;
    if (!nickname.isEmpty()
        && (nickname.front() == QLatin1Char(SC_HOST)
            || nickname.front() == QLatin1Char(SC_OWNER)
            || nickname.front() == QLatin1Char(SC_SPECTATOR)
            || nickname.front() == QLatin1Char(SC_HASVOICE))) {
        nickname.remove(0, 1);
    }
    pui->SetName(nickname);
    if (pui == g_puiSelf) SetMyNameNick(nickname);
    g_mapNickToPtr->insert(nickname, pui);
}

void ChatChangeAdmin(CChatDoc* doc, const QString& nickname, int setModes,
                     int unsetModes)
{
    if (!doc) return;
    CUserInfo* pui = LookupPui(nickname, doc);
    if (!pui) return;
    pui->SetFlag(static_cast<unsigned short>(setModes), true);
    pui->SetFlag(static_cast<unsigned short>(unsetModes), false);
    pui->SetFlag(UF_SPECTATOR,
                 (doc->m_proto->m_dwModes & CM_MODERATED)
                     && !pui->CheckFlag(UF_HASVOICE)
                     && !pui->CheckFlag(UF_OPERATOR));
    if (doc->m_memberList) {
        doc->m_memberList->AddUser(pui);
        doc->m_memberList->Sort();
    }
    if (pui == g_puiSelf) doc->UpdateAdminMenu();

    if (setModes & UF_OPERATOR) {
        QString identity = nickname;
        if (!pui->GetFullName().isEmpty()) {
            identity += QLatin1Char('!') + pui->GetFullName();
        }
        QString server = QString::fromUtf8(GetMyServer());
        QString channel = doc->m_proto ? doc->m_proto->m_strChannel
                                      : QString();
        QString eventMessage;
        theApp.m_dynaRules.bMatchAndApplyRules(
            eOnNewHost, nullptr, nullptr, server, identity,
            channel, eventMessage);
    }
}

void UpdateIgnoreOnEntry(const QString& room, const QString& nick,
                         const QString& user, const QString& host)
{
    CChatDoc* doc = LookupDoc(room);
    if (!doc) return;
    if (CUserInfo* pui = LookupPui(nick, doc)) {
        const QString identity = user + QLatin1Char('@') + host;
        pui->SetFullName(identity);
        if (!pui->IsSelf() && IsIgnored(identity)) {
            pui->Ignore(true);
            if (doc->m_memberList) {
                doc->m_memberList->AddUser(pui);
                doc->m_memberList->Sort();
            }
        }
    }
}

void AddIgnore(const QString& nickMask)
{
    theApp.m_ignores.insert(nickMask);
}

void RemoveIgnore(const QString& nickMask)
{
    theApp.m_ignores.remove(nickMask);
}

bool IsIgnored(const QString& nickMask)
{
    return theApp.m_ignores.contains(nickMask);
}

void IgnoreUser(const QString& nick, const QString& nickMask,
                bool ignore, bool autoIgnore)
{
    if (ignore) {
        if (!IsIgnored(nickMask)) AddIgnore(nickMask);
    } else {
        RemoveIgnore(nickMask);
    }

    const QString resource = autoIgnore
        ? QStringLiteral("IDS_AUTOFLOOD_IGNORE")
        : (ignore ? QStringLiteral("IDS_IGNORE_FEEDBACK")
                  : QStringLiteral("IDS_UNIGNORE_FEEDBACK"));
    for (CChatDoc* document : g_docs) {
        if (!document) continue;
        for (CUserInfo* pui : document->m_allChannelPuis) {
            if (!pui || pui->IsSelf() || pui->IsDeparted()) continue;
            if (!pui->GetFullName().isEmpty()
                && !pui->MatchesNickMask(nickMask)) continue;
            if (!nick.isEmpty()
                && pui->GetName().compare(nick, Qt::CaseInsensitive) != 0) continue;
            pui->Ignore(ignore);
            if (document->m_memberList) {
                document->m_memberList->AddUser(pui);
                document->m_memberList->Sort();
            }
            QString display = originalResourceString(resource);
            display.replace(QStringLiteral("%1"), pui->GetScreenName());
            AddAndExecute(new GetInfoEntry(pui, display), document);
        }
    }

    if (CWhisperBox* box = GetWhisperBox()) {
        for (int index = 0; index < box->m_leaves.size(); ++index) {
            CWhisperLeaf* leaf = box->m_leaves[index];
            if (!leaf || (leaf->m_nick != nick && leaf->m_id != nickMask))
                continue;
            QString display = originalResourceString(resource);
            display.replace(QStringLiteral("%1"),
                            DecodeNickForScreen(leaf->m_nick));
            const QByteArray bytes = display.toUtf8();
            leaf->m_richCore->iDisplayInfo(
                nullptr, 0, "", 0, bytes.constData(), 0,
                mtGetInfo, msParticipant, nullptr, -1, nullptr, 0);
            leaf->m_bIgnore = ignore;
            box->m_ignoreButton->setChecked(ignore);
        }
    }

    if (CUserInfo* external = ExternalPui(nick, nickMask, false)) {
        if (external->GetFullName().isEmpty()
            || external->MatchesNickMask(nickMask)) {
            external->Ignore(ignore);
        }
    }
}

void AssignArbitraryAvatar(CUserInfo* pui)
{
    if (!pui) {
        return;
    }
    QString avatarName;
    GetNextAvatarName(avatarName);
    CAvatarX* avatar = GetAvatar3(avatarName);
    if (!avatar) {
        return;
    }
    pui->SetAvatarID(avatar->m_avatarID);
    avatar->m_userInfo = pui;
}

CUserInfo* LookupPui(const QString& nickname, CChatDoc* doc)
{
    QString name = nickname;
    if (!name.isEmpty() && STATUS_CHAR(name.front().toLatin1())) {
        name.remove(0, 1);
    }
    if (!doc) doc = GetChatDoc();
    if (!doc) return nullptr;
    if (CUserInfo* direct = doc->m_mapNickToPtr.value(name)) return direct;
    for (auto it = doc->m_mapNickToPtr.cbegin();
         it != doc->m_mapNickToPtr.cend(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0) return it.value();
    }
    return nullptr;
}

CUserInfo* CIUserJoin(CUserInfo* pui)
{
    CChatDoc* doc = GetChatDoc();
    if (!doc || !pui) {
        return nullptr;
    }

    const QString nickname = pui->GetName();
    if (pui->IsSelf()) {
        g_puiSelf = pui;
        doc->m_puiSelf = pui;
        pui->ComicUser(true);
        if (doc->m_bComicView && MyAvatarID() != 0) {
            pui->SetAvatarID(static_cast<unsigned short>(MyAvatarID()));
            if (CAvatarX* avatar = MyAvatar()) {
                avatar->m_userInfo = pui;
            }
        }
        doc->UpdateAdminMenu();
    } else if (doc->m_bComicView) {
        AssignArbitraryAvatar(pui);
    }

    if (doc->m_proto && (doc->m_proto->m_dwModes & CM_MODERATED)
        && !pui->CheckFlag(UF_HASVOICE) && !pui->IsOperator()) {
        pui->SetFlag(UF_SPECTATOR, true);
    }

    doc->m_mapNickToPtr.insert(nickname, pui);
    doc->m_allChannelPuis.append(pui);
    AddToMembersList(pui, doc);
    return pui;
}

int AddToImageList(CUserInfo* pui)
{
    if (!pui) return -1;
    CAvatarX* avatar = GetAvatar(pui->GetAvatarID());
    if (!avatar) return -1;
    CAvatarX* original = avatar->m_origID
        ? GetAvatar(avatar->m_origID) : avatar;
    if (!original) return -1;

    if (original->m_iconIndex < 0) {
        CPose* iconPose = avatar->GetIconPose();
        CDIB* drawing = iconPose ? iconPose->GetDrawing() : nullptr;
        if (!drawing || drawing->Image().isNull()) return -1;

        original->m_iconIndex = theApp.m_ImageList.size();
        theApp.m_ImageList.append(
            QIcon(QPixmap::fromImage(drawing->Image())));
    }
    return original->m_iconIndex;
}

int FindMemberListIndex(CUserInfo* pui, CChatDoc* doc)
{
    if (!pui) return -1;
    if (!doc) doc = GetChatDoc();
    CMemberListCtrl* members = doc && doc->m_memberList
        ? doc->m_memberList->m_MemberListBox : nullptr;
    if (!members) return -1;
    for (INT index = 0; index < members->count(); ++index) {
        QListWidgetItem* item = members->item(index);
        if (item && item->data(CMemberList::UserPointerRole).value<void*>()
                == pui) {
            return index;
        }
    }
    return -1;
}

int RemoveMemberFromList(CUserInfo* pui)
{
    CChatDoc* doc = GetChatDoc();
    const INT index = FindMemberListIndex(pui, doc);
    CMemberListCtrl* members = doc && doc->m_memberList
        ? doc->m_memberList->m_MemberListBox : nullptr;
    if (members && index >= 0) delete members->takeItem(index);
    return index;
}

bool bSingleJoin(const QString& attedNick, void* pDoc, unsigned long)
{
    auto* doc = static_cast<CChatDoc*>(pDoc);
    if (!doc) {
        return false;
    }
    CUserInfo* pui = new CUserInfo(attedNick);
    AddAndExecute(new JoinEntry(pui), doc);
    return pui == doc->m_puiSelf;
}

void ProcessBeginEnumeration()
{
    g_bInEnumeration = true;
}

void ProcessEndEnumeration(CChatDoc* doc)
{
    g_bInEnumeration = false;
    if (!doc) doc = GetChatDoc();
    if (!doc) {
        return;
    }
    if (!doc->m_puiSelf && doc->m_proto) {
        doc->m_proto->ChatPartChannel(doc, false);
        return;
    }
    if (doc->m_bComicView) {
        UpdateTitle(doc);
        if (doc->m_bIconMembers && doc->m_memberList) {
            doc->m_memberList->Sort();
        }
    }
    doc->ResetStatus(false, true);
    doc->SetModifiedFlag(false);
}

bool bProcessAddChannel(const QString& channelName, CRoomInfo* proto,
                        SHORT* keepServer, BOOL* prompt)
{
    if (!proto || !keepServer || !prompt || *keepServer < 0) return false;
    CChatDoc* doc = LookupDoc(channelName);
    if (!doc) doc = LookupDoc(QString());
    if (!doc && theApp.m_pMainWnd) {
        ++*keepServer;
        *prompt = FALSE;
        theApp.m_pMainWnd->CreateNewDocument();
        doc = LookupDoc(QString());
    }
    if (!doc) {
        // This is the original low-resource/document-creation failure path.
        proto->SendMessageText(QStringLiteral("PART %1\r\n").arg(channelName));
        delete proto;
        return false;
    }
    if (doc->m_proto != proto) delete doc->m_proto;
    currentRoom = proto;
    doc->m_proto = dynamic_cast<CIrcProto*>(proto);
    if (!doc->m_proto) {
        delete proto;
        return false;
    }
    proto->m_doc = doc;
    proto->m_strChannel = channelName;
    proto->m_strPrettyChannel = DecodeChan(channelName);
    doc->SetTitle(proto->m_strPrettyChannel);
    if (theApp.m_pMainWnd) theApp.m_pMainWnd->ActivateDocument(doc);
    ChatSetChannel(DecodeChan(g_enterInfo.m_strChannel));
    proto->SetConnectionStatus(CX_INCHANNEL);
    SetChatDoc(doc);
    doc->DestroyUserState();
    doc->m_mapNickToPtr.clear();
    ChatEmptyMemberList(doc);
    doc->InitHistory();
    doc->ResetStatus();
    ProcessBeginEnumeration();
    return true;
}

void InitializeChannelConnection(CRoomInfo& enterInfo, SHORT* keepServer,
                                 BOOL* prompt, BOOL createRoom)
{
    if (!keepServer || !prompt || *keepServer < 0) return;
    if (*keepServer > 0) --*keepServer;
    if (*prompt) {
        CChannelDlg dialog(theApp.m_pMainWnd.data());
        dialog.m_bIsIRCX = GetDefaultProto() && GetDefaultProto()->IsIRCX();
        dialog.exec();
        bInitEnterInfo(enterInfo, dialog.m_strChannel,
                       dialog.m_strPassword, QString(), 0L, TRUE);
    }
    *prompt = TRUE;

    CRoomInfo* protocol = GetDefaultProto();
    if (!protocol) return;
    if (createRoom) protocol->ChatCreateChannel(enterInfo);
    else protocol->ChatJoinChannel(enterInfo);
}

bool bInitEnterInfo(CRoomInfo& enterInfo, const QString& channelName,
                    const QString& channelPassword,
                    const QString& creationModes, DWORD maxUsers,
                    BOOL encodeChannel)
{
    enterInfo.m_bSetMode = FALSE;
    enterInfo.m_dwModes = CM_NOEXTERN | CM_TOPICHOST;
    enterInfo.m_dwMaxUsers = maxUsers;
    enterInfo.m_strPassword = channelPassword;
    enterInfo.m_strCreationModes = creationModes;
    enterInfo.m_strChannel = encodeChannel
        ? EncodeChan(channelName) : channelName;
    enterInfo.m_strTopic.clear();
    FreeAndNullFormatting(&enterInfo.m_prgdwTopicFormatting);

    if (enterInfo.m_strChannel.contains(QLatin1Char(','))) {
        ShowBadChannelName(enterInfo.m_strChannel);
        return false;
    }
    return true;
}

void ChatSwitchChannel(const QString& channelName)
{
    CChannelDlg dialog(theApp.m_pMainWnd.data());
    dialog.m_bIsIRCX = GetDefaultProto() && GetDefaultProto()->IsIRCX();
    if (!channelName.isNull()) dialog.m_strChannel = DecodeChan(channelName);
    if (dialog.exec() != QDialog::Accepted) return;

    g_bEnterOnCreate = FALSE;
    bSwitchToRoom(dialog.m_strChannel, dialog.m_strPassword,
                  QString(), 0L, TRUE);
}

void OnBadChannelPassword(CRoomInfo& enterInfo)
{
    constexpr int maximumDisplayedChannelLength = 20;
    QString channelName = DecodeChan(enterInfo.m_strChannel);
    if (channelName.size() > maximumDisplayedChannelLength) {
        channelName = channelName.left(maximumDisplayedChannelLength)
            + QStringLiteral("...");
    }

    if (!enterInfo.m_strPassword.isEmpty())
        showOriginalMessage(IDS_BAD_PASSWORD);

    CPasswordDlg dialog(theApp.m_pMainWnd.data());
    dialog.m_strMessage = originalResourceString(ID_PASSWORD_PROMPT);
    ReplaceToken(dialog.m_strMessage, QStringLiteral("%1"), channelName);
    dialog.m_strPassword = enterInfo.m_strPassword;
    enterInfo.m_strPassword.clear();
    if (dialog.exec() == QDialog::Accepted) {
        g_bEnterOnCreate = FALSE;
        bSwitchToRoom(enterInfo.m_strChannel, dialog.m_strPassword,
                      QString(), 0L, FALSE, TRUE);
    }
}

void ShowBadChannelName(const QString& channelName)
{
    const BOOL onCreate = g_bEnterOnCreate;
    QString message = originalResourceString(ID_ERR_BADCHANNELNAME);
    message.replace(QStringLiteral("%1"), DecodeChan(channelName));
    showOriginalMessage(message);
    if (onCreate) ChatCreateRoom(g_enterInfo);
    else ChatSwitchChannel(channelName);
}

void ChatCreateRoom(CRoomInfo& enterInfo)
{
    CChannelCreateDlg properties(theApp.m_pMainWnd.data());
    properties.m_bIsIRCX = GetDefaultProto() && GetDefaultProto()->IsIRCX();

    bInitEnterInfo(enterInfo, QStringLiteral(""), QString(),
                   QString(), 0L, FALSE);
    properties.m_bSecret = (enterInfo.m_dwModes & CM_HIDDEN) != 0;
    properties.m_bPrivate = (enterInfo.m_dwModes & CM_PRIVATE) != 0;
    properties.m_bInviteOnly = (enterInfo.m_dwModes & CM_INVITEONLY) != 0;
    properties.m_bModerated = (enterInfo.m_dwModes & CM_MODERATED) != 0;
    properties.m_bTopicAnyone = (enterInfo.m_dwModes & CM_TOPICHOST) == 0;
    properties.m_bSetMax = enterInfo.m_dwMaxUsers != 0;
    properties.m_uMaxParticipants = enterInfo.m_dwMaxUsers;
    properties.m_bSetPassword = !enterInfo.m_strPassword.isEmpty()
        && (enterInfo.m_dwModes & CM_CHANNELKEY);
    properties.m_strPassword = enterInfo.m_strPassword;
    properties.m_strChannelName = DecodeChan(enterInfo.m_strChannel);
    properties.m_rtfTopic.m_strText = enterInfo.m_strTopic;
    FreeAndNullFormatting(&properties.m_rtfTopic.m_prgdwFormatting);
    properties.m_rtfTopic.m_prgdwFormatting = CopyFormatting(
        enterInfo.m_prgdwTopicFormatting);
    properties.m_rtfTopic.DefineDefaultCharFormat();

    if (properties.exec() != QDialog::Accepted) return;

    enterInfo.m_dwModes = CM_NOEXTERN;
    if (properties.m_bSecret) enterInfo.m_dwModes |= CM_HIDDEN;
    if (properties.m_bPrivate) enterInfo.m_dwModes |= CM_PRIVATE;
    if (properties.m_bInviteOnly) enterInfo.m_dwModes |= CM_INVITEONLY;
    if (properties.m_bModerated) enterInfo.m_dwModes |= CM_MODERATED;
    if (!properties.m_bTopicAnyone) enterInfo.m_dwModes |= CM_TOPICHOST;
    if (properties.m_bSetMax) {
        enterInfo.m_dwMaxUsers = properties.m_uMaxParticipants;
        enterInfo.m_dwModes |= CM_USERLIMIT;
    }
    if (properties.m_bSetPassword) {
        enterInfo.m_strPassword = properties.m_strPassword;
        enterInfo.m_dwModes |= CM_CHANNELKEY;
    }
    enterInfo.m_strTopic = properties.m_rtfTopic.m_strText;
    FreeAndNullFormatting(&enterInfo.m_prgdwTopicFormatting);
    enterInfo.m_prgdwTopicFormatting = CopyFormatting(
        properties.m_rtfTopic.m_prgdwFormatting);
    enterInfo.m_bSetMode = TRUE;
    enterInfo.m_strChannel = EncodeChan(properties.m_strChannelName);
    g_bEnterOnCreate = TRUE;
    if (!enterInfo.m_strChannel.contains(QLatin1Char(','))) bSwitchToRoom();
    else ShowBadChannelName(enterInfo.m_strChannel);
}

void ConfirmAway(const QString* conditionallyOnText)
{
    if (!theApp.m_bAway || !theApp.m_bAwayPrompt) return;
    if (conditionallyOnText && conditionallyOnText->startsWith(QLatin1Char('/'))) {
        const QString command = conditionallyOnText->section(
            QRegularExpression(QStringLiteral("\\s")), 0, 0);
        if (command.compare(QStringLiteral("/AWAY"),
                            Qt::CaseInsensitive) == 0) return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        theApp.m_pMainWnd.data(),
        originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
        originalResourceString(IDS_CONFIRMAWAY),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        theApp.m_bAway = false;
        theApp.m_bAwayPrompt = false;
        if (CRoomInfo* protocol = GetDefaultProto()) {
            protocol->ChatSetAway(false, theApp.m_strAwayMessage);
        }
    } else {
        theApp.m_bAwayPrompt = false;
    }
}

bool bSwitchToRoom(const QString& newRoom, const QString& password,
                   const QString& creationModes, DWORD maxUsers,
                   BOOL encodeChannel, BOOL createRoomInfo,
                   BOOL createRoom)
{
    ConfirmAway();

    CRoomInfo* enterRoom = &g_enterInfo;
    if (createRoomInfo) {
        if (newRoom.isNull() || newRoom.isEmpty()) return false;
        enterRoom = new CRoomInfo;
        theApp.AddRoomInfo(enterRoom);
    }

    QString room = newRoom.isNull() ? g_enterInfo.m_strChannel : newRoom;
    if (encodeChannel) room = EncodeChan(room);
    if (room.contains(QLatin1Char(','))) {
        ShowBadChannelName(room);
        return false;
    }

    CChatDoc* document = LookupDoc(room);
    if (document && document->GetConnectionStatus() != CX_INCHANNEL) {
        if (theApp.m_pMainWnd) theApp.m_pMainWnd->CloseDocument(document);
        document = nullptr;
    }

    if (document) {
        if (theApp.m_pMainWnd) theApp.m_pMainWnd->ActivateDocument(document);
    } else {
        g_bCXPrompt = FALSE;
        if (!newRoom.isNull()) {
            bInitEnterInfo(*enterRoom, room, password, creationModes,
                           maxUsers, FALSE);
        }
        InitializeChannelConnection(*enterRoom, &g_nCXKeepServer,
                                    &g_bCXPrompt, createRoom);
    }
    return true;
}

void ChatServerDisconnect(BOOL bCheckRules, BOOL bResumeConnection)
{
    serverConn.Disconnect();

    if (!bResumeConnection) {
        theApp.m_SrvConnector.Cleanup();
    }

    QString ident;
    if (!theApp.m_myIdent.isEmpty()) {
        ident = QLatin1Char('!') + theApp.m_myIdent;
    }

    serverConn.m_queries.FreeRemoveAll();

    theApp.m_bAway = FALSE;
    theApp.m_ignores.clear();
    theApp.m_nMyIdentLength = 0;
    theApp.m_myIdent.clear();
    theApp.CleanRoomInfos();

    serverConn.Reset();

    for (CChatDoc* document : g_docs) {
        if (!document) continue;
        auto* protocol = dynamic_cast<CIrcProto*>(document->m_proto);
        if (protocol && protocol->m_bInRoom) GotPartChannel(document);
    }

    CRoomInfo* defaultProtocol = GetDefaultProto();
    const ConnectionStatus previousStatus = defaultProtocol
        ? defaultProtocol->GetConnectionStatus() : CX_DISCONNECTED;
    if (defaultProtocol) {
        defaultProtocol->SetConnectionStatus(
            bResumeConnection ? CX_CONNECTING : CX_DISCONNECTED);
    }

    if (bCheckRules && previousStatus != CX_DISCONNECTED
        && previousStatus != CX_CONNECTING) {
        theApp.m_dynaNotifs.bStopNotifsDaemon();
        theApp.m_dynaNotifs.bUpdateNotifsDaemonExt(TRUE);
        theApp.m_dynaRules.bStopRulesDaemon();
        theApp.m_dynaRules.bUpdateRuleSetsDaemonExt(TRUE);
        QString server = QString::fromUtf8(GetMyServer());
        QString identity = QString::fromUtf8(GetMyNickName()) + ident;
        QString channel;
        QString message;
        theApp.m_dynaRules.bMatchAndApplyRules(
            eOnDisconnect, nullptr, nullptr, server, identity,
            channel, message);
    }
}

BOOL bChatServerConnect(const QString& server)
{
    CRoomInfo* protocol = GetDefaultProto();
    if (!protocol || protocol->GetConnectionStatus() != CX_DISCONNECTED)
        return FALSE;
    protocol->SetConnectionStatus(CX_CONNECTING);
    if (!theApp.m_SrvConnector.BeginConnectToService(server)) {
        theApp.OnConnectError();
        return FALSE;
    }
    theApp.StartConnectionTimer();
    return TRUE;
}

void ReconnectToServer(const QString& decodedNickname,
                       const QString& serverName)
{
    SetMyName(decodedNickname);
    ChatServerDisconnect(FALSE, FALSE);
    if (bInitEnterInfo(g_enterInfo, theApp.m_myChannel, QString(),
                       QString(), 0L, TRUE)) {
        QString service;
        theApp.m_listChatServices.GetServiceNameFromDisplayName(
            serverName, service);
        theApp.m_strConnectedService = service;
        bChatServerConnect(service);
    }
}

void GotPartChannel(CChatDoc* doc)
{
    if (!doc) return;
    if (doc->GetConnectionStatus() != CX_DISCONNECTED && doc->m_proto) {
        doc->m_proto->SetConnectionStatus(CX_NOCHANNEL);
    }
    ChatEmptyMemberList(doc);
}

bool CRoomInfo::SlashRaw(const QString& message, IRCPARSE* parse)
{
    if (message.startsWith(QStringLiteral("JOIN "), Qt::CaseInsensitive)
        || message.startsWith(QStringLiteral("CREATE "),
                              Qt::CaseInsensitive)) {
        showOriginalMessage(IDS_ERR_RAWJOINCREATE);
        return false;
    }

    // The source permits LIST/LISTX when the WinInet ratings DLL is absent.
    // Qt has no replacement ratings provider, so this is that exact fallback.
    if (message.startsWith(QStringLiteral("MODE "), Qt::CaseInsensitive)
        && parse && parse->nArgs > 2) {
        return bRegisterMode(message.mid(5));
    }

    SendMessageText(message + QStringLiteral("\r\n"));
    return true;
}

bool CRoomInfo::ProcessSlashCommand(const QString&, CDWordArray*,
                                    unsigned short, bool)
{
    showOriginalMessage(IDS_NOSLASH_COMMANDS);
    return false;
}

SYNTAX CIrcProto::GetSyntaxFromCmdId(enumCmdId command,
                                     unsigned short* index) const
{
    unsigned short found = 0;
    while (found < g_uSyntaxCount && g_rgSyntax[found].cmdid != command) {
        ++found;
    }
    if (index) *index = found;
    return found < g_uSyntaxCount ? g_rgSyntax[found] : SYNTAX{};
}

QString CIrcProto::StrSyntaxMessage(enumCmdId command) const
{
    QString result = originalResourceString(IDS_SYNTAXPREFIX);
    const QString space = originalResourceString(IDS_AT_SPACEMULTIPLE);
    unsigned int index = 0;
    while (index < g_uSyntaxCount && g_rgSyntax[index].cmdid != command) {
        ++index;
    }

    while (index < g_uSyntaxCount && g_rgSyntax[index].cmdid == command) {
        const SYNTAX syntax = g_rgSyntax[index];
        result += QLatin1Char('\n')
            + QString::fromLatin1(g_rgIrcCmd[command].szCmd);
        for (unsigned int argument = 0; argument < syntax.uArgNum; ++argument) {
            result += QLatin1Char(' ');
            if (syntax.dwArgType[argument] & AT_SHOWCOLON) {
                result += QLatin1Char(':');
            }
            if (syntax.dwArgType[argument] & AT_OPTIONAL) {
                result += QLatin1Char('[');
            }

            QString argumentText = QStringLiteral("<");
            DWORD type = AT_NICKNAME;
            bool first = true;
            for (unsigned int typeIndex = 0; typeIndex < AT_COUNT;
                 ++typeIndex, type *= 2) {
                if (!(syntax.dwArgType[argument] & type)) continue;
                const QString key = originalResourceString(
                    IDS_AT_NICKNAME + static_cast<int>(typeIndex));
                if (key.isEmpty()) continue;
                if (!first) argumentText += QLatin1Char('|');
                argumentText += key;
                first = false;
            }
            argumentText += QLatin1Char('>');
            result += argumentText;
            if (syntax.dwArgType[argument] & AT_COMMAMULTIPLE) {
                result += QStringLiteral("{,") + argumentText
                    + QLatin1Char('}');
            }
            if (syntax.dwArgType[argument] & AT_SPACEMULTIPLE) {
                result += QStringLiteral("{<") + space + QLatin1Char('>')
                    + argumentText + QLatin1Char('}');
            }
            if (syntax.dwArgType[argument] & AT_OPTIONAL) {
                result += QLatin1Char(']');
            }
        }
        if (syntax.uIDSComment) {
            const QString comment = originalResourceString(
                static_cast<int>(syntax.uIDSComment));
            if (!comment.isEmpty()) result += QLatin1Char('\n') + comment;
        }
        ++index;
    }
    return result;
}

bool CIrcProto::SlashGeneric(enumCmdId command, IRCPARSE* parse,
                             const QString& message,
                             CDWordArray* formatting)
{
    if (!parse || command >= cmdidMax) return false;
    if (parse->nArgs < g_rgIrcCmd[command].uMinArg + 1) {
        showOriginalMessage(StrSyntaxMessage(command));
        return false;
    }

    const SYNTAX syntax = GetSyntaxFromCmdId(command);
    int encoding = ENC_DBCS;
    QString output = QString::fromLatin1(g_rgIrcCmd[command].szCmd);
    for (int argument = 0;
         argument < syntax.uArgNum && argument < parse->nArgs - 1;
         ++argument) {
        const DWORD argumentType = syntax.dwArgType[argument];
        QString parameter;
        if (argumentType & (AT_TOPIC | AT_MESSAGE | AT_REASON)) {
            bool converted = true;
            parameter = controlFullTail(
                message, parse->nOffsets[argument + 1], formatting, &converted);
            if (!converted) return false;
        } else if (argumentType & AT_SPACEMULTIPLE) {
            parameter = sourceTail(message, parse->nOffsets[argument + 1]);
        } else {
            parameter = parse->args.value(argument + 1);
        }
        if (parameter.isEmpty()) continue;

        output += QLatin1Char(' ');
        if (argumentType & AT_COLON) output += QLatin1Char(':');
        if (argumentType & (AT_COMMAMULTIPLE | AT_SPACEMULTIPLE)) {
            const QStringList words = commandWords(parameter);
            const QChar separator = argumentType & AT_COMMAMULTIPLE
                ? QLatin1Char(',') : QLatin1Char(' ');
            QStringList encoded;
            for (const QString& word : words) {
                encoded.append(StrEncodeCommandParam(argumentType, &encoding,
                                                     word));
            }
            output += encoded.join(separator);
        } else {
            output += StrEncodeCommandParam(argumentType, &encoding,
                                            parameter);
        }
    }
    SendMessageText(output + QStringLiteral("\r\n"));
    return true;
}

bool CIrcProto::SlashMode(IRCPARSE* parse)
{
    if (!parse || parse->nArgs < 2 || parse->nArgs > 7) {
        showOriginalMessage(StrSyntaxMessage(cmdidMode));
        return false;
    }

    int encoding = ENC_DBCS;
    unsigned short syntaxIndex = 0;
    SYNTAX syntax = GetSyntaxFromCmdId(cmdidMode, &syntaxIndex);
    const QString target = StrEncodeCommandParam(
        AT_CHANNEL | AT_NICKNAME, &encoding, parse->args.at(1));
    QString output = QStringLiteral("MODE ") + target;
    const bool channel = !target.isEmpty()
        && (target.front() == QLatin1Char('#')
            || target.front() == QLatin1Char('%')
            || target.front() == QLatin1Char('&'));
    if (channel) {
        if (parse->nArgs < 7 && parse->nArgs > 2) {
            const QString flags = parse->args.at(2);
            unsigned short typeIndex = 2;
            if (flags.contains(QLatin1Char('l')))
                syntax.dwArgType[typeIndex++] = AT_MAXMEMBER;
            if (flags.contains(QLatin1Char('o'))
                || flags.contains(QLatin1Char('q'))
                || flags.contains(QLatin1Char('v')))
                syntax.dwArgType[typeIndex++] = AT_NICKNAME;
            if (flags.contains(QLatin1Char('b')))
                syntax.dwArgType[typeIndex++] = AT_NICKMASK;
            if (flags.contains(QLatin1Char('k')))
                syntax.dwArgType[typeIndex++] = AT_PASSWORD;
        }
    } else {
        if (parse->nArgs > 3) {
            showOriginalMessage(StrSyntaxMessage(cmdidMode));
            return false;
        }
        syntax = g_rgSyntax[syntaxIndex + 1];
    }

    for (int argument = 1;
         argument < syntax.uArgNum && argument < parse->nArgs - 1;
         ++argument) {
        output += QLatin1Char(' ')
            + StrEncodeCommandParam(syntax.dwArgType[argument], &encoding,
                                    parse->args.at(argument + 1));
    }
    if (parse->nArgs > 2) return bRegisterMode(output.mid(5));
    SendMessageText(output + QStringLiteral("\r\n"));
    return true;
}

bool CIrcProto::SlashProp(IRCPARSE* parse)
{
    if (!parse || parse->nArgs < 2) {
        showOriginalMessage(StrSyntaxMessage(cmdidProp));
        return false;
    }

    int encoding = ENC_DBCS;
    unsigned short syntaxIndex = 0;
    SYNTAX syntax = GetSyntaxFromCmdId(cmdidProp, &syntaxIndex);
    if (parse->bHasLastString) syntax = g_rgSyntax[syntaxIndex + 1];
    QString output = QStringLiteral("PROP");
    int argument = 0;
    for (; argument < syntax.uArgNum && argument < parse->nArgs - 1;
         ++argument) {
        output += QLatin1Char(' ')
            + StrEncodeCommandParam(syntax.dwArgType[argument], &encoding,
                                    parse->args.at(argument + 1));
    }
    if (parse->bHasLastString) {
        output += QStringLiteral(" :");
        if (!parse->lastString.isEmpty()) {
            output += StrEncodeCommandParam(syntax.dwArgType[argument],
                                            &encoding, parse->lastString);
        }
    }
    SendMessageText(output + QStringLiteral("\r\n"));
    return true;
}

bool CIrcProto::SlashJoin(IRCPARSE* parse)
{
    if (!parse || parse->nArgs < 2 || parse->nArgs > 3
        || parse->args.at(1).contains(QLatin1Char(','))) {
        showOriginalMessage(StrSyntaxMessage(cmdidJoin));
        return false;
    }
    const QString password = parse->nArgs > 2
        ? parse->args.at(2) : QString();
    g_bEnterOnCreate = FALSE;
    return bSwitchToRoom(parse->args.at(1), password, QString(),
                         0L, TRUE);
}

bool CIrcProto::SlashCreate(IRCPARSE* parse)
{
    if (!parse || parse->nArgs < 3 || parse->nArgs > 5
        || parse->args.at(1).contains(QLatin1Char(','))) {
        showOriginalMessage(StrSyntaxMessage(cmdidCreate));
        return false;
    }
    const QString modes = parse->args.at(2);
    QString password;
    DWORD maxUsers = 0L;
    if (parse->nArgs > 4) {
        password = parse->args.at(4);
        maxUsers = parse->args.at(3).toULong();
    } else if (parse->nArgs > 3) {
        const QString value = parse->args.at(3);
        bool allDigits = !value.isEmpty();
        for (QChar character : value) {
            if (!character.isDigit()) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) maxUsers = value.toULong();
        else password = value;
    }
    g_bEnterOnCreate = TRUE;
    return bSwitchToRoom(parse->args.at(1), password, modes,
                         maxUsers, TRUE, FALSE, TRUE);
}

bool CIrcProto::SlashPart(IRCPARSE* parse)
{
    if (!parse || parse->nArgs > 2) {
        showOriginalMessage(StrSyntaxMessage(cmdidPart));
        return false;
    }
    CChatDoc* document = parse->nArgs > 1
        ? LookupDoc(EncodeChan(parse->args.at(1))) : GetChatDoc();
    if (!document || (parse->nArgs == 1 && document->m_bStatusView)) {
        QString message;
        if (parse->nArgs == 1 && document && document->m_bStatusView) {
            message = originalResourceString(IDS_ROOMNEEDSFOCUS);
        } else if (parse->nArgs > 1) {
            message = originalResourceString(IDS_NOTINROOM);
            message.replace(QStringLiteral("%1"), parse->args.at(1));
        } else {
            message = originalResourceString(IDS_NOTINANYROOM);
        }
        showOriginalMessage(message);
        return false;
    }
    document->OnLeave();
    return true;
}

bool CIrcProto::SlashNick(IRCPARSE* parse)
{
    if (!parse || parse->nArgs != 2) {
        showOriginalMessage(StrSyntaxMessage(cmdidNick));
        return false;
    }
    ChatSetNick(trimOuterQuotes(parse->args.at(1)));
    return true;
}

bool CIrcProto::SlashPrivMsg(IRCPARSE* parse, const QString& message,
                             CDWordArray* formatting,
                             bool invokedByWhisperBox)
{
    if (!parse || parse->nArgs < 3) {
        showOriginalMessage(StrSyntaxMessage(cmdidPrivMsg));
        return false;
    }

    CDWordArray* pulled = PullFormattingOffsets(formatting,
                                                parse->nOffsets[2]);
    const QString text = sourceTail(message, parse->nOffsets[2]);
    const QStringList receivers = commandWords(parse->args.at(1));
    for (const QString& receiverValue : receivers) {
        int encoding = ENC_DBCS;
        const QString receiver = StrEncodeCommandParam(
            AT_CHANNEL | AT_NICKNAME, &encoding, receiverValue);
        const bool channel = !receiver.isEmpty()
            && (receiver.front() == QLatin1Char('#')
                || receiver.front() == QLatin1Char('%')
                || receiver.front() == QLatin1Char('&'));
        if (channel) {
            CChatDoc* target = LookupDoc(receiver);
            if (!target || target->GetConnectionStatus() != CX_INCHANNEL)
                continue;
            CChatDoc* old = GetChatDoc();
            if (old != target) SetChatDoc(target);
            bChatSendText(text, BM_SAY, true, pulled, &receiver, false,
                          invokedByWhisperBox);
            if (old != target) SetChatDoc(old);
            continue;
        }

        CChatDoc* target = nullptr;
        CUserInfo* pui = PuiFromDocNickIdent(
            &target, trimOuterQuotes(receiver), QString(), true, true);
        if (!invokedByWhisperBox && target
            && target->GetConnectionStatus() == CX_INCHANNEL) {
            g_rgpuiWhisperees.clear();
            g_rgpuiWhisperees.append(pui);
            const QString channelName = target->m_proto->m_strChannel;
            bChatSendText(text, BM_WHISPER, true, pulled, &channelName, true,
                          false);
        } else if (pui) {
            WhisperBox(pui, FALSE);
            bWhisperInBox(QString(), text, pulled, BM_WHISPER);
        }
    }
    if (pulled) FreeAndNullFormatting(&pulled);
    return true;
}

bool CIrcProto::SlashMeOrThink(enumCmdId command, IRCPARSE* parse,
                               const QString& message,
                               CDWordArray* formatting,
                               unsigned short modes,
                               bool invokedByWhisperBox)
{
    if (!parse || parse->nArgs < 2) {
        showOriginalMessage(StrSyntaxMessage(command));
        return false;
    }
    CDWordArray* pulled = PullFormattingOffsets(formatting,
                                                parse->nOffsets[1]);
    bool result = false;
    if (invokedByWhisperBox) {
        const unsigned short sendModes = command == cmdidMe
            ? static_cast<unsigned short>(BM_WHISPER | BM_ACTION)
            : BM_WHISPER;
        result = bWhisperInBox(
            QString(), sourceTail(message, parse->nOffsets[1]),
            pulled, sendModes);
    } else {
        const unsigned short sendModes = command == cmdidMe
            ? static_cast<unsigned short>(BM_ACTION | (modes & BM_WHISPER))
            : BM_THINK;
        result = bChatSendText(
            sourceTail(message, parse->nOffsets[1]), sendModes, true, pulled);
    }
    if (pulled) FreeAndNullFormatting(&pulled);
    return result;
}

bool CIrcProto::SlashSound(IRCPARSE* parse, const QString&,
                           CDWordArray*)
{
    if (!parse || (parse->nArgs != 2
                   || parse->args.at(1).compare(
                          QStringLiteral("off"), Qt::CaseInsensitive) != 0)
            && parse->nArgs < 3) {
        showOriginalMessage(StrSyntaxMessage(cmdidSound));
    }
    // Playback and the source's bChatSendSound path belong to mcithrd.*.
    return false;
}

bool CIrcProto::SlashList(IRCPARSE* parse, const QString& message)
{
    if (!parse || parse->nArgs > 2) {
        showOriginalMessage(StrSyntaxMessage(cmdidList));
        return false;
    }
    if (parse->nArgs > 1) {
        const QString parameters = sourceTail(message, parse->nOffsets[1]);
        const QString command = parse->args.at(1).contains(QLatin1Char(','))
                || !IsIRCX()
            ? QStringLiteral("LIST ") : QStringLiteral("LISTX ");
        OnChatroomListAux(command + parameters + QStringLiteral("\r\n"));
    } else {
        OnChatroomListAux();
    }
    return true;
}

bool CIrcProto::SlashWho(const QString& message)
{
    OnUserListAux(message.startsWith(QLatin1Char('/'))
                      ? message.mid(1) : message,
                  QString(), QString());
    return true;
}

bool CIrcProto::SlashServer(IRCPARSE* parse, const QString& message)
{
    if (!parse || parse->nArgs < 2) {
        showOriginalMessage(StrSyntaxMessage(cmdidServer));
        return false;
    }
    const BOOL prompt = g_bCXPrompt;
    g_bCXPrompt = FALSE;
    ReconnectToServer(QString::fromUtf8(GetMyName()),
                      sourceTail(message, parse->nOffsets[1]).trimmed());
    g_bCXPrompt = prompt;
    return true;
}

bool CIrcProto::SlashAway(IRCPARSE* parse, const QString& message,
                          CDWordArray* formatting)
{
    if (!parse) return false;
    if (parse->nArgs > 1) {
        theApp.m_bAway = true;
        theApp.m_bAwayPrompt = true;
        bool converted = true;
        const QString away = controlFullTail(
            message, parse->nOffsets[1], formatting, &converted);
        if (!converted) return false;
        ChatSetAway(true, away);
    } else if (theApp.m_bAway) {
        theApp.m_bAway = false;
        theApp.m_bAwayPrompt = false;
        ChatSetAway(false, QString());
    }
    return true;
}

bool CIrcProto::ProcessSlashCommand(const QString& message,
                                    CDWordArray* formatting,
                                    unsigned short modes,
                                    bool invokedByWhisperBox)
{
    IRCPARSE parse;
    ParseIt(message, &parse, TRUE);
    if (parse.args.isEmpty()) return false;
    const QString commandText = parse.args.first();
    const SHORT command = NGetCmd(commandText.mid(1));

    if (command < 0 || (g_rgIrcCmd[command].uFlags & CMD_MUSTBECONNECTED)) {
        const int status = GetConnectionStatus();
        if (status != CX_NOCHANNEL && status != CX_INCHANNEL) {
            showOriginalMessage(IDS_MUSTBE_CONNECTED);
            return false;
        }
    }

    switch (command) {
    case cmdidAction:
    case cmdidMe:
    case cmdidThink:
        return SlashMeOrThink(command == cmdidAction ? cmdidMe
                                                      : enumCmdId(command),
                              &parse, message, formatting, modes,
                              invokedByWhisperBox);
    case cmdidAway:
        return SlashAway(&parse, message, formatting);
    case cmdidCreate:
        return SlashCreate(&parse);
    case cmdidJoin:
        return SlashJoin(&parse);
    case cmdidList:
        return SlashList(&parse, message);
    case cmdidMode:
        return SlashMode(&parse);
    case cmdidMsg:
    case cmdidPrivMsg:
        return SlashPrivMsg(&parse, message, formatting,
                            invokedByWhisperBox);
    case cmdidNick:
        return SlashNick(&parse);
    case cmdidPart:
        return SlashPart(&parse);
    case cmdidProp:
        return SlashProp(&parse);
    case cmdidQuote:
    case cmdidRaw: {
        const QByteArray bytes = message.toUtf8();
        qsizetype separator = bytes.indexOf(' ');
        if (separator < 0) return true;
        while (separator < bytes.size()
               && std::isspace(static_cast<unsigned char>(bytes.at(separator)))) {
            ++separator;
        }
        return SlashRaw(QString::fromUtf8(bytes.mid(separator)), &parse);
    }
    case cmdidServer:
        return SlashServer(&parse, message);
    case cmdidSound:
        return SlashSound(&parse, message, formatting);
    case cmdidWho:
        return SlashWho(message);
    case cmdidInvite:
    case cmdidIsOn:
    case cmdidKick:
    case cmdidKill:
    case cmdidNames:
    case cmdidTopic:
    case cmdidUserHost:
    case cmdidWhoIs:
        return SlashGeneric(enumCmdId(command), &parse, message, formatting);
    default:
        return SlashRaw(message.mid(1), &parse);
    }
}

bool bChatSendText(QString str, unsigned short modes, bool echo,
                   CDWordArray* formatting, const QString* encodedChannelName,
                   bool whispereesFilled, bool invokedByWhisperBox)
{
    const int lengthBeforeRightTrim = str.size();
    while (!str.isEmpty() && str.back().isSpace()) str.chop(1);
    if (str.isEmpty()) return true;

    CChatDoc* doc = GetChatDoc();
    CRoomInfo* proto = currentRoom;
    if (!proto && doc) proto = doc->m_proto;
    if (!proto) proto = GetDefaultProto();
    if (encodedChannelName && !encodedChannelName->isEmpty()) {
        if (!doc || !doc->m_proto
            || doc->m_proto->m_strChannel.compare(*encodedChannelName,
                                                   Qt::CaseInsensitive) != 0
            || doc->GetConnectionStatus() != CX_INCHANNEL) {
            return true;
        }
        proto = doc->m_proto;
    }
    if (!proto) return true;
    if (str.startsWith(QLatin1Char('/'))) {
        return proto->ProcessSlashCommand(str, formatting, modes,
                                          invokedByWhisperBox);
    }

    bool comicView = false;
    CUserInfo* puiSelf = nullptr;
    if (proto->m_doc) {
        doc = proto->m_doc;
        comicView = doc->m_bComicView;
        puiSelf = doc->m_puiSelf;
    }

    CDWordArray* formattingTmp = formatting;
    bool freeFormatting = false;
    int lengthAfter = str.size();
    if (lengthBeforeRightTrim != lengthAfter && formattingTmp) {
        formattingTmp = CutFormattingArray(CopyFormatting(formattingTmp),
                                            static_cast<SHORT>(lengthAfter));
        freeFormatting = true;
    }

    const int lengthBeforeLeftTrim = str.size();
    while (!str.isEmpty() && str.front().isSpace()) str.remove(0, 1);
    lengthAfter = str.size();
    if (lengthBeforeLeftTrim != lengthAfter && formattingTmp) {
        CDWordArray* pulled = PullFormattingOffsets(
            formattingTmp, static_cast<SHORT>(lengthBeforeLeftTrim - lengthAfter));
        if (freeFormatting) FreeAndNullFormatting(&formattingTmp);
        formattingTmp = pulled;
        freeFormatting = true;
    }

    QString echoText = str;
    QString controlFull = str;
    if (formattingTmp) {
        const QByteArray plain = str.toUtf8();
        char* full = SzControlFull(plain.constData(), formattingTmp);
        if (!full) {
            if (freeFormatting) FreeAndNullFormatting(&formattingTmp);
            return false;
        }
        controlFull = QString::fromUtf8(full);
        delete[] full;
    }

    const unsigned short echoModes = modes;
    if ((echoModes & BM_ACTION) && echo) {
        echoText.prepend(QString::fromUtf8(GetMyScreenName()) + QLatin1Char(' '));
        if (formattingTmp) {
            PushFormattingOffsets(formattingTmp,
                                  static_cast<SHORT>(std::strlen(GetMyName()) + 1));
        }
    }

    bool success = true;
    const int status = proto->GetConnectionStatus();
    if (status == CX_INCHANNEL || status == CX_NOCHANNEL) {
        if (puiSelf) {
            if (whispereesFilled) puiSelf->m_udi.m_talkTos = g_rgpuiWhisperees;
            else MListTalkTosToPuiself(puiSelf);
        }

        QString annotations;
        if (comicView && (g_bSendComicsData || proto->IsIRCX())) {
            bool useAnnotations = true;
            if ((modes & BM_WHISPER) && g_rgpuiWhisperees.size() == 1) {
                CUserInfo* pui = g_rgpuiWhisperees.first();
                useAnnotations = pui && !pui->IsExternal();
            }
            if (useAnnotations) {
                bInsertAnnotations(puiSelf, annotations, modes, !proto->IsIRCX());
            }
        }
        if ((!g_bSendComicsData || !comicView)
            && ((modes & BM_ACTION) || (modes & BM_THINK))) {
            ProcessNonComicsMsg(controlFull, modes);
        }

        if (!(modes & BM_WHISPER)) {
            if (!puiSelf || puiSelf->CheckFlag(UF_SPECTATOR)) {
                success = false;
            } else {
                QString nmText = formattingTmp ? str : QString();
                success = proto->bChatSendToChannel(
                    annotations, controlFull, formattingTmp ? &nmText : nullptr,
                    modes);
            }
        } else {
            bool justToMe = false;
            QString nmText = formattingTmp ? str : QString();
            success = proto->bSendWhispers(
                annotations, controlFull, formattingTmp ? &nmText : nullptr,
                modes, &justToMe);
            if (justToMe) echo = false;
        }
    }

    if ((proto->m_dwModes & CM_NOFORMAT) && modes != BM_WHISPER) echo = false;
    if (success && echo) {
        ShowSay(proto->m_doc, puiSelf, echoText, formattingTmp,
                static_cast<BYTE>(comicView), echoModes);
    }
    if (freeFormatting) FreeAndNullFormatting(&formattingTmp);
    return success;
}

void OnTextMsg(CChatDoc* doc, const QString& nickname, const QString& userIdent,
               const QString& message, unsigned char msgType,
               const QList<CUserInfo*>* talkTos)
{
    if (channelPrefix(nickname)) {
        return;
    }

    CUserInfo* pui = PuiFromDocNickIdent(
        &doc, nickname, userIdent, true, true);
    if (!pui) {
        return;
    }

    if (!message.startsWith(QLatin1Char('#')) || !ProcessComment(doc, pui, message, msgType)) {
        ProcessSay(doc, pui, message, msgType, talkTos);
    }
}

void OnDataMsg(CChatDoc* doc, const QString& nickname, const QString& userIdent,
               const QString& data, unsigned char msgType)
{
    if (channelPrefix(nickname) || data.isEmpty()) {
        return;
    }

    CUserInfo* pui = PuiFromDocNickIdent(
        &doc, nickname, userIdent, true, true);
    if (!pui) {
        return;
    }

    if (data.size() > 1 && data[1] == QLatin1Char(' ')) {
        ProcessComment(doc, pui, data, msgType);
    } else {
        ProcessUDIData(doc, pui, data);
    }
}

namespace {
struct KeyPairPosition {
    qsizetype key = -1;
    qsizetype value = -1;
    qsizetype end = -1;
    qsizetype next = -1;
};

bool findInKeyString(const QString& keyString, const QString* wanted,
                     KeyPairPosition* result)
{
    qsizetype position = 0;
    while (position < keyString.size()) {
        const qsizetype equals = keyString.indexOf(QLatin1Char('='), position);
        if (equals < 0) return false;

        qsizetype scan = equals + 1;
        if (scan < keyString.size()
            && keyString.at(scan) == QLatin1Char('"')) {
            const qsizetype closingQuote = keyString.indexOf(
                QLatin1Char('"'), scan + 1);
            scan = closingQuote < 0 ? keyString.size() : closingQuote + 1;
        }
        const qsizetype separator = keyString.indexOf(QLatin1Char(';'), scan);
        const qsizetype pairEnd = separator < 0 ? keyString.size() : separator;
        const qsizetype next = separator < 0 ? -1 : separator + 1;

        const bool matches = !wanted
            || (equals - position == wanted->size()
                && keyString.mid(position, wanted->size()) == *wanted);
        if (matches) {
            if (result) {
                result->key = position;
                result->value = equals + 1;
                result->end = pairEnd;
                result->next = next;
            }
            return true;
        }
        if (next < 0) break;
        position = next;
    }
    return false;
}
}

bool GetValueFromKeyString(const QString& keyString, const QString& key,
                           QString& value)
{
    if (key.isEmpty() || key.contains(QRegularExpression(
            QStringLiteral("[=;\"]")))) {
        return false;
    }
    KeyPairPosition pair;
    if (!findInKeyString(keyString, &key, &pair)) return false;
    value = keyString.mid(pair.value, pair.end - pair.value);
    while (value.endsWith(QLatin1Char(';'))) value.chop(1);
    if (value.size() >= 2 && value.front() == QLatin1Char('"')
        && value.back() == QLatin1Char('"')) {
        value = value.mid(1, value.size() - 2);
    }
    return true;
}

bool EnumKeyString(QString& remaining, QString& key, QString& value)
{
    KeyPairPosition pair;
    if (!findInKeyString(remaining, nullptr, &pair)) return false;
    const qsizetype equals = pair.value - 1;
    key = remaining.mid(pair.key, equals - pair.key);
    if (!GetValueFromKeyString(remaining, key, value)) return false;
    if (pair.next < 0) remaining.clear();
    else remaining.remove(0, pair.next);
    return true;
}

bool ChangeKeyString(QString& keyString, const QString& key,
                     const QString* value, int maximumSize)
{
    if (key.isEmpty() || key.contains(QRegularExpression(
            QStringLiteral("[=;\"]")))) {
        return false;
    }

    QString changed = keyString;
    KeyPairPosition pair;
    if (findInKeyString(changed, &key, &pair)) {
        if (pair.next < 0) {
            if (pair.key > 0) changed.truncate(pair.key - 1);
            else changed.clear();
        } else {
            changed.remove(pair.key, pair.next - pair.key);
        }
    }

    // Preserve the source's observable early-return behavior: its deletion
    // branch does not assign the modified temporary back to strKeyString.
    if (!value || value->isEmpty()) return true;
    if (value->contains(QLatin1Char('"'))) return false;

    QString pairText = key + QLatin1Char('=');
    if (value->contains(QLatin1Char('='))
        || value->contains(QLatin1Char(';'))) {
        pairText += QLatin1Char('"') + *value + QLatin1Char('"');
    } else {
        pairText += *value;
    }
    if (!changed.isEmpty()) pairText.prepend(QLatin1Char(';'));
    if ((changed + pairText).toUtf8().size() > maximumSize) return false;
    keyString = changed + pairText;
    return true;
}
