#include "autopage.h"

#include "chatdoc.h"
#include "format.h"
#include "ircproto.h"
#include "originalassets.h"
#include "userinfo.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

class CapturingIrcProto final : public CIrcProto {
public:
    void SendMessageText(const QString& raw) override { sent.append(raw); }
    void SendMessageBytes(const QByteArray& raw) override
    {
        sent.append(QString::fromUtf8(raw));
    }
    QStringList sent;
};

const OriginalDialogControl* resourceControl(
    const OriginalDialogResource& dialog, const QString& identifier)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier == identifier) return &control;
    }
    return nullptr;
}

class DialogUnitMapper {
public:
    explicit DialogUnitMapper(const QFont& font)
    {
        const QFontMetrics metrics(font);
        const QString alphabet = QStringLiteral(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        m_baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
        m_baseY = qMax(1, metrics.height());
    }

    int x(int dlu) const { return (dlu * m_baseX + 2) / 4; }
    int y(int dlu) const { return (dlu * m_baseY + 4) / 8; }
    QRect rect(const OriginalDialogControl& control) const
    {
        return {x(control.x), y(control.y),
                x(control.width), y(control.height)};
    }

private:
    int m_baseX = 1;
    int m_baseY = 1;
};

void requireResourceWidget(QWidget* widget, const QString& identifier)
{
    REQUIRE(widget != nullptr);
    const OriginalDialogResource resource = originalDialogResource(identifier);
    const DialogUnitMapper mapper(widget->font());
    REQUIRE(widget->objectName() == identifier);
    REQUIRE(widget->size() == QSize(mapper.x(resource.width),
                                    mapper.y(resource.height)));
    REQUIRE(widget->layout() == nullptr);
    if (auto* dialog = dynamic_cast<QDialog*>(widget))
        REQUIRE(dialog->windowTitle() == resource.caption);
}

template<typename T>
T* pointerFromData(const QVariant& value)
{
    return reinterpret_cast<T*>(value.value<quintptr>());
}

bool unsupportedAction(enumActions action)
{
    return action == aPlaySound || action == aSendSound
        || action == aSendFileLine || action == aWhisperFileLine;
}

QString ruleSetName(const QString& resourceIdentifier)
{
    const QString stored = originalResourceString(resourceIdentifier);
    return stored.mid(stored.indexOf(QLatin1Char('|')) + 1);
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.InitializeFonts();

    REQUIRE(theApp.m_rulesData.bInitAlloc());
    REQUIRE(theApp.m_rulesData.bLoadStrings());
    REQUIRE(theApp.m_dynaRules.bLoadRulesFromResource());
    REQUIRE(theApp.m_dynaRules.GetRuleSetsArray().size() == 2);

    const QStringList keys = originalDialogInitStrings(
        QStringLiteral("IDD_AUTOMATION_PAGE"), QStringLiteral("IDC_KEY"));
    REQUIRE(keys.size() == NMACROS);
    REQUIRE(keys.first() == QStringLiteral("Alt+0"));
    REQUIRE(keys.last() == QStringLiteral("Alt+9"));
    for (INT index = 0; index < NMACROS; ++index)
        REQUIRE(!theApp.m_macros[index].m_bDefined);

    const QString macroValue = originalResourceString(
        QStringLiteral("IDS_DFLTAWAYMSG"));
    REQUIRE(!macroValue.isEmpty());
    CMacro serializedSource;
    serializedSource.m_bDefined = TRUE;
    serializedSource.m_strName = keys.first();
    serializedSource.m_strValue = macroValue;
    const QByteArray nameBytes = serializedSource.m_strName.toUtf8();
    const QByteArray valueBytes = serializedSource.m_strValue.toUtf8();
    QByteArray serialized(nameBytes.size() + valueBytes.size() + 8, '\x7f');
    const INT serializedLength = serializedSource.Serialize(
        serialized.data(), serialized.size());
    QByteArray expectedSerialized = nameBytes;
    expectedSerialized.append('\0');
    expectedSerialized.append(valueBytes);
    expectedSerialized.append('\0');
    expectedSerialized.append('\0');
    REQUIRE(serializedLength == expectedSerialized.size());
    REQUIRE(serialized.left(serializedLength) == expectedSerialized);
    REQUIRE(serialized.at(serializedLength - 1) == '\0');
    REQUIRE(serialized.at(serializedLength - 2) == '\0');
    CMacro serializedCopy;
    serializedCopy.UnSerialize(serialized.constData());
    REQUIRE(serializedCopy.m_bDefined);
    REQUIRE(serializedCopy.m_strName == serializedSource.m_strName);
    REQUIRE(serializedCopy.m_strValue == serializedSource.m_strValue);

    CMacro savedMacros[NMACROS];
    for (INT index = 0; index < NMACROS; ++index)
        savedMacros[index] = theApp.m_macros[index];
    const QString savedGreeting = theApp.m_strGreetingMesg;
    const INT savedGreetingType = theApp.m_iGreetingType;
    const UCHAR savedFloodFlags = theApp.m_uFloodFlags;
    const UCHAR savedFloodCount = theApp.m_uFloodCount;
    const UCHAR savedFloodInterval = theApp.m_uFloodInterval;

    {
        CAutomationPage page;
        requireResourceWidget(&page, QStringLiteral("IDD_AUTOMATION_PAGE"));
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_AUTOMATION_PAGE"));
        const DialogUnitMapper mapper(page.font());
        auto* key = page.findChild<QComboBox*>(QStringLiteral("IDC_KEY"));
        auto* name = page.findChild<QLineEdit*>(
            QStringLiteral("IDC_MACRONAME"));
        auto* macro = page.findChild<QTextEdit*>(
            QStringLiteral("IDC_MACRORICHEDIT"));
        auto* add = page.findChild<QPushButton*>(
            QStringLiteral("IDC_ADD_MACRO"));
        auto* remove = page.findChild<QPushButton*>(
            QStringLiteral("IDC_DELETE_MACRO"));
        auto* count = page.findChild<QLineEdit*>(
            QStringLiteral("IDC_MESGCOUNT"));
        auto* interval = page.findChild<QLineEdit*>(
            QStringLiteral("IDC_INTERVAL"));
        auto* countSpin = page.findChild<QSpinBox*>(
            QStringLiteral("IDC_MESGCOUNTSPIN"));
        auto* intervalSpin = page.findChild<QSpinBox*>(
            QStringLiteral("IDC_INTERVALSPIN"));
        REQUIRE(key && name && macro && add && remove && count && interval
                && countSpin && intervalSpin);
        REQUIRE(key->geometry() == mapper.rect(*resourceControl(
            resource, QStringLiteral("IDC_KEY"))));
        REQUIRE(key->count() == keys.size());
        for (INT index = 0; index < key->count(); ++index)
            REQUIRE(key->itemText(index) == keys[index]);
        REQUIRE(name->maxLength() == 20);
        REQUIRE(count->maxLength() == 3 && interval->maxLength() == 3);
        REQUIRE(countSpin->minimum() == 1 && countSpin->maximum() == 255);
        REQUIRE(intervalSpin->minimum() == 1
                && intervalSpin->maximum() == 255);
        REQUIRE(!remove->isEnabled());

        name->setText(keys.first());
        macro->setPlainText(macroValue);
        add->click();
        REQUIRE(page.m_macros[0].m_bDefined);
        REQUIRE(page.m_macros[0].m_strName == keys.first());
        REQUIRE(page.m_macros[0].m_strValue == macroValue);
        REQUIRE(remove->isEnabled());
        page.OnOK();
        REQUIRE(theApp.m_macros[0].m_bDefined);
        REQUIRE(theApp.m_macros[0].m_strName == keys.first());
        REQUIRE(theApp.m_macros[0].m_strValue == macroValue);
    }

    for (INT index = 0; index < NMACROS; ++index)
        theApp.m_macros[index] = savedMacros[index];
    theApp.m_strGreetingMesg = savedGreeting;
    theApp.m_iGreetingType = savedGreetingType;
    theApp.m_uFloodFlags = savedFloodFlags;
    theApp.m_uFloodCount = savedFloodCount;
    theApp.m_uFloodInterval = savedFloodInterval;

    {
        CapturingIrcProto protocol;
        CChatDoc document;
        document.m_bComicView = false;
        delete document.m_proto;
        document.m_proto = &protocol;
        protocol.m_doc = &document;
        protocol.m_strChannel = originalResourceString(
            QStringLiteral("IDS_DEFAULT_CHANNEL"));
        protocol.m_strPrettyChannel = protocol.m_strChannel;
        protocol.SetConnectionStatus(CX_INCHANNEL);
        CUserInfo self(originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK")));
        document.m_puiSelf = &self;
        SetChatDoc(&document);

        CMacro macro;
        macro.m_bDefined = TRUE;
        macro.m_strName = keys.first();
        macro.m_strValue = macroValue;
        macro.Invoke();
        REQUIRE(protocol.sent.size() == 1);
        REQUIRE(protocol.sent.first()
                == QStringLiteral("PRIVMSG %1 :%2\r\n")
                       .arg(protocol.m_strChannel, macroValue));

        SetChatDoc(nullptr);
        document.m_puiSelf = nullptr;
        document.m_proto = nullptr;
    }

    CCDynaRules ruleSetsCopy;
    ruleSetsCopy = theApp.m_dynaRules;
    {
        CRuleSetsPage page;
        page.SetDynaRules(&ruleSetsCopy);
        requireResourceWidget(&page, QStringLiteral("IDD_RULESETSPAGE"));
        auto* list = dynamic_cast<CRuleSetsListBox*>(
            page.findChild<QListWidget*>(QStringLiteral("IDC_LSTRULESETS")));
        REQUIRE(list != nullptr);
        REQUIRE(list->count() == ruleSetsCopy.GetRuleSetsArray().size());
        for (INT index = 0; index < list->count(); ++index) {
            REQUIRE(list->RuleSetAt(index)
                    == ruleSetsCopy.GetRuleSetsArray()[index]);
            REQUIRE(list->item(index)->text()
                    == ruleSetsCopy.GetRuleSetsArray()[index]->GetName());
            REQUIRE(!list->item(index)->icon().isNull());
        }
        list->setCurrentRow(0);
        CCRuleSet* firstSet = list->RuleSetAt(0);
        const BOOL wasActive = firstSet->bActive();
        QKeyEvent toggle(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(list, &toggle);
        REQUIRE(firstSet->bActive() != wasActive);
        REQUIRE(page.IsModified());
        list->SwitchActivation(0);
        REQUIRE(firstSet->bActive() == wasActive);
    }

    CCDynaRules rulesCopy;
    rulesCopy = theApp.m_dynaRules;
    CCRuleSet* samples = rulesCopy.GetRuleSetFromName(
        ruleSetName(QStringLiteral("IDS_SAMPLES_RULESET")));
    CCRuleSet* general = rulesCopy.GetRuleSetFromName(
        ruleSetName(QStringLiteral("IDS_GENERAL_RULESET")));
    REQUIRE(samples && general);
    REQUIRE(samples->GetRulesArray().size() == 7);
    rulesCopy.SetSelectedRuleSet(samples);

    {
        CRulesPage page;
        page.SetDynaRules(&rulesCopy);
        requireResourceWidget(&page, QStringLiteral("IDD_RULESPAGE"));
        auto* sets = page.findChild<QListWidget*>(QStringLiteral("IDC_LSTSETS"));
        auto* rules = dynamic_cast<CRulesListCtrl*>(
            page.findChild<QTreeWidget*>(QStringLiteral("IDC_LSTRULES")));
        REQUIRE(sets && rules);
        REQUIRE(sets->count() == rulesCopy.GetRuleSetsArray().size());
        REQUIRE(rules->headerItem()->text(0) == originalResourceString(
            QStringLiteral("IDS_EVENTS_LABEL")));
        REQUIRE(rules->headerItem()->text(1) == originalResourceString(
            QStringLiteral("IDS_ACTIONS_LABEL")));
        REQUIRE(rules->topLevelItemCount() == samples->GetRulesArray().size());
        for (INT index = 0; index < rules->topLevelItemCount(); ++index) {
            CCRule* sourceRule = samples->GetRulesArray()[index];
            REQUIRE(rules->RuleAt(index) == sourceRule);
            REQUIRE(rules->topLevelItem(index)->text(0)
                    == sourceRule->StrGetEventDisplay());
            REQUIRE(rules->topLevelItem(index)->text(1)
                    == sourceRule->StrGetActionDisplay());
        }
        QTreeWidgetItem* firstItem = rules->topLevelItem(0);
        rules->setCurrentItem(firstItem);
        firstItem->setSelected(true);
        CCRule* firstRule = rules->RuleAt(0);
        const WORD savedFlags = firstRule->wGetFlags();
        QKeyEvent toggle(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(rules, &toggle);
        if (savedFlags & g_wStopped)
            REQUIRE(!(firstRule->wGetFlags() & g_wStopped));
        else
            REQUIRE(firstRule->bActive() != BOOL(savedFlags & g_wActive));
        firstRule->SetFlags(savedFlags);
    }

    CCRule* editRule = samples->GetRulesArray().first();
    {
        CEditRule editor;
        editor.UseRule(editRule, &rulesCopy);
        REQUIRE(editor.InitializeRule());
        requireResourceWidget(&editor, QStringLiteral("IDD_EDITRULE"));
        auto* events = editor.findChild<QComboBox*>(
            QStringLiteral("IDC_CMBEVENTS"));
        auto* actions = editor.findChild<QComboBox*>(
            QStringLiteral("IDC_CMBACTIONS"));
        auto* ok = editor.findChild<QPushButton*>(QStringLiteral("IDOK"));
        REQUIRE(events && actions && ok);
        REQUIRE(events->count() == static_cast<INT>(eMax));

        QSet<INT> eventIDs;
        QSet<INT> actionIDs;
        QString previousEvent;
        for (INT eventIndex = 0; eventIndex < events->count(); ++eventIndex) {
            CCEvent* event = pointerFromData<CCEvent>(
                events->itemData(eventIndex));
            REQUIRE(event != nullptr);
            eventIDs.insert(static_cast<INT>(event->GetID()));
            REQUIRE(events->itemText(eventIndex) == event->GetLongDesc());
            if (eventIndex > 0)
                REQUIRE(QString::localeAwareCompare(previousEvent,
                                                     events->itemText(eventIndex)) <= 0);
            previousEvent = events->itemText(eventIndex);
            events->setCurrentIndex(eventIndex);

            QList<CCAction*> expectedActions;
            DWORD mask = 1;
            for (UINT actionIndex = 0;
                 actionIndex < static_cast<UINT>(aMax); ++actionIndex) {
                if (event->GetEnabledActions() & mask) {
                    if (CCAction* action = theApp.m_rulesData.GetAction(actionIndex))
                        expectedActions.append(action);
                }
                mask <<= 1;
            }
            std::sort(expectedActions.begin(), expectedActions.end(),
                      [](CCAction* left, CCAction* right) {
                return QString::localeAwareCompare(left->GetLongDesc(),
                                                    right->GetLongDesc()) < 0;
            });
            REQUIRE(actions->count() == expectedActions.size());
            auto* model = qobject_cast<QStandardItemModel*>(actions->model());
            REQUIRE(model != nullptr);
            for (INT actionIndex = 0;
                 actionIndex < actions->count(); ++actionIndex) {
                CCAction* action = pointerFromData<CCAction>(
                    actions->itemData(actionIndex));
                REQUIRE(action == expectedActions[actionIndex]);
                REQUIRE(actions->itemText(actionIndex)
                        == action->GetLongDesc());
                REQUIRE(model->item(actionIndex) != nullptr);
                REQUIRE(model->item(actionIndex)->isEnabled()
                        == !unsupportedAction(action->GetID()));
                actionIDs.insert(static_cast<INT>(action->GetID()));
            }

            const QString eventParamIds[] = {
                QStringLiteral("IDC_CMBEP0"),
                QStringLiteral("IDC_CMBEP1"),
                QStringLiteral("IDC_CMBEP2")};
            for (UINT param = 0; param < g_uMaxEventParams; ++param) {
                auto* control = editor.findChild<QComboBox*>(eventParamIds[param]);
                REQUIRE(control != nullptr);
                REQUIRE(control->isHidden() == (param >= event->GetParamNum()));
            }
        }
        REQUIRE(eventIDs.size() == static_cast<INT>(eMax));
        REQUIRE(actionIDs.size() == static_cast<INT>(aMax));

        auto* delay = editor.findChild<QSpinBox*>(
            QStringLiteral("IDC_RULEDELAY"));
        REQUIRE(delay && delay->maximum() == 255);
    }

    {
        CAdvancedEventParams dialog(editRule->wGetFlags());
        requireResourceWidget(&dialog,
                              QStringLiteral("IDD_ADVANCEDEVENTPARAMS"));
        REQUIRE(dialog.findChild<QCheckBox*>(
            QStringLiteral("IDC_CHKMATCHCASE")) != nullptr);
        REQUIRE(dialog.findChild<QCheckBox*>(
            QStringLiteral("IDC_CHKMATCHWORD")) != nullptr);
    }
    {
        CAdvancedRuleSettings dialog(rulesCopy.GetFloodingOccurrences(),
                                     rulesCopy.GetFloodingInterval());
        requireResourceWidget(&dialog,
                              QStringLiteral("IDD_ADVANCEDRULESETTINGS"));
        auto* occurrences = dialog.findChild<QSpinBox*>(
            QStringLiteral("IDC_RULEOCC"));
        auto* interval = dialog.findChild<QSpinBox*>(
            QStringLiteral("IDC_RULEINT"));
        REQUIRE(occurrences && interval);
        REQUIRE(occurrences->minimum() == 1 && occurrences->maximum() == 255);
        REQUIRE(interval->minimum() == 1 && interval->maximum() == 255);
        REQUIRE(occurrences->value() == rulesCopy.GetFloodingOccurrences());
        REQUIRE(interval->value() == rulesCopy.GetFloodingInterval());
    }

    CRuleSetsListBox names;
    for (CCRuleSet* set : rulesCopy.GetRuleSetsArray()) names.AddRuleSet(set);
    {
        CCreateSet dialog(&names);
        requireResourceWidget(&dialog, QStringLiteral("IDD_CREATESET"));
        auto* edit = dialog.findChild<QLineEdit*>(
            QStringLiteral("IDC_CREATEDSET"));
        REQUIRE(edit && edit->maxLength() == static_cast<INT>(
            g_uMaxSetNameLength));
        QString invalid = QStringLiteral("/");
        INT position = 0;
        REQUIRE(edit->validator()->validate(invalid, position)
                == QValidator::Invalid);
    }
    {
        CRenameSet dialog(samples, &names);
        requireResourceWidget(&dialog, QStringLiteral("IDD_RENAMESET"));
        auto* edit = dialog.findChild<QLineEdit*>(
            QStringLiteral("IDC_RENAMEDSET"));
        REQUIRE(edit && edit->text() == samples->GetName());
    }
    {
        CSetNameConflict dialog(samples->GetName(), &names);
        requireResourceWidget(&dialog,
                              QStringLiteral("IDD_RENAMELOADEDSET"));
        auto* edit = dialog.findChild<QLineEdit*>(
            QStringLiteral("IDC_RENAMEDRULESET"));
        REQUIRE(edit && edit->text() == samples->GetName());
    }
    {
        CAddToSets dialog(&rulesCopy, samples,
                          samples->GetRulesArray().first());
        requireResourceWidget(&dialog, QStringLiteral("IDD_ADDTOSETS"));
        auto* sets = dialog.findChild<QListWidget*>(
            QStringLiteral("IDC_ALSTSETS"));
        auto* rule = dialog.findChild<QTreeWidget*>(
            QStringLiteral("IDC_ALSTRULES"));
        REQUIRE(sets && rule);
        REQUIRE(sets->count() == 1);
        REQUIRE(sets->item(0)->text() == general->GetName());
        REQUIRE(rule->topLevelItemCount() == 1);
    }

    const BOOL sourceSetActive =
        theApp.m_dynaRules.GetRuleSetsArray().first()->bActive();
    theApp.m_dynaNotifs.SetStartUpIdent(originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK")));
    theApp.m_iAutoPage = 2;
    BOOL sheetObserved = FALSE;
    QTimer::singleShot(0, [&sheetObserved, sourceSetActive] {
        auto* sheet = dynamic_cast<QDialog*>(QApplication::activeModalWidget());
        REQUIRE(sheet != nullptr);
        REQUIRE(sheet->objectName() == QStringLiteral("IDS_AUTOMATIONS"));
        REQUIRE(sheet->windowTitle() == originalResourceString(
            QStringLiteral("IDS_AUTOMATIONS")));
        auto* tabs = sheet->findChild<QTabWidget*>();
        REQUIRE(tabs && tabs->count() == 4);
        REQUIRE(tabs->tabText(0) == originalDialogCaption(
            QStringLiteral("IDD_AUTOMATION_PAGE")));
        REQUIRE(tabs->tabText(1) == originalDialogCaption(
            QStringLiteral("IDD_NOTIFICATIONS")));
        REQUIRE(tabs->tabText(2) == originalDialogCaption(
            QStringLiteral("IDD_RULESETSPAGE")));
        REQUIRE(tabs->tabText(3) == originalDialogCaption(
            QStringLiteral("IDD_RULESPAGE")));
        REQUIRE(tabs->currentIndex() == 2);
        auto* list = dynamic_cast<CRuleSetsListBox*>(
            sheet->findChild<QListWidget*>(QStringLiteral("IDC_LSTRULESETS")));
        REQUIRE(list && list->count() > 0 && list->RuleSetAt(0));
        REQUIRE(list->RuleSetAt(0)
                != theApp.m_dynaRules.GetRuleSetsArray().first());
        list->SwitchActivation(0);
        REQUIRE(list->RuleSetAt(0)->bActive() != sourceSetActive);
        REQUIRE(theApp.m_dynaRules.GetRuleSetsArray().first()->bActive()
                == sourceSetActive);
        sheetObserved = TRUE;
        sheet->reject();
    });
    theApp.OnViewAutomations();
    REQUIRE(sheetObserved);
    REQUIRE(theApp.m_iAutoPage == -1);
    REQUIRE(theApp.m_dynaNotifs.GetStartUpIdent().isEmpty());
    REQUIRE(theApp.m_dynaRules.GetRuleSetsArray().first()->bActive()
            == sourceSetActive);

    return EXIT_SUCCESS;
}
