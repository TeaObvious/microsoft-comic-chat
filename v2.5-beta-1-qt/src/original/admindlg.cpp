// Ported from v2.5-beta-1-modern/admindlg.cpp.

#include "admindlg.h"

#include "chatprot.h"
#include "defines.h"
#include "ircproto.h"
#include "originalassets.h"
#include "protsupp.h"
#include "resource.h"

#include <QCheckBox>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QTextCursor>
#include <QTextEdit>

namespace {
class DialogUnitMapper {
public:
    explicit DialogUnitMapper(const QFont& font)
    {
        const QFontMetrics metrics(font);
        const QString alphabet = QStringLiteral(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        m_baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
        m_baseY = qMax(1, metrics.height());
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
                  const DialogUnitMapper& mapper,
                  const QString& identifier, int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = findControl(
            dialog, identifier, occurrence)) {
        widget->setGeometry(mapper.rect(*control));
        widget->setVisible(control->visible);
    }
}

QFont resourceFont(const OriginalDialogResource& dialog)
{
    QFont font(dialog.fontFamily);
    if (dialog.fontPointSize > 0) font.setPointSize(dialog.fontPointSize);
    return font;
}

void limitTextEdit(QTextEdit* edit, int maximum)
{
    if (!edit || edit->toPlainText().size() <= maximum) return;
    const QString text = edit->toPlainText().left(maximum);
    edit->blockSignals(true);
    edit->setPlainText(text);
    QTextCursor cursor = edit->textCursor();
    cursor.movePosition(QTextCursor::End);
    edit->setTextCursor(cursor);
    edit->blockSignals(false);
}

void trimLeft(QString& value)
{
    while (!value.isEmpty() && value.front().isSpace()) value.remove(0, 1);
}
}

CKickDialog::CKickDialog(QWidget* parent)
    : QDialog(parent)
{
    const QString resource = QStringLiteral("IDD_KICK");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    m_Kick = new QLabel(this);
    placeControl(m_Kick, dialog, mapper, QStringLiteral("IDC_KICKMSG"));

    m_reasonEdit = new QTextEdit(this);
    m_reasonEdit->setAcceptRichText(false);
    placeControl(m_reasonEdit, dialog, mapper, QStringLiteral("IDC_REASON"));
    connect(m_reasonEdit, &QTextEdit::textChanged, this, [this] {
        limitTextEdit(m_reasonEdit, MAX_INPUTLEN);
    });

    m_banToo = new QCheckBox(originalDialogControlText(
        resource, QStringLiteral("IDC_BANTOO")), this);
    placeControl(m_banToo, dialog, mapper, QStringLiteral("IDC_BANTOO"));
    connect(m_banToo, &QCheckBox::clicked, this,
            [this] { OnBantoo(); });

    m_banPattern = new QLineEdit(this);
    placeControl(m_banPattern, dialog, mapper,
                 QStringLiteral("IDC_BANTOO_NAME"));

    auto* ok = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDOK")), this);
    ok->setDefault(true);
    placeControl(ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDCANCEL")), this);
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    connect(ok, &QPushButton::clicked, this, &CKickDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void CKickDialog::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) initializeDialog();
    QDialog::showEvent(event);
}

void CKickDialog::initializeDialog()
{
    m_bInitialized = TRUE;
    m_Kick->setText(m_strKick);
    m_reasonEdit->setPlainText(m_reason);
    m_banToo->setChecked(m_bBanToo);
    m_banPattern->setText(m_strBanPattern);
    m_banPattern->setEnabled(m_bBanToo);
}

void CKickDialog::OnBantoo()
{
    m_bBanToo = !m_bBanToo;
    m_banToo->setChecked(m_bBanToo);
    m_banPattern->setEnabled(m_bBanToo);
}

void CKickDialog::accept()
{
    m_reason = m_reasonEdit->toPlainText();
    m_bBanToo = m_banToo->isChecked();
    m_strBanPattern = m_banPattern->text();
    QDialog::accept();
}

CBanDlg::CBanDlg(QWidget* parent)
    : QDialog(parent)
{
    const QString resource = QStringLiteral("IDD_BAN");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    placeControl(m_message, dialog, mapper, QStringLiteral("IDC_BANMESG"));

    m_ctlBans = new QWidget(this);
    placeControl(m_ctlBans, dialog, mapper,
                 QStringLiteral("IDC_BANNED_USERS"));
    const int editHeight = qMin(m_ctlBans->height(), mapper.y(14));
    m_banEdit = new QLineEdit(m_ctlBans);
    m_banEdit->setGeometry(0, 0, m_ctlBans->width(), editHeight);
    m_banList = new QListWidget(m_ctlBans);
    m_banList->setGeometry(0, qMax(0, editHeight - 1),
                           m_ctlBans->width(),
                           m_ctlBans->height() - qMax(0, editHeight - 1));
    connect(m_banEdit, &QLineEdit::textChanged, this,
            [this] { OnEditChange(); });
    connect(m_banList, &QListWidget::currentTextChanged, this,
            [this](const QString& value) {
                if (value.isNull()) return;
                m_banEdit->setText(value);
                OnSelchange();
            });

    m_banButton = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDBAN")), this);
    placeControl(m_banButton, dialog, mapper, QStringLiteral("IDBAN"));
    m_banButton->setDefault(true);
    m_unbanButton = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDUNBAN")), this);
    placeControl(m_unbanButton, dialog, mapper, QStringLiteral("IDUNBAN"));
    auto* ok = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDOK")), this);
    placeControl(ok, dialog, mapper, QStringLiteral("IDOK"));

    connect(m_banButton, &QPushButton::clicked, this, &CBanDlg::OnBan);
    connect(m_unbanButton, &QPushButton::clicked, this, &CBanDlg::OnUnban);
    connect(ok, &QPushButton::clicked, this, &CBanDlg::accept);
}

void CBanDlg::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) initializeDialog();
    QDialog::showEvent(event);
}

void CBanDlg::initializeDialog()
{
    m_bInitialized = TRUE;
    m_message->setText(m_strMesg);
    m_banList->clear();
    if (m_banArray) {
        for (const QString& encoded : *m_banArray)
            addSorted(DecodeNick(encoded));
    }
    m_banEdit->setText(m_strBanPattern);
    OnEditChange();
}

int CBanDlg::findExact(const QString& pattern) const
{
    for (int index = 0; index < m_banList->count(); ++index) {
        if (m_banList->item(index)->text().compare(
                pattern, Qt::CaseInsensitive) == 0) {
            return index;
        }
    }
    return -1;
}

void CBanDlg::addSorted(const QString& pattern)
{
    int index = 0;
    while (index < m_banList->count()
           && QString::localeAwareCompare(
                  m_banList->item(index)->text(), pattern) < 0) {
        ++index;
    }
    m_banList->insertItem(index, pattern);
}

void CBanDlg::DoBan(BOOL ban)
{
    QString pattern = m_banEdit->text();
    trimLeft(pattern);
    if (pattern.isEmpty() || !currentRoom) return;

    currentRoom->ChatBanUser(pattern, ban, m_szEncodedChannel);
    if (ban) {
        addSorted(pattern);
    } else {
        const int index = findExact(pattern);
        if (index >= 0) delete m_banList->takeItem(index);
    }
}

void CBanDlg::OnBan()
{
    DoBan(TRUE);
    m_unbanButton->setEnabled(true);
    m_unbanButton->setFocus();
    m_banButton->setEnabled(false);
}

void CBanDlg::OnUnban()
{
    DoBan(FALSE);
    m_banButton->setEnabled(true);
    m_unbanButton->setEnabled(false);
    m_banEdit->setFocus();
}

void CBanDlg::OnEditChange()
{
    QString pattern = m_banEdit->text();
    trimLeft(pattern);
    if (pattern.isEmpty()) {
        m_banButton->setEnabled(false);
        m_unbanButton->setEnabled(false);
        return;
    }
    const bool found = findExact(pattern) >= 0;
    m_banButton->setEnabled(!found);
    m_unbanButton->setEnabled(found);
}

void CBanDlg::OnSelchange()
{
    m_banButton->setEnabled(false);
    m_unbanButton->setEnabled(true);
}

void CBanDlg::accept()
{
    m_strBanPattern = m_banEdit->text();
    QDialog::accept();
}

CInviteDlg::CInviteDlg(QWidget* parent)
    : QDialog(parent)
{
    const QString resource = QStringLiteral("IDD_INVITE");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    auto* label = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_STATIC")), this);
    label->setWordWrap(true);
    placeControl(label, dialog, mapper, QStringLiteral("IDC_STATIC"));

    m_invitees = new QTextEdit(this);
    m_invitees->setAcceptRichText(false);
    placeControl(m_invitees, dialog, mapper, QStringLiteral("IDC_INVITEES"));
    connect(m_invitees, &QTextEdit::textChanged, this, [this] {
        limitTextEdit(m_invitees, 255);
    });

    auto* ok = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDOK")), this);
    ok->setDefault(true);
    placeControl(ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDCANCEL")), this);
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    connect(ok, &QPushButton::clicked, this, &CInviteDlg::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void CInviteDlg::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) initializeDialog();
    QDialog::showEvent(event);
}

void CInviteDlg::initializeDialog()
{
    m_bInitialized = TRUE;
    m_invitees->setPlainText(m_strInvitees);
    m_invitees->setFocus();
}

void CInviteDlg::accept()
{
    m_strInvitees = m_invitees->toPlainText();
    QDialog::accept();
}

CInvitationDlg::CInvitationDlg(QWidget* parent)
    : QDialog(parent)
{
    const QString resource = QStringLiteral("IDD_INVITATION");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    placeControl(m_message, dialog, mapper, QStringLiteral("IDC_MESSAGE"));
    auto* explanation = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_STATIC")), this);
    explanation->setWordWrap(true);
    placeControl(explanation, dialog, mapper, QStringLiteral("IDC_STATIC"));

    m_ignore = new QCheckBox(originalDialogControlText(
        resource, QStringLiteral("IDC_IGNORE")), this);
    placeControl(m_ignore, dialog, mapper, QStringLiteral("IDC_IGNORE"));

    auto* no = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDNO")), this);
    no->setDefault(true);
    placeControl(no, dialog, mapper, QStringLiteral("IDNO"));
    auto* yes = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDYES")), this);
    placeControl(yes, dialog, mapper, QStringLiteral("IDYES"));
    connect(no, &QPushButton::clicked, this, &CInvitationDlg::OnNo);
    connect(yes, &QPushButton::clicked, this, &CInvitationDlg::OnYes);
}

void CInvitationDlg::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) initializeDialog();
    QDialog::showEvent(event);
}

void CInvitationDlg::initializeDialog()
{
    m_bInitialized = TRUE;
    m_message->setText(m_strMessage);
    m_ignore->setChecked(m_bIgnore);
}

void CInvitationDlg::OnNo()
{
    m_bIgnore = m_ignore->isChecked();
    done(IDNO);
}

void CInvitationDlg::OnYes()
{
    m_bIgnore = m_ignore->isChecked();
    done(IDYES);
}

void CInvitationDlg::reject()
{
    m_bIgnore = m_ignore->isChecked();
    done(IDCANCEL);
}
