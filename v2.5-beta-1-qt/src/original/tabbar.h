// Ported from v2.5-beta-1-modern/tabbar.h.

#pragma once

#include <QTabBar>
#include <QToolBar>

class CChatDoc;
class QKeyEvent;

class CTabBarTabCtrl : public QTabBar {
public:
    explicit CTabBarTabCtrl(QWidget* parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

class CTabBar : public QToolBar {
public:
    explicit CTabBar(QWidget* parent = nullptr);

    void AddMDITab(const QString& channelName, CChatDoc* doc, bool selectIt = true);
    void DelMDITab(int tab);
    QString GetTabString(int tab) const;
    int FindTabNum(CChatDoc* doc) const;
    CChatDoc* GetTabDoc(int index) const;
    void SetTabIcon(int tabNum, int icon);
    CTabBarTabCtrl* TabControl() const { return m_tabCtrl; }

private:
    QIcon originalTabIcon(int icon) const;

    CTabBarTabCtrl* m_tabCtrl = nullptr;
    QList<CChatDoc*> m_docs;
    QList<QIcon> m_images;
    long m_lLargestTab = 64L;
};
