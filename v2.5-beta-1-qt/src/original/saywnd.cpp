// Ported from v2.5-beta-1-modern/saywnd.cpp.

#include "saywnd.h"

#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "memblst.h"
#include "pageview.h"
#include "protsupp.h"
#include "originalassets.h"
#include "whisprbx.h"

#include <QApplication>
#include <QBitmap>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QFontInfo>
#include <QTextBlockFormat>
#include <QTextCursor>

#include <algorithm>

namespace {
constexpr int cxToolBar = 17;
constexpr int cyToolBar = 17;
constexpr int BUTTONSIZE = 24;

const char* const balloons_say[SAYNBUTTONS] = {
    "ID_ACTIONS_SAY",
    "ID_ACTIONS_THINK",
    "ID_ACTIONS_WHISPER",
    "ID_SEND_ACTION",
    "ID_WHISPER_ACTION",
    "ID_PLAY_SOUND",
    "ID_WHISPER_SOUND"
};

CAccelTable accelWhisper(QStringLiteral("IDR_WHISPERACCEL"));

QString commandToolTip(const QString& identifier)
{
    return originalResourceString(identifier).section(QLatin1Char('\n'), 1, 1);
}
}

unsigned long CSayWnd::m_dwDefaultButtons = 0;

CSayCtrl::CSayCtrl(CSayWnd* parent)
    : CRtfCtrl(parent)
    , m_sayWnd(parent)
{
    setObjectName(QStringLiteral("ID_SAYCTRL"));
    setAcceptRichText(true);
    setLineWrapMode(QTextEdit::WidgetWidth);
    connect(this, &QTextEdit::textChanged, this, [this] { enforceMaximumLength(); });
}

void CSayCtrl::ValidateIMEEntry()
{
    if (QGuiApplication::inputMethod()) QGuiApplication::inputMethod()->commit();
}

bool CSayCtrl::IsEmpty() const
{
    QString text = toPlainText();
    while (!text.isEmpty() && text.back().isSpace()) text.chop(1);
    return text.isEmpty();
}

bool CSayCtrl::bEmptyAndIndent()
{
    const bool previousFreeze = m_bEnChangeFreeze;
    m_bEnChangeFreeze = true;
    clear();
    QTextCursor cursor = textCursor();
    QTextBlockFormat format = cursor.blockFormat();
    const qreal dpi = logicalDpiX() > 0 ? logicalDpiX() : 96.0;
    format.setLeftMargin(72.0 * dpi / 1440.0);
    cursor.setBlockFormat(format);
    setTextCursor(cursor);
    document()->clearUndoRedoStacks();
    m_bEnChangeFreeze = previousFreeze;
    return true;
}

void CSayCtrl::enforceMaximumLength()
{
    if (m_bTruncating || toPlainText().size() <= MAX_INPUTLEN) return;
    m_bTruncating = true;
    QTextCursor cursor(document());
    cursor.setPosition(MAX_INPUTLEN);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    m_bTruncating = false;
    QMessageBox::information(this,
        originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
        originalResourceString(QStringLiteral("IDS_TOOMUCHTEXT")));
}

void CSayCtrl::insertFromMimeData(const QMimeData* source)
{
    CRtfCtrl::insertFromMimeData(source);
    QString text = toPlainText();
    while (text.endsWith(QLatin1Char('\n'))) text.chop(1);
    if (text != toPlainText()) {
        QTextCursor cursor(document());
        cursor.select(QTextCursor::Document);
        cursor.insertText(text);
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
    }
}

void CSayCtrl::keyPressEvent(QKeyEvent* event)
{
    if (m_bWhisperSay) {
        const QString command = accelWhisper.Lookup(event);
        if (!command.isEmpty() && m_sayWnd) {
            if (command == QLatin1String("ID_ACTIONS_WHISPER"))
                m_sayWnd->OnActionsWhisper();
            else if (command == QLatin1String("ID_WHISPER_ACTION"))
                m_sayWnd->OnSendAction();
            else if (command == QLatin1String("ID_WHISPER_SOUND"))
                m_sayWnd->OnPlaySound();
            return;
        }
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_sayWnd) m_sayWnd->SendSayFromReturn();
        return;
    }
    if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
        if (m_bWhisperSay) {
            if (auto* box = dynamic_cast<CWhisperBox*>(m_sayWnd->window()))
                box->SendScrollKey(event);
        } else {
            CChatDoc* document = GetChatDoc();
            QWidget* output = document
                ? document->GetComponentWindow(CHATFOCUS_OUTPUTWND)
                : nullptr;
            if (output) QApplication::sendEvent(output, event);
        }
        return;
    }
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        const bool backward = event->key() == Qt::Key_Backtab
            || (event->modifiers() & Qt::ShiftModifier);
        if (m_bWhisperSay) {
            if (auto* box = dynamic_cast<CWhisperBox*>(m_sayWnd->window()))
                box->CycleFocus(backward);
        } else if (CChatDoc* document = GetChatDoc()) {
            document->CycleFocus(CHATFOCUS_INPUTWND, backward);
        }
        return;
    }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_L) {
        ValidateIMEEntry();
        bEmptyAndIndent();
        return;
    }
    if (!(event->modifiers() & Qt::ControlModifier)
        && toPlainText().isEmpty() && !event->text().isEmpty()
        && event->text().front().isSpace()) {
        return;
    }
    CRtfCtrl::keyPressEvent(event);
}

CSayToolBar::CSayToolBar(QWidget* parent)
    : QToolBar(parent)
{
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(cxToolBar, cyToolBar));
    setToolButtonStyle(Qt::ToolButtonIconOnly);
}

CSayWnd::CSayWnd(QWidget* parent)
    : QWidget(parent)
{
    SetToolBarInfo(false, m_dwDefaultButtons);
    initialize();
}

CSayWnd::CSayWnd(bool whisperSay, unsigned long buttons, QWidget* parent)
    : QWidget(parent)
{
    SetToolBarInfo(whisperSay, buttons);
    initialize();
}

void CSayWnd::SetToolBarInfo(bool whisperSay, unsigned long buttons)
{
    m_bWhisperSay = whisperSay;
    m_dwButtons = buttons;
    m_cntBalloons = 0;
    for (int index = 0; index < SAYNBUTTONS; ++index)
        if (buttons & (1UL << index)) ++m_cntBalloons;
    m_cxSayBar = m_cntBalloons * BUTTONSIZE;
}

void CSayWnd::initialize()
{
    setAutoFillBackground(true);
    m_wndSayCtrl = new CSayCtrl(this);
    m_wndSayCtrl->m_bWhisperSay = m_bWhisperSay;
    m_wndSayCtrl->setFrameShape(m_bWhisperSay ? QFrame::StyledPanel : QFrame::NoFrame);
    if (theApp.m_bComicView)
        SetFont(theApp.m_comicsFont);
    else
        SetFont(theApp.m_textFont);
    m_wndSayCtrl->SetDosKey(m_bWhisperSay
                                ? &theApp.m_doskeyWhisper
                                : &theApp.m_doskeyMain);
    m_wndSayCtrl->setTextColor(Qt::black);
    m_wndSayCtrl->m_crTextColor = RGB(0, 0, 0);
    m_wndSayCtrl->bEmptyAndIndent();
    setFocusProxy(m_wndSayCtrl);
    createSayBar();
    m_wndSayCtrl->setFocus();
}

BOOL CSayWnd::SetFont(QFont& logFont, BOOL matchButtonsToSelection)
{
    if (!m_wndSayCtrl) return FALSE;

    QFont selected = logFont;
    if (FFixedPitchFont(selected.family()) >= 0
        || FSymbolFont(selected.family()) >= 0 || selected.fixedPitch()) {
        selected.setFamily(QApplication::font().family());
    }
    selected.setFixedPitch(false);
    selected.setWeight(QFont::Normal);
    selected.setItalic(false);
    selected.setUnderline(false);
    selected.setStrikeOut(false);
    selected.setPixelSize(qAbs(nFontHeight));

    const QString physicalFace = QFontInfo(selected).family();
    if (!physicalFace.isEmpty()) selected.setFamily(physicalFace);
    logFont.setFamily(selected.family());

    m_fontText = selected;
    m_wndSayCtrl->setFont(m_fontText);
    m_wndSayCtrl->m_pFont = &m_fontText;
    m_wndSayCtrl->UseDefaultCharFormat(TRUE);
    if (matchButtonsToSelection)
        m_wndSayCtrl->MatchButtonsToSelection();
    return TRUE;
}

void CSayWnd::createSayBar()
{
    if (!m_cntBalloons) return;
    m_wndSayBar = new CSayToolBar(this);
    const QPixmap strip(originalFileResourcePath(QStringLiteral("IDB_SAY_BAR"),
                                                 QStringLiteral("BITMAP")));
    for (int index = 0; index < SAYNBUTTONS; ++index) {
        if (!(m_dwButtons & (1UL << index))) continue;
        QPixmap glyph = strip.copy(index * cxToolBar, 0, cxToolBar, cyToolBar);
        if (!glyph.isNull())
            glyph.setMask(glyph.createMaskFromColor(QColor(192, 192, 192), Qt::MaskInColor));
        const QString identifier = QString::fromLatin1(balloons_say[index]);
        QAction* action = m_wndSayBar->addAction(QIcon(glyph), commandToolTip(identifier));
        action->setData(identifier);
        action->setToolTip(commandToolTip(identifier));
        action->setStatusTip(originalResourceString(identifier).section(QLatin1Char('\n'), 0, 0));
        if (index == 0) connect(action, &QAction::triggered, this, &CSayWnd::OnActionsSay);
        else if (index == 1) connect(action, &QAction::triggered, this, &CSayWnd::OnActionsThink);
        else if (index == 2) connect(action, &QAction::triggered, this, &CSayWnd::OnActionsWhisper);
        else if (index == 3 || index == 4)
            connect(action, &QAction::triggered, this, &CSayWnd::OnSendAction);
        else {
            connect(action, &QAction::triggered, this, &CSayWnd::OnPlaySound);
            action->setEnabled(false);
        }
    }
    m_wndSayBar->show();
}

void CSayWnd::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const int controlWidth = std::max(0, width() - static_cast<int>(m_cxSayBar));
    m_wndSayCtrl->setGeometry(0, 0, controlWidth, height());
    if (m_wndSayBar) {
        m_wndSayBar->setGeometry(width() - static_cast<int>(m_cxSayBar) - 6,
                                 m_bWhisperSay ? -2 : -3,
                                 static_cast<int>(m_cxSayBar) + 12,
                                 cyToolBar + 12 + (m_bWhisperSay ? 3 : 0));
    }
}

void CSayWnd::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QColor(192, 192, 192));
}

void CSayWnd::showOriginalMessage(const QString& identifier)
{
    QMessageBox::information(this,
        originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
        originalResourceString(identifier));
}

BOOL bLegalToSend(BOOL privateMessage)
{
    CRoomInfo* room = currentRoom;
    if (!room) {
        CChatDoc* document = GetChatDoc();
        room = document ? document->m_proto : nullptr;
    }
    if (!room) return false;
    const int status = room->GetConnectionStatus();
    if ((status == CX_NOCHANNEL && !privateMessage) || status == CX_CONNECTING) {
        if (room->m_doc && room->m_doc->m_bStatusView)
            QMessageBox::information(
                theApp.m_pMainWnd,
                originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
                originalResourceString(QStringLiteral("IDS_ILLEGAL_NOSLASH")));
        else
            QMessageBox::information(
                theApp.m_pMainWnd,
                originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
                originalResourceString(QStringLiteral("IDS_ILLEGAL_TO_SEND")));
        return false;
    }
    if (privateMessage && (status == CX_DISCONNECTED || status == CX_CONNECTING)) {
        QMessageBox::information(
            theApp.m_pMainWnd,
            originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
            originalResourceString(QStringLiteral("IDS_MUSTBE_CONNECTED")));
        return false;
    }
    return true;
}

void CSayWnd::SwitchSelectionFormat(unsigned short format)
{
    m_wndSayCtrl->SwitchSelectionFormat(format);
}

unsigned short CSayWnd::wGetConsistentFormats() const
{
    return m_wndSayCtrl->wGetConsistentFormats();
}

bool CSayWnd::IsEmpty() const
{
    return m_wndSayCtrl->IsEmpty();
}

bool CSayWnd::TextEntered() const
{
    return !m_wndSayCtrl->toPlainText().isEmpty();
}

void CSayWnd::SendReturn()
{
    SendSayFromReturn();
}

void CSayWnd::SetFocusToSayWnd()
{
    if (m_wndSayCtrl) m_wndSayCtrl->setFocus();
}

void CSayWnd::SendSayFromReturn()
{
    m_wndSayCtrl->ValidateIMEEntry();
    QString text = m_wndSayCtrl->toPlainText();
    const bool empty = text.isEmpty();
    const bool command = !empty && text.front() == QLatin1Char('/');
    if ((empty || !command) && !bLegalToSend(m_bWhisperSay)) return;
    if (empty) {
        CChatDoc* document = GetChatDoc();
        if (!m_bWhisperSay && document && document->m_bComicView
            && GetSendComicsData() && bCanDance()) {
            text = QStringLiteral("<Chr>");
        } else {
            if (m_bWhisperSay) showOriginalMessage(QStringLiteral("IDS_SENDENABLED"));
            return;
        }
    }
    CDWordArray* formatting = m_wndSayCtrl->m_pFont
        ? PRGDWGetFormatting(m_wndSayCtrl, m_wndSayCtrl->m_pFont,
                             m_wndSayCtrl->m_crTextColor)
        : nullptr;
    if (!empty && m_wndSayCtrl->m_pDosKey)
        m_wndSayCtrl->m_pDosKey->bAppendEntry(text, formatting);
    m_wndSayCtrl->bEmptyAndIndent();
    if (m_bWhisperSay)
        bWhisperInBox(QString(), text, formatting, BM_WHISPER);
    else
        bChatSendText(text, BM_SAY, true, formatting);
    FreeAndNullFormatting(&formatting);
}

void CSayWnd::OnActionsSay()
{
    m_wndSayCtrl->ValidateIMEEntry();
    const QString text = m_wndSayCtrl->toPlainText();
    if (text.isEmpty()) {
        showOriginalMessage(QStringLiteral("IDS_SENDENABLED"));
        return;
    }
    if (!bLegalToSend()) return;
    CDWordArray* formatting = PRGDWGetFormatting(
        m_wndSayCtrl, &m_fontText, m_wndSayCtrl->m_crTextColor);
    if (m_wndSayCtrl->m_pDosKey)
        m_wndSayCtrl->m_pDosKey->bAppendEntry(text, formatting);
    m_wndSayCtrl->bEmptyAndIndent();
    bChatSendText(text, BM_SAY, true, formatting);
    FreeAndNullFormatting(&formatting);
}

void CSayWnd::OnActionsThink()
{
    CChatDoc* document = GetChatDoc();
    if (document && document->m_bStatusView) return;
    m_wndSayCtrl->ValidateIMEEntry();
    const QString text = m_wndSayCtrl->toPlainText();
    if (text.isEmpty()) {
        showOriginalMessage(QStringLiteral("IDS_SENDENABLED"));
        return;
    }
    if (!bLegalToSend()) return;
    CDWordArray* formatting = PRGDWGetFormatting(
        m_wndSayCtrl, &m_fontText, m_wndSayCtrl->m_crTextColor);
    if (m_wndSayCtrl->m_pDosKey)
        m_wndSayCtrl->m_pDosKey->bAppendEntry(text, formatting);
    m_wndSayCtrl->bEmptyAndIndent();
    bChatSendText(text, BM_THINK, true, formatting);
    FreeAndNullFormatting(&formatting);
}

void CSayWnd::OnActionsWhisper()
{
    CChatDoc* document = GetChatDoc();
    if (document && document->m_bStatusView) return;
    m_wndSayCtrl->ValidateIMEEntry();
    const QString text = m_wndSayCtrl->toPlainText();
    if (!bLegalToSend(m_bWhisperSay)) return;
    if (m_bWhisperSay) {
        if (text.isEmpty()) {
            showOriginalMessage(QStringLiteral("IDS_SENDENABLED"));
            return;
        }
        CDWordArray* formatting = PRGDWGetFormatting(
            m_wndSayCtrl, &m_fontText, m_wndSayCtrl->m_crTextColor);
        m_wndSayCtrl->bEmptyAndIndent();
        bWhisperInBox(QString(), text, formatting, BM_WHISPER);
        if (m_wndSayCtrl->m_pDosKey)
            m_wndSayCtrl->m_pDosKey->bAppendEntry(text, formatting);
        FreeAndNullFormatting(&formatting);
        return;
    }
    if (document && document->GetConnectionStatus() == CX_DISCONNECTED) {
        showOriginalMessage(QStringLiteral("IDS_MUSTBE_CONNECTED"));
        return;
    }
    GetSelectedPuis(g_rgpuiWhisperees);
    if (text.isEmpty() || g_rgpuiWhisperees.isEmpty()) {
        showOriginalMessage(QStringLiteral("IDS_WHISPERENABLED"));
        return;
    }
    CDWordArray* formatting = PRGDWGetFormatting(
        m_wndSayCtrl, &m_fontText, m_wndSayCtrl->m_crTextColor);
    m_wndSayCtrl->bEmptyAndIndent();
    bChatSendText(text, BM_WHISPER, true, formatting);
    if (m_wndSayCtrl->m_pDosKey)
        m_wndSayCtrl->m_pDosKey->bAppendEntry(text, formatting);
    FreeAndNullFormatting(&formatting);
}

void CSayWnd::OnSendAction()
{
    CChatDoc* document = GetChatDoc();
    if (document && document->m_bStatusView) return;
    m_wndSayCtrl->ValidateIMEEntry();
    const QString text = m_wndSayCtrl->toPlainText();
    if (text.isEmpty()) {
        showOriginalMessage(QStringLiteral("IDS_SENDENABLED"));
        return;
    }
    if (!bLegalToSend(m_bWhisperSay)) return;
    CDWordArray* formatting = PRGDWGetFormatting(
        m_wndSayCtrl, &m_fontText, m_wndSayCtrl->m_crTextColor);
    if (m_wndSayCtrl->m_pDosKey)
        m_wndSayCtrl->m_pDosKey->bAppendEntry(text, formatting);
    m_wndSayCtrl->bEmptyAndIndent();
    if (m_bWhisperSay)
        bWhisperInBox(QString(), text, formatting, BM_WHISPER | BM_ACTION);
    else
        bChatSendText(text, BM_ACTION, true, formatting);
    FreeAndNullFormatting(&formatting);
}

void CSayWnd::OnPlaySound()
{
    // `CSoundDlg` and `bChatSendSound` remain unported. The corresponding
    // buttons stay disabled; no replacement sound workflow is invented.
}

CSayWnd* GetSay()
{
    CChatDoc* document = GetChatDoc();
    return document ? dynamic_cast<CSayWnd*>(document->m_sayWnd) : nullptr;
}
