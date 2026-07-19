// Ported from v2.5-beta-1-modern/userinfo.cpp.

#include "userinfo.h"

#include "chat.h"
#include "chatdoc.h"
#include "avatar.h"
#include "histent.h"
#include "ircproto.h"
#include "memblst.h"
#include "protsupp.h"

#include <QItemSelectionModel>
#include <QListWidget>

#include <ctime>

CUserInfo* g_puiSelf = nullptr;

USHORT ExtractAvatarID(void* userInfo)
{
    auto* pui = static_cast<CUserInfo*>(userInfo);
    return pui ? pui->GetAvatarID() : 0;
}

void SetMyPUIAvatarID(UINT avatarID)
{
    if (!g_puiSelf) return;
    g_puiSelf->SetAvatarID(static_cast<USHORT>(avatarID));
    if (CAvatarX* avatar = GetAvatar(static_cast<USHORT>(avatarID)))
        avatar->m_userInfo = g_puiSelf;
}

void SetUserAvatarID(CUserInfo* pui, unsigned short avatarID)
{
    if (!pui) return;
    pui->SetAvatarID(avatarID);
    if (CAvatarX* avatar = GetAvatar(avatarID)) avatar->m_userInfo = pui;
}

void SetUserAvatarRealInfo(CUserInfo* pui, const QString& name,
                           const QString& url, CChatDoc* document, BOOL live)
{
    if (!pui) return;
    if (name.isEmpty() || url.isEmpty()) {
        pui->SetAvatarRealInfo(QString(), QString());
        pui->SetFlag(UF_AUTODOWNLOAD | UF_INTERACTIVEDOWNLOAD, false);
        return;
    }

    CAvatarX* avatar = GetAvatar(pui->GetAvatarID());
    const QString loadedName = avatar && avatar->OriginalName()
        ? QString::fromUtf8(avatar->OriginalName()) : QString();
    if (!loadedName.isEmpty()
        && loadedName.compare(name, Qt::CaseInsensitive) == 0) {
        pui->SetAvatarRealInfo(QString(), QString());
        pui->SetFlag(UF_AUTODOWNLOAD | UF_INTERACTIVEDOWNLOAD, false);
        return;
    }

    const bool repeated = pui->GetAvatarRealName().compare(
        name, Qt::CaseInsensitive) == 0;
    pui->SetAvatarRealInfo(name, url);
    // The modern source's downloader boundary is disabled. Preserve the
    // source-visible informational entry, but do not create a Qt downloader.
    if (!repeated && live)
        AddAndExecute(new ComicCharacterEntry(pui), document);
}

CUserInfo::CUserInfo() = default;

CUserInfo::CUserInfo(const QString& attedNick, const QString& fullName)
{
    m_udi.Reset();
    QString nickname = attedNick;
    if (!nickname.isEmpty()) {
        const QChar prefix = nickname.front();
        if (prefix == QLatin1Char(SC_HOST) || prefix == QLatin1Char(SC_OWNER)) {
            nickname.remove(0, 1);
            SetOperator(true);
            if (prefix == QLatin1Char(SC_OWNER)) {
                SetOwner(true);
            }
        } else if (prefix == QLatin1Char(SC_SPECTATOR)) {
            nickname.remove(0, 1);
            SetFlag(UF_SPECTATOR, true);
        } else if (prefix == QLatin1Char(SC_HASVOICE)) {
            nickname.remove(0, 1);
            SetFlag(UF_HASVOICE, true);
        }
    }
    SetName(nickname);
    m_fullName = fullName;
}

QString& CUserInfo::GetScreenName()
{
    return (m_flags & UF_SCREENNAME) ? m_strScreenName : m_strName;
}

const QString CUserInfo::GetQualifiedName() const
{
    const QString nick = (m_flags & UF_SCREENNAME)
        ? m_strScreenName : m_strName;
    return theApp.m_bShowIdentity && !m_fullName.isEmpty()
        ? QStringLiteral("%1 (%2)").arg(nick, m_fullName) : nick;
}

void CUserInfo::SetName(const QString& nick)
{
    m_strName = nick;
    if (nick.startsWith(QLatin1Char('\''))) {
        SetScreenName(DecodeNickForScreen(nick));
    } else {
        SetScreenName(QString());
    }
}

void CUserInfo::SetScreenName(const QString& name)
{
    if (name.isEmpty()) {
        setFlag(UF_SCREENNAME, false);
        m_strScreenName.clear();
        return;
    }
    m_strScreenName = name;
    setFlag(UF_SCREENNAME, true);
}

bool CUserInfo::IsSelf() const
{
    return !theApp.m_myNick.isEmpty() && m_strName.compare(theApp.m_myNick, Qt::CaseInsensitive) == 0;
}

bool CUserInfo::IsFlooding()
{
    if (!(theApp.m_uFloodFlags & FLOOD_IGNORE) || this == g_puiSelf) {
        return false;
    }

    const USHORT now = static_cast<USHORT>(std::time(nullptr) & 0xffff);
    const USHORT interval = static_cast<USHORT>(
        qAbs(static_cast<int>(now) - static_cast<int>(m_uIntervalStart)));
    if (interval > theApp.m_uFloodInterval) {
        m_uIntervalStart = now;
        m_uMsgCount = 1;
        return false;
    }
    if (++m_uMsgCount < theApp.m_uFloodCount) {
        return false;
    }

    if (CRoomInfo* protocol = GetDefaultProto()) {
        protocol->DoIgnoreUser(this, true, true);
    }
    return true;
}

void CUserInfo::GetAttedNick(QString& attedNick, bool screen) const
{
    if (IsOperator()) {
        attedNick = QStringLiteral("@");
    } else if (IsSpectator()) {
        attedNick = QStringLiteral(">");
    } else {
        attedNick.clear();
    }
    attedNick += screen ? ((m_flags & UF_SCREENNAME) ? m_strScreenName : m_strName) : m_strName;
}

void CUserInfo::setFlag(unsigned short flag, bool value)
{
    m_flags = value ? static_cast<unsigned short>(m_flags | flag)
                    : static_cast<unsigned short>(m_flags & ~flag);
}

void CUserInfo::IncrementRequestInfo(unsigned short request)
{
    const unsigned short credits = m_uRequests & request;
    if (request == RF_PROFILE) {
        if (credits <= 12) m_uRequests = static_cast<unsigned short>(m_uRequests + PROFILECREDITS);
        else m_uRequests |= 0x000F;
        return;
    }

    unsigned short increment = 0;
    switch (request) {
    case RF_TIME: increment = 0x0010; break;
    case RF_EMAIL: increment = 0x0040; break;
    case RF_HOMEPAGE: increment = 0x0100; break;
    case RF_NETMEETING: increment = 0x0400; break;
    case RF_VERSION: increment = 0x1000; break;
    default: return;
    }
    if (credits < request) m_uRequests = static_cast<unsigned short>(m_uRequests + increment);
    else m_uRequests |= request;
}

void CUserInfo::DecrementRequestInfo(unsigned short request)
{
    if (!(m_uRequests & request)) return;
    unsigned short decrement = 0;
    switch (request) {
    case RF_PROFILE: decrement = 0x0001; break;
    case RF_TIME: decrement = 0x0010; break;
    case RF_EMAIL: decrement = 0x0040; break;
    case RF_HOMEPAGE: decrement = 0x0100; break;
    case RF_NETMEETING: decrement = 0x0400; break;
    case RF_VERSION: decrement = 0x1000; break;
    default: return;
    }
    m_uRequests = static_cast<unsigned short>(m_uRequests - decrement);
}

void CUserInfo::SelectInMemberList(CUserInfo* addressee, BOOL select,
                                   BOOL extend)
{
    CChatDoc* document = GetChatDoc();
    CMemberList* members = document ? document->m_memberList : nullptr;
    if (!members || !members->m_list) return;
    if (!extend) {
        members->m_list->clearSelection();
        members->m_list->setCurrentItem(nullptr);
    }
    if (!addressee) return;
    const INT index = FindMemberListIndex(addressee, document);
    if (index < 0) return;
    QListWidgetItem* item = members->m_list->item(index);
    if (!item) return;
    item->setSelected(select);
    if (select) members->m_list->setCurrentItem(
        item, QItemSelectionModel::NoUpdate);
    else if (members->m_list->currentItem() == item)
        members->m_list->setCurrentItem(nullptr);
}

void MListTalkTosToPuiself(CUserInfo* puiSelf)
{
    if (!puiSelf) {
        return;
    }
    puiSelf->ClearTalkTos();

    CChatDoc* document = GetChatDoc();
    if (!document || !document->m_memberList) {
        return;
    }
    const QList<CUserInfo*> selected = document->m_memberList->selectedUsers();
    for (CUserInfo* pui : selected) {
        if (pui && !pui->IsSelf()) {
            puiSelf->m_udi.m_talkTos.append(pui);
        }
    }
}
