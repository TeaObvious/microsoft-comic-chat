// Ported from v2.5-beta-1-modern/userinfo.h.

#pragma once

#include "defines.h"
#include "wincompat.h"

#include <QList>
#include <QString>

#define UF_IGNORED      1
#define UF_COMICUSER    2
#define UF_DEPARTED     4
#define UF_OPERATOR     8
#define UF_EXTERNAL     16
#define UF_SPECTATOR    32
#define UF_REQUESTPING  64
#define UF_HASVOICE     128
#define UF_AWAY         256
#define UF_SCREENNAME   512
#define UF_OWNER        1024
#define UF_AUTODOWNLOAD 16384
#define UF_INTERACTIVEDOWNLOAD 32768

#define RF_PROFILE     0x000F
#define RF_TIME        0x0030
#define RF_EMAIL       0x00C0
#define RF_HOMEPAGE    0x0300
#define RF_NETMEETING  0x0C00
#define RF_VERSION     0x3000

#define PROFILECREDITS 3

class CUserInfo;
class CChatDoc;

class CUserDisplayInfo {
public:
    void Reset()
    {
        m_chGest = -1;
        m_chExpr = -1;
        m_chGestE = 0;
        m_chGestI = 0;
        m_chExprE = 0;
        m_chExprI = 0;
        m_bbCooked = 0;
        m_bbReq = 0;
        m_uModes = BM_SAY;
        m_talkTos.clear();
    }

    signed char m_chGest = -1;
    signed char m_chExpr = -1;
    signed char m_chGestE = 0;
    signed char m_chGestI = 0;
    signed char m_chExprE = 0;
    signed char m_chExprI = 0;
    unsigned char m_bbCooked = 0;
    unsigned char m_bbReq = 0;
    unsigned short m_uModes = BM_SAY;
    QList<CUserInfo*> m_talkTos;
};

class CUserInfo {
public:
    CUserInfo();
    explicit CUserInfo(const QString& attedNick, const QString& fullName = QString());

    QString& GetName() { return m_strName; }
    QString& GetFullName() { return m_fullName; }
    QString& GetScreenName();
    const QString GetQualifiedName() const;
    void SetName(const QString& nick);
    void SetScreenName(const QString& name);
    void SetFullName(const QString& fullName) { m_fullName = fullName; }

    bool Ignored() const { return m_flags & UF_IGNORED; }
    bool IsComicUser() const { return m_flags & UF_COMICUSER; }
    bool IsDeparted() const { return m_flags & UF_DEPARTED; }
    bool IsOperator() const { return m_flags & UF_OPERATOR; }
    bool IsOwner() const { return m_flags & UF_OWNER; }
    bool IsExternal() const { return m_flags & UF_EXTERNAL; }
    bool NeedsDownload() const { return m_flags & (UF_AUTODOWNLOAD | UF_INTERACTIVEDOWNLOAD); }
    bool IsDownloadInteractive() const { return m_flags & UF_INTERACTIVEDOWNLOAD; }
    bool IsSpectator() const { return m_flags & UF_SPECTATOR; }
    bool IsSpeaker() const { return !(m_flags & UF_OPERATOR) && !(m_flags & UF_SPECTATOR); }
    bool CheckFlag(unsigned short flag) const { return m_flags & flag; }
    bool IsRequestInfo(unsigned short request) const { return m_uRequests & request; }
    bool IsSelf() const;
    bool IsFlooding();
    bool MatchesNickMask(const QString& mask) const { return m_fullName == mask; }

    void Ignore(bool value) { setFlag(UF_IGNORED, value); }
    void ComicUser(bool value) { setFlag(UF_COMICUSER, value); }
    void SetDeparted(bool value) { setFlag(UF_DEPARTED, value); }
    void SetOperator(bool value) { setFlag(UF_OPERATOR, value); }
    void SetOwner(bool value) { setFlag(UF_OWNER, value); }
    void SetExternal(bool value) { setFlag(UF_EXTERNAL, value); }
    void SetFlag(unsigned short flag, bool value) { setFlag(flag, value); }
    void GetAttedNick(QString& attedNick, bool screen = true) const;
    void ClearTalkTos() { m_udi.m_talkTos.clear(); }
    void SelectInMemberList(CUserInfo* addressee, BOOL select = TRUE,
                            BOOL extend = FALSE);
    void IncrementRequestInfo(unsigned short request);
    void DecrementRequestInfo(unsigned short request);

    unsigned short GetAvatarID() const { return m_avatarID; }
    void SetAvatarID(unsigned short avID) { m_avatarID = avID; }
    void SetAvatarRealInfo(const QString& name, const QString& url)
    {
        m_strAvatarRealName = name;
        m_strAvatarRealURL = url;
    }
    const QString& GetAvatarRealName() const { return m_strAvatarRealName; }
    const QString& GetAvatarRealURL() const { return m_strAvatarRealURL; }
    bool IsAvatarReal() const { return m_strAvatarRealName.isEmpty(); }

    unsigned int m_nSends = 0;
    unsigned char m_bbValidUDI = 0;
    CUserDisplayInfo m_udi;

private:
    void setFlag(unsigned short flag, bool value);

    QString m_strName;
    QString m_fullName;
    QString m_strScreenName;
    QString m_strAvatarRealName;
    QString m_strAvatarRealURL;
    unsigned short m_flags = 0;
    unsigned short m_uRequests = 0;
    unsigned short m_avatarID = 0;
    unsigned short m_uIntervalStart = 0;
    unsigned char m_uMsgCount = 0;
};

extern CUserInfo* g_puiSelf;
USHORT ExtractAvatarID(void* userInfo);
void SetMyPUIAvatarID(UINT avatarID);
void SetUserAvatarID(CUserInfo* pui, unsigned short avatarID);
void SetUserAvatarRealInfo(CUserInfo* pui, const QString& name,
                           const QString& url, CChatDoc* document,
                           BOOL live = TRUE);
void MListTalkTosToPuiself(CUserInfo* puiSelf);
