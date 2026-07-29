// Ported from v2.5-beta-1-modern/rtfcmb.cpp.

#include "rtfcmb.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QHideEvent>
#include <QLineEdit>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTextDocument>
#include <QTextCursor>

CRtfCmbEdit::CRtfCmbEdit(QWidget* parent)
    : CRtfCtrl(parent)
{
    SetAcceptMultiLine(FALSE);
    setTabChangesFocus(true);
    setLineWrapMode(QTextEdit::NoWrap);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setContentsMargins(0, 0, 0, 0);
    document()->setDocumentMargin(0.0);
    connect(this, &QTextEdit::textChanged,
            this, &CRtfCmbEdit::OnTextChanged);
}

void CRtfCmbEdit::SetMaximumLength(INT maximumLength)
{
    m_maximumLength = qMax(0, maximumLength);
    OnTextChanged();
}

void CRtfCmbEdit::OnTextChanged()
{
    if (m_enforcingLimit) return;
    m_enforcingLimit = true;
    QTextCursor activeCursor = textCursor();
    INT activePosition = activeCursor.position();
    INT activeAnchor = activeCursor.anchor();
    QString text = toPlainText();
    for (INT index = text.size() - 1; index >= 0; --index) {
        const QChar character = text.at(index);
        if (character != QLatin1Char('\r')
            && character != QLatin1Char('\n')
            && character.unicode() != 0x2028
            && character.unicode() != 0x2029) {
            continue;
        }
        QTextCursor removal(document());
        removal.setPosition(index);
        removal.setPosition(index + 1, QTextCursor::KeepAnchor);
        removal.removeSelectedText();
        if (index < activePosition) --activePosition;
        if (index < activeAnchor) --activeAnchor;
    }
    text = toPlainText();
    if (text.size() > m_maximumLength) {
        QTextCursor cursor(document());
        cursor.setPosition(m_maximumLength);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    const INT finalLength = toPlainText().size();
    activePosition = qBound(0, activePosition, finalLength);
    activeAnchor = qBound(0, activeAnchor, finalLength);
    activeCursor = textCursor();
    activeCursor.setPosition(activeAnchor);
    activeCursor.setPosition(activePosition, QTextCursor::KeepAnchor);
    setTextCursor(activeCursor);
    m_enforcingLimit = false;
    if (m_pParent) m_pParent->SyncPlainTextFromRtf();
}

void CRtfCmbEdit::focusInEvent(QFocusEvent* event)
{
    if (!m_bColorWnd) selectAll();
    CRtfCtrl::focusInEvent(event);
}

void CRtfCmbEdit::focusOutEvent(QFocusEvent* event)
{
    CRtfCtrl::focusOutEvent(event);
    if (m_bSelectAll) {
        QTextCursor cursor = textCursor();
        cursor.setPosition(0);
        setTextCursor(cursor);
        viewport()->setCursor(Qt::ArrowCursor);
    }
}

CRtfCmb::CRtfCmb(QWidget* parent)
    : QComboBox(parent)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    m_plainFocusPolicy = focusPolicy();
    if (lineEdit()) {
        connect(lineEdit(), &QLineEdit::textEdited,
                this, &CRtfCmb::OnEditChange);
    }
    connect(this, &QComboBox::activated,
            this, &CRtfCmb::OnSelEndOK);
}

CRtfCmb::~CRtfCmb()
{
    delete m_pRtfCtrl;
    m_pRtfCtrl = nullptr;
}

BOOL CRtfCmb::bSetRtfMode(BOOL rtfMode)
{
    if (rtfMode == m_bRtfMode) return TRUE;
    if (rtfMode) {
        QWidget* owner = parentWidget() ? parentWidget() : this;
        m_pRtfCtrl = new CRtfCmbEdit(owner);
        if (!m_pRtfCtrl) return FALSE;
        m_pRtfCtrl->SetParent(this);
        m_pRtfCtrl->setFont(font());
        m_bRtfMode = TRUE;
        m_bAttached = FALSE;
        setFocusPolicy(Qt::ClickFocus);
        setFocusProxy(m_pRtfCtrl);
        return TRUE;
    }

    delete m_pRtfCtrl;
    m_pRtfCtrl = nullptr;
    m_bRtfMode = FALSE;
    m_bAttached = FALSE;
    setFocusProxy(nullptr);
    setFocusPolicy(m_plainFocusPolicy);
    return TRUE;
}

BOOL CRtfCmb::bAttachRtfCtrl(const QString& identifier)
{
    if (!m_pRtfCtrl || m_bAttached) return TRUE;
    m_pRtfCtrl->setObjectName(identifier);
    m_pRtfCtrl->DefineDefaultCharFormat();
    m_pRtfCtrl->UseDefaultCharFormat();
    m_pRtfCtrl->setEnabled(isEnabled());
    m_bAttached = TRUE;
    UpdateRtfGeometry();
    m_pRtfCtrl->setVisible(isVisible());
    m_pRtfCtrl->raise();
    return TRUE;
}

QWidget* CRtfCmb::RedirectFocus()
{
    if (!m_bRtfMode || !m_pRtfCtrl) return nullptr;
    QWidget* previous = QApplication::focusWidget();
    m_pRtfCtrl->setFocus(Qt::OtherFocusReason);
    return previous;
}

void CRtfCmb::RedirectSelection()
{
    if (!m_bRtfMode || !m_pRtfCtrl || !lineEdit()) return;
    INT start = lineEdit()->selectionStart();
    if (start < 0) start = lineEdit()->cursorPosition();
    const INT end = start + lineEdit()->selectedText().size();
    QTextCursor cursor = m_pRtfCtrl->textCursor();
    cursor.setPosition(qBound(0, start, m_pRtfCtrl->toPlainText().size()));
    cursor.setPosition(qBound(0, end, m_pRtfCtrl->toPlainText().size()),
                       QTextCursor::KeepAnchor);
    m_pRtfCtrl->setTextCursor(cursor);
}

void CRtfCmb::SetWindowText(const QString& text)
{
    if (m_bRtfMode && m_pRtfCtrl)
        m_pRtfCtrl->setPlainText(text);
    else
        setEditText(text);
}

QString CRtfCmb::GetWindowText() const
{
    return m_bRtfMode && m_pRtfCtrl
        ? m_pRtfCtrl->toPlainText() : currentText();
}

BOOL CRtfCmb::LimitText(INT maximumLength)
{
    if (lineEdit()) lineEdit()->setMaxLength(maximumLength);
    if (m_pRtfCtrl) m_pRtfCtrl->SetMaximumLength(maximumLength);
    return TRUE;
}

void CRtfCmb::SyncPlainTextFromRtf()
{
    if (m_pRtfCtrl) SetRawComboText(m_pRtfCtrl->toPlainText());
}

void CRtfCmb::SetRawComboText(const QString& text)
{
    if (!lineEdit()) return;
    const QSignalBlocker lineBlocker(lineEdit());
    const QSignalBlocker comboBlocker(this);
    lineEdit()->setText(text);
}

void CRtfCmb::OnEditChange(const QString& text)
{
    if (m_bRtfMode && m_pRtfCtrl)
        m_pRtfCtrl->setPlainText(text);
}

void CRtfCmb::OnSelEndOK(INT index)
{
    if (!m_bRtfMode || !m_pRtfCtrl || index < 0 || index >= count())
        return;
    const QString text = itemText(index);
    m_pRtfCtrl->setPlainText(text);
    m_pRtfCtrl->selectAll();
    m_pRtfCtrl->UseDefaultCharFormat(TRUE);
    RedirectFocus();
}

void CRtfCmb::UpdateRtfGeometry()
{
    if (!m_bAttached || !m_pRtfCtrl || !m_pRtfCtrl->parentWidget()) return;
    QStyleOptionComboBox option;
    initStyleOption(&option);
    const QRect editRect = style()->subControlRect(
        QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
    const QPoint topLeft = mapTo(
        m_pRtfCtrl->parentWidget(), editRect.topLeft());
    m_pRtfCtrl->setGeometry(QRect(topLeft, editRect.size()));
    m_pRtfCtrl->raise();
}

void CRtfCmb::showPopup()
{
    RedirectSelection();
    QComboBox::showPopup();
}

void CRtfCmb::hidePopup()
{
    QComboBox::hidePopup();
    RedirectFocus();
}

void CRtfCmb::showEvent(QShowEvent* event)
{
    QComboBox::showEvent(event);
    if (m_bAttached && m_pRtfCtrl) {
        UpdateRtfGeometry();
        m_pRtfCtrl->show();
        m_pRtfCtrl->raise();
    }
}

void CRtfCmb::hideEvent(QHideEvent* event)
{
    if (m_pRtfCtrl) m_pRtfCtrl->hide();
    QComboBox::hideEvent(event);
}

void CRtfCmb::moveEvent(QMoveEvent* event)
{
    QComboBox::moveEvent(event);
    UpdateRtfGeometry();
}

void CRtfCmb::resizeEvent(QResizeEvent* event)
{
    QComboBox::resizeEvent(event);
    UpdateRtfGeometry();
}

void CRtfCmb::changeEvent(QEvent* event)
{
    QComboBox::changeEvent(event);
    if (!m_pRtfCtrl) return;
    if (event->type() == QEvent::EnabledChange)
        m_pRtfCtrl->setEnabled(isEnabled());
    else if (event->type() == QEvent::FontChange)
        m_pRtfCtrl->setFont(font());
    else if (event->type() == QEvent::PaletteChange)
        m_pRtfCtrl->setPalette(palette());
}
