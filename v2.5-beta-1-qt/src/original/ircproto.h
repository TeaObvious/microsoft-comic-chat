// Ported from v2.5-beta-1-modern/ircproto.h.
// Qt replaces Winsock sending; original method names are retained.

#pragma once

#include "chatprot.h"
#include "ircsock.h"
#include "query.h"
#include "wincompat.h"

#include <QString>

class CIrcSocket;
class CUserInfo;
class CRoomList;
class CUserList;

extern CIrcSocket serverConn;

constexpr int ENC_CHANNEL = 0;
constexpr int ENC_DBCS = 1;
constexpr int ENC_UTF8 = 2;
constexpr short g_nDefaultIOBuff = 512;
constexpr WORD g_wIgnoreIdent = 0x0001;
constexpr WORD g_wAutoIgnoreIdent = 0x0002;

inline constexpr char actionID[] = "\001ACTION";
inline constexpr char soundID[] = "\001SOUND";
inline constexpr char versionID[] = "\001VERSION";
inline constexpr char pingID[] = "\001PING";
inline constexpr char timeID[] = "\001TIME";
inline constexpr char emailID[] = "\001EMAIL";
inline constexpr char urlID[] = "\001URL";
inline constexpr char netMeetingID[] = "\001NETMEET";
inline constexpr char awayID[] = "\001AWAY";
inline constexpr char clientInfoID[] = "\001CLIENTINFO";
inline constexpr char fileDCCID[] = "\001DCC";
inline constexpr char xvchatID[] = "\001X-VCHAT";

inline constexpr short g_nActionLen = 7;
inline constexpr short g_nSoundLen = 6;
inline constexpr short g_nVersionLen = 8;
inline constexpr short g_nPingLen = 5;
inline constexpr short g_nTimeLen = 5;
inline constexpr short g_nEmailLen = 6;
inline constexpr short g_nUrlLen = 4;
inline constexpr short g_nNetMeetLen = 8;
inline constexpr short g_nAwayLen = 5;
inline constexpr short g_nClientInfoLen = 11;
inline constexpr short g_nFileDCCLen = 4;
inline constexpr short g_nHeresInfoLen = 12;
inline constexpr short g_nAppearsAsLen = 12;
inline constexpr short g_nXVChatLen = 8;
inline constexpr short g_nGetCharLen = 12;

bool bExtendedNickname(const QString& nickname);
QString EncodeNick(const QString& nickname, bool escapeWildcards = false);
QString DecodeNick(const QString& nickname);
QString DecodeNickForScreen(const QString& nickname);
QString EncodeChan(const QString& channel);
QString DecodeChan(const QString& channel, bool forceDbcs = false);
QString DecodeString(const QByteArray& string, int encoding);
void GetModeChars(DWORD flags, char* buffer);
void FixMICChannelName(CChatDoc* doc, CRoomInfo* enterRoom);
CRoomInfo* NewDefaultProto(CChatDoc* doc);
CIrcProto* GetIrcProto();
bool CommunicationInits();
void CommunicationCleanup();
void ChatFillRoomList(CRoomList* roomList);
void ChatFillUserList(CUserList* userList);
long GetMyIP();

class CIrcProto : public CRoomInfo {
public:
    CIrcProto();
    ~CIrcProto() override;

    bool ConnectToServer(const QString& server, const QString& nick,
                         const QString& realName, const QString& channel,
                         int onConnectAction = CA_JOINROOM);
    void Disconnect();

    void OnLogin();
    void ChatJoinChannel(CRoomInfo& enterInfo) override;
    void ChatJoinAux(CRoomInfo& enterInfo) override;
    void ChatCreateChannel(CRoomInfo& enterInfo) override;
    void ChatCreateAux(CRoomInfo& enterInfo) override;
    void ChatPartChannel(CChatDoc* doc, bool checkRules) override;
    void ChatSendSay(const QString& text) override;
    void ChatSendAction(const QString& text);
    bool bChatSendToTarget(const QString& addressee, const QString& annotations,
                           const QString& message, unsigned short modes = 0,
                           bool asNotice = false);
    bool bChatSendToChannel(const QString& annotations, const QString& message,
                            QString* nmText = nullptr, unsigned short modes = 0) override;
    bool bChatSendPrivMesg(const QString& addressee, const QString& annotations,
                           const QString& message, QString* nmText = nullptr,
                           bool asNotice = false, unsigned short modes = 0) override;
    void TryNewNick(int messageId, const QString& showNick = QString(),
                    BOOL registerNick = TRUE,
                    QString* newNick = nullptr);
    bool ChatChangeNick(const QString& newNick);
    bool ChatKickUser(const QString& nickname,
                      const QString& reason) override;
    void ChatKickUser(CUserInfo* pui) override;
    void ChatBanUser(CUserInfo* pui) override;
    bool ChatBanUser(const QString& banPattern, BOOL ban,
                     const QString& encodedChannel = QString()) override;
    bool ChatSendInvitation(const QString& nickname) override;
    void ChatSetNick(const QString& nickname);
    bool ChatSetClientData(const QString& clientData);
    bool ChatSetTopic(const QString& topic) override;
    bool ChatSetMode(DWORD newMode, DWORD newMaxUsers,
                     const QString& newPassword) override;
    bool bChatShowMOTD() override;
    int GetType() const override { return PC_IRC; }
    void HandleClientDataChange(const QString& newClientData);
    bool ChangeProperty(CUserInfo* puiSelf, const QString& property,
                        const QString* value) override;
    void ChatSetAway(bool away, const QString& message,
                     CUserInfo* pui = nullptr,
                     bool protoNotify = true) override;
    void DoIgnoreUser(CUserInfo* pui, bool ignore,
                      bool autoIgnore = false,
                      const QString& nickname = QString()) override;
    void ChatGetIdentity(CUserInfo* pui,
                         const QString& nickname = QString()) override;
    bool bExecuteQuery(enumQueryPurpose qp, enumCommandType ct, enumDataType dt, void* pvData,
                       const QString& channelName, const QString& nicknameMask);
    void SetVisibility(bool visible);
    bool bRegisterMode(const QString& message) override;
    bool ProcessSlashCommand(const QString& message, CDWordArray* formatting,
                             unsigned short modes,
                             bool invokedByWhisperBox = false) override;
    bool SlashGeneric(enumCmdId command, IRCPARSE* parse,
                      const QString& message, CDWordArray* formatting);
    bool SlashMode(IRCPARSE* parse);
    bool SlashProp(IRCPARSE* parse);
    bool SlashJoin(IRCPARSE* parse);
    bool SlashCreate(IRCPARSE* parse);
    bool SlashPart(IRCPARSE* parse);
    bool SlashNick(IRCPARSE* parse);
    bool SlashPrivMsg(IRCPARSE* parse, const QString& message,
                      CDWordArray* formatting, bool invokedByWhisperBox);
    bool SlashMeOrThink(enumCmdId command, IRCPARSE* parse,
                        const QString& message, CDWordArray* formatting,
                        unsigned short modes, bool invokedByWhisperBox);
    bool SlashSound(IRCPARSE* parse, const QString& message,
                    CDWordArray* formatting);
    bool SlashList(IRCPARSE* parse, const QString& message);
    bool SlashWho(const QString& message);
    bool SlashServer(IRCPARSE* parse, const QString& message);
    bool SlashAway(IRCPARSE* parse, const QString& message,
                   CDWordArray* formatting);
    SYNTAX GetSyntaxFromCmdId(enumCmdId command,
                              unsigned short* index = nullptr) const;
    QString StrSyntaxMessage(enumCmdId command) const;
    QString StrEncodeCommandParam(DWORD argumentType, int* encoding,
                                  QString parameter) const;

    void SendMessageText(const QString& raw) override;
    virtual void SendMessageBytes(const QByteArray& raw);
    QString GetMyNickName() const;
    int EncodingType() const;
    QString EncodeString(const QString& string,
                         int encoding = ENC_CHANNEL) const;
    QByteArray EncodeStringBytes(const QString& string,
                                 int encoding = ENC_CHANNEL) const;
    bool IsIRCX() const override
    {
        return m_pSock && m_pSock->m_bIrcXServer;
    }
    void SetConnectionStatus(ConnectionStatus status) override;
    ConnectionStatus GetConnectionStatus() const override;

    CIrcSocket* m_pSock = nullptr;
    bool m_bInRoom = false;
    QString m_strClientData;
};
