// Ported from v2.5-beta-1-modern/motd.h.

#pragma once

#include "rtfctrl.h"

#include <QDialog>
#include <QTextEdit>

class QCheckBox;
class QPushButton;
class QShowEvent;

class CMOTD : public QDialog {
public:
    explicit CMOTD(QWidget* parent = nullptr);

    QTextEdit m_edit;
    QString m_strLUSER;
    QString m_strMOTD;
    BOOL m_bShowMOTD = FALSE;

    void accept() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void initializeDialog();

    QCheckBox* m_showMOTD = nullptr;
    BOOL m_bInitialized = FALSE;
};

class CAwayDlg : public QDialog {
public:
    explicit CAwayDlg(QWidget* parent = nullptr);

    CRtfCtrl m_rtfAwayMsg;
    void accept() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void initializeDialog();
    void updateOK();

    QPushButton* m_ok = nullptr;
    BOOL m_bInitialized = FALSE;
};

void ShowMOTD(const QString& lusers, const QString& motd);
