// Ported from v2.5-beta-1-modern/chatbars.h. CChatToolBar remains the single
// coolbar manager; its three CCoolToolBarEx children are Qt toolbars.

#pragma once

#include "coolbar.h"

#include <QIcon>
#include <QPoint>

#include <functional>

class QAction;
class QMainWindow;
class QMenu;
class QWidget;

class CChatToolBar : public CCoolBarEx {
public:
    using ActionFactory = std::function<QAction*(
        CCoolToolBarEx*, const QString&, const QIcon&)>;
    using CommandInvoker = std::function<void(const QString&)>;
    using FavoritesMenuProvider = std::function<QMenu*()>;

    explicit CChatToolBar(QObject* parent = nullptr);
    BOOL Create(QMainWindow* parentWindow, BOOL doCB32,
                ActionFactory actionFactory,
                CommandInvoker commandInvoker,
                FavoritesMenuProvider favoritesMenuProvider);
    void ToggleBar(UINT which);
    void OnContextMenu(CCoolToolBarEx* toolbar, const QPoint& point);
    QMenu* CreateContextMenu(QWidget* parent = nullptr) const;

protected:
    CCoolToolBarEx* CreateToolBar(UINT id) override;
    void OnPrepareToolBar(UINT id, CCoolToolBarEx* toolbar) override;

private:
    ActionFactory m_actionFactory;
    CommandInvoker m_commandInvoker;
    FavoritesMenuProvider m_favoritesMenuProvider;
};
