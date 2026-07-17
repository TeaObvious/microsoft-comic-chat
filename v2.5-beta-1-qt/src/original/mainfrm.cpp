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
#include "tabbar.h"
#include "utils.h"
#include "originalassets.h"
#include "userinfo.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMimeData>
#include <QShortcut>
#include <QStatusBar>
#include <QTextEdit>
#include <QtPrintSupport/QPrinter>

#include <algorithm>
#include <utility>

namespace {
QString commandStatusText(const QString& commandIdentifier)
{
    return originalResourceString(commandIdentifier).section(QLatin1Char('\n'), 0, 0);
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
        QStringLiteral("ID_MOTD"),
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

void setActionsEnabled(const QHash<QString, QList<QAction*>>& actions,
                       const QString& commandIdentifier, bool enabled)
{
    for (QAction* action : actions.value(commandIdentifier)) action->setEnabled(enabled);
}

void setActionsChecked(const QHash<QString, QList<QAction*>>& actions,
                       const QString& commandIdentifier, bool checked)
{
    for (QAction* action : actions.value(commandIdentifier)) {
        action->setCheckable(true);
        action->setChecked(checked);
    }
}

bool hasComicSelection(CChatDoc* document)
{
    if (!document) return false;
    int index = -1;
    while (CUserInfo* pui = document->GetNextSelectedMember(index)) {
        if (pui->IsComicUser()) return true;
    }
    return false;
}

bool ignoreSelectionState(CChatDoc* document, bool* allIgnored)
{
    bool enabled = false;
    bool ignored = true;
    if (document) {
        int index = -1;
        while (CUserInfo* pui = document->GetNextSelectedMember(index)) {
            if (pui->IsSelf()) continue;
            enabled = true;
            if (!pui->Ignored()) {
                ignored = false;
                break;
            }
        }
    }
    if (allIgnored) *allIgnored = enabled && ignored;
    return enabled;
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

    auto* frame = new CChildFrame(document, ownsDocument, m_mdiArea);
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
        if (theApp.m_flags1 & F1_MAXMDI) frame->showMaximized();
        else frame->show();
        if (activate) frame->ActivateFrame();
    } else {
        frame->hide();
    }
    AutoArrangeWindows();
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
        m_doc = nullptr;
        m_chatView = nullptr;
        SetChatDoc(nullptr);
        SetStatusPaneString(1, QString());
        updateCommandUi();
        return;
    }
    m_doc = frame->GetDocument();
    m_chatView = frame->GetChatView();
    SetChatDoc(m_doc);
    if (m_doc) {
        m_doc->ResetStatus(true, true);
        const int tab = m_wndTabBar->FindTabNum(m_doc);
        if (tab >= 0 && m_wndTabBar->TabControl()->currentIndex() != tab)
            m_wndTabBar->TabControl()->setCurrentIndex(tab);
    }
    updateCommandUi();
}

void CMainFrame::UpdateDocumentTitle(CChatDoc* document,
                                     const QString& title)
{
    if (!document) return;
    if (CChildFrame* frame = m_childFrames.value(document))
        frame->SetDocumentTitle(title);
    const int tab = m_wndTabBar->FindTabNum(document);
    if (tab >= 0) m_wndTabBar->DelMDITab(tab);
    if (!document->m_bStatusView
        || (theApp.m_flags0 & F0_SHOWSTATUSWINDOW)) {
        m_wndTabBar->AddMDITab(title, document, document == m_doc);
    }
}

void CMainFrame::ShowStatusWindow(bool show)
{
    if (!m_statusDoc) return;
    CChildFrame* frame = m_childFrames.value(m_statusDoc);
    if (!frame) return;
    if (show) {
        theApp.m_flags0 |= F0_SHOWSTATUSWINDOW;
        if (m_wndTabBar->FindTabNum(m_statusDoc) < 0)
            m_wndTabBar->AddMDITab(m_statusDoc->GetTitle(), m_statusDoc, true);
        frame->ActivateFrame();
    } else {
        theApp.m_flags0 &= ~DWORD(F0_SHOWSTATUSWINDOW);
        const int tab = m_wndTabBar->FindTabNum(m_statusDoc);
        if (tab >= 0) m_wndTabBar->DelMDITab(tab);
        frame->hide();
        for (QMdiSubWindow* candidate : m_mdiArea->subWindowList(
                 QMdiArea::ActivationHistoryOrder)) {
            if (candidate != frame && !candidate->isHidden()) {
                m_mdiArea->setActiveSubWindow(candidate);
                break;
            }
        }
    }
    AutoArrangeWindows();
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
    TileWindows((theApp.m_flags0 & F0_AUTOARRANGEISVERT) != 0);
}

QAction* CMainFrame::addCommand(QMenu* menu, const QString& text,
                                const QString& commandIdentifier)
{
    QAction* action = menu->addAction(text);
    action->setData(commandIdentifier);
    action->setStatusTip(commandStatusText(commandIdentifier));
    action->setCheckable(isCheckCommand(commandIdentifier));
    connect(action, &QAction::triggered, this, [this, commandIdentifier] {
        executeCommand(commandIdentifier);
    });
    m_commandActions[commandIdentifier].append(action);
    return action;
}

QAction* CMainFrame::addToolCommand(CCoolToolBarEx* bar,
                                    const QString& commandIdentifier,
                                    const QIcon& icon)
{
    QAction* action = bar->addAction(icon, commandToolTip(commandIdentifier));
    action->setData(commandIdentifier);
    action->setToolTip(commandToolTip(commandIdentifier));
    action->setStatusTip(commandStatusText(commandIdentifier));
    action->setCheckable(isCheckCommand(commandIdentifier));
    connect(action, &QAction::triggered, this, [this, commandIdentifier] {
        executeCommand(commandIdentifier);
    });
    m_commandActions[commandIdentifier].append(action);
    return action;
}

void CMainFrame::appendMenuItems(QMenu* menu, const QList<OriginalMenuItem>& items)
{
    for (const OriginalMenuItem& item : items) {
        if (item.type == OriginalMenuItemType::Separator) {
            menu->addSeparator();
        } else if (item.type == OriginalMenuItemType::Popup) {
            QMenu* popup = menu->addMenu(item.text);
            connect(popup, &QMenu::aboutToShow, this, &CMainFrame::updateCommandUi);
            appendMenuItems(popup, item.children);
            for (const OriginalMenuItem& child : item.children) {
                if (child.commandIdentifier == QLatin1String("ID_DEFINE_MACRO")) {
                    m_macroMenus.append(popup);
                    break;
                }
            }
        } else {
            QAction* action = addCommand(menu, item.text, item.commandIdentifier);
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
            const QString command = action->data().toString();
            if (!command.isEmpty()) m_commandActions[command].removeAll(action);
            menu->removeAction(action);
            delete action;
        }
        BOOL present = FALSE;
        for (INT macro = 0; macro < NMACROS; ++macro) {
            if (!theApp.m_macros[macro].m_bDefined) continue;
            if (!present) {
                menu->addSeparator();
                present = TRUE;
            }
            const QString command = QStringLiteral("ID_MACRO_A%1").arg(macro);
            QAction* action = addCommand(menu,
                theApp.m_macros[macro].m_strName, command);
            action->setShortcut(QKeySequence(
                Qt::ALT | static_cast<Qt::Key>(Qt::Key_0 + macro)));
            action->setShortcutContext(Qt::ApplicationShortcut);
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
        connect(menu, &QMenu::aboutToShow, this, &CMainFrame::updateCommandUi);
        appendMenuItems(menu, item.children);
    }
}

void CMainFrame::createAccelerators()
{
    for (const OriginalAccelerator& accelerator : originalAcceleratorResource(
             QStringLiteral("IDR_MAINFRAME"))) {
        const QKeySequence sequence = acceleratorSequence(accelerator);
        if (sequence.isEmpty()) continue;
        auto* shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::ApplicationShortcut);
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
        });
    if (!theApp.m_pbCoolBarState.isEmpty())
        m_wndToolBar->LoadStateFromBuffer(theApp.m_pbCoolBarState);
}

void CMainFrame::createStatusBar()
{
    m_status0 = new QLabel(originalResourceString(QStringLiteral("ID_DISCONNECTED")), this);
    QString memberText = originalResourceString(QStringLiteral("ID_USER_PLURAL"));
    memberText.replace(QStringLiteral("%1"), QStringLiteral("0"));
    m_status1 = new QLabel(memberText, this);
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
    if (pane == 0 && m_status0) m_status0->setText(text);
    else if (pane == 1 && m_status1) m_status1->setText(text);
}

bool CMainFrame::commandIsPorted(const QString& commandIdentifier) const
{
    if (commandIdentifier.startsWith(QLatin1String("ID_MACRO_A"))) {
        bool valid = false;
        const INT macro = commandIdentifier.mid(10).toInt(&valid);
        return valid && macro >= 0 && macro < NMACROS;
    }
    static const QStringList ported = {
        QStringLiteral("ID_APP_EXIT"),
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
        QStringLiteral("ID_MEMBER_GETINFO"),
        QStringLiteral("ID_MEMBER_IGNORE"),
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
        QStringLiteral("ID_MAKEADMIN"),
        QStringLiteral("ID_MAKESPEAKER"),
        QStringLiteral("ID_MAKESPECTATOR"),
        QStringLiteral("ID_INVITE")
    };
    return ported.contains(commandIdentifier);
}

void CMainFrame::updateCommandUi()
{
    for (auto iterator = m_commandActions.begin(); iterator != m_commandActions.end(); ++iterator)
        for (QAction* action : iterator.value()) action->setEnabled(commandIsPorted(iterator.key()));

    const ConnectionStatus status = m_doc ? m_doc->GetConnectionStatus() : CX_DISCONNECTED;
    const ConnectionStatus serverStatus = GetIrcProto()
        ? GetIrcProto()->GetConnectionStatus() : CX_DISCONNECTED;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SESSION_CONNECT"),
                      serverStatus == CX_DISCONNECTED);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SESSION_DISCONNECT"),
                      serverStatus != CX_DISCONNECTED);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SESSION_NEWROOM"),
                      serverStatus == CX_INCHANNEL
                          || serverStatus == CX_NOCHANNEL);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_ROOM_CREATEROOM"),
                      serverStatus == CX_INCHANNEL
                          || serverStatus == CX_NOCHANNEL);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SESSION_LEAVE"),
                      m_doc && status == CX_INCHANNEL);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MOTD"),
                      (serverStatus == CX_INCHANNEL
                       || serverStatus == CX_NOCHANNEL)
                          && !theApp.m_bDisableMOTD);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_AWAY_TOGGLE"),
                      serverStatus == CX_INCHANNEL
                          || serverStatus == CX_NOCHANNEL);
    const bool canSearch = !theApp.m_bInSearch
        && (serverStatus == CX_INCHANNEL || serverStatus == CX_NOCHANNEL);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_CHATROOM_LIST"),
                      canSearch);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_USER_LIST"),
                      canSearch);
    setActionsChecked(m_commandActions, QStringLiteral("ID_AWAY_TOGGLE"),
                      theApp.m_bAway);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_FILE_CLOSE"),
                      m_doc != nullptr);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_FILE_PRINT"),
                      m_doc != nullptr && m_chatView != nullptr);
    setActionsEnabled(m_commandActions,
                      QStringLiteral("ID_FILE_PRINT_SETUP"), true);

    const bool comicView = m_doc && m_doc->m_bComicView;
    const bool statusView = m_doc && m_doc->m_bStatusView;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_COMICS"),
                      !statusView && (!m_doc || !m_doc->m_proto
                          || !(m_doc->m_proto->m_dwModes & CM_NOFORMAT)));
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_TEXT"),
                      !statusView);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_COMICS"), comicView);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_TEXT"), !comicView);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_ICON"), comicView);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_LIST"),
                      comicView && status == CX_INCHANNEL);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_ICON"),
                      comicView && m_doc && m_doc->m_bIconMembers);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_LIST"),
                      !m_doc || !m_doc->m_bIconMembers);

    setActionsEnabled(m_commandActions, QStringLiteral("ID_MEMBER_GETINFO"),
                      hasComicSelection(m_doc));
    bool allIgnored = false;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MEMBER_IGNORE"),
                      ignoreSelectionState(m_doc, &allIgnored));
    setActionsChecked(m_commandActions, QStringLiteral("ID_MEMBER_IGNORE"),
                      allIgnored);
    const bool selectedInRoom = m_doc && status == CX_INCHANNEL
        && m_doc->SelectedMemberCount() > 0;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_GETIDENTITY"),
                      selectedInRoom);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_GET_VERSION"),
                      selectedInRoom);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_PING_USER"),
                      selectedInRoom);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_GET_LOCALTIME"),
                      selectedInRoom);
    CUserInfo* singleMember = m_doc ? m_doc->GetSingleSelectedMember()
                                    : nullptr;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SEND_EMAIL"),
                      singleMember && !singleMember->IsSelf()
                          && singleMember->IsComicUser());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VISIT_HOMEPAGE"),
                      singleMember && singleMember->IsComicUser());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_WHISPERBOX_MLIST"),
                      singleMember && !singleMember->IsSelf());
    setActionsEnabled(m_commandActions,
                      QStringLiteral("ID_ADMINISTRATOR_KICK"),
                      singleMember && !singleMember->IsSelf());
    const int selectedMembers = m_doc && m_doc->m_memberList
        ? m_doc->SelectedMemberCount() : 2;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_ADMIN_BAN"),
                      selectedMembers < 2
                          && (!singleMember || !singleMember->IsSelf()));
    setActionsEnabled(m_commandActions, QStringLiteral("ID_INVITE"),
                      bCanInvite());
    const bool canSetRole = singleMember && m_doc && m_doc->m_puiSelf
        && m_doc->m_puiSelf->IsOperator() && status == CX_INCHANNEL;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MAKEADMIN"),
                      canSetRole);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MAKESPEAKER"),
                      canSetRole);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_MAKESPECTATOR"),
                      canSetRole && m_doc->m_proto
                          && (m_doc->m_proto->m_dwModes & CM_MODERATED));
    setActionsChecked(m_commandActions, QStringLiteral("ID_MAKEADMIN"),
                      singleMember && singleMember->IsOperator());
    setActionsChecked(m_commandActions, QStringLiteral("ID_MAKESPEAKER"),
                      singleMember && singleMember->IsSpeaker()
                          && !singleMember->IsOperator());
    setActionsChecked(m_commandActions, QStringLiteral("ID_MAKESPECTATOR"),
                      singleMember && singleMember->IsSpectator());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_CHANNELPROPS"),
                      m_doc && status == CX_INCHANNEL && g_puiSelf
                          && m_doc->m_puiSelf
                          && !m_doc->m_allChannelPuis.isEmpty());

    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_TOOLBAR_MAIN"),
                      (theApp.m_iShowBars & SB_TOOLBAR_MAIN) != 0);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_TOOLBAR_MEMBER"),
                      (theApp.m_iShowBars & SB_TOOLBAR_MEMBER) != 0);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_TOOLBAR_TEXT"),
                      (theApp.m_iShowBars & SB_TOOLBAR_TEXT) != 0);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_TABBAR"),
                      m_wndTabBar && m_wndTabBar->isVisible());
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_STATUS_BAR"),
                      statusBar()->isVisible());
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_STATUSWINDOW"),
                      (theApp.m_flags0 & F0_SHOWSTATUSWINDOW) != 0);
    setActionsChecked(m_commandActions, QStringLiteral("ID_VIEW_LOGINNOTIFS"),
                      theApp.m_bLoginNotifsShown);
    const bool automationEnabled = !m_doc
        || m_doc->GetConnectionStatus() != CX_CONNECTING;
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_AUTOMATIONS"),
                      automationEnabled);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_VIEW_OPTIONS"),
                      automationEnabled);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_DEFINE_MACRO"),
                      automationEnabled);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_SETFONT"),
                      m_doc && !statusView && !comicView);
    for (INT macro = 0; macro < NMACROS; ++macro) {
        const QString command = QStringLiteral("ID_MACRO_A%1").arg(macro);
        setActionsEnabled(m_commandActions, command,
            m_doc && m_doc->OnUpdateMacro(ID_MACRO_A0 + macro));
    }
    setActionsChecked(m_commandActions, QStringLiteral("ID_WINDOW_TILE_AUTO"),
                      (theApp.m_flags0 & F0_AUTOARRANGEWNDS) != 0);

    QTextEdit* edit = qobject_cast<QTextEdit*>(QApplication::focusWidget());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_UNDO"),
                      edit && !edit->isReadOnly() && edit->document()->isUndoAvailable());
    const bool hasSelection = edit && edit->textCursor().hasSelection();
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_CUT"),
                      hasSelection && !edit->isReadOnly());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_COPY"), hasSelection);
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_DELETE"),
                      hasSelection && !edit->isReadOnly());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_PASTE"),
                      edit && !edit->isReadOnly()
                          && QApplication::clipboard()->mimeData()->hasText());
    setActionsEnabled(m_commandActions, QStringLiteral("ID_EDIT_SELECTALL"), edit != nullptr);
}

void CMainFrame::executeCommand(const QString& commandIdentifier)
{
    if (!commandIsPorted(commandIdentifier)) return;

    if (commandIdentifier == QLatin1String("ID_APP_EXIT")) {
        qApp->quit();
    } else if (commandIdentifier == QLatin1String("ID_FILE_NEW")) {
        CreateNewDocument();
    } else if (commandIdentifier == QLatin1String("ID_FILE_CLOSE")) {
        if (QMdiSubWindow* active = m_mdiArea->activeSubWindow()) active->close();
    } else if (commandIdentifier == QLatin1String("ID_FILE_PRINT")) {
        if (m_chatView) m_chatView->OnFilePrint(m_printer.get(), FALSE);
    } else if (commandIdentifier == QLatin1String("ID_FILE_PRINT_SETUP")) {
        theApp.OnFilePrintSetup(m_printer.get(), this);
    } else if (commandIdentifier == QLatin1String("ID_SESSION_CONNECT")) {
        onConnect();
    } else if (commandIdentifier == QLatin1String("ID_SESSION_DISCONNECT")) {
        if (GetIrcProto()) GetIrcProto()->Disconnect();
    } else if (commandIdentifier == QLatin1String("ID_SESSION_NEWROOM")) {
        ChatSwitchChannel();
    } else if (commandIdentifier == QLatin1String("ID_ROOM_CREATEROOM")) {
        ChatCreateRoom(g_enterInfo);
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
        if (m_chatView && (!m_doc || !m_doc->m_bComicView)) m_chatView->CreateComicView(true);
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TEXT")) {
        if (m_chatView && m_doc && m_doc->m_bComicView) m_chatView->CreateTextView(true);
    } else if (commandIdentifier == QLatin1String("ID_VIEW_LIST")) {
        if (m_doc) m_doc->OnViewListAux();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_ICON")) {
        if (m_doc) m_doc->OnViewIcon();
    } else if (commandIdentifier == QLatin1String("ID_CLEAR_HISTORY")) {
        if (m_doc) m_doc->OnClearHistory();
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TOOLBAR_MAIN")) {
        if (m_wndToolBar) m_wndToolBar->ToggleBar(CHAT_TOOLBAR_MAIN);
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TOOLBAR_MEMBER")) {
        if (m_wndToolBar) m_wndToolBar->ToggleBar(CHAT_TOOLBAR_MEMBER);
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TOOLBAR_TEXT")) {
        if (m_wndToolBar) m_wndToolBar->ToggleBar(CHAT_TOOLBAR_TEXT);
    } else if (commandIdentifier == QLatin1String("ID_VIEW_TABBAR")) {
        if (m_wndTabBar) {
            const bool show = !m_wndTabBar->isVisible();
            m_wndTabBar->setVisible(show);
            if (show) theApp.m_flags1 |= F1_SHOWTABBAR;
            else theApp.m_flags1 &= ~DWORD(F1_SHOWTABBAR);
        }
    } else if (commandIdentifier == QLatin1String("ID_VIEW_STATUS_BAR")) {
        const bool show = !statusBar()->isVisible();
        statusBar()->setVisible(show);
        if (show) theApp.m_iShowBars |= SB_STATUSBAR;
        else theApp.m_iShowBars &= ~SB_STATUSBAR;
    } else if (commandIdentifier == QLatin1String("ID_VIEW_STATUSWINDOW")) {
        ShowStatusWindow((theApp.m_flags0 & F0_SHOWSTATUSWINDOW) == 0);
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
    } else if (commandIdentifier == QLatin1String("ID_ACTIONS_SAY")) {
        if (GetSay()) GetSay()->OnActionsSay();
    } else if (commandIdentifier == QLatin1String("ID_ACTIONS_THINK")) {
        if (GetSay()) GetSay()->OnActionsThink();
    } else if (commandIdentifier == QLatin1String("ID_ACTIONS_WHISPER")) {
        if (GetSay()) GetSay()->OnActionsWhisper();
    } else if (commandIdentifier == QLatin1String("ID_SEND_ACTION")) {
        if (GetSay()) GetSay()->OnSendAction();
    } else if (commandIdentifier == QLatin1String("ID_SETFONT")) {
        if (m_doc && !m_doc->m_bComicView) m_doc->OnSetfont();
    } else if (commandIdentifier == QLatin1String("ID_MEMBER_GETINFO")) {
        if (m_doc && hasComicSelection(m_doc)) m_doc->OnMemberGetinfo();
    } else if (commandIdentifier == QLatin1String("ID_MEMBER_IGNORE")) {
        if (m_doc && ignoreSelectionState(m_doc, nullptr))
            m_doc->OnMemberIgnore();
    } else if (commandIdentifier == QLatin1String("ID_GETIDENTITY")) {
        if (m_doc && m_doc->GetConnectionStatus() == CX_INCHANNEL)
            m_doc->OnGetidentity();
    } else if (commandIdentifier == QLatin1String("ID_GET_VERSION")) {
        if (m_doc && m_doc->GetConnectionStatus() == CX_INCHANNEL)
            m_doc->OnGetVersion();
    } else if (commandIdentifier == QLatin1String("ID_PING_USER")) {
        if (m_doc && m_doc->GetConnectionStatus() == CX_INCHANNEL)
            m_doc->OnPingUser();
    } else if (commandIdentifier == QLatin1String("ID_GET_LOCALTIME")) {
        if (m_doc && m_doc->GetConnectionStatus() == CX_INCHANNEL)
            m_doc->OnGetLocaltime();
    } else if (commandIdentifier == QLatin1String("ID_WHISPERBOX_MLIST")) {
        CUserInfo* pui = m_doc ? m_doc->GetSingleSelectedMember() : nullptr;
        if (pui && !pui->IsSelf()) m_doc->OnWhisperboxMlist();
    } else if (commandIdentifier == QLatin1String("ID_SEND_EMAIL")) {
        if (m_doc) m_doc->OnSendEmail();
    } else if (commandIdentifier == QLatin1String("ID_VISIT_HOMEPAGE")) {
        if (m_doc) m_doc->OnVisitHomepage();
    } else if (commandIdentifier == QLatin1String("ID_ADMINISTRATOR_KICK")) {
        CUserInfo* pui = m_doc ? m_doc->GetSingleSelectedMember() : nullptr;
        if (pui && !pui->IsSelf()) m_doc->OnAdministratorKick();
    } else if (commandIdentifier == QLatin1String("ID_ADMIN_BAN")) {
        CUserInfo* pui = m_doc ? m_doc->GetSingleSelectedMember() : nullptr;
        const int selected = m_doc && m_doc->m_memberList
            ? m_doc->SelectedMemberCount() : 2;
        if (m_doc && selected < 2 && (!pui || !pui->IsSelf()))
            m_doc->OnAdminBan();
    } else if (commandIdentifier == QLatin1String("ID_INVITE")) {
        if (m_doc && bCanInvite()) m_doc->OnInvite();
    } else if (commandIdentifier == QLatin1String("ID_MAKEADMIN")) {
        if (m_doc) m_doc->OnMakeadmin();
    } else if (commandIdentifier == QLatin1String("ID_MAKESPEAKER")) {
        if (m_doc) m_doc->OnMakespeaker();
    } else if (commandIdentifier == QLatin1String("ID_MAKESPECTATOR")) {
        if (m_doc) m_doc->OnMakespectator();
    } else if (commandIdentifier == QLatin1String("ID_CHANNELPROPS")) {
        if (m_doc && m_doc->GetConnectionStatus() == CX_INCHANNEL
            && g_puiSelf && m_doc->m_puiSelf
            && !m_doc->m_allChannelPuis.isEmpty()) {
            m_doc->OnChannelprops();
        }
    } else if (commandIdentifier == QLatin1String("ID_SETCOLOR")) {
        if (GetSay()) GetSay()->SwitchSelectionFormat(wForeground);
    } else if (commandIdentifier == QLatin1String("ID_SWITCHBOLD")) {
        if (GetSay()) GetSay()->SwitchSelectionFormat(wBold);
    } else if (commandIdentifier == QLatin1String("ID_SWITCHITALIC")) {
        if (GetSay()) GetSay()->SwitchSelectionFormat(wItalic);
    } else if (commandIdentifier == QLatin1String("ID_SWITCHUNDERLINED")) {
        if (GetSay()) GetSay()->SwitchSelectionFormat(wUnderline);
    } else if (commandIdentifier == QLatin1String("ID_SWITCHFIXEDPITCH")) {
        if (GetSay()) GetSay()->SwitchSelectionFormat(wFixedPitch);
    } else if (commandIdentifier == QLatin1String("ID_SWITCHSYMBOL")) {
        if (GetSay()) GetSay()->SwitchSelectionFormat(wSymbol);
    } else if (QTextEdit* edit = qobject_cast<QTextEdit*>(QApplication::focusWidget())) {
        if (commandIdentifier == QLatin1String("ID_EDIT_UNDO")) edit->undo();
        else if (commandIdentifier == QLatin1String("ID_EDIT_CUT")) edit->cut();
        else if (commandIdentifier == QLatin1String("ID_EDIT_COPY")) edit->copy();
        else if (commandIdentifier == QLatin1String("ID_EDIT_PASTE")) edit->paste();
        else if (commandIdentifier == QLatin1String("ID_EDIT_DELETE")) {
            QTextCursor cursor = edit->textCursor();
            cursor.removeSelectedText();
        } else if (commandIdentifier == QLatin1String("ID_EDIT_SELECTALL")) {
            edit->selectAll();
        }
    }
    updateCommandUi();
}

void CMainFrame::onConnect()
{
    CSetupDlg dlg(this);
    if (dlg.exec() != QDialog::Accepted || !GetIrcProto()) return;
    GetIrcProto()->ConnectToServer(dlg.server(), dlg.nickname(), dlg.realName(),
                                   dlg.channel(), dlg.onConnectAction());
}
