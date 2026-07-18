// Ported from v2.5-beta-1-modern/protsupp.h.

#pragma once

#include "chatprot.h"
#include "format.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class CChatDoc;
class CUserInfo;
class CRoom;
class CUser;
struct IRCPARSE;

extern QMap<QString, CUserInfo*>* g_mapNickToPtr;
extern QList<CUserInfo*> g_rgpuiWhisperees;
extern CRoomInfo g_enterInfo;
extern CRoomInfo* currentRoom;
extern SHORT g_nCXKeepServer;
extern BOOL g_bCXPrompt;
extern BOOL g_bEnterOnCreate;
extern BOOL g_bCanViewUnrated;

BOOL ReplaceToken(QString& value, const QString& token,
                  const QString& replacement);
BOOL bReplaceMacroTokens(QString& message, BOOL in);
bool ToggleSendComicsData();
void SetSendComicsData(bool sendComicsData);
bool GetSendComicsData();
BOOL bCanViewUnrated(BOOL promptOverride = FALSE);
BOOL bPassesRatings(const QString& rating,
                    BOOL promptOverride = FALSE);
void ListMembers(const QString& room, const QString& prettyRoom);
void StartRoomList();
void EndRoomList();
void EndUserList();
void AddToRoomList(CRoom* room, BOOL addIt = TRUE);
void AddToUserList(CUser* user);
CUser* CreateUserFromWhoReply(IRCPARSE* parse);
bool bCanDance();
CRoomInfo* GetDefaultProto();

unsigned char IndexToByte(unsigned char byteIn);
unsigned char ByteToIndex(unsigned char byteIn);
unsigned short SM2BM(unsigned char byteMode);
unsigned char BM2SM(unsigned short modes);

bool bForEachWord(const QString& line, bool (*pfn)(const QString&, void*, unsigned long),
                  void* clientData, unsigned long data, const QString& separators,
                  bool doubleQuotes = false);
bool bCanInvite();
bool bDoInvite(const QString& invitee, void* protocol, unsigned long data);
void DoBanDlg(const QString& encodedChannelName, const QString& ban,
              QStringList& banArray);
void OnInvite(const QString& sender, const QString& fullName,
              const QString& room);
void AcknowledgeInvite(const QString& nickname, const QString& room);
void ChatEmptyMemberList(CChatDoc* doc = nullptr);
int AddToImageList(CUserInfo* pui);
void AddToMembersList(CUserInfo* pui, CChatDoc* doc = nullptr);
void CIUserPart(const QString& nickname, CChatDoc* doc = nullptr);
void ProcessNick(CChatDoc* doc, const QString& oldNick, const QString& newNick,
                 bool updateMemberList = true);
void ReinstallPui(CUserInfo* pui, const QString& newNick);
void ChatChangeAdmin(CChatDoc* doc, const QString& nickname, int setModes,
                     int unsetModes);
void UpdateIgnoreOnEntry(const QString& room, const QString& nick,
                         const QString& user, const QString& host);
void AddIgnore(const QString& nickMask);
void RemoveIgnore(const QString& nickMask);
bool IsIgnored(const QString& nickMask);
void IgnoreUser(const QString& nick, const QString& nickMask,
                bool ignore, bool autoIgnore);
void DoUserAway(CChatDoc* doc, CUserInfo* pui, bool away);
void ShowAway(CUserInfo* pui, QString awayMessage, CChatDoc* doc);
void ShowIdentity(const QString& nick, const QString& user,
                  const QString& host);
void ShowVersion(CUserInfo* pui, QString message);
void ShowPing(CUserInfo* pui, const QString& message);
void ShowTime(CUserInfo* pui, QString message);
void ShowEmail(CUserInfo* pui, QString address);
void ShowHomePage(CUserInfo* pui, QString url);
QString PrepareTextAction(CUserInfo* pui, const QString& message,
                          unsigned short& modes);
QString PrepareComicsAction(CUserInfo* pui, const QString& message);
QString PrepareSound(CUserInfo* pui, const QString& message,
                     unsigned short& modes);
void IdentifyWhispers(CChatDoc* doc, CUserInfo* pui, unsigned char msgType,
                      unsigned short& modes,
                      const QList<CUserInfo*>* talkTos = nullptr);
bool AcceptWhispers();
bool ExpandVariables(QString& message, CChatDoc* doc,
                     CUserInfo* pui = nullptr,
                     bool invokedByRule = false);
void AutoGreet(const QString& nickname);
void AssignArbitraryAvatar(CUserInfo* pui);
CUserInfo* LookupPui(const QString& nickname, CChatDoc* doc = nullptr);
CUserInfo* ExternalPui(const QString& nickname, const QString& fullName,
                       bool addIfNotThere = false);
void DestroyExternalUserInfos();
CUserInfo* PuiFromDocNickIdent(CChatDoc** doc, const QString& nickname,
                               const QString& userIdent,
                               bool skipObscuredChannels,
                               bool addExternalIfNotThere);
void GetTalkTos(CChatDoc* doc, QList<CUserInfo*>* talkTos,
                const QString& names);
QString GetAddressees(CUserInfo* pui, const QString& separator,
                      bool useNick);
CUserInfo* CIUserJoin(CUserInfo* pui);
bool bSingleJoin(const QString& attedNick, void* doc, unsigned long data);
void ProcessBeginEnumeration();
void ProcessEndEnumeration(CChatDoc* doc = nullptr);
bool bProcessAddChannel(const QString& channelName, CRoomInfo* proto,
                        SHORT* keepServer = &g_nCXKeepServer,
                        BOOL* prompt = &g_bCXPrompt);
void InitializeChannelConnection(CRoomInfo& enterInfo, SHORT* keepServer,
                                 BOOL* prompt, BOOL createRoom);
bool bInitEnterInfo(CRoomInfo& enterInfo, const QString& channelName,
                    const QString& channelPassword,
                    const QString& creationModes, DWORD maxUsers,
                    BOOL encodeChannel);
void ChatSwitchChannel(const QString& channelName = QString());
void ChatCreateRoom(CRoomInfo& enterInfo);
void ShowBadChannelName(const QString& channelName);
void OnBadChannelPassword(CRoomInfo& enterInfo);
bool bSwitchToRoom(const QString& newRoom = QString(),
                   const QString& password = QString(),
                   const QString& creationModes = QString(),
                   DWORD maxUsers = 0L, BOOL encodeChannel = FALSE,
                   BOOL createRoomInfo = FALSE,
                   BOOL createRoom = FALSE);
void ConfirmAway(const QString* conditionallyOnText = nullptr);
void ChatServerDisconnect(BOOL bCheckRules = FALSE,
                          BOOL bResumeConnection = FALSE);
BOOL bChatServerConnect(const QString& server);
void ReconnectToServer(const QString& decodedNickname,
                       const QString& serverName);
void GotPartChannel(CChatDoc* doc);
bool bChatSendText(QString str, unsigned short modes, bool echo = true,
                   CDWordArray* formatting = nullptr,
                   const QString* encodedChannelName = nullptr,
                   bool whispereesFilled = false,
                   bool invokedByWhisperBox = false);
void OnTextMsg(CChatDoc* doc, const QString& nickname, const QString& userIdent,
               const QString& message, unsigned char msgType = 0,
               const QList<CUserInfo*>* talkTos = nullptr);
void OnDataMsg(CChatDoc* doc, const QString& nickname, const QString& userIdent,
               const QString& data, unsigned char msgType);
void ProcessSay(CChatDoc* doc, CUserInfo* pui, QString message,
                unsigned char msgType,
                const QList<CUserInfo*>* talkTos = nullptr);
void OnKick(CChatDoc* doc, const QString& kicker, const QString& kickee,
            const QString& message);
void ShowSay(CChatDoc* document, CUserInfo* pui, const QString& text,
             CDWordArray* formatting, BYTE cooked, unsigned short modes);
void UpdateTitle(CChatDoc* doc = nullptr);
bool ChangeKeyString(QString& keyString, const QString& key,
                     const QString* value, int maximumSize);
bool GetValueFromKeyString(const QString& keyString, const QString& key,
                           QString& value);
bool EnumKeyString(QString& remaining, QString& key, QString& value);
