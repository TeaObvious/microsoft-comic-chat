// Ported from v2.5-beta-1-modern/motd.cpp.

#include "motd.h"

#include "chat.h"
#include "defines.h"
#include "format.h"
#include "mainfrm.h"
#include "originalassets.h"

#include <QCheckBox>
#include <QFontMetrics>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTextCharFormat>
#include <QTextCursor>

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
    }
}

QFont resourceFont(const OriginalDialogResource& dialog)
{
    QFont font(dialog.fontFamily);
    if (dialog.fontPointSize > 0) font.setPointSize(dialog.fontPointSize);
    return font;
}
}

CMOTD::CMOTD(QWidget* parent)
    : QDialog(parent)
    , m_edit(this)
{
    const QString resource = QStringLiteral("IDD_MOTD");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    m_showMOTD = new QCheckBox(originalDialogControlText(
        resource, QStringLiteral("IDC_SHOW_MOTD")), this);
    placeControl(m_showMOTD, dialog, mapper,
                 QStringLiteral("IDC_SHOW_MOTD"));
    placeControl(&m_edit, dialog, mapper, QStringLiteral("IDC_EDITPOS"));
    m_edit.setReadOnly(true);
    m_edit.setAcceptRichText(false);

    auto* ok = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDOK")), this);
    ok->setObjectName(QStringLiteral("IDOK"));
    ok->setDefault(true);
    placeControl(ok, dialog, mapper, QStringLiteral("IDOK"));
    connect(ok, &QPushButton::clicked, this, &CMOTD::accept);
}

void CMOTD::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) initializeDialog();
    QDialog::showEvent(event);
}

void CMOTD::initializeDialog()
{
    m_bInitialized = TRUE;
    m_showMOTD->setChecked(m_bShowMOTD);
    m_edit.clear();
    QTextCursor cursor(m_edit.document());
    QTextCharFormat format;
    format.setForeground(QColor(0, 0, 255));
    cursor.setCharFormat(format);
    cursor.insertText(m_strLUSER);
    if (!m_strLUSER.isEmpty()) cursor.insertText(QStringLiteral("\n"));
    format.setForeground(QColor(0, 0, 0));
    cursor.setCharFormat(format);
    cursor.insertText(m_strMOTD);
    cursor.setPosition(0);
    m_edit.setTextCursor(cursor);
    m_edit.setFocus();
}

void CMOTD::accept()
{
    m_bShowMOTD = m_showMOTD->isChecked();
    QDialog::accept();
}

CAwayDlg::CAwayDlg(QWidget* parent)
    : QDialog(parent)
    , m_rtfAwayMsg(this)
{
    const QString resource = QStringLiteral("IDD_AWAYDLG");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    auto* label = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_STATIC")), this);
    label->setBuddy(&m_rtfAwayMsg);
    placeControl(label, dialog, mapper, QStringLiteral("IDC_STATIC"));
    placeControl(&m_rtfAwayMsg, dialog, mapper,
                 QStringLiteral("IDC_AWAYMSG"));

    m_ok = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDOK")), this);
    m_ok->setObjectName(QStringLiteral("IDOK"));
    m_ok->setDefault(true);
    placeControl(m_ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDCANCEL")), this);
    cancel->setObjectName(QStringLiteral("IDCANCEL"));
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));

    connect(m_ok, &QPushButton::clicked, this, &CAwayDlg::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(&m_rtfAwayMsg, &QTextEdit::textChanged, this, [this] {
        QString text = m_rtfAwayMsg.toPlainText();
        if (text.size() > MAX_INPUTLEN) {
            QTextCursor cursor(m_rtfAwayMsg.document());
            cursor.setPosition(MAX_INPUTLEN);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
        updateOK();
    });
}

void CAwayDlg::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) initializeDialog();
    QDialog::showEvent(event);
}

void CAwayDlg::initializeDialog()
{
    m_bInitialized = TRUE;
    m_rtfAwayMsg.UseDefaultCharFormat();
    m_rtfAwayMsg.bSetTextColor(m_rtfAwayMsg.m_crTextColor);
    m_rtfAwayMsg.bSetWindowFormattedText(
        m_rtfAwayMsg.m_strText, m_rtfAwayMsg.m_prgdwFormatting);
    updateOK();
}

void CAwayDlg::updateOK()
{
    QString text = m_rtfAwayMsg.toPlainText();
    while (!text.isEmpty() && text.front().isSpace()) text.remove(0, 1);
    m_ok->setEnabled(!text.isEmpty());
}

void CAwayDlg::accept()
{
    if (!m_ok->isEnabled()) return;
    if (m_rtfAwayMsg.m_pFont) {
        FreeAndNullFormatting(&m_rtfAwayMsg.m_prgdwFormatting);
        m_rtfAwayMsg.m_prgdwFormatting = PRGDWGetFormatting(
            &m_rtfAwayMsg, m_rtfAwayMsg.m_pFont,
            m_rtfAwayMsg.m_crTextColor);
    }
    m_rtfAwayMsg.m_strText = m_rtfAwayMsg.toPlainText();
    QDialog::accept();
}

void ShowMOTD(const QString& lusers, const QString& motd)
{
    CMOTD dialog(theApp.m_pMainWnd.data());
    dialog.m_strLUSER = lusers;
    dialog.m_strMOTD = motd;
    dialog.m_bShowMOTD = (theApp.m_flags1 & F1_SHOWMOTD) != 0;
    dialog.exec();
    if (dialog.m_bShowMOTD) theApp.m_flags1 |= F1_SHOWMOTD;
    else theApp.m_flags1 &= ~DWORD(F1_SHOWMOTD);
}
