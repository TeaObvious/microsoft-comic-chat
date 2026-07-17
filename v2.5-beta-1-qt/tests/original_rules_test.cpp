#include "actions.h"
#include "originalassets.h"
#include "rules.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <array>
#include <cassert>

namespace {
QList<enumActions> executed;
int failures = 0;
QString eventKeyValue;
QString finalMessage;
CDWordArray* finalFormatting = nullptr;

BOOL eventKey(QString&, enumKeyEventParam)
{
    return TRUE;
}

BOOL eventValue(QString& value, QString& filter, PPRUSERMATCH,
                WORD flags, enumParamType type)
{
    if (type != ptMessage) return TRUE;
    return bRndEventParam(value, filter, nullptr, flags, type);
}

QString getEventKey(enumParamType)
{
    return eventKeyValue;
}

QString getActionKey(enumKeyActionParam key, QString& server,
                     QString& identity, QString& channel,
                     QString& recipients, QString& message)
{
    switch (key) {
    case kapEventMessage: return message;
    case kapEventNickname: return identity;
    case kapEventRoom: return channel;
    case kapEventServer: return server;
    case kapEventRecipients: return recipients;
    case kapMe: return eventKeyValue;
    default: return QString();
    }
}

BOOL execute(CCDynaRules* dynaRules, CCRule* rule,
             CCActionContext* context)
{
    assert(context);
    executed.append(context->GetActionID());
    if (context->GetActionID() == aDoNotDisplay && dynaRules) {
        dynaRules->AddFlag(g_wDoNotDisplay);
    } else if (context->GetActionID() == aHighlightMessage && dynaRules) {
        dynaRules->AddFlag(g_wHighlight);
    } else if (context->GetActionID() == aReplaceMessage) {
        assert(dynaRules && rule);
        return dynaRules->bReplaceMessage(rule);
    } else if (context->GetActionID() == aSendMessage) {
        finalMessage = context->GetFinalActionParam(1);
        FreeAndNullFormatting(&finalFormatting);
        finalFormatting = CopyFormatting(context->GetFinalMsgFormatting());
    }
    return TRUE;
}

BOOL failure(CCRuleSet*, CCRule*, UINT error)
{
    assert(error == g_uErrFlooding);
    ++failures;
    return TRUE;
}

void bind(CCDynaRules& rules, CCRulesData& data,
          CCDelayedRules& delayed)
{
    rules.SetRulesData(&data);
    rules.SetDelayedRules(&delayed);
    rules.SetEKPFunction(eventKey);
    rules.SetERPFunction(eventValue);
    rules.SetGEKPFunction(getEventKey);
    rules.SetGAKPFunction(getActionKey);
    rules.SetExecuteActionFunction(execute);
    rules.SetRuleFailureFunction(failure);
    delayed.SetExecuteActionFunction(execute);
}

void setMessageEvent(CCRule* rule, CCRulesData& data,
                     enumKeyEventParam messageKey = kepAny)
{
    rule->SetEvent(data.GetEvent(eOnMessage));
    const enumKeyEventParam keys[] = {
        kepAnyoneButMe, kepMyActivatedRoom, messageKey
    };
    for (UINT index = 0; index < g_uMaxEventParams; ++index) {
        rule->SetEventKeyParam(index, keys[index]);
        rule->SetEventParam(index, data.GetKeyEventParam(keys[index]));
    }
}

CCRule* makeActionRule(CCDynaRules& rules, CCRulesData& data,
                       enumActions action)
{
    auto* rule = new CCRule(&rules);
    setMessageEvent(rule, data);
    rule->SetAction(data.GetAction(action));
    rule->SetFlags(g_wActive);
    return rule;
}

QString setName(const QString& resource)
{
    const QString stored = originalResourceString(resource);
    return stored.mid(stored.indexOf(QLatin1Char('|')) + 1);
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    CCRulesData data;
    assert(data.bInitAlloc());
    assert(data.bLoadStrings());
    assert(data.GetEvent(eOnMessage)->GetParamNum() == 3);
    assert(data.GetEvent(eOnMessage)->GetLongDesc()
           == originalResourceString(QStringLiteral("IDS_EVENT_LONG_DESC6")));
    assert(data.GetAction(aSendMessage)->GetParamNum() == 2);
    assert(data.GetKeyActionParam(kapEventMessage)
           == originalResourceString(QStringLiteral("IDS_KEY_ACTION_PARAM2")));

    CCDynaRules resourceRules;
    CCDelayedRules resourceDelayed;
    bind(resourceRules, data, resourceDelayed);
    assert(resourceRules.bLoadRulesFromResource());
    assert(resourceRules.GetRuleSetsArray().size() == 2);
    CCRuleSet* samples = resourceRules.GetRuleSetFromName(
        setName(QStringLiteral("IDS_SAMPLES_RULESET")));
    CCRuleSet* general = resourceRules.GetRuleSetFromName(
        setName(QStringLiteral("IDS_GENERAL_RULESET")));
    assert(samples && !samples->bActive());
    assert(samples->GetRulesArray().size() == 7);
    assert(general && general->bActive());
    assert(general->GetRulesArray().isEmpty());

    std::array<char, g_uMaxSerializedRule> serialized{};
    CCRule* sourceRule = samples->GetRulesArray().at(6);
    const INT serializedLength = sourceRule->Serialize(
        serialized.data(), serialized.size());
    assert(serializedLength > static_cast<INT>(g_uRuleFixedPrefix));
    CCRule restored(&resourceRules);
    assert(restored.UnSerialize(
               reinterpret_cast<const BYTE*>(serialized.data()),
               serializedLength) == serializedLength);
    assert(restored.StrGetEventDisplay() == sourceRule->StrGetEventDisplay());
    assert(restored.StrGetActionDisplay() == sourceRule->StrGetActionDisplay());

    CCDynaRules orderedRules;
    CCDelayedRules orderedDelayed;
    bind(orderedRules, data, orderedDelayed);
    auto* orderedSet = new CCRuleSet(&orderedRules);
    orderedSet->SetName(setName(QStringLiteral("IDS_GENERAL_RULESET")));
    orderedSet->Activate();
    CCRule* filteredStop = makeActionRule(
        orderedRules, data, aDoNotDisplay);
    filteredStop->SetFlags(g_wActive | g_wNoSubsequent);
    orderedSet->bAddRule(filteredStop);
    CCRule* highlight = makeActionRule(
        orderedRules, data, aHighlightMessage);
    QString highlightType = originalResourceString(
        QStringLiteral("IDS_HIGHLIGHT_TYPE"));
    highlightType.replace(QStringLiteral("%d"), QStringLiteral("1"));
    highlight->SetActionKeyParam(0, kapMax);
    highlight->SetActionParam(0, highlightType);
    orderedSet->bAddRule(highlight);
    orderedRules.bAddRuleSet(orderedSet);

    QString empty;
    enumActions approved[] = {
        static_cast<enumActions>(1), aHighlightMessage
    };
    executed.clear();
    assert(orderedRules.bMatchAndApplyRules(
        eOnMessage, approved, nullptr, empty, empty, empty, empty));
    assert(executed.isEmpty());
    filteredStop->SetFlags(g_wActive);
    assert(orderedRules.bMatchAndApplyRules(
        eOnMessage, approved, nullptr, empty, empty, empty, empty));
    assert(executed == QList<enumActions>{aHighlightMessage});
    assert(orderedRules.GetFlags() & g_wHighlight);

    CCDynaRules formattedRules;
    CCDelayedRules formattedDelayed;
    bind(formattedRules, data, formattedDelayed);
    auto* formattedSet = new CCRuleSet(&formattedRules);
    formattedSet->SetName(setName(QStringLiteral("IDS_GENERAL_RULESET")));
    formattedSet->Activate();
    CCRule* formattedRule = makeActionRule(
        formattedRules, data, aSendMessage);
    formattedRule->SetActionKeyParam(0, kapEventRoom);
    formattedRule->SetActionParam(0,
        data.GetKeyActionParam(kapEventRoom));
    formattedRule->SetActionKeyParam(1, kapEventMessage);
    formattedRule->SetActionParam(1,
        data.GetKeyActionParam(kapEventMessage));
    formattedSet->bAddRule(formattedRule);
    formattedRules.bAddRuleSet(formattedSet);
    CDWordArray bold;
    bold.Add(MAKELONG(wBold, 0));
    const QByteArray plain = data.GetKeyActionParam(kapEventMessage).toUtf8();
    char* full = SzControlFull(plain.constData(), &bold);
    QString formattedInput = QString::fromUtf8(full);
    delete[] full;
    executed.clear();
    assert(formattedRules.bMatchAndApplyRules(
        eOnMessage, nullptr, nullptr, empty, empty, empty, formattedInput));
    assert(executed == QList<enumActions>{aSendMessage});
    assert(finalMessage == data.GetKeyActionParam(kapEventMessage));
    assert(finalFormatting && finalFormatting->GetSize() == 1);
    assert(LOWORD(finalFormatting->GetAt(0)) & wBold);

    CCDynaRules replaceRules;
    CCDelayedRules replaceDelayed;
    bind(replaceRules, data, replaceDelayed);
    auto* replaceSet = new CCRuleSet(&replaceRules);
    replaceSet->SetName(setName(QStringLiteral("IDS_GENERAL_RULESET")));
    replaceSet->Activate();
    auto* replaceRule = new CCRule(&replaceRules);
    setMessageEvent(replaceRule, data, kepMax);
    const QString replaceWhat = data.GetKeyActionParam(kapEventMessage);
    replaceRule->SetEventParam(2, replaceWhat);
    replaceRule->SetAction(data.GetAction(aReplaceMessage));
    replaceRule->SetActionKeyParam(0, kapMax);
    eventKeyValue = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
    replaceRule->SetActionParam(0, data.GetKeyEventParam(kepMe));
    replaceRule->SetFlags(g_wActive | g_wMatchCase);
    replaceSet->bAddRule(replaceRule);
    replaceRules.bAddRuleSet(replaceSet);
    QString replaceInput = replaceWhat;
    executed.clear();
    assert(replaceRules.bMatchAndApplyRules(
        eOnMessage, nullptr, nullptr, empty, empty, empty, replaceInput));
    assert(executed == QList<enumActions>{aReplaceMessage});
    assert(replaceRules.GetFlags() & g_wReplace);
    assert(replaceRules.GetCFFinalMessage() == eventKeyValue);

    replaceRules.SetFloodParams(g_uDefRuleFloodInt, 1);
    failures = 0;
    replaceRule->SetFlags(g_wActive | g_wMatchCase);
    replaceRules.bMatchAndApplyRules(
        eOnMessage, nullptr, nullptr, empty, empty, empty, replaceInput);
    replaceRules.bMatchAndApplyRules(
        eOnMessage, nullptr, nullptr, empty, empty, empty, replaceInput);
    assert(replaceRule->bStopped());
    assert(failures == 1);

    CCDynaRules delayedRules;
    CCDelayedRules delayed;
    bind(delayedRules, data, delayed);
    auto* delayedSet = new CCRuleSet(&delayedRules);
    delayedSet->SetName(setName(QStringLiteral("IDS_GENERAL_RULESET")));
    delayedSet->Activate();
    CCRule* delayedRule = makeActionRule(delayedRules, data, aDoNotDisplay);
    delayedRule->SetDelay(2);
    delayedSet->bAddRule(delayedRule);
    delayedRules.bAddRuleSet(delayedSet);
    executed.clear();
    assert(delayedRules.bMatchAndApplyRules(
        eOnMessage, nullptr, nullptr, empty, empty, empty, empty));
    assert(executed.isEmpty() && delayed.GetCount() == 1);
    delayed.bExecuteActions();
    assert(executed.isEmpty() && delayed.GetCount() == 1);
    delayed.bExecuteActions();
    assert(executed == QList<enumActions>{aDoNotDisplay});
    assert(delayed.GetCount() == 0);

    QTemporaryDir directory;
    assert(directory.isValid());
    const QString fileName = directory.filePath(QStringLiteral("rules.crs"));
    UINT error = 0;
    assert(formattedSet->bSaveToFile(fileName, &error));
    assert(error == 0);
    CCDynaRules fileRules;
    CCDelayedRules fileDelayed;
    bind(fileRules, data, fileDelayed);
    CCRuleSet loaded(&fileRules);
    assert(loaded.bLoadFromFile(fileName, &error));
    assert(error == 0);
    assert(loaded.GetName() == formattedSet->GetName());
    assert(loaded.GetRulesArray().size() == 1);
    assert(loaded.GetRulesArray().front()->StrGetActionDisplay()
           == formattedRule->StrGetActionDisplay());

    FreeAndNullFormatting(&finalFormatting);
    return 0;
}

