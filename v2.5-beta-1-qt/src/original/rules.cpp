//=--------------------------------------------------------------------------=
// Rules.Cpp -- Qt port of v2.5-beta-1-modern/rules.cpp
//=--------------------------------------------------------------------------=

#include "rules.h"

#include "actions.h"
#include "ccommon.h"
#include "notif.h"
#include "originalassets.h"
#include "originalsettings.h"
#include "userlist.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>

namespace {
constexpr UINT eventParamNums[eMax] = {
    2, 2, 2, 2, 2, 2, 3, 2, 1, 2, 3
};

constexpr BOOL eventNeedDaemon[eMax] = {
    TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
    FALSE, FALSE, TRUE, FALSE, FALSE
};

constexpr WORD actionParamFlags[aMax] = {
    0x0010, 0x0011, 0x0000, 0x0011, 0x0010, 0x0010,
    0x0011, 0x0010, 0x0010, 0x0001, 0x0010, 0x0011,
    0x0011, 0x0011, 0x0010, 0x0010, 0x0011, 0x0011,
    0x0012, 0x0001, 0x0012, 0x0013, 0x0012, 0x0013,
    0x0012, 0x0012, 0x0013, 0x0013, 0x0010, 0x0012
};

constexpr enumParamType eventParamTypes[eMax][g_uMaxEventParams] = {
    {ptNickname, ptServerName, ptMax},
    {ptNickname, ptServerName, ptMax},
    {ptNickname, ptRoomName, ptMax},
    {ptNickname, ptRoomName, ptMax},
    {ptNickname, ptRoomName, ptMax},
    {ptNickname, ptRoomName, ptMax},
    {ptNickname, ptRoomName, ptMessage},
    {ptNickname, ptRoomName, ptMax},
    {ptRoomName, ptMax, ptMax},
    {ptNickname, ptMessage, ptMax},
    {ptNickname, ptRoomName, ptMessage}
};

constexpr UINT eventKeyParams[eMax][g_uMaxEventParams] = {
    {0x04, 0x01, 0x00}, {0x04, 0x01, 0x00},
    {0x08, 0x01, 0x00}, {0x0e, 0x70, 0x00},
    {0x0e, 0x70, 0x00}, {0x0e, 0x70, 0x00},
    {0x08, 0x70, 0x01}, {0x0e, 0x70, 0x00},
    {0x00, 0x00, 0x00}, {0x08, 0x01, 0x00},
    {0x08, 0x70, 0x01}
};

constexpr DWORD eventEnabledActions[eMax] = {
    0x3ff31c0a, 0x2ff7000a, 0x3ff31c0a, 0x3ff3fffb,
    0x3ff35e0b, 0x3ff35e0b, 0x3ffbffff, 0x3ff3f9fb,
    0x3ff3100a, 0x3ffb1e0e, 0x3ffbffff
};

constexpr DWORD eventExposedActionKeys[eMax] = {
    0x05eb, 0x05eb, 0x05db, 0x05db, 0x05db, 0x05db,
    0x08df, 0x05db, 0x05d3, 0x05cf, 0x08df
};

constexpr enumParamType actionParamTypes[aMax][g_uMaxActionParams] = {
    {ptMax, ptMax, ptMax},
    {ptBeepCount, ptMax, ptMax},
    {ptMax, ptMax, ptMax},
    {ptMacroName, ptMax, ptMax},
    {ptMax, ptMax, ptMax},
    {ptMax, ptMax, ptMax},
    {ptNickname, ptMax, ptMax},
    {ptMax, ptMax, ptMax},
    {ptMax, ptMax, ptMax},
    {ptHighlight, ptMax, ptMax},
    {ptMax, ptMax, ptMax},
    {ptRoomName, ptMax, ptMax},
    {ptRoomName, ptMax, ptMax},
    {ptReason, ptMax, ptMax},
    {ptMax, ptMax, ptMax},
    {ptMax, ptMax, ptMax},
    {ptMessage, ptMax, ptMax},
    {ptSoundFileName, ptMax, ptMax},
    {ptNickname, ptServerName, ptMax},
    {ptMessage, ptMax, ptMax},
    {ptRoomName, ptMessage, ptMax},
    {ptRoomName, ptTextFileName, ptLineNumber},
    {ptRoomName, ptMessage, ptMax},
    {ptRoomName, ptMessage, ptSoundFileName},
    {ptRoomName, ptMessage, ptMax},
    {ptNickname, ptMessage, ptMax},
    {ptNickname, ptRoomName, ptMessage},
    {ptNickname, ptTextFileName, ptLineNumber},
    {ptMax, ptMax, ptMax},
    {ptRuleSetName, ptActivate, ptMax}
};

constexpr UINT actionKeyParams[aMax][g_uMaxActionParams] = {
    {0x0000, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000},
    {0x0408, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000}, {0x0011, 0x0000, 0x0000},
    {0x0010, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000},
    {0x0000, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000},
    {0x0004, 0x0000, 0x0000}, {0x0000, 0x0000, 0x0000},
    {0x0008, 0x0020, 0x0000}, {0x0000, 0x0000, 0x0000},
    {0x0011, 0x0000, 0x0000}, {0x0011, 0x0000, 0x0042},
    {0x0011, 0x0004, 0x0000}, {0x0011, 0x0004, 0x0000},
    {0x0011, 0x0000, 0x0000}, {0x0008, 0x0004, 0x0000},
    {0x0208, 0x0011, 0x0004}, {0x0008, 0x0000, 0x0042},
    {0x0000, 0x0000, 0x0000}, {0x0000, 0x0180, 0x0000}
};

constexpr char beginParams[] = "(";
constexpr char endParams[] = ")";
constexpr char beginParam[] = "<";
constexpr char endParam[] = ">";
constexpr char paramSeparator[] = ", ";
constexpr char continuation[] = "...";

UINT actionParamNum(UINT index)
{
    return actionParamFlags[index] & 0x000f;
}

BOOL actionDelayOK(UINT index)
{
    return actionParamFlags[index] & 0x00f0 ? TRUE : FALSE;
}

bool rtfParam(enumParamType type, enumActions action)
{
    return action != aNotifyDialog && type == ptMessage;
}

void replaceCaseInsensitive(QString& input, const QString& from,
                            const QString& to)
{
    if (from.isEmpty()) return;
    qsizetype offset = 0;
    while ((offset = input.indexOf(from, offset, Qt::CaseInsensitive)) >= 0) {
        input.replace(offset, from.size(), to);
        offset += to.size();
    }
}

QString controlLess(const QString& controlFull, CDWordArray* formatting)
{
    QByteArray bytes = controlFull.toUtf8();
    char* plain = SzControlLess(bytes.data(), formatting);
    return QString::fromUtf8(plain);
}

QString controlFull(const QString& plain, CDWordArray* formatting)
{
    const QByteArray bytes = plain.toUtf8();
    char* formatted = SzControlFull(bytes.constData(), formatting);
    const QString result = QString::fromUtf8(formatted);
    delete[] formatted;
    return result;
}

void appendWord(QByteArray& bytes, WORD value)
{
    const WORD little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char*>(&little), sizeof(little));
}

void appendDWord(QByteArray& bytes, DWORD value)
{
    const DWORD little = qToLittleEndian(value);
    bytes.append(reinterpret_cast<const char*>(&little), sizeof(little));
}

bool readWord(const BYTE*& cursor, int& left, WORD* value)
{
    if (left < static_cast<int>(sizeof(WORD))) return false;
    *value = qFromLittleEndian<WORD>(cursor);
    cursor += sizeof(WORD);
    left -= sizeof(WORD);
    return true;
}

bool readDWord(const BYTE*& cursor, int& left, DWORD* value)
{
    if (left < static_cast<int>(sizeof(DWORD))) return false;
    *value = qFromLittleEndian<DWORD>(cursor);
    cursor += sizeof(DWORD);
    left -= sizeof(DWORD);
    return true;
}

QString readZeroTerminated(const BYTE*& cursor, int& left, bool* ok)
{
    int length = 0;
    while (length < left && cursor[length] != 0) ++length;
    if (length >= left) {
        *ok = false;
        return {};
    }
    const QString result = QString::fromUtf8(
        reinterpret_cast<const char*>(cursor), length);
    cursor += length + 1;
    left -= length + 1;
    *ok = true;
    return result;
}
}

CCRulesData::CCRulesData()
{
    for (UINT index = 0; index < ptMax; ++index) {
        m_rguIDS_MissingEventParamError[index] =
            IDS_MISSING_EVENT_PARAM_ERROR0 + index;
        m_rguIDS_MissingActionParamError[index] =
            IDS_MISSING_ACTION_PARAM_ERROR0 + index;
    }
}

CCRulesData::~CCRulesData()
{
    for (CCEvent*& event : m_rgpEvents) {
        delete event;
        event = nullptr;
    }
    for (CCAction*& action : m_rgpActions) {
        delete action;
        action = nullptr;
    }
}

BOOL CCRulesData::bInitAlloc()
{
    if (m_bInitAlloc) return TRUE;

    for (UINT index = 0; index < eMax; ++index) {
        auto* event = new CCEvent;
        event->m_eID = static_cast<enumEvents>(index);
        event->m_uIDS_LongDesc = IDS_EVENT_LONG_DESC0 + index;
        event->m_uIDS_ShortDesc = IDS_EVENT_SHORT_DESC0 + index;
        event->m_uParamNum = eventParamNums[index];
        event->m_dwActionKeysExposed = eventExposedActionKeys[index];
        event->m_dwEnabledActions = eventEnabledActions[index];
        event->m_bNeedDaemon = eventNeedDaemon[index];
        for (UINT parameter = 0; parameter < g_uMaxEventParams; ++parameter) {
            event->m_rguIDS_ParamDesc[parameter] = parameter < event->m_uParamNum
                ? IDS_EVENT_PARAM_DESC0 + index * g_uMaxEventParams + parameter
                : 0;
            event->m_rguKeyParam[parameter] = eventKeyParams[index][parameter];
            event->m_rgpt[parameter] = eventParamTypes[index][parameter];
        }
        m_rgpEvents[index] = event;
    }

    for (UINT index = 0; index < aMax; ++index) {
        auto* action = new CCAction;
        action->m_aID = static_cast<enumActions>(index);
        action->m_uIDS_LongDesc = IDS_ACTION_LONG_DESC0 + index;
        action->m_uIDS_ShortDesc = IDS_ACTION_SHORT_DESC0 + index;
        action->m_uParamNum = actionParamNum(index);
        action->m_bDelayOK = actionDelayOK(index);
        for (UINT parameter = 0; parameter < g_uMaxActionParams; ++parameter) {
            action->m_rguIDS_ParamDesc[parameter] = parameter < action->m_uParamNum
                ? IDS_ACTION_PARAM_DESC0 + index * g_uMaxActionParams + parameter
                : 0;
            action->m_rguKeyParam[parameter] = actionKeyParams[index][parameter];
            action->m_rgpt[parameter] = actionParamTypes[index][parameter];
        }
        m_rgpActions[index] = action;
    }

    m_bInitAlloc = TRUE;
    return TRUE;
}

BOOL CCRulesData::bLoadStrings()
{
    if (!m_bInitAlloc || m_bStringsLoaded) return m_bInitAlloc;

    for (CCEvent* event : m_rgpEvents) {
        if (!event) return FALSE;
        for (UINT index = 0; index < event->m_uParamNum; ++index) {
            event->m_rgstrParamDesc[index] = originalResourceString(
                QStringLiteral("IDS_EVENT_PARAM_DESC%1").arg(
                    event->m_rguIDS_ParamDesc[index] - IDS_EVENT_PARAM_DESC0));
            if (event->m_rgstrParamDesc[index].isEmpty()) return FALSE;
        }
        event->m_strLongDesc = originalResourceString(
            QStringLiteral("IDS_EVENT_LONG_DESC%1").arg(
                event->m_uIDS_LongDesc - IDS_EVENT_LONG_DESC0));
        event->m_strShortDesc = originalResourceString(
            QStringLiteral("IDS_EVENT_SHORT_DESC%1").arg(
                event->m_uIDS_ShortDesc - IDS_EVENT_SHORT_DESC0));
        if (event->m_strLongDesc.isEmpty() || event->m_strShortDesc.isEmpty()) {
            return FALSE;
        }
    }

    for (CCAction* action : m_rgpActions) {
        if (!action) return FALSE;
        for (UINT index = 0; index < action->m_uParamNum; ++index) {
            action->m_rgstrParamDesc[index] = originalResourceString(
                QStringLiteral("IDS_ACTION_PARAM_DESC%1").arg(
                    action->m_rguIDS_ParamDesc[index] - IDS_ACTION_PARAM_DESC0));
            if (action->m_rgstrParamDesc[index].isEmpty()) return FALSE;
        }
        action->m_strLongDesc = originalResourceString(
            QStringLiteral("IDS_ACTION_LONG_DESC%1").arg(
                action->m_uIDS_LongDesc - IDS_ACTION_LONG_DESC0));
        action->m_strShortDesc = originalResourceString(
            QStringLiteral("IDS_ACTION_SHORT_DESC%1").arg(
                action->m_uIDS_ShortDesc - IDS_ACTION_SHORT_DESC0));
        if (action->m_strLongDesc.isEmpty() || action->m_strShortDesc.isEmpty()) {
            return FALSE;
        }
    }

    for (UINT index = 0; index < kepMax; ++index) {
        m_rgstrKeyEventParam[index] = originalResourceString(
            QStringLiteral("IDS_KEY_EVENT_PARAM%1").arg(index));
        if (m_rgstrKeyEventParam[index].isEmpty()) return FALSE;
    }
    for (UINT index = 0; index < kapMax; ++index) {
        m_rgstrKeyActionParam[index] = originalResourceString(
            QStringLiteral("IDS_KEY_ACTION_PARAM%1").arg(index));
        if (m_rgstrKeyActionParam[index].isEmpty()) return FALSE;
    }

    m_strEventsDesc = originalResourceString(QStringLiteral("IDS_EVENTCMBDESC"));
    m_strActionsDesc = originalResourceString(QStringLiteral("IDS_ACTIONCMBDESC"));
    if (m_strEventsDesc.isEmpty() || m_strActionsDesc.isEmpty()) return FALSE;
    m_bStringsLoaded = TRUE;
    return TRUE;
}

CCEvent* CCRulesData::GetEvent(UINT index)
{
    return m_bInitAlloc && index < eMax ? m_rgpEvents[index] : nullptr;
}

CCAction* CCRulesData::GetAction(UINT index)
{
    return m_bInitAlloc && index < aMax ? m_rgpActions[index] : nullptr;
}

UINT CCRulesData::GetMissingEventParamError(enumParamType type) const
{
    return type < ptMax ? m_rguIDS_MissingEventParamError[type] : 0;
}

UINT CCRulesData::GetMissingActionParamError(enumParamType type) const
{
    return type < ptMax ? m_rguIDS_MissingActionParamError[type] : 0;
}

QString CCRulesData::GetKeyEventParam(enumKeyEventParam key) const
{
    return key < kepMax ? m_rgstrKeyEventParam[key] : QString();
}

QString CCRulesData::GetKeyActionParam(enumKeyActionParam key) const
{
    return key < kapMax ? m_rgstrKeyActionParam[key] : QString();
}

QString CCRulesData::StrFindAndReplaceKeyParams(QString input, BOOL incoming) const
{
    struct EventSubstitution { enumKeyEventParam key; const char* storage; };
    static constexpr EventSubstitution eventSubstitutions[] = {
        {kepMe, "#Me#"}, {kepMyActivatedRoom, "#MyActivatedRoom#"}
    };
    for (const EventSubstitution& substitution : eventSubstitutions) {
        const QString storage = QString::fromLatin1(substitution.storage);
        replaceCaseInsensitive(input,
            incoming ? storage : GetKeyEventParam(substitution.key),
            incoming ? GetKeyEventParam(substitution.key) : storage);
    }

    struct ActionSubstitution { enumKeyActionParam key; const char* storage; };
    static constexpr ActionSubstitution actionSubstitutions[] = {
        {kapMyActivatedRoom, "#MyActivatedRoom#"},
        {kapEventMessage, "#EventMessage#"},
        {kapEventNickname, "#EventNickname#"},
        {kapEventRoom, "#EventRoom#"},
        {kapEventServer, "#EventServer#"},
        {kapEventRecipients, "#EventRecipients#"},
        {kapMe, "#Me#"}
    };
    for (const ActionSubstitution& substitution : actionSubstitutions) {
        const QString storage = QString::fromLatin1(substitution.storage);
        replaceCaseInsensitive(input,
            incoming ? storage : GetKeyActionParam(substitution.key),
            incoming ? GetKeyActionParam(substitution.key) : storage);
    }
    return input;
}

BOOL CCChannel::operator==(const CCChannel& channel) const
{
    return m_strChannelName == channel.m_strChannelName;
}

void CCItemPtrArray::FreeRemoveAll()
{
    for (void* item : m_items) {
        if (m_it == itUser) {
            static_cast<CUser*>(item)->Release();
        } else if (m_it == itChannel) {
            delete static_cast<CCChannel*>(item);
        }
    }
    m_items.clear();
}

CCDaemonExt::CCDaemonExt(enumItemTypes itemType)
    : m_it(itemType)
{
}

CCDaemonExt::~CCDaemonExt()
{
    bCleanUpItemLists();
}

void CCDaemonExt::AddRef()
{
    ++m_nRefCount;
}

void CCDaemonExt::Release()
{
    if (m_nRefCount > 0 && --m_nRefCount == 0) delete this;
}

BOOL CCDaemonExt::bAllocNewItemList(UINT listCount)
{
    if ((listCount != 1 && listCount != 2)
        || (m_it != itUser && m_it != itChannel)) {
        return FALSE;
    }
    for (UINT index = 0; index < listCount; ++index) {
        auto* items = new CCItemPtrArray(m_it);
        items->m_items.reserve(10);
        items->m_nCredits = static_cast<SHORT>(2 / listCount + index);
        m_itemLists.append(items);
    }
    return TRUE;
}

BOOL CCDaemonExt::bCleanUpItemLists()
{
    qDeleteAll(m_itemLists);
    m_itemLists.clear();
    m_bClearedItemLists = TRUE;
    return TRUE;
}

BOOL CCDaemonExt::bAddChannelToCurrentList(const QString& channelName)
{
    if (m_itemLists.size() < 2 || m_it != itChannel) return FALSE;
    auto* channel = new CCChannel;
    channel->m_strChannelName = channelName;
    m_itemLists.last()->m_items.append(channel);
    return TRUE;
}

BOOL CCDaemonExt::bAddUserToCurrentList(CUser* user)
{
    if (!user || m_itemLists.size() < 2 || m_it != itUser) return FALSE;
    m_itemLists.last()->m_items.append(user);
    return TRUE;
}

BOOL CCDaemonExt::bTreatNewItems(CCDynaRules* dynaRules, CCRule* rule,
                                 CCItemPtrArray* previousItems,
                                 CCItemPtrArray* currentItems)
{
    if (!dynaRules || !rule || !previousItems || !currentItems
        || (m_it != itUser && m_it != itChannel)
        || !dynaRules->m_pfGetKeyEventParam
        || !dynaRules->m_pfExecuteAction) {
        return FALSE;
    }

    for (void* currentItem : currentItems->m_items) {
        bool foundPrevious = false;
        for (void* previousItem : previousItems->m_items) {
            if (m_it == itUser) {
                if (static_cast<CUser*>(currentItem)->m_strIdentity
                    == static_cast<CUser*>(previousItem)->m_strIdentity) {
                    foundPrevious = true;
                    break;
                }
            } else if (*static_cast<CCChannel*>(currentItem)
                       == *static_cast<CCChannel*>(previousItem)) {
                foundPrevious = true;
                break;
            }
        }
        if (foundPrevious) continue;

        QString server = dynaRules->m_pfGetKeyEventParam(ptServerName);
        QString identity;
        QString channel;
        if (m_it == itUser) {
            CUser* user = static_cast<CUser*>(currentItem);
            identity = user->m_strNickname + QLatin1Char('!')
                + user->m_strIdentity;
        } else {
            channel = static_cast<CCChannel*>(currentItem)->m_strChannelName;
        }
        dynaRules->SetCachVariables(rule->GetEvent()->GetID(), identity,
                                    server, channel);
        dynaRules->bReplaceKeyActionParams(rule);
        CCActionContext context;
        if (context.bInitActionContext(dynaRules, rule)) {
            dynaRules->m_pfExecuteAction(dynaRules, rule, &context);
            if (m_bClearedItemLists) return TRUE;
        }
    }
    return TRUE;
}

BOOL CCDaemonExt::bTreatOldItems(CCDynaRules* dynaRules, CCRule* rule,
                                 CCItemPtrArray* previousItems,
                                 CCItemPtrArray* currentItems)
{
    if (!dynaRules || !rule || !previousItems || !currentItems
        || m_it != itUser || !dynaRules->m_pfGetKeyEventParam
        || !dynaRules->m_pfExecuteAction) {
        return FALSE;
    }

    for (void* previousItem : previousItems->m_items) {
        CUser* previousUser = static_cast<CUser*>(previousItem);
        bool foundCurrent = false;
        for (void* currentItem : currentItems->m_items) {
            if (static_cast<CUser*>(currentItem)->m_strIdentity
                == previousUser->m_strIdentity) {
                foundCurrent = true;
                break;
            }
        }
        if (foundCurrent) continue;

        QString server = dynaRules->m_pfGetKeyEventParam(ptServerName);
        QString identity = previousUser->m_strNickname + QLatin1Char('!')
            + previousUser->m_strIdentity;
        QString channel;
        dynaRules->SetCachVariables(rule->GetEvent()->GetID(), identity,
                                    server, channel);
        dynaRules->bReplaceKeyActionParams(rule);
        CCActionContext context;
        if (context.bInitActionContext(dynaRules, rule)) {
            dynaRules->m_pfExecuteAction(dynaRules, rule, &context);
            if (m_bClearedItemLists) return TRUE;
        }
    }
    return TRUE;
}

BOOL CCDaemonExt::bOnEndOfListing(CCDynaRules* dynaRules, CCRule* rule,
                                  enumQueryPurpose queryPurpose)
{
    if (!dynaRules || !rule || m_itemLists.size() < 2
        || (queryPurpose != qpOnConnectEvent
            && queryPurpose != qpOnDisconnectEvent
            && queryPurpose != qpOnNewRoomEvent)) {
        return FALSE;
    }

    CCItemPtrArray* previousItems = m_itemLists.at(m_itemLists.size() - 2);
    CCItemPtrArray* currentItems = m_itemLists.last();
    if (previousItems->m_nCredits <= 0 || currentItems->m_nCredits <= 0
        || !bAllocNewItemList()) {
        return FALSE;
    }
    m_bClearedItemLists = FALSE;

    const BOOL result = queryPurpose == qpOnDisconnectEvent
        ? bTreatOldItems(dynaRules, rule, previousItems, currentItems)
        : bTreatNewItems(dynaRules, rule, previousItems, currentItems);

    if (!m_bClearedItemLists) {
        auto consumeCredit = [this](CCItemPtrArray* items) {
            if (--items->m_nCredits == 0) {
                m_itemLists.removeOne(items);
                delete items;
            }
        };
        consumeCredit(previousItems);
        consumeCredit(currentItems);
    }
    return result;
}

BOOL CCDaemonExt::bTreatNewItems(CCDynaNotifs* dynaNotifs, CCNotif* notif,
                                 CCItemPtrArray* previousItems,
                                 CCItemPtrArray* currentItems)
{
    if (!dynaNotifs || !notif || !previousItems || !currentItems
        || m_it != itUser) {
        return FALSE;
    }
    for (void* currentItem : currentItems->m_items) {
        CUser* currentUser = static_cast<CUser*>(currentItem);
        bool foundPrevious = false;
        for (void* previousItem : previousItems->m_items) {
            CUser* previousUser = static_cast<CUser*>(previousItem);
            if (currentUser->m_strNickname == previousUser->m_strNickname
                && currentUser->m_strIdentity == previousUser->m_strIdentity
                && currentUser->m_strPrettyRoom
                    == previousUser->m_strPrettyRoom) {
                foundPrevious = true;
                break;
            }
        }
        if (!foundPrevious) dynaNotifs->bAddNotificationUser(currentUser);
    }
    return TRUE;
}

BOOL CCDaemonExt::bTreatOldItems(CCDynaNotifs* dynaNotifs, CCNotif* notif,
                                 CCItemPtrArray* previousItems,
                                 CCItemPtrArray* currentItems)
{
    if (!dynaNotifs || !notif || !previousItems || !currentItems
        || m_it != itUser) {
        return FALSE;
    }
    for (void* previousItem : previousItems->m_items) {
        CUser* previousUser = static_cast<CUser*>(previousItem);
        bool foundCurrent = false;
        for (void* currentItem : currentItems->m_items) {
            CUser* currentUser = static_cast<CUser*>(currentItem);
            if (currentUser->m_strNickname == previousUser->m_strNickname
                && currentUser->m_strIdentity == previousUser->m_strIdentity
                && currentUser->m_strPrettyRoom
                    == previousUser->m_strPrettyRoom) {
                foundCurrent = true;
                break;
            }
        }
        if (!foundCurrent) {
            dynaNotifs->bModifyNotificationUser(
                previousUser, 0, g_wConnected);
        }
    }
    return TRUE;
}

BOOL CCDaemonExt::bOnEndOfListing(CCDynaNotifs* dynaNotifs,
                                  CCNotif* notif)
{
    if (!dynaNotifs || !notif || m_itemLists.size() < 2) return FALSE;
    dynaNotifs->DecrementWhosCount();

    CCItemPtrArray* previousItems = m_itemLists.at(m_itemLists.size() - 2);
    CCItemPtrArray* currentItems = m_itemLists.last();
    if (previousItems->m_nCredits <= 0 || currentItems->m_nCredits <= 0
        || !bAllocNewItemList()) {
        return FALSE;
    }

    BOOL result = bTreatOldItems(
        dynaNotifs, notif, previousItems, currentItems);
    result = bTreatNewItems(
        dynaNotifs, notif, previousItems, currentItems) && result;

    auto consumeCredit = [this](CCItemPtrArray* items) {
        if (--items->m_nCredits == 0) {
            m_itemLists.removeOne(items);
            delete items;
        }
    };
    consumeCredit(previousItems);
    consumeCredit(currentItems);

    if (dynaNotifs->GetWhosCount() == 0
        && dynaNotifs->m_pfDisplayNotifications) {
        dynaNotifs->m_pfDisplayNotifications(dynaNotifs);
    }
    if (dynaNotifs->GetUpdateCount() > 0) {
        dynaNotifs->DecrementUpdateCount();
        if (dynaNotifs->bUpdateNotifs()
            && dynaNotifs->m_pfSignalNewUpdate) {
            dynaNotifs->m_pfSignalNewUpdate(dynaNotifs);
        }
    }
    return result;
}

CCRule::CCRule(CCDynaRules* dynaRules)
    : m_pDynaRules(dynaRules)
{
}

CCRule::CCRule(CCRule* rule, CCDynaRules* dynaRules)
    : m_pDynaRules(dynaRules)
{
    if (rule) {
        m_uPeriodStart = rule->m_uPeriodStart;
        m_uOccurrences = rule->m_uOccurrences;
        if ((m_pDaemonExt = rule->m_pDaemonExt)) m_pDaemonExt->AddRef();
        CopyRule(rule);
    }
}

CCRule::~CCRule()
{
    FreeAndNullFormatting(&m_prgdwMsgFormatting);
    FreeAndNullFormatting(&m_prgdwFinalMsgFormatting);
    if (m_pDaemonExt) m_pDaemonExt->Release();
}

void CCRule::AddRef()
{
    ++m_nRefCount;
}

void CCRule::Release()
{
    if (m_nRefCount > 0 && --m_nRefCount == 0) delete this;
}

void CCRule::CopyRule(CCRule* rule)
{
    if (!rule) return;
    m_pEvent = rule->m_pEvent;
    m_pAction = rule->m_pAction;
    m_wFlags = rule->m_wFlags;
    m_uDelay = rule->m_uDelay;
    for (UINT index = 0; index < g_uMaxEventParams; ++index) {
        m_rgkep[index] = rule->m_rgkep[index];
        SetEventParam(index, rule->m_rgstrEventParams[index]);
    }
    for (UINT index = 0; index < g_uMaxActionParams; ++index) {
        m_rgkap[index] = rule->m_rgkap[index];
        m_rgstrActionParams[index] = rule->m_rgstrActionParams[index];
    }
    FreeAndNullFormatting(&m_prgdwMsgFormatting);
    m_prgdwMsgFormatting = CopyFormatting(rule->m_prgdwMsgFormatting);
}

void CCRule::SetMsgFormatting(CDWordArray* formatting, BOOL makeCopy)
{
    FreeAndNullFormatting(&m_prgdwMsgFormatting);
    m_prgdwMsgFormatting = makeCopy ? CopyFormatting(formatting) : formatting;
}

void CCRule::SetEventParam(UINT index, const QString& parameter)
{
    if (index >= g_uMaxEventParams) return;
    m_rgstrEventParams[index] = parameter;
    if (m_pEvent && m_pEvent->m_rgpt[index] == ptNickname
        && m_rgkep[index] == kepMax) {
        QByteArray mask;
        if (!bWideToCodePage(QStringView(parameter), GetACP(), &mask)) {
            mask = parameter.toLatin1();
        }
        bGetUserMatchFromMask(mask.constData(), &m_prUserMatch);
    }
}

QString CCRule::StrParamBeginning(const QString& parameter) const
{
    if (parameter.size() > static_cast<qsizetype>(g_uMaxShortParamLength)) {
        return parameter.left(g_uMaxShortParamLength - 3)
            + QString::fromLatin1(continuation);
    }
    return parameter;
}

QString CCRule::StrGetEventDisplay()
{
    if (!m_pEvent) return {};
    QString result = m_pEvent->m_strShortDesc + QString::fromLatin1(beginParams);
    for (UINT index = 0; index < m_pEvent->m_uParamNum; ++index) {
        if (index) result += QString::fromLatin1(paramSeparator);
        result += QString::fromLatin1(beginParam)
            + StrParamBeginning(m_rgstrEventParams[index])
            + QString::fromLatin1(endParam);
    }
    return result + QString::fromLatin1(endParams);
}

QString CCRule::StrGetActionDisplay()
{
    if (!m_pAction) return {};
    QString result = m_pAction->m_strShortDesc + QString::fromLatin1(beginParams);
    for (UINT index = 0; index < m_pAction->m_uParamNum; ++index) {
        if (index) result += QString::fromLatin1(paramSeparator);
        result += QString::fromLatin1(beginParam)
            + StrParamBeginning(m_rgstrActionParams[index])
            + QString::fromLatin1(endParam);
    }
    return result + QString::fromLatin1(endParams);
}

void CCRule::clearRule()
{
    if (m_pDaemonExt) {
        m_pDaemonExt->Release();
        m_pDaemonExt = nullptr;
    }
    m_wFlags = 0;
    m_uDelay = 0;
    m_pEvent = nullptr;
    m_pAction = nullptr;
    for (UINT index = 0; index < g_uMaxEventParams; ++index) {
        m_rgstrEventParams[index].clear();
        m_rgkep[index] = kepMax;
    }
    for (UINT index = 0; index < g_uMaxActionParams; ++index) {
        m_rgstrActionParams[index].clear();
        m_rgstrActionFinalParams[index].clear();
        m_rgkap[index] = kapMax;
    }
    FreeAndNullFormatting(&m_prgdwMsgFormatting);
    FreeAndNullFormatting(&m_prgdwFinalMsgFormatting);
}

INT CCRule::Serialize(char* buffer, INT bufferLength)
{
    if (!buffer || !m_pDynaRules || !m_pDynaRules->GetRulesData()
        || !m_pEvent || !m_pAction) {
        return -1;
    }

    QByteArray serialized;
    serialized.append(static_cast<char>(g_wVersion));
    appendWord(serialized, 0);
    appendWord(serialized, static_cast<WORD>(m_wFlags & ~g_wStopped));
    serialized.append(static_cast<char>(m_uDelay));
    appendWord(serialized, 0);
    appendWord(serialized, static_cast<WORD>(m_pEvent->m_eID));
    serialized.append(static_cast<char>(m_pEvent->m_uParamNum));

    CCRulesData* data = m_pDynaRules->GetRulesData();
    for (UINT index = 0; index < m_pEvent->m_uParamNum; ++index) {
        if (m_rgkep[index] != kepMax) {
            serialized.append(static_cast<char>(m_rgkep[index]));
        } else {
            serialized.append(static_cast<char>(0xff));
            serialized.append(data->StrFindAndReplaceKeyParams(
                m_rgstrEventParams[index], FALSE).toUtf8());
            serialized.append('\0');
        }
    }

    serialized.append(static_cast<char>(0x01));
    appendWord(serialized, static_cast<WORD>(m_pAction->m_aID));
    appendWord(serialized, 0);
    serialized.append(static_cast<char>(m_pAction->m_uParamNum));
    for (UINT index = 0; index < m_pAction->m_uParamNum; ++index) {
        if (m_rgkap[index] != kapMax) {
            serialized.append(static_cast<char>(m_rgkap[index]));
        } else {
            serialized.append(static_cast<char>(0xff));
            QString parameter = m_rgstrActionParams[index];
            if (rtfParam(m_pAction->m_rgpt[index], m_pAction->m_aID)
                && m_prgdwMsgFormatting) {
                parameter = controlFull(parameter, m_prgdwMsgFormatting);
            }
            serialized.append(data->StrFindAndReplaceKeyParams(
                parameter, FALSE).toUtf8());
            serialized.append('\0');
        }
    }

    if (serialized.size() > bufferLength
        || serialized.size() > static_cast<int>(std::numeric_limits<WORD>::max())) {
        return -1;
    }
    const WORD length = qToLittleEndian(static_cast<WORD>(serialized.size()));
    std::memcpy(serialized.data() + 1, &length, sizeof(length));
    std::memcpy(buffer, serialized.constData(), serialized.size());
    return serialized.size();
}

INT CCRule::UnSerialize(const BYTE* buffer, INT bufferLength)
{
    if (!buffer || !m_pDynaRules || !m_pDynaRules->GetRulesData()) return -1;
    clearRule();
    if (bufferLength < static_cast<INT>(g_uRuleFixedPrefix)) {
        return -bufferLength;
    }

    const BYTE* cursor = buffer;
    int left = bufferLength;
    const BYTE version = *cursor++;
    --left;
    WORD ruleLength = 0;
    if (!readWord(cursor, left, &ruleLength)) return -bufferLength;
    if (!ruleLength || ruleLength > bufferLength || version != g_wVersion) {
        return -static_cast<INT>(ruleLength ? ruleLength : bufferLength);
    }

    WORD value = 0;
    if (!readWord(cursor, left, &value)) goto failure;
    m_wFlags = value & (g_wActive | g_wNoSubsequent | g_wMatchCase | g_wMatchWord);
    if (left < 1) goto failure;
    m_uDelay = *cursor++;
    --left;
    if (!readWord(cursor, left, &value)) goto failure;
    if (!readWord(cursor, left, &value) || value >= eMax) goto failure;
    m_pEvent = m_pDynaRules->GetRulesData()->GetEvent(value);
    if (!m_pEvent || left < 1 || *cursor != m_pEvent->m_uParamNum) goto failure;
    ++cursor;
    --left;

    for (UINT index = 0; index < m_pEvent->m_uParamNum; ++index) {
        if (left < 1) goto failure;
        const BYTE marker = *cursor++;
        --left;
        if (marker != 0xff) {
            if (marker >= kepMax) goto failure;
            m_rgkep[index] = static_cast<enumKeyEventParam>(marker);
            SetEventParam(index,
                m_pDynaRules->GetRulesData()->GetKeyEventParam(m_rgkep[index]));
        } else {
            bool ok = false;
            const QString parameter = readZeroTerminated(cursor, left, &ok);
            if (!ok) goto failure;
            m_rgkep[index] = kepMax;
            SetEventParam(index,
                m_pDynaRules->GetRulesData()->StrFindAndReplaceKeyParams(
                    parameter, TRUE));
        }
    }

    if (left < 6 || *cursor++ != 0x01) goto failure;
    --left;
    if (!readWord(cursor, left, &value) || value >= aMax) goto failure;
    m_pAction = m_pDynaRules->GetRulesData()->GetAction(value);
    if (!m_pAction || !readWord(cursor, left, &value)) goto failure;
    if (left < 1 || *cursor != m_pAction->m_uParamNum) goto failure;
    ++cursor;
    --left;

    for (UINT index = 0; index < m_pAction->m_uParamNum; ++index) {
        if (left < 1) goto failure;
        const BYTE marker = *cursor++;
        --left;
        if (marker != 0xff) {
            if (marker >= kapMax) goto failure;
            m_rgkap[index] = static_cast<enumKeyActionParam>(marker);
            m_rgstrActionParams[index] =
                m_pDynaRules->GetRulesData()->GetKeyActionParam(m_rgkap[index]);
        } else {
            bool ok = false;
            QString parameter = readZeroTerminated(cursor, left, &ok);
            if (!ok) goto failure;
            parameter = m_pDynaRules->GetRulesData()->StrFindAndReplaceKeyParams(
                parameter, TRUE);
            m_rgkap[index] = kapMax;
            if (rtfParam(m_pAction->m_rgpt[index], m_pAction->m_aID)) {
                m_prgdwMsgFormatting = new CDWordArray;
                m_rgstrActionParams[index] =
                    controlLess(parameter, m_prgdwMsgFormatting);
                if (m_rgstrActionParams[index].toUtf8().size()
                    > static_cast<int>(g_uMaxParamLength)) {
                    goto failure;
                }
                if (!m_prgdwMsgFormatting->GetSize()) {
                    FreeAndNullFormatting(&m_prgdwMsgFormatting);
                }
            } else {
                m_rgstrActionParams[index] = parameter;
            }
        }
    }

    for (const RULEX& exception : g_rgex) {
        if (exception.ex == etMinDelay && exception.eID == m_pEvent->GetID()
            && exception.aID == m_pAction->GetID()) {
            if (exception.dwValue > m_uDelay) {
                m_uDelay = static_cast<UCHAR>(exception.dwValue);
            }
            break;
        }
    }

    if (bufferLength - left == ruleLength) {
        InitRuleDaemon();
        return ruleLength;
    }

failure:
    clearRule();
    return -static_cast<INT>(ruleLength);
}

BOOL CCRule::bUnSerialize(const QString& rule)
{
    if (!m_pDynaRules || !m_pDynaRules->GetRulesData()) return FALSE;
    clearRule();
    const QStringList tokens = rule.split(QLatin1Char('|'), Qt::KeepEmptyParts);
    int token = 0;
    bool ok = false;
    auto nextNumber = [&](int maximum, int* result) -> bool {
        if (token >= tokens.size()) return false;
        const int number = tokens.at(token++).toInt(&ok);
        if (!ok || number < 0 || number >= maximum) return false;
        *result = number;
        return true;
    };

    if (token >= tokens.size()) return FALSE;
    const int flags = tokens.at(token++).toInt(&ok);
    if (!ok || flags & ~(g_wActive | g_wNoSubsequent | g_wMatchCase | g_wMatchWord)) {
        clearRule();
        return FALSE;
    }
    m_wFlags = static_cast<WORD>(flags);
    int index = 0;
    if (!nextNumber(eMax, &index)) goto textFailure;
    m_pEvent = m_pDynaRules->GetRulesData()->GetEvent(index);
    if (!nextNumber(aMax, &index)) goto textFailure;
    m_pAction = m_pDynaRules->GetRulesData()->GetAction(index);
    if (!m_pEvent || !m_pAction) goto textFailure;

    for (UINT parameter = 0; parameter < m_pEvent->m_uParamNum; ++parameter) {
        if (token >= tokens.size()) goto textFailure;
        const QString value = tokens.at(token++);
        if (value.startsWith(QLatin1Char('$'))) {
            const int key = value.mid(1).toInt(&ok);
            if (!ok || key < 0 || key >= kepMax) goto textFailure;
            m_rgkep[parameter] = static_cast<enumKeyEventParam>(key);
            SetEventParam(parameter,
                m_pDynaRules->GetRulesData()->GetKeyEventParam(m_rgkep[parameter]));
        } else {
            m_rgkep[parameter] = kepMax;
            SetEventParam(parameter,
                m_pDynaRules->GetRulesData()->StrFindAndReplaceKeyParams(value, TRUE));
        }
    }
    for (UINT parameter = 0; parameter < m_pAction->m_uParamNum; ++parameter) {
        if (token >= tokens.size()) goto textFailure;
        const QString value = tokens.at(token++);
        if (value.startsWith(QLatin1Char('$'))) {
            const int key = value.mid(1).toInt(&ok);
            if (!ok || key < 0 || key >= kapMax) goto textFailure;
            m_rgkap[parameter] = static_cast<enumKeyActionParam>(key);
            m_rgstrActionParams[parameter] =
                m_pDynaRules->GetRulesData()->GetKeyActionParam(m_rgkap[parameter]);
        } else {
            m_rgkap[parameter] = kapMax;
            const QString stored =
                m_pDynaRules->GetRulesData()->StrFindAndReplaceKeyParams(value, TRUE);
            if (rtfParam(m_pAction->m_rgpt[parameter], m_pAction->m_aID)) {
                m_prgdwMsgFormatting = new CDWordArray;
                m_rgstrActionParams[parameter] =
                    controlLess(stored, m_prgdwMsgFormatting);
                if (!m_prgdwMsgFormatting->GetSize()) {
                    FreeAndNullFormatting(&m_prgdwMsgFormatting);
                }
            } else {
                m_rgstrActionParams[parameter] = stored;
            }
        }
    }
    InitRuleDaemon();
    return TRUE;

textFailure:
    clearRule();
    return FALSE;
}

INT CCRule::iGetHighlightTypeIndex(QString parameter)
{
    const QString format = originalResourceString(QStringLiteral("IDS_HIGHLIGHT_TYPE"));
    for (INT index = 0; index < NHIGHLIGHTEDFONTS / 2; ++index) {
        QString type = format;
        type.replace(QStringLiteral("%d"), QString::number(index + 1));
        if (type.compare(parameter, Qt::CaseInsensitive) == 0) return index;
    }
    return -1;
}

BOOL CCRule::bValidateRuleEvent(UINT index, QString&, UINT* errorID)
{
    if (!m_pEvent || !m_pAction) return FALSE;
    if ((m_pEvent->m_eID == eOnConnect || m_pEvent->m_eID == eOnDisconnect)
        && index == 0) {
        const PRUSERMATCH& match = m_prUserMatch;
        if (!match.cbNickname && !match.cbUserName && !match.cbIPAddress) {
            if (errorID) *errorID = IDS_ERR_MATCHALL;
            return FALSE;
        }
    }
    if (errorID) *errorID = 0;
    return TRUE;
}

BOOL CCRule::bValidateRuleAction(UINT index, QString& parameter, UINT* errorID)
{
    if (!m_pEvent || !m_pAction) return FALSE;
    if ((m_pAction->m_aID == aSendFileLine
         || m_pAction->m_aID == aWhisperFileLine) && index == 2) {
        QByteArray ranges = parameter.toLatin1();
        char* range = ranges.data();
        while (range && *range) {
            UINT minimum = 0;
            UINT maximum = 0;
            if (!bGetNextRange(&range, &minimum, &maximum)
                || !minimum || !maximum) {
                if (errorID) *errorID = IDS_ERR_FILELINERANGE;
                return FALSE;
            }
        }
    } else if (m_pAction->m_aID == aActivateRuleSet && index == 1) {
        if (errorID) *errorID = IDS_ERR_NOBOOLEAN;
        return FALSE;
    } else if (m_pAction->m_aID == aHighlightMessage && index == 0
               && iGetHighlightTypeIndex(parameter) < 0) {
        if (errorID) *errorID = IDS_ERR_NOHIGHLIGHT;
        return FALSE;
    }
    if (errorID) *errorID = 0;
    return TRUE;
}

BOOL CCRule::bDaemonNeeded() const
{
    if (!m_pEvent || !m_pAction || !bActive() || bStopped()
        || !m_pEvent->m_bNeedDaemon) {
        return FALSE;
    }
    if (m_pEvent->m_eID == eOnConnect || m_pEvent->m_eID == eOnDisconnect) {
        return m_rgkep[0] != kepMe;
    }
    return m_pEvent->m_eID == eOnNewRoom;
}

void CCRule::InitRuleDaemon()
{
    if (m_pDaemonExt) {
        m_pDaemonExt->Release();
        m_pDaemonExt = nullptr;
    }
    if (!m_pEvent || !m_pEvent->m_bNeedDaemon) return;

    enumItemTypes itemType = itMax;
    if (m_pEvent->m_eID == eOnConnect
        || m_pEvent->m_eID == eOnDisconnect) {
        itemType = itUser;
    } else if (m_pEvent->m_eID == eOnNewRoom) {
        itemType = itChannel;
    } else {
        return;
    }
    m_pDaemonExt = new CCDaemonExt(itemType);
    if (!m_pDaemonExt->bAllocNewItemList(2)) {
        m_pDaemonExt->Release();
        m_pDaemonExt = nullptr;
    }
}

BOOL CCRule::bUpdateDaemonExt(BOOL resetItemLists, enumEvents event)
{
    if (bDaemonNeeded()) {
        if (m_pDaemonExt) {
            if (resetItemLists || m_pDaemonExt->m_bResetItemLists) {
                m_pDaemonExt->m_bResetItemLists = FALSE;
                resetItemLists = TRUE;
                m_pDaemonExt->bCleanUpItemLists();
            }
        } else {
            enumItemTypes itemType = itMax;
            if (event == eOnConnect || event == eOnDisconnect)
                itemType = itUser;
            else if (event == eOnNewRoom)
                itemType = itChannel;
            if (itemType == itMax) return FALSE;
            m_pDaemonExt = new CCDaemonExt(itemType);
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

BOOL CCRule::bIsFlooding()
{
    if (!m_pDynaRules) return FALSE;
    const USHORT now = static_cast<USHORT>(std::time(nullptr) & 0xffff);
    const USHORT interval = static_cast<USHORT>(
        std::abs(static_cast<int>(now) - static_cast<int>(m_uPeriodStart)));
    if (interval > m_pDynaRules->GetFloodingInterval()) {
        m_uPeriodStart = now;
        m_uOccurrences = 1;
        return FALSE;
    }
    return !(++m_uOccurrences <= m_pDynaRules->GetFloodingOccurrences());
}

CCRuleSet::CCRuleSet(CCDynaRules* dynaRules)
    : m_pDynaRules(dynaRules)
{
}

CCRuleSet::CCRuleSet(CCRuleSet* ruleSet, CCDynaRules* dynaRules)
    : m_pDynaRules(dynaRules)
{
    if (!ruleSet) return;
    for (CCRule* rule : ruleSet->m_rgpRules) {
        m_rgpRules.append(new CCRule(rule, dynaRules));
    }
    m_strSetName = ruleSet->m_strSetName;
    m_wFlags = ruleSet->m_wFlags;
}

CCRuleSet::~CCRuleSet()
{
    CleanUpRulesArray();
}

void CCRuleSet::CleanUpRulesArray()
{
    for (CCRule* rule : m_rgpRules) {
        if (rule) {
            rule->Desactivate();
            rule->Release();
        }
    }
    m_rgpRules.clear();
}

BOOL CCRuleSet::bAddRule(CCRule* rule, INT index)
{
    if (!rule) return FALSE;
    if (index < 0 || index >= m_rgpRules.size()) m_rgpRules.append(rule);
    else m_rgpRules.insert(index, rule);
    return TRUE;
}

BOOL CCRuleSet::bRemoveRule(CCRule* rule, INT index)
{
    if (index < 0) index = m_rgpRules.indexOf(rule);
    if (index < 0 || index >= m_rgpRules.size()) return FALSE;
    rule = m_rgpRules.at(index);
    rule->Desactivate();
    rule->Release();
    m_rgpRules.removeAt(index);
    return TRUE;
}

BOOL CCRuleSet::bDuplicateRule(INT index, CCRule** result)
{
    if (!result || index < 0 || index >= m_rgpRules.size()) return FALSE;
    *result = new CCRule(m_pDynaRules);
    (*result)->CopyRule(m_rgpRules.at(index));
    return bAddRule(*result, index + 1);
}

BOOL CCRuleSet::bUpRule(CCRule* rule, INT index)
{
    if (index < 0) index = m_rgpRules.indexOf(rule);
    if (index < 0 || index >= m_rgpRules.size()) return FALSE;
    if (index > 0) m_rgpRules.swapItemsAt(index, index - 1);
    return TRUE;
}

BOOL CCRuleSet::bDownRule(CCRule* rule, INT index)
{
    if (index < 0) index = m_rgpRules.indexOf(rule);
    if (index < 0 || index >= m_rgpRules.size()) return FALSE;
    if (index + 1 < m_rgpRules.size()) m_rgpRules.swapItemsAt(index, index + 1);
    return TRUE;
}

BOOL CCRuleSet::bSaveToFile(const QString& fileName, UINT* error)
{
    if (error) *error = 0;
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = g_uErrFormat;
        return FALSE;
    }

    QByteArray data;
    appendDWord(data, MAKELONG(m_wFlags & g_wActive, g_wVersion));
    appendDWord(data, 0);
    data.append(m_strSetName.toUtf8());
    data.append('\0');
    std::array<char, g_uMaxSerializedRule> buffer{};
    for (CCRule* rule : m_rgpRules) {
        const INT length = rule ? rule->Serialize(buffer.data(), buffer.size()) : -1;
        if (length <= 0) {
            if (error) *error = g_uErrFormat;
            file.close();
            return FALSE;
        }
        data.append(buffer.data(), length);
    }
    if (file.write(data) != data.size()) {
        if (error) *error = g_uErrFormat;
        file.close();
        return FALSE;
    }
    return file.flush();
}

BOOL CCRuleSet::bLoadFromFile(const QString& fileName, UINT* error)
{
    if (error) *error = 0;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = g_uErrFormat;
        return FALSE;
    }
    const QByteArray bytes = file.readAll();
    const BYTE* cursor = reinterpret_cast<const BYTE*>(bytes.constData());
    int left = bytes.size();
    DWORD header = 0;
    if (!readDWord(cursor, left, &header)) goto fileFailure;
    if (HIWORD(header) != g_wVersion) {
        if (error) *error = g_uErrVersion;
        goto fileFailure;
    }
    m_wFlags = LOWORD(header) & g_wActive;
    if (!readDWord(cursor, left, &header)) goto fileFailure;

    {
        int length = 0;
        while (length < left && length <= static_cast<int>(g_uMaxSetNameLength)
               && cursor[length] != 0) {
            ++length;
        }
        if (length >= left || length > static_cast<int>(g_uMaxSetNameLength)
            || cursor[length] != 0) {
            goto fileFailure;
        }
        m_strSetName = QString::fromUtf8(
            reinterpret_cast<const char*>(cursor), length);
        cursor += length + 1;
        left -= length + 1;
    }

    {
        BOOL skipped = FALSE;
        while (left > 0) {
            auto* rule = new CCRule(m_pDynaRules);
            INT length = rule->UnSerialize(cursor, left);
            if (length > 0) {
                bAddRule(rule);
            } else {
                skipped = TRUE;
                length = -length;
                rule->Release();
                if (length <= 0 || length > left) goto fileFailure;
            }
            cursor += length;
            left -= length;
        }
        if (error && skipped) *error = g_uErrRulesSkipped;
    }
    return TRUE;

fileFailure:
    if (error && !*error) *error = g_uErrFormat;
    CleanUpRulesArray();
    return FALSE;
}

BOOL CCRuleSet::bDaemonNeeded() const
{
    for (CCRule* rule : m_rgpRules) {
        if (rule && rule->bDaemonNeeded()) return TRUE;
    }
    return FALSE;
}

BOOL CCRuleSet::bUpdateRulesDaemonExt(BOOL resetItemLists)
{
    BOOL result = TRUE;
    for (CCRule* rule : m_rgpRules) {
        if (rule && rule->GetEvent()) {
            result &= rule->bUpdateDaemonExt(
                resetItemLists, rule->GetEvent()->GetID());
        }
    }
    return result;
}

CCDynaRules::CCDynaRules() = default;

CCDynaRules::~CCDynaRules()
{
    bStopRulesDaemon();
    delete m_rulesDaemonTimer;
    m_rulesDaemonTimer = nullptr;
    CleanUpRuleSetsArray();
    FreeAndNullFormatting(&m_prgdwMsgFormattingCach);
}

const CCDynaRules& CCDynaRules::operator=(const CCDynaRules& source)
{
    if (this == &source) return *this;
    CleanUpRuleSetsArray();
    m_pSelectedRuleSet = nullptr;
    for (CCRuleSet* ruleSet : source.m_rgpRuleSets) {
        auto* copy = new CCRuleSet(ruleSet, this);
        m_rgpRuleSets.append(copy);
        if (source.m_pSelectedRuleSet == ruleSet) m_pSelectedRuleSet = copy;
    }
    FreeAndNullFormatting(&m_prgdwMsgFormattingCach);
    m_bDaemonRunning = FALSE;
    m_pfEventKeyParam = source.m_pfEventKeyParam;
    m_pfEventRndParam = source.m_pfEventRndParam;
    m_pfGetKeyEventParam = source.m_pfGetKeyEventParam;
    m_pfGetKeyActionParam = source.m_pfGetKeyActionParam;
    m_pfExecuteAction = source.m_pfExecuteAction;
    m_pfRuleFailure = source.m_pfRuleFailure;
    m_pfDaemonQuery = source.m_pfDaemonQuery;
    m_pRulesData = source.m_pRulesData;
    m_pDelayedRules = source.m_pDelayedRules;
    m_eIDCach = source.m_eIDCach;
    m_strIdentityCach = source.m_strIdentityCach;
    m_strServerCach = source.m_strServerCach;
    m_strChannelCach = source.m_strChannelCach;
    m_strRecipientsCach = source.m_strRecipientsCach;
    m_strCLMessageCach = source.m_strCLMessageCach;
    m_strCFMessageCach = source.m_strCFMessageCach;
    m_prgdwMsgFormattingCach =
        CopyFormatting(source.m_prgdwMsgFormattingCach);
    m_paApprovedIDsCach = nullptr;
    m_paRejectedIDsCach = nullptr;
    m_wFlags = source.m_wFlags;
    m_strCFFinalMessage = source.m_strCFFinalMessage;
    m_uFloodInterval = source.m_uFloodInterval;
    m_uFloodOccurrences = source.m_uFloodOccurrences;
    return *this;
}

void CCDynaRules::SetCachVariables(enumEvents event, QString& identity,
                                   QString& server, QString& channel)
{
    m_eIDCach = event;
    m_paApprovedIDsCach = nullptr;
    m_paRejectedIDsCach = nullptr;
    m_strIdentityCach = identity;
    m_strServerCach = server;
    m_strChannelCach = channel;
    m_strRecipientsCach.clear();
    m_strCFMessageCach.clear();
    m_strCLMessageCach.clear();
    FreeAndNullFormatting(&m_prgdwMsgFormattingCach);
}

void CCDynaRules::CleanUpRuleSetsArray()
{
    qDeleteAll(m_rgpRuleSets);
    m_rgpRuleSets.clear();
    m_pSelectedRuleSet = nullptr;
}

BOOL CCDynaRules::bUpdateRuleSetsDaemonExt(BOOL resetItemLists)
{
    BOOL result = TRUE;
    for (CCRuleSet* ruleSet : m_rgpRuleSets) {
        if (ruleSet) {
            result &= ruleSet->bUpdateRulesDaemonExt(resetItemLists);
        }
    }
    return result;
}

BOOL CCDynaRules::bAddRuleSet(CCRuleSet* ruleSet, INT index)
{
    if (!ruleSet) return FALSE;
    if (index < 0 || index >= m_rgpRuleSets.size()) m_rgpRuleSets.append(ruleSet);
    else m_rgpRuleSets.insert(index, ruleSet);
    return TRUE;
}

BOOL CCDynaRules::bRemoveRuleSet(CCRuleSet* ruleSet, INT index)
{
    if (index < 0) index = m_rgpRuleSets.indexOf(ruleSet);
    if (index < 0 || index >= m_rgpRuleSets.size()) return FALSE;
    ruleSet = m_rgpRuleSets.takeAt(index);
    if (m_pSelectedRuleSet == ruleSet) m_pSelectedRuleSet = nullptr;
    delete ruleSet;
    return TRUE;
}

BOOL CCDynaRules::bUpRuleSet(CCRuleSet* ruleSet, INT index)
{
    if (index < 0) index = m_rgpRuleSets.indexOf(ruleSet);
    if (index < 0 || index >= m_rgpRuleSets.size()) return FALSE;
    if (index > 0) m_rgpRuleSets.swapItemsAt(index, index - 1);
    return TRUE;
}

BOOL CCDynaRules::bDownRuleSet(CCRuleSet* ruleSet, INT index)
{
    if (index < 0) index = m_rgpRuleSets.indexOf(ruleSet);
    if (index < 0 || index >= m_rgpRuleSets.size()) return FALSE;
    if (index + 1 < m_rgpRuleSets.size()) {
        m_rgpRuleSets.swapItemsAt(index, index + 1);
    }
    return TRUE;
}

CCRuleSet* CCDynaRules::GetRuleSetFromName(const QString& setName)
{
    for (INT index = m_rgpRuleSets.size() - 1; index >= 0; --index) {
        CCRuleSet* ruleSet = m_rgpRuleSets.at(index);
        if (ruleSet->GetName().compare(setName, Qt::CaseInsensitive) == 0) {
            return ruleSet;
        }
    }
    return nullptr;
}

BOOL CCDynaRules::bReplaceKeyEventParams(QString& eventParameter)
{
    if (!m_pRulesData || !m_pfGetKeyEventParam) return FALSE;
    const enumKeyEventParam keys[] = {kepMe, kepMyActivatedRoom};
    for (enumKeyEventParam key : keys) {
        replaceCaseInsensitive(eventParameter, m_pRulesData->GetKeyEventParam(key),
            m_pfGetKeyEventParam(key == kepMe ? ptNickname : ptRoomName));
    }
    return TRUE;
}

BOOL CCDynaRules::bReplaceKeyActionParams(CCRule* rule)
{
    if (!rule || !rule->m_pAction || !m_pRulesData
        || !m_pfGetKeyEventParam || !m_pfGetKeyActionParam) {
        return FALSE;
    }
    FreeAndNullFormatting(&rule->m_prgdwFinalMsgFormatting);

    for (UINT parameter = 0; parameter < rule->m_pAction->m_uParamNum;
         ++parameter) {
        const bool formattedParameter = rtfParam(
            rule->m_pAction->m_rgpt[parameter], rule->m_pAction->m_aID);
        QString result = formattedParameter && rule->m_prgdwMsgFormatting
            ? controlFull(rule->m_rgstrActionParams[parameter],
                          rule->m_prgdwMsgFormatting)
            : rule->m_rgstrActionParams[parameter];

        replaceCaseInsensitive(result,
            m_pRulesData->GetKeyEventParam(kepMe),
            m_pfGetKeyEventParam(ptNickname));

        for (UINT keyIndex = 0; keyIndex < kapMax; ++keyIndex) {
            const auto key = static_cast<enumKeyActionParam>(keyIndex);
            if (key == kapYes || key == kapNo) continue;
            const bool preserveEventFormatting = key == kapEventMessage
                && rule->m_pAction->m_rgpt[parameter] == ptMessage
                && rule->m_pAction->m_aID != aNotifyDialog;
            if (preserveEventFormatting) {
                CDWordArray templateFormatting;
                QByteArray templateFull = result.toUtf8();
                char* templatePlain = SzControlLess(
                    templateFull.data(), &templateFormatting);
                const QByteArray what =
                    m_pRulesData->GetKeyActionParam(kapEventMessage).toUtf8();
                const QByteArray replacement = m_strCLMessageCach.toUtf8();
                char* replaced = SzReplaceFormattedString(
                    what.constData(), replacement.constData(), templatePlain,
                    m_prgdwMsgFormattingCach, &templateFormatting, 0);
                if (!replaced) return FALSE;
                result = QString::fromUtf8(replaced);
                delete[] replaced;
            } else {
                QString server = m_strServerCach;
                QString identity = m_strIdentityCach;
                QString channel = m_strChannelCach;
                QString recipients = m_strRecipientsCach;
                QString message = m_strCLMessageCach;
                replaceCaseInsensitive(result, m_pRulesData->GetKeyActionParam(key),
                    m_pfGetKeyActionParam(key, server, identity, channel,
                                          recipients, message));
            }
        }

        if (formattedParameter) {
            if (!rule->m_prgdwFinalMsgFormatting) {
                rule->m_prgdwFinalMsgFormatting = new CDWordArray;
            }
            rule->m_rgstrActionFinalParams[parameter] =
                controlLess(result, rule->m_prgdwFinalMsgFormatting);
        } else {
            rule->m_rgstrActionFinalParams[parameter] = result;
        }
    }
    return TRUE;
}

BOOL CCDynaRules::bInActionIDs(enumActions* actions, enumActions action) const
{
    if (!actions) return FALSE;
    const UINT count = static_cast<UINT>(actions[0]);
    for (UINT index = 1; index <= count; ++index) {
        if (actions[index] == action) return TRUE;
    }
    return FALSE;
}

BOOL CCDynaRules::bRuleFilteredOut(CCRule* rule) const
{
    if (!rule || !rule->m_pAction) return TRUE;
    if (m_paApprovedIDsCach
        && !bInActionIDs(m_paApprovedIDsCach, rule->m_pAction->m_aID)) {
        return TRUE;
    }
    return m_paRejectedIDsCach
        && bInActionIDs(m_paRejectedIDsCach, rule->m_pAction->m_aID);
}

BOOL CCDynaRules::bMatchingRule(CCRule* rule)
{
    if (!rule || !rule->m_pEvent || !rule->m_pAction
        || m_eIDCach != rule->m_pEvent->m_eID
        || !rule->bActive() || rule->bStopped()) {
        return FALSE;
    }

    for (UINT index = 0; index < rule->m_pEvent->m_uParamNum; ++index) {
        QString* value = nullptr;
        PPRUSERMATCH userMatch = nullptr;
        const enumParamType type = rule->m_pEvent->m_rgpt[index];
        switch (type) {
        case ptRoomName:
            value = &m_strChannelCach;
            break;
        case ptMessage:
            value = &m_strCLMessageCach;
            break;
        case ptNickname:
            value = &m_strIdentityCach;
            userMatch = &rule->m_prUserMatch;
            break;
        case ptServerName:
            value = &m_strServerCach;
            break;
        default:
            return FALSE;
        }

        BOOL passes = FALSE;
        if (rule->m_rgkep[index] != kepMax) {
            if (!m_pfEventKeyParam) return FALSE;
            passes = m_pfEventKeyParam(*value, rule->m_rgkep[index]);
        } else {
            if (!m_pfEventRndParam || rule->m_rgstrEventParams[index].isEmpty()) {
                return FALSE;
            }
            QString filter = rule->m_rgstrEventParams[index];
            if (type == ptMessage) bReplaceKeyEventParams(filter);
            passes = m_pfEventRndParam(*value, filter, userMatch,
                                       rule->m_wFlags, type);
        }
        if (!passes) return FALSE;
    }
    return TRUE;
}

INT CCDynaRules::iGetFirstMatchingRule(INT* ruleSetIndex, enumEvents event,
                                       enumActions* approvedIDs,
                                       enumActions* rejectedIDs,
                                       QString& server, QString& identity,
                                       QString& channel, QString& message,
                                       CCRule** result)
{
    if (!ruleSetIndex) return -1;
    m_eIDCach = event;
    m_paApprovedIDsCach = approvedIDs;
    m_paRejectedIDsCach = rejectedIDs;
    m_strIdentityCach = identity;
    m_strServerCach = server;
    m_strChannelCach = channel;
    m_strCLMessageCach = m_strCFMessageCach = message;
    if (!message.isEmpty()) {
        if (!m_prgdwMsgFormattingCach) {
            m_prgdwMsgFormattingCach = new CDWordArray;
        }
        m_strCLMessageCach = controlLess(message, m_prgdwMsgFormattingCach);
    }
    if (result) *result = nullptr;

    BOOL stop = FALSE;
    for (INT setIndex = 0; setIndex < m_rgpRuleSets.size() && !stop; ++setIndex) {
        CCRuleSet* ruleSet = m_rgpRuleSets.at(setIndex);
        if (!ruleSet->bActive()) continue;
        for (INT ruleIndex = 0; ruleIndex < ruleSet->m_rgpRules.size() && !stop;
             ++ruleIndex) {
            CCRule* rule = ruleSet->m_rgpRules.at(ruleIndex);
            if (!bMatchingRule(rule)) continue;
            if (bRuleFilteredOut(rule)) {
                stop = rule->m_wFlags & g_wNoSubsequent;
                continue;
            }
            if (result) *result = rule;
            *ruleSetIndex = setIndex;
            return ruleIndex;
        }
    }
    *ruleSetIndex = -1;
    return -1;
}

INT CCDynaRules::iGetNextMatchingRule(INT* previousRuleSet, INT previousRule,
                                      CCRule** result)
{
    if (!previousRuleSet || *previousRuleSet < 0
        || *previousRuleSet >= m_rgpRuleSets.size()) {
        return -1;
    }
    if (result) *result = nullptr;
    BOOL stop = FALSE;
    INT startRule = previousRule + 1;
    for (INT setIndex = *previousRuleSet;
         setIndex < m_rgpRuleSets.size() && !stop; ++setIndex) {
        CCRuleSet* ruleSet = m_rgpRuleSets.at(setIndex);
        if (ruleSet->bActive()) {
            for (INT ruleIndex = startRule;
                 ruleIndex < ruleSet->m_rgpRules.size() && !stop; ++ruleIndex) {
                CCRule* rule = ruleSet->m_rgpRules.at(ruleIndex);
                if (!bMatchingRule(rule)) continue;
                if (bRuleFilteredOut(rule)) {
                    stop = rule->m_wFlags & g_wNoSubsequent;
                    continue;
                }
                if (result) *result = rule;
                *previousRuleSet = setIndex;
                return ruleIndex;
            }
        }
        startRule = 0;
    }
    return -1;
}

BOOL CCDynaRules::executeMatchingRule(INT ruleSetIndex, CCRule* rule)
{
    if (!rule) return FALSE;
    if (rule->bIsFlooding()) {
        rule->SetFlags(rule->wGetFlags() | g_wStopped);
        if (m_pfRuleFailure && ruleSetIndex >= 0
            && ruleSetIndex < m_rgpRuleSets.size()) {
            m_pfRuleFailure(m_rgpRuleSets.at(ruleSetIndex), rule, g_uErrFlooding);
        }
        return TRUE;
    }
    if (!bReplaceKeyActionParams(rule)) return FALSE;
    if (!m_pfExecuteAction) return FALSE;
    if (!rule->GetDelay()) {
        CCActionContext context;
        context.bInitActionContext(this, rule);
        m_pfExecuteAction(this, rule, &context);
    } else {
        auto* context = new CCActionContext;
        context->bInitActionContext(this, rule);
        if (!m_pDelayedRules || !m_pDelayedRules->bAddActionCtx(context)) {
            delete context;
            return FALSE;
        }
    }
    return TRUE;
}

BOOL CCDynaRules::bMatchAndApplyRules(enumEvents event, enumActions* approvedIDs,
                                      enumActions* rejectedIDs, QString& server,
                                      QString& identity, QString& channel,
                                      QString& message)
{
    ResetFlags();
    INT ruleSetIndex = -1;
    CCRule* rule = nullptr;
    INT ruleIndex = iGetFirstMatchingRule(&ruleSetIndex, event, approvedIDs,
        rejectedIDs, server, identity, channel, message, &rule);
    if (ruleIndex < 0) return TRUE;
    executeMatchingRule(ruleSetIndex, rule);
    while (!(rule->m_wFlags & g_wNoSubsequent)
           && (ruleIndex = iGetNextMatchingRule(
                   &ruleSetIndex, ruleIndex, &rule)) >= 0) {
        executeMatchingRule(ruleSetIndex, rule);
    }
    return TRUE;
}

BOOL CCDynaRules::bReplaceMessage(CCRule* rule)
{
    if (!rule || !rule->m_pEvent || !rule->m_pAction) return FALSE;
    UINT eventParameter = 0;
    while (eventParameter < rule->m_pEvent->m_uParamNum
           && rule->m_pEvent->m_rgpt[eventParameter] != ptMessage) {
        ++eventParameter;
    }
    UINT actionParameter = 0;
    while (actionParameter < rule->m_pAction->m_uParamNum
           && rule->m_pAction->m_rgpt[actionParameter] != ptMessage) {
        ++actionParameter;
    }
    if (eventParameter >= rule->m_pEvent->m_uParamNum
        || actionParameter >= rule->m_pAction->m_uParamNum) {
        return FALSE;
    }
    const QString replaceWhat = rule->m_rgkep[eventParameter] == kepMax
        ? rule->m_rgstrEventParams[eventParameter] : m_strCLMessageCach;
    const QString replaceBy = rule->m_rgstrActionFinalParams[actionParameter];
    UINT flags = 0;
    if (rule->m_wFlags & g_wMatchCase) flags |= 0x00000004U;
    if (rule->m_wFlags & g_wMatchWord) flags |= 0x00000002U;
    const QByteArray what = replaceWhat.toUtf8();
    const QByteArray by = replaceBy.toUtf8();
    const QByteArray in = m_strCLMessageCach.toUtf8();
    char* replaced = SzReplaceFormattedString(
        what.constData(), by.constData(), in.constData(),
        rule->m_prgdwFinalMsgFormatting, m_prgdwMsgFormattingCach, flags);
    if (!replaced) return FALSE;
    m_strCFFinalMessage = QString::fromUtf8(replaced);
    delete[] replaced;
    AddFlag(g_wReplace);
    return TRUE;
}

BOOL CCDynaRules::bLoadRulesFromResource()
{
    if (!m_pRulesData || !m_pRulesData->bInitAlloc()
        || !m_pRulesData->bLoadStrings()) {
        return FALSE;
    }
    const QString setResources[] = {
        QStringLiteral("IDS_SAMPLES_RULESET"),
        QStringLiteral("IDS_GENERAL_RULESET")
    };
    BOOL result = TRUE;
    for (INT setIndex = 0; setIndex < 2; ++setIndex) {
        const QString stored = originalResourceString(setResources[setIndex]);
        const qsizetype separator = stored.indexOf(QLatin1Char('|'));
        bool ok = false;
        const int flags = stored.left(separator).toInt(&ok);
        if (separator < 0 || !ok || flags & ~g_wActive) return FALSE;
        const QString name = stored.mid(separator + 1);
        if (GetRuleSetFromName(name)) continue;
        auto* ruleSet = new CCRuleSet(this);
        ruleSet->m_strSetName = name;
        ruleSet->m_wFlags = static_cast<WORD>(flags);
        if (setIndex == 0) {
            for (INT ruleIndex = 1; ruleIndex <= 7; ++ruleIndex) {
                const QString storedRule = originalResourceString(
                    QStringLiteral("IDS_SAMPLES_RULE%1").arg(ruleIndex));
                if (storedRule.isEmpty()) continue;
                auto* rule = new CCRule(this);
                if (!rule->bUnSerialize(storedRule)
                    || !ruleSet->bAddRule(rule)) {
                    rule->Release();
                }
            }
        }
        result &= bAddRuleSet(ruleSet);
    }
    return result;
}

BOOL CCDynaRules::bSaveRulesToReg()
{
    QSettings settings = originalUserSettings();
    settings.beginGroup(originalSettingsSubKey(g_szRuleSetsSubKey));

    // RegEnumKeyEx/RegDeleteKey remove only existing RuleSet subkeys. Values
    // directly on the RuleSets key, although not produced by source, remain.
    const QStringList previousRuleSets = settings.childGroups();
    for (const QString& ruleSetName : previousRuleSets) {
        settings.remove(ruleSetName);
    }

    std::array<char, g_uMaxSerializedRule> buffer{};
    for (CCRuleSet* ruleSet : m_rgpRuleSets) {
        if (!ruleSet) continue;
        settings.beginGroup(ruleSet->m_strSetName);
        settings.setValue(QString::fromLatin1(g_szRuleSetFlags),
                          static_cast<quint32>(MAKELONG(
                              ruleSet->m_wFlags & g_wActive, g_wVersion)));

        for (INT index = 0; index < ruleSet->m_rgpRules.size(); ++index) {
            CCRule* rule = ruleSet->m_rgpRules.at(index);
            if (!rule) continue;
            const INT length = rule->Serialize(buffer.data(), buffer.size());
            if (length > 0) {
                settings.setValue(QString::number(index),
                                  QByteArray(buffer.data(), length));
            }
        }
        settings.endGroup();
    }

    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

BOOL CCDynaRules::bLoadRulesFromReg()
{
    if (!m_pRulesData) return FALSE;

    QSettings settings = originalUserSettings();
    settings.beginGroup(originalSettingsSubKey(g_szRuleSetsSubKey));
    const QStringList ruleSetNames = settings.childGroups();
    if (ruleSetNames.isEmpty()) {
        settings.endGroup();
        // RegOpenKeyEx failure is deliberately reported as success by source.
        return TRUE;
    }

    if (!m_pRulesData->bInitAlloc() || !m_pRulesData->bLoadStrings()) {
        settings.endGroup();
        return FALSE;
    }

    for (const QString& ruleSetName : ruleSetNames) {
        settings.beginGroup(ruleSetName);
        if (GetRuleSetFromName(ruleSetName)) {
            settings.endGroup();
            continue;
        }

        auto* ruleSet = new CCRuleSet(this);
        ruleSet->m_strSetName = ruleSetName;
        CCRule* pendingRule = nullptr;

        const QStringList valueNames = settings.childKeys();
        for (const QString& valueName : valueNames) {
            const QVariant stored = settings.value(valueName);
            if (valueName == QString::fromLatin1(g_szRuleSetFlags)) {
                bool ok = false;
                const DWORD flags = stored.toUInt(&ok);
                if (!ok || HIWORD(flags) != g_wVersion) {
                    delete ruleSet;
                    ruleSet = nullptr;
                    break;
                }
                ruleSet->m_wFlags = LOWORD(flags) & g_wActive;
                continue;
            }

            if (stored.metaType().id() != QMetaType::QByteArray) continue;
            const QByteArray bytes = stored.toByteArray();
            if (bytes.size() > static_cast<qsizetype>(g_uMaxSerializedRule)) {
                continue;
            }
            if (!pendingRule) pendingRule = new CCRule(this);
            if (pendingRule->UnSerialize(
                    reinterpret_cast<const BYTE*>(bytes.constData()),
                    bytes.size()) > 0) {
                ruleSet->bAddRule(pendingRule);
                pendingRule = nullptr;
            }
        }

        if (pendingRule) pendingRule->Release();
        if (ruleSet) bAddRuleSet(ruleSet);
        settings.endGroup();
    }

    settings.endGroup();
    return TRUE;
}

BOOL CCDynaRules::bDaemonNeeded() const
{
    for (CCRuleSet* ruleSet : m_rgpRuleSets) {
        if (ruleSet && ruleSet->bDaemonNeeded()) return TRUE;
    }
    return FALSE;
}

BOOL CCDynaRules::bStartRulesDaemon(UINT elapse, BOOL forceReset)
{
    if (m_bDaemonRunning && !forceReset) return TRUE;
    if (!QCoreApplication::instance()) return FALSE;
    bStopRulesDaemon();
    if (!m_rulesDaemonTimer) {
        m_rulesDaemonTimer = new QTimer;
        QObject::connect(m_rulesDaemonTimer, &QTimer::timeout,
                         [this] { OnRulesDaemonTimer(); });
    }
    m_rulesDaemonTimer->setInterval(static_cast<int>(elapse) * 1000);
    m_rulesDaemonTimer->start();
    m_bDaemonRunning = m_rulesDaemonTimer->isActive();
    return m_bDaemonRunning;
}

BOOL CCDynaRules::bStopRulesDaemon()
{
    if (m_rulesDaemonTimer) m_rulesDaemonTimer->stop();
    m_bDaemonRunning = FALSE;
    return TRUE;
}

void CCDynaRules::OnRulesDaemonTimer()
{
    if (!m_bDaemonRunning) return;
    bStartRulesDaemon(g_uRulesDaemonLongElapse, TRUE);

    BOOL passes = TRUE;
    for (CCRuleSet* ruleSet : m_rgpRuleSets) {
        if (!ruleSet || !ruleSet->bActive()) continue;
        for (CCRule* rule : ruleSet->m_rgpRules) {
            if (!rule || !rule->bActive() || rule->bStopped()
                || !rule->bDaemonNeeded() || !rule->m_pDaemonExt) {
                continue;
            }
            if (rule->bIsFlooding()) {
                rule->SetFlags(rule->wGetFlags() | g_wStopped);
                if (m_pfRuleFailure) {
                    m_pfRuleFailure(ruleSet, rule, g_uErrFlooding);
                }
                continue;
            }

            if (rule->m_pDaemonExt->GetIT() == itUser) {
                if (!m_pfGetKeyEventParam) continue;
                QString currentServer = m_pfGetKeyEventParam(ptServerName);
                if (rule->m_rgkep[1] != kepMax) {
                    passes = m_pfEventKeyParam
                        && m_pfEventKeyParam(currentServer, rule->m_rgkep[1]);
                } else {
                    QString filter = rule->m_rgstrEventParams[1];
                    passes = m_pfEventRndParam
                        && !filter.isEmpty()
                        && m_pfEventRndParam(currentServer, filter, nullptr,
                                             rule->m_wFlags, ptServerName);
                }
            }
            if (passes && m_pfDaemonQuery) m_pfDaemonQuery(rule);
        }
    }
}

CCActionContext::CCActionContext() = default;

CCActionContext::~CCActionContext()
{
    FreeAndNullFormatting(&m_prgdwFinalMsgFormatting);
}

BOOL CCActionContext::bInitActionContext(CCDynaRules* dynaRules, CCRule* rule)
{
    if (!dynaRules || !rule || !rule->GetEvent() || !rule->GetAction()) {
        return FALSE;
    }
    m_uDelay = rule->GetDelay();
    m_eID = rule->GetEvent()->GetID();
    m_aID = rule->GetAction()->GetID();
    for (UINT index = 0; index < rule->GetAction()->GetParamNum(); ++index) {
        m_rgkap[index] = rule->GetActionKeyParam(index);
        m_rgstrActionFinalParams[index] = rule->GetFinalActionParam(index);
    }
    m_prgdwFinalMsgFormatting = CopyFormatting(rule->GetFinalMsgFormatting());
    m_strIdentityCach = dynaRules->GetCachedIdentity();
    m_strChannelCach = dynaRules->GetCachedChannel();
    return TRUE;
}

CCDelayedRules::CCDelayedRules() = default;

CCDelayedRules::~CCDelayedRules()
{
    FreeRemoveAll();
    delete m_timer;
    m_timer = nullptr;
}

BOOL CCDelayedRules::bAddActionCtx(CCActionContext* actionContext)
{
    if (!actionContext) return FALSE;
    m_plActionCtx.prepend(actionContext);
    bStartTimer();
    return TRUE;
}

BOOL CCDelayedRules::bExecuteActions()
{
    for (INT index = 0; index < m_plActionCtx.size();) {
        CCActionContext* context = m_plActionCtx.at(index);
        if (!context || !context->GetDelay()) {
            ++index;
            continue;
        }
        if (!context->GetDecrementedDelay()) {
            m_plActionCtx.removeAt(index);
            if (m_pfExecuteAction) m_pfExecuteAction(nullptr, nullptr, context);
            delete context;
        } else {
            ++index;
        }
    }
    if (m_plActionCtx.isEmpty()) bStopTimer();
    return TRUE;
}

BOOL CCDelayedRules::bStartTimer()
{
    if (m_bTimerRunning) return TRUE;
    if (!QCoreApplication::instance()) return FALSE;
    if (!m_timer) {
        m_timer = new QTimer;
        QObject::connect(m_timer, &QTimer::timeout, m_timer,
                         [this] { bExecuteActions(); });
    }
    m_timer->start(1000 * g_uDelayedRulesElapse);
    m_bTimerRunning = m_timer->isActive();
    return m_bTimerRunning;
}

BOOL CCDelayedRules::bStopTimer()
{
    if (m_timer) m_timer->stop();
    m_bTimerRunning = FALSE;
    return TRUE;
}

void CCDelayedRules::FreeRemoveAll()
{
    qDeleteAll(m_plActionCtx);
    m_plActionCtx.clear();
    bStopTimer();
}
