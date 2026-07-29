#include "autopage.h"

#include "actions.h"
#include "chatdoc.h"
#include "format.h"
#include "ircproto.h"
#include "originalassets.h"
#include "protsupp.h"
#include "rtfcmb.h"
#include "userinfo.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QWidget>

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

class TestRtfCmb final : public CRtfCmb {
public:
    using CRtfCmb::CRtfCmb;

    void OpenPopup() { showPopup(); }
    void ClosePopup() { hidePopup(); }
};

BOOL executeFileAction(enumActions action, const QString& target,
                       const QString& fileName, const QString& ranges)
{
    CCRule rule(&theApp.m_dynaRules);
    rule.SetEvent(theApp.m_rulesData.GetEvent(eOnMessage));
    rule.SetAction(theApp.m_rulesData.GetAction(action));
    REQUIRE(rule.GetEvent() != nullptr && rule.GetAction() != nullptr);
    const QString parameters[] = {target, fileName, ranges};
    for (UINT index = 0; index < g_uMaxActionParams; ++index) {
        rule.SetActionKeyParam(index, kapMax);
        rule.SetActionParam(index, parameters[index]);
    }
    REQUIRE(theApp.m_dynaRules.bReplaceKeyActionParams(&rule));
    CCActionContext context;
    REQUIRE(context.bInitActionContext(&theApp.m_dynaRules, &rule));
    return bExecuteAction(&theApp.m_dynaRules, &rule, &context);
}

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
    return action == aPlaySound || action == aSendSound;
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

    {
        QWidget owner;
        owner.resize(360, 100);
        owner.show();
        TestRtfCmb combo(&owner);
        combo.setGeometry(20, 20, 260, combo.sizeHint().height());
        combo.addItem(QStringLiteral("Keyword"));
        combo.show();
        application.processEvents();

        const Qt::FocusPolicy plainFocusPolicy = combo.focusPolicy();
        REQUIRE(combo.bSetRtfMode(TRUE));
        REQUIRE(combo.bGetRtfMode());
        REQUIRE(combo.focusPolicy() == Qt::ClickFocus);
        REQUIRE(combo.bAttachRtfCtrl(QStringLiteral("IDC_TEST_RTF")));
        CRtfCmbEdit* richEdit = combo.GetRtfCmbEdit();
        REQUIRE(richEdit != nullptr);
        REQUIRE(richEdit->objectName() == QStringLiteral("IDC_TEST_RTF"));
        REQUIRE(!richEdit->m_bAcceptMultiLine);
        REQUIRE(richEdit->tabChangesFocus());
        REQUIRE(richEdit->lineWrapMode() == QTextEdit::NoWrap);
        REQUIRE(richEdit->horizontalScrollBarPolicy()
                == Qt::ScrollBarAlwaysOff);
        REQUIRE(richEdit->verticalScrollBarPolicy()
                == Qt::ScrollBarAlwaysOff);

        QStyleOptionComboBox styleOption;
        styleOption.initFrom(&combo);
        styleOption.editable = combo.isEditable();
        const QRect comboEditRect = combo.style()->subControlRect(
            QStyle::CC_ComboBox, &styleOption,
            QStyle::SC_ComboBoxEditField, &combo);
        const QRect expectedRichRect(
            combo.mapTo(&owner, comboEditRect.topLeft()),
            comboEditRect.size());
        REQUIRE(richEdit->geometry() == expectedRichRect);
        REQUIRE(richEdit->geometry().right() < combo.geometry().right());

        combo.LimitText(4);
        combo.SetWindowText(QStringLiteral("abcdef"));
        application.processEvents();
        REQUIRE(combo.GetWindowText() == QStringLiteral("abcd"));
        REQUIRE(combo.lineEdit()->text() == QStringLiteral("abcd"));
        combo.LimitText(g_uMaxParamLength);
        combo.SetWindowText(QStringLiteral("first\nsecond\r\nthird"));
        REQUIRE(combo.GetWindowText() == QStringLiteral("firstsecondthird"));

        // Rebuilding the source keyword list must not clear the separately
        // formatted edit control layered over the combo's edit field.
        combo.clear();
        combo.addItem(QStringLiteral("Keyword"));
        REQUIRE(combo.GetWindowText() == QStringLiteral("firstsecondthird"));
        combo.SetWindowText(QStringLiteral("selection"));
        combo.lineEdit()->setSelection(1, 3);
        combo.RedirectSelection();
        REQUIRE(richEdit->textCursor().selectedText()
                == QStringLiteral("ele"));

        QLineEdit popupFocusTarget(&owner);
        popupFocusTarget.show();
        popupFocusTarget.setFocus();
        combo.ClosePopup();
        application.processEvents();
        REQUIRE(QApplication::focusWidget() == richEdit);

        REQUIRE(QMetaObject::invokeMethod(
            &combo, "activated", Qt::DirectConnection, Q_ARG(int, 0)));
        REQUIRE(combo.GetWindowText() == QStringLiteral("Keyword"));
        REQUIRE(richEdit->textCursor().selectedText()
                == QStringLiteral("Keyword"));

        QKeyEvent returnKey(QEvent::KeyPress, Qt::Key_Return,
                            Qt::NoModifier, QStringLiteral("\n"));
        QApplication::sendEvent(richEdit, &returnKey);
        REQUIRE(combo.GetWindowText() == QStringLiteral("Keyword"));

        QLineEdit nextControl(&owner);
        nextControl.setGeometry(20, 60, 120, 24);
        nextControl.show();
        nextControl.setFocus();
        application.processEvents();
        REQUIRE(richEdit->textCursor().position() == 0);
        REQUIRE(richEdit->textCursor().anchor() == 0);
        REQUIRE(richEdit->viewport()->cursor().shape() == Qt::ArrowCursor);

        combo.hide();
        REQUIRE(richEdit->isHidden());
        combo.show();
        application.processEvents();
        REQUIRE(richEdit->isVisible());

        QPointer<CRtfCmbEdit> deletedEdit = richEdit;
        REQUIRE(combo.bSetRtfMode(FALSE));
        REQUIRE(!combo.bGetRtfMode());
        REQUIRE(deletedEdit.isNull());
        REQUIRE(combo.GetRtfCmbEdit() == nullptr);
        REQUIRE(combo.focusPolicy() == plainFocusPolicy);
    }

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

    {
        QTemporaryDir files;
        REQUIRE(files.isValid());
        QDir base(files.path());
        REQUIRE(base.mkdir(QStringLiteral("Nested")));
        QFile lines(base.filePath(QStringLiteral("Nested/Lines.TXT")));
        REQUIRE(lines.open(QIODevice::WriteOnly));
        REQUIRE(lines.write("one\r\n\r\ntwo\nthree") == 16);
        lines.close();

        const QString savedBaseDir = theApp.m_strBaseDir;
        theApp.m_strBaseDir = files.path();
        REQUIRE(CommunicationInits());
        struct CommunicationCleanupGuard {
            ~CommunicationCleanupGuard() { CommunicationCleanup(); }
        } communicationCleanup;

        CapturingIrcProto activeProtocol;
        CapturingIrcProto targetProtocol;
        CChatDoc activeDocument;
        CChatDoc targetDocument;
        delete activeDocument.m_proto;
        delete targetDocument.m_proto;
        activeDocument.m_proto = &activeProtocol;
        targetDocument.m_proto = &targetProtocol;
        activeProtocol.m_doc = &activeDocument;
        targetProtocol.m_doc = &targetDocument;
        activeProtocol.m_strChannel = QStringLiteral("#file-source");
        activeProtocol.m_strPrettyChannel = activeProtocol.m_strChannel;
        targetProtocol.m_strChannel = QStringLiteral("#file-target");
        targetProtocol.m_strPrettyChannel = targetProtocol.m_strChannel;
        activeProtocol.SetConnectionStatus(CX_INCHANNEL);
        targetProtocol.SetConnectionStatus(CX_INCHANNEL);
        activeDocument.m_bComicView = false;
        targetDocument.m_bComicView = false;

        auto* activeSelf = new CUserInfo(
            originalResourceString(QStringLiteral("IDS_DEFAULT_NICK")));
        auto* targetSelf = new CUserInfo(
            originalResourceString(QStringLiteral("IDS_DEFAULT_NICK")));
        auto* targetUser = new CUserInfo(QStringLiteral("Other"),
                                         QStringLiteral("user@example.test"));
        activeDocument.m_puiSelf = activeSelf;
        activeDocument.m_allChannelPuis.append(activeSelf);
        activeDocument.m_mapNickToPtr.insert(activeSelf->GetName(), activeSelf);
        targetDocument.m_puiSelf = targetSelf;
        targetDocument.m_allChannelPuis = {targetSelf, targetUser};
        targetDocument.m_mapNickToPtr.insert(targetSelf->GetName(), targetSelf);
        targetDocument.m_mapNickToPtr.insert(targetUser->GetName(), targetUser);
        SetChatDoc(&activeDocument);

        const QString relativeFile = QStringLiteral("nested\\lines.txt");
        REQUIRE(executeFileAction(
            aSendFileLine, targetProtocol.m_strPrettyChannel,
            relativeFile, QStringLiteral("3,1-2,4")));
        REQUIRE(targetProtocol.sent == QStringList({
            QStringLiteral("PRIVMSG #file-target :two\r\n"),
            QStringLiteral("PRIVMSG #file-target :one\r\n"),
            QStringLiteral("PRIVMSG #file-target :three\r\n")}));

        // A missing file and a bad interval after an already valid interval
        // are both source-defined handled actions without user-facing errors.
        targetProtocol.sent.clear();
        REQUIRE(executeFileAction(
            aSendFileLine, targetProtocol.m_strPrettyChannel,
            QStringLiteral("missing.txt"), QStringLiteral("1")));
        REQUIRE(targetProtocol.sent.isEmpty());
        REQUIRE(executeFileAction(
            aSendFileLine, targetProtocol.m_strPrettyChannel,
            relativeFile, QStringLiteral("1,,2")));
        REQUIRE(targetProtocol.sent == QStringList({
            QStringLiteral("PRIVMSG #file-target :one\r\n")}));
        targetProtocol.sent.clear();
        REQUIRE(executeFileAction(
            aSendFileLine, targetProtocol.m_strPrettyChannel,
            relativeFile, QStringLiteral("1-2 3")));
        REQUIRE(targetProtocol.sent == QStringList({
            QStringLiteral("PRIVMSG #file-target :one\r\n"),
            QStringLiteral("PRIVMSG #file-target :two\r\n")}));

        targetProtocol.sent.clear();
        REQUIRE(executeFileAction(
            aSendFileLine, targetProtocol.m_strPrettyChannel,
            relativeFile,
            theApp.m_rulesData.GetKeyActionParam(kapAll)));
        REQUIRE(targetProtocol.sent == QStringList({
            QStringLiteral("PRIVMSG #file-target :one\r\n"),
            QStringLiteral("PRIVMSG #file-target :two\r\n"),
            QStringLiteral("PRIVMSG #file-target :three\r\n")}));

        std::srand(173);
        const UINT randomLine = static_cast<UINT>(
            4 * (static_cast<float>(std::rand())
                 / (static_cast<float>(RAND_MAX) + 1.0F))
            + 1.0F);
        std::srand(173);
        targetProtocol.sent.clear();
        REQUIRE(executeFileAction(
            aSendFileLine, targetProtocol.m_strPrettyChannel,
            relativeFile,
            theApp.m_rulesData.GetKeyActionParam(kapRandom)));
        const QStringList sourceLines = {
            QStringLiteral("one"), QString(), QStringLiteral("two"),
            QStringLiteral("three")};
        const QString randomlySelected = sourceLines.at(randomLine - 1);
        if (randomlySelected.isEmpty()) {
            REQUIRE(targetProtocol.sent.isEmpty());
        } else {
            REQUIRE(targetProtocol.sent == QStringList({
                QStringLiteral("PRIVMSG #file-target :%1\r\n")
                    .arg(randomlySelected)}));
        }

        targetProtocol.sent.clear();
        REQUIRE(executeFileAction(
            aWhisperFileLine,
            targetUser->GetName() + QLatin1Char(';') + targetUser->GetName(),
            relativeFile, QStringLiteral("1-4")));
        REQUIRE(targetProtocol.sent == QStringList({
            QStringLiteral("PRIVMSG Other :one\r\n"),
            QStringLiteral("PRIVMSG Other :two\r\n"),
            QStringLiteral("PRIVMSG Other :three\r\n")}));

        CCRule validated(&theApp.m_dynaRules);
        validated.SetEvent(theApp.m_rulesData.GetEvent(eOnMessage));
        validated.SetAction(theApp.m_rulesData.GetAction(aSendFileLine));
        UINT validationError = 0;
        QString invalidRange = QStringLiteral("1,,2");
        REQUIRE(!validated.bValidateRuleAction(
            2, invalidRange, &validationError));
        REQUIRE(validationError == IDS_ERR_FILELINERANGE);
        QString reversedRange = QStringLiteral("4-2,1");
        REQUIRE(validated.bValidateRuleAction(
            2, reversedRange, &validationError));
        REQUIRE(validationError == 0);
        QString sourceWhitespaceRange = QStringLiteral("1-2 3");
        REQUIRE(validated.bValidateRuleAction(
            2, sourceWhitespaceRange, &validationError));
        REQUIRE(validationError == 0);

        g_rgpuiWhisperees.clear();
        SetChatDoc(nullptr);
        activeProtocol.m_doc = nullptr;
        targetProtocol.m_doc = nullptr;
        activeDocument.m_proto = nullptr;
        targetDocument.m_proto = nullptr;
        theApp.m_strBaseDir = savedBaseDir;
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
        REQUIRE(dynamic_cast<CRtfCmb*>(events) != nullptr);
        REQUIRE(dynamic_cast<CRtfCmb*>(actions) != nullptr);
        REQUIRE(events->count() == static_cast<INT>(eMax));
        editor.show();
        application.processEvents();

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

        auto selectAction = [&](enumActions wanted) -> CCAction* {
            for (INT eventIndex = 0;
                 eventIndex < events->count(); ++eventIndex) {
                CCEvent* event = pointerFromData<CCEvent>(
                    events->itemData(eventIndex));
                const DWORD actionBit = DWORD{1}
                    << static_cast<UINT>(wanted);
                if (!event || !(event->GetEnabledActions() & actionBit))
                    continue;
                events->setCurrentIndex(eventIndex);
                application.processEvents();
                for (INT actionIndex = 0;
                     actionIndex < actions->count(); ++actionIndex) {
                    CCAction* action = pointerFromData<CCAction>(
                        actions->itemData(actionIndex));
                    if (action && action->GetID() == wanted) {
                        actions->setCurrentIndex(actionIndex);
                        application.processEvents();
                        return action;
                    }
                }
            }
            return nullptr;
        };

        REQUIRE(selectAction(aSendMessage) != nullptr);
        auto* richMessage = dynamic_cast<CRtfCmb*>(
            editor.findChild<QComboBox*>(QStringLiteral("IDC_CMBAP1")));
        REQUIRE(richMessage != nullptr);
        REQUIRE(richMessage->isVisible());
        REQUIRE(richMessage->bGetRtfMode());
        CRtfCmbEdit* richMessageEdit = richMessage->GetRtfCmbEdit();
        REQUIRE(richMessageEdit != nullptr);
        REQUIRE(richMessageEdit->isVisible());
        REQUIRE(richMessageEdit->objectName()
                == QStringLiteral("IDC_RTFAP1"));
        QStyleOptionComboBox richStyleOption;
        richStyleOption.initFrom(richMessage);
        richStyleOption.editable = richMessage->isEditable();
        const QRect richComboEditRect =
            richMessage->style()->subControlRect(
                QStyle::CC_ComboBox, &richStyleOption,
                QStyle::SC_ComboBoxEditField, richMessage);
        REQUIRE(richMessageEdit->geometry() == QRect(
            richMessage->mapTo(&editor, richComboEditRect.topLeft()),
            richComboEditRect.size()));
        REQUIRE(richMessageEdit->geometry().right()
                < richMessage->geometry().right());

        auto expectedKeywords = [](CCEvent* event, CCAction* action,
                                   UINT parameter) {
            QStringList result;
            UINT bit = 1;
            const UINT keys = action->GetKeyParam(parameter);
            const DWORD exposed = event->GetActionKeysExposed();
            for (UINT key = 0; key < static_cast<UINT>(kapMax); ++key) {
                if ((keys & bit) && (exposed & bit)) {
                    result.append(theApp.m_rulesData.GetKeyActionParam(
                        static_cast<enumKeyActionParam>(key)));
                }
                bit <<= 1;
            }
            return result;
        };
        auto comboItems = [](QComboBox* combo) {
            QStringList result;
            for (INT index = 0; index < combo->count(); ++index)
                result.append(combo->itemText(index));
            return result;
        };
        CCAction* sendMessageAction = pointerFromData<CCAction>(
            actions->currentData());
        CCEvent* initialMessageEvent = pointerFromData<CCEvent>(
            events->currentData());
        REQUIRE(sendMessageAction
                && sendMessageAction->GetID() == aSendMessage);
        const QStringList initialKeywords = expectedKeywords(
            initialMessageEvent, sendMessageAction, 1);
        REQUIRE(comboItems(richMessage) == initialKeywords);
        richMessage->SetWindowText(QStringLiteral("formatted text"));
        bool checkedChangedExposure = false;
        for (INT eventIndex = 0;
             eventIndex < events->count(); ++eventIndex) {
            CCEvent* candidate = pointerFromData<CCEvent>(
                events->itemData(eventIndex));
            if (!candidate
                || !(candidate->GetEnabledActions()
                     & (DWORD{1} << static_cast<UINT>(aSendMessage)))) {
                continue;
            }
            const QStringList candidateKeywords = expectedKeywords(
                candidate, sendMessageAction, 1);
            if (candidateKeywords == initialKeywords) continue;
            events->setCurrentIndex(eventIndex);
            application.processEvents();
            REQUIRE(pointerFromData<CCAction>(actions->currentData())
                    == sendMessageAction);
            REQUIRE(comboItems(richMessage) == candidateKeywords);
            REQUIRE(richMessage->GetWindowText()
                    == QStringLiteral("formatted text"));
            checkedChangedExposure = true;
            break;
        }
        REQUIRE(checkedChangedExposure);

        auto focusBelongsTo = [](QWidget* widget) {
            QWidget* focused = QApplication::focusWidget();
            return widget && focused
                && (focused == widget || widget->isAncestorOf(focused));
        };
        auto pressTab = [&](bool forward) {
            QWidget* focused = QApplication::focusWidget();
            REQUIRE(focused != nullptr);
            QKeyEvent tabKey(
                QEvent::KeyPress,
                forward ? Qt::Key_Tab : Qt::Key_Backtab,
                forward ? Qt::NoModifier : Qt::ShiftModifier);
            QApplication::sendEvent(focused, &tabKey);
            application.processEvents();
        };
        auto* firstMessageParameter =
            editor.findChild<QComboBox*>(QStringLiteral("IDC_CMBAP0"));
        auto* delayControl =
            editor.findChild<QSpinBox*>(QStringLiteral("IDC_RULEDELAY"));
        auto* subRulesControl =
            editor.findChild<QCheckBox*>(QStringLiteral("IDC_CHKSUBRULES"));
        REQUIRE(firstMessageParameter && delayControl && subRulesControl);
        actions->setFocus();
        application.processEvents();
        pressTab(true);
        REQUIRE(focusBelongsTo(firstMessageParameter));
        pressTab(true);
        REQUIRE(focusBelongsTo(richMessageEdit));
        pressTab(true);
        QWidget* afterLastParameter = delayControl->isEnabled()
            ? static_cast<QWidget*>(delayControl)
            : static_cast<QWidget*>(subRulesControl);
        REQUIRE(focusBelongsTo(afterLastParameter));
        pressTab(false);
        REQUIRE(focusBelongsTo(richMessageEdit));

        REQUIRE(selectAction(aNotifyDialog) != nullptr);
        auto* plainNotification = dynamic_cast<CRtfCmb*>(
            editor.findChild<QComboBox*>(QStringLiteral("IDC_CMBAP0")));
        REQUIRE(plainNotification != nullptr);
        REQUIRE(plainNotification->isVisible());
        REQUIRE(!plainNotification->bGetRtfMode());
        REQUIRE(plainNotification->GetRtfCmbEdit() == nullptr);

        REQUIRE(selectAction(aSendFileLine) != nullptr);
        auto* textFiles = dynamic_cast<CRtfCmb*>(
            editor.findChild<QComboBox*>(QStringLiteral("IDC_CMBAP1")));
        REQUIRE(textFiles != nullptr);
        REQUIRE(textFiles->isVisible());
        REQUIRE(!textFiles->bGetRtfMode());
        QSet<QString> enumeratedTextFiles;
        for (INT index = 0; index < textFiles->count(); ++index)
            enumeratedTextFiles.insert(textFiles->itemText(index));
        const QSet<QString> expectedTextFiles = {
            QStringLiteral("Irc.txt"),
            QStringLiteral("Ircnew.txt"),
            QStringLiteral("Ircorig.txt"),
            QStringLiteral("Profile.txt"),
            QStringLiteral("Rtwsupport.txt"),
            QStringLiteral("Strings.txt")};
        REQUIRE(enumeratedTextFiles == expectedTextFiles);

        QTemporaryDir enumerationTree;
        REQUIRE(enumerationTree.isValid());
        QDir enumerationBase(enumerationTree.path());
        REQUIRE(enumerationBase.mkdir(QStringLiteral("Sub")));
        REQUIRE(enumerationBase.mkdir(QStringLiteral(".ignored")));
        auto writeEnumerationFile = [](const QString& path) {
            QFile file(path);
            REQUIRE(file.open(QIODevice::WriteOnly));
            REQUIRE(file.write("test") == 4);
        };
        writeEnumerationFile(enumerationBase.filePath(
            QStringLiteral("Sub/mixed.TxT")));
        writeEnumerationFile(enumerationBase.filePath(
            QStringLiteral(".ignored/hidden.txt")));
        writeEnumerationFile(enumerationBase.filePath(
            QStringLiteral("README.TXT")));
        writeEnumerationFile(enumerationBase.filePath(
            QStringLiteral("license.txt")));
        writeEnumerationFile(enumerationBase.filePath(
            QStringLiteral("support.txt")));
        const QString sourceBaseDir = theApp.m_strBaseDir;
        theApp.m_strBaseDir = enumerationTree.path();
        REQUIRE(selectAction(aNotifyDialog) != nullptr);
        REQUIRE(selectAction(aSendFileLine) != nullptr);
        REQUIRE(textFiles->count() == 1);
        REQUIRE(textFiles->itemText(0)
                == QStringLiteral("Sub\\Mixed.txt"));
        theApp.m_strBaseDir = sourceBaseDir;

        REQUIRE(selectAction(aConnect) != nullptr);
        auto* networkParameter = dynamic_cast<CChatServiceComboBox*>(
            editor.findChild<QComboBox*>(QStringLiteral("IDC_CMBAPNS")));
        auto* coveredParameter = editor.findChild<QComboBox*>(
            QStringLiteral("IDC_CMBAP1"));
        REQUIRE(networkParameter != nullptr && coveredParameter != nullptr);
        REQUIRE(networkParameter->isVisible());
        REQUIRE(coveredParameter->isHidden());
        REQUIRE(networkParameter->geometry() == coveredParameter->geometry());

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
