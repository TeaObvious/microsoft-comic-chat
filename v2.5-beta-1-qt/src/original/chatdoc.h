// Ported from v2.5-beta-1-modern/chatdoc.h.
// MFC CDocument is replaced by a small Qt-side document/state object.

#pragma once

#include "chatprot.h"
#include "wincompat.h"

#include <QList>
#include <QMap>
#include <QString>

class CBodyCam;
class CChatView;
class CMemberList;
class CPageView;
class CPage;
class CTextView;
class CDWordArray;
class CIrcProto;
class CUserInfo;
class QMenu;
class HistoryEntry;
class QTextStream;
class QWidget;

class CChatDoc {
public:
    bool m_bComicView = true;
    bool m_bStatusView = false;
    bool m_bIconMembers = false;
    bool m_bLastMemberView = false;
    bool m_bNewContent = false;
    bool m_bArchived = false;
    bool m_bObscured = false;
    CIrcProto* m_proto = nullptr;
    CUserInfo* m_puiSelf = nullptr;
    CChatView* m_client = nullptr;
    CPageView* m_view = nullptr;
    CTextView* m_textView = nullptr;
    QWidget* m_sayWnd = nullptr;
    CMemberList* m_memberList = nullptr;
    CBodyCam* m_bodyCam = nullptr;
    unsigned short m_myAvatarID = 0;
    unsigned int m_myBackDropID = 0;
    QList<CPage*> m_pages;
    QList<CUserInfo*> m_allChannelPuis;
    QMap<QString, CUserInfo*> m_mapNickToPtr;
    QList<HistoryEntry*> m_history;
    QString m_strStatus;

    CChatDoc();
    ~CChatDoc();

    ConnectionStatus GetConnectionStatus() const;
    void SetComicsTitle(const QString& title);
    QString GetComicsTitle();
    void SetTitle(const QString& title);
    const QString& GetTitle() const { return m_title; }
    void SaveConnectStatus(const QString& status);
    void ResetStatus(bool left = true, bool right = false);
    void RegisterNewContent() { m_bNewContent = true; }
    void SetModifiedFlag(bool modified) { m_bModified = modified; }
    bool IsModified() const { return m_bModified; }
    void InitHistory();
    void InitMyDocument();
    void LoadDocData();
    void ExecuteHistory(int mode);
    void DestroyHistory();
    void ChatSaveConversation(QTextStream& stream) const;
    bool ChatLoadConversation(QTextStream& stream);
    void AddLine(UINT avatarId, const char* text, USHORT modes,
                 CDWordArray* formatting = nullptr);
    void ProcessLine(UINT avatarId, const char* text, USHORT modes,
                     BYTE cooked, CDWordArray* formatting = nullptr);
    void TallySpeech(UINT avatarId);
    void ShowInfo(CUserInfo* pui, const QString& info,
                  bool onlyInComics = false, char hotLinkChar = 0);
    void AddNewPage();
    void DestroyPages();
    void SetBackDropID(UINT id) { m_myBackDropID = id; }
    UINT GetBackDropID() const { return m_myBackDropID; }
    void DestroyUserState();
    void UpdateAdminMenu() {}
    void SetFocusToSayWnd();
    QWidget* GetComponentWindow(UINT component) const;
    void CycleFocus(UINT currentFocus, bool backward);
    void OnViewIcon();
    void OnViewListAux();
    void OnViewText();
    void OnClearHistory();
    void OnSetfont();
    CUserInfo* GetNextSelectedMember(int& index) const;
    int SelectedMemberCount() const;
    CUserInfo* GetSingleSelectedMember() const;
    void OnMemberGetinfo();
    void OnMemberIgnore();
    void OnAddToNotifs();
    void OnGetidentity();
    void OnGetComicCharacter();
    void OnGetVersion();
    void OnPingUser();
    void OnGetLocaltime();
    void OnWhisperboxMlist();
    void OnSendEmail();
    void OnVisitHomepage();
    void OnAdministratorKick();
    void OnAdminBan();
    void OnInvite();
    void OnMakeadmin();
    void OnMakespeaker();
    void OnMakespectator();
    void OnChannelprops();
    void OnLeave();
    void OnMacro(UINT commandID);
    BOOL OnUpdateMacro(UINT commandID) const;
    void UpdateMacroMenu();
    void UpdateComicCharacterMenu(QMenu* menu = nullptr);

private:
    QString m_comicsTitle;
    QString m_title;
    bool m_bModified = false;
    bool m_bDocumentInitialized = false;
};

CChatDoc* GetChatDoc();
void SetChatDoc(CChatDoc* doc);
CChatDoc* LookupDoc(const QString& channel);
extern QList<CChatDoc*> g_docs;
