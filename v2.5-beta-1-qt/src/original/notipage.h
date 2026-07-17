//=--------------------------------------------------------------------------=
// NotiPage.h -- Qt port of v2.5-beta-1-modern/notipage.h
//=--------------------------------------------------------------------------=
// Qt replaces only MFC property-page, dialog and list-view plumbing. The
// classes, entry points and notification state remain in the original module.

#pragma once

#include "notif.h"

#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QRect>
#include <QSize>
#include <QTreeWidget>
#include <QWidget>

class CUser;
class CChatServiceComboBox;
class QCloseEvent;
class QComboBox;
class QEvent;
class QHideEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QMoveEvent;
class QPushButton;
class QResizeEvent;
class QShowEvent;

inline constexpr char g_szAnyReplacement[] = "*";
constexpr UINT g_uRightMargin = 6;
constexpr UINT g_uLeftMargin = g_uRightMargin;
constexpr UINT g_uTopMargin = g_uRightMargin;
constexpr UINT g_uBottomListMargin = 4;
constexpr UINT g_uBottomButtonMargin = 4;
constexpr UINT g_uBottomLabelMargin = 3;
constexpr UINT g_uInterButton = 4;

class CNotificationsPage;

class CNotifsListCtrl : public QTreeWidget {
public:
    explicit CNotifsListCtrl(CNotificationsPage* parent);

    void SetSortSettings(UCHAR sortColumn, BOOL sortAscending)
    {
        m_uSortColumn = sortColumn;
        m_bSortAscending = sortAscending;
    }

    BOOL bFill(CCDynaNotifs* dynaNotifs);
    BOOL bAddNotif(CCNotif* notif, INT index = -1);
    INT iGetSelectedNotif(CCNotif** notif) const;
    INT iGetSortPosition(CCNotif* notif) const;
    void SwitchActivation(INT index);

    CCNotif* NotifAt(INT index) const;
    UCHAR SortColumn() const { return m_uSortColumn; }
    BOOL SortAscending() const { return m_bSortAscending; }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    friend class CNotificationsPage;

    void OnColumnClick(INT column);
    void UpdateItem(QTreeWidgetItem* item, CCNotif* notif);

    CNotificationsPage* m_notificationsPage = nullptr;
    QList<QIcon> m_ilActiveStatus;
    UCHAR m_uSortColumn = 0;
    BOOL m_bSortAscending = FALSE;
};

class CNotificationsPage : public QWidget {
public:
    explicit CNotificationsPage(QWidget* parent = nullptr);

    void SetDynaNotifs(CCDynaNotifs* dynaCopy);
    void SortNotifs(UCHAR sortColumn, BOOL sortAscending);
    BOOL OnSetActive();
    void OnOK();
    void SetModified(BOOL modified = TRUE) { m_bModified = modified; }
    BOOL IsModified() const { return m_bModified; }

private:
    friend class CNotifsListCtrl;

    void UpdateButtonsStatus();
    void FillUpCombos();
    void FillUpParamsFromIdent(QString& ident);
    void OnNotifItemChanged();
    void OnComboChange(INT parameter);
    void OnAddNotifClick();
    void OnModifyNotifClick();
    void OnDeleteNotifClick();

    CNotifsListCtrl* m_lstNotifs = nullptr;
    QComboBox* m_cmbOperators[g_uNotifParamNum - 1]{};
    QLineEdit* m_editArgs[g_uNotifParamNum - 1]{};
    CChatServiceComboBox* m_cmbNetArg = nullptr;
    QPushButton* m_addNotif = nullptr;
    QPushButton* m_modifyNotif = nullptr;
    QPushButton* m_deleteNotif = nullptr;
    BOOL m_bNotifsColumnSet = FALSE;
    BOOL m_bActivateNew = TRUE;
    BOOL m_bFreezeButtons = FALSE;
    BOOL m_bModified = FALSE;
    CCDynaNotifs* m_pDynaCopy = nullptr;
};

class CNotificationUsers : public QDialog {
public:
    explicit CNotificationUsers(QWidget* parent = nullptr);

    void SetPostCreate(BOOL value) { m_bPostCreate = value; }
    BOOL bFillList(CCItemPtrArray* notifUsers, UINT modifiedUsersCount);
    BOOL bSignalNewEntries();
    BOOL bSignalNewUpdate();

    CUser* GetSelectedUser() const;
    QString GetSelectedNickname() const;
    void UpdateButtons();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void reject() override;

private:
    INT iFindUserIndex(CUser* user, INT lastItemsCount) const;
    BOOL bUpdateCountLabel();
    BOOL bUpdateLastUpdateLabel();
    void RedirectFocus();
    void SaveNotifCoords();
    void OnCloseDialog();
    void OnColumnClick(INT column);
    void OnNotifInvite();
    void OnNotifWhisper();
    void OnNotifJoin();
    void OnNotifClear();
    void OnDefineNotif();
    void OnNotifUpdate();
    void UpdateItem(QTreeWidgetItem* item, CUser* user);
    void ApplyResize(INT widthDelta, INT heightDelta);

    QTreeWidget* m_lstUsers = nullptr;
    QPushButton* m_defineNotif = nullptr;
    QPushButton* m_notifWhisper = nullptr;
    QPushButton* m_notifInvite = nullptr;
    QPushButton* m_notifJoin = nullptr;
    QPushButton* m_notifUpdate = nullptr;
    QPushButton* m_notifClear = nullptr;
    QPushButton* m_closeNotifUsers = nullptr;
    QLabel* m_notifCount = nullptr;
    QLabel* m_notifTime = nullptr;
    BOOL m_bInverted = FALSE;
    BOOL m_bPostCreate = FALSE;
    BOOL m_bSortAscending = FALSE;
    UCHAR m_uSortColumn = 0;
    QSize m_sizeDialog;
    QSize m_sizeMinimal;
    QList<QIcon> m_ImageList;
    QIcon m_StateIcon;
    QHash<QWidget*, QRect> m_originalControlGeometry;
};
