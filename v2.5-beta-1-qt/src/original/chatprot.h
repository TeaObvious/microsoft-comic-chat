// Ported from v2.5-beta-1-modern/chatprot.h.
// MFC document/window pointers are replaced with Qt-side forward declarations.

#pragma once

#include "defines.h"
#include "format.h"

#include <QList>
#include <QString>

class CChatDoc;
class CUserInfo;
struct IRCPARSE;

using ConnectionStatus = int;

class CRoomInfo {
public:
    virtual ~CRoomInfo()
    {
        FreeAndNullFormatting(&m_prgdwTopicFormatting);
    }

    QString m_strChannel;
    QString m_strPrettyChannel;
    QString m_strPassword;
    QString m_strCreationModes;
    QString m_strTopic;
    CDWordArray* m_prgdwTopicFormatting = nullptr;
    unsigned long m_dwModes = 0;
    unsigned long m_dwMaxUsers = 0;
    bool m_bSetMode = false;
    CChatDoc* m_doc = nullptr;

    virtual void ChatJoinChannel(CRoomInfo&) {}
    virtual void ChatJoinAux(CRoomInfo&) {}
    virtual void ChatCreateChannel(CRoomInfo&) {}
    virtual void ChatCreateAux(CRoomInfo&) {}
    virtual void ChatPartChannel(CChatDoc*, bool) {}
    virtual void SendMessageText(const QString&) {}
    virtual void ChatSendSay(const QString&) {}
    virtual bool bChatSendToChannel(const QString&, const QString&, QString* = nullptr,
                                    unsigned short = 0) { return false; }
    virtual bool bChatSendPrivMesg(const QString&, const QString&, const QString&,
                                   QString* = nullptr, bool = false,
                                   unsigned short = 0) { return false; }
    virtual bool bSendWhispers(const QString& annotations, const QString& message,
                               QString* nmText = nullptr,
                               unsigned short modes = BM_WHISPER,
                               bool* justToMe = nullptr);
    virtual void ChatAnnounceNewAvatar(const QString& avatarName, const QString& url,
                                       const QString& addressee = QString(),
                                       bool force = false);
    virtual void ChatGetInfo(CUserInfo* pui);
    virtual void ChatGetIdentity(CUserInfo*, const QString& = QString()) {}
    virtual bool ChatKickUser(const QString&, const QString&) { return false; }
    virtual void ChatKickUser(CUserInfo*) {}
    virtual void ChatBanUser(CUserInfo*) {}
    virtual bool ChatBanUser(const QString&, BOOL,
                             const QString& = QString()) { return false; }
    virtual bool ChatSendInvitation(const QString&) { return false; }
    virtual void ChatGetAvatarInfo(CUserInfo* pui, bool interactive);
    virtual void ChatSyncBackDrop(CChatDoc* document, const QString& backdrop,
                                  const QString& url = QString());
    virtual void ChatSetAway(bool away, const QString& message,
                             CUserInfo* pui = nullptr,
                             bool protoNotify = true);
    virtual void ReplyVersion(CUserInfo* pui);
    virtual void ReplyPing(CUserInfo* pui, const QString& message);
    virtual void ReplyTime(CUserInfo* pui);
    virtual void ReplyEmail(CUserInfo* pui);
    virtual void ReplyHomePage(CUserInfo* pui);
    virtual void OnPropertyChange(const QString& property,
                                  const QString* value);
    virtual bool ChangeProperty(CUserInfo*, const QString&,
                                const QString*) { return false; }
    virtual void ChatGetVersion(CUserInfo* pui);
    virtual void ChatPingUser(CUserInfo* pui);
    virtual void ChatGetLocalTime(CUserInfo* pui);
    virtual void ChatGetEmail(CUserInfo* pui);
    virtual void ChatGetHomePage(CUserInfo* pui);
    virtual bool ChatSetTopic(const QString&) { return false; }
    virtual bool ChatSetMode(DWORD, DWORD, const QString&) { return false; }
    virtual bool bChatShowMOTD() { return false; }
    virtual void DoChannelDialog();
    virtual void DoKickDlg(const QString& nick, const QString& banPattern);
    virtual void ChatSetOperator(CUserInfo* pui, int mode);
    virtual void ChatInvite();
    virtual void DoIgnoreUser(CUserInfo* pui, bool ignore,
                              bool autoIgnore = false,
                              const QString& nickname = QString());
    virtual bool bRegisterMode(const QString&) { return false; }
    virtual bool SlashRaw(const QString& message, IRCPARSE* parse);
    virtual bool ProcessSlashCommand(const QString& message,
                                     CDWordArray* formatting,
                                     unsigned short modes,
                                     bool invokedByWhisperBox = false);
    virtual bool IsIRCX() const { return false; }
    virtual void UpdateStatus();
    virtual void SetConnectionStatus(ConnectionStatus status) { m_status = status; }
    virtual ConnectionStatus GetConnectionStatus() const { return m_status; }

protected:
    ConnectionStatus m_status = CX_DISCONNECTED;
};
