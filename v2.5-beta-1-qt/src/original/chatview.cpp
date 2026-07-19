// Ported from v2.5-beta-1-modern/chatview.cpp.

#include "chatview.h"

#include "bodycam.h"
#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "memblst.h"
#include "pageview.h"
#include "saywnd.h"
#include "status.h"
#include "textview.h"
#include "userinfo.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitter>
#include <QSplitterHandle>
#include <QVBoxLayout>
#include <QtPrintSupport/QAbstractPrintDialog>
#include <QtPrintSupport/QPrintDialog>
#include <QtPrintSupport/QPrinter>

#include <algorithm>

namespace {
class CFixedSplitterHandle final : public QSplitterHandle {
public:
    CFixedSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
        : QSplitterHandle(orientation, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }
    void mouseMoveEvent(QMouseEvent* event) override { event->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { event->accept(); }
};
}

CFixedSplitter::CFixedSplitter(QWidget* parent)
    : CSplitSay(parent)
{
}

QSplitterHandle* CFixedSplitter::createHandle()
{
    return new CFixedSplitterHandle(orientation(), this);
}

CChatView::CChatView(CChatDoc* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    if (m_doc) {
        m_doc->m_client = this;
    }
    if (m_doc && m_doc->m_bStatusView)
        CreateStatusView();
    else if (m_doc && !m_doc->m_bComicView)
        CreateTextView(false);
    else
        CreateComicView(false);
}

CChatView::~CChatView()
{
    clearLayout();
    if (m_doc && m_doc->m_client == this) m_doc->m_client = nullptr;
}

void CChatView::clearLayout()
{
    if (m_wndSplitter) {
        if (m_doc) {
            m_doc->m_view = nullptr;
            m_doc->m_textView = nullptr;
            m_doc->m_sayWnd = nullptr;
            m_doc->m_memberList = nullptr;
            m_doc->m_bodyCam = nullptr;
        }
        layout()->removeWidget(m_wndSplitter);
        delete m_wndSplitter;
        m_wndSplitter = nullptr;
        m_wndLSplitter = nullptr;
        m_wndRSplitter = nullptr;
    }
}

void CChatView::CreateComicView(bool doUpdate)
{
    clearLayout();
    if (!m_doc) {
        return;
    }
    m_doc->m_bComicView = true;
    m_doc->m_bStatusView = false;
    theApp.m_bComicView = true;

    auto* outer = new CSplitChatV(this);
    auto* right = new CSplitChat(outer);
    auto* left = new CFixedSplitter(outer);

    auto* page = new CPageView(m_doc, left);
    CSayWnd::SetDefaultButtons(SB_SAY | SB_THINK | SB_WHISPER | SB_ACTION | SB_SOUND);
    auto* say = new CSayWnd(left);
    auto* members = new CMemberList(right);
    auto* bodyCam = new CBodyCam(right);

    left->addWidget(page);
    left->addWidget(say);
    right->addWidget(members);
    right->addWidget(bodyCam);
    outer->addWidget(left);
    outer->addWidget(right);
    layout()->addWidget(outer);
    m_wndSplitter = outer;
    m_wndLSplitter = left;
    m_wndRSplitter = right;
    m_doc->m_view = page;
    m_doc->m_textView = nullptr;
    m_doc->m_sayWnd = say;
    m_doc->m_memberList = members;
    m_doc->m_bodyCam = bodyCam;
    if (m_doc->m_bLastMemberView) m_doc->OnViewIcon();
    else m_doc->OnViewListAux();
    if (doUpdate) {
        // CChatDoc::OnViewComics rebuilds the page model only when changing
        // back from Text view.  Initial view creation preserves the title page
        // made by CChatDoc::InitMyDocument before the view exists.
        page->ResetExistingPanels(true);
        m_doc->ExecuteHistory(HM_RELOAD);
        for (CUserInfo* pui : m_doc->m_allChannelPuis) {
            if (pui && !pui->IsDeparted()) members->AddUser(pui);
        }
        if (g_puiSelf) g_puiSelf->ClearTalkTos();
    } else {
        page->RefreshPanelN(0);
    }
}

void CChatView::CreateTextView(bool doUpdate)
{
    clearLayout();
    if (!m_doc) {
        return;
    }
    m_doc->m_bLastMemberView = m_doc->m_bIconMembers;
    m_doc->m_bComicView = false;
    m_doc->m_bStatusView = false;
    theApp.m_bComicView = false;

    auto* outer = new CSplitChatV(this);
    auto* left = new CFixedSplitter(outer);
    auto* text = new CTextView(m_doc, left);
    CSayWnd::SetDefaultButtons(SB_SAY | SB_THINK | SB_WHISPER | SB_ACTION | SB_SOUND);
    auto* say = new CSayWnd(left);
    auto* members = new CMemberList(outer);

    left->addWidget(text);
    left->addWidget(say);
    outer->addWidget(left);
    outer->addWidget(members);
    layout()->addWidget(outer);
    m_wndSplitter = outer;
    m_wndLSplitter = left;
    m_doc->m_view = nullptr;
    m_doc->m_textView = text;
    m_doc->m_sayWnd = say;
    m_doc->m_memberList = members;
    m_doc->m_bodyCam = nullptr;
    m_doc->OnViewListAux();
    if (doUpdate) {
        m_doc->DestroyPages();
        m_doc->ExecuteHistory(HM_RELOAD);
        for (CUserInfo* pui : m_doc->m_allChannelPuis) {
            if (pui && !pui->IsDeparted()) members->AddUser(pui);
        }
        if (g_puiSelf) g_puiSelf->ClearTalkTos();
    }
}

void CChatView::CreateStatusView()
{
    clearLayout();
    if (!m_doc) return;
    m_doc->m_bComicView = false;
    m_doc->m_bStatusView = true;
    auto* splitter = new CFixedSplitter(this);
    auto* status = new CStatusView(m_doc, splitter);
    CSayWnd::SetDefaultButtons(0);
    auto* say = new CSayWnd(splitter);
    splitter->addWidget(status);
    splitter->addWidget(say);
    layout()->addWidget(splitter);
    m_wndSplitter = splitter;
    m_wndLSplitter = splitter;
    m_doc->m_view = nullptr;
    m_doc->m_textView = status;
    m_doc->m_sayWnd = say;
    m_doc->m_memberList = nullptr;
    m_doc->m_bodyCam = nullptr;
}

QWidget* CChatView::GetPrimaryView() const
{
    return !m_doc ? nullptr
        : (m_doc->m_bComicView ? static_cast<QWidget*>(m_doc->m_view)
                              : static_cast<QWidget*>(m_doc->m_textView));
}

BOOL CChatView::OnPreparePrinting(QPrinter* printer)
{
    QWidget* primary = GetPrimaryView();
    if (auto* page = dynamic_cast<CPageView*>(primary))
        return page->OnPreparePrinting(printer);
    if (auto* text = dynamic_cast<CTextView*>(primary))
        return text->OnPreparePrinting(printer);
    return FALSE;
}

void CChatView::OnBeginPrinting(QPrinter* printer)
{
    QWidget* primary = GetPrimaryView();
    if (auto* page = dynamic_cast<CPageView*>(primary))
        page->OnBeginPrinting(printer);
    else if (auto* text = dynamic_cast<CTextView*>(primary))
        text->OnBeginPrinting(printer);
}

void CChatView::OnEndPrinting(QPrinter* printer)
{
    QWidget* primary = GetPrimaryView();
    if (auto* page = dynamic_cast<CPageView*>(primary))
        page->OnEndPrinting(printer);
    else if (auto* text = dynamic_cast<CTextView*>(primary))
        text->OnEndPrinting(printer);
}

void CChatView::OnPrepareDC(QPrinter* printer, UINT pageNumber)
{
    if (auto* page = dynamic_cast<CPageView*>(GetPrimaryView()))
        page->OnPrepareDC(printer, pageNumber);
}

void CChatView::OnPrint(QPrinter* printer, QPainter* painter,
                        UINT pageNumber)
{
    QWidget* primary = GetPrimaryView();
    if (auto* page = dynamic_cast<CPageView*>(primary))
        page->OnPrint(printer, painter, pageNumber);
    else if (auto* text = dynamic_cast<CTextView*>(primary))
        text->OnPrint(printer, painter, pageNumber);
}

int CChatView::GetPhysicalPageCount(QPrinter* printer)
{
    QWidget* primary = GetPrimaryView();
    if (auto* page = dynamic_cast<CPageView*>(primary))
        return page->GetPhysicalPageCount(printer);
    if (auto* text = dynamic_cast<CTextView*>(primary))
        return static_cast<int>(text->lPrintPage(
            printer, nullptr, 0, FALSE));
    return 0;
}

BOOL CChatView::OnFilePrint(QPrinter* printer, BOOL direct)
{
    if (!printer || !OnPreparePrinting(printer)) return FALSE;
    if (m_doc) printer->setDocName(m_doc->GetTitle());

    const int pageCount = GetPhysicalPageCount(printer);
    if (pageCount <= 0) return FALSE;
    if (!direct) {
        QPrintDialog dialog(printer, this);
        dialog.setMinMax(1, pageCount);
        dialog.setFromTo(1, pageCount);
        dialog.setOption(QAbstractPrintDialog::PrintPageRange,
                         pageCount > 1);
        dialog.setOption(QAbstractPrintDialog::PrintSelection, false);
        dialog.setOption(QAbstractPrintDialog::PrintCurrentPage, false);
        if (dialog.exec() != QDialog::Accepted) return FALSE;
    }
    return Print(printer);
}

BOOL CChatView::Print(QPrinter* printer)
{
    if (!printer || !OnPreparePrinting(printer)) return FALSE;
    OnBeginPrinting(printer);
    const int pageCount = GetPhysicalPageCount(printer);
    if (pageCount <= 0) {
        OnEndPrinting(printer);
        return FALSE;
    }

    int firstPage = 1;
    int lastPage = pageCount;
    if (printer->printRange() == QPrinter::PageRange) {
        firstPage = std::clamp(printer->fromPage(), 1, pageCount);
        lastPage = std::clamp(printer->toPage(), firstPage, pageCount);
    }

    QList<int> pages;
    for (int page = firstPage; page <= lastPage; ++page) pages.append(page);
    if (printer->pageOrder() == QPrinter::LastPageFirst)
        std::reverse(pages.begin(), pages.end());

    QPainter painter;
    if (!painter.begin(printer)) {
        OnEndPrinting(printer);
        return FALSE;
    }

    BOOL success = TRUE;
    for (int index = 0; index < pages.size(); ++index) {
        if (index > 0 && !printer->newPage()) {
            success = FALSE;
            break;
        }
        const UINT page = static_cast<UINT>(pages[index]);
        OnPrepareDC(printer, page);
        OnPrint(printer, &painter, page);
    }
    painter.end();
    OnEndPrinting(printer);
    return success;
}
