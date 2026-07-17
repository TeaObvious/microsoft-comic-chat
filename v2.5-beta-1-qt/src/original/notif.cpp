//=--------------------------------------------------------------------------=
// Notif.Cpp -- Qt port of v2.5-beta-1-modern/notif.cpp
//=--------------------------------------------------------------------------=

#include "notif.h"

#include "actions.h"
#include "originalassets.h"
#include "originalsettings.h"
#include "userlist.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include <QVariant>

#include <array>
#include <cstring>

namespace {
QString anyParameter()
{
    return originalResourceString(IDS_KEY_EVENT_PARAM0 + kepAny);
}

bool sameUserAndRoom(const CUser* left, const CUser* right)
{
    return left && right
        && left->m_strNickname == right->m_strNickname
        && left->m_strIdentity == right->m_strIdentity
        && left->m_strPrettyRoom == right->m_strPrettyRoom;
}
}

NETVALID_FN CCNotif::m_pfNetValid = bNetValid;

CCNotif::CCNotif() = default;

CCNotif::CCNotif(CCNotif* notif)
{
    if (!notif) return;
    m_wFlags = notif->m_wFlags;
    if ((m_pDaemonExt = notif->m_pDaemonExt)) m_pDaemonExt->AddRef();
    CopyNotif(notif);
}

CCNotif::~CCNotif()
{
    if (m_pDaemonExt) m_pDaemonExt->Release();
}

BOOL CCNotif::operator==(const CCNotif& notif) const
{
    for (UCHAR parameter = g_uNickname; parameter <= g_uHostName;
         ++parameter) {
        if (m_uOperators[parameter] != notif.m_uOperators[parameter])
            return FALSE;
    }
    for (UCHAR parameter = g_uNickname; parameter <= g_uNetName;
         ++parameter) {
        if (m_strParams[parameter].compare(
                notif.m_strParams[parameter], Qt::CaseInsensitive) != 0) {
            return FALSE;
        }
    }
    // The authoritative source performs this network comparison twice.
    if (m_strParams[g_uNetName].compare(
            notif.m_strParams[g_uNetName], Qt::CaseInsensitive) != 0) {
        return FALSE;
    }
    return TRUE;
}

void CCNotif::CopyNotif(CCNotif* notif)
{
    if (!notif) return;
    m_wFlags = notif->m_wFlags;
    for (UCHAR parameter = g_uNickname; parameter <= g_uHostName;
         ++parameter) {
        m_strParams[parameter] = notif->m_strParams[parameter];
        m_uOperators[parameter] = notif->m_uOperators[parameter];
    }
    m_strParams[g_uNetName] = notif->m_strParams[g_uNetName];
}

void CCNotif::AddRef()
{
    ++m_nRefCount;
}

void CCNotif::Release()
{
    if (m_nRefCount > 0 && --m_nRefCount == 0) delete this;
}

INT CCNotif::Serialize(char* buffer, INT bufferLength) const
{
    if (!buffer || bufferLength <= 0) return -1;

    QByteArray parameters[g_uNotifParamNum];
    UINT lengths[g_uNotifParamNum]{};
    INT total = g_uNotifParamNum + 4;

    for (UCHAR parameter = g_uNickname; parameter <= g_uHostName;
         ++parameter) {
        if (m_uOperators[parameter] == g_uAny) continue;
        parameters[parameter] = m_strParams[parameter].toLatin1();
        lengths[parameter] = static_cast<UINT>(parameters[parameter].size() + 1);
        total += static_cast<INT>(lengths[parameter]);
    }

    const QString any = anyParameter();
    if (m_strParams[g_uNetName].compare(any, Qt::CaseInsensitive) == 0) {
        lengths[g_uNetName] = 1;
    } else {
        parameters[g_uNetName] = m_strParams[g_uNetName].toLatin1();
        lengths[g_uNetName] =
            static_cast<UINT>(parameters[g_uNetName].size() + 1);
    }
    total += static_cast<INT>(lengths[g_uNetName]);

    if (total > bufferLength) return -1;

    char* cursor = buffer;
    *cursor++ = static_cast<char>(g_wVersion);
    *cursor++ = static_cast<char>(m_wFlags & 0xff);
    *cursor++ = static_cast<char>((m_wFlags >> 8) & 0xff);
    *cursor++ = static_cast<char>(g_uNotifParamNum - 1);
    *cursor++ = 1;

    for (UCHAR parameter = g_uNickname; parameter <= g_uHostName;
         ++parameter) {
        *cursor++ = static_cast<char>(m_uOperators[parameter]);
        if (m_uOperators[parameter] != g_uAny) {
            std::memcpy(cursor, parameters[parameter].constData(),
                        parameters[parameter].size());
            cursor += parameters[parameter].size();
            *cursor++ = '\0';
        }
    }

    if (lengths[g_uNetName] == 1) {
        *cursor++ = '\0';
    } else {
        std::memcpy(cursor, parameters[g_uNetName].constData(),
                    parameters[g_uNetName].size());
        cursor += parameters[g_uNetName].size();
        *cursor++ = '\0';
    }
    return static_cast<INT>(cursor - buffer);
}

INT CCNotif::UnSerialize(const BYTE* buffer, INT bufferLength)
{
    if (!buffer || bufferLength < 5) goto failure;

    {
        const BYTE* cursor = buffer;
        INT left = bufferLength;

        if (*cursor != static_cast<BYTE>(g_wVersion)) goto failure;
        ++cursor;
        --left;

        if (left < static_cast<INT>(sizeof(WORD))) goto failure;
        m_wFlags = static_cast<WORD>(cursor[0])
            | (static_cast<WORD>(cursor[1]) << 8);
        if (m_wFlags & ~g_wActive) goto failure;
        cursor += sizeof(WORD);
        left -= sizeof(WORD);

        if (left < 2 || *cursor++ != g_uNotifParamNum - 1) goto failure;
        --left;
        if (*cursor++ != 1) goto failure;
        --left;

        for (UCHAR parameter = g_uNickname; parameter <= g_uHostName;
             ++parameter) {
            if (left < 1) goto failure;
            m_uOperators[parameter] = *cursor++;
            --left;
            if (m_uOperators[parameter] > g_uEndsWith) goto failure;
            if (m_uOperators[parameter] == g_uAny) continue;

            INT length = 0;
            while (length < left
                   && length < static_cast<INT>(g_uMaxNotifParamLength)
                   && cursor[length] != 0) {
                ++length;
            }
            if (length >= left || cursor[length] != 0) goto failure;
            m_strParams[parameter] = QString::fromLatin1(
                reinterpret_cast<const char*>(cursor), length);
            cursor += length + 1;
            left -= length + 1;
        }

        INT length = 0;
        while (length < left
               && length < static_cast<INT>(g_uMaxNetArgLength)
               && cursor[length] != 0) {
            ++length;
        }
        if (length >= left || cursor[length] != 0) goto failure;
        if (length == 0) {
            m_strParams[g_uNetName] = anyParameter();
        } else {
            m_strParams[g_uNetName] = QString::fromLatin1(
                reinterpret_cast<const char*>(cursor), length);
        }
        left -= length + 1;

        bUpdateDaemonExt(TRUE);
        return bufferLength - left;
    }

failure:
    m_wFlags = 0;
    return 0;
}

BOOL CCNotif::bUpdateDaemonExt(BOOL resetItemLists)
{
    if (bActive()) {
        if (m_pDaemonExt) {
            if (resetItemLists || m_pDaemonExt->m_bResetItemLists) {
                m_pDaemonExt->m_bResetItemLists = FALSE;
                resetItemLists = TRUE;
                m_pDaemonExt->bCleanUpItemLists();
            }
        } else {
            m_pDaemonExt = new CCDaemonExt(itUser);
            resetItemLists = TRUE;
        }
        if (m_pDaemonExt && resetItemLists
            && !m_pDaemonExt->bAllocNewItemList(2)) {
            m_pDaemonExt->Release();
            m_pDaemonExt = nullptr;
            return FALSE;
        }
    } else if (m_pDaemonExt) {
        m_pDaemonExt->Release();
        m_pDaemonExt = nullptr;
    }
    return TRUE;
}

BOOL CCNotif::bDaemonNeeded() const
{
    if (!bActive()) return FALSE;
    const QString any = anyParameter();
    if (m_strParams[g_uNetName].compare(any, Qt::CaseInsensitive) == 0)
        return TRUE;
    return m_pfNetValid && m_pfNetValid(m_strParams[g_uNetName]);
}

CCDynaNotifs::CCDynaNotifs() = default;

CCDynaNotifs::~CCDynaNotifs()
{
    bStopNotifsDaemon();
    delete m_notifsDaemonTimer;
    m_notifsDaemonTimer = nullptr;
    CleanUpNotifsArray();
    bRemoveAllUsers();
}

const CCDynaNotifs& CCDynaNotifs::operator=(const CCDynaNotifs& dynaNotifs)
{
    if (this == &dynaNotifs) return *this;
    CleanUpNotifsArray();
    for (CCNotif* notif : dynaNotifs.m_rgpNotifs) {
        if (notif) m_rgpNotifs.append(new CCNotif(notif));
    }
    m_pfDisplayNotifications = dynaNotifs.m_pfDisplayNotifications;
    m_pfSignalNewUpdate = dynaNotifs.m_pfSignalNewUpdate;
    m_pfDaemonQuery = dynaNotifs.m_pfDaemonQuery;
    m_bDaemonRunning = dynaNotifs.m_bDaemonRunning;
    m_wFlags = dynaNotifs.m_wFlags;
    m_strStartUpIdent = dynaNotifs.m_strStartUpIdent;
    // The source intentionally does not inherit counts or current users.
    return *this;
}

void CCDynaNotifs::CleanUpNotifsArray()
{
    for (CCNotif* notif : m_rgpNotifs) {
        if (notif) notif->Release();
    }
    m_rgpNotifs.clear();
}

BOOL CCDynaNotifs::bRemoveAllUsers()
{
    m_rgpNotifUsers.FreeRemoveAll();
    return TRUE;
}

BOOL CCDynaNotifs::bRemoveUsersWithoutFlag(WORD flag)
{
    INT users = m_rgpNotifUsers.m_items.size();
    for (INT index = 0; index < users; ++index) {
        CUser* user = static_cast<CUser*>(m_rgpNotifUsers.m_items.at(index));
        if (!(user->GetFlags() & flag)) {
            m_rgpNotifUsers.m_items.removeAt(index);
            user->Release();
            --users;
        }
    }
    return TRUE;
}

BOOL CCDynaNotifs::bRemoveFlagsFromAllUsers(WORD flags)
{
    for (void* item : m_rgpNotifUsers.m_items) {
        CUser* user = static_cast<CUser*>(item);
        user->SetFlags(user->GetFlags() & ~flags);
    }
    return TRUE;
}

BOOL CCDynaNotifs::bUpdateNotifs()
{
    if (m_uWhosCount == 0) {
        bUpdateNotifsDaemonExt(TRUE);
        bRemoveAllUsers();
        if (bDaemonNeeded()) OnNotifsDaemonTimer();
        return TRUE;
    }
    ++m_uUpdateCount;
    return FALSE;
}

BOOL CCDynaNotifs::bUpdateNotifsDaemonExt(BOOL resetItemLists)
{
    BOOL result = TRUE;
    for (CCNotif* notif : m_rgpNotifs) {
        if (notif) result &= notif->bUpdateDaemonExt(resetItemLists);
    }
    return result;
}

BOOL CCDynaNotifs::bNotifExists(CCNotif* notif) const
{
    if (!notif) return FALSE;
    for (CCNotif* current : m_rgpNotifs) {
        if (current && *current == *notif) return TRUE;
    }
    return FALSE;
}

BOOL CCDynaNotifs::bAddNotif(CCNotif* notif, INT index)
{
    if (!notif) return FALSE;
    if (index < 0 || index >= m_rgpNotifs.size())
        m_rgpNotifs.append(notif);
    else
        m_rgpNotifs.insert(index, notif);
    return TRUE;
}

BOOL CCDynaNotifs::bRemoveNotif(CCNotif* notif, INT index)
{
    INT found = index;
    if (found < 0) found = m_rgpNotifs.indexOf(notif);
    if (found < 0 || found >= m_rgpNotifs.size()) return FALSE;
    CCNotif* removed = m_rgpNotifs.at(found);
    if (!removed) return FALSE;
    removed->Desactivate();
    removed->Release();
    m_rgpNotifs.removeAt(found);
    return TRUE;
}

BOOL CCDynaNotifs::bSortNotifs()
{
    const UCHAR sortColumn = static_cast<UCHAR>(m_wFlags >> 12);
    if (sortColumn >= g_uNotifParamNum) return FALSE;
    const BOOL ascending = !(m_wFlags & g_wSortDescending);
    const QVector<CCNotif*> saved = m_rgpNotifs;
    m_rgpNotifs.clear();
    for (CCNotif* notif : saved) {
        INT index = 0;
        for (; index < m_rgpNotifs.size(); ++index) {
            CCNotif* other = m_rgpNotifs.at(index);
            const INT order = notif->GetParam(sortColumn).compare(
                other->GetParam(sortColumn), Qt::CaseInsensitive);
            if ((order <= 0 && ascending) || (order >= 0 && !ascending))
                break;
        }
        m_rgpNotifs.insert(index, notif);
    }
    return TRUE;
}

BOOL CCDynaNotifs::bSaveNotifsToReg()
{
    QSettings settings = originalUserSettings();
    settings.beginGroup(originalSettingsSubKey(g_szNotificationsSubKey));

    const INT notificationCount = m_rgpNotifs.size();
    const INT previousValueCount = settings.childKeys().size();
    for (INT index = notificationCount; index < previousValueCount; ++index) {
        settings.remove(QString::number(index));
    }

    settings.setValue(QString::fromLatin1(g_szNotificationFlags),
                      static_cast<quint32>(MAKELONG(m_wFlags, g_wVersion)));

    std::array<char, g_uMaxSerializedNotif> buffer{};
    for (INT index = 0; index < notificationCount; ++index) {
        CCNotif* notification = m_rgpNotifs.at(index);
        if (!notification) continue;
        const INT length = notification->Serialize(buffer.data(), buffer.size());
        if (length > 0) {
            settings.setValue(QString::number(index),
                              QByteArray(buffer.data(), length));
        }
    }

    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

BOOL CCDynaNotifs::bLoadNotifsFromReg()
{
    QSettings settings = originalUserSettings();
    settings.beginGroup(originalSettingsSubKey(g_szNotificationsSubKey));
    const QStringList valueNames = settings.childKeys();
    if (valueNames.isEmpty()) {
        settings.endGroup();
        // RegOpenKeyEx failure is deliberately reported as success by source.
        return TRUE;
    }

    CCNotif* pendingNotification = nullptr;
    for (const QString& valueName : valueNames) {
        const QVariant stored = settings.value(valueName);
        if (valueName == QString::fromLatin1(g_szNotificationFlags)) {
            bool ok = false;
            const DWORD flags = stored.toUInt(&ok);
            m_wFlags = ok && HIWORD(flags) == g_wVersion
                ? LOWORD(flags)
                : 0;
            continue;
        }

        if (stored.metaType().id() != QMetaType::QByteArray) continue;
        const QByteArray bytes = stored.toByteArray();
        if (bytes.size() > static_cast<qsizetype>(g_uMaxSerializedNotif)) {
            continue;
        }
        if (!pendingNotification) pendingNotification = new CCNotif;
        if (pendingNotification->UnSerialize(
                reinterpret_cast<const BYTE*>(bytes.constData()),
                bytes.size()) == bytes.size()) {
            bAddNotif(pendingNotification);
            pendingNotification = nullptr;
        }
    }

    if (pendingNotification) pendingNotification->Release();
    settings.endGroup();
    return TRUE;
}

BOOL CCDynaNotifs::bDaemonNeeded() const
{
    for (CCNotif* notif : m_rgpNotifs) {
        if (notif && notif->bDaemonNeeded()) return TRUE;
    }
    return FALSE;
}

BOOL CCDynaNotifs::bStartNotifsDaemon(UINT elapsed, BOOL forceReset)
{
    if (m_bDaemonRunning && !forceReset) return TRUE;
    if (!QCoreApplication::instance()) return FALSE;
    bStopNotifsDaemon();
    if (!m_notifsDaemonTimer) {
        m_notifsDaemonTimer = new QTimer;
        QObject::connect(m_notifsDaemonTimer, &QTimer::timeout,
                         [this] { OnNotifsDaemonTimer(); });
    }
    m_notifsDaemonTimer->setInterval(
        static_cast<int>(elapsed) * 1000 + 60);
    m_notifsDaemonTimer->start();
    m_bDaemonRunning = m_notifsDaemonTimer->isActive();
    return m_bDaemonRunning;
}

BOOL CCDynaNotifs::bStopNotifsDaemon()
{
    if (m_notifsDaemonTimer) m_notifsDaemonTimer->stop();
    m_bDaemonRunning = FALSE;
    return TRUE;
}

void CCDynaNotifs::OnNotifsDaemonTimer()
{
    if (!m_bDaemonRunning) return;
    bStartNotifsDaemon(g_uNotifsDaemonLongElapse, TRUE);
    if (m_uWhosCount != 0) return;

    for (CCNotif* notif : m_rgpNotifs) {
        if (!notif || !notif->bDaemonNeeded() || !notif->m_pDaemonExt)
            continue;
        const BOOL passes = TRUE;
        if (passes && m_pfDaemonQuery && m_pfDaemonQuery(notif))
            ++m_uWhosCount;
    }
}

INT CCDynaNotifs::iFindUserIndex(CUser* user) const
{
    if (!user) return -1;
    for (INT index = 0; index < m_rgpNotifUsers.m_items.size(); ++index) {
        CUser* current = static_cast<CUser*>(
            m_rgpNotifUsers.m_items.at(index));
        if (user->m_strNickname == current->m_strNickname
            && user->m_strIdentity == current->m_strIdentity) {
            return index;
        }
    }
    return -1;
}

BOOL CCDynaNotifs::bAddNotificationUser(CUser* user)
{
    if (!user) return FALSE;
    const INT index = iFindUserIndex(user);
    if (index >= 0)
        return bModifyNotificationUser(user, g_wConnected, 0, index);

    m_rgpNotifUsers.m_items.append(user);
    user->SetFlags(g_wVisible | g_wConnected | g_wNew | g_wAltered);
    user->AddRef();
    ++m_uModifiedUsersCount;
    return TRUE;
}

BOOL CCDynaNotifs::bModifyNotificationUser(CUser* user, WORD addFlags,
                                            WORD removeFlags, INT index)
{
    if (!user) return FALSE;
    INT found = index;
    if (found < 0) {
        found = iFindUserIndex(user);
        if (found < 0) return TRUE;
    }
    if (found >= m_rgpNotifUsers.m_items.size()) return FALSE;

    CUser* stored = static_cast<CUser*>(m_rgpNotifUsers.m_items.at(found));
    WORD flags = stored->GetFlags();
    const BOOL alreadyAltered = flags & g_wAltered;
    flags |= addFlags | g_wVisible | g_wNew | g_wAltered;
    flags &= ~removeFlags;
    stored->SetFlags(flags);

    if (flags & g_wConnected) {
        stored->m_strPrettyRoom = user->m_strPrettyRoom;
        stored->m_strRoom = user->m_strRoom;
    } else {
        stored->m_strPrettyRoom.clear();
        stored->m_strRoom.clear();
    }

    m_rgpNotifUsers.m_items.removeAt(found);
    m_rgpNotifUsers.m_items.append(stored);
    if (!alreadyAltered) ++m_uModifiedUsersCount;
    return TRUE;
}
