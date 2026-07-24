// Ported from v2.5-beta-1-modern/chat.h and chat.cpp.
// This Qt port keeps application-global names but replaces MFC CWinApp plumbing.

#pragma once

#include "chatprot.h"
#include "chatsrv.h"
#include "ccommon.h"
#include "defines.h"
#include "doskey.h"
#include "notif.h"
#include "rules.h"
#include "wincompat.h"

#include <QByteArray>
#include <QFont>
#include <QIcon>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>

class QApplication;
class CMainFrame;
class CChatDoc;
class CIrcProto;
class CRoomList;
class CUserList;
class CUserInfo;
class QTimer;
class QPrinter;
class QWidget;

class CMacro {
public:
    BOOL m_bDefined = FALSE;
    QString m_strName;
    QString m_strValue;

    void UnSerialize(const char* buffer);
    INT Serialize(char* buffer, INT bufferLength) const;
    void Invoke(const QString* encodedChannelName = nullptr,
                CUserInfo* pui = nullptr,
                BOOL invokedByRule = FALSE,
                BOOL inWhisperBox = FALSE);
};

class CChatApp {
public:
    bool m_bComicView = true;
    bool m_bShowMode = false;
    bool m_bShowArrivals = true;
    bool m_bAway = false;
    bool m_bAwayPrompt = false;
    bool m_bVIPMode = false;
    bool m_bAcceptWhispers = true;
    bool m_bAllowInvites = true;
    bool m_bPlaySounds = true;
    bool m_bSaveViewMode = true;
    bool m_bNoRefresh = false;
    bool m_bEmbedded = false;
    bool m_bPrompt = false;
    bool m_bIconMembers = true;
    bool m_bDoTest = false;
    bool m_bAutoDownloadAvatars = false;
    bool m_bAutoDownloadBackdrops = true;
    bool m_bFoundArt = false;
    bool m_bAllowFileTX = true;
    bool m_bNoMIDI = false;
    bool m_bAcceptNMCalls = true;
    bool m_bShowIdentity = true;
    bool m_bDisableMOTD = false;
    bool m_bLoadURL = false;
    bool m_bInSearch = false;
    bool m_bListRegistered = false;
    bool m_bLoginNotifsShown = false;
    bool m_bMainLoopReady = false;
    DWORD m_flags1 = ~DWORD{0};
    DWORD m_flags0 = 0;
    COLORREF m_comicsColor = RGB(0, 0, 0);
    COLORREF m_textColor = RGB(0, 0, 0);
    int m_iFontHeightBalloon = -240;
    int m_textSpacing = 0;
    int m_iHostHighlight = HH_BOLD_HEADERS | HH_BOLD_MESSAGES;
    int m_iGreetingType = AGT_NONE;
    UCHAR m_uFloodInterval = 8;
    UCHAR m_uFloodCount = 8;
    UCHAR m_uFloodFlags = FLOOD_IGNORE;
    BYTE m_charSet = ANSI_CHARSET;
    QFont m_comicsFont;
    QFont m_textFont;
    QList<QIcon> m_ImageList;
    QList<QIcon> m_StatusIcons;
    CHARFORMAT m_cfArray[NFONTS]{};
    bool m_bCfInitialized = false;
    bool m_bCfHLInitialized = false;
    int m_iShowBars = SB_TOOLBAR_ANY | SB_STATUSBAR;
    QByteArray m_pbCoolBarState;
    int m_iOnConnectAction = CA_JOINROOM;
    int m_iAutoPage = -1;
    int m_xFrame = 0;
    int m_yFrame = 0;
    int m_cxFrame = 0;
    int m_cyFrame = 0;
    int m_maxedFrame = 0;
    QString m_strConnectedService;
    QString m_strConnectedServer;
    QString m_myName;
    QString m_myNick;
    QString m_myIdent;
    QString m_myRealName;
    QString m_myChannel;
    QString m_strEmail;
    QString m_strHomePage;
    QString m_myProfile;
    QString m_strUserName;
    QString m_strGreetingMesg;
    QString m_myCharacterName;
    QString m_strAwayMessage;
    QString m_strFavoritesDir;
    QString m_strChatRooms;
    QString m_strFileTXDir;
    QString m_strBaseDir;
    QString m_strAvatarDir;
    QString m_strBackDropDir;
    QString m_strDefaultArtDir;
    QString m_soundPath;
    CMacro m_macros[NMACROS];
    QSet<QString> m_ignores;
    short m_nMyIdentLength = 0;
    QString m_lastBackDrop;
    QPointer<CMainFrame> m_pMainWnd;
    CChatDoc* m_pDoc = nullptr;
    CChatDoc* m_pExitingDoc = nullptr;
    CDosKey m_doskeyMain;
    CDosKey m_doskeyWhisper;
    QList<CRoomInfo*> m_enterInfos;
    CRoomList* m_pRoomList = nullptr;
    CUserList* m_pUserList = nullptr;
    QRect m_rectWhisper;
    QRect m_rectNotifs;
    CCRulesData m_rulesData;
    CCDynaRules m_dynaRules;
    CCDelayedRules m_delayedRules;
    CCDynaNotifs m_dynaNotifs;
    CChatServiceList m_listChatServices;
    CChatServiceConnector m_SrvConnector;

    int run(QApplication& app);
    void InitVals();
    BOOL LoadFromReg();
    BOOL SaveToReg(BOOL shortSave);
    void InitializeFonts();
    void InitializeComicsFonts();
    void InitStatusIcons();
    void SetStatusPaneString(int pane, const QString& text);
    void OnConnectError();
    void OnConnectConnected();
    void ContinueConnection();
    void StartConnectionTimer();
    void ResumeConnection();
    void CompleteConnection();
    void OnSessionConnect();
    void OnNewroom();
    void OnCreateroom();
    void OnDisconnect();
    void OnAwayToggle();
    void OnMotd();
    void OnChatroomList();
    void OnUserList();
    void OnViewLoginNotifs();
    void OnViewAutomations();
    void OnViewOptions();
    void OnViewTabbar();
    BOOL OnViewToolBar(UINT commandID);
    void OnViewStatuswindow();
    void OnDefineMacro();
    BOOL OnUpdateSessionConnect() const;
    BOOL OnUpdateNewroom() const;
    BOOL OnUpdateDisconnect() const;
    BOOL OnUpdateViewAutomations() const;
    BOOL OnUpdateViewOptions() const;
    BOOL OnUpdateCanSearch() const;
    BOOL OnUpdateAwayToggle(BOOL* checked = nullptr) const;
    BOOL OnUpdateViewTabbar(BOOL* checked = nullptr) const;
    BOOL OnUpdateViewToolBar(UINT commandID,
                             BOOL* checked = nullptr) const;
    BOOL OnUpdateMotd() const;
    BOOL OnUpdateViewStatuswindow(BOOL* checked = nullptr) const;
    BOOL OnUpdateViewLoginNotifs(BOOL* checked = nullptr) const;
    void OnAppAbout();
    void OnHelpFreestuff();
    void OnHelpProductnews();
    void OnHelpFaq();
    void OnHelpOnlineSupport();
    void OnHelpBestofWeb();
    void OnHelpSearchtheWeb();
    void OnHelpMsHomepage();
    void DoOptionsDialog(BOOL comicsView, UINT initialPageId = 0);
    bool ProcessShellCommand(const QString& argument, QString* fileName,
                             BOOL* fileNew);
    void ScheduleDocumentInitialize(CChatDoc* document);
    void OnFileOpen();
    CChatDoc* OpenDocumentFile(const QString& fileName);
    void SetPrinterResolution(QPrinter* printer);
    void OnFilePrintSetup(QPrinter* printer, QWidget* parent);
    BOOL StartDownloadingAvatar(CUserInfo* user, CChatDoc* document,
                                BOOL interactive);
    BOOL StartDownloadingBackdrop(const char* backdrop, const char* url);
    CRoomInfo* GetRoomInfoFromName(const QString& channelName,
                                   int* index = nullptr,
                                   bool cloneOK = false,
                                   bool nullOK = false);
    int AddRoomInfo(CRoomInfo* enterInfo);
    void RemoveRoomInfo(int index);
    void CleanRoomInfos();

    const QString& GetBaseDir() const { return m_strBaseDir; }
    const QString& GetAvatarDir() const { return m_strAvatarDir; }
    const QString& GetBackDropDir() const { return m_strBackDropDir; }

private:
    QTimer* m_connectTimer = nullptr;
    bool m_bDocumentInitializeActive = false;
};

extern CChatApp theApp;

void ChatPreSendText(QString& str, int avID = 0);
void GetVersionString(QString& version);
void OnChatroomListAux(const QString& query = QString());
void OnUserListAux(const QString& query = QString(),
                   const QString& encodedRoom = QString(),
                   const QString& prettyRoom = QString());
