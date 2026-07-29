//=--------------------------------------------------------------------------=
// Actions.H -- Qt port of v2.5-beta-1-modern/actions.h
//=--------------------------------------------------------------------------=

#pragma once

#include "rules.h"

#include <QString>

class CNotificationUsers;
class CIrcProto;

inline constexpr char g_szAllLines[] = "1-999999";
inline constexpr char g_szRandomLine[] = "RND";

BOOL bGetNextRange(char** string, UINT* minimum, UINT* maximum);
void TrimQuotes(QString& input);
QString GetNextToken(QString& tokens, CHAR separator, BOOL trim);
QString StrExtractNickname(QString identity);
QString StrExtractIdent(QString identity);
BOOL bKeyEventParam(QString& parameter, enumKeyEventParam key);
BOOL bNetValid(QString netParameter);
BOOL bRndEventParam(QString& value, QString& filter,
                    PPRUSERMATCH userMatch, WORD flags,
                    enumParamType type);
QString StrGetKeyEventParam(enumParamType type);
QString StrGetKeyActionParam(enumKeyActionParam key,
                             QString& eventServer,
                             QString& eventIdentity,
                             QString& eventChannel,
                             QString& eventRecipients,
                             QString& eventControlLessMessage);
BOOL bSendOrWhisperFileLine(CIrcProto* protocol,
                            CCActionContext* actionContext,
                            BOOL whisper);
BOOL bExecuteAction(CCDynaRules* dynaRules, CCRule* rule,
                    CCActionContext* actionContext);
BOOL bRuleDaemonQuery(CCRule* rule);
QString StrAddWildcards(QString input, UCHAR op, BOOL isNickname);
BOOL bNotifDaemonQuery(CCNotif* notif);
BOOL bDisplayNotifications(CCDynaNotifs* dynaNotifs);
BOOL bSignalNewUpdate(CCDynaNotifs* dynaNotifs);
CNotificationUsers* GetNotifBox();
CNotificationUsers* CreateNotificationBox();
void DestroyNotificationBox();
BOOL bReportRuleFailure(CCRuleSet* ruleSet, CCRule* rule,
                        UINT errorCode);
