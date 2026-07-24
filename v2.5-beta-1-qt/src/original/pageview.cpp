// Ported from v2.5-beta-1-modern/pageview.cpp. Panels are rendered through
// their original Draw methods into the retained-panel equivalent QImage.

#include "pageview.h"

#include "avatar.h"
#include "backdrop.h"
#include "bbox.h"
#include "chat.h"
#include "chatdoc.h"
#include "defines.h"
#include "format.h"
#include "histent.h"
#include "intl.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "memblst.h"
#include "originalassets.h"
#include "paintdc.h"
#include "panel.h"
#include "userinfo.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QDateTime>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QTextEdit>
#include <QLocale>
#include <QtPrintSupport/QPrinter>

#include <algorithm>
#include <cmath>

namespace {
constexpr int SCROLLWIDTH = 16;
constexpr int COMFORTABLEPANELWIDTH = 3000;
constexpr int FOOTERMARGIN = 1000;
constexpr int FOOTERFONTHEIGHT = 250;
constexpr int FOOTERHEIGHT = FOOTERFONTHEIGHT * 3 / 2;
constexpr auto TIMEDATESEP = "   ";

qreal printerDpiX(QPrinter* printer)
{
    return printer && printer->logicalDpiX() > 0
        ? printer->logicalDpiX() : 1.0;
}

qreal printerDpiY(QPrinter* printer)
{
    return printer && printer->logicalDpiY() > 0
        ? printer->logicalDpiY() : 1.0;
}

SIZE comicsPrintPageSize(QPrinter* printer)
{
    if (!printer) return {};
    const QRectF page = printer->pageRect(QPrinter::DevicePixel);
    SIZE result{
        static_cast<LONG>(page.width() * 1440.0 / printerDpiX(printer)),
        static_cast<LONG>(page.height() * 1440.0 / printerDpiY(printer))
    };
    result.cy = std::max<LONG>(0, result.cy - FOOTERHEIGHT);
    return result;
}

QString localPrintTimeDate()
{
    const QLocale locale;
    const QDateTime now = QDateTime::currentDateTime();
    return locale.toString(now.time(), QLocale::LongFormat)
        + QString::fromLatin1(TIMEDATESEP)
        + locale.toString(now.date(), QLocale::ShortFormat);
}

void AppendViewContextMenu(QMenu& menu,
                           const QList<OriginalMenuItem>& items)
{
    for (const OriginalMenuItem& item : items) {
        if (item.type == OriginalMenuItemType::Separator) {
            menu.addSeparator();
        } else if (item.type == OriginalMenuItemType::Popup) {
            QMenu* popup = menu.addMenu(item.text);
            AppendViewContextMenu(*popup, item.children);
        } else {
            QAction* action = menu.addAction(item.text);
            action->setData(item.commandIdentifier);
            action->setStatusTip(originalResourceString(
                item.commandIdentifier).section(QLatin1Char('\n'), 0, 0));
        }
    }
}

void ExecuteViewContextCommand(CChatDoc* document, const QString& command)
{
    if (command == QLatin1String("ID_EDIT_COPY")) {
        if (QTextEdit* edit = qobject_cast<QTextEdit*>(
                QApplication::focusWidget())) {
            edit->copy();
        }
    } else if (command == QLatin1String("ID_CLEAR_HISTORY")) {
        if (document) document->OnClearHistory();
    } else if (command == QLatin1String("ID_VIEW_TEXT")) {
        if (document) document->OnViewText();
    } else if (command == QLatin1String("ID_CHANNELPROPS")) {
        if (document && document->GetConnectionStatus() == CX_INCHANNEL
            && g_puiSelf && document->m_puiSelf
            && !document->m_allChannelPuis.isEmpty()) {
            document->OnChannelprops();
        }
    }
}
}

BOOL g_bNewedPanel = FALSE;
CUserInfo* mousedPui = nullptr;

CPageView::CPageView(QWidget* parent)
    : CPageView(GetChatDoc(), parent)
{
}

CPageView::CPageView(CChatDoc* document, QWidget* parent)
    : QAbstractScrollArea(parent)
    , m_doc(document)
{
    viewport()->setAutoFillBackground(false);
    viewport()->setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    horizontalScrollBar()->setSingleStep(1);
    verticalScrollBar()->setSingleStep(1);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        m_bAtBottom = AtBottom();
        viewport()->update();
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged,
            viewport(), qOverload<>(&QWidget::update));
}

CPageView::~CPageView()
{
    theApp.SaveToReg(TRUE);
    FreeRetainedPanelP();
    delete m_footerFont;
    if (m_doc && m_doc->m_view == this)
        m_doc->m_view = nullptr;
}

BOOL CPageView::OnPreparePrinting(QPrinter* printer) const
{
    return printer && m_doc;
}

void CPageView::FreeRetainedPanelP()
{
    delete m_printRetainedPanel;
    m_printRetainedPanel = nullptr;
}

void CPageView::OnBeginPrinting(QPrinter* printer)
{
    delete m_footerFont;
    m_footerFont = new QFont(theApp.m_comicsFont);
    m_footerFont->setPixelSize(FOOTERFONTHEIGHT);
    m_footerFont->setWeight(QFont::Normal);

    // Modern creates one 24-bit retained panel from the printer DC here and
    // reuses it until OnEndPrinting. RGB888 is the direct Qt storage adapter.
    FreeRetainedPanelP();
    if (printer) {
        const int width = std::max(1, static_cast<int>(std::ceil(
            CUnitPanelPage::m_unitWidth * printerDpiX(printer) / 1440.0)));
        const int height = std::max(1, static_cast<int>(std::ceil(
            CUnitPanelPage::m_unitHeight * printerDpiY(printer) / 1440.0)));
        m_printRetainedPanel = new QImage(
            width, height, QImage::Format_RGB888);
        m_printRetainedPanel->fill(Qt::white);
    }
    m_printPage = nullptr;
    m_printPhysicalPage = 0;
}

int CPageView::GetPhysicalPageCount(QPrinter* printer) const
{
    if (!printer || !m_doc) return 0;
    const SIZE pageSize = comicsPrintPageSize(printer);
    int physicalPages = 0;
    for (CPage* page : m_doc->m_pages) {
        if (page) physicalPages += page->GetPhysicalPageCount(pageSize);
    }
    return physicalPages;
}

void CPageView::OnPrepareDC(QPrinter* printer, UINT pageNumber)
{
    m_printPage = nullptr;
    m_printPhysicalPage = pageNumber;
    m_printInfo = {};
    m_printPageSize = comicsPrintPageSize(printer);
    if (!m_doc || pageNumber == 0) return;

    UINT pagesSoFar = 0;
    UINT pagesBeforeThis = 0;
    for (CPage* page : m_doc->m_pages) {
        if (!page) continue;
        const UINT physicalPages = static_cast<UINT>(
            page->GetPhysicalPageCount(m_printPageSize));
        pagesSoFar += physicalPages;
        if (pageNumber <= pagesSoFar) {
            m_printPage = page;
            m_printInfo = page->PreparePrintDC(
                m_printPageSize,
                static_cast<int>(pageNumber - pagesBeforeThis));
            return;
        }
        pagesBeforeThis += physicalPages;
    }
}

void CPageView::PrintFooter(QPainter* painter, const QRectF& pageRect,
                            UINT pageNumber, qreal dpiX, qreal dpiY) const
{
    if (!painter || !painter->isActive() || !m_footerFont) return;
    QFont footer(*m_footerFont);
    footer.setPixelSize(std::max(
        1, static_cast<int>(std::lround(
            FOOTERFONTHEIGHT * dpiY / 1440.0))));
    footer.setWeight(QFont::Normal);

    const qreal left = pageRect.left() + FOOTERMARGIN * dpiX / 1440.0;
    const qreal right = pageRect.right() - FOOTERMARGIN * dpiX / 1440.0;
    const qreal bottom = pageRect.bottom() - dpiY / 1440.0;
    QString page = originalResourceString(QStringLiteral("IDS_PAGEFOOTER"));
    page.replace(QStringLiteral("%1"), QString::number(pageNumber));
    const QString title = QStringLiteral("Microsoft Chat");
    const QString dateTime = localPrintTimeDate();

    painter->save();
    painter->setClipping(false);
    painter->setPen(Qt::black);
    painter->setFont(footer);
    const QFontMetricsF metrics(footer, painter->device());
    const qreal baseline = bottom - metrics.descent();
    painter->drawText(QPointF(left, baseline), title);
    painter->drawText(QPointF((left + right
        - metrics.horizontalAdvance(page)) / 2.0, baseline), page);
    painter->drawText(QPointF(right
        - metrics.horizontalAdvance(dateTime), baseline), dateTime);
    painter->restore();
}

void CPageView::OnPrint(QPrinter* printer, QPainter* painter,
                        UINT pageNumber)
{
    if (!printer || !painter) return;
    if (m_printPhysicalPage != pageNumber) OnPrepareDC(printer, pageNumber);
    if (!m_printPage) return;

    const QRectF pageRect = printer->pageRect(QPrinter::DevicePixel);
    const qreal dpiX = printerDpiX(printer);
    const qreal dpiY = printerDpiY(printer);
    m_printPage->Draw(this, painter, m_printInfo, dpiX / 1440.0,
                      dpiY / 1440.0, pageRect.topLeft());
    PrintFooter(painter, pageRect, pageNumber, dpiX, dpiY);
}

void CPageView::OnEndPrinting(QPrinter*)
{
    delete m_footerFont;
    m_footerFont = nullptr;
    FreeRetainedPanelP();
    m_printPage = nullptr;
    m_printPhysicalPage = 0;
    m_printInfo = {};
    m_printPageSize = {};
    FlushBackDropCache(FALSE);
}

qreal CPageView::logicalUnitsPerPixelX() const
{
    const qreal dpi = logicalDpiX() > 0 ? logicalDpiX() : 96.0;
    return 1440.0 / dpi;
}

qreal CPageView::logicalUnitsPerPixelY() const
{
    const qreal dpi = logicalDpiY() > 0 ? logicalDpiY() : 96.0;
    return 1440.0 / dpi;
}

int CPageView::GetProspectivePanelWidth(int wide) const
{
    if (wide <= 0) return 0;
    const int logicalWidth = static_cast<int>(std::ceil(
        std::max(0, viewport()->width() - SCROLLWIDTH) * logicalUnitsPerPixelX()));
    int goalPanelWidth = (logicalWidth
        + CUnitPanelPage::m_vInterstice * (1 - wide)) / wide;
    const int logicalHeight = static_cast<int>(std::ceil(
        viewport()->height() * logicalUnitsPerPixelY()));
    int high = static_cast<int>(std::ceil(
        static_cast<double>(logicalHeight + CUnitPanelPage::m_hInterstice)
        / (goalPanelWidth + CUnitPanelPage::m_hInterstice)));
    high = std::max(high, 1);
    const int goalPanelHeight = (logicalHeight
        + CUnitPanelPage::m_hInterstice * (1 - high)) / high;
    return std::min(goalPanelWidth, goalPanelHeight);
}

void CPageView::SetPanelsWide(int wide, int forcedPanelWidth)
{
    int goalPanelWidth = forcedPanelWidth
        ? forcedPanelWidth : GetProspectivePanelWidth(wide);
    goalPanelWidth = std::max(goalPanelWidth, MINUNITPANELWIDTH);
    CUnitPanelPage::SetUnitPanelWidth(goalPanelWidth);
    CUnitPanelPage::SetUnitPanelHeight(goalPanelWidth);
    CUnitPanelPage::SetUnitPanelsPerRow(wide);
    ResetExistingPanels(TRUE);
    if (m_doc) m_doc->ExecuteHistory(HM_RELOAD);
    ScrollToBottom();
}

int CPageView::FitPanelsWide() const
{
    int best = 1;
    for (int count = 1; count <= 5; ++count) {
        if (GetProspectivePanelWidth(count) >= COMFORTABLEPANELWIDTH) best = count;
        else break;
    }
    return best;
}

void CPageView::ResetExistingPanels(BOOL addPage)
{
    if (!m_doc) m_doc = GetChatDoc();
    if (!m_doc) return;
    m_doc->DestroyPages();
    horizontalScrollBar()->setRange(0, 0);
    verticalScrollBar()->setRange(0, 0);
    if (addPage) {
        m_doc->AddNewPage();
        CPage* firstPage = m_doc->m_pages.first();
        const QByteArray title = IntlTextFromQString(
            QStringView(m_doc->GetComicsTitle()));
        firstPage->AddTitle(title.constData());
    }
    updateScrollRanges();
    viewport()->update();
}

void CPageView::UpdateTitle()
{
    if (!m_doc || m_doc->m_pages.isEmpty()) return;
    m_doc->m_pages.first()->UpdateTitle();
}

void UpdateTitle(CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target || target->m_pages.isEmpty()) return;
    target->m_pages.first()->UpdateTitle();
}

void CPageView::ClearHistory()
{
    ResetExistingPanels(TRUE);
}

QRect CPageView::panelDeviceRect(int panel) const
{
    const int columns = std::max(1, CUnitPanelPage::m_panelsPerRow);
    const int row = panel / columns;
    const int column = panel % columns;
    const qreal unitX = logicalUnitsPerPixelX();
    const qreal unitY = logicalUnitsPerPixelY();
    const int panelWidth = static_cast<int>(std::ceil(CUnitPanelPage::m_unitWidth / unitX));
    const int panelHeight = static_cast<int>(std::ceil(CUnitPanelPage::m_unitHeight / unitY));
    const int gapX = static_cast<int>(std::ceil(CUnitPanelPage::m_vInterstice / unitX));
    const int gapY = static_cast<int>(std::ceil(CUnitPanelPage::m_hInterstice / unitY));
    return QRect(column * (panelWidth + gapX) - horizontalScrollBar()->value(),
                 row * (panelHeight + gapY) - verticalScrollBar()->value(),
                 panelWidth, panelHeight);
}

void CPageView::updateScrollRanges()
{
    const qreal unitX = logicalUnitsPerPixelX();
    const qreal unitY = logicalUnitsPerPixelY();
    int contentWidth = 0;
    int contentHeight = 0;
    if (m_doc) {
        // CPageView::OnUpdate unions each page's CDamage rectangle into
        // m_bbox. Recompute that same extent here because Qt does not carry
        // MFC update hints. Do not stack pages: AddNewPage() leaves every
        // page at (0,0) and marks later placement as unfinished.
        for (CPage* page : m_doc->m_pages) {
            if (!page || page->m_panels.isEmpty()) continue;
            RECT box{};
            page->GetBBox(&box);
            contentWidth = std::max(contentWidth, static_cast<int>(
                std::ceil(box.right / unitX)));
            contentHeight = std::max(contentHeight, static_cast<int>(
                std::ceil(-box.bottom / unitY)));
        }
    }
    const QSize logicalPage = CUnitPanelPage::GetScrollPage();
    const int pageX = std::max(1, static_cast<int>(std::ceil(
        logicalPage.width() / logicalUnitsPerPixelX())));
    const int pageY = std::max(1, static_cast<int>(std::ceil(
        logicalPage.height() / logicalUnitsPerPixelY())));
    horizontalScrollBar()->setPageStep(pageX);
    verticalScrollBar()->setPageStep(pageY);
    horizontalScrollBar()->setSingleStep(std::max(1, pageX / 10));
    verticalScrollBar()->setSingleStep(std::max(1, pageY / 10));
    horizontalScrollBar()->setRange(0, std::max(0, contentWidth - viewport()->width()));
    verticalScrollBar()->setRange(0, std::max(0, contentHeight - viewport()->height()));
}

BOOL CPageView::AtBottom() const
{
    return verticalScrollBar()->value() == verticalScrollBar()->maximum()
        || (verticalScrollBar()->value() == 0
            && viewport()->height() >= verticalScrollBar()->maximum());
}

void CPageView::UpdateScroll()
{
    updateScrollRanges();
}

void CPageView::ScrollToBottom()
{
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void CPageView::RefreshPanelN(int panel)
{
    const BOOL wasAtBottom = m_bAtBottom;
    updateScrollRanges();
    if (panel != 0 && wasAtBottom) ScrollToBottom();
    viewport()->update(panelDeviceRect(panel));
}

void CPageView::paintEvent(QPaintEvent* event)
{
    QPainter painter(viewport());
    painter.fillRect(event->rect(), Qt::white);
    painter.setRenderHint(QPainter::Antialiasing, false);
    if (!m_doc || m_doc->m_pages.isEmpty()) return;

    // CPageView::OnDraw walks every CPage from the head of m_pages. The
    // original screen Draw starts each page at (0,0); retaining that overlap
    // is intentional until original code supplies the commented-out page
    // placement calculation.
    for (CPage* page : m_doc->m_pages) {
        if (!page) continue;
        for (int index = 0; index < page->m_panels.size(); ++index) {
            const QRect target = panelDeviceRect(index);
            if (!target.intersects(event->rect())) continue;
            CPanel* panel = page->m_panels[index];
            if (!panel) continue;
            QImage retained(target.size(), QImage::Format_RGB32);
            retained.fill(Qt::white);
            QtPaintDC dc(&retained, CUnitPanelPage::m_unitWidth,
                         CUnitPanelPage::m_unitHeight);
            RECT damage{0, 0, CUnitPanelPage::m_unitWidth,
                        -CUnitPanelPage::m_unitHeight};
            panel->Draw(&dc, nullptr, &damage);
            painter.drawImage(target.topLeft(), retained);
        }
    }
}

BOOL CPageView::panelPointFromDevice(POINT point, int panel,
                                     POINT& panelPoint) const
{
    const QRect target = panelDeviceRect(panel);
    if (!target.contains(QPoint(point.x, point.y))
        || target.width() <= 0 || target.height() <= 0) {
        return FALSE;
    }
    panelPoint.x = static_cast<LONG>(
        (point.x - target.left())
        * static_cast<qreal>(CUnitPanelPage::m_unitWidth) / target.width());
    panelPoint.y = static_cast<LONG>(
        -(point.y - target.top())
        * static_cast<qreal>(CUnitPanelPage::m_unitHeight) / target.height());
    return TRUE;
}

unsigned int CPageView::FindAvatarUnderPoint(POINT point)
{
    if (!m_doc) m_doc = GetChatDoc();
    if (!m_doc) return 0;
    for (CPage* page : m_doc->m_pages) {
        if (!page) continue;
        for (int panelIndex = 0; panelIndex < page->m_panels.size();
             ++panelIndex) {
            POINT panelPoint{};
            if (!panelPointFromDevice(point, panelIndex, panelPoint)) continue;
            CPanel* panel = page->m_panels[panelIndex];
            if (!panel) continue;
            for (CBody* body : panel->m_bodies) {
                if (body && inside_bbox(&panelPoint, &body->m_bbox))
                    return body->m_avatarID;
            }
        }
    }
    return 0;
}

void* CPageView::FindLabelUnderPoint(POINT point, POINT& panelPoint,
                                     void*& hitPanel)
{
    hitPanel = nullptr;
    if (!m_doc) m_doc = GetChatDoc();
    if (!m_doc) return nullptr;
    for (CPage* page : m_doc->m_pages) {
        if (!page) continue;
        for (int panelIndex = 0; panelIndex < page->m_panels.size();
             ++panelIndex) {
            if (!panelPointFromDevice(point, panelIndex, panelPoint)) continue;
            CPanel* panel = page->m_panels[panelIndex];
            if (!panel) continue;
            for (CPanelElement* element : panel->m_elements) {
                CLabel* label = dynamic_cast<CLabel*>(element);
                if (label && inside_bbox(&panelPoint, &label->m_bbox)) {
                    hitPanel = panel;
                    return label;
                }
            }
        }
    }
    return nullptr;
}

void CPageView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        if (m_doc) {
            m_doc->CycleFocus(CHATFOCUS_COMICVIEW,
                              event->key() == Qt::Key_Backtab
                                  || (event->modifiers() & Qt::ShiftModifier));
        }
        event->accept();
        return;
    }

    QScrollBar* horizontal = horizontalScrollBar();
    QScrollBar* vertical = verticalScrollBar();
    const bool hasHorizontal = horizontal->maximum() > horizontal->minimum()
        && horizontal->pageStep() != 0;
    const bool hasVertical = vertical->maximum() > vertical->minimum()
        && vertical->pageStep() != 0;
    bool handled = false;
    switch (event->key()) {
    case Qt::Key_Home:
        if (hasHorizontal || hasVertical) {
            horizontal->setValue(horizontal->minimum());
            vertical->setValue(vertical->minimum());
            handled = true;
        }
        break;
    case Qt::Key_End:
        if (hasHorizontal || hasVertical) {
            if (hasHorizontal) horizontal->setValue(horizontal->maximum());
            if (hasVertical) vertical->setValue(vertical->maximum());
            handled = true;
        }
        break;
    case Qt::Key_Down:
        if (hasVertical) {
            vertical->setValue(vertical->value() + vertical->singleStep());
            handled = true;
        }
        break;
    case Qt::Key_Up:
        if (hasVertical) {
            vertical->setValue(vertical->value() - vertical->singleStep());
            handled = true;
        }
        break;
    case Qt::Key_PageDown:
        if (hasVertical) {
            vertical->setValue(vertical->value() + vertical->pageStep());
            handled = true;
        }
        break;
    case Qt::Key_PageUp:
        if (hasVertical) {
            vertical->setValue(vertical->value() - vertical->pageStep());
            handled = true;
        }
        break;
    case Qt::Key_Left:
        if (hasHorizontal) {
            horizontal->setValue(horizontal->value() - horizontal->singleStep());
            handled = true;
        }
        break;
    case Qt::Key_Right:
        if (hasHorizontal) {
            horizontal->setValue(horizontal->value() + horizontal->singleStep());
            handled = true;
        }
        break;
    default:
        break;
    }
    if (handled) {
        event->accept();
        return;
    }

    if (!event->text().isEmpty()
        && event->text().front().unicode() >= 11) {
        ForwardToSayWnd(event->text().front().unicode());
        event->accept();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void CPageView::mousePressEvent(QMouseEvent* event)
{
    POINT point{event->position().toPoint().x(),
                event->position().toPoint().y()};
    const UINT avatarID = FindAvatarUnderPoint(point);
    if (event->button() == Qt::LeftButton) {
        if (!avatarID) {
            POINT panelPoint{};
            void* panel = nullptr;
            auto* label = static_cast<CLabel*>(
                FindLabelUnderPoint(point, panelPoint, panel));
            if (label && bURLPresent(label->m_prgdwFormatting)) {
                label->OnLButtonDown(panelPoint, static_cast<CPanel*>(panel));
                event->accept();
                return;
            }
        }
        if (g_puiSelf) {
            CAvatarX* avatar = avatarID
                ? GetAvatar(static_cast<USHORT>(avatarID)) : nullptr;
            auto* addressee = avatar
                ? static_cast<CUserInfo*>(avatar->m_userInfo) : nullptr;
            const BOOL extend = event->modifiers()
                & (Qt::ControlModifier | Qt::ShiftModifier);
            g_puiSelf->SelectInMemberList(addressee, TRUE, extend);
            if (m_doc && m_doc->m_memberList)
                m_doc->m_memberList->MakeVisible(addressee);
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton && g_puiSelf) {
        CAvatarX* avatar = avatarID
            ? GetAvatar(static_cast<USHORT>(avatarID)) : nullptr;
        auto* addressee = avatar
            ? static_cast<CUserInfo*>(avatar->m_userInfo) : nullptr;
        g_puiSelf->SelectInMemberList(
            addressee, TRUE, event->modifiers() & Qt::ShiftModifier);
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void CPageView::OnContextMenu(QContextMenuEvent* event)
{
    const BOOL keyboard = event->reason() == QContextMenuEvent::Keyboard;
    POINT clientPoint{-1, -1};
    QPoint screenPoint;
    if (keyboard) {
        screenPoint = viewport()->mapToGlobal(viewport()->rect().center());
    } else {
        clientPoint = POINT{event->pos().x(), event->pos().y()};
        screenPoint = event->globalPos();
    }

    const UINT avatarID = FindAvatarUnderPoint(clientPoint);
    if (avatarID) {
        CAvatarX* avatar = GetAvatar(static_cast<USHORT>(avatarID));
        mousedPui = avatar
            ? static_cast<CUserInfo*>(avatar->m_userInfo) : nullptr;
        ShowMemberContext(screenPoint.x(), screenPoint.y());
        return;
    }

    QMenu menu(this);
    const QList<OriginalMenuItem> resource = originalMenuResource(
        QStringLiteral("IDR_VIEWCONTEXT"));
    if (resource.size() == 1
        && resource.first().type == OriginalMenuItemType::Popup) {
        AppendViewContextMenu(menu, resource.first().children);
    } else {
        AppendViewContextMenu(menu, resource);
    }

    QActionGroup viewGroup(&menu);
    viewGroup.setExclusive(true);
    QTextEdit* focusedEdit = qobject_cast<QTextEdit*>(
        QApplication::focusWidget());
    for (QAction* action : menu.actions()) {
        const QString command = action->data().toString();
        if (command == QLatin1String("ID_EDIT_COPY")) {
            action->setEnabled(focusedEdit
                && focusedEdit->textCursor().hasSelection());
        } else if (command == QLatin1String("ID_VIEW_COMICS")) {
            viewGroup.addAction(action);
            action->setCheckable(true);
            action->setChecked(m_doc && m_doc->m_bComicView);
            action->setEnabled(!m_doc || !m_doc->m_proto
                || !(m_doc->m_proto->m_dwModes & CM_NOFORMAT));
        } else if (command == QLatin1String("ID_VIEW_TEXT")) {
            viewGroup.addAction(action);
            action->setCheckable(true);
            action->setChecked(m_doc && !m_doc->m_bComicView);
        } else if (command == QLatin1String("ID_CHANNELPROPS")) {
            action->setEnabled(m_doc
                && m_doc->GetConnectionStatus() == CX_INCHANNEL
                && g_puiSelf && m_doc->m_puiSelf
                && !m_doc->m_allChannelPuis.isEmpty());
        }
    }
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->ConfigureContextMenu(&menu);
    if (QAction* selected = menu.exec(screenPoint))
        ExecuteViewContextCommand(m_doc, selected->data().toString());
}

bool CPageView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::ContextMenu) {
        OnContextMenu(static_cast<QContextMenuEvent*>(event));
        return true;
    }
    if (event->type() == QEvent::ToolTip) {
        auto* helpEvent = static_cast<QHelpEvent*>(event);
        POINT point{helpEvent->pos().x(), helpEvent->pos().y()};
        const UINT avatarID = FindAvatarUnderPoint(point);
        CAvatarX* avatar = avatarID
            ? GetAvatar(static_cast<USHORT>(avatarID)) : nullptr;
        const char* screenName = nullptr;
        if (avatar) avatar->GetScreenName(&screenName);
        if (screenName) {
            QToolTip::showText(helpEvent->globalPos(),
                               IntlTextToQString(screenName), viewport());
        } else {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void CPageView::ScheduleAutoFitPanels()
{
    if (m_bAutoFitPending) return;
    m_bAutoFitPending = TRUE;
    QTimer::singleShot(0, this, [this] {
        m_bAutoFitPending = FALSE;
        AutoFitPanels();
    });
}

void CPageView::AutoFitPanels()
{
    if (m_bAutoFitting || !theApp.m_bComicView || !m_doc) return;
    const int fit = FitPanelsWide();
    if (fit > 0 && fit != CUnitPanelPage::GetUnitPanelsPerRow()) {
        m_bAutoFitting = TRUE;
        SetPanelsWide(fit);
        m_bAutoFitting = FALSE;
    }
}

void CPageView::resizeEvent(QResizeEvent* event)
{
    const BOOL firstSize = m_bFirstTime;
    if (m_bAtBottom) ScrollToBottom();
    QAbstractScrollArea::resizeEvent(event);
    if (m_bFirstTime && viewport()->width() > 0 && viewport()->height() > 0) {
        m_bFirstTime = FALSE;
        bool hasMessages = false;
        if (m_doc && !m_doc->m_pages.isEmpty())
            hasMessages = m_doc->m_pages.first()->m_panels.size() > 1;
        if (!hasMessages && CUnitPanelPage::m_unitWidth < MINUNITPANELWIDTH)
            SetPanelsWide(DEFAULTPANELPERCOLUMN);
    }
    updateScrollRanges();
    if (!firstSize && viewport()->width() > 0 && viewport()->height() > 0
        && theApp.m_bComicView) {
        ScheduleAutoFitPanels();
    }
}
