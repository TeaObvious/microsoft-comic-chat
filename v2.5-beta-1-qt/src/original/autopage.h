//=--------------------------------------------------------------------------=
// AutoPage.h -- Qt port of v2.5-beta-1-modern/autopage.h
//=--------------------------------------------------------------------------=
// Qt replaces only MFC property pages, dialogs and owner-drawn list controls.
// Rule, macro and notification state remains in the original modules.

#pragma once

#include "chat.h"
#include "notipage.h"
#include "rtfcmb.h"
#include "rules.h"

#include <QDialog>
#include <QComboBox>
#include <QIcon>
#include <QListWidget>
#include <QTreeWidget>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QKeyEvent;
class QMouseEvent;
class QShowEvent;
class CChatServiceComboBox;

constexpr SHORT g_nIconCount = 2;
constexpr SHORT g_nInactiveIndex = 0;
constexpr SHORT g_nActiveIndex = 1;
constexpr SHORT g_nIconMargin = 2;
constexpr SHORT g_nIconWidth = 16;
constexpr SHORT g_nIconHeight = 15;

constexpr INT IDOVERWRITE = 3;
constexpr INT IDRENAME = 4;

class CAutomationPage : public QWidget {
public:
    explicit CAutomationPage(QWidget* parent = nullptr);

    BOOL OnSetActive();
    void OnOK();
    BOOL OnKillActive();
    void SetModified(BOOL modified = TRUE) { m_bModified = modified; }
    BOOL IsModified() const { return m_bModified; }

    INT m_iGreetingType = AGT_NONE;
    CMacro m_macros[NMACROS];
    BOOL m_bOKing = FALSE;
    BOOL m_bSetActiveNeverCalled = TRUE;

private:
    void OnMesgCountChg();
    void OnIntervalChg();
    void OnAutoIgnore();
    void OnNogreeting();
    void OnSaygreeting();
    void OnWhispergreeting();
    void OnAddMacro();
    void OnDeleteMacro();
    void OnSelchangeKey();
    void OnChangeGreetingMesg();
    void LimitRichText(CRtfCtrl* control);

    CRtfCtrl* m_rtfMacro = nullptr;
    CRtfCtrl* m_rtfGreetingMesg = nullptr;
    QLineEdit* m_macroNameCtl = nullptr;
    QLineEdit* m_mesgCntCtl = nullptr;
    QLineEdit* m_intervalCtl = nullptr;
    QComboBox* m_keyCtl = nullptr;
    QSpinBox* m_spinMesgCnt = nullptr;
    QSpinBox* m_spinInterval = nullptr;
    QCheckBox* m_autoIgnore = nullptr;
    QRadioButton* m_noGreeting = nullptr;
    QRadioButton* m_whisperGreeting = nullptr;
    QRadioButton* m_sayGreeting = nullptr;
    QPushButton* m_addMacro = nullptr;
    QPushButton* m_deleteMacro = nullptr;
    BOOL m_bAutoIgnore = FALSE;
    UINT m_uMesgCnt = 1;
    UINT m_uInterval = 1;
    QString m_strMesgCnt;
    QString m_strInterval;
    BOOL m_bModified = FALSE;
    BOOL m_bUpdating = FALSE;
};

class CRuleIcons {
public:
    CRuleIcons();
    QIcon GetIcon(SHORT index) const;

private:
    QIcon m_icons[g_nIconCount];
};

class CRuleSetsPage;

class CRuleSetsListBox : public QListWidget {
public:
    explicit CRuleSetsListBox(QWidget* parent = nullptr);

    void AddRuleSet(CCRuleSet* ruleSet, INT index = -1);
    CCRuleSet* RuleSetAt(INT index) const;
    void SwitchActivation(INT index);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void UpdateItem(INT index);
    CRuleIcons m_icons;
};

class CRulesPage;

class CRulesListCtrl : public QTreeWidget {
public:
    explicit CRulesListCtrl(QWidget* parent = nullptr);

    BOOL bFill(CCDynaRules* dynaRules);
    BOOL bAddRule(CCRule* rule, INT index = -1);
    INT iGetSelectedRule(CCRule** rule = nullptr) const;
    void SwitchActivation(INT index);
    CCRule* RuleAt(INT index) const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void UpdateItem(QTreeWidgetItem* item, CCRule* rule);
    CCRuleSet* m_pRuleSet = nullptr;
    QIcon m_statusIcons[3];
};

class CAdvancedEventParams : public QDialog {
public:
    explicit CAdvancedEventParams(WORD flags, QWidget* parent = nullptr);

    INT m_iMatchCase = 0;
    INT m_iMatchWord = 0;

protected:
    void accept() override;

private:
    QCheckBox* m_matchCase = nullptr;
    QCheckBox* m_matchWord = nullptr;
};

class CAdvancedRuleSettings : public QDialog {
public:
    CAdvancedRuleSettings(UCHAR occurrences, UCHAR interval,
                          QWidget* parent = nullptr);

    UCHAR m_uOcc = 1;
    UCHAR m_uInt = 1;

protected:
    void accept() override;

private:
    QSpinBox* m_spinOcc = nullptr;
    QSpinBox* m_spinInt = nullptr;
};

class CSoundComboBox : public QComboBox {
public:
    explicit CSoundComboBox(QWidget* parent = nullptr);
    void SetFilled(BOOL filled) { m_bFilled = filled; }
    BOOL GetFilled() const { return m_bFilled; }

private:
    BOOL m_bFilled = FALSE;
};

class CEditRule : public QDialog {
public:
    explicit CEditRule(QWidget* parent = nullptr);
    ~CEditRule() override;

    void UseRule(CCRule* rule, CCDynaRules* dynaRules);
    UCHAR GetDelay() const { return m_uDelay; }
    BOOL InitializeRule();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool focusNextPrevChild(bool next) override;
    void showEvent(QShowEvent* event) override;
    void accept() override;

private:
    BOOL bFillActionsFromEvent(BOOL* actionChanged = nullptr);
    void FillParamLabels(BOOL events, BOOL actions);
    BOOL bFillParamsFromRule(BOOL events, BOOL actions);
    BOOL bCorrectActionKeys();
    void SaveComboParams(BOOL events, BOOL actions);
    void SaveRuleParams();
    void UpdateAdvancedControls();
    void OnEventChanged();
    void OnActionChanged();
    void OnFlagsCheckClick();
    void OnAdvancedClick();
    void SetDescription(const QString& description);
    QString EventParamText(UINT index) const;
    QString ActionParamText(UINT index) const;
    void SetEventParamText(UINT index, const QString& text);
    void SetActionParamText(UINT index, const QString& text,
                            CDWordArray* formatting = nullptr);
    BOOL IsUnsupportedAction(enumActions action) const;

    CRtfCmb* m_cmbEvents = nullptr;
    QComboBox* m_cmbEventParams[g_uMaxEventParams]{};
    CRtfCmb* m_cmbActions = nullptr;
    CRtfCmb* m_cmbActionParams[g_uMaxActionParams]{};
    CChatServiceComboBox* m_cmbActionNetParam = nullptr;
    QLabel* m_lblEventParams[g_uMaxEventParams]{};
    QLabel* m_lblActionParams[g_uMaxActionParams]{};
    QLabel* m_lblParamDesc = nullptr;
    QLabel* m_lblEventHeader = nullptr;
    QLabel* m_lblActionHeader = nullptr;
    QPushButton* m_btnAdvanced = nullptr;
    QCheckBox* m_chkSubRules = nullptr;
    QSpinBox* m_spinDelay = nullptr;
    QPushButton* m_ok = nullptr;
    QPushButton* m_cancel = nullptr;
    UCHAR m_uMinDelay = 0;
    UCHAR m_uDelay = 0;
    CCRule* m_pRule = nullptr;
    CCDynaRules* m_pDynaRules = nullptr;
    QString m_rgstrParamLabels[ptMax];
    QString m_rgstrEventParams[ptMax];
    QString m_rgstrActionParams[ptMax];
    CDWordArray* m_prgdwActionParamFormatting[ptMax]{};
    BOOL m_bInitialized = FALSE;
    BOOL m_bUpdating = FALSE;
};

class CAddToSets : public QDialog {
public:
    CAddToSets(CCDynaRules* dynaCopy, CCRuleSet* ruleSet, CCRule* rule,
               QWidget* parent = nullptr);
    BOOL bRuleAdded() const { return m_bRuleAdded; }

protected:
    void accept() override;

private:
    BOOL bFillRuleSets();
    BOOL bAddToSets();
    CCDynaRules* m_pDynaCopy = nullptr;
    CCRuleSet* m_pRuleSet = nullptr;
    CCRule* m_pRule = nullptr;
    QTreeWidget* m_lstRule = nullptr;
    QListWidget* m_lstSets = nullptr;
    BOOL m_bRuleAdded = FALSE;
};

class CSetNameConflict : public QDialog {
public:
    CSetNameConflict(const QString& setName, CRuleSetsListBox* sets,
                     QWidget* parent = nullptr);
    QString m_strSetName;

private:
    void OnOverwriteRuleSetClick();
    void OnRenameRuleSetClick();
    void OnRenamedRuleSetChanged();
    CRuleSetsListBox* m_plstSets = nullptr;
    QLineEdit* m_editSetName = nullptr;
    QPushButton* m_rename = nullptr;
};

class CCreateSet : public QDialog {
public:
    explicit CCreateSet(CRuleSetsListBox* sets, QWidget* parent = nullptr);
    QString m_strSetName;

protected:
    void accept() override;

private:
    void OnCreatedRuleSetChanged();
    CRuleSetsListBox* m_plstSets = nullptr;
    QLineEdit* m_editSetName = nullptr;
    QPushButton* m_ok = nullptr;
};

class CRenameSet : public QDialog {
public:
    CRenameSet(CCRuleSet* ruleSet, CRuleSetsListBox* sets,
               QWidget* parent = nullptr);

protected:
    void accept() override;

private:
    void OnRenamedRuleSetChanged();
    CRuleSetsListBox* m_plstSets = nullptr;
    CCRuleSet* m_pRuleSet = nullptr;
    QLineEdit* m_editSetName = nullptr;
    QPushButton* m_ok = nullptr;
    QString m_strSetName;
};

class CRuleSetsPage : public QWidget {
public:
    explicit CRuleSetsPage(QWidget* parent = nullptr);
    void SetDynaRules(CCDynaRules* dynaCopy);
    BOOL OnSetActive();
    void OnOK();
    void SetModified(BOOL modified = TRUE) { m_bModified = modified; }
    BOOL IsModified() const { return m_bModified; }

private:
    friend class CRuleSetsListBox;
    void UpdateButtonsStatus();
    void SetCurRuleSet(CCRuleSet* ruleSet, INT index);
    BOOL bFillRuleSets();
    void OnCreateRuleSet();
    void OnRenameRuleSet();
    void OnDeleteRuleSet();
    void OnMoveUpRuleSet();
    void OnMoveDownRuleSet();
    void OnLoadRuleSet();
    void OnSaveRuleSet();
    void OnRuleSetItemChanged();
    void AddLoadedRuleSet(CCRuleSet* ruleSet);

    CRuleSetsListBox* m_lstSets = nullptr;
    QPushButton* m_create = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_delete = nullptr;
    QPushButton* m_moveUp = nullptr;
    QPushButton* m_moveDown = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_load = nullptr;
    CCDynaRules* m_pDynaCopy = nullptr;
    BOOL m_bModified = FALSE;
};

class CRulesPage : public QWidget {
public:
    explicit CRulesPage(QWidget* parent = nullptr);
    void SetDynaRules(CCDynaRules* dynaCopy);
    BOOL OnSetActive();
    void OnOK();
    void SetModified(BOOL modified = TRUE) { m_bModified = modified; }
    BOOL IsModified() const { return m_bModified; }

private:
    friend class CRulesListCtrl;
    void UpdateButtonsStatus();
    void SetCurRuleSet(CCRuleSet* ruleSet, INT index);
    BOOL bFillRuleSets();
    void OnRuleSetItemChanged();
    void OnAddRule();
    void OnEditRule();
    void OnDeleteRule();
    void OnDuplicateRule();
    void OnMoveUpRule();
    void OnMoveDownRule();
    void OnAddToRuleSets();
    void OnAdvancedRuleSettings();

    CRulesListCtrl* m_lstRules = nullptr;
    QListWidget* m_lstSets = nullptr;
    QPushButton* m_add = nullptr;
    QPushButton* m_edit = nullptr;
    QPushButton* m_delete = nullptr;
    QPushButton* m_duplicate = nullptr;
    QPushButton* m_addToSets = nullptr;
    QPushButton* m_moveUp = nullptr;
    QPushButton* m_moveDown = nullptr;
    QPushButton* m_advanced = nullptr;
    CCDynaRules* m_pDynaCopy = nullptr;
    BOOL m_bRulesColumnSet = FALSE;
    BOOL m_bModified = FALSE;
};
