// Ported from v2.5-beta-1-modern/rtfcmb.h.

#pragma once

#include "rtfctrl.h"

#include <QComboBox>

class QChangeEvent;
class QFocusEvent;
class QHideEvent;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;

class CRtfCmb;

class CRtfCmbEdit : public CRtfCtrl {
public:
    explicit CRtfCmbEdit(QWidget* parent = nullptr);

    void SetParent(CRtfCmb* parent) { m_pParent = parent; }
    void SetMaximumLength(INT maximumLength);

protected:
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void OnTextChanged();

    CRtfCmb* m_pParent = nullptr;
    INT m_maximumLength = 32767;
    bool m_enforcingLimit = false;
};

class CRtfCmb : public QComboBox {
public:
    explicit CRtfCmb(QWidget* parent = nullptr);
    ~CRtfCmb() override;

    QWidget* RedirectFocus();
    CRtfCmbEdit* GetRtfCmbEdit() const { return m_pRtfCtrl; }
    BOOL bSetRtfMode(BOOL rtfMode);
    BOOL bGetRtfMode() const { return m_bRtfMode; }
    BOOL bAttachRtfCtrl(const QString& identifier);
    void RedirectSelection();

    void SetWindowText(const QString& text);
    QString GetWindowText() const;
    BOOL LimitText(INT maximumLength);

    void SyncPlainTextFromRtf();

protected:
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void showPopup() override;
    void hidePopup() override;

private:
    void OnEditChange(const QString& text);
    void OnSelEndOK(INT index);
    void SetRawComboText(const QString& text);
    void UpdateRtfGeometry();

    BOOL m_bAttached = FALSE;
    BOOL m_bRtfMode = FALSE;
    CRtfCmbEdit* m_pRtfCtrl = nullptr;
    Qt::FocusPolicy m_plainFocusPolicy = Qt::StrongFocus;
};
