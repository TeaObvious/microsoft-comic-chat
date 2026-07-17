// Ported from v2.5-beta-1-modern/chanprop.h.

#pragma once

#include "rtfctrl.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;

class CChannelProp : public QDialog {
public:
    explicit CChannelProp(QWidget* parent = nullptr);
    void DoMyInits();

    BOOL m_bAuditorium = FALSE;
    BOOL m_bSecret = FALSE;
    BOOL m_bInviteOnly = FALSE;
    BOOL m_bModerated = FALSE;
    BOOL m_bPrivate = FALSE;
    CRtfCtrl m_rtfTopic;
    BOOL m_bTopicAnyone = FALSE;
    UINT m_uMaxParticipants = 0;
    BOOL m_bSetMax = FALSE;
    BOOL m_bNoWhispers = FALSE;
    QString m_strPassword;
    BOOL m_bSetPassword = FALSE;
    BOOL m_bIsIRCX = FALSE;

    void accept() override;

protected:
    CChannelProp(const QString& dialogResource, QWidget* parent);
    void showEvent(QShowEvent* event) override;

    QString m_dialogResource;
    BOOL m_bWaiveParticipantLimit = FALSE;
    BOOL m_bCreateDialog = FALSE;
    QLineEdit* m_channelCtl = nullptr;
    QString* m_channelValue = nullptr;

private:
    void initializeDialog();

    BOOL m_bInitialized = FALSE;
    QLabel* m_chanPropMesg = nullptr;
    QCheckBox* m_auditorium = nullptr;
    QCheckBox* m_moderated = nullptr;
    QCheckBox* m_topicAnyone = nullptr;
    QCheckBox* m_inviteOnly = nullptr;
    QCheckBox* m_hidden = nullptr;
    QCheckBox* m_private = nullptr;
    QCheckBox* m_noWhispers = nullptr;
    QCheckBox* m_setMax = nullptr;
    QLineEdit* m_maxParticipants = nullptr;
    QCheckBox* m_passwordGiven = nullptr;
    QLineEdit* m_password = nullptr;
    QPushButton* m_ok = nullptr;
};

class CChannelCreateDlg : public CChannelProp {
public:
    explicit CChannelCreateDlg(QWidget* parent = nullptr);

    QString m_strChannelName;
};
