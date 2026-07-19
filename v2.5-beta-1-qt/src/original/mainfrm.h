// Ported from v2.5-beta-1-modern/mainfrm.h.

#pragma once

#include <QHash>
#include <QIcon>
#include <QList>
#include <QMainWindow>
#include <QString>

#include <memory>

class QAction;
class CChatDoc;
class CChatToolBar;
class CCoolToolBarEx;
class CChatView;
class CChildFrame;
class CTabBar;
class QLabel;
class QMenu;
class QMdiArea;
class QPrinter;
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
    CChatDoc* CreateNewDocument();
    CChatDoc* CreateStatusWindow();
    CChildFrame* AddDocument(CChatDoc* document, bool ownsDocument,
                             bool activate = true);
    void ActivateDocument(CChatDoc* document);
    void CloseDocument(CChatDoc* document);
    void UpdateDocumentTitle(CChatDoc* document, const QString& title);
    void ShowStatusWindow(bool show);
    void AutoArrangeWindows();
    void UpdateMacroMenu();

private:
    QAction* addCommand(QMenu* menu, const QString& text,
                        const QString& commandIdentifier);
    QAction* addToolCommand(CCoolToolBarEx* bar,
                            const QString& commandIdentifier,
                            const QIcon& icon = QIcon());
    void appendMenuItems(QMenu* menu, const QList<OriginalMenuItem>& items);
    void createMenus();
    void createAccelerators();
    void createToolBars();
    void createStatusBar();
    void updateCommandUi();
    bool commandIsPorted(const QString& commandIdentifier) const;
    void executeCommand(const QString& commandIdentifier);
    void onConnect();
    void OnMDIActivate(CChildFrame* frame);
    void TileWindows(bool vertical);
    QString NextUntitledTitle();

    CChatDoc* m_doc = nullptr;
    CChatView* m_chatView = nullptr;
    CChatDoc* m_statusDoc = nullptr;
    QMdiArea* m_mdiArea = nullptr;
    CChatToolBar* m_wndToolBar = nullptr;
    CTabBar* m_wndTabBar = nullptr;
    QLabel* m_status0 = nullptr;
    QLabel* m_status1 = nullptr;
    QHash<QString, QList<QAction*>> m_commandActions;
    QList<QMenu*> m_macroMenus;
    QList<QMenu*> m_memberMenus;
    QHash<CChatDoc*, CChildFrame*> m_childFrames;
    int m_nextUntitled = 1;
    bool m_destroying = false;
    std::unique_ptr<QPrinter> m_printer;
};
