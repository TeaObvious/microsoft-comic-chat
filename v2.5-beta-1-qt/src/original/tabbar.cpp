// Ported from v2.5-beta-1-modern/tabbar.cpp.

#include "tabbar.h"

#include "chatdoc.h"
#include "mainfrm.h"
#include "memblst.h"
#include "originalassets.h"

#include <QBitmap>
#include <QFont>
#include <QKeyEvent>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

CTabBarTabCtrl::CTabBarTabCtrl(QWidget* parent)
    : QTabBar(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

void CTabBarTabCtrl::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        if (CChatDoc* document = GetChatDoc()) {
            const BOOL backward = event->key() == Qt::Key_Backtab
                || (event->modifiers() & Qt::ShiftModifier);
            document->CycleFocus(CHATFOCUS_TABBAR, backward);
        }
        event->accept();
        return;
    }
    if (!event->text().isEmpty()) {
        ForwardToSayWnd(event->text().front().unicode());
        event->accept();
        return;
    }
    QTabBar::keyPressEvent(event);
}

CTabBar::CTabBar(QWidget* parent)
    : QToolBar(parent)
    , m_tabCtrl(new CTabBarTabCtrl(this))
{
    setMovable(true);
    setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
    setWindowTitle(originalResourceString(QStringLiteral("IDS_TABTITLE")));
    setFixedHeight(29);

    QFont tabFont = font();
    tabFont.setWeight(static_cast<QFont::Weight>(originalResourceString(
        QStringLiteral("IDS_ROOMTAB_FONTWEIGHT")).toInt()));
    m_tabCtrl->setFont(tabFont);
    m_tabCtrl->setExpanding(false);
    m_tabCtrl->setDocumentMode(false);
    m_tabCtrl->setUsesScrollButtons(true);

    const QPixmap strip(originalFileResourcePath(QStringLiteral("IDB_TABS"),
                                                 QStringLiteral("BITMAP")));
    for (int index = 0; index < 4 && !strip.isNull(); ++index) {
        QPixmap image = strip.copy(index * 16, 0, 16, strip.height());
        image.setMask(image.createMaskFromColor(QColor(0, 255, 0), Qt::MaskInColor));
        m_images.append(QIcon(image));
    }

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 5, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabCtrl);
    addWidget(container);
    m_tabCtrl->hide();

    connect(m_tabCtrl, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0 || index >= m_docs.size()) return;
        CChatDoc* document = m_docs[index];
        if (!document) return;
        if (auto* frame = dynamic_cast<CMainFrame*>(window()))
            frame->ActivateDocument(document);
        else
            SetChatDoc(document);
    });
}

QIcon CTabBar::originalTabIcon(int icon) const
{
    return icon >= 0 && icon < m_images.size() ? m_images[icon] : QIcon();
}

void CTabBar::AddMDITab(const QString& channelName, CChatDoc* doc, bool selectIt)
{
    const int tabCount = m_docs.size();
    if (tabCount == 0) m_tabCtrl->show();

    int place = 0;
    if (doc && !doc->m_bStatusView) {
        while (place < tabCount) {
            CChatDoc* otherDocument = m_docs[place];
            if (channelName.compare(GetTabString(place), Qt::CaseInsensitive) < 0
                && otherDocument && !otherDocument->m_bStatusView) {
                break;
            }
            ++place;
        }
    }

    m_tabCtrl->insertTab(place, originalTabIcon(doc && doc->m_bStatusView ? 2 : 0),
                         channelName);
    m_docs.insert(place, doc);
    if (selectIt) m_tabCtrl->setCurrentIndex(place);
    m_lLargestTab = std::max<long>(m_lLargestTab, m_tabCtrl->tabRect(place).width());
    if (doc) doc->m_bNewContent = false;
}

void CTabBar::DelMDITab(int tab)
{
    if (tab < 0 || tab >= m_docs.size()) return;
    const int removedWidth = m_tabCtrl->tabRect(tab).width();
    m_tabCtrl->removeTab(tab);
    m_docs.removeAt(tab);
    if (m_docs.isEmpty()) {
        m_tabCtrl->hide();
        m_lLargestTab = 64L;
    } else if (removedWidth == m_lLargestTab) {
        m_lLargestTab = 64L;
        for (int index = 0; index < m_docs.size(); ++index)
            m_lLargestTab = std::max<long>(m_lLargestTab, m_tabCtrl->tabRect(index).width());
    }
}

QString CTabBar::GetTabString(int tab) const
{
    return tab >= 0 && tab < m_tabCtrl->count() ? m_tabCtrl->tabText(tab) : QString();
}

int CTabBar::FindTabNum(CChatDoc* doc) const
{
    for (int index = 0; index < m_docs.size(); ++index)
        if (m_docs[index] == doc) return index;
    return -1;
}

CChatDoc* CTabBar::GetTabDoc(int index) const
{
    return index >= 0 && index < m_docs.size() ? m_docs[index] : nullptr;
}

void CTabBar::SetTabIcon(int tabNum, int icon)
{
    if (tabNum >= 0 && tabNum < m_tabCtrl->count())
        m_tabCtrl->setTabIcon(tabNum, originalTabIcon(icon));
}
