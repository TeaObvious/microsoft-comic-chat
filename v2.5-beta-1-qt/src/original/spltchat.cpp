// Ported from v2.5-beta-1-modern/spltchat.cpp.

#include "spltchat.h"

#include "chat.h"
#include "chatdoc.h"
#include "saywnd.h"

#include <QApplication>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScopedValueRollback>

#include <algorithm>

namespace {
constexpr int NPIXELSSAYMIN = 23;

int availableLength(const QSplitter* splitter)
{
    const int fullLength = splitter->orientation() == Qt::Horizontal
        ? splitter->width() : splitter->height();
    return std::max(0, fullLength - std::max(0, splitter->count() - 1)
        * splitter->handleWidth());
}

bool ForwardToSayWnd(QKeyEvent* event)
{
    static thread_local bool forwarding = false;
    if (forwarding) {
        event->accept();
        return true;
    }
    if (event->text().isEmpty()) return false;

    CChatDoc* document = GetChatDoc();
    auto* sayWindow = document
        ? dynamic_cast<CSayWnd*>(document->m_sayWnd) : nullptr;
    CSayCtrl* sayControl = sayWindow ? sayWindow->GetSayEdit() : nullptr;
    if (!sayControl) return false;

    QScopedValueRollback<bool> guard(forwarding, true);
    QKeyEvent forwarded(QEvent::KeyPress, event->key(), event->modifiers(),
                        event->nativeScanCode(), event->nativeVirtualKey(),
                        event->nativeModifiers(), event->text(),
                        event->isAutoRepeat(), event->count());
    QApplication::sendEvent(sayControl, &forwarded);
    sayWindow->SetFocusToSayWnd();
    event->accept();
    return true;
}
}

CSplitChat::CSplitChat(QWidget* parent)
    : QSplitter(Qt::Vertical, parent)
{
    connect(this, &QSplitter::splitterMoved, this, [this](int, int) {
        if (m_bApplyingSizes || count() != 2) return;
        const QList<int> paneSizes = sizes();
        const int total = paneSizes.value(0) + paneSizes.value(1);
        if (total > 0) m_nPctBottom = paneSizes.value(1) * 100 / total;
    });
}

void CSplitChat::applyOriginalSizes()
{
    if (count() != 2) return;
    const int total = availableLength(this);
    const int top = total * (100 - m_nPctBottom) / 100;
    m_bApplyingSizes = true;
    setSizes({top, total - top});
    m_bApplyingSizes = false;
}

void CSplitChat::resizeEvent(QResizeEvent* event)
{
    QSplitter::resizeEvent(event);
    applyOriginalSizes();
}

void CSplitChat::keyPressEvent(QKeyEvent* event)
{
    if (!ForwardToSayWnd(event)) QSplitter::keyPressEvent(event);
}

CSplitChatV::CSplitChatV(QWidget* parent)
    : QSplitter(Qt::Horizontal, parent)
    , m_nPctLeft(theApp.m_bShowMode ? 100 : 80)
{
    connect(this, &QSplitter::splitterMoved, this, [this](int, int) {
        if (m_bApplyingSizes || count() != 2) return;
        const QList<int> paneSizes = sizes();
        const int total = paneSizes.value(0) + paneSizes.value(1);
        if (total > 0) m_nPctLeft = paneSizes.value(0) * 100 / total;
    });
}

void CSplitChatV::applyOriginalSizes()
{
    if (count() != 2) return;
    const int total = availableLength(this);
    const int left = total * m_nPctLeft / 100;
    m_bApplyingSizes = true;
    setSizes({left, total - left});
    m_bApplyingSizes = false;
}

void CSplitChatV::resizeEvent(QResizeEvent* event)
{
    QSplitter::resizeEvent(event);
    applyOriginalSizes();
}

void CSplitChatV::keyPressEvent(QKeyEvent* event)
{
    if (!ForwardToSayWnd(event)) QSplitter::keyPressEvent(event);
}

CSplitSay::CSplitSay(QWidget* parent)
    : QSplitter(Qt::Vertical, parent)
{
    connect(this, &QSplitter::splitterMoved, this, [this](int, int) {
        if (m_bApplyingSizes || count() != 2) return;
        const QList<int> paneSizes = sizes();
        m_nPixelsClient = paneSizes.value(0);
        m_nPixelsSay = std::max(paneSizes.value(1), SayMinimumPixels());
    });
}

int CSplitSay::SayMinimumPixels() const
{
    if (theApp.m_bShowMode) return 0;
    // Modern deliberately runs DPI-unaware, so DpiScale(23) is exactly 23.
    // Qt widget coordinates are already logical pixels; applying logicalDpiY
    // again would double-scale this one splitter relative to the rest.
    return NPIXELSSAYMIN;
}

void CSplitSay::applyOriginalSizes(bool initialSizing)
{
    if (count() != 2) return;
    const int total = availableLength(this);
    m_nPixelsSayMin = SayMinimumPixels();
    if (initialSizing || m_nPixelsSay == 0) {
        m_nPixelsSay = m_nPixelsSayMin;
    }
    m_nPixelsSay = std::max(m_nPixelsSay, m_nPixelsSayMin);
    m_nPixelsSay = std::min(m_nPixelsSay, total);
    m_nPixelsClient = total - m_nPixelsSay;
    m_bApplyingSizes = true;
    setSizes({m_nPixelsClient, m_nPixelsSay});
    m_bApplyingSizes = false;
}

void CSplitSay::resizeEvent(QResizeEvent* event)
{
    QSplitter::resizeEvent(event);
    applyOriginalSizes(!m_bInitialSizingDone);
    m_bInitialSizingDone = count() == 2;
}

void CSplitSay::keyPressEvent(QKeyEvent* event)
{
    if (!ForwardToSayWnd(event)) QSplitter::keyPressEvent(event);
}
