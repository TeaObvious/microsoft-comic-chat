// Ported from v2.5-beta-1-modern/mainfrm.h.

#pragma once

#include <QHash>
#include <QIcon>
#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QString>

#include <memory>

class QAction;
class CChatDoc;
class CChatToolBar;
class CCoolToolBarEx;
class CChatView;
class CChildFrame;
class CTabBar;
class QCloseEvent;
class QLabel;
class QEvent;
class QMenu;
class QMdiArea;
class QPrinter;
class QResizeEvent;
class QShortcut;
class QWidget;
struct OriginalMenuItem;

class CMainFrame : public QMainWindow {
public:
    explicit CMainFrame(CChatDoc* doc = nullptr,
                        bool ownsDocument = false,
                        QWidget* parent = nullptr);
    ~CMainFrame() override;

    void SetStatusPaneString(int pane, const QString& text);
    CChatToolBar* GetToolBar() const { return m_wndToolBar; }
    CChatView* GetChatView() const { return m_chatView; }
    CTabBar* GetTabBar() const { return m_wndTabBar; }
    QMdiArea* GetMDIArea() const { return m_mdiArea; }
    CChatDoc* GetActiveDocument() const { return m_doc; }
    QPrinter* GetPrinter() const { return m_printer.get(); }
    CChatDoc* CreateNewDocument();
    CChatDoc* CreateStatusWindow();
    CChildFrame* AddDocument(CChatDoc* document, bool ownsDocument,
                             bool activate = true);
    void ActivateDocument(CChatDoc* document);
    bool CloseDocument(CChatDoc* document);
    void UpdateDocumentTitle(CChatDoc* document, const QString& title);
    void ShowStatusWindow(bool show, bool activate = true);
    void AutoArrangeWindows();
    void UpdateMacroMenu();
    void UpdateAdminMenu(CChatDoc* document);
    QAction* InsertDynamicCommand(QMenu* menu, QAction* before,
                                  const QString& text,
                                  const QString& commandIdentifier);
    void RemoveDynamicCommand(QMenu* menu, QAction* action);
    bool IsRegisteredMemberMenu(QMenu* menu) const;
    void ConfigureCommandMenu(QMenu* menu);
    void ConfigureContextMenu(QMenu* menu);
    QWidget* GetCommandFocusWidget() const;
    void RefreshCommandUi();
    void UpdateVisibilityInfo();

private:
    enum class CommandClass {
        Active,
        Deferred,
        NoHandler,
        Unresolved
    };

    QAction* addCommand(QMenu* menu, const QString& text,
                        const QString& commandIdentifier);
    QAction* addToolCommand(CCoolToolBarEx* bar,
                            const QString& commandIdentifier,
                            const QIcon& icon = QIcon());
    void configureCommandAction(QAction* action,
                                const QString& commandIdentifier);
    void appendMenuItems(QMenu* menu, const QList<OriginalMenuItem>& items);
    void unregisterMenuActions(QMenu* menu);
    void createMenus();
    void createAccelerators();
    void createToolBars();
    void createStatusBar();
    void updateWindowMenu();
    void updateCommandUi();
    void scheduleCommandUiUpdate();
    CommandClass commandClass(const QString& commandIdentifier) const;
    void executeCommand(const QString& commandIdentifier);
    void OnMDIActivate(CChildFrame* frame);
    void TileWindows(bool vertical);
    void ArrangeIcons();
    QString NextUntitledTitle();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    CChatDoc* m_doc = nullptr;
    CChatView* m_chatView = nullptr;
    CChatDoc* m_statusDoc = nullptr;
    QMdiArea* m_mdiArea = nullptr;
    CChatToolBar* m_wndToolBar = nullptr;
    CTabBar* m_wndTabBar = nullptr;
    QLabel* m_status0 = nullptr;
    QLabel* m_status1 = nullptr;
    QHash<QString, QList<QPointer<QAction>>> m_commandActions;
    QHash<QString, QList<QPointer<QShortcut>>> m_commandShortcuts;
    QList<QMenu*> m_macroMenus;
    QList<QMenu*> m_memberMenus;
    QList<QPointer<QAction>> m_memberListPopupActions;
    QPointer<QMenu> m_windowMenu;
    QList<QPointer<QAction>> m_windowDocumentActions;
    QPointer<QAction> m_windowDocumentSeparator;
    QHash<QMenu*, QAction*> m_adminMenuActions;
    QHash<QMenu*, QAction*> m_adminSeparators;
    QHash<CChatDoc*, CChildFrame*> m_childFrames;
    QPointer<QWidget> m_commandFocusWidget;
    QString m_statusPaneStrings[2];
    int m_nextUntitled = 1;
    bool m_destroying = false;
    bool m_closingAccepted = false;
    bool m_uiUpdatePending = false;
    std::unique_ptr<QPrinter> m_printer;
};
