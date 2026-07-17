//=--------------------------------------------------------------------------=
// Rules.H -- Qt port of v2.5-beta-1-modern/rules.h
//=--------------------------------------------------------------------------=
// The rule model, names and observable ordering remain in this original
// module. Qt containers and timers replace only MFC/Win32 infrastructure.

#pragma once

#include "ccomp.h"
#include "defines.h"
#include "format.h"
#include "query.h"
#include "resource.h"
#include "wincompat.h"

#include <QList>
#include <QString>
#include <QVector>

class QTimer;

constexpr UINT g_uErrVersion = 1;
constexpr UINT g_uErrFormat = g_uErrVersion + 1;
constexpr UINT g_uErrRulesSkipped = g_uErrFormat + 1;
constexpr UINT g_uErrOOM = g_uErrRulesSkipped + 1;
constexpr UINT g_uErrFlooding = 0;

constexpr UINT g_uMaxEventParams = 3;
constexpr UINT g_uMaxActionParams = 3;
constexpr UINT g_uMaxSetNameLength = 19;
constexpr UINT g_uMaxParamLength = 128;
constexpr UINT g_uMaxShortParamLength = 14;
constexpr UINT g_uRuleFixedPrefix = 17;
constexpr UINT g_uMaxSerializedRule =
    g_uRuleFixedPrefix
    + (g_uMaxParamLength + 1)
        * (g_uMaxEventParams
           + g_uMaxActionParams * (MAX_FORMATTINGPERBYTE + 1))
    + 9;

constexpr UCHAR g_uDefRuleFloodOcc = 12;
constexpr UCHAR g_uDefRuleFloodInt = 4;

constexpr WORD g_wDoNotDisplay = 0x0001;
constexpr WORD g_wHighlight = 0x0002;
constexpr WORD g_wReplace = 0x0004;

constexpr UINT g_uRulesDaemonTimer = 82;
constexpr UINT g_uRulesDaemonShortElapse = 12;
constexpr UINT g_uRulesDaemonLongElapse = 150;
constexpr UINT g_uDelayedRulesTimer = 84;
constexpr UINT g_uDelayedRulesElapse = 1;

inline constexpr char g_szRuleSetsSubKey[] = "\\RuleSets";
inline constexpr char g_szRuleSetFlags[] = "RuleSetFlags";
inline constexpr char g_szRuleSetsClass[] = "Rule Sets Data";
inline constexpr char g_szRulesClass[] = "Rules Data";

constexpr WORD g_wVersion = 0x0001;
constexpr WORD g_wActive = 0x0001;
constexpr WORD g_wNoSubsequent = 0x0002;
constexpr WORD g_wMatchCase = 0x0004;
constexpr WORD g_wMatchWord = 0x0008;
constexpr WORD g_wStopped = 0x0040;
constexpr WORD g_wSortDescending = 0x0080;

enum enumEvents {
    eOnConnect,
    eOnDisconnect,
    eOnInvitation,
    eOnJoin,
    eOnKick,
    eOnLeave,
    eOnMessage,
    eOnNewHost,
    eOnNewRoom,
    eOnWhisper,
    eOnWhisperInRoom,
    eMax
};

enum enumActions {
    aBan,
    aBeep,
    aDoNotDisplay,
    aExecuteMacro,
    aGetIdentity,
    aGetLagTime,
    aGetLocalTime,
    aGetProfile,
    aGetVersion,
    aHighlightMessage,
    aIgnore,
    aInvite,
    aJoinRoom,
    aKick,
    aLeaveRoom,
    aMakeHost,
    aNotifyDialog,
    aPlaySound,
    aConnect,
    aReplaceMessage,
    aSendAction,
    aSendFileLine,
    aSendMessage,
    aSendSound,
    aSendThought,
    aSendWhisper,
    aSendWhisperInRoom,
    aWhisperFileLine,
    aDisconnect,
    aActivateRuleSet,
    aMax
};

enum enumParamType {
    ptActivate,
    ptBeepCount,
    ptHighlight,
    ptLineNumber,
    ptMacroName,
    ptMessage,
    ptNickname,
    ptReason,
    ptRoomName,
    ptRuleSetName,
    ptServerName,
    ptSoundFileName,
    ptTextFileName,
    ptMax
};

enum enumKeyEventParam {
    kepAny,
    kepAnyone,
    kepMe,
    kepAnyoneButMe,
    kepAnyOfMyRooms,
    kepMyActivatedRoom,
    kepMyInactivatedRooms,
    kepMax
};

enum enumKeyActionParam {
    kapMyActivatedRoom,
    kapAll,
    kapEventMessage,
    kapEventNickname,
    kapEventRoom,
    kapEventServer,
    kapRandom,
    kapYes,
    kapNo,
    kapEventRecipients,
    kapMe,
    kapMax
};

enum enumItemTypes {
    itChannel,
    itUser,
    itMax
};

enum enumExceptionTypes {
    etMinDelay,
    etMax
};

struct RULEX {
    enumExceptionTypes ex;
    enumEvents eID;
    enumActions aID;
    DWORD dwValue;
};

inline constexpr RULEX g_rgex[] = {
    {etMinDelay, eOnDisconnect, aConnect, 5L}
};

inline BOOL RTFParam(enumParamType actionParam, enumActions actionID)
{
    return actionID != aNotifyDialog && actionParam == ptMessage;
}

class CCRulesData;
class CCDynaRules;
class CCRuleSet;
class CCRule;
class CCDaemonExt;
class CCActionContext;
class CCDelayedRules;
class CCNotif;
class CCDynaNotifs;
class CUser;

using EVENT_KEY_PARAM_FN = BOOL (*)(QString&, enumKeyEventParam);
using EVENT_RND_PARAM_FN = BOOL (*)(QString&, QString&, PPRUSERMATCH, WORD,
                                    enumParamType);
using GET_EVENT_KEY_FN = QString (*)(enumParamType);
using GET_ACTION_KEY_FN = QString (*)(enumKeyActionParam, QString&, QString&,
                                      QString&, QString&, QString&);
using EXECUTE_ACTION_FN = BOOL (*)(CCDynaRules*, CCRule*, CCActionContext*);
using RULE_FAILURE_FN = BOOL (*)(CCRuleSet*, CCRule*, UINT);
using RULEDAEMON_QUERY_FN = BOOL (*)(CCRule*);

class CCEvent {
    friend class CCRulesData;
    friend class CCDynaRules;
    friend class CCRule;

public:
    enumEvents GetID() const { return m_eID; }
    QString& GetLongDesc() { return m_strLongDesc; }
    QString& GetParamDesc(UINT index) { return m_rgstrParamDesc[index]; }
    DWORD GetEnabledActions() const { return m_dwEnabledActions; }
    enumParamType GetParamType(UINT index) const { return m_rgpt[index]; }
    UINT GetParamNum() const { return m_uParamNum; }
    UINT GetKeyParam(UINT index) const { return m_rguKeyParam[index]; }
    DWORD GetActionKeysExposed() const { return m_dwActionKeysExposed; }
    QString GetShortDesc() const { return m_strShortDesc; }

private:
    enumEvents m_eID = eMax;
    UINT m_uIDS_LongDesc = 0;
    UINT m_uIDS_ShortDesc = 0;
    UINT m_rguIDS_ParamDesc[g_uMaxEventParams]{};
    UINT m_uParamNum = 0;
    UINT m_rguKeyParam[g_uMaxEventParams]{};
    enumParamType m_rgpt[g_uMaxEventParams]{ptMax, ptMax, ptMax};
    DWORD m_dwActionKeysExposed = 0;
    DWORD m_dwEnabledActions = 0;
    QString m_strLongDesc;
    QString m_strShortDesc;
    QString m_rgstrParamDesc[g_uMaxEventParams];
    BOOL m_bNeedDaemon = FALSE;
};

class CCAction {
    friend class CCRulesData;
    friend class CCDynaRules;
    friend class CCRule;

public:
    enumActions GetID() const { return m_aID; }
    QString& GetLongDesc() { return m_strLongDesc; }
    QString& GetParamDesc(UINT index) { return m_rgstrParamDesc[index]; }
    enumParamType GetParamType(UINT index) const { return m_rgpt[index]; }
    UINT GetParamNum() const { return m_uParamNum; }
    UINT GetKeyParam(UINT index) const { return m_rguKeyParam[index]; }
    BOOL GetDelayOK() const { return m_bDelayOK; }
    QString GetShortDesc() const { return m_strShortDesc; }

private:
    enumActions m_aID = aMax;
    UINT m_uIDS_LongDesc = 0;
    UINT m_uIDS_ShortDesc = 0;
    UINT m_rguIDS_ParamDesc[g_uMaxActionParams]{};
    UINT m_uParamNum = 0;
    UINT m_rguKeyParam[g_uMaxActionParams]{};
    BOOL m_bDelayOK = FALSE;
    enumParamType m_rgpt[g_uMaxActionParams]{ptMax, ptMax, ptMax};
    QString m_strLongDesc;
    QString m_strShortDesc;
    QString m_rgstrParamDesc[g_uMaxActionParams];
};

class CCRulesData {
public:
    CCRulesData();
    ~CCRulesData();

    BOOL bInitAlloc();
    BOOL bLoadStrings();
    CCEvent* GetEvent(UINT index);
    CCAction* GetAction(UINT index);
    UINT GetMissingEventParamError(enumParamType type) const;
    UINT GetMissingActionParamError(enumParamType type) const;
    QString GetKeyEventParam(enumKeyEventParam key) const;
    QString GetKeyActionParam(enumKeyActionParam key) const;
    QString GetEventsDesc() const { return m_strEventsDesc; }
    QString GetActionsDesc() const { return m_strActionsDesc; }
    QString StrFindAndReplaceKeyParams(QString input, BOOL incoming) const;

private:
    UINT m_rguIDS_MissingEventParamError[ptMax]{};
    UINT m_rguIDS_MissingActionParamError[ptMax]{};
    QString m_rgstrKeyEventParam[kepMax];
    QString m_rgstrKeyActionParam[kapMax];
    QString m_strEventsDesc;
    QString m_strActionsDesc;
    CCEvent* m_rgpEvents[eMax]{};
    CCAction* m_rgpActions[aMax]{};
    BOOL m_bInitAlloc = FALSE;
    BOOL m_bStringsLoaded = FALSE;
};

class CCChannel {
    friend class CCItemPtrArray;
    friend class CCDaemonExt;

public:
    BOOL operator==(const CCChannel& channel) const;

private:
    QString m_strChannelName;
};

class CCItemPtrArray {
    friend class CCDaemonExt;
    friend class CCDynaNotifs;

public:
    explicit CCItemPtrArray(enumItemTypes itemType = itUser)
        : m_it(itemType) {}
    ~CCItemPtrArray() { FreeRemoveAll(); }

    void FreeRemoveAll();
    INT GetSize() const { return m_items.size(); }
    void* GetAt(INT index) const
    {
        return index >= 0 && index < m_items.size() ? m_items.at(index)
                                                    : nullptr;
    }

private:
    QVector<void*> m_items;
    SHORT m_nCredits = 0;
    enumItemTypes m_it = itUser;
};

class CCDaemonExt {
    friend class CCRule;
    friend class CCNotif;

public:
    explicit CCDaemonExt(enumItemTypes itemType);
    ~CCDaemonExt();

    enumItemTypes GetIT() const { return m_it; }
    BOOL bAllocNewItemList(UINT listCount = 1);
    BOOL bCleanUpItemLists();
    BOOL bAddUserToCurrentList(CUser* user);
    BOOL bAddChannelToCurrentList(const QString& channelName);
    BOOL bOnEndOfListing(CCDynaRules* dynaRules, CCRule* rule,
                         enumQueryPurpose queryPurpose);
    BOOL bOnEndOfListing(CCDynaNotifs* dynaNotifs, CCNotif* notif);
    BOOL bTreatNewItems(CCDynaRules* dynaRules, CCRule* rule,
                        CCItemPtrArray* previousItems,
                        CCItemPtrArray* currentItems);
    BOOL bTreatOldItems(CCDynaRules* dynaRules, CCRule* rule,
                        CCItemPtrArray* previousItems,
                        CCItemPtrArray* currentItems);
    BOOL bTreatNewItems(CCDynaNotifs* dynaNotifs, CCNotif* notif,
                        CCItemPtrArray* previousItems,
                        CCItemPtrArray* currentItems);
    BOOL bTreatOldItems(CCDynaNotifs* dynaNotifs, CCNotif* notif,
                        CCItemPtrArray* previousItems,
                        CCItemPtrArray* currentItems);
    void SetResetItemLists(BOOL reset) { m_bResetItemLists = reset; }
    void AddRef();
    void Release();

private:
    QList<CCItemPtrArray*> m_itemLists;
    SHORT m_nRefCount = 1;
    BOOL m_bResetItemLists = FALSE;
    BOOL m_bClearedItemLists = FALSE;
    enumItemTypes m_it = itMax;
};

class CCActionContext {
public:
    CCActionContext();
    ~CCActionContext();

    enumEvents GetEventID() const { return m_eID; }
    enumActions GetActionID() const { return m_aID; }
    enumKeyActionParam GetActionKeyParam(UINT index) const {
        return m_rgkap[index];
    }
    QString GetFinalActionParam(UINT index) const {
        return m_rgstrActionFinalParams[index];
    }
    CDWordArray* GetFinalMsgFormatting() const {
        return m_prgdwFinalMsgFormatting;
    }
    QString GetCachedIdentity() const { return m_strIdentityCach; }
    QString GetCachedChannel() const { return m_strChannelCach; }
    UCHAR GetDelay() const { return m_uDelay; }
    UCHAR GetDecrementedDelay() { return --m_uDelay; }
    BOOL bInitActionContext(CCDynaRules* dynaRules, CCRule* rule);

private:
    UCHAR m_uDelay = 0;
    enumEvents m_eID = eMax;
    enumActions m_aID = aMax;
    enumKeyActionParam m_rgkap[g_uMaxActionParams]{kapMax, kapMax, kapMax};
    QString m_rgstrActionFinalParams[g_uMaxActionParams];
    CDWordArray* m_prgdwFinalMsgFormatting = nullptr;
    QString m_strIdentityCach;
    QString m_strChannelCach;
};

class CCRule {
    friend class CCDynaRules;
    friend class CCDaemonExt;

public:
    explicit CCRule(CCDynaRules* dynaRules);
    CCRule(CCRule* rule, CCDynaRules* dynaRules);
    ~CCRule();

    void AddRef();
    void Release();
    void CopyRule(CCRule* rule);
    QString StrGetEventDisplay();
    QString StrGetActionDisplay();
    void Activate() { m_wFlags |= g_wActive; }
    void Desactivate() { m_wFlags &= ~g_wActive; }
    BOOL bActive() const { return m_wFlags & g_wActive; }
    BOOL bStopped() const { return m_wFlags & g_wStopped; }
    WORD wGetFlags() const { return m_wFlags; }
    void SetFlags(WORD flags) { m_wFlags = flags; }
    void SetDelay(UCHAR delay) { m_uDelay = delay; }
    UCHAR GetDelay() const { return m_uDelay; }
    void SetDynaRules(CCDynaRules* dynaRules) { m_pDynaRules = dynaRules; }
    CCEvent* GetEvent() const { return m_pEvent; }
    CCAction* GetAction() const { return m_pAction; }
    void SetEvent(CCEvent* event) { m_pEvent = event; }
    void SetAction(CCAction* action) { m_pAction = action; }
    void SetEventParam(UINT index, const QString& parameter);
    void SetActionParam(UINT index, const QString& parameter) {
        m_rgstrActionParams[index] = parameter;
    }
    void SetEventKeyParam(UINT index, enumKeyEventParam key) {
        m_rgkep[index] = key;
    }
    enumKeyEventParam GetEventKeyParam(UINT index) const { return m_rgkep[index]; }
    void SetActionKeyParam(UINT index, enumKeyActionParam key) {
        m_rgkap[index] = key;
    }
    enumKeyActionParam GetActionKeyParam(UINT index) const {
        return m_rgkap[index];
    }
    QString GetEventParam(UINT index) const { return m_rgstrEventParams[index]; }
    QString GetActionParam(UINT index) const { return m_rgstrActionParams[index]; }
    QString GetFinalActionParam(UINT index) const {
        return m_rgstrActionFinalParams[index];
    }
    CDWordArray* GetFinalMsgFormatting() const {
        return m_prgdwFinalMsgFormatting;
    }
    CDWordArray* GetMsgFormatting() const { return m_prgdwMsgFormatting; }
    void SetMsgFormatting(CDWordArray* formatting, BOOL makeCopy);
    CCDaemonExt* GetDaemonExt() const { return m_pDaemonExt; }
    void SetDaemonExt(CCDaemonExt* daemonExt) { m_pDaemonExt = daemonExt; }
    void InitRuleDaemon();
    INT Serialize(char* buffer, INT bufferLength);
    INT UnSerialize(const BYTE* buffer, INT bufferLength);
    BOOL bUnSerialize(const QString& rule);
    INT iGetHighlightTypeIndex(QString parameter);
    BOOL bValidateRuleEvent(UINT index, QString& parameter, UINT* errorID);
    BOOL bValidateRuleAction(UINT index, QString& parameter, UINT* errorID);
    BOOL bDaemonNeeded() const;
    BOOL bUpdateDaemonExt(BOOL resetItemLists, enumEvents event);
    BOOL bIsFlooding();

private:
    QString StrParamBeginning(const QString& parameter) const;
    void clearRule();

    CCEvent* m_pEvent = nullptr;
    CCAction* m_pAction = nullptr;
    QString m_rgstrEventParams[g_uMaxEventParams];
    enumKeyEventParam m_rgkep[g_uMaxEventParams]{kepMax, kepMax, kepMax};
    QString m_rgstrActionParams[g_uMaxActionParams];
    enumKeyActionParam m_rgkap[g_uMaxActionParams]{kapMax, kapMax, kapMax};
    CDWordArray* m_prgdwMsgFormatting = nullptr;
    QString m_rgstrActionFinalParams[g_uMaxActionParams];
    CDWordArray* m_prgdwFinalMsgFormatting = nullptr;
    PRUSERMATCH m_prUserMatch;
    SHORT m_nRefCount = 1;
    WORD m_wFlags = 0;
    UCHAR m_uDelay = 0;
    USHORT m_uPeriodStart = 0;
    UCHAR m_uOccurrences = 0;
    CCDynaRules* m_pDynaRules = nullptr;
    CCDaemonExt* m_pDaemonExt = nullptr;
};

class CCRuleSet {
    friend class CCDynaRules;

public:
    explicit CCRuleSet(CCDynaRules* dynaRules);
    CCRuleSet(CCRuleSet* ruleSet, CCDynaRules* dynaRules);
    ~CCRuleSet();

    void SetName(const QString& name) { m_strSetName = name; }
    QString GetName() const { return m_strSetName; }
    void Activate() { m_wFlags |= g_wActive; }
    void Desactivate() { m_wFlags &= ~g_wActive; }
    BOOL bActive() const { return m_wFlags & g_wActive; }
    WORD wGetFlags() const { return m_wFlags; }
    void SetFlags(WORD flags) { m_wFlags = flags; }
    void SetDynaRules(CCDynaRules* dynaRules) { m_pDynaRules = dynaRules; }
    CCDynaRules* GetDynaRules() const { return m_pDynaRules; }
    QVector<CCRule*>& GetRulesArray() { return m_rgpRules; }
    const QVector<CCRule*>& GetRulesArray() const { return m_rgpRules; }
    BOOL bAddRule(CCRule* rule, INT index = -1);
    BOOL bRemoveRule(CCRule* rule, INT index = -1);
    BOOL bDuplicateRule(INT index, CCRule** rule);
    BOOL bUpRule(CCRule* rule, INT index = -1);
    BOOL bDownRule(CCRule* rule, INT index = -1);
    BOOL bSaveToFile(const QString& fileName, UINT* error = nullptr);
    BOOL bLoadFromFile(const QString& fileName, UINT* error = nullptr);
    BOOL bDaemonNeeded() const;
    BOOL bUpdateRulesDaemonExt(BOOL resetItemLists);

private:
    void CleanUpRulesArray();
    CCDynaRules* m_pDynaRules = nullptr;
    QVector<CCRule*> m_rgpRules;
    WORD m_wFlags = 0;
    QString m_strSetName;
};

class CCDynaRules {
    friend class CCDaemonExt;
public:
    CCDynaRules();
    ~CCDynaRules();
    const CCDynaRules& operator=(const CCDynaRules& dynaRules);

    CCRulesData* GetRulesData() const { return m_pRulesData; }
    CCRuleSet* GetSelectedRuleSet() const { return m_pSelectedRuleSet; }
    CCRuleSet* GetRuleSetFromName(const QString& setName);
    QVector<CCRuleSet*>& GetRuleSetsArray() { return m_rgpRuleSets; }
    const QVector<CCRuleSet*>& GetRuleSetsArray() const { return m_rgpRuleSets; }
    QString GetCachedIdentity() const { return m_strIdentityCach; }
    QString GetCachedServer() const { return m_strServerCach; }
    QString GetCachedChannel() const { return m_strChannelCach; }
    QString GetCachedRecipients() const { return m_strRecipientsCach; }
    QString GetCachedCFMesage() const { return m_strCFMessageCach; }
    QString GetCachedCLMesage() const { return m_strCLMessageCach; }
    WORD GetFlags() const { return m_wFlags; }
    UCHAR GetFloodingInterval() const { return m_uFloodInterval; }
    UCHAR GetFloodingOccurrences() const { return m_uFloodOccurrences; }
    void SetFloodParams(UCHAR interval, UCHAR occurrences) {
        m_uFloodInterval = interval;
        m_uFloodOccurrences = occurrences;
    }
    void SetCachVariables(enumEvents event, QString& identity, QString& server,
                          QString& channel);
    void SetCachRecipients(const QString& recipients) {
        m_strRecipientsCach = recipients;
    }
    void ResetFlags() { m_wFlags = 0; }
    void AddFlag(WORD flag) { m_wFlags |= flag; }
    void SetRulesData(CCRulesData* rulesData) { m_pRulesData = rulesData; }
    void SetDelayedRules(CCDelayedRules* delayedRules) {
        m_pDelayedRules = delayedRules;
    }
    void SetSelectedRuleSet(CCRuleSet* ruleSet) { m_pSelectedRuleSet = ruleSet; }
    void SetCFFinalMessage(const QString& message) { m_strCFFinalMessage = message; }
    QString GetCFFinalMessage() const { return m_strCFFinalMessage; }
    void SetEKPFunction(EVENT_KEY_PARAM_FN function) { m_pfEventKeyParam = function; }
    void SetERPFunction(EVENT_RND_PARAM_FN function) { m_pfEventRndParam = function; }
    void SetGEKPFunction(GET_EVENT_KEY_FN function) {
        m_pfGetKeyEventParam = function;
    }
    void SetGAKPFunction(GET_ACTION_KEY_FN function) {
        m_pfGetKeyActionParam = function;
    }
    void SetExecuteActionFunction(EXECUTE_ACTION_FN function) {
        m_pfExecuteAction = function;
    }
    void SetRuleFailureFunction(RULE_FAILURE_FN function) {
        m_pfRuleFailure = function;
    }
    void SetDaemonQueryFunction(RULEDAEMON_QUERY_FN function) {
        m_pfDaemonQuery = function;
    }

    BOOL bAddRuleSet(CCRuleSet* ruleSet, INT index = -1);
    BOOL bRemoveRuleSet(CCRuleSet* ruleSet, INT index = -1);
    BOOL bUpRuleSet(CCRuleSet* ruleSet, INT index = -1);
    BOOL bDownRuleSet(CCRuleSet* ruleSet, INT index = -1);
    BOOL bReplaceMessage(CCRule* rule);
    BOOL bReplaceKeyActionParams(CCRule* rule);
    BOOL bReplaceKeyEventParams(QString& eventParameter);
    BOOL bMatchAndApplyRules(enumEvents event, enumActions* approvedIDs,
                             enumActions* rejectedIDs, QString& server,
                             QString& identity, QString& channel,
                             QString& message);
    BOOL bInActionIDs(enumActions* actions, enumActions action) const;
    INT iGetFirstMatchingRule(INT* ruleSet, enumEvents event,
                              enumActions* approvedIDs, enumActions* rejectedIDs,
                              QString& server, QString& identity, QString& channel,
                              QString& message, CCRule** rule = nullptr);
    INT iGetNextMatchingRule(INT* previousRuleSet, INT previousRule,
                             CCRule** rule = nullptr);
    BOOL bSaveRulesToReg();
    BOOL bLoadRulesFromReg();
    BOOL bLoadRulesFromResource();
    BOOL bDaemonNeeded() const;
    BOOL bUpdateRuleSetsDaemonExt(BOOL resetItemLists);
    BOOL bStartRulesDaemon(UINT elapse, BOOL forceReset);
    BOOL bStopRulesDaemon();
    void OnRulesDaemonTimer();

private:
    BOOL bRuleFilteredOut(CCRule* rule) const;
    BOOL bMatchingRule(CCRule* rule);
    BOOL executeMatchingRule(INT ruleSetIndex, CCRule* rule);
    void CleanUpRuleSetsArray();

    CCRulesData* m_pRulesData = nullptr;
    CCDelayedRules* m_pDelayedRules = nullptr;
    WORD m_wFlags = 0;
    QVector<CCRuleSet*> m_rgpRuleSets;
    CCRuleSet* m_pSelectedRuleSet = nullptr;
    enumEvents m_eIDCach = eMax;
    enumActions* m_paApprovedIDsCach = nullptr;
    enumActions* m_paRejectedIDsCach = nullptr;
    QString m_strIdentityCach;
    QString m_strServerCach;
    QString m_strChannelCach;
    QString m_strRecipientsCach;
    QString m_strCFMessageCach;
    QString m_strCLMessageCach;
    CDWordArray* m_prgdwMsgFormattingCach = nullptr;
    QString m_strCFFinalMessage;
    EVENT_KEY_PARAM_FN m_pfEventKeyParam = nullptr;
    EVENT_RND_PARAM_FN m_pfEventRndParam = nullptr;
    GET_EVENT_KEY_FN m_pfGetKeyEventParam = nullptr;
    GET_ACTION_KEY_FN m_pfGetKeyActionParam = nullptr;
    EXECUTE_ACTION_FN m_pfExecuteAction = nullptr;
    RULE_FAILURE_FN m_pfRuleFailure = nullptr;
    RULEDAEMON_QUERY_FN m_pfDaemonQuery = nullptr;
    BOOL m_bDaemonRunning = FALSE;
    QTimer* m_rulesDaemonTimer = nullptr;
    UCHAR m_uFloodInterval = g_uDefRuleFloodInt;
    UCHAR m_uFloodOccurrences = g_uDefRuleFloodOcc;
};

class CCDelayedRules {
public:
    CCDelayedRules();
    ~CCDelayedRules();
    void SetExecuteActionFunction(EXECUTE_ACTION_FN function) {
        m_pfExecuteAction = function;
    }
    BOOL bAddActionCtx(CCActionContext* actionContext);
    BOOL bExecuteActions();
    BOOL bStartTimer();
    BOOL bStopTimer();
    void FreeRemoveAll();
    BOOL bTimerRunning() const { return m_bTimerRunning; }
    INT GetCount() const { return m_plActionCtx.size(); }

private:
    EXECUTE_ACTION_FN m_pfExecuteAction = nullptr;
    BOOL m_bTimerRunning = FALSE;
    QList<CCActionContext*> m_plActionCtx;
    QTimer* m_timer = nullptr;
};
