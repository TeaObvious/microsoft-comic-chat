// Ported from v2.5-beta-1-modern/childfrm.cpp.

#include "childfrm.h"

#include "chat.h"
#include "chatdoc.h"
#include "chatview.h"
#include "ircproto.h"
#include "mainfrm.h"

#include <QCloseEvent>
#include <QMdiArea>

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
    if (m_ownsDocument && m_document && !m_document->m_bStatusView
        && m_document->m_proto) {
        m_document->m_proto->ChatPartChannel(m_document, false);
    }
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

void CChildFrame::ActivateFrame()
{
    if (QMdiArea* area = mdiArea()) area->setActiveSubWindow(this);
    show();
    if (!m_bPositioned && (theApp.m_flags1 & F1_MAXMDI)) showMaximized();
    m_bPositioned = true;
}

void CChildFrame::SetDocumentTitle(const QString& title)
{
    setWindowTitle(title);
}

void CChildFrame::closeEvent(QCloseEvent* event)
{
    if (m_document && m_document->m_bStatusView) {
        if (theApp.m_pMainWnd) theApp.m_pMainWnd->ShowStatusWindow(false);
        event->ignore();
        return;
    }
    if (m_document && m_document->m_proto) {
        m_document->m_proto->ChatPartChannel(m_document, false);
    }
    event->accept();
}
