// Ported from v2.5-beta-1-modern/chanprop.cpp.

#include "chanprop.h"

#include "chat.h"
#include "defines.h"
#include "format.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "protsupp.h"
#include "userinfo.h"

#include <QCheckBox>
#include <QFontMetrics>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShowEvent>
#include <QTextCursor>

#include <limits>

namespace {
class DialogUnitMapper {
public:
    explicit DialogUnitMapper(const QFont& font)
    {
        const QFontMetrics metrics(font);
        const QString alphabet = QStringLiteral(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        m_baseX = (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2;
        m_baseY = metrics.height();
        if (m_baseX < 1) m_baseX = 1;
        if (m_baseY < 1) m_baseY = 1;
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

const OriginalDialogControl* findControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

void placeControl(QWidget* widget, const OriginalDialogResource& dialog,
                  const DialogUnitMapper& mapper, const QString& identifier,
                  int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = findControl(
            dialog, identifier, occurrence)) {
        widget->setGeometry(mapper.rect(*control));
        widget->setVisible(control->visible);
    }
}
}

CChannelProp::CChannelProp(QWidget* parent)
    : CChannelProp(QStringLiteral("IDD_CHANNELPROP"), parent)
{
}

CChannelProp::CChannelProp(const QString& dialogResource, QWidget* parent)
    : QDialog(parent)
    , m_rtfTopic(this)
    , m_dialogResource(dialogResource)
    , m_bCreateDialog(dialogResource == QLatin1String("IDD_CHANNELCREATE"))
{
    DoMyInits();

    const OriginalDialogResource dialog = originalDialogResource(dialogResource);
    QFont dialogFont(dialog.fontFamily);
    if (dialog.fontPointSize > 0) dialogFont.setPointSize(dialog.fontPointSize);
    setFont(dialogFont);
    const DialogUnitMapper mapper(dialogFont);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    if (!m_bCreateDialog) {
        m_chanPropMesg = new QLabel(originalDialogControlText(
            dialogResource, QStringLiteral("IDC_CHANPROP_MESG")), this);
        m_chanPropMesg->setWordWrap(true);
        placeControl(m_chanPropMesg, dialog, mapper,
                     QStringLiteral("IDC_CHANPROP_MESG"));
    } else {
        auto* channelLabel = new QLabel(originalDialogControlText(
            dialogResource, QStringLiteral("IDC_STATIC"), 0), this);
        m_channelCtl = new QLineEdit(this);
        channelLabel->setBuddy(m_channelCtl);
        placeControl(channelLabel, dialog, mapper,
                     QStringLiteral("IDC_STATIC"), 0);
        placeControl(m_channelCtl, dialog, mapper,
                     QStringLiteral("IDC_CHANNELNAME"));
        m_channelCtl->setMaxLength(MAX_IRCXCHANNAME);
        m_channelCtl->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[^,\\s\\r\\n\\a]*")),
            m_channelCtl));
    }

    const int topicStatic = m_bCreateDialog ? 1 : 0;
    auto* topicLabel = new QLabel(originalDialogControlText(
        dialogResource, QStringLiteral("IDC_STATIC"), topicStatic), this);
    topicLabel->setBuddy(&m_rtfTopic);
    placeControl(topicLabel, dialog, mapper,
                 QStringLiteral("IDC_STATIC"), topicStatic);
    placeControl(&m_rtfTopic, dialog, mapper,
                 QStringLiteral("IDC_TOPIC_RICHEDIT"));

    const auto checkBox = [&](const QString& identifier) {
        auto* box = new QCheckBox(originalDialogControlText(
            dialogResource, identifier), this);
        placeControl(box, dialog, mapper, identifier);
        return box;
    };
    m_auditorium = checkBox(QStringLiteral("IDC_AUDITORIUM"));
    m_moderated = checkBox(QStringLiteral("IDC_MODERATED"));
    m_topicAnyone = checkBox(QStringLiteral("IDC_TOPICANYONE"));
    m_inviteOnly = checkBox(QStringLiteral("IDC_INVITEONLY"));
    m_hidden = checkBox(QStringLiteral("IDC_HIDDEN"));
    m_private = checkBox(QStringLiteral("IDC_PRIVATE"));
    m_noWhispers = checkBox(QStringLiteral("IDC_NOWHISPERS"));
    m_setMax = checkBox(QStringLiteral("IDC_SETMAX"));
    m_maxParticipants = new QLineEdit(this);
    placeControl(m_maxParticipants, dialog, mapper,
                 QStringLiteral("IDC_MAXPARTICIPANTS"));
    m_passwordGiven = checkBox(QStringLiteral("IDC_PASSWORD_GIVEN"));
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setMaxLength(MAX_CHANNELPWD);
    placeControl(m_password, dialog, mapper, QStringLiteral("IDC_PASSWORD"));

    m_ok = new QPushButton(originalDialogControlText(
        dialogResource, QStringLiteral("IDOK")), this);
    m_ok->setObjectName(QStringLiteral("IDOK"));
    m_ok->setDefault(true);
    placeControl(m_ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(originalDialogControlText(
        dialogResource, QStringLiteral("IDCANCEL")), this);
    cancel->setObjectName(QStringLiteral("IDCANCEL"));
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));

    connect(m_ok, &QPushButton::clicked, this, &CChannelProp::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_setMax, &QCheckBox::toggled, this, [this](bool checked) {
        m_maxParticipants->setEnabled(checked
            && (m_bCreateDialog || (g_puiSelf && g_puiSelf->IsOperator())));
    });
    connect(m_passwordGiven, &QCheckBox::toggled, this,
            [this](bool checked) {
        m_password->setEnabled(checked
            && (m_bCreateDialog || (g_puiSelf && g_puiSelf->IsOperator())));
    });
    connect(m_hidden, &QCheckBox::clicked, m_private,
            [this] { m_private->setChecked(false); });
    connect(m_private, &QCheckBox::clicked, m_hidden,
            [this] { m_hidden->setChecked(false); });
    connect(&m_rtfTopic, &QTextEdit::textChanged, this, [this] {
        if (m_rtfTopic.toPlainText().size() <= MAX_TOPICLEN) return;
        QTextCursor cursor(m_rtfTopic.document());
        cursor.setPosition(MAX_TOPICLEN);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    });
    if (m_channelCtl) {
        connect(m_channelCtl, &QLineEdit::textChanged, this,
                [this](const QString& text) { m_ok->setEnabled(!text.isEmpty()); });
    }
}

void CChannelProp::DoMyInits()
{
    m_bAuditorium = FALSE;
    m_bSecret = FALSE;
    m_bInviteOnly = FALSE;
    m_bModerated = FALSE;
    m_bPrivate = FALSE;
    m_bTopicAnyone = FALSE;
    m_uMaxParticipants = 0;
    m_bSetMax = FALSE;
    m_bNoWhispers = FALSE;
    m_strPassword.clear();
    m_bSetPassword = FALSE;
    m_bIsIRCX = FALSE;
    m_bWaiveParticipantLimit = FALSE;
}

void CChannelProp::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) initializeDialog();
    QDialog::showEvent(event);
}

void CChannelProp::initializeDialog()
{
    m_bInitialized = TRUE;
    m_bWaiveParticipantLimit = m_uMaxParticipants > 10000U;

    m_auditorium->setChecked(m_bAuditorium);
    m_hidden->setChecked(m_bSecret);
    m_inviteOnly->setChecked(m_bInviteOnly);
    m_moderated->setChecked(m_bModerated);
    m_private->setChecked(m_bPrivate);
    m_topicAnyone->setChecked(m_bTopicAnyone);
    m_setMax->setChecked(m_bSetMax);
    m_noWhispers->setChecked(m_bNoWhispers);
    m_passwordGiven->setChecked(m_bSetPassword);
    m_maxParticipants->setText(QString::number(m_uMaxParticipants));
    m_password->setText(m_strPassword);
    m_maxParticipants->setValidator(new QIntValidator(
        0, m_bWaiveParticipantLimit ? std::numeric_limits<int>::max() : 10000,
        m_maxParticipants));

    m_rtfTopic.UseDefaultCharFormat();
    m_rtfTopic.SetAcceptMultiLine(FALSE);
    if (!m_bCreateDialog) {
        m_rtfTopic.bSetWindowFormattedText(
            m_rtfTopic.m_strText, m_rtfTopic.m_prgdwFormatting);
        const BOOL isOperator = g_puiSelf && g_puiSelf->IsOperator();
        const QString messageIdentifier = isOperator
            ? QStringLiteral("IDS_CHANPROP_ADMIN")
            : (m_bTopicAnyone ? QStringLiteral("IDS_CHANPROP_TOPICONLY")
                              : QStringLiteral("IDS_CHANPROP_NOTADMIN"));
        m_chanPropMesg->setText(originalResourceString(messageIdentifier));
        m_hidden->setEnabled(isOperator);
        m_inviteOnly->setEnabled(isOperator);
        m_moderated->setEnabled(isOperator);
        m_private->setEnabled(isOperator);
        m_topicAnyone->setEnabled(isOperator);
        m_noWhispers->setEnabled(isOperator && serverConn.m_bIrcXServer);
        m_setMax->setEnabled(isOperator);
        m_maxParticipants->setEnabled(m_bSetMax && isOperator);
        m_passwordGiven->setEnabled(isOperator);
        m_password->setEnabled(m_bSetPassword && isOperator);
        m_rtfTopic.setReadOnly(!isOperator && !m_bTopicAnyone);
    } else {
        m_noWhispers->setEnabled(serverConn.m_bIrcXServer);
        m_maxParticipants->setEnabled(m_bSetMax);
        m_password->setEnabled(m_bSetPassword);
        m_channelCtl->setText(m_channelValue ? *m_channelValue : QString());
        m_channelCtl->setMaxLength(m_bIsIRCX
            ? MAX_IRCXCHANNAME : MAX_IRCCHANNAME);
        m_ok->setEnabled(!m_channelCtl->text().isEmpty());
    }
}

void CChannelProp::accept()
{
    bool maxOk = false;
    const uint maxParticipants = m_maxParticipants->text().toUInt(&maxOk);
    const uint maximum = m_bWaiveParticipantLimit
        ? std::numeric_limits<uint>::max() : 10000U;
    if (!maxOk || maxParticipants > maximum) {
        m_maxParticipants->setFocus();
        return;
    }
    if (m_bCreateDialog && m_channelCtl->text().isEmpty()) {
        m_channelCtl->setFocus();
        return;
    }

    m_bAuditorium = m_auditorium->isChecked();
    m_bSecret = m_hidden->isChecked();
    m_bInviteOnly = m_inviteOnly->isChecked();
    m_bModerated = m_moderated->isChecked();
    m_bPrivate = m_private->isChecked();
    m_bTopicAnyone = m_topicAnyone->isChecked();
    m_bSetMax = m_setMax->isChecked();
    m_uMaxParticipants = maxParticipants;
    m_bNoWhispers = m_noWhispers->isChecked();
    m_bSetPassword = m_passwordGiven->isChecked();
    m_strPassword = m_password->text();
    if (m_channelValue) *m_channelValue = m_channelCtl->text();

    m_rtfTopic.m_strText = m_rtfTopic.toPlainText();
    if (m_rtfTopic.m_pFont) {
        FreeAndNullFormatting(&m_rtfTopic.m_prgdwFormatting);
        m_rtfTopic.m_prgdwFormatting = PRGDWGetFormatting(
            &m_rtfTopic, m_rtfTopic.m_pFont, m_rtfTopic.m_crTextColor);
    }
    QDialog::accept();
}

CChannelCreateDlg::CChannelCreateDlg(QWidget* parent)
    : CChannelProp(QStringLiteral("IDD_CHANNELCREATE"), parent)
{
    m_strChannelName.clear();
    m_channelValue = &m_strChannelName;
}
