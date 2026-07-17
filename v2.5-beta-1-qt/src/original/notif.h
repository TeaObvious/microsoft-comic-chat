//=--------------------------------------------------------------------------=
// Notif.H -- Qt port of v2.5-beta-1-modern/notif.h
//=--------------------------------------------------------------------------=
// Notification definitions and WHO-derived state stay in the original
// module. QVector and QTimer replace only MFC containers and window timers.

#pragma once

#include "rules.h"

#include <QString>
#include <QVector>

class QTimer;

constexpr UINT g_uMaxNetArgLength = 100;
constexpr UINT g_uMaxNotifKeyLength = 20;
constexpr UINT g_uMaxNotifParamLength = 32;
constexpr UINT g_uMaxSerializedNotif =
    12 + 3 * g_uMaxNotifParamLength + g_uMaxNetArgLength;

inline constexpr char g_szNotificationsSubKey[] = "\\Notifications";
inline constexpr char g_szNotificationFlags[] = "NotificationFlags";
inline constexpr char g_szNotificationsClass[] = "Notifications Data";

constexpr UCHAR g_uNotifParamNum = 4;
constexpr UCHAR g_uNickname = 0;
constexpr UCHAR g_uUserName = 1;
constexpr UCHAR g_uHostName = 2;
constexpr UCHAR g_uNetName = 3;

constexpr UCHAR g_uAny = 0;
constexpr UCHAR g_uEquals = 1;
constexpr UCHAR g_uContains = 2;
constexpr UCHAR g_uStartsWith = 3;
constexpr UCHAR g_uEndsWith = 4;

constexpr UINT g_uNotifsDaemonTimer = 83;
constexpr UINT g_uNotifsDaemonNoElapse = 0;
constexpr UINT g_uNotifsDaemonShortElapse = 10;
constexpr UINT g_uNotifsDaemonLongElapse = 150;

constexpr WORD g_wVisible = 0x0001;
constexpr WORD g_wConnected = 0x0002;
constexpr WORD g_wNew = 0x0004;
constexpr WORD g_wAltered = 0x1000;

class CCNotif;
class CCDynaNotifs;

using DISPLAY_NOTIFICATIONS_FN = BOOL (*)(CCDynaNotifs*);
using SIGNAL_NEW_UPDATE_FN = BOOL (*)(CCDynaNotifs*);
using NOTIFDAEMON_QUERY_FN = BOOL (*)(CCNotif*);
using NETVALID_FN = BOOL (*)(QString);

class CCNotif {
    friend class CCDynaNotifs;

public:
    CCNotif();
    explicit CCNotif(CCNotif* notif);
    ~CCNotif();

    BOOL operator==(const CCNotif& notif) const;

    void AddRef();
    void Release();
    void CopyNotif(CCNotif* notif);

    void Activate() { m_wFlags |= g_wActive; }
    void Desactivate() { m_wFlags &= ~g_wActive; }
    BOOL bActive() const { return m_wFlags & g_wActive; }
    WORD wGetFlags() const { return m_wFlags; }
    void SetFlags(WORD flags) { m_wFlags = flags; }

    BOOL bDaemonNeeded() const;
    CCDaemonExt* GetDaemonExt() const { return m_pDaemonExt; }
    void SetDaemonExt(CCDaemonExt* daemonExt) { m_pDaemonExt = daemonExt; }

    INT Serialize(char* buffer, INT bufferLength) const;
    INT UnSerialize(const BYTE* buffer, INT bufferLength);

    // These declarations exist in the authoritative header but have no
    // definition in its notif.cpp. They remain intentionally unimplemented.
    BOOL bValidateNotif(UINT index, QString& parameter, UINT* errorID);
    BOOL bUpdateDaemonExt(BOOL resetUserLists);

    void SetParam(UCHAR parameter, const QString& value)
    {
        if (parameter < g_uNotifParamNum) m_strParams[parameter] = value;
    }
    void SetOperator(UCHAR parameter, UCHAR op)
    {
        if (parameter < g_uNotifParamNum - 1) m_uOperators[parameter] = op;
    }
    QString GetParam(UCHAR parameter) const
    {
        return parameter < g_uNotifParamNum ? m_strParams[parameter]
                                             : QString();
    }
    UCHAR GetOperator(UCHAR parameter) const
    {
        return parameter < g_uNotifParamNum - 1 ? m_uOperators[parameter]
                                                 : g_uAny;
    }

    static NETVALID_FN m_pfNetValid;

private:
    QString m_strParams[g_uNotifParamNum];
    UCHAR m_uOperators[g_uNotifParamNum - 1]{g_uAny, g_uAny, g_uAny};
    SHORT m_nRefCount = 1;
    WORD m_wFlags = 0;
    CCDaemonExt* m_pDaemonExt = nullptr;
};

class CCDynaNotifs {
    friend class CCDaemonExt;

public:
    CCDynaNotifs();
    ~CCDynaNotifs();

    const CCDynaNotifs& operator=(const CCDynaNotifs& dynaNotifs);

    QVector<CCNotif*>& GetNotifsArray() { return m_rgpNotifs; }
    const QVector<CCNotif*>& GetNotifsArray() const { return m_rgpNotifs; }
    CCItemPtrArray* GetNotifUsersArray() { return &m_rgpNotifUsers; }
    const CCItemPtrArray* GetNotifUsersArray() const { return &m_rgpNotifUsers; }

    void ResetModifiedUsersCount() { m_uModifiedUsersCount = 0; }
    UINT GetModifiedUsersCount() const { return m_uModifiedUsersCount; }
    WORD GetFlags() const { return m_wFlags; }
    void SetFlags(WORD flags) { m_wFlags = flags; }
    void AddFlag(WORD flag) { m_wFlags |= flag; }
    QString& GetStartUpIdent() { return m_strStartUpIdent; }
    void SetStartUpIdent(const QString& identity) { m_strStartUpIdent = identity; }
    void DecrementUpdateCount() { --m_uUpdateCount; }
    UINT GetUpdateCount() const { return m_uUpdateCount; }
    void DecrementWhosCount() { --m_uWhosCount; }
    UINT GetWhosCount() const { return m_uWhosCount; }

    void SetDisplayNotificationsFunction(DISPLAY_NOTIFICATIONS_FN function)
    {
        m_pfDisplayNotifications = function;
    }
    void SetSignalNewUpdateFunction(SIGNAL_NEW_UPDATE_FN function)
    {
        m_pfSignalNewUpdate = function;
    }
    void SetDaemonQueryFunction(NOTIFDAEMON_QUERY_FN function)
    {
        m_pfDaemonQuery = function;
    }

    // Declared, but not defined, by v2.5-beta-1-modern/notif.cpp.
    QString StrGetOperatorDisplay(UCHAR op);

    INT iFindUserIndex(CUser* user) const;
    BOOL bNotifExists(CCNotif* notif) const;
    BOOL bAddNotif(CCNotif* notif, INT index = -1);
    BOOL bRemoveNotif(CCNotif* notif, INT index = -1);
    BOOL bSortNotifs();
    BOOL bRemoveAllUsers();
    BOOL bRemoveUsersWithoutFlag(WORD flag);
    BOOL bRemoveFlagsFromAllUsers(WORD flags);
    BOOL bUpdateNotifs();

    BOOL bSaveNotifsToReg();
    BOOL bLoadNotifsFromReg();

    BOOL bUpdateNotifsDaemonExt(BOOL resetUserLists);
    BOOL bStartNotifsDaemon(UINT notifsDaemonElapse, BOOL forceReset);
    BOOL bStopNotifsDaemon();
    BOOL bDaemonNeeded() const;
    BOOL bAddNotificationUser(CUser* user);
    BOOL bModifyNotificationUser(CUser* user, WORD addFlags,
                                 WORD removeFlags, INT index = -1);
    void OnNotifsDaemonTimer();

private:
    void CleanUpNotifsArray();

    BOOL m_bDaemonRunning = FALSE;
    WORD m_wFlags = 0;
    UINT m_uModifiedUsersCount = 0;
    UINT m_uWhosCount = 0;
    UINT m_uUpdateCount = 0;
    QVector<CCNotif*> m_rgpNotifs;
    CCItemPtrArray m_rgpNotifUsers{itUser};
    QString m_strStartUpIdent;
    DISPLAY_NOTIFICATIONS_FN m_pfDisplayNotifications = nullptr;
    SIGNAL_NEW_UPDATE_FN m_pfSignalNewUpdate = nullptr;
    NOTIFDAEMON_QUERY_FN m_pfDaemonQuery = nullptr;
    QTimer* m_notifsDaemonTimer = nullptr;
};
