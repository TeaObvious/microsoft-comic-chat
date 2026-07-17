// Ported from v2.5-beta-1-modern/admindlg.h.
// Qt widgets replace only the MFC dialog/control boundary.

#pragma once

#include "wincompat.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QShowEvent;
class QTextEdit;
class QWidget;

class CKickDialog : public QDialog {
public:
    explicit CKickDialog(QWidget* parent = nullptr);

    QString m_reason;
    QString m_strKick;
    BOOL m_bBanToo = FALSE;
    QString m_strBanPattern;

    QLabel* m_Kick = nullptr;
    QTextEdit* m_reasonEdit = nullptr;
    QCheckBox* m_banToo = nullptr;
    QLineEdit* m_banPattern = nullptr;

    void OnBantoo();
    void accept() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void initializeDialog();

    BOOL m_bInitialized = FALSE;
};

class CBanDlg : public QDialog {
public:
    explicit CBanDlg(QWidget* parent = nullptr);

    QString m_strMesg;
    QString m_strBanPattern;
    QStringList* m_banArray = nullptr;
    QString m_szEncodedChannel;

    // CBS_SIMPLE is represented by one resource-sized holder containing its
    // permanently visible edit and sorted list portions.
    QWidget* m_ctlBans = nullptr;
    QLineEdit* m_banEdit = nullptr;
    QListWidget* m_banList = nullptr;
    QPushButton* m_banButton = nullptr;
    QPushButton* m_unbanButton = nullptr;

    void DoBan(BOOL ban);
    void OnBan();
    void OnUnban();
    void OnEditChange();
    void OnSelchange();
    void accept() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void initializeDialog();
    int findExact(const QString& pattern) const;
    void addSorted(const QString& pattern);

    QLabel* m_message = nullptr;
    BOOL m_bInitialized = FALSE;
};

class CInviteDlg : public QDialog {
public:
    explicit CInviteDlg(QWidget* parent = nullptr);

    QString m_strInvitees;
    QTextEdit* m_invitees = nullptr;

    void accept() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void initializeDialog();

    BOOL m_bInitialized = FALSE;
};

class CInvitationDlg : public QDialog {
public:
    explicit CInvitationDlg(QWidget* parent = nullptr);

    BOOL m_bIgnore = FALSE;
    QString m_strMessage;
    QCheckBox* m_ignore = nullptr;
    QLabel* m_message = nullptr;

    void OnNo();
    void OnYes();
    void reject() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void initializeDialog();

    BOOL m_bInitialized = FALSE;
};
