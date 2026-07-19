// Ported from v2.5-beta-1-modern/whisprbx.h.
// Qt replaces CDialog/CTabCtrl/CRichEditCtrl only; leaf state and public
// function boundaries retain the original names.

#pragma once

#include "saywnd.h"
#include "textcore.h"

#include <QDialog>
#include <QList>
#include <QRect>
#include <QString>

class CUserInfo;
class CWhisperTabCtrl;
class QCheckBox;
class QCloseEvent;
class QContextMenuEvent;
class QEvent;
class QKeyEvent;
class QMoveEvent;
class QPushButton;
class QResizeEvent;
class QTextEdit;

class CWhisperLeaf {
public:
    explicit CWhisperLeaf(CUserInfo* pui);
    ~CWhisperLeaf();

    QString m_label;
    QString m_nick;
    QString m_id;
    BOOL m_bIgnore = FALSE;
    BOOL m_bModified = FALSE;
    QTextEdit* m_richView = nullptr;
    CTextCore* m_richCore = nullptr;
};

class CWhisperBox : public QDialog {
public:
    explicit CWhisperBox(QWidget* parent = nullptr);
    ~CWhisperBox() override;

    void FreeLeaves();
    int AddTab(CUserInfo* pui);
    int GetTab(const QString& nick) const;
    void SwitchToTab(int tabNumber);
    void SetModified(int tabNumber, BOOL value);
    void OnOK();
    void SaveWhisperCoords();
    QTextEdit* GetCurrentEdit() const;
    void OnDeleteTab();
    void OnIgnoreWbox();
    void OnContextMenu(const QPoint& screenPoint);
    BOOL HandleLink(const QString& link);
    void CycleFocus(BOOL backward);
    void SendScrollKey(QKeyEvent* event);

    int m_currentIndex = -1;
    QList<CWhisperLeaf*> m_leaves;
    CSayWnd* m_sayWnd = nullptr;
    QRect m_richRect;
    BOOL m_bPostCreate = FALSE;
    BOOL m_bInverted = FALSE;
    QCheckBox* m_ignoreButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    CWhisperTabCtrl* m_tabCtrl = nullptr;

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void reject() override;

private:
    void updateChildGeometry();
    void updateRichGeometry();

    int m_sayHeight = 0;
    int m_marginLeft = 0;
    int m_marginTop = 0;
    int m_marginRight = 0;
    int m_marginBottom = 0;
    int m_sayTopFromBottom = 0;
    int m_tabBottomFromBottom = 0;
};

CWhisperBox* GetWhisperBox();
CWhisperBox* CreateWhisperBox();
void WhisperBox(CUserInfo* pui, BOOL giveFocus = TRUE,
                BOOL restore = TRUE);
void DestroyWhisperBox();
void InitializeWhisperCores(BOOL restoreOld);
BOOL bAddToWhisperBox(CUserInfo* pui, USHORT modes,
                      const QString& message);
BOOL bWhisperInBox(const QString& filename, const QString& message,
                   CDWordArray* formatting, USHORT modes);
