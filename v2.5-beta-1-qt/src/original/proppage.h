// Ported from v2.5-beta-1-modern/proppage.h.
// QWidget replaces only the MFC property-page boundary. Resource controls,
// art enumeration and commit order remain owned by the original page classes.

#pragma once

#include "chatsrv.h"
#include "defines.h"
#include "resource.h"
#include "utils.h"
#include "wincompat.h"

#include <QComboBox>
#include <QDialog>
#include <QWidget>

class CAvatarX;
class CBodyCam;
class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QHideEvent;
class QShowEvent;
class QSpinBox;
class QTextEdit;

class CPersonalPage final : public QWidget {
public:
    explicit CPersonalPage(QWidget* parent = nullptr);
    ~CPersonalPage() override;

    QString nickname() const;
    QString realName() const;
    void SetNickname(const QString& nickname);
    bool validate();
    void apply();

private:
    QLineEdit* m_realName = nullptr;
    QLineEdit* m_nickname = nullptr;
    QLineEdit* m_email = nullptr;
    QLineEdit* m_homePage = nullptr;
    QTextEdit* m_profile = nullptr;
};

CPersonalPage* GetPersonalPage();

class CCharacterPage final : public QWidget {
public:
    explicit CCharacterPage(QWidget* parent = nullptr);
    ~CCharacterPage() override;

    void apply();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void selectAvatar();
    void updateCopyright(CAvatarX* avatar);

    QListWidget* m_avatarList = nullptr;
    CBodyCam* m_bodyCam = nullptr;
    QTextEdit* m_copyright = nullptr;
    QString m_selectedName;
};

class CBackgroundPage final : public QWidget {
public:
    explicit CBackgroundPage(QWidget* parent = nullptr);

    void apply();

private:
    void previewSelection();

    QListWidget* m_backgroundList = nullptr;
    QLabel* m_preview = nullptr;
    QTextEdit* m_copyright = nullptr;
    QString m_selectedFile;
};

class CTextFontPage final : public QWidget {
public:
    explicit CTextFontPage(UINT resourceId = IDD_TEXTFONTPAGE_IRC,
                           QWidget* parent = nullptr);

    void OnSetfont();
    void OnLinespaceAll();
    void OnLinespaceDifferent();
    void OnLinespaceNone();
    void OnResetTextfonts();
    void OnHostHdrsBold();
    void OnHostMsgsBold();
    void OnHeaderSeparate();
    void apply();

    BYTE m_spacing = 0;
    BOOL m_bCfInitialized = FALSE;
    BOOL m_bCfHLInitialized = FALSE;
    CHARFORMAT m_cfArray[NFONTS]{};
    BOOL m_bCfChangesMade = FALSE;

private:
    BOOL m_hostHdrsBold = FALSE;
    BOOL m_hostMsgsBold = FALSE;
    BOOL m_bHeaderSeparate = FALSE;
    QRadioButton* m_linespaceAll = nullptr;
    QRadioButton* m_linespaceDifferent = nullptr;
    QRadioButton* m_linespaceNone = nullptr;
    QCheckBox* m_headerSeparate = nullptr;
    QCheckBox* m_hostHeadersBold = nullptr;
    QCheckBox* m_hostMessagesBold = nullptr;
};

void SetTextFont();

class CComicsPropPage final : public QWidget {
public:
    explicit CComicsPropPage(QWidget* parent = nullptr);

    void OnSetfont();
    void OnResetfont();
    void OnSelchangePanels();
    void OnShowComicRTF();
    void OnAutoDownloadChars();
    void OnAutoDownloadBackdrops();
    void apply();

private:
    QComboBox* m_comboPanels = nullptr;
    QCheckBox* m_showComicRtf = nullptr;
    QCheckBox* m_autoDownloadChars = nullptr;
    QCheckBox* m_autoDownloadBackdrops = nullptr;
    int m_nPanelsSel = 0;
    BOOL m_bPanelClicked = FALSE;
    BOOL m_bShowComicRTF = FALSE;
    BOOL m_bAutoDownloadChars = FALSE;
    BOOL m_bAutoDownloadBackdrops = FALSE;
};

void SetComicsFont();

class CServersOnlyComboBox final : public QComboBox {
public:
    explicit CServersOnlyComboBox(QWidget* parent = nullptr);
};

class CServersPage final : public QWidget {
public:
    explicit CServersPage(QWidget* parent = nullptr,
                          CChatServiceList* serviceList = nullptr);
    ~CServersPage() override;

    bool validate();
    bool apply();
    void revert();

    static QString sm_strUnassociatedGroup;

private:
    enum UIUpdateHint {
        updateAll = 0xffff,
        updateChangeCurrSrvGroup = 0x000f,
        updateNewServerGroupText = 0x0002,
        updateAddSrvGroup = 0x000f,
        updateRemoveSrvGroup = 0x000f,
        updateChangeCurrServer = 0x000c,
        updateNewServerText = 0x0004,
        updateAddServer = 0x000e,
        updateRemoveServer = 0x000e,
        updateChangeAuthenticationType = 0x0008,
    };
    enum UIUpdateElement {
        updateelemEnableServerUI = 0x0001,
        updateelemAddRemoveGroup = 0x0002,
        updateelemAddRemoveServer = 0x0004,
        updateelemServerProps = 0x0008,
    };

    void ReloadSettings();
    void UpdateUIState(unsigned int updateHint = updateAll);
    void ChangeServerGroup(bool keystroke, bool force = false);
    void ChangeServer(bool keystroke, bool force = false);
    void SwitchGroup();
    void SwitchServer();
    void SetServerPropsToPage();
    void GetServerPropsFromPage();
    bool AcceptServerSettings();
    bool ValidatePortNumber();
    void OnAddServer();
    void OnRemoveServer();
    void OnAddServerGroup();
    void OnRemoveServerGroup();
    void OnChangeAuthenticationType();
    void OnChangeServerProp();
    HCHATSRVGROUP GetCurrGroup() const;
    HCHATSERVER GetCurrServer() const;

    CChatServiceList* m_pSvcList = nullptr;
    CChatServiceUI m_ui;
    CServersOnlyComboBox* m_comboGroups = nullptr;
    CSimpleComboBox* m_comboServers = nullptr;
    QPushButton* m_addGroup = nullptr;
    QPushButton* m_removeGroup = nullptr;
    QLabel* m_serversLabel = nullptr;
    QPushButton* m_addServer = nullptr;
    QPushButton* m_removeServer = nullptr;
    QGroupBox* m_serverSecurity = nullptr;
    QLabel* m_portLabel = nullptr;
    QSpinBox* m_port = nullptr;
    QLabel* m_connectUsingLabel = nullptr;
    QRadioButton* m_noPassword = nullptr;
    QRadioButton* m_userPassword = nullptr;
    QLabel* m_noPasswordMessage = nullptr;
    QLabel* m_userNameLabel = nullptr;
    QLineEdit* m_userName = nullptr;
    QLabel* m_passwordLabel = nullptr;
    QLineEdit* m_password = nullptr;
    QCheckBox* m_rememberPassword = nullptr;

    QString m_strCurrentGroup;
    QString m_strCurrentServer;
    int m_nPort = 6667;
    int m_nSecurity = 0;
    QString m_strUserName;
    QString m_strPassword;
    bool m_bRememberPassword = false;
    QString m_strSecurityPackages;
    int m_nCurrSelGroup = -2;
    int m_nCurrSelServer = -2;
    bool m_bServerPropChange = false;
    bool m_bInDDX = false;
    bool m_applied = false;
};

class COptionsDialog : public QDialog {
public:
    explicit COptionsDialog(QWidget* parent = nullptr);
    COptionsDialog(BOOL comicsMode, UINT initialPageId,
                   QWidget* parent = nullptr);

private:
    void build(BOOL comicsMode, UINT initialPageId);
};
