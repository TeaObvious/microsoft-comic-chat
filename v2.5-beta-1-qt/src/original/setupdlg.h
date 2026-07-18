// Ported from v2.5-beta-1-modern/setupdlg.h.

#pragma once

#include "wincompat.h"

#include <QDialog>

class CBackgroundPage;
class CCharacterPage;
class CPersonalPage;
class CSetupPage;
class CChatService;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTabWidget;

class CSetupDlg : public QDialog {
public:
    explicit CSetupDlg(QWidget* parent = nullptr);

    QString server() const;
    QString nickname() const;
    QString realName() const;
    QString channel() const;
    int onConnectAction() const;

    void accept() override;

private:
    QTabWidget* m_tabs = nullptr;
    CSetupPage* m_setupPage = nullptr;
    CPersonalPage* m_personalPage = nullptr;
    CCharacterPage* m_characterPage = nullptr;
    CBackgroundPage* m_backgroundPage = nullptr;
};

class CChannelDlg : public QDialog {
public:
    explicit CChannelDlg(QWidget* parent = nullptr);
    QString channel() const;
    QString password() const;
    void accept() override;

    QString m_strChannel;
    QString m_strPassword;
    BOOL m_bIsIRCX = FALSE;

protected:
    void showEvent(QShowEvent* event) override;

private:
    QLineEdit* m_channel = nullptr;
    QLineEdit* m_password = nullptr;
    QPushButton* m_ok = nullptr;
    BOOL m_bInitialized = FALSE;
};

class CNicknameDlg : public QDialog {
public:
    explicit CNicknameDlg(QWidget* parent = nullptr);
    void accept() override;

    QString m_label;
    QString m_strNickname;
    BOOL m_bSpacesAllowed = TRUE;

protected:
    void showEvent(QShowEvent* event) override;

private:
    QLabel* m_staticNick = nullptr;
    QLineEdit* m_editNick = nullptr;
};

class CPasswordDlg : public QDialog {
public:
    explicit CPasswordDlg(QWidget* parent = nullptr);
    void accept() override;

    QString m_strPassword;
    QString m_strMessage;

protected:
    void showEvent(QShowEvent* event) override;

private:
    QLabel* m_message = nullptr;
    QLineEdit* m_password = nullptr;
};

const char* GetMyCharacter();
void SetMyCharacter(const QString& characterName);
const char* GetMyName();
const char* GetMyScreenName();
const char* GetMyNickName();
const char* GetMyIdent();
const char* GetMyServer();
const char* GetMyPhysicalServer();
const char* GetMyRealName();
const char* GetMyChannel();
const char* GetMyHomePage();
const char* GetMyEmail();
const char* GetMyUserName();
void SetMyIdent(const QString& ident);
void SetMyName(const QString& name);
void SetMyNameNick(const QString& nickname);
void SetMyRealName(const QString& realName);
void SetMyHomePage(const QString& homePage);
void SetMyEmail(const QString& email);
void ChatSetChannel(const QString& channelName);
void ChatSetServer(const QString& serverName);
CChatService* AddToServerList(const QString& service);
