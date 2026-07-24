// Ported from v2.5-beta-1-modern/textview.cpp. Qt supplies the read-only text
// control, menu and key events; message classification and TextCore calls keep
// the original order and resource strings.

#include "textview.h"

#include "chat.h"
#include "chatdoc.h"
#include "chatview.h"
#include "ircproto.h"
#include "intl.h"
#include "mainfrm.h"
#include "saywnd.h"
#include "protsupp.h"
#include "txtfntdg.h"
#include "userinfo.h"
#include "originalassets.h"

#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtPrintSupport/QPrinter>

#include <algorithm>
#include <cstring>

namespace {
constexpr int TIMEDATESEP_LENGTH = 3;
constexpr int TEXT_FOOTER_FONT_HEIGHT = 45;

void SetBoldFont(CTextCore* textCore, MSG_TYPE type, BOOL header)
{
    QTextCharFormat* format = nullptr;
    QTextCharFormat local;
    if (textCore->bGetMessageFormat(&format, type, msHost, header) && format) {
        local = *format;
    } else {
        if (!textCore->bGetDefaultMessageFormat(&format, type, header) || !format)
            return;
        local = *format;
    }
    local.setFontWeight(QFont::Bold);
    textCore->bSetMessageFormat(&local, type, msHost, header);
}

void AppendResourceMenu(QMenu& menu, const QList<OriginalMenuItem>& items)
{
    for (const OriginalMenuItem& item : items) {
        if (item.type == OriginalMenuItemType::Separator) {
            menu.addSeparator();
        } else if (item.type == OriginalMenuItemType::Popup) {
            QMenu* popup = menu.addMenu(item.text);
            AppendResourceMenu(*popup, item.children);
        } else {
            QAction* action = menu.addAction(item.text);
            action->setData(item.commandIdentifier);
            if (item.flags.contains(QStringLiteral("GRAYED"))
                || item.flags.contains(QStringLiteral("INACTIVE"))) {
                action->setEnabled(false);
            }
        }
    }
}

QByteArray utf8(const QString& text)
{
    return text.toUtf8();
}

QRectF textPrintRect(QPrinter* printer)
{
    if (!printer) return {};
    const QRectF page = printer->pageRect(QPrinter::DevicePixel);
    const int horizontalResolution = static_cast<int>(page.width());
    const int verticalResolution = static_cast<int>(page.height());
    const int dpiX = std::max(1, printer->logicalDpiX());
    const int dpiY = std::max(1, printer->logicalDpiY());

    // Preserve the integer division in CTextView::lPrintPage before applying
    // the original TWIPS margins.
    const int pageRight = (horizontalResolution / dpiX) * 1440;
    const int pageBottom = (verticalResolution / dpiY) * 1440;
    const int left = 1440 / 2;
    const int top = 1440 / 2;
    const int right = pageRight - 1440 / 2;
    const int bottom = pageBottom - 1440 / 4;
    if (right <= left || bottom <= top) return {};
    return QRectF(page.left() + left * dpiX / 1440.0,
                  page.top() + top * dpiY / 1440.0,
                  (right - left) * dpiX / 1440.0,
                  (bottom - top) * dpiY / 1440.0);
}

QString localPrintTimeDate()
{
    const QLocale locale;
    const QDateTime now = QDateTime::currentDateTime();
    return locale.toString(now.time(), QLocale::LongFormat)
        + QString(TIMEDATESEP_LENGTH, QLatin1Char(' '))
        + locale.toString(now.date(), QLocale::ShortFormat);
}

CHARFORMAT defaultRichEditFormat(const QTextCharFormat& source)
{
    CHARFORMAT result{};
    result.cbSize = sizeof(CHARFORMAT);
    result.dwMask = CFM_FACE | CFM_SIZE | CFM_OFFSET | CFM_COLOR
        | CFM_BOLD | CFM_ITALIC | CFM_STRIKEOUT | CFM_UNDERLINE
        | CFM_CHARSET | CFM_LINK;
    const QFont font = source.font();
    if (font.weight() >= QFont::Bold) result.dwEffects |= CFE_BOLD;
    if (font.italic()) result.dwEffects |= CFE_ITALIC;
    if (font.underline()) result.dwEffects |= CFE_UNDERLINE;
    if (font.strikeOut()) result.dwEffects |= CFE_STRIKEOUT;
    result.yHeight = 10 * 20;
    result.yOffset = 0;
    const QColor color = source.foreground().color();
    result.crTextColor = color.isValid()
        ? RGB(static_cast<BYTE>(color.red()),
              static_cast<BYTE>(color.green()),
              static_cast<BYTE>(color.blue()))
        : RGB(0, 0, 0);
    result.bCharSet = theApp.m_charSet;
    result.bPitchAndFamily = 0;
    const QByteArray face = font.family().toLatin1().left(LF_FACESIZE - 1);
    std::memcpy(result.szFaceName, face.constData(),
                static_cast<size_t>(face.size()));
    return result;
}
}

void SetSpecificFont(CTextCore* textCore, CHARFORMAT& format,
                     MSG_TYPE type, BOOL header)
{
    if (type < mtGetInfo) {
        for (int member = 0; member < msEndEnum; ++member) {
            textCore->bSetMessageFormat(
                &format, type, static_cast<MEMBER_STATUS>(member), header);
        }
    } else {
        for (int info = mtBeginInfo; info < mtEndEnum; ++info) {
            textCore->bSetMessageFormat(
                &format, static_cast<MSG_TYPE>(info),
                static_cast<MEMBER_STATUS>(0), header);
        }
    }
}

void ResetSayFont(CHARFORMAT* format)
{
    if (!format) return;
    LOGFONT logFont{};
    if (!bCHARFORMATToLOGFONT(format, format->dwMask, &logFont)) return;
    QFont font;
    if (logFont.lfFaceName[0] != '\0')
        font.setFamily(QString::fromLatin1(logFont.lfFaceName));
    else
        font.setStyleHint(QFont::SansSerif);
    if (logFont.lfHeight != 0) font.setPixelSize(qAbs(logFont.lfHeight));
    font.setWeight(logFont.lfWeight >= 700 ? QFont::Bold : QFont::Normal);
    font.setItalic(logFont.lfItalic != 0);
    font.setUnderline(logFont.lfUnderline != 0);
    font.setStrikeOut(logFont.lfStrikeOut != 0);
    theApp.m_textFont = font;
    theApp.m_charSet = format->bCharSet;
    CSayWnd* sayWindow = GetSay();
    if (sayWindow)
        sayWindow->SetFont(theApp.m_textFont, TRUE);
}

void InitializeTextCore(CTextCore* textCore, BOOL resetOld, BOOL resetSay)
{
    if (!textCore) return;
    if (resetOld) {
        textCore->bReSetDefaultMsgTypeProperties(TRUE);
        textCore->bReSetDefaultHighlightFormats(TRUE);
    }

    textCore->SetHeaderSeparate(theApp.m_flags1 & F1_HEADERSEPARATE);
    textCore->bSetInsertBlank(theApp.m_textSpacing);
    textCore->SetURLBrowser(theApp.m_bEmbedded);
    textCore->SetDBCSSystem(GetMime() != nullptr);

    if (!theApp.m_bCfInitialized) {
        QTextCharFormat* defaultFormat = nullptr;
        if (textCore->bGetTextViewDefaultFormat(&defaultFormat)
            && defaultFormat) {
            QTextCharFormat tenPoint = *defaultFormat;
            tenPoint.setFontPointSize(10.0);
            textCore->bSetTextViewDefaultFormat(&tenPoint);
            if (resetSay) {
                CHARFORMAT sayFormat = defaultRichEditFormat(tenPoint);
                ResetSayFont(&sayFormat);
            }
        }
    } else {
        SetSpecificFont(textCore, theApp.m_cfArray[0], mtJoin, FALSE);
        SetSpecificFont(textCore, theApp.m_cfArray[1], mtNormal, TRUE);
        SetSpecificFont(textCore, theApp.m_cfArray[2], mtNormal, FALSE);
        SetSpecificFont(textCore, theApp.m_cfArray[3], mtWhisper, TRUE);
        SetSpecificFont(textCore, theApp.m_cfArray[4], mtWhisper, FALSE);
        SetSpecificFont(textCore, theApp.m_cfArray[5], mtThought, TRUE);
        SetSpecificFont(textCore, theApp.m_cfArray[6], mtThought, FALSE);
        SetSpecificFont(textCore, theApp.m_cfArray[7], mtAction, FALSE);
        SetSpecificFont(textCore, theApp.m_cfArray[8], mtGetInfo, FALSE);
        SetSpecificFont(textCore, theApp.m_cfArray[9], mtLeave, FALSE);
        if (resetSay) ResetSayFont(&theApp.m_cfArray[2]);
    }

    if (theApp.m_bCfHLInitialized) {
        for (SHORT index = 0; index < g_nHighlightedFormats; ++index) {
            textCore->bSetHighlightFormat(
                &theApp.m_cfArray[NREGULARFONTS + index], index);
        }
    }

    for (int type = 0; type < mtBeginInfo; ++type) {
        if (theApp.m_iHostHighlight & HH_BOLD_MESSAGES)
            SetBoldFont(textCore, static_cast<MSG_TYPE>(type), FALSE);
        if (type < mtBeginActions
            && (theApp.m_iHostHighlight & HH_BOLD_HEADERS)) {
            SetBoldFont(textCore, static_cast<MSG_TYPE>(type), TRUE);
        }
    }
}

CTextEdit::CTextEdit(CTextView* owner)
    : QTextEdit(owner)
    , m_owner(owner)
{
    setObjectName(QStringLiteral("IDC_RICHEDIT"));
    setReadOnly(true);
    setAcceptRichText(true);
    setUndoRedoEnabled(false);
    setLineWrapMode(QTextEdit::WidgetWidth);
    viewport()->setMouseTracking(true);
}

void CTextEdit::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_owner) m_owner->ShowContextMenu(event->globalPos());
    event->accept();
}

void CTextEdit::focusOutEvent(QFocusEvent* event)
{
    QTextEdit::focusOutEvent(event);
}

void CTextEdit::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        if (CChatDoc* document = m_owner ? m_owner->GetDocument() : nullptr) {
            document->CycleFocus(CHATFOCUS_TEXTVIEW,
                                 event->key() == Qt::Key_Backtab
                                     || (event->modifiers() & Qt::ShiftModifier));
        }
        event->accept();
        return;
    }

    const Qt::KeyboardModifiers commandModifiers =
        Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    if (!event->text().isEmpty()
        && !(event->modifiers() & commandModifiers)) {
        CChatDoc* document = m_owner ? m_owner->GetDocument() : nullptr;
        if (document && document->m_sayWnd) {
            document->SetFocusToSayWnd();
            if (QWidget* focus = QApplication::focusWidget(); focus && focus != this)
                QApplication::sendEvent(focus, event);
            event->accept();
            return;
        }
    }
    QTextEdit::keyPressEvent(event);
}

void CTextEdit::mouseMoveEvent(QMouseEvent* event)
{
    viewport()->setCursor(anchorAt(event->position().toPoint()).isEmpty()
                              ? Qt::IBeamCursor
                              : Qt::PointingHandCursor);
    QTextEdit::mouseMoveEvent(event);
}

void CTextEdit::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton
        && !(event->modifiers() & Qt::ControlModifier)) {
        const QString link = anchorAt(event->position().toPoint());
        if (!link.isEmpty() && m_owner
            && m_owner->m_textCore.bHandleLink(link)) {
            event->accept();
            return;
        }
    }
    QTextEdit::mousePressEvent(event);
}

CTextView::CTextView(CChatDoc* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
    , m_pRichEdit(new CTextEdit(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_pRichEdit);

    m_textCore.AttachTextViewHWnd(m_pRichEdit);
    m_textCore.bSetTextViewBufferMaxSize(256000);
    InitializeTextCore(&m_textCore, FALSE, TRUE);
}

CTextView::~CTextView()
{
    m_textCore.DetachTextViewHWnd();
    delete m_pFooterFont;
    delete m_fontText;
    delete m_printDocument;
    if (m_document && m_document->m_textView == this)
        m_document->m_textView = nullptr;
}

BOOL CTextView::OnPreparePrinting(QPrinter* printer) const
{
    return printer && m_pRichEdit && m_pRichEdit->document();
}

void CTextView::PreparePrintDocument(QPrinter* printer)
{
    delete m_printDocument;
    m_printDocument = nullptr;
    m_printTextRect = textPrintRect(printer);
    m_printPageCount = 0;
    if (!m_pRichEdit || !m_pRichEdit->document()
        || m_printTextRect.isEmpty()) {
        return;
    }

    m_printDocument = m_pRichEdit->document()->clone();
    m_printDocument->setDocumentMargin(0.0);
    m_printDocument->setPageSize(m_printTextRect.size());
    m_printDocument->documentLayout()->documentSize();
    m_printPageCount = std::max(1, m_printDocument->pageCount());
}

void CTextView::OnBeginPrinting(QPrinter* printer)
{
    delete m_pFooterFont;
    m_pFooterFont = new QFont(theApp.m_textFont);
    m_pFooterFont->setPixelSize(TEXT_FOOTER_FONT_HEIGHT);
    m_pFooterFont->setWeight(QFont::Normal);
    PreparePrintDocument(printer);
}

long CTextView::lPrintPage(QPrinter* printer, QPainter* painter,
                           UINT currentPage, BOOL display)
{
    const QRectF requestedRect = textPrintRect(printer);
    if (!m_printDocument || requestedRect != m_printTextRect)
        PreparePrintDocument(printer);
    if (!m_printDocument) return 0;
    if (!display) return std::max(1, m_printPageCount);
    if (!painter || !painter->isActive() || currentPage == 0
        || currentPage > static_cast<UINT>(m_printPageCount)) {
        return 0;
    }

    const qreal pageHeight = m_printTextRect.height();
    const qreal documentTop = (currentPage - 1) * pageHeight;
    QAbstractTextDocumentLayout::PaintContext context;
    context.clip = QRectF(0.0, documentTop,
                          m_printTextRect.width(), pageHeight);

    painter->save();
    painter->setClipRect(m_printTextRect, Qt::IntersectClip);
    painter->translate(m_printTextRect.left(),
                       m_printTextRect.top() - documentTop);
    m_printDocument->documentLayout()->draw(painter, context);
    painter->restore();

    const int hit = m_printDocument->documentLayout()->hitTest(
        QPointF(m_printTextRect.width() - 1.0,
                documentTop + pageHeight - 1.0),
        Qt::FuzzyHit);
    return std::clamp(hit + 1, 0,
                      std::max(0, m_printDocument->characterCount() - 1));
}

void CTextView::PrintFooter(QPainter* painter, const QRectF& pageRect,
                            UINT pageNumber) const
{
    if (!painter || !painter->isActive() || !m_pFooterFont) return;
    const qreal left = pageRect.left() + 50.0;
    const qreal right = pageRect.right() - 50.0;
    const qreal bottom = pageRect.bottom() - 50.0;
    QString page = originalResourceString(QStringLiteral("IDS_PAGEFOOTER"));
    page.replace(QStringLiteral("%1"), QString::number(pageNumber));
    const QString title = originalResourceString(
        QStringLiteral("AFX_IDS_APP_TITLE"));
    const QString dateTime = localPrintTimeDate();

    painter->save();
    painter->setClipping(false);
    painter->setPen(Qt::black);
    painter->setFont(*m_pFooterFont);
    const QFontMetricsF metrics(*m_pFooterFont, painter->device());
    const qreal baseline = bottom - metrics.descent();
    painter->drawText(QPointF(left, baseline), title);
    painter->drawText(QPointF((left + right
        - metrics.horizontalAdvance(page)) / 2.0, baseline), page);
    painter->drawText(QPointF(right
        - metrics.horizontalAdvance(dateTime), baseline), dateTime);
    painter->restore();
}

void CTextView::OnPrint(QPrinter* printer, QPainter* painter,
                        UINT pageNumber)
{
    if (!printer || !painter) return;
    lPrintPage(printer, painter, pageNumber, TRUE);
    PrintFooter(painter, printer->pageRect(QPrinter::DevicePixel),
                pageNumber);
}

void CTextView::OnEndPrinting(QPrinter*)
{
    delete m_pFooterFont;
    m_pFooterFont = nullptr;
    delete m_printDocument;
    m_printDocument = nullptr;
    m_printTextRect = {};
    m_printPageCount = 0;
}

void CTextView::TextLine(CUserInfo* puiSender, const char* senderNickname,
                         const char* receiver, const char* line, USHORT modes,
                         BYTE cooked, CDWordArray* formatting,
                         char highlightType)
{
    Q_UNUSED(cooked);
    if (!line) return;
    if ((modes & BM_SAY) && std::strcmp(line, "<Chr>") == 0) return;

    QByteArray fromStorage = senderNickname ? QByteArray(senderNickname) : QByteArray();
    QByteArray toStorage;
    const char* from = fromStorage.constData();
    MEMBER_STATUS memberStatus = msParticipant;
    MSG_TYPE messageType = mtEndEnum;

    if (puiSender) {
        fromStorage = utf8(puiSender->GetScreenName());
        from = fromStorage.constData();
        memberStatus = puiSender->IsOperator() ? msHost : msParticipant;
    }
    if (modes == BM_SAY) {
        messageType = mtNormal;
    } else if (modes == BM_THINK) {
        messageType = mtThought;
    } else if (modes & BM_WHISPER) {
        messageType = mtWhisper;
        toStorage = utf8(GetAddressees(puiSender, QStringLiteral(", "), false));
    } else if (modes & BM_EXCHAN) {
        if (!receiver) return;
        messageType = mtExChan;
        toStorage = receiver;
    }

    const int highlight = static_cast<signed char>(highlightType);
    DWORD* ranges = formatting ? formatting->GetData() : nullptr;
    const int rangeCount = formatting ? formatting->GetSize() : 0;
    if (messageType != mtEndEnum) {
        const int length = static_cast<int>(std::strlen(line));
        m_textCore.iDisplayMsgHeader(length, from, 0, toStorage.constData(), 0,
                                    messageType, memberStatus, nullptr,
                                    2 * highlight);
        if (modes & BM_ACTION) {
            m_textCore.iDisplayAction(nullptr, 0, line, 0, memberStatus,
                                     TRUE, DEFAULT_INDENT, nullptr,
                                     2 * highlight + 1, ranges, rangeCount);
        } else {
            m_textCore.iDisplayMsgText(line, length, messageType, memberStatus,
                                      TRUE, FALSE, FALSE, DEFAULT_INDENT,
                                      nullptr, 2 * highlight + 1,
                                      ranges, rangeCount);
        }
    } else if (modes & BM_ACTION) {
        m_textCore.iDisplayAction(nullptr, 0, line, 0, memberStatus,
                                 TRUE, 0, nullptr, 2 * highlight + 1,
                                 ranges, rangeCount);
    } else if (modes & BM_NOFORMAT) {
        m_textCore.iDisplayMsgText(line, 0, mtNormal, msParticipant,
                                  TRUE, FALSE, FALSE, DEFAULT_INDENT,
                                  nullptr, 2 * highlight + 1,
                                  ranges, rangeCount);
    }
}

void CTextView::DisplayPart(const char* nick, char highlightType)
{
    if (!nick) return;
    CUserInfo* pui = LookupPui(QString::fromUtf8(nick));
    const MEMBER_STATUS memberStatus = pui && pui->IsOperator()
        ? msHost : msParticipant;
    const QByteArray qualified = pui ? utf8(pui->GetQualifiedName()) : QByteArray(nick);
    const int highlight = static_cast<signed char>(highlightType);
    m_textCore.iDisplayMemberStatus(qualified.constData(), 0, mtLeave,
                                   memberStatus, nullptr, 2 * highlight + 1);
}

void CTextView::DisplayJoin(const char* nick, char highlightType)
{
    if (!nick) return;
    CUserInfo* pui = LookupPui(QString::fromUtf8(nick));
    const MEMBER_STATUS memberStatus = pui && pui->IsOperator()
        ? msHost : msParticipant;
    const QByteArray qualified = pui ? utf8(pui->GetQualifiedName()) : QByteArray(nick);
    const int highlight = static_cast<signed char>(highlightType);
    m_textCore.iDisplayMemberStatus(qualified.constData(), 0, mtJoin,
                                   memberStatus, nullptr, 2 * highlight + 1);
}

void CTextView::DisplayNickChange(CUserInfo* pui, const char* oldNick)
{
    if (!pui || !oldNick) return;
    const MEMBER_STATUS memberStatus = pui->IsOperator() ? msHost : msParticipant;
    const QByteArray oldName = utf8(DecodeNickForScreen(
        QString::fromUtf8(oldNick)));
    const QByteArray newName = utf8(pui->GetScreenName());
    m_textCore.iDisplayInfo(oldName.constData(), 0, newName.constData(), 0,
                           nullptr, 0, mtAliasChange, memberStatus);
}

void CTextView::ShowInfo(CUserInfo* pui, const char* info)
{
    if (!info) return;
    const MEMBER_STATUS memberStatus = pui && pui->IsOperator()
        ? msHost : msParticipant;
    QByteArray controlFull(info);
    controlFull.append('\0');
    auto* formatting = new CDWordArray;
    char* controlLess = SzControlLess(controlFull.data(), formatting);
    m_textCore.iDisplayInfo(nullptr, 0, "", 0, controlLess, 0,
                           mtGetInfo, memberStatus, nullptr, -1,
                           formatting->GetData(), formatting->GetSize());
    FreeAndNullFormatting(&formatting);
}

void CTextView::ClearTextView()
{
    m_textCore.dwClearTextViewBuffer(0);
}

void CTextView::SetURLBrowser(BOOL newBrowser)
{
    m_textCore.SetURLBrowser(newBrowser);
}

int CTextView::LoadContextMenu(QMenu& menu)
{
    const QList<OriginalMenuItem> resource =
        originalMenuResource(QStringLiteral("IDR_VIEWCONTEXT"));
    if (resource.size() == 1
        && resource.first().type == OriginalMenuItemType::Popup) {
        AppendResourceMenu(menu, resource.first().children);
    } else {
        AppendResourceMenu(menu, resource);
    }
    return 0;
}

void CTextView::ShowContextMenu(const QPoint& globalPosition)
{
    QMenu menu(this);
    LoadContextMenu(menu);
    QActionGroup viewGroup(&menu);
    viewGroup.setExclusive(true);
    for (QAction* action : menu.actions()) {
        const QString command = action->data().toString();
        if (command == QLatin1String("ID_EDIT_COPY"))
            action->setEnabled(m_pRichEdit->textCursor().hasSelection());
        else if (command == QLatin1String("ID_VIEW_TEXT")) {
            viewGroup.addAction(action);
            action->setCheckable(true);
            action->setChecked(true);
        } else if (command == QLatin1String("ID_VIEW_COMICS")) {
            viewGroup.addAction(action);
            action->setCheckable(true);
            action->setChecked(false);
            action->setEnabled(m_document
                && (!m_document->m_proto
                    || !(m_document->m_proto->m_dwModes
                         & CM_NOFORMAT)));
        } else if (command == QLatin1String("ID_CHANNELPROPS")) {
            action->setEnabled(m_document
                && m_document->GetConnectionStatus() == CX_INCHANNEL
                && g_puiSelf && m_document->m_puiSelf
                && !m_document->m_allChannelPuis.isEmpty());
        }
    }
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->ConfigureContextMenu(&menu);
    if (QAction* selected = menu.exec(globalPosition))
        ExecuteContextCommand(selected->data().toString());
}

void CTextView::ExecuteContextCommand(const QString& commandIdentifier)
{
    if (commandIdentifier == QLatin1String("ID_EDIT_COPY")) {
        m_pRichEdit->copy();
    } else if (commandIdentifier == QLatin1String("ID_CLEAR_HISTORY")) {
        if (m_document) m_document->OnClearHistory();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_COMICS")) {
        if (m_document) m_document->OnViewComics();
    } else if (commandIdentifier == QLatin1String("ID_CHANNELPROPS")) {
        if (m_document && m_document->GetConnectionStatus() == CX_INCHANNEL
            && g_puiSelf && m_document->m_puiSelf
            && !m_document->m_allChannelPuis.isEmpty()) {
            m_document->OnChannelprops();
        }
    }
}

CTextView* GetTextView()
{
    CChatDoc* document = GetChatDoc();
    return document ? document->m_textView : nullptr;
}

void InitializeTextCores(BOOL resetOld, BOOL resetSay)
{
    for (CChatDoc* document : g_docs) {
        if (document && !document->m_bComicView
            && !document->m_bStatusView && document->m_textView) {
            InitializeTextCore(&document->m_textView->m_textCore,
                               resetOld, resetSay);
        }
    }
}
