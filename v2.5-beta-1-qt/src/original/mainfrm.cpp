// Ported from v2.5-beta-1-modern/mainfrm.cpp.

#include "mainfrm.h"

#include "chat.h"
#include "chatbars.h"
#include "childfrm.h"
#include "chatdoc.h"
#include "chatview.h"
#include "ircproto.h"
#include "pageview.h"
#include "protsupp.h"
#include "saywnd.h"
#include "setupdlg.h"
#include "status.h"
#include "tabbar.h"
#include "utils.h"
#include "originalassets.h"
#include "userinfo.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QResizeEvent>
#include <QSet>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QtPrintSupport/QPrinter>

#include <algorithm>
#include <utility>

namespace {
QString commandStatusText(const QString& commandIdentifier)
{
    QString resourceIdentifier = commandIdentifier;
    if (commandIdentifier.startsWith(QLatin1String("ID_MACRO_A"))
        && commandIdentifier != QLatin1String("ID_MACRO_A0")) {
        resourceIdentifier = QStringLiteral("ID_MACRO_A0");
    }
    return originalResourceString(resourceIdentifier).section(
        QLatin1Char('\n'), 0, 0);
}

QString commandToolTip(const QString& commandIdentifier)
{
    return originalResourceString(commandIdentifier).section(QLatin1Char('\n'), 1, 1);
}

bool isCheckCommand(const QString& commandIdentifier)
{
    static const QStringList commands = {
        QStringLiteral("ID_VIEW_TOOLBAR_MAIN"),
        QStringLiteral("ID_VIEW_TOOLBAR_MEMBER"),
        QStringLiteral("ID_VIEW_TOOLBAR_TEXT"),
        QStringLiteral("ID_VIEW_TABBAR"),
        QStringLiteral("ID_VIEW_STATUS_BAR"),
        QStringLiteral("ID_VIEW_STATUSWINDOW"),
        QStringLiteral("ID_VIEW_LOGINNOTIFS"),
        QStringLiteral("ID_WINDOW_TILE_AUTO"),
        QStringLiteral("ID_VIEW_COMICS"),
        QStringLiteral("ID_VIEW_TEXT"),
        QStringLiteral("ID_VIEW_LIST"),
        QStringLiteral("ID_VIEW_ICON"),
        QStringLiteral("ID_MEMBER_IGNORE"),
        QStringLiteral("ID_MAKEADMIN"),
        QStringLiteral("ID_MAKESPEAKER"),
        QStringLiteral("ID_MAKESPECTATOR"),
        QStringLiteral("ID_AWAY_TOGGLE"),
        QStringLiteral("ID_SWITCHBOLD"),
        QStringLiteral("ID_SWITCHITALIC"),
        QStringLiteral("ID_SWITCHUNDERLINED"),
        QStringLiteral("ID_SWITCHFIXEDPITCH"),
        QStringLiteral("ID_SWITCHSYMBOL")
    };
    return commands.contains(commandIdentifier);
}

QKeySequence acceleratorSequence(const OriginalAccelerator& accelerator)
{
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    if (accelerator.alt) modifiers |= Qt::AltModifier;
    if (accelerator.control) modifiers |= Qt::ControlModifier;
    if (accelerator.shift) modifiers |= Qt::ShiftModifier;

    int key = 0;
    if (accelerator.key.size() == 1) {
        const QChar character = accelerator.key.front().toUpper();
        if (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
            key = Qt::Key_A + character.unicode() - QLatin1Char('A').unicode();
        else if (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
            key = Qt::Key_0 + character.unicode() - QLatin1Char('0').unicode();
        else
            key = character.unicode();
    } else if (accelerator.key == QLatin1String("VK_BACK")) {
        key = Qt::Key_Backspace;
    } else if (accelerator.key == QLatin1String("VK_DELETE")) {
        key = Qt::Key_Delete;
    } else if (accelerator.key == QLatin1String("VK_INSERT")) {
        key = Qt::Key_Insert;
    }
    return key ? QKeySequence(QKeyCombination(modifiers, Qt::Key(key))) : QKeySequence();
}

void setActionsEnabled(
    const QHash<QString, QList<QPointer<QAction>>>& actions,
                       const QString& commandIdentifier, bool enabled)
{
    for (const QPointer<QAction>& action :
         actions.value(commandIdentifier)) {
        if (action) action->setEnabled(enabled);
    }
}

void setActionsChecked(
    const QHash<QString, QList<QPointer<QAction>>>& actions,
                       const QString& commandIdentifier, bool checked)
{
    for (const QPointer<QAction>& action :
         actions.value(commandIdentifier)) {
        if (!action) continue;
        action->setCheckable(true);
        action->setChecked(checked);
    }
}

bool isMenuFocusWidget(QWidget* widget)
{
    for (QWidget* current = widget; current;
         current = current->parentWidget()) {
        if (qobject_cast<QMenu*>(current)
            || qobject_cast<QMenuBar*>(current)) {
            return true;
        }
    }
    return false;
}

QString quoteMenuAmpersands(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&&"));
    return text;
}

void makeExclusiveMenuGroup(QMenu* menu,
                            const QStringList& commandIdentifiers)
{
    if (!menu) return;
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);
    for (QAction* action : menu->actions()) {
        if (commandIdentifiers.contains(action->data().toString()))
            group->addAction(action);
    }
    if (group->actions().size() < 2) delete group;
}
}

CMainFrame::CMainFrame(CChatDoc* doc, bool ownsDocument, QWidget* parent)
    : QMainWindow(parent)
    , m_mdiArea(new QMdiArea(this))
    , m_wndTabBar(new CTabBar(this))
    , m_printer(std::make_unique<QPrinter>(QPrinter::HighResolution))
{
    m_printer->setFullPage(false);
    setWindowTitle(originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")));
    setWindowIcon(QIcon(originalFileResourcePath(QStringLiteral("IDR_MAINFRAME"),
                                                 QStringLiteral("ICON"))));
    if (theApp.m_cxFrame && theApp.m_cyFrame) {
        QRect frameRect(theApp.m_xFrame, theApp.m_yFrame,
                        theApp.m_cxFrame, theApp.m_cyFrame);
        MakeRectVisibleOnScreen(&frameRect);
        setGeometry(frameRect);
    }
    m_mdiArea->setViewMode(QMdiArea::SubWindowView);
    setCentralWidget(m_mdiArea);
    qApp->installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this,
            [this](QWidget* old, QWidget* now) {
                if (now && !isMenuFocusWidget(now))
                    m_commandFocusWidget = now;
                else if (!m_commandFocusWidget && old
                         && !isMenuFocusWidget(old))
                    m_commandFocusWidget = old;
                scheduleCommandUiUpdate();
            });
    if (QClipboard* clipboard = QApplication::clipboard()) {
        connect(clipboard, &QClipboard::dataChanged, this,
                &CMainFrame::scheduleCommandUiUpdate);
    }
    connect(m_mdiArea, &QMdiArea::subWindowActivated, this,
            [this](QMdiSubWindow* window) {
                OnMDIActivate(dynamic_cast<CChildFrame*>(window));
            });
    createMenus();
    UpdateMacroMenu();
    createAccelerators();
    createToolBars();
    createStatusBar();
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, m_wndTabBar);
    m_wndTabBar->setVisible((theApp.m_flags1 & F1_SHOWTABBAR) != 0);
    if (doc) AddDocument(doc, ownsDocument, true);
    updateCommandUi();
}

CMainFrame::~CMainFrame()
{
    m_destroying = true;
    qApp->removeEventFilter(this);
    if (m_wndToolBar)
        m_wndToolBar->SaveStateToBuffer(&theApp.m_pbCoolBarState);
    const QList<CChildFrame*> frames = m_childFrames.values();
    m_childFrames.clear();
    for (CChildFrame* frame : frames) delete frame;
}

QString CMainFrame::NextUntitledTitle()
{
    const QString base = originalResourceString(
        QStringLiteral("IDR_MAINFRAME")).section(QLatin1Char('\n'), 1, 1);
    return base + QString::number(m_nextUntitled++);
}

CChildFrame* CMainFrame::AddDocument(CChatDoc* document, bool ownsDocument,
                                     bool activate)
{
    if (!document) return nullptr;
    if (CChildFrame* existing = m_childFrames.value(document)) {
        if (activate) existing->ActivateFrame();
        return existing;
    }

    // MFC calls CChatDoc::OnNewDocument/InitMyDocument before it creates the
    // child view and supplies that document through CCreateContext.  Keep the
    // same ordering and context while the Qt child constructs its controls.
    CChatDoc* previousDocument = GetChatDoc();
    SetChatDoc(document);
    auto* frame = new CChildFrame(document, ownsDocument, m_mdiArea);
    if (previousDocument != document) SetChatDoc(previousDocument);
    m_mdiArea->addSubWindow(frame);
    m_childFrames.insert(document, frame);
    connect(frame, &QObject::destroyed, this,
            [this, document, frame] {
                const int tab = m_wndTabBar
                    ? m_wndTabBar->FindTabNum(document) : -1;
                if (tab >= 0) m_wndTabBar->DelMDITab(tab);
                if (m_childFrames.value(document) == frame)
                    m_childFrames.remove(document);
                if (m_statusDoc == document) m_statusDoc = nullptr;
                if (!m_destroying && m_doc == document) {
                    OnMDIActivate(dynamic_cast<CChildFrame*>(
                        m_mdiArea->activeSubWindow()));
                }
                AutoArrangeWindows();
            });

    QString title = document->GetTitle();
    if (title.isEmpty()) {
        title = document->m_bStatusView
            ? originalResourceString(QStringLiteral("IDS_STATUSTITLE"))
            : NextUntitledTitle();
    }
    document->SetTitle(title);
    if (theApp.m_pMainWnd != this)
        UpdateDocumentTitle(document, title);

    const bool visible = !document->m_bStatusView
        || (theApp.m_flags0 & F0_SHOWSTATUSWINDOW);
    if (visible) {
        frame->ActivateFrame(activate);
    } else {
        frame->hide();
    }
    AutoArrangeWindows();
    UpdateVisibilityInfo();
    return frame;
}

CChatDoc* CMainFrame::CreateNewDocument()
{
    auto* document = new CChatDoc;
    AddDocument(document, true, true);
    return document;
}

CChatDoc* CMainFrame::CreateStatusWindow()
{
    if (m_statusDoc) return m_statusDoc;
    auto* document = new CChatDoc;
    document->m_bComicView = false;
    document->m_bStatusView = true;
    if (document->m_proto)
        document->m_proto->m_strChannel = QString::fromLatin1(STATUS_WINDOW_NAME);
    m_statusDoc = document;
    AddDocument(document, true,
                (theApp.m_flags0 & F0_SHOWSTATUSWINDOW) != 0);
    return document;
}

void CMainFrame::ActivateDocument(CChatDoc* document)
{
    if (CChildFrame* frame = m_childFrames.value(document))
        frame->ActivateFrame();
}

void CMainFrame::CloseDocument(CChatDoc* document)
{
    if (CChildFrame* frame = m_childFrames.value(document)) frame->close();
}

void CMainFrame::OnMDIActivate(CChildFrame* frame)
{
    if (!frame) {
        setWindowTitle(originalResourceString(
            QStringLiteral("AFX_IDS_APP_TITLE")));
        m_doc = nullptr;
        m_chatView = nullptr;
        SetChatDoc(nullptr);
        UpdateAdminMenu(nullptr);
        SetStatusPaneString(1, QString());
        if (CRoomInfo* protocol = GetDefaultProto())
            protocol->UpdateStatus();
        updateCommandUi();
        UpdateVisibilityInfo();
        return;
    }
    m_doc = frame->GetDocument();
    m_chatView = frame->GetChatView();
    if (m_doc) {
        setWindowTitle(
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE"))
            + QStringLiteral(" - [") + m_doc->GetTitle()
            + QLatin1Char(']'));
    }
    SetChatDoc(m_doc);
    SetStatusPaneString(1, QString());
    if (m_doc) {
        m_doc->LoadDocData();
        m_doc->UpdateAdminMenu();
        m_doc->UpdateComicCharacterMenu();
        m_doc->ResetStatus(true, true);
        if (currentRoom) currentRoom->UpdateStatus();
        const int tab = m_wndTabBar->FindTabNum(m_doc);
        if (tab >= 0 && m_wndTabBar->TabControl()->currentIndex() != tab)
            m_wndTabBar->TabControl()->setCurrentIndex(tab);
        m_doc->SetFocusToSayWnd();
    }
    updateCommandUi();
    UpdateVisibilityInfo();
}

void CMainFrame::UpdateDocumentTitle(CChatDoc* document,
                                     const QString& title)
{
    if (!document) return;
    if (CChildFrame* frame = m_childFrames.value(document))
        frame->SetDocumentTitle(title);
    if (document == m_doc) {
        setWindowTitle(
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE"))
            + QStringLiteral(" - [") + title + QLatin1Char(']'));
    }
    if (g_bFreezeTabs) return;
    const int tab = m_wndTabBar->FindTabNum(document);
    if (tab >= 0) m_wndTabBar->DelMDITab(tab);
    if (!document->m_bStatusView
        || (theApp.m_flags0 & F0_SHOWSTATUSWINDOW)) {
        m_wndTabBar->AddMDITab(title, document, document == m_doc);
    }
}

void CMainFrame::ShowStatusWindow(bool show, bool activate)
{
    if (!m_statusDoc) return;
    CChildFrame* frame = m_childFrames.value(m_statusDoc);
    if (!frame) return;
    if (show) {
        theApp.m_flags0 |= F0_SHOWSTATUSWINDOW;
        if (m_wndTabBar->FindTabNum(m_statusDoc) < 0)
            m_wndTabBar->AddMDITab(
                m_statusDoc->GetTitle(), m_statusDoc, activate);
        frame->ActivateFrame(activate);
    } else {
        const bool statusWasActive =
            m_mdiArea->activeSubWindow() == frame;
        theApp.m_flags0 &= ~DWORD(F0_SHOWSTATUSWINDOW);
        if (statusWasActive) {
            const QList<QMdiSubWindow*> history =
                m_mdiArea->subWindowList(
                    QMdiArea::ActivationHistoryOrder);
            bool activatedSuccessor = false;
            for (auto iterator = history.crbegin();
                 iterator != history.crend(); ++iterator) {
                QMdiSubWindow* candidate = *iterator;
                if (candidate != frame && !candidate->isHidden()) {
                    m_mdiArea->setActiveSubWindow(candidate);
                    activatedSuccessor = true;
                    break;
                }
            }
            if (!activatedSuccessor) {
                // MDI has no MDINext target. Hide first, then explicitly
                // clear Qt's active pointer; a future child clears any
                // addSubWindow WindowActive artifact before it is revealed.
                frame->hide();
                m_mdiArea->setActiveSubWindow(nullptr);
            }
        }
        const int tab = m_wndTabBar->FindTabNum(m_statusDoc);
        if (tab >= 0) m_wndTabBar->DelMDITab(tab);
        if (!frame->isHidden()) frame->hide();
    }
    AutoArrangeWindows();
    UpdateVisibilityInfo();
    updateCommandUi();
}

void CMainFrame::TileWindows(bool vertical)
{
    QList<QMdiSubWindow*> windows;
    for (QMdiSubWindow* window : m_mdiArea->subWindowList()) {
        if (!window->isHidden() && !window->isMinimized()) windows.append(window);
    }
    if (windows.isEmpty()) return;
    const QRect area = m_mdiArea->viewport()->rect();
    for (int index = 0; index < windows.size(); ++index) {
        if (vertical) {
            const int left = area.left() + area.width() * index / windows.size();
            const int right = area.left() + area.width() * (index + 1) / windows.size();
            windows[index]->showNormal();
            windows[index]->setGeometry(left, area.top(), right - left,
                                        area.height());
        } else {
            const int top = area.top() + area.height() * index / windows.size();
            const int bottom = area.top() + area.height() * (index + 1) / windows.size();
            windows[index]->showNormal();
            windows[index]->setGeometry(area.left(), top, area.width(),
                                        bottom - top);
        }
    }
}

void CMainFrame::AutoArrangeWindows()
{
    if (!(theApp.m_flags0 & F0_AUTOARRANGEWNDS) || !m_mdiArea) return;
    const bool vertical =
        (theApp.m_flags0 & F0_AUTOARRANGEISVERT) != 0;
    QTimer::singleShot(0, this, [this, vertical] {
        if (!m_destroying && m_mdiArea) TileWindows(vertical);
    });
}

QAction* CMainFrame::addCommand(QMenu* menu, const QString& text,
                                const QString& commandIdentifier)
{
    QAction* action = menu->addAction(text);
    configureCommandAction(action, commandIdentifier);
    return action;
}

QAction* CMainFrame::addToolCommand(CCoolToolBarEx* bar,
                                    const QString& commandIdentifier,
                                    const QIcon& icon)
{
    QAction* action = bar->addAction(icon, commandToolTip(commandIdentifier));
    action->setToolTip(commandToolTip(commandIdentifier));
    configureCommandAction(action, commandIdentifier);
    return action;
}

void CMainFrame::configureCommandAction(
    QAction* action, const QString& commandIdentifier)
{
    if (!action) return;
    action->setData(commandIdentifier);
    action->setStatusTip(commandStatusText(commandIdentifier));
    action->setCheckable(isCheckCommand(commandIdentifier));
    QString classification;
    switch (commandClass(commandIdentifier)) {
    case CommandClass::Active:
        classification = QStringLiteral("active");
        break;
    case CommandClass::Deferred:
        classification = QStringLiteral("deferred");
        break;
    case CommandClass::NoHandler:
        classification = QStringLiteral("no-handler");
        break;
    case CommandClass::Unresolved:
        classification = QStringLiteral("unresolved");
        break;
    }
    action->setProperty("originalCommandClass", classification);
    connect(action, &QAction::triggered, this, [this, commandIdentifier] {
        executeCommand(commandIdentifier);
    });
    connect(action, &QAction::hovered, this, [this, action] {
        if (m_status0) m_status0->setText(action->statusTip());
    });
    m_commandActions[commandIdentifier].append(QPointer<QAction>(action));
}

QAction* CMainFrame::InsertDynamicCommand(
    QMenu* menu, QAction* before, const QString& text,
    const QString& commandIdentifier)
{
    if (!menu) return nullptr;
    auto* action = new QAction(text, menu);
    configureCommandAction(action, commandIdentifier);
    menu->insertAction(before, action);
    return action;
}

void CMainFrame::RemoveDynamicCommand(QMenu* menu, QAction* action)
{
    if (!action) return;
    const QString commandIdentifier = action->data().toString();
    if (!commandIdentifier.isEmpty()) {
        m_commandActions[commandIdentifier].removeAll(
            QPointer<QAction>(action));
        if (m_commandActions[commandIdentifier].isEmpty())
            m_commandActions.remove(commandIdentifier);
    }
    if (menu) menu->removeAction(action);
    delete action;
}

void CMainFrame::unregisterMenuActions(QMenu* menu)
{
    if (!menu) return;
    const QList<QAction*> actions = menu->actions();
    for (QAction* action : actions) {
        if (action->menu()) unregisterMenuActions(action->menu());
        const QString commandIdentifier = action->data().toString();
        if (commandIdentifier.isEmpty()) continue;
        m_commandActions[commandIdentifier].removeAll(
            QPointer<QAction>(action));
        if (m_commandActions[commandIdentifier].isEmpty())
            m_commandActions.remove(commandIdentifier);
    }
}

bool CMainFrame::IsRegisteredMemberMenu(QMenu* menu) const
{
    return menu && m_memberMenus.contains(menu);
}

void CMainFrame::ConfigureCommandMenu(QMenu* menu)
{
    if (!menu
        || menu->property("originalCommandMenuConfigured").toBool()) {
        return;
    }
    menu->setProperty("originalCommandMenuConfigured", true);
    connect(menu, &QMenu::aboutToShow,
            this, &CMainFrame::updateCommandUi);
    connect(menu, &QMenu::hovered, this,
            [this](QAction* action) {
                if (!m_status0) return;
                if (!action || action->isSeparator()
                    || action->menu()
                    || action->statusTip().isEmpty()) {
                    m_status0->setText(m_statusPaneStrings[0]);
                } else {
                    m_status0->setText(action->statusTip());
                }
            });
    connect(menu, &QMenu::aboutToHide, this, [this] {
        if (m_status0)
            m_status0->setText(m_statusPaneStrings[0]);
    });
}

void CMainFrame::ConfigureContextMenu(QMenu* menu)
{
    if (!menu) return;
    ConfigureCommandMenu(menu);
    for (QAction* action : menu->actions()) {
        if (!action) continue;
        if (QMenu* submenu = action->menu()) {
            ConfigureContextMenu(submenu);
            continue;
        }
        const QString commandIdentifier = action->data().toString();
        if (!commandIdentifier.isEmpty())
            action->setStatusTip(commandStatusText(commandIdentifier));
    }
}

void CMainFrame::appendMenuItems(QMenu* menu, const QList<OriginalMenuItem>& items)
{
    BOOL hasMemberProfile = FALSE;
    BOOL hasMemberIdentity = FALSE;
    for (const OriginalMenuItem& item : items) {
        if (item.type == OriginalMenuItemType::Separator) {
            menu->addSeparator();
        } else if (item.type == OriginalMenuItemType::Popup) {
            QMenu* popup = menu->addMenu(item.text);
            ConfigureCommandMenu(popup);
            appendMenuItems(popup, item.children);
            bool hasList = false;
            bool hasIcon = false;
            for (const OriginalMenuItem& child : item.children) {
                if (child.commandIdentifier == QLatin1String("ID_DEFINE_MACRO")) {
                    m_macroMenus.append(popup);
                }
                hasList = hasList
                    || child.commandIdentifier
                        == QLatin1String("ID_VIEW_LIST");
                hasIcon = hasIcon
                    || child.commandIdentifier
                        == QLatin1String("ID_VIEW_ICON");
            }
            if (hasList && hasIcon)
                m_memberListPopupActions.append(
                    QPointer<QAction>(popup->menuAction()));
        } else {
            QAction* action = addCommand(menu, item.text, item.commandIdentifier);
            if (item.commandIdentifier
                == QLatin1String("ID_MEMBER_GETINFO")) {
                hasMemberProfile = TRUE;
            } else if (item.commandIdentifier
                       == QLatin1String("ID_GETIDENTITY")) {
                hasMemberIdentity = TRUE;
            }
            if (item.flags.contains(QStringLiteral("GRAYED"))
                || item.flags.contains(QStringLiteral("INACTIVE"))) {
                action->setEnabled(false);
            }
            if (item.flags.contains(QStringLiteral("CHECKED"))) {
                action->setCheckable(true);
                action->setChecked(true);
            }
        }
    }
    if (hasMemberProfile && hasMemberIdentity
        && !m_memberMenus.contains(menu)) {
        m_memberMenus.append(menu);
    }
    makeExclusiveMenuGroup(
        menu, {QStringLiteral("ID_VIEW_COMICS"),
               QStringLiteral("ID_VIEW_TEXT")});
    makeExclusiveMenuGroup(
        menu, {QStringLiteral("ID_VIEW_LIST"),
               QStringLiteral("ID_VIEW_ICON")});
    makeExclusiveMenuGroup(
        menu, {QStringLiteral("ID_MAKEADMIN"),
               QStringLiteral("ID_MAKESPEAKER"),
               QStringLiteral("ID_MAKESPECTATOR")});
}

void CMainFrame::UpdateMacroMenu()
{
    for (QMenu* menu : std::as_const(m_macroMenus)) {
        if (!menu) continue;
        const QList<QAction*> actions = menu->actions();
        INT defineIndex = -1;
        for (INT index = 0; index < actions.size(); ++index) {
            if (actions[index]->data().toString()
                == QLatin1String("ID_DEFINE_MACRO")) {
                defineIndex = index;
                break;
            }
        }
        if (defineIndex < 0) continue;
        for (INT index = actions.size() - 1; index > defineIndex; --index) {
            QAction* action = actions[index];
            RemoveDynamicCommand(menu, action);
        }
        BOOL present = FALSE;
        for (INT macro = 0; macro < NMACROS; ++macro) {
            if (!theApp.m_macros[macro].m_bDefined) continue;
            if (!present) {
                menu->addSeparator();
                present = TRUE;
            }
            const QString command = QStringLiteral("ID_MACRO_A%1").arg(macro);
            addCommand(menu,
                QStringLiteral("%1\tAlt+%2")
                    .arg(theApp.m_macros[macro].m_strName)
                    .arg(macro),
                command);
        }
    }
    updateCommandUi();
}

void CMainFrame::createMenus()
{
    const QList<OriginalMenuItem> root = originalMenuResource(QStringLiteral("IDR_MAINFRAME"));
    for (const OriginalMenuItem& item : root) {
        if (item.type != OriginalMenuItemType::Popup) continue;
        QMenu* menu = menuBar()->addMenu(item.text);
        ConfigureCommandMenu(menu);
        appendMenuItems(menu, item.children);
        for (const OriginalMenuItem& child : item.children) {
            if (child.commandIdentifier
                == QLatin1String("ID_WINDOW_CASCADE")) {
                m_windowMenu = menu;
                connect(menu, &QMenu::aboutToShow,
                        this, &CMainFrame::updateWindowMenu);
                break;
            }
        }
    }
}

void CMainFrame::updateWindowMenu()
{
    if (!m_windowMenu || !m_mdiArea) return;
    for (const QPointer<QAction>& action :
         std::as_const(m_windowDocumentActions)) {
        if (!action) continue;
        m_windowMenu->removeAction(action);
        delete action;
    }
    m_windowDocumentActions.clear();
    if (m_windowDocumentSeparator) {
        m_windowMenu->removeAction(m_windowDocumentSeparator);
        delete m_windowDocumentSeparator;
        m_windowDocumentSeparator = nullptr;
    }

    QList<CChildFrame*> visibleFrames;
    for (QMdiSubWindow* window :
         m_mdiArea->subWindowList(QMdiArea::CreationOrder)) {
        auto* child = dynamic_cast<CChildFrame*>(window);
        if (child && child->GetDocument() && !child->isHidden())
            visibleFrames.append(child);
    }
    if (visibleFrames.isEmpty()) return;

    m_windowDocumentSeparator = m_windowMenu->addSeparator();
    const INT sourceMdiWindowLimit = 9;
    const INT count = std::min(
        sourceMdiWindowLimit,
        static_cast<INT>(visibleFrames.size()));
    for (INT index = 0; index < count; ++index) {
        CChildFrame* child = visibleFrames.at(index);
        CChatDoc* document = child->GetDocument();
        QAction* action = m_windowMenu->addAction(
            QStringLiteral("&%1 %2")
                .arg(index + 1)
                .arg(quoteMenuAmpersands(document->GetTitle())));
        action->setCheckable(true);
        action->setChecked(child == m_mdiArea->activeSubWindow());
        action->setProperty(
            "originalMdiDocument",
            QVariant::fromValue<void*>(document));
        connect(action, &QAction::triggered, this,
                [this, document] { ActivateDocument(document); });
        m_windowDocumentActions.append(
            QPointer<QAction>(action));
    }
}

void CMainFrame::createAccelerators()
{
    for (const OriginalAccelerator& accelerator : originalAcceleratorResource(
             QStringLiteral("IDR_MAINFRAME"))) {
        const QKeySequence sequence = acceleratorSequence(accelerator);
        if (sequence.isEmpty()) continue;
        auto* shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WindowShortcut);
        shortcut->setProperty("originalCommandIdentifier",
                              accelerator.commandIdentifier);
        QString classification;
        switch (commandClass(accelerator.commandIdentifier)) {
        case CommandClass::Active:
            classification = QStringLiteral("active");
            break;
        case CommandClass::Deferred:
            classification = QStringLiteral("deferred");
            break;
        case CommandClass::NoHandler:
            classification = QStringLiteral("no-handler");
            break;
        case CommandClass::Unresolved:
            classification = QStringLiteral("unresolved");
            break;
        }
        shortcut->setProperty("originalCommandClass", classification);
        m_commandShortcuts[accelerator.commandIdentifier].append(
            QPointer<QShortcut>(shortcut));
        connect(shortcut, &QShortcut::activated, this,
                [this, command = accelerator.commandIdentifier] {
                    executeCommand(command);
                });
    }
}

void CMainFrame::createToolBars()
{
    m_wndToolBar = new CChatToolBar(this);
    m_wndToolBar->Create(
        this, FALSE,
        [this](CCoolToolBarEx* toolbar, const QString& command,
               const QIcon& icon) {
            return addToolCommand(toolbar, command, icon);
        },
        [this](const QString& command) { executeCommand(command); },
        [this]() -> QMenu* {
            const QList<QAction*> menus = menuBar()->actions();
            return menus.size() > 6 ? menus[6]->menu() : nullptr;
        },
        [this](QAction* action, const QString& command) {
            configureCommandAction(action, command);
        },
        [this](QMenu* menu) { ConfigureCommandMenu(menu); });
    if (!theApp.m_pbCoolBarState.isEmpty())
        m_wndToolBar->LoadStateFromBuffer(theApp.m_pbCoolBarState);
}

void CMainFrame::createStatusBar()
{
    m_statusPaneStrings[0] =
        originalResourceString(QStringLiteral("ID_DISCONNECTED"));
    QString memberText = originalResourceString(QStringLiteral("ID_USER_PLURAL"));
    memberText.replace(QStringLiteral("%1"), QStringLiteral("0"));
    m_statusPaneStrings[1] = memberText;
    m_status0 = new QLabel(m_statusPaneStrings[0], this);
    m_status1 = new QLabel(m_statusPaneStrings[1], this);
    m_status0->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_status1->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    m_status1->setFixedWidth(originalResourceString(
        QStringLiteral("IDS_MEMBER_COUNT_WIDTH")).toInt());
    statusBar()->addWidget(m_status0, 1);
    statusBar()->addPermanentWidget(m_status1);
    statusBar()->setVisible((theApp.m_iShowBars & SB_STATUSBAR) != 0);
}

void CMainFrame::SetStatusPaneString(int pane, const QString& text)
{
    if (pane < 0 || pane >= 2) return;
    m_statusPaneStrings[pane] = text;
    if (pane == 0 && m_status0) m_status0->setText(text);
    else if (pane == 1 && m_status1) m_status1->setText(text);
}

void CMainFrame::UpdateAdminMenu(CChatDoc* document)
{
    const bool showAdminMenu = document && document->m_puiSelf
        && document->m_puiSelf->IsOperator();
    const QList<OriginalMenuItem> resource =
        originalMenuResource(QStringLiteral("IDR_ADMIN"));
    const QList<OriginalMenuItem> adminItems =
        !resource.isEmpty()
            && resource.first().type == OriginalMenuItemType::Popup
        ? resource.first().children : QList<OriginalMenuItem>();

    for (QMenu* memberMenu : std::as_const(m_memberMenus)) {
        if (!memberMenu) continue;
        QAction* existing = m_adminMenuActions.value(memberMenu);
        if (showAdminMenu && !existing && !adminItems.isEmpty()) {
            QAction* separator = memberMenu->addSeparator();
            auto* adminMenu = new QMenu(
                originalResourceString(
                    QStringLiteral("IDS_ADMINMENU_LABEL")),
                memberMenu);
            ConfigureCommandMenu(adminMenu);
            appendMenuItems(adminMenu, adminItems);
            m_adminSeparators.insert(memberMenu, separator);
            m_adminMenuActions.insert(memberMenu,
                                      memberMenu->addMenu(adminMenu));
        } else if (!showAdminMenu && existing) {
            QMenu* adminMenu = existing->menu();
            unregisterMenuActions(adminMenu);
            memberMenu->removeAction(existing);
            m_adminMenuActions.remove(memberMenu);
            if (adminMenu) delete adminMenu;

            if (QAction* separator =
                    m_adminSeparators.take(memberMenu)) {
                memberMenu->removeAction(separator);
                delete separator;
            }
        }
    }
}

QWidget* CMainFrame::GetCommandFocusWidget() const
{
    QWidget* focus = QApplication::focusWidget();
    if (focus && !isMenuFocusWidget(focus)) return focus;
    return m_commandFocusWidget;
}

void CMainFrame::RefreshCommandUi()
{
    updateCommandUi();
}

void CMainFrame::scheduleCommandUiUpdate()
{
    if (m_uiUpdatePending || m_destroying) return;
    m_uiUpdatePending = true;
    QTimer::singleShot(0, this, [this] {
        m_uiUpdatePending = false;
        if (!m_destroying) updateCommandUi();
    });
}

bool CMainFrame::eventFilter(QObject* watched, QEvent* event)
{
    if (!event) return QMainWindow::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::FocusIn:
        if (auto* widget = qobject_cast<QWidget*>(watched);
            widget && !isMenuFocusWidget(widget)) {
            m_commandFocusWidget = widget;
        }
        scheduleCommandUiUpdate();
        break;
    case QEvent::FocusOut:
    case QEvent::KeyRelease:
    case QEvent::MouseButtonRelease:
    case QEvent::Shortcut:
        scheduleCommandUiUpdate();
        break;
    case QEvent::Leave:
        if (qobject_cast<QToolBar*>(watched) && m_status0)
            m_status0->setText(m_statusPaneStrings[0]);
        break;
    case QEvent::Hide:
        if (qobject_cast<QMenu*>(watched) && m_status0)
            m_status0->setText(m_statusPaneStrings[0]);
        [[fallthrough]];
    case QEvent::Show:
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::WindowStateChange:
    case QEvent::ZOrderChange:
        if (dynamic_cast<CChildFrame*>(watched)) {
            QTimer::singleShot(0, this, [this] {
                if (!m_destroying) UpdateVisibilityInfo();
            });
        }
        scheduleCommandUiUpdate();
        break;
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void CMainFrame::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    AutoArrangeWindows();
}

void CMainFrame::UpdateVisibilityInfo()
{
    if (!m_mdiArea || m_destroying) return;
    const QList<QMdiSubWindow*> windows =
        m_mdiArea->subWindowList(QMdiArea::StackingOrder);
    for (INT index = 0; index < windows.size(); ++index) {
        auto* frame = dynamic_cast<CChildFrame*>(windows.at(index));
        if (!frame || !frame->GetDocument()) continue;
        if (frame->isMinimized()) {
            frame->GetDocument()->SetObscured(TRUE);
            continue;
        }

        const QRect frameRect = frame->geometry();
        const qint64 frameArea =
            qint64(frameRect.width()) * frameRect.height();
        qint64 coveredArea = 0;
        for (INT above = index + 1; above < windows.size(); ++above) {
            QMdiSubWindow* covering = windows.at(above);
            if (!covering) continue;
            const QRect intersection =
                frameRect.intersected(covering->geometry());
            coveredArea +=
                qint64(intersection.width()) * intersection.height();
        }
        frame->GetDocument()->SetObscured(
            frameArea <= 0 || 2 * coveredArea >= frameArea);
    }
}

void CMainFrame::ArrangeIcons()
{
    if (!m_mdiArea) return;
    QList<QMdiSubWindow*> icons;
    for (QMdiSubWindow* window :
         m_mdiArea->subWindowList(QMdiArea::StackingOrder)) {
        if (!window->isHidden() && window->isMinimized())
            icons.append(window);
    }
    if (icons.isEmpty()) return;

    const QRect area = m_mdiArea->viewport()->rect();
    const int iconHeight = std::max(
        style()->pixelMetric(QStyle::PM_TitleBarHeight) + 8, 24);
    const int iconWidth = std::max(
        area.width() / static_cast<int>(icons.size()), 160);
    int left = area.left();
    int top = area.bottom() - iconHeight + 1;
    for (QMdiSubWindow* icon : icons) {
        const int width = std::min(iconWidth,
                                   area.right() - left + 1);
        icon->setGeometry(left, top, std::max(width, 1), iconHeight);
        left += iconWidth;
        if (left > area.right()) break;
    }
    UpdateVisibilityInfo();
}

CMainFrame::CommandClass CMainFrame::commandClass(
    const QString& commandIdentifier) const
{
    if (commandIdentifier.startsWith(QLatin1String("ID_MACRO_A"))) {
        bool valid = false;
        const INT macro = commandIdentifier.mid(10).toInt(&valid);
        return valid && macro >= 0 && macro < NMACROS
            ? CommandClass::Active : CommandClass::Unresolved;
    }
    static const QSet<QString> active = {
        QStringLiteral("ID_APP_EXIT"),
        QStringLiteral("ID_APP_ABOUT"),
        QStringLiteral("ID_FILE_NEW"),
        QStringLiteral("ID_FILE_CLOSE"),
        QStringLiteral("ID_FILE_PRINT"),
        QStringLiteral("ID_FILE_PRINT_SETUP"),
        QStringLiteral("ID_EDIT_UNDO"),
        QStringLiteral("ID_EDIT_CUT"),
        QStringLiteral("ID_EDIT_COPY"),
        QStringLiteral("ID_EDIT_PASTE"),
        QStringLiteral("ID_EDIT_DELETE"),
        QStringLiteral("ID_EDIT_SELECTALL"),
        QStringLiteral("ID_SESSION_CONNECT"),
        QStringLiteral("ID_SESSION_DISCONNECT"),
        QStringLiteral("ID_SESSION_NEWROOM"),
        QStringLiteral("ID_SESSION_LEAVE"),
        QStringLiteral("ID_ROOM_CREATEROOM"),
        QStringLiteral("ID_MOTD"),
        QStringLiteral("ID_VIEW_TOOLBAR_MAIN"),
        QStringLiteral("ID_VIEW_TOOLBAR_MEMBER"),
        QStringLiteral("ID_VIEW_TOOLBAR_TEXT"),
        QStringLiteral("ID_VIEW_TABBAR"),
        QStringLiteral("ID_VIEW_STATUS_BAR"),
        QStringLiteral("ID_VIEW_STATUSWINDOW"),
        QStringLiteral("ID_VIEW_LOGINNOTIFS"),
        QStringLiteral("ID_VIEW_AUTOMATIONS"),
        QStringLiteral("ID_VIEW_OPTIONS"),
        QStringLiteral("ID_DEFINE_MACRO"),
        QStringLiteral("ID_VIEW_COMICS"),
        QStringLiteral("ID_VIEW_TEXT"),
        QStringLiteral("ID_VIEW_LIST"),
        QStringLiteral("ID_VIEW_ICON"),
        QStringLiteral("ID_CLEAR_HISTORY"),
        QStringLiteral("ID_ACTIONS_SAY"),
        QStringLiteral("ID_ACTIONS_THINK"),
        QStringLiteral("ID_ACTIONS_WHISPER"),
        QStringLiteral("ID_SEND_ACTION"),
        QStringLiteral("ID_SETFONT"),
        QStringLiteral("ID_SETCOLOR"),
        QStringLiteral("ID_SWITCHBOLD"),
        QStringLiteral("ID_SWITCHITALIC"),
        QStringLiteral("ID_SWITCHUNDERLINED"),
        QStringLiteral("ID_SWITCHFIXEDPITCH"),
        QStringLiteral("ID_SWITCHSYMBOL"),
        QStringLiteral("ID_WINDOW_CASCADE"),
        QStringLiteral("ID_WINDOW_TILE_HORZ"),
        QStringLiteral("ID_WINDOW_TILE_VERT"),
        QStringLiteral("ID_WINDOW_TILE_AUTO"),
        QStringLiteral("ID_WINDOW_ARRANGE"),
        QStringLiteral("ID_MEMBER_GETINFO"),
        QStringLiteral("ID_MEMBER_GETCHAR"),
        QStringLiteral("ID_MEMBER_IGNORE"),
        QStringLiteral("ID_ADDTONOTIFICATIONS"),
        QStringLiteral("ID_GETIDENTITY"),
        QStringLiteral("ID_GET_VERSION"),
        QStringLiteral("ID_PING_USER"),
        QStringLiteral("ID_GET_LOCALTIME"),
        QStringLiteral("ID_AWAY_TOGGLE"),
        QStringLiteral("ID_SEND_EMAIL"),
        QStringLiteral("ID_VISIT_HOMEPAGE"),
        QStringLiteral("ID_CHANNELPROPS"),
        QStringLiteral("ID_CHATROOM_LIST"),
        QStringLiteral("ID_USER_LIST"),
        QStringLiteral("ID_WHISPERBOX_MLIST"),
        QStringLiteral("ID_ADMINISTRATOR_KICK"),
        QStringLiteral("ID_ADMIN_BAN"),
        QStringLiteral("ID_ADMIN_BGRNDSYNC"),
        QStringLiteral("ID_MAKEADMIN"),
        QStringLiteral("ID_MAKESPEAKER"),
        QStringLiteral("ID_MAKESPECTATOR"),
        QStringLiteral("ID_INVITE"),
        QStringLiteral("ID_HELP_FREESTUFF"),
        QStringLiteral("ID_HELP_PRODUCTNEWS"),
        QStringLiteral("ID_HELP_FAQ"),
        QStringLiteral("ID_HELP_ONLINESUPPORT"),
        QStringLiteral("ID_HELP_BESTOFWEB"),
        QStringLiteral("ID_HELP_SEARCHTHEWEB"),
        QStringLiteral("ID_HELP_MSHOMEPAGE")
    };
    if (active.contains(commandIdentifier)) return CommandClass::Active;

    static const QSet<QString> deferred = {
        QStringLiteral("ID_FILE_OPEN"),
        QStringLiteral("ID_FILE_SAVE"),
        QStringLiteral("ID_FILE_SAVE_AS"),
        QStringLiteral("ID_FILE_CREATESHORTCUT"),
        QStringLiteral("ID_FILE_PRINT_PREVIEW"),
        QStringLiteral("ID_FAVORITES_ADDTOFAVORITES"),
        QStringLiteral("ID_FAVORITES_OPENFAVORITES"),
        QStringLiteral("ID_TURN_OFF_SOUNDS"),
        QStringLiteral("ID_PLAY_SOUND"),
        QStringLiteral("ID_START_NETMEETING"),
        QStringLiteral("ID_SEND_FILE"),
        QStringLiteral("ID_HELP_TOPICS"),
        QStringLiteral("ID_HELP_RELEASENOTES")
    };
    if (deferred.contains(commandIdentifier)) return CommandClass::Deferred;
    if (commandIdentifier == QLatin1String("ID_VIEW_MACROS"))
        return CommandClass::NoHandler;
    return CommandClass::Unresolved;
}

void CMainFrame::updateCommandUi()
{
    for (auto iterator = m_commandActions.begin();
         iterator != m_commandActions.end(); ++iterator) {
        for (INT index = iterator.value().size() - 1;
             index >= 0; --index) {
            if (!iterator.value().at(index))
                iterator.value().removeAt(index);
        }
        const bool active = commandClass(iterator.key())
            == CommandClass::Active;
        for (const QPointer<QAction>& action : iterator.value()) {
            if (action) action->setEnabled(active);
        }
    }

    setActionsEnabled(m_commandActions, QStringLiteral("ID_SESSION_CONNECT"),
                      theApp.OnUpdateSessionConnect());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SESSION_DISCONNECT"),
                      theApp.OnUpdateDisconnect());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SESSION_NEWROOM"),
                      theApp.OnUpdateNewroom());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_ROOM_CREATEROOM"),
                      theApp.OnUpdateNewroom());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SESSION_LEAVE"),
                      m_doc && m_doc->OnUpdateLeave());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MOTD"),
                      theApp.OnUpdateMotd());
    BOOL checked = FALSE;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_AWAY_TOGGLE"),
                      theApp.OnUpdateAwayToggle(&checked));
    setActionsChecked(m_commandActions, QStringLiteral("ID_AWAY_TOGGLE"),
                      checked);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_CHATROOM_LIST"),
                      theApp.OnUpdateCanSearch());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_USER_LIST"),
                      theApp.OnUpdateCanSearch());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_FILE_CLOSE"),
                      m_doc != nullptr);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_FILE_PRINT"),
                      m_doc && m_chatView
                          && m_doc->OnUpdateFilePrint());
    setActionsEnabled(m_commandActions,
                      QStringLiteral("ID_FILE_PRINT_SETUP"), true);

    const bool hasDocument = m_doc != nullptr;
    BOOL comicsChecked = FALSE;
    BOOL textChecked = FALSE;
    BOOL comicsEnabled = FALSE;
    BOOL textEnabled = FALSE;
    if (m_doc) {
        if (m_doc->m_bStatusView) {
            auto* statusView =
                dynamic_cast<CStatusView*>(m_doc->m_textView);
            if (statusView) {
                comicsEnabled =
                    statusView->OnUpdateViewComics(&comicsChecked);
                textEnabled =
                    statusView->OnUpdateViewText(&textChecked);
            }
        } else {
            comicsEnabled =
                m_doc->OnUpdateViewComics(&comicsChecked);
            textEnabled =
                m_doc->OnUpdateViewText(&textChecked);
        }
    }
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_COMICS"),
                      comicsEnabled);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_TEXT"),
                      textEnabled);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_COMICS"),
                      comicsChecked);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_TEXT"),
                      textChecked);
    BOOL iconChecked = FALSE;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_ICON"),
                      m_doc
                          && m_doc->OnUpdateViewIcon(&iconChecked));
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_ICON"),
                      iconChecked);
    BOOL listChecked = FALSE;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_LIST"),
                      m_doc
                          && m_doc->OnUpdateViewList(
                              FALSE, &listChecked));
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_LIST"),
                      listChecked);
    for (const QPointer<QAction>& popup :
         std::as_const(m_memberListPopupActions)) {
        if (popup)
            popup->setEnabled(
                m_doc && m_doc->OnUpdateViewList(TRUE));
    }

    setActionsEnabled(m_commandActions, QStringLiteral("ID_MEMBER_GETINFO"),
                      m_doc && m_doc->OnUpdateMemberGetinfo());
    setActionsEnabled(
        m_commandActions, QStringLiteral("ID_MEMBER_GETCHAR"),
        m_doc && m_doc->OnUpdateGetComicCharacter());
    BOOL allIgnored = FALSE;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MEMBER_IGNORE"),
                      m_doc
                          && m_doc->OnUpdateMemberIgnore(
                              &allIgnored));
    setActionsChecked(m_commandActions, QStringLiteral("ID_MEMBER_IGNORE"),
                      allIgnored);
    setActionsEnabled(m_commandActions,
                      QStringLiteral("ID_ADDTONOTIFICATIONS"),
                      m_doc && m_doc->OnUpdateAddToNotifs());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_GETIDENTITY"),
                      m_doc && m_doc->OnUpdateGetidentity());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_GET_VERSION"),
                      m_doc && m_doc->OnUpdateGetidentity());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_PING_USER"),
                      m_doc && m_doc->OnUpdateGetidentity());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_GET_LOCALTIME"),
                      m_doc && m_doc->OnUpdateGetidentity());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SEND_EMAIL"),
                      m_doc && m_doc->OnUpdateComicUserNotSelf());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VISIT_HOMEPAGE"),
                      m_doc && m_doc->OnUpdateVisitHomepage());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_WHISPERBOX_MLIST"),
                      m_doc && m_doc->OnUpdate1SelectionNotSelf());
    setActionsEnabled(m_commandActions,
                      QStringLiteral("ID_ADMINISTRATOR_KICK"),
                      m_doc && m_doc->OnUpdate1SelectionNotSelf());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_ADMIN_BAN"),
                      m_doc && m_doc->OnUpdateAdminBan());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_INVITE"),
                      m_doc && m_doc->OnUpdateInvite());
    BOOL adminChecked = FALSE;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MAKEADMIN"),
                      m_doc
                          && m_doc->OnUpdateMakeadmin(
                              &adminChecked));
    setActionsChecked(m_commandActions, QStringLiteral("ID_MAKEADMIN"),
                      adminChecked);
    BOOL speakerChecked = FALSE;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MAKESPEAKER"),
                      m_doc
                          && m_doc->OnUpdateMakespeaker(
                              &speakerChecked));
    setActionsChecked(m_commandActions, QStringLiteral("ID_MAKESPEAKER"),
                      speakerChecked);
    BOOL spectatorChecked = FALSE;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MAKESPECTATOR"),
                      m_doc
                          && m_doc->OnUpdateMakespectator(
                              &spectatorChecked));
    setActionsChecked(m_commandActions, QStringLiteral("ID_MAKESPECTATOR"),
                      spectatorChecked);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_CHANNELPROPS"),
                      m_doc && m_doc->OnUpdateChannelprops());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_ADMIN_BGRNDSYNC"),
                      m_doc && m_doc->OnUpdateAdminBgrndsync());

    for (const auto& toolbar : {
             std::pair{QStringLiteral("ID_VIEW_TOOLBAR_MAIN"),
                       UINT(ID_VIEW_TOOLBAR_MAIN)},
             std::pair{QStringLiteral("ID_VIEW_TOOLBAR_MEMBER"),
                       UINT(ID_VIEW_TOOLBAR_MEMBER)},
             std::pair{QStringLiteral("ID_VIEW_TOOLBAR_TEXT"),
                       UINT(ID_VIEW_TOOLBAR_TEXT)}}) {
        BOOL toolbarChecked = FALSE;
        setActionsEnabled(
            m_commandActions, toolbar.first,
            theApp.OnUpdateViewToolBar(
                toolbar.second, &toolbarChecked));
        setActionsChecked(m_commandActions, toolbar.first,
                          toolbarChecked);
    }
    BOOL tabbarChecked = FALSE;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_TABBAR"),
                      theApp.OnUpdateViewTabbar(&tabbarChecked));
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_TABBAR"),
                      tabbarChecked);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_STATUS_BAR"),
                      statusBar()->isVisible());
    BOOL statusWindowChecked = FALSE;
    setActionsEnabled(
        m_commandActions, QStringLiteral("ID_VIEW_STATUSWINDOW"),
        theApp.OnUpdateViewStatuswindow(&statusWindowChecked));
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_STATUSWINDOW"),
                      statusWindowChecked);
    BOOL loginNotifsChecked = FALSE;
    setActionsEnabled(
        m_commandActions, QStringLiteral("ID_VIEW_LOGINNOTIFS"),
        theApp.OnUpdateViewLoginNotifs(&loginNotifsChecked));
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_LOGINNOTIFS"),
                      loginNotifsChecked);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_AUTOMATIONS"),
                      theApp.OnUpdateViewAutomations());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_OPTIONS"),
                      theApp.OnUpdateViewOptions());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_DEFINE_MACRO"),
                      true);
    CSayWnd* sayWindow = m_doc ? GetSay() : nullptr;
    const bool hasSayWindow = sayWindow && sayWindow->GetSayEdit();
    setActionsEnabled(m_commandActions, QStringLiteral("ID_CLEAR_HISTORY"),
                      hasDocument);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_ACTIONS_SAY"),
                      hasSayWindow);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_ACTIONS_THINK"),
                      hasSayWindow);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_ACTIONS_WHISPER"),
                      hasSayWindow);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SEND_ACTION"),
                      hasSayWindow);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SETFONT"),
                      hasSayWindow);
    BOOL colorChecked = FALSE;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SETCOLOR"),
                      m_doc
                          && m_doc->OnUpdateFormat(
                              ID_SETCOLOR, &colorChecked));
    for (const auto& format : {
             std::pair{QStringLiteral("ID_SWITCHBOLD"),
                       UINT(ID_SWITCHBOLD)},
             std::pair{QStringLiteral("ID_SWITCHITALIC"),
                       UINT(ID_SWITCHITALIC)},
             std::pair{QStringLiteral("ID_SWITCHUNDERLINED"),
                       UINT(ID_SWITCHUNDERLINED)},
             std::pair{QStringLiteral("ID_SWITCHFIXEDPITCH"),
                       UINT(ID_SWITCHFIXEDPITCH)},
             std::pair{QStringLiteral("ID_SWITCHSYMBOL"),
                       UINT(ID_SWITCHSYMBOL)}}) {
        BOOL formatChecked = FALSE;
        setActionsEnabled(
            m_commandActions, format.first,
            m_doc
                && m_doc->OnUpdateFormat(
                    format.second, &formatChecked));
        setActionsChecked(m_commandActions, format.first,
                          formatChecked);
    }

    UpdateAdminMenu(m_doc);
    for (QMenu* memberMenu : std::as_const(m_memberMenus)) {
        if (!memberMenu) continue;
        if (m_doc) {
            m_doc->UpdateComicCharacterMenu(memberMenu);
        } else {
            for (QAction* action : memberMenu->actions()) {
                if (action->data().toString()
                    == QLatin1String("ID_MEMBER_GETCHAR")) {
                    RemoveDynamicCommand(memberMenu, action);
                    break;
                }
            }
        }
    }
    for (INT macro = 0; macro < NMACROS; ++macro) {
        const QString command = QStringLiteral("ID_MACRO_A%1").arg(macro);
        setActionsEnabled(m_commandActions, command,
            m_doc && m_doc->OnUpdateMacro(ID_MACRO_A0 + macro));
    }
    setActionsChecked(m_commandActions, QStringLiteral("ID_WINDOW_TILE_AUTO"),
                      (theApp.m_flags0 & F0_AUTOARRANGEWNDS) != 0);

    bool hasVisibleWindow = false;
    bool hasMinimizedWindow = false;
    for (QMdiSubWindow* window : m_mdiArea->subWindowList()) {
        if (window->isHidden()) continue;
        hasVisibleWindow = true;
        if (window->isMinimized()) hasMinimizedWindow = true;
    }
    setActionsEnabled(m_commandActions, QStringLiteral("ID_WINDOW_CASCADE"),
                      hasVisibleWindow);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_WINDOW_TILE_HORZ"),
                      hasVisibleWindow);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_WINDOW_TILE_VERT"),
                      hasVisibleWindow);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_WINDOW_ARRANGE"),
                      hasMinimizedWindow);

    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_UNDO"),
                      m_doc && m_doc->OnUpdateEditUndo());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_CUT"),
                      m_doc && m_doc->OnUpdateEditCut());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_COPY"),
                      m_doc && m_doc->OnUpdateEditCopy());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_DELETE"),
                      m_doc && m_doc->OnUpdateEditDelete());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_PASTE"),
                      m_doc && m_doc->OnUpdateEditPaste());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_SELECTALL"),
                      m_doc && m_doc->OnUpdateEditSelectAll());

    for (auto iterator = m_commandShortcuts.begin();
         iterator != m_commandShortcuts.end(); ++iterator) {
        const CommandClass classification =
            commandClass(iterator.key());
        bool enabled = classification == CommandClass::Deferred
            || classification == CommandClass::NoHandler;
        if (classification == CommandClass::Active) {
            if (iterator.key().startsWith(
                    QLatin1String("ID_MACRO_A"))) {
                bool valid = false;
                const INT macro = iterator.key().mid(10).toInt(&valid);
                enabled = valid && macro >= 0 && macro < NMACROS
                    && m_doc
                    && m_doc->OnUpdateMacro(ID_MACRO_A0 + macro);
            } else if (iterator.key()
                           == QLatin1String("ID_ACTIONS_SAY")
                       || iterator.key()
                           == QLatin1String("ID_ACTIONS_THINK")
                       || iterator.key()
                           == QLatin1String("ID_ACTIONS_WHISPER")
                       || iterator.key()
                           == QLatin1String("ID_SEND_ACTION")) {
                enabled = hasSayWindow;
            } else {
                for (const QPointer<QAction>& action :
                     m_commandActions.value(iterator.key())) {
                    if (action && action->isEnabled()) {
                        enabled = true;
                        break;
                    }
                }
            }
        }
        for (const QPointer<QShortcut>& shortcut : iterator.value()) {
            if (shortcut) shortcut->setEnabled(enabled);
        }
    }
}

void CMainFrame::executeCommand(const QString& commandIdentifier)
{
    if (commandClass(commandIdentifier) != CommandClass::Active) return;

    if (commandIdentifier == QLatin1String("ID_APP_EXIT")) {
        qApp->quit();
    } else if (commandIdentifier == QLatin1String("ID_FILE_NEW")) {
        CreateNewDocument();
    } else if (commandIdentifier == QLatin1String("ID_FILE_CLOSE")) {
        if (m_doc) m_doc->OnFileClose();
    } else if (commandIdentifier == QLatin1String("ID_FILE_PRINT")) {
        if (m_chatView) m_chatView->OnFilePrint(m_printer.get(), FALSE);
    } else if (commandIdentifier == QLatin1String("ID_FILE_PRINT_SETUP")) {
        theApp.OnFilePrintSetup(m_printer.get(), this);
    } else if (commandIdentifier == QLatin1String("ID_SESSION_CONNECT")) {
        theApp.OnSessionConnect();
    } else if (commandIdentifier == QLatin1String("ID_SESSION_DISCONNECT")) {
        theApp.OnDisconnect();
    } else if (commandIdentifier == QLatin1String("ID_SESSION_NEWROOM")) {
        theApp.OnNewroom();
    } else if (commandIdentifier == QLatin1String("ID_ROOM_CREATEROOM")) {
        theApp.OnCreateroom();
    } else if (commandIdentifier == QLatin1String("ID_SESSION_LEAVE")) {
        if (m_doc) m_doc->OnLeave();
    } else if (commandIdentifier == QLatin1String("ID_MOTD")) {
        theApp.OnMotd();
    } else if (commandIdentifier == QLatin1String("ID_AWAY_TOGGLE")) {
        theApp.OnAwayToggle();
    } else if (commandIdentifier == QLatin1String("ID_CHATROOM_LIST")) {
        theApp.OnChatroomList();
    } else if (commandIdentifier == QLatin1String("ID_USER_LIST")) {
        theApp.OnUserList();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_COMICS")) {
        if (m_doc) m_doc->OnViewComics();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TEXT")) {
        if (m_doc) m_doc->OnViewText();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_LIST")) {
        if (m_doc) m_doc->OnViewList();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_ICON")) {
        if (m_doc) m_doc->OnViewIcon();
    } else if (commandIdentifier == QLatin1String("ID_CLEAR_HISTORY")) {
        if (m_doc) m_doc->OnClearHistory();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TOOLBAR_MAIN")) {
        theApp.OnViewToolBar(ID_VIEW_TOOLBAR_MAIN);
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TOOLBAR_MEMBER")) {
        theApp.OnViewToolBar(ID_VIEW_TOOLBAR_MEMBER);
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TOOLBAR_TEXT")) {
        theApp.OnViewToolBar(ID_VIEW_TOOLBAR_TEXT);
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TABBAR")) {
        theApp.OnViewTabbar();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_STATUS_BAR")) {
        const bool show = !statusBar()->isVisible();
        statusBar()->setVisible(show);
        if (show) theApp.m_iShowBars |= SB_STATUSBAR;
        else theApp.m_iShowBars &= ~SB_STATUSBAR;
    } else if (commandIdentifier == QLatin1String("ID_VIEW_STATUSWINDOW")) {
        theApp.OnViewStatuswindow();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_LOGINNOTIFS")) {
        theApp.OnViewLoginNotifs();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_AUTOMATIONS")) {
        theApp.OnViewAutomations();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_OPTIONS")) {
        theApp.OnViewOptions();
    } else if (commandIdentifier == QLatin1String("ID_DEFINE_MACRO")) {
        theApp.OnDefineMacro();
    } else if (commandIdentifier.startsWith(QLatin1String("ID_MACRO_A"))) {
        bool valid = false;
        const INT macro = commandIdentifier.mid(10).toInt(&valid);
        if (valid && macro >= 0 && macro < NMACROS && m_doc)
            m_doc->OnMacro(ID_MACRO_A0 + macro);
    } else if (commandIdentifier == QLatin1String("ID_WINDOW_CASCADE")) {
        m_mdiArea->cascadeSubWindows();
    } else if (commandIdentifier == QLatin1String("ID_WINDOW_TILE_HORZ")) {
        theApp.m_flags0 &= ~DWORD(F0_AUTOARRANGEISVERT);
        TileWindows(false);
    } else if (commandIdentifier == QLatin1String("ID_WINDOW_TILE_VERT")) {
        theApp.m_flags0 |= F0_AUTOARRANGEISVERT;
        TileWindows(true);
    } else if (commandIdentifier == QLatin1String("ID_WINDOW_TILE_AUTO")) {
        theApp.m_flags0 ^= F0_AUTOARRANGEWNDS;
        AutoArrangeWindows();
    } else if (commandIdentifier == QLatin1String("ID_WINDOW_ARRANGE")) {
        ArrangeIcons();
    } else if (commandIdentifier == QLatin1String("ID_ACTIONS_SAY")) {
        if (m_doc) m_doc->OnActionsSay();
    } else if (commandIdentifier == QLatin1String("ID_ACTIONS_THINK")) {
        if (m_doc) m_doc->OnActionsThink();
    } else if (commandIdentifier == QLatin1String("ID_ACTIONS_WHISPER")) {
        if (m_doc) m_doc->OnActionsWhisper();
    } else if (commandIdentifier == QLatin1String("ID_SEND_ACTION")) {
        if (m_doc) m_doc->OnSendAction();
    } else if (commandIdentifier == QLatin1String("ID_SETFONT")) {
        if (m_doc) m_doc->OnSetfont();
    } else if (commandIdentifier == QLatin1String("ID_MEMBER_GETINFO")) {
        if (m_doc) m_doc->OnMemberGetinfo();
    } else if (commandIdentifier == QLatin1String("ID_MEMBER_GETCHAR")) {
        if (m_doc) m_doc->OnGetComicCharacter();
    } else if (commandIdentifier == QLatin1String("ID_MEMBER_IGNORE")) {
        if (m_doc) m_doc->OnMemberIgnore();
    } else if (commandIdentifier
               == QLatin1String("ID_ADDTONOTIFICATIONS")) {
        if (m_doc) m_doc->OnAddToNotifs();
    } else if (commandIdentifier == QLatin1String("ID_GETIDENTITY")) {
        if (m_doc) m_doc->OnGetidentity();
    } else if (commandIdentifier == QLatin1String("ID_GET_VERSION")) {
        if (m_doc) m_doc->OnGetVersion();
    } else if (commandIdentifier == QLatin1String("ID_PING_USER")) {
        if (m_doc) m_doc->OnPingUser();
    } else if (commandIdentifier == QLatin1String("ID_GET_LOCALTIME")) {
        if (m_doc) m_doc->OnGetLocaltime();
    } else if (commandIdentifier == QLatin1String("ID_WHISPERBOX_MLIST")) {
        if (m_doc) m_doc->OnWhisperboxMlist();
    } else if (commandIdentifier == QLatin1String("ID_SEND_EMAIL")) {
        if (m_doc) m_doc->OnSendEmail();
    } else if (commandIdentifier == QLatin1String("ID_VISIT_HOMEPAGE")) {
        if (m_doc) m_doc->OnVisitHomepage();
    } else if (commandIdentifier == QLatin1String("ID_ADMINISTRATOR_KICK")) {
        if (m_doc) m_doc->OnAdministratorKick();
    } else if (commandIdentifier == QLatin1String("ID_ADMIN_BAN")) {
        if (m_doc) m_doc->OnAdminBan();
    } else if (commandIdentifier == QLatin1String("ID_ADMIN_BGRNDSYNC")) {
        if (m_doc) m_doc->OnAdminBgrndsync();
    } else if (commandIdentifier == QLatin1String("ID_INVITE")) {
        if (m_doc) m_doc->OnInvite();
    } else if (commandIdentifier == QLatin1String("ID_MAKEADMIN")) {
        if (m_doc) m_doc->OnMakeadmin();
    } else if (commandIdentifier == QLatin1String("ID_MAKESPEAKER")) {
        if (m_doc) m_doc->OnMakespeaker();
    } else if (commandIdentifier == QLatin1String("ID_MAKESPECTATOR")) {
        if (m_doc) m_doc->OnMakespectator();
    } else if (commandIdentifier == QLatin1String("ID_CHANNELPROPS")) {
        if (m_doc) m_doc->OnChannelprops();
    } else if (commandIdentifier == QLatin1String("ID_SETCOLOR")) {
        if (m_doc) m_doc->OnSetColor();
    } else if (commandIdentifier == QLatin1String("ID_SWITCHBOLD")) {
        if (m_doc) m_doc->OnSwitchBold();
    } else if (commandIdentifier == QLatin1String("ID_SWITCHITALIC")) {
        if (m_doc) m_doc->OnSwitchItalic();
    } else if (commandIdentifier == QLatin1String("ID_SWITCHUNDERLINED")) {
        if (m_doc) m_doc->OnSwitchUnderlined();
    } else if (commandIdentifier == QLatin1String("ID_SWITCHFIXEDPITCH")) {
        if (m_doc) m_doc->OnSwitchFixedPitch();
    } else if (commandIdentifier == QLatin1String("ID_SWITCHSYMBOL")) {
        if (m_doc) m_doc->OnSwitchSymbol();
    } else if (commandIdentifier == QLatin1String("ID_APP_ABOUT")) {
        theApp.OnAppAbout();
    } else if (commandIdentifier == QLatin1String("ID_HELP_FREESTUFF")) {
        theApp.OnHelpFreestuff();
    } else if (commandIdentifier == QLatin1String("ID_HELP_PRODUCTNEWS")) {
        theApp.OnHelpProductnews();
    } else if (commandIdentifier == QLatin1String("ID_HELP_FAQ")) {
        theApp.OnHelpFaq();
    } else if (commandIdentifier == QLatin1String("ID_HELP_ONLINESUPPORT")) {
        theApp.OnHelpOnlineSupport();
    } else if (commandIdentifier == QLatin1String("ID_HELP_BESTOFWEB")) {
        theApp.OnHelpBestofWeb();
    } else if (commandIdentifier == QLatin1String("ID_HELP_SEARCHTHEWEB")) {
        theApp.OnHelpSearchtheWeb();
    } else if (commandIdentifier == QLatin1String("ID_HELP_MSHOMEPAGE")) {
        theApp.OnHelpMsHomepage();
    } else if (m_doc) {
        if (commandIdentifier == QLatin1String("ID_EDIT_UNDO"))
            m_doc->OnEditUndo();
        else if (commandIdentifier == QLatin1String("ID_EDIT_CUT"))
            m_doc->OnEditCut();
        else if (commandIdentifier == QLatin1String("ID_EDIT_COPY"))
            m_doc->OnEditCopy();
        else if (commandIdentifier == QLatin1String("ID_EDIT_PASTE"))
            m_doc->OnEditPaste();
        else if (commandIdentifier == QLatin1String("ID_EDIT_DELETE"))
            m_doc->OnEditDelete();
        else if (commandIdentifier == QLatin1String("ID_EDIT_SELECTALL"))
            m_doc->OnEditSelectAll();
    }
    updateCommandUi();
}
