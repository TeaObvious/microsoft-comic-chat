// Ported from v2.5-beta-1-modern/childfrm.cpp.

#include "childfrm.h"

#include "chat.h"
#include "chatdoc.h"
#include "chatview.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "protsupp.h"
#include "tabbar.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QHideEvent>
#include <QMdiArea>
#include <QMoveEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QTimer>

CChildFrame::CChildFrame(CChatDoc* document, bool ownsDocument,
                         QWidget* parent)
    : QMdiSubWindow(parent)
    , m_document(document)
    , m_view(new CChatView(document, this))
    , m_ownsDocument(ownsDocument)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWidget(m_view);
}

CChildFrame::~CChildFrame()
{
    if (m_ownsDocument && m_document && !m_document->IsCloseStarted())
        m_document->OnCloseDocument();
    if (m_document) {
        m_document->m_client = nullptr;
        m_document->m_view = nullptr;
        m_document->m_textView = nullptr;
        m_document->m_sayWnd = nullptr;
        m_document->m_memberList = nullptr;
        m_document->m_bodyCam = nullptr;
    }
    if (m_view) {
        setWidget(nullptr);
        delete m_view;
        m_view = nullptr;
    }
    if (m_ownsDocument) delete m_document;
    m_document = nullptr;
}

void CChildFrame::ActivateFrame(bool activate)
{
    const bool statusNoActivate = m_document
        && m_document->m_bStatusView && !activate;
    QMdiArea* area = mdiArea();
    const bool zeroHeight = !widget() || widget()->height() == 0
        || (area && !area->isVisible());
    const bool maximizeFirstFrame = !m_bPositioned
        && !statusNoActivate
        && ((theApp.m_flags1 & F1_MAXMDI) || zeroHeight);
    QMdiSubWindow* previous = area ? area->activeSubWindow() : nullptr;
    const QPointer<QWidget> previousFocus =
        statusNoActivate ? QApplication::focusWidget() : nullptr;
    const auto reveal = [this] { show(); };
    if (statusNoActivate && area) {
        // QMdiArea activates a hidden subwindow as a side effect of show().
        // Block that adapter signal and restore the previous MDI child so the
        // original SW_SHOWNOACTIVATE path cannot change document or focus.
        const QSignalBlocker blocker(area);
        reveal();
        if (area->activeSubWindow() != previous)
            area->setActiveSubWindow(previous);
        if (previousFocus && previousFocus->isVisible()
            && previousFocus->isEnabled()) {
            previousFocus->setFocus(Qt::OtherFocusReason);
        }
    } else {
        reveal();
        if (activate && area) {
            if (area->activeSubWindow() != this
                && windowState().testFlag(Qt::WindowActive)) {
                // show() can activate QMdiSubWindow's private state while the
                // QMdiArea still has no active child. Deliver the matching Qt
                // deactivation event before asking the area to activate it.
                QEvent deactivateEvent(QEvent::WindowDeactivate);
                QCoreApplication::sendEvent(this, &deactivateEvent);
            }
            area->setActiveSubWindow(this);
            if (maximizeFirstFrame) {
                // MFC's MDIMaximize precedes its base ActivateFrame call.
                // Qt must first register/activate a plain shown subwindow or
                // it can retain WindowActive while activeSubWindow is null.
                showMaximized();
                area->setActiveSubWindow(this);
            }
            // A hidden QMdiArea can accept this call and then discard the
            // activation at the end of the same event. Check again at the
            // first Qt event boundary, but never displace a different child
            // that became active in the meantime.
            QTimer::singleShot(0, this, [this] {
                QMdiArea* currentArea = mdiArea();
                if (currentArea && !isHidden()
                    && !currentArea->activeSubWindow()) {
                    if (windowState().testFlag(Qt::WindowActive)) {
                        QEvent deactivateEvent(
                            QEvent::WindowDeactivate);
                        QCoreApplication::sendEvent(
                            this, &deactivateEvent);
                    }
                    currentArea->setActiveSubWindow(this);
                }
            });
        }
    }
    m_bPositioned = true;
}

void CChildFrame::SetDocumentTitle(const QString& title)
{
    setWindowTitle(title);
}

void CChildFrame::setVisible(bool visible)
{
    if (m_document && m_document->m_bStatusView) {
        const bool sourceVisible =
            (theApp.m_flags0 & F0_SHOWSTATUSWINDOW) != 0;
        if (visible != sourceVisible) return;
    }
    QMdiSubWindow::setVisible(visible);
}

void CChildFrame::closeEvent(QCloseEvent* event)
{
    if (m_document && m_document->m_bStatusView) {
        theApp.OnViewStatuswindow();
        event->ignore();
        return;
    }
    if (m_document && !m_document->IsCloseStarted()) {
        if (!m_document->SaveModified(this)) {
            event->ignore();
            return;
        }
        m_document->OnCloseDocument();
    }
    event->accept();
}

bool CChildFrame::event(QEvent* event)
{
    const bool result = QMdiSubWindow::event(event);
    // QEvent::WindowActivate is delivered through QWidget::event(), not
    // changeEvent(). Modern updates the global F1_MAXMDI bit from
    // OnWindowPosChanged, which also runs on MDI activation.
    if (event && event->type() == QEvent::WindowActivate) {
        UpdateMaximizedFlag();
        if (theApp.m_pMainWnd)
            theApp.m_pMainWnd->UpdateVisibilityInfo();
    }
    return result;
}

void CChildFrame::hideEvent(QHideEvent* event)
{
    QMdiSubWindow::hideEvent(event);
    if (!theApp.m_pMainWnd || !m_document) return;
    // Hiding the top-level frame also sends descendants a hide event, but
    // does not change this MDI child's own WS_VISIBLE-equivalent state.
    // The original UpdateMDITab only reacts when the child itself is hidden.
    if (!isHidden()) {
        theApp.m_pMainWnd->UpdateVisibilityInfo();
        return;
    }
    if (CTabBar* tabBar = theApp.m_pMainWnd->GetTabBar()) {
        const int tab = tabBar->FindTabNum(m_document);
        if (tab >= 0) tabBar->DelMDITab(tab);
    }
    theApp.m_pMainWnd->UpdateVisibilityInfo();
}

void CChildFrame::showEvent(QShowEvent* event)
{
    QMdiSubWindow::showEvent(event);
    if (!theApp.m_pMainWnd || !m_document) return;
    if (CTabBar* tabBar = theApp.m_pMainWnd->GetTabBar()) {
        if (!g_bFreezeTabs
            && tabBar->FindTabNum(m_document) < 0
            && (!m_document->m_bStatusView
                || (theApp.m_flags0 & F0_SHOWSTATUSWINDOW))) {
            tabBar->AddMDITab(
                m_document->GetTitle(), m_document,
                mdiArea() && mdiArea()->activeSubWindow() == this);
        }
    }
    theApp.m_pMainWnd->UpdateVisibilityInfo();
}

void CChildFrame::changeEvent(QEvent* event)
{
    QMdiSubWindow::changeEvent(event);
    if (!event || event->type() != QEvent::WindowStateChange) {
        return;
    }
    UpdateMaximizedFlag();
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->UpdateVisibilityInfo();
}

void CChildFrame::moveEvent(QMoveEvent* event)
{
    QMdiSubWindow::moveEvent(event);
    UpdateMaximizedFlag();
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->UpdateVisibilityInfo();
}

void CChildFrame::resizeEvent(QResizeEvent* event)
{
    QMdiSubWindow::resizeEvent(event);
    UpdateMaximizedFlag();
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->UpdateVisibilityInfo();
}

void CChildFrame::UpdateMaximizedFlag()
{
    if (!m_bPositioned || theApp.m_pExitingDoc
        || theApp.m_bEmbedded || !isVisible() || isMinimized()) {
        return;
    }
    if (isMaximized()) theApp.m_flags1 |= F1_MAXMDI;
    else theApp.m_flags1 &= ~DWORD(F1_MAXMDI);
}
