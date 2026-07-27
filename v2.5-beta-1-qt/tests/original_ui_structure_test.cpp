#include "avatar.h"
#include "backdrop.h"
#include "bodycam.h"
#include "chat.h"
#include "chatdoc.h"
#include "chatview.h"
#include "childfrm.h"
#include "colordlg.h"
#include "doskey.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "memblst.h"
#include "originalassets.h"
#include "panel.h"
#include "proppage.h"
#include "protsupp.h"
#include "saywnd.h"
#include "setupdlg.h"
#include "spltchat.h"
#include "status.h"
#include "tabbar.h"
#include "textview.h"
#include "avatario.h"
#include "userinfo.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QDebug>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSet>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QTextCursor>
#include <QTimer>

#include <cmath>
#include <cstdlib>

namespace {
bool nearPercent(int part, int total, int expected)
{
    return total > 0 && std::abs(part * 100 - total * expected) <= total * 2;
}

QMenu* menuWithDirectCommand(QMenuBar* menuBar, const QString& command)
{
    if (!menuBar) return nullptr;
    for (QAction* rootAction : menuBar->actions()) {
        QMenu* menu = rootAction->menu();
        if (!menu) continue;
        for (QAction* action : menu->actions()) {
            if (action->data().toString() == command) return menu;
        }
    }
    return nullptr;
}

QAction* directCommand(QMenu* menu, const QString& command)
{
    if (!menu) return nullptr;
    for (QAction* action : menu->actions()) {
        if (action->data().toString() == command) return action;
    }
    return nullptr;
}

QStringList directCommands(QMenu* menu)
{
    QStringList commands;
    if (!menu) return commands;
    for (QAction* action : menu->actions()) {
        const QString command = action->data().toString();
        if (!command.isEmpty()) commands.append(command);
    }
    return commands;
}

QList<QAction*> registeredCommandActions(
    QObject* root, const QString& command = QString())
{
    QList<QAction*> result;
    if (!root) return result;
    for (QAction* action : root->findChildren<QAction*>()) {
        if (!action
            || !action->property("originalCommandClass").isValid()) {
            continue;
        }
        if (command.isEmpty() || action->data().toString() == command)
            result.append(action);
    }
    return result;
}

QAction* registeredCommandAction(QObject* root, const QString& command)
{
    const QList<QAction*> actions = registeredCommandActions(root, command);
    return actions.isEmpty() ? nullptr : actions.first();
}

bool registeredActionsHaveState(QObject* root, const QString& command,
                                bool enabled, bool checked = false,
                                bool compareChecked = false)
{
    const QList<QAction*> actions = registeredCommandActions(root, command);
    if (actions.isEmpty()) return false;
    for (QAction* action : actions) {
        if (action->isEnabled() != enabled) return false;
        if (compareChecked && action->isChecked() != checked) return false;
    }
    return true;
}

void collectMenuCommands(const QList<OriginalMenuItem>& items,
                         QStringList* commands)
{
    if (!commands) return;
    for (const OriginalMenuItem& item : items) {
        if (item.type == OriginalMenuItemType::Popup) {
            collectMenuCommands(item.children, commands);
        } else if (item.type == OriginalMenuItemType::Command
                   && !item.commandIdentifier.isEmpty()) {
            commands->append(item.commandIdentifier);
        }
    }
}

QString acceleratorPortableText(const OriginalAccelerator& accelerator)
{
    QStringList parts;
    if (accelerator.control) parts.append(QStringLiteral("Ctrl"));
    if (accelerator.alt) parts.append(QStringLiteral("Alt"));
    if (accelerator.shift) parts.append(QStringLiteral("Shift"));
    if (accelerator.key == QLatin1String("VK_BACK"))
        parts.append(QStringLiteral("Backspace"));
    else if (accelerator.key == QLatin1String("VK_DELETE"))
        parts.append(QStringLiteral("Del"));
    else if (accelerator.key == QLatin1String("VK_INSERT"))
        parts.append(QStringLiteral("Ins"));
    else
        parts.append(accelerator.key);
    return parts.join(QLatin1Char('+'));
}

QImage tabIconImage(CTabBar* tabBar, CChatDoc* document)
{
    if (!tabBar || !document) return {};
    const int tab = tabBar->FindTabNum(document);
    if (tab < 0) return {};
    return tabBar->TabControl()->tabIcon(tab)
        .pixmap(QSize(16, 16)).toImage();
}

class CountingIrcProto : public CIrcProto {
public:
    explicit CountingIrcProto(int* partCount)
        : m_partCount(partCount)
    {
    }

    void ChatPartChannel(CChatDoc*, bool) override
    {
        if (m_partCount) ++*m_partCount;
    }

private:
    int* m_partCount = nullptr;
};
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    theApp.InitializeFonts();

    if (originalDialogCaption(QStringLiteral("IDD_CHOOSECOLOR"))
            != QStringLiteral("Choose Color")) {
        return EXIT_FAILURE;
    }

    {
        CColorDlg colorDialog(static_cast<LONG>(RGB(255, 0, 0)));
        COLORREF selected = 0;
        if (!colorDialog.GetSelectedColorRGB(&selected)
            || selected != RGB(255, 0, 0)) {
            return EXIT_FAILURE;
        }
    }

    {
        CDosKey doskey;
        for (int index = 0; index < 65; ++index)
            doskey.bAppendEntry(QString::number(index), nullptr);
        CDWordArray* formatting = reinterpret_cast<CDWordArray*>(quintptr(1));
        if (doskey.StrGetPrevEntry(&formatting) != QStringLiteral("64")
            || formatting != nullptr) {
            return EXIT_FAILURE;
        }
        QString oldest;
        for (int index = 0; index < 63; ++index)
            oldest = doskey.StrGetPrevEntry(&formatting);
        if (oldest != QStringLiteral("1")) return EXIT_FAILURE;
    }

    {
        CSplitChatV splitter;
        splitter.addWidget(new QWidget(&splitter));
        splitter.addWidget(new QWidget(&splitter));
        splitter.resize(1000, 400);
        splitter.show();
        application.processEvents();
        const QList<int> sizes = splitter.sizes();
        const int total = sizes.value(0) + sizes.value(1);
        if (!nearPercent(sizes.value(0), total, 80)) return EXIT_FAILURE;
    }

    {
        CSplitChat splitter;
        splitter.addWidget(new QWidget(&splitter));
        splitter.addWidget(new QWidget(&splitter));
        splitter.resize(400, 1000);
        splitter.show();
        application.processEvents();
        const QList<int> sizes = splitter.sizes();
        const int total = sizes.value(0) + sizes.value(1);
        if (!nearPercent(sizes.value(1), total, 70)) return EXIT_FAILURE;
    }

    {
        CFixedSplitter splitter;
        splitter.addWidget(new QWidget(&splitter));
        splitter.addWidget(new QWidget(&splitter));
        splitter.resize(400, 500);
        splitter.show();
        application.processEvents();
        const QList<int> sizes = splitter.sizes();
        // Modern's DPI-unaware DpiScale(23) evaluates to exactly 23.
        if (splitter.SayMinimumPixels() != 23
            || sizes.size() != 2
            || sizes[1] != splitter.SayMinimumPixels()) {
            qWarning() << "fixed splitter" << sizes << splitter.SayMinimumPixels();
            return EXIT_FAILURE;
        }
    }

    {
        CSayWnd::SetDefaultButtons(SB_SAY | SB_THINK | SB_WHISPER | SB_ACTION | SB_SOUND);
        CSayWnd sayWindow;
        sayWindow.resize(400, 40);
        sayWindow.show();
        application.processEvents();
        CSayToolBar* bar = sayWindow.GetSayBar();
        if (!bar || sayWindow.m_cntBalloons != 5 || sayWindow.m_cxSayBar != 120
            || bar->actions().size() != 5
            || bar->actions()[0]->data().toString() != QStringLiteral("ID_ACTIONS_SAY")
            || bar->actions()[1]->data().toString() != QStringLiteral("ID_ACTIONS_THINK")
            || bar->actions()[2]->data().toString() != QStringLiteral("ID_ACTIONS_WHISPER")
            || bar->actions()[3]->data().toString() != QStringLiteral("ID_SEND_ACTION")
            || bar->actions()[4]->data().toString() != QStringLiteral("ID_PLAY_SOUND")
            || bar->actions()[4]->isEnabled()
            || sayWindow.GetSayEdit()->font().pixelSize() != qAbs(nFontHeight)
            || sayWindow.GetSayEdit()->geometry() != QRect(0, 0, 280, 40)
            || bar->geometry() != QRect(274, -3, 132, 29)) {
            qWarning() << "saybar" << sayWindow.m_cntBalloons << sayWindow.m_cxSayBar
                       << (bar ? bar->geometry() : QRect())
                       << sayWindow.GetSayEdit()->geometry();
            return EXIT_FAILURE;
        }

        const QImage sourceSayStrip(originalFileResourcePath(
            QStringLiteral("IDB_SAY_BAR"), QStringLiteral("BITMAP")));
        if (sourceSayStrip.height() != 17 || sourceSayStrip.width() < 4 * 17) {
            qWarning() << "say icon source dimensions" << sourceSayStrip.size();
            return EXIT_FAILURE;
        }
        for (int button = 0; button < 4; ++button) {
            const QImage icon = bar->actions()[button]->icon()
                .pixmap(QSize(17, 17), QIcon::Normal, QIcon::Off).toImage();
            if (icon.size() != QSize(17, 17)) return EXIT_FAILURE;
            int transparent = 0;
            int opaque = 0;
            for (int y = 0; y < 17; ++y) {
                for (int x = 0; x < 17; ++x) {
                    const bool sourceGray = sourceSayStrip.pixelColor(
                        button * 17 + x, y).rgb() == qRgb(192, 192, 192);
                    const int alpha = icon.pixelColor(x, y).alpha();
                    if ((sourceGray && alpha != 0)
                        || (!sourceGray && alpha == 0)) {
                        qWarning() << "say icon mask" << button << x << y
                                   << sourceGray << alpha;
                        return EXIT_FAILURE;
                    }
                    if (alpha == 0) ++transparent;
                    else ++opaque;
                }
            }
            if (transparent == 0 || opaque == 0) return EXIT_FAILURE;
        }

        CSayCtrl* edit = sayWindow.GetSayEdit();
        edit->setPlainText(QStringLiteral("AB"));
        QTextCursor cursor(edit->document());
        cursor.setPosition(0);
        cursor.setPosition(1, QTextCursor::KeepAnchor);
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        cursor.mergeCharFormat(bold);
        CDWordArray* formatting = PRGDWGetFormatting(
            edit, &sayWindow.m_fontText, edit->m_crTextColor);
        if (!formatting || formatting->GetSize() != 2
            || formatting->GetAt(0) != MAKELONG(wBold, 0)
            || formatting->GetAt(1) != MAKELONG(0, 1)) {
            qWarning() << "say formatting"
                       << (formatting ? formatting->GetSize() : -1);
            FreeAndNullFormatting(&formatting);
            return EXIT_FAILURE;
        }
        FreeAndNullFormatting(&formatting);
    }

    {
        CSayWnd::SetDefaultButtons(0);
        CSayWnd statusInput;
        statusInput.resize(400, 40);
        statusInput.show();
        application.processEvents();
        if (statusInput.GetSayBar() || statusInput.m_cntBalloons != 0
            || statusInput.m_cxSayBar != 0
            || statusInput.GetSayEdit()->geometry() != QRect(0, 0, 400, 40)) {
            return EXIT_FAILURE;
        }
    }

    theApp.InitVals();
    theApp.InitializeFonts();
    CUnitPanelPage::SetFonts(theApp.m_comicsFont, theApp.m_comicsColor);
    InitializeBackDrops();
    InitializeAvatars();
    LoadEmotionStrings();

    {
        CMainFrame emptyFrame;
        emptyFrame.RefreshCommandUi();
        const struct {
            const char* command;
            bool checked;
            bool compareChecked;
        } noDocumentStates[] = {
            {"ID_VIEW_COMICS", false, true},
            {"ID_VIEW_TEXT", false, true},
            {"ID_CLEAR_HISTORY", false, false},
            {"ID_SETFONT", false, false},
            {"ID_EDIT_SELECTALL", false, false},
        };
        for (const auto& state : noDocumentStates) {
            const QString command = QString::fromLatin1(state.command);
            if (registeredActionsHaveState(
                    &emptyFrame, command, false,
                    state.checked, state.compareChecked)) {
                continue;
            }
            qWarning() << "no-document command UI" << command;
            for (QAction* action :
                 registeredCommandActions(&emptyFrame, command)) {
                qWarning() << action << action->isEnabled()
                           << action->isCheckable()
                           << action->isChecked();
            }
            return EXIT_FAILURE;
        }
    }

    SetArtDir("artpack1");
    {
        CChatDoc artResetDocument;
        artResetDocument.m_bComicView = false;
        SetChatDoc(&artResetDocument);
        if (!ArtDirsOK()
            || QFileInfo(theApp.GetAvatarDir()).fileName().compare(
                   QStringLiteral("comicart"), Qt::CaseInsensitive) != 0
            || theApp.GetAvatarDir() != theApp.GetBackDropDir()) {
            qWarning() << "document default ArtDir reset"
                       << theApp.GetAvatarDir() << theApp.GetBackDropDir();
            return EXIT_FAILURE;
        }
        SetChatDoc(nullptr);
    }

    {
        CChatDoc forwardingDocument;
        SetChatDoc(&forwardingDocument);
        CSplitSay splitter;
        splitter.addWidget(new QWidget(&splitter));
        auto* sayWindow = new CSayWnd(&splitter);
        splitter.addWidget(sayWindow);
        forwardingDocument.m_sayWnd = sayWindow;
        splitter.resize(400, 200);
        splitter.show();
        application.processEvents();

        const QString sourceCharacter = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK")).left(1);
        if (sourceCharacter.isEmpty()) return EXIT_FAILURE;
        QKeyEvent splitterCharacter(QEvent::KeyPress, 0, Qt::NoModifier,
                                    sourceCharacter);
        splitterCharacter.ignore();
        QApplication::sendEvent(&splitter, &splitterCharacter);
        if (sayWindow->GetSayEdit()->toPlainText() != sourceCharacter) {
            qWarning() << "splitter character forwarding"
                       << sayWindow->GetSayEdit()->toPlainText();
            return EXIT_FAILURE;
        }

        sayWindow->GetSayEdit()->clear();
        CTabBarTabCtrl tabControl;
        QKeyEvent tabCharacter(QEvent::KeyPress, 0, Qt::NoModifier,
                               sourceCharacter);
        tabCharacter.ignore();
        QApplication::sendEvent(&tabControl, &tabCharacter);
        if (sayWindow->GetSayEdit()->toPlainText() != sourceCharacter) {
            qWarning() << "tab character forwarding"
                       << sayWindow->GetSayEdit()->toPlainText();
            return EXIT_FAILURE;
        }

        forwardingDocument.m_sayWnd = nullptr;
        SetChatDoc(nullptr);
    }

    CChatDoc roomB;
    CChatDoc status;
    CChatDoc roomA;
    status.m_bStatusView = true;
    CTabBar tabBar;
    const QString roomTitle = originalResourceString(
        QStringLiteral("IDR_MAINFRAME")).section(QLatin1Char('\n'), 1, 1);
    tabBar.AddMDITab(originalResourceString(QStringLiteral("IDS_STATUSTITLE")), &status, false);
    tabBar.AddMDITab(roomTitle + QStringLiteral("2"), &roomB, false);
    tabBar.AddMDITab(roomTitle + QStringLiteral("1"), &roomA, false);
    if (tabBar.height() != 29
        || tabBar.GetTabString(0) != originalResourceString(QStringLiteral("IDS_STATUSTITLE"))
        || tabBar.GetTabString(1) != roomTitle + QStringLiteral("1")
        || tabBar.GetTabString(2) != roomTitle + QStringLiteral("2")
        || tabBar.GetTabDoc(0) != &status) {
        qWarning() << "tabbar" << tabBar.height() << tabBar.GetTabString(0)
                   << tabBar.GetTabString(1) << tabBar.GetTabString(2);
        return EXIT_FAILURE;
    }

    const QImage sourceTabStrip(originalFileResourcePath(
        QStringLiteral("IDB_TABS"), QStringLiteral("BITMAP")));
    const QImage roomTabIcon = tabBar.TabControl()->tabIcon(1)
        .pixmap(QSize(16, 16)).toImage();
    if (sourceTabStrip.size() != QSize(64, 16)
        || roomTabIcon.size() != QSize(16, 16)) {
        qWarning() << "tab icon dimensions" << sourceTabStrip.size()
                   << roomTabIcon.size();
        return EXIT_FAILURE;
    }
    int transparentPixels = 0;
    int opaquePixels = 0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool sourceGreen = sourceTabStrip.pixelColor(x, y).rgb()
                == qRgb(0, 255, 0);
            const int alpha = roomTabIcon.pixelColor(x, y).alpha();
            if ((sourceGreen && alpha != 0) || (!sourceGreen && alpha == 0)) {
                qWarning() << "tab icon mask" << x << y << sourceGreen << alpha;
                return EXIT_FAILURE;
            }
            if (alpha == 0) ++transparentPixels;
            else ++opaquePixels;
        }
    }
    if (transparentPixels == 0 || opaquePixels == 0) return EXIT_FAILURE;

    CChatDoc document;
    SetChatDoc(&document);
    CMainFrame frame(&document);
    theApp.m_pMainWnd = &frame;
    frame.show();
    application.processEvents();
    frame.UpdateMacroMenu();

    {
        QStringList sourceCommands;
        collectMenuCommands(
            originalMenuResource(QStringLiteral("IDR_MAINFRAME")),
            &sourceCommands);
        for (const QString& toolbarIdentifier : {
                 QStringLiteral("IDR_MAINFRAME"),
                 QStringLiteral("IDR_USERTOOLBAR"),
                 QStringLiteral("IDR_TEXTTOOLBAR")}) {
            const OriginalToolbarResource toolbar =
                originalToolbarResource(toolbarIdentifier);
            for (const OriginalToolbarItem& item : toolbar.items) {
                if (!item.separator && !item.commandIdentifier.isEmpty())
                    sourceCommands.append(item.commandIdentifier);
            }
        }

        QHash<QString, int> sourceOccurrences;
        for (const QString& command : sourceCommands)
            ++sourceOccurrences[command];

        const QSet<QString> deferredCommands = {
            QStringLiteral("ID_FILE_CREATESHORTCUT"),
            QStringLiteral("ID_FAVORITES_ADDTOFAVORITES"),
            QStringLiteral("ID_FAVORITES_OPENFAVORITES"),
            QStringLiteral("ID_TURN_OFF_SOUNDS"),
            QStringLiteral("ID_PLAY_SOUND"),
            QStringLiteral("ID_START_NETMEETING"),
            QStringLiteral("ID_HELP_TOPICS"),
            QStringLiteral("ID_HELP_RELEASENOTES")
        };
        for (auto iterator = sourceOccurrences.cbegin();
             iterator != sourceOccurrences.cend(); ++iterator) {
            const QList<QAction*> actions =
                registeredCommandActions(&frame, iterator.key());
            const QString expectedClass = deferredCommands.contains(
                iterator.key())
                ? QStringLiteral("deferred")
                : QStringLiteral("active");
            if (actions.size() != iterator.value()) {
                qWarning() << "command occurrence" << iterator.key()
                           << actions.size() << iterator.value();
                return EXIT_FAILURE;
            }
            for (QAction* action : actions) {
                if (action->property("originalCommandClass").toString()
                        != expectedClass
                    || (expectedClass == QLatin1String("deferred")
                        && action->isEnabled())) {
                    qWarning() << "command classification"
                               << iterator.key()
                               << action->property(
                                      "originalCommandClass")
                                      .toString()
                               << action->isEnabled();
                    return EXIT_FAILURE;
                }
            }
        }
        for (const QString& command : {
                 QStringLiteral("ID_FILE_OPEN"),
                 QStringLiteral("ID_FILE_SAVE"),
                 QStringLiteral("ID_FILE_SAVE_AS")}) {
            const QList<QAction*> actions =
                registeredCommandActions(&frame, command);
            if (actions.isEmpty()) {
                qWarning() << "missing active file command" << command;
                return EXIT_FAILURE;
            }
            for (QAction* action : actions) {
                if (action->property("originalCommandClass").toString()
                        != QLatin1String("active")
                    || !action->isEnabled()) {
                    qWarning() << "inactive file command" << command
                               << action->property(
                                      "originalCommandClass")
                                      .toString()
                               << action->isEnabled();
                    return EXIT_FAILURE;
                }
            }
        }
        for (QAction* action : registeredCommandActions(&frame)) {
            if (action->property("originalCommandClass").toString()
                == QLatin1String("unresolved")) {
                qWarning() << "unresolved reachable command"
                           << action->data().toString();
                return EXIT_FAILURE;
            }
        }

        const QList<OriginalAccelerator> sourceAccelerators =
            originalAcceleratorResource(QStringLiteral("IDR_MAINFRAME"));
        const QList<QShortcut*> shortcuts =
            frame.findChildren<QShortcut*>(
                QString(), Qt::FindDirectChildrenOnly);
        if (shortcuts.size() != sourceAccelerators.size()) {
            qWarning() << "accelerator count" << shortcuts.size()
                       << sourceAccelerators.size();
            return EXIT_FAILURE;
        }
        QStringList expectedAccelerators;
        for (const OriginalAccelerator& accelerator : sourceAccelerators) {
            QString classification = QStringLiteral("active");
            if (deferredCommands.contains(accelerator.commandIdentifier))
                classification = QStringLiteral("deferred");
            else if (accelerator.commandIdentifier
                     == QLatin1String("ID_VIEW_MACROS"))
                classification = QStringLiteral("no-handler");
            expectedAccelerators.append(
                accelerator.commandIdentifier + QLatin1Char('|')
                + acceleratorPortableText(accelerator)
                + QLatin1Char('|') + classification);
        }
        for (QShortcut* shortcut : shortcuts) {
            if (shortcut->context() != Qt::WindowShortcut) {
                qWarning() << "accelerator scope" << shortcut->key();
                return EXIT_FAILURE;
            }
            const QString actual =
                shortcut->property(
                    "originalCommandIdentifier").toString()
                + QLatin1Char('|')
                + shortcut->key().toString(QKeySequence::PortableText)
                + QLatin1Char('|')
                + shortcut->property(
                    "originalCommandClass").toString();
            const int match = expectedAccelerators.indexOf(actual);
            if (match < 0) {
                qWarning() << "accelerator identity" << actual
                           << expectedAccelerators;
                return EXIT_FAILURE;
            }
            expectedAccelerators.removeAt(match);
        }
        if (!expectedAccelerators.isEmpty()) return EXIT_FAILURE;
    }

    int comicFontActions = 0;
    for (QAction* action : frame.findChildren<QAction*>()) {
        if (action->data().toString() != QLatin1String("ID_SETFONT"))
            continue;
        ++comicFontActions;
        if (!action->isEnabled()) {
            qWarning() << "comic font command disabled";
            return EXIT_FAILURE;
        }
    }
    if (comicFontActions == 0) return EXIT_FAILURE;

    if (!frame.GetMDIArea()
        || frame.GetMDIArea()->subWindowList().size() != 1
        || frame.GetActiveDocument() != &document
        || document.GetTitle() != roomTitle + QStringLiteral("1")) {
        qWarning() << "mdi initial" << frame.GetActiveDocument()
                   << document.GetTitle();
        return EXIT_FAILURE;
    }

    {
        auto* sayWindow = dynamic_cast<CSayWnd*>(document.m_sayWnd);
        if (!sayWindow || !sayWindow->GetSayEdit()) return EXIT_FAILURE;
        CSayCtrl* sayEdit = sayWindow->GetSayEdit();

        QTextEdit unrelated(&frame);
        unrelated.setPlainText(QStringLiteral("unrelated"));
        unrelated.setGeometry(0, 0, 120, 30);
        unrelated.show();
        unrelated.setFocus();
        application.processEvents();
        frame.RefreshCommandUi();
        for (const QString& command : {
                 QStringLiteral("ID_EDIT_UNDO"),
                 QStringLiteral("ID_EDIT_CUT"),
                 QStringLiteral("ID_EDIT_COPY"),
                 QStringLiteral("ID_EDIT_PASTE"),
                 QStringLiteral("ID_EDIT_DELETE"),
                 QStringLiteral("ID_EDIT_SELECTALL")}) {
            if (!registeredActionsHaveState(&frame, command, false)) {
                qWarning() << "unrelated edit received command UI"
                           << command;
                return EXIT_FAILURE;
            }
        }
        const QString unrelatedText = unrelated.toPlainText();
        if (QAction* clear = registeredCommandAction(
                &frame, QStringLiteral("ID_EDIT_DELETE"))) {
            clear->trigger();
        }
        if (unrelated.toPlainText() != unrelatedText)
            return EXIT_FAILURE;
        unrelated.hide();

        sayEdit->clear();
        sayEdit->insertPlainText(QStringLiteral("AB"));
        sayEdit->setFocus();
        application.processEvents();
        QTextCursor sayCursor = sayEdit->textCursor();
        sayCursor.setPosition(0);
        sayCursor.setPosition(1, QTextCursor::KeepAnchor);
        sayEdit->setTextCursor(sayCursor);
        QApplication::clipboard()->setText(QStringLiteral("paste-source"));
        frame.RefreshCommandUi();
        for (const QString& command : {
                 QStringLiteral("ID_EDIT_UNDO"),
                 QStringLiteral("ID_EDIT_CUT"),
                 QStringLiteral("ID_EDIT_COPY"),
                 QStringLiteral("ID_EDIT_PASTE"),
                 QStringLiteral("ID_EDIT_DELETE"),
                 QStringLiteral("ID_EDIT_SELECTALL")}) {
            if (!registeredActionsHaveState(&frame, command, true)) {
                qWarning() << "Say edit command UI" << command;
                return EXIT_FAILURE;
            }
        }
        registeredCommandAction(
            &frame, QStringLiteral("ID_EDIT_COPY"))->trigger();
        if (QApplication::clipboard()->text() != QStringLiteral("A")) {
            qWarning() << "Say edit copy target"
                       << QApplication::clipboard()->text()
                       << sayEdit->textCursor().selectedText()
                       << (frame.GetCommandFocusWidget() == sayEdit);
            return EXIT_FAILURE;
        }

        registeredCommandAction(
            &frame, QStringLiteral("ID_SWITCHBOLD"))->trigger();
        frame.RefreshCommandUi();
        if (!registeredActionsHaveState(
                &frame, QStringLiteral("ID_SWITCHBOLD"),
                true, true, true)) {
            qWarning() << "format command copies";
            return EXIT_FAILURE;
        }

        registeredCommandAction(
            &frame, QStringLiteral("ID_EDIT_CUT"))->trigger();
        if (sayEdit->toPlainText() != QStringLiteral("B"))
            return EXIT_FAILURE;
        registeredCommandAction(
            &frame, QStringLiteral("ID_EDIT_UNDO"))->trigger();
        if (sayEdit->toPlainText() != QStringLiteral("AB"))
            return EXIT_FAILURE;

        frame.GetChatView()->CreateTextView(true);
        application.processEvents();
        CTextView* textView = document.m_textView;
        if (!textView || !textView->m_pRichEdit) return EXIT_FAILURE;
        CTextEdit* output = textView->m_pRichEdit;
        output->setPlainText(QStringLiteral("output"));
        QTextCursor outputCursor = output->textCursor();
        outputCursor.setPosition(0);
        outputCursor.setPosition(3, QTextCursor::KeepAnchor);
        output->setTextCursor(outputCursor);
        output->setFocus();
        application.processEvents();
        frame.RefreshCommandUi();
        if (!registeredActionsHaveState(
                &frame, QStringLiteral("ID_EDIT_COPY"), true)
            || !registeredActionsHaveState(
                &frame, QStringLiteral("ID_EDIT_SELECTALL"), true)
            || !registeredActionsHaveState(
                &frame, QStringLiteral("ID_EDIT_UNDO"), false)
            || !registeredActionsHaveState(
                &frame, QStringLiteral("ID_EDIT_CUT"), false)
            || !registeredActionsHaveState(
                &frame, QStringLiteral("ID_EDIT_PASTE"), false)
            || !registeredActionsHaveState(
                &frame, QStringLiteral("ID_EDIT_DELETE"), false)) {
            qWarning() << "Text output command UI";
            return EXIT_FAILURE;
        }
        registeredCommandAction(
            &frame, QStringLiteral("ID_EDIT_COPY"))->trigger();
        if (QApplication::clipboard()->text() != QStringLiteral("out"))
            return EXIT_FAILURE;

        QMenu* editMenu = menuWithDirectCommand(
            frame.menuBar(), QStringLiteral("ID_EDIT_UNDO"));
        if (!editMenu) return EXIT_FAILURE;
        editMenu->popup(frame.mapToGlobal(QPoint(4, 4)));
        application.processEvents();
        frame.RefreshCommandUi();
        if (frame.GetCommandFocusWidget() != output
            || !registeredActionsHaveState(
                &frame, QStringLiteral("ID_EDIT_COPY"), true)) {
            qWarning() << "menu previous focus"
                       << frame.GetCommandFocusWidget() << output;
            return EXIT_FAILURE;
        }
        editMenu->hide();
        application.processEvents();

        frame.GetChatView()->CreateComicView(true);
        application.processEvents();
        frame.RefreshCommandUi();
        if (!document.m_bComicView || !document.m_sayWnd
            || !document.m_memberList || !document.m_bodyCam) {
            qWarning() << "comic view restoration";
            return EXIT_FAILURE;
        }
    }

    {
        CBodyCam* bodyCam = document.m_bodyCam;
        auto* sayWindow = dynamic_cast<CSayWnd*>(document.m_sayWnd);
        if (!bodyCam || !sayWindow || !document.m_memberList)
            return EXIT_FAILURE;

        const QString sourceCharacter = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK")).left(1);
        sayWindow->GetSayEdit()->clear();
        bodyCam->setFocus();
        application.processEvents();
        QKeyEvent characterEvent(QEvent::KeyPress, 0, Qt::NoModifier,
                                 sourceCharacter);
        QApplication::sendEvent(bodyCam, &characterEvent);
        if (sayWindow->GetSayEdit()->toPlainText() != sourceCharacter
            || QApplication::focusWidget() != sayWindow->GetSayEdit()) {
            qWarning() << "BodyCam character forwarding"
                       << sayWindow->GetSayEdit()->toPlainText()
                       << QApplication::focusWidget();
            return EXIT_FAILURE;
        }

        bodyCam->setFocus();
        QKeyEvent tabEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(bodyCam, &tabEvent);
        application.processEvents();
        if (!frame.GetTabBar()->TabControl()->hasFocus()) {
            qWarning() << "BodyCam forward focus cycle"
                       << QApplication::focusWidget();
            return EXIT_FAILURE;
        }

        const bool savedEmbedded = theApp.m_bEmbedded;
        theApp.m_bEmbedded = true;
        bodyCam->setFocus();
        document.CycleFocus(CHATFOCUS_EMOTIONWND, false);
        application.processEvents();
        QWidget* comicFocus =
            document.GetComponentWindow(CHATFOCUS_COMICVIEW);
        QWidget* focused = QApplication::focusWidget();
        if (!comicFocus
            || (focused != comicFocus
                && !comicFocus->isAncestorOf(focused))
            || frame.GetTabBar()->TabControl()->hasFocus()) {
            qWarning() << "embedded focus cycle"
                       << focused << comicFocus;
            return EXIT_FAILURE;
        }
        theApp.m_bEmbedded = savedEmbedded;

        bodyCam->setFocus();
        QKeyEvent backtabEvent(QEvent::KeyPress, Qt::Key_Backtab,
                               Qt::ShiftModifier);
        QApplication::sendEvent(bodyCam, &backtabEvent);
        application.processEvents();
        if (!document.m_memberList->FocusWidget()->hasFocus()) {
            qWarning() << "BodyCam backward focus cycle"
                       << QApplication::focusWidget();
            return EXIT_FAILURE;
        }

        bool optionsSeen = false;
        bool characterPageSelected = false;
        QTimer::singleShot(0, [&] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = dynamic_cast<QDialog*>(widget);
                if (!dialog || dialog->windowTitle()
                        != originalResourceString(QStringLiteral("IDS_OPTIONS"))) {
                    continue;
                }
                optionsSeen = true;
                QTabWidget* tabs = dialog->findChild<QTabWidget*>();
                characterPageSelected = tabs
                    && dynamic_cast<CCharacterPage*>(tabs->currentWidget());
                dialog->reject();
            }
        });
        const QPointF doubleClickPoint(1.0, 1.0);
        QMouseEvent doubleClick(
            QEvent::MouseButtonDblClick, doubleClickPoint, doubleClickPoint,
            QPointF(bodyCam->mapToGlobal(QPoint(1, 1))), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bodyCam, &doubleClick);
        if (!optionsSeen || !characterPageSelected) {
            qWarning() << "BodyCam character-page double click"
                       << optionsSeen << characterPageSelected;
            return EXIT_FAILURE;
        }

        const ConnectionStatus savedStatus = document.GetConnectionStatus();
        document.m_proto->SetConnectionStatus(CX_CONNECTING);
        bool optionsWhileConnecting = false;
        QTimer::singleShot(0, [&] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = dynamic_cast<QDialog*>(widget);
                if (!dialog || dialog->windowTitle()
                        != originalResourceString(QStringLiteral("IDS_OPTIONS"))) {
                    continue;
                }
                optionsWhileConnecting = true;
                dialog->reject();
            }
        });
        QMouseEvent connectingDoubleClick(
            QEvent::MouseButtonDblClick, doubleClickPoint, doubleClickPoint,
            QPointF(bodyCam->mapToGlobal(QPoint(1, 1))), Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bodyCam, &connectingDoubleClick);
        application.processEvents();
        document.m_proto->SetConnectionStatus(savedStatus);
        if (optionsWhileConnecting) {
            qWarning() << "BodyCam opened Character page while connecting";
            return EXIT_FAILURE;
        }
    }

    {
        CCharacterPage characterPage(&frame);
        CBodyCam* preview = nullptr;
        for (QWidget* child : characterPage.findChildren<QWidget*>()) {
            if ((preview = dynamic_cast<CBodyCam*>(child))) break;
        }
        QListWidget* avatarList = characterPage.findChild<QListWidget*>();
        if (!preview || !avatarList || preview->m_forcedDelete
            || preview->m_avatar || avatarList->count() != 0) {
            qWarning() << "Character-page deferred setup" << preview
                       << avatarList
                       << (preview ? preview->m_avatar : nullptr)
                       << (avatarList ? avatarList->count() : -1);
            return EXIT_FAILURE;
        }

        characterPage.show();
        application.processEvents();
        if (!preview->m_avatar || avatarList->count() == 0
            || GetCharSelBodyCam() != preview
            || !RefreshBodyPreview(preview->m_avatar)) {
            qWarning() << "Character-page preview registration";
            return EXIT_FAILURE;
        }

        QContextMenuEvent previewContext(
            QContextMenuEvent::Mouse, QPoint(2, 2),
            preview->mapToGlobal(QPoint(2, 2)));
        QApplication::sendEvent(preview, &previewContext);
        application.processEvents();
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (qobject_cast<QMenu*>(widget) && widget->isVisible()) {
                qWarning() << "Character-page preview exposed a context menu";
                return EXIT_FAILURE;
            }
        }

        QString inactiveAvatar;
        const QFileInfoList artPackFiles = QDir(
            originalAssetDirectoryPath(QStringLiteral("artpack1")))
            .entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo& file : artPackFiles) {
            if (file.suffix().compare(QStringLiteral("avb"),
                                      Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString candidate = file.completeBaseName();
            const QString activeFile = originalFileInDirectoryPath(
                theApp.GetAvatarDir(), candidate + QStringLiteral(".avb"));
            if (activeFile.isEmpty() && !GetAvatar(candidate)) {
                inactiveAvatar = candidate;
                break;
            }
        }
        if (inactiveAvatar.isEmpty()) return EXIT_FAILURE;

        const QString previousAvatar = QString::fromUtf8(
            preview->m_avatar->OriginalName());
        QString displayName = inactiveAvatar;
        displayName[0] = displayName[0].toUpper();
        auto* unavailableItem = new QListWidgetItem(displayName, avatarList);
        unavailableItem->setData(Qt::UserRole, inactiveAvatar);
        QString shownMessage;
        QTimer::singleShot(0, [&] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* message = dynamic_cast<QMessageBox*>(widget);
                if (!message) continue;
                shownMessage = message->text();
                message->accept();
            }
        });
        avatarList->setCurrentItem(unavailableItem);
        QString expectedMessage = originalResourceString(
            QStringLiteral("IDS_INVALIDART"));
        expectedMessage.replace(QStringLiteral("%1"), displayName);
        if (shownMessage != expectedMessage
            || !avatarList->currentItem()
            || avatarList->currentItem()->data(Qt::UserRole).toString().compare(
                   previousAvatar, Qt::CaseInsensitive) != 0
            || QString::fromUtf8(preview->m_avatar->OriginalName()).compare(
                   previousAvatar, Qt::CaseInsensitive) != 0) {
            qWarning() << "Character-page invalid art recovery"
                       << shownMessage << expectedMessage;
            return EXIT_FAILURE;
        }
        {
            const QSignalBlocker blocker(avatarList);
            delete avatarList->takeItem(avatarList->row(unavailableItem));
        }

        const QString savedCharacter = QString::fromUtf8(GetMyCharacter());
        const unsigned int savedAvatarId = MyAvatarID();
        QListWidgetItem* applyItem = nullptr;
        CAvatarX* applyAvatar = nullptr;
        for (int index = 0; index < avatarList->count(); ++index) {
            QListWidgetItem* candidate = avatarList->item(index);
            const QString candidateName = candidate->data(Qt::UserRole).toString();
            CAvatarX* candidateAvatar = GetAvatar2(candidateName);
            if (candidateName.compare(savedCharacter, Qt::CaseInsensitive) != 0
                && candidateAvatar
                && candidateAvatar->m_avatarID != savedAvatarId) {
                applyItem = candidate;
                applyAvatar = candidateAvatar;
                break;
            }
        }
        if (!applyItem || !applyAvatar) return EXIT_FAILURE;
        avatarList->setCurrentItem(applyItem);
        application.processEvents();
        const QString selectedCharacter = applyItem->data(Qt::UserRole).toString();
        if (QString::fromUtf8(GetMyCharacter()) != savedCharacter) {
            qWarning() << "Character selection changed application state before apply";
            return EXIT_FAILURE;
        }
        characterPage.hide();
        application.processEvents();
        if (avatarList->count() != 0 || !preview->m_avatar
            || QString::fromUtf8(preview->m_avatar->OriginalName()).compare(
                   selectedCharacter, Qt::CaseInsensitive) != 0
            || GetCharSelBodyCam()) {
            qWarning() << "Character-page inactive cleanup";
            return EXIT_FAILURE;
        }
        const bool savedNoRefresh = theApp.m_bNoRefresh;
        CUserInfo* savedSelf = g_puiSelf;
        const bool savedComicView = document.m_bComicView;
        theApp.m_bNoRefresh = true;
        g_puiSelf = nullptr;
        document.m_bComicView = true;
        characterPage.apply();
        document.m_bComicView = savedComicView;
        g_puiSelf = savedSelf;
        theApp.m_bNoRefresh = savedNoRefresh;
        if (MyAvatarID() != applyAvatar->m_avatarID
            || QString::fromUtf8(GetMyCharacter()).compare(
                   selectedCharacter, Qt::CaseInsensitive) != 0) {
            qWarning() << "Character-page hidden apply";
            return EXIT_FAILURE;
        }
        if (savedAvatarId)
            SetMyAvatar(savedAvatarId, FALSE);
        else
            SetMyAvatarID(0);
        SetMyCharacter(savedCharacter);

        characterPage.show();
        application.processEvents();
        if (avatarList->count() == 0 || !avatarList->currentItem()
            || avatarList->currentItem()->data(Qt::UserRole).toString().compare(
                   selectedCharacter, Qt::CaseInsensitive) != 0
            || !preview->m_avatar
            || QString::fromUtf8(preview->m_avatar->OriginalName()).compare(
                   selectedCharacter, Qt::CaseInsensitive) != 0
            || GetCharSelBodyCam() != preview) {
            qWarning() << "Character-page reactivation";
            return EXIT_FAILURE;
        }
        document.m_bComicView = false;
        characterPage.apply();
        document.m_bComicView = savedComicView;
        if (QString::fromUtf8(GetMyCharacter()).compare(
                selectedCharacter, Qt::CaseInsensitive) != 0) {
            qWarning() << "Character-page non-comic apply";
            return EXIT_FAILURE;
        }
        SetMyCharacter(savedCharacter);

        characterPage.hide();
        application.processEvents();
        if (GetCharSelBodyCam() || avatarList->count() != 0
            || !preview->m_avatar) {
            qWarning() << "hidden Character-page preview remained registered";
            return EXIT_FAILURE;
        }
    }

    {
        if (!document.m_memberList || !document.m_proto)
            return EXIT_FAILURE;
        const QString savedNick = theApp.m_myNick;
        const INT savedAutoPage = theApp.m_iAutoPage;
        const ConnectionStatus savedStatus = document.GetConnectionStatus();
        const QString selfNick = originalResourceString(
            QStringLiteral("IDS_DEFAULT_NICK"));
        QString memberName;
        GetNextAvatarName(memberName);
        if (memberName.isEmpty()) return EXIT_FAILURE;
        const QString server = originalResourceString(
            QStringLiteral("IDS_DEFAULT_SERVER"));
        CUserInfo selfMember(selfNick, selfNick + QLatin1Char('@') + server);
        CUserInfo otherMember(memberName,
                              memberName + QLatin1Char('@') + server);
        selfMember.ComicUser(true);
        otherMember.ComicUser(true);
        otherMember.SetAvatarRealInfo(memberName,
                                      originalResourceString(
                                          QStringLiteral("IDS_URL_MSPREFIX")));
        theApp.m_myNick = selfNick;
        theApp.m_iAutoPage = -1;
        document.m_bComicView = true;
        document.m_puiSelf = &selfMember;
        g_puiSelf = &selfMember;
        document.m_allChannelPuis.append(&selfMember);
        document.m_allChannelPuis.append(&otherMember);
        document.m_memberList->AddUser(&selfMember);
        document.m_memberList->AddUser(&otherMember);
        otherMember.SelectInMemberList(&otherMember, TRUE, FALSE);
        document.m_proto->SetConnectionStatus(CX_INCHANNEL);

        QMenu* memberMenu = menuWithDirectCommand(
            frame.menuBar(), QStringLiteral("ID_MEMBER_GETINFO"));
        if (!memberMenu) return EXIT_FAILURE;
        memberMenu->aboutToShow();
        application.processEvents();
        const QStringList commands = directCommands(memberMenu);
        const int profile = commands.indexOf(
            QStringLiteral("ID_MEMBER_GETINFO"));
        const int identity = commands.indexOf(
            QStringLiteral("ID_GETIDENTITY"));
        const int character = commands.indexOf(
            QStringLiteral("ID_MEMBER_GETCHAR"));
        QAction* notifications = directCommand(
            memberMenu, QStringLiteral("ID_ADDTONOTIFICATIONS"));
        QAction* getCharacter = directCommand(
            memberMenu, QStringLiteral("ID_MEMBER_GETCHAR"));
        QAction* sendFile = directCommand(
            memberMenu, QStringLiteral("ID_SEND_FILE"));
        if (profile < 0 || identity != profile + 1
            || character != identity + 1
            || !notifications || !notifications->isEnabled()
            || !getCharacter || !getCharacter->isEnabled()
            || !sendFile || !sendFile->isEnabled()
            || getCharacter->text() != originalResourceString(
                QStringLiteral("IDS_GET_CHARACTER"))) {
            qWarning() << "member menu state" << commands
                       << (notifications && notifications->isEnabled())
                       << (getCharacter && getCharacter->isEnabled())
                       << (sendFile && sendFile->isEnabled());
            return EXIT_FAILURE;
        }

        selfMember.SelectInMemberList(&selfMember, TRUE, FALSE);
        memberMenu->aboutToShow();
        if (sendFile->isEnabled()) {
            qWarning() << "Send File enabled for the local member";
            return EXIT_FAILURE;
        }
        otherMember.SelectInMemberList(&otherMember, TRUE, TRUE);
        memberMenu->aboutToShow();
        if (sendFile->isEnabled()) {
            qWarning() << "Send File enabled for multiple members";
            return EXIT_FAILURE;
        }
        otherMember.SelectInMemberList(&otherMember, TRUE, FALSE);
        memberMenu->aboutToShow();
        if (!sendFile->isEnabled()) {
            qWarning() << "Send File disabled for one remote member";
            return EXIT_FAILURE;
        }

        theApp.m_iAutoPage = 1;
        memberMenu->aboutToShow();
        if (directCommand(memberMenu,
                          QStringLiteral("ID_ADDTONOTIFICATIONS"))->isEnabled()) {
            return EXIT_FAILURE;
        }
        theApp.m_iAutoPage = -1;
        document.m_bComicView = false;
        memberMenu->aboutToShow();
        if (directCommand(memberMenu, QStringLiteral("ID_MEMBER_GETCHAR")))
            return EXIT_FAILURE;
        document.m_bComicView = true;
        memberMenu->aboutToShow();
        if (!directCommand(memberMenu, QStringLiteral("ID_MEMBER_GETCHAR")))
            return EXIT_FAILURE;

        const DWORD savedModes = document.m_proto->m_dwModes;
        document.m_proto->m_dwModes |= CM_MODERATED;
        selfMember.SetOperator(true);
        document.UpdateAdminMenu();
        frame.RefreshCommandUi();
        memberMenu->aboutToShow();
        QAction* hostRoot = nullptr;
        for (QAction* action : memberMenu->actions()) {
            if (action->menu()
                && action->text() == originalResourceString(
                       QStringLiteral("IDS_ADMINMENU_LABEL"))) {
                hostRoot = action;
                break;
            }
        }
        const QList<OriginalMenuItem> adminResource =
            originalMenuResource(QStringLiteral("IDR_ADMIN"));
        if (!hostRoot || !hostRoot->menu()
            || adminResource.size() != 1
            || directCommands(hostRoot->menu()) != QStringList({
                   QStringLiteral("ID_ADMINISTRATOR_KICK"),
                   QStringLiteral("ID_ADMIN_BAN"),
                   QStringLiteral("ID_ADMIN_BGRNDSYNC"),
                   QStringLiteral("ID_MAKEADMIN"),
                   QStringLiteral("ID_MAKESPEAKER"),
                   QStringLiteral("ID_MAKESPECTATOR")
               })
            || !directCommand(
                    hostRoot->menu(),
                    QStringLiteral("ID_ADMINISTRATOR_KICK"))->isEnabled()
            || !directCommand(
                    hostRoot->menu(),
                    QStringLiteral("ID_ADMIN_BAN"))->isEnabled()
            || !directCommand(
                    hostRoot->menu(),
                    QStringLiteral("ID_ADMIN_BGRNDSYNC"))->isEnabled()
            || !directCommand(
                    hostRoot->menu(),
                    QStringLiteral("ID_MAKEADMIN"))->isEnabled()
            || !directCommand(
                    hostRoot->menu(),
                    QStringLiteral("ID_MAKESPEAKER"))->isEnabled()
            || !directCommand(
                    hostRoot->menu(),
                    QStringLiteral("ID_MAKESPECTATOR"))->isEnabled()
            || directCommand(
                    hostRoot->menu(),
                    QStringLiteral("ID_MAKEADMIN"))->isChecked()
            || !directCommand(
                    hostRoot->menu(),
                    QStringLiteral("ID_MAKESPEAKER"))->isChecked()
            || directCommand(
                    hostRoot->menu(),
                    QStringLiteral("ID_MAKESPECTATOR"))->isChecked()) {
            qWarning() << "dynamic Host menu";
            return EXIT_FAILURE;
        }

        selfMember.SetOperator(false);
        document.UpdateAdminMenu();
        frame.RefreshCommandUi();
        for (QAction* action : memberMenu->actions()) {
            if (action->menu()
                && action->text() == originalResourceString(
                       QStringLiteral("IDS_ADMINMENU_LABEL"))) {
                qWarning() << "Host menu remained after operator loss";
                return EXIT_FAILURE;
            }
        }
        document.m_proto->m_dwModes = savedModes;

        document.m_memberList->Clear();
        document.m_allChannelPuis.clear();
        document.m_puiSelf = nullptr;
        g_puiSelf = nullptr;
        document.m_proto->SetConnectionStatus(savedStatus);
        theApp.m_myNick = savedNick;
        theApp.m_iAutoPage = savedAutoPage;
    }

    // The focused interaction checks require a visible, registered main
    // frame. Restore the pre-existing structure-test harness before its MDI
    // lifecycle checks; the offscreen plugin cannot reactivate a hidden
    // top-level window after nested modal loops.
    frame.hide();
    theApp.m_pMainWnd = nullptr;
    frame.ActivateDocument(&document);
    SetChatDoc(&document);
    application.processEvents();

    CChatDoc* statusDocument = frame.CreateStatusWindow();
    if (!statusDocument || !statusDocument->m_bStatusView
        || !statusDocument->m_proto
        || statusDocument->m_proto->m_strChannel
               != QString::fromLatin1(STATUS_WINDOW_NAME)
        || !GetStatusView()
        || frame.GetTabBar()->FindTabNum(statusDocument) >= 0) {
        qWarning() << "mdi status";
        return EXIT_FAILURE;
    }
    frame.ShowStatusWindow(true);
    application.processEvents();
    if (!(theApp.m_flags0 & F0_SHOWSTATUSWINDOW)
        || frame.GetActiveDocument() != statusDocument
        || frame.GetTabBar()->FindTabNum(statusDocument) < 0) {
        qWarning() << "mdi status show";
        return EXIT_FAILURE;
    }
    frame.ShowStatusWindow(false);
    application.processEvents();
    if (theApp.m_flags0 & F0_SHOWSTATUSWINDOW
        || frame.GetActiveDocument() != &document
        || frame.GetTabBar()->FindTabNum(statusDocument) >= 0) {
        qWarning() << "mdi status hide" << theApp.m_flags0
                   << frame.GetActiveDocument() << &document
                   << frame.GetTabBar()->FindTabNum(statusDocument);
        return EXIT_FAILURE;
    }

    CChatDoc* secondDocument = frame.CreateNewDocument();
    application.processEvents();
    if (!secondDocument
        || secondDocument->GetTitle() != roomTitle + QStringLiteral("2")
        || frame.GetMDIArea()->subWindowList().size() != 3
        || frame.GetActiveDocument() != secondDocument) {
        qWarning() << "mdi second";
        return EXIT_FAILURE;
    }

    const QString mdiFrameTitle = originalResourceString(
        QStringLiteral("AFX_IDS_APP_TITLE")) + QStringLiteral(" - [")
        + secondDocument->GetTitle() + QLatin1Char(']');
    if (frame.windowTitle() != mdiFrameTitle
        || frame.menuBar()->actions().size() != 9) {
        qWarning() << "frame" << frame.windowTitle() << frame.menuBar()->actions().size();
        return EXIT_FAILURE;
    }
    const QList<OriginalMenuItem> sourceMenu = originalMenuResource(QStringLiteral("IDR_MAINFRAME"));
    for (int index = 0; index < sourceMenu.size(); ++index) {
        if (frame.menuBar()->actions()[index]->text() != sourceMenu[index].text)
        {
            qWarning() << "menu" << index << frame.menuBar()->actions()[index]->text()
                       << sourceMenu[index].text;
            return EXIT_FAILURE;
        }
    }

    bool foundMain = false;
    bool foundMember = false;
    bool foundText = false;
    bool foundTab = false;
    for (QToolBar* toolbar : frame.findChildren<QToolBar*>()) {
        if (toolbar->windowTitle() == originalMenuItemText(QStringLiteral("ID_VIEW_TOOLBAR_MAIN")).remove(QLatin1Char('&'))) {
            foundMain = toolbar->actions().size() == 13;
        } else if (toolbar->windowTitle() == originalMenuItemText(QStringLiteral("ID_VIEW_TOOLBAR_MEMBER")).remove(QLatin1Char('&'))) {
            foundMember = toolbar->actions().size() == 8;
        } else if (toolbar->windowTitle() == originalMenuItemText(QStringLiteral("ID_VIEW_TOOLBAR_TEXT")).remove(QLatin1Char('&'))) {
            foundText = toolbar->actions().size() == 7;
        } else if (toolbar->windowTitle() == originalResourceString(QStringLiteral("IDS_TABTITLE"))) {
            foundTab = toolbar->height() == 29;
        }
    }
    if (!foundMain || !foundMember || !foundText || !foundTab) {
        qWarning() << "toolbars" << foundMain << foundMember << foundText << foundTab;
        return EXIT_FAILURE;
    }

    theApp.m_pMainWnd = &frame;
    QMenu* windowMenu = menuWithDirectCommand(
        frame.menuBar(), QStringLiteral("ID_WINDOW_CASCADE"));
    if (!windowMenu) return EXIT_FAILURE;
    windowMenu->aboutToShow();
    application.processEvents();
    QList<QAction*> mdiActions;
    for (QAction* action : windowMenu->actions()) {
        if (action->property("originalMdiDocument").isValid())
            mdiActions.append(action);
    }
    if (mdiActions.size() != 2
        || mdiActions[0]->property("originalMdiDocument").value<void*>()
               != &document
        || mdiActions[1]->property("originalMdiDocument").value<void*>()
               != secondDocument
        || !mdiActions[1]->isChecked()) {
        qWarning() << "dynamic Window menu" << mdiActions.size();
        return EXIT_FAILURE;
    }

    mdiActions[0]->trigger();
    application.processEvents();
    frame.UpdateVisibilityInfo();
    if (frame.GetActiveDocument() != &document
        || !secondDocument->m_bObscured) {
        qWarning() << "dynamic Window activation"
                   << frame.GetActiveDocument()
                   << secondDocument->m_bObscured;
        return EXIT_FAILURE;
    }

    const QImage normalTabIcon =
        tabIconImage(frame.GetTabBar(), secondDocument);
    secondDocument->RegisterNewContent();
    const QImage newContentTabIcon =
        tabIconImage(frame.GetTabBar(), secondDocument);
    if (!secondDocument->m_bNewContent
        || normalTabIcon.isNull() || newContentTabIcon.isNull()
        || normalTabIcon == newContentTabIcon) {
        qWarning() << "tab new-content icon"
                   << secondDocument->m_bNewContent;
        return EXIT_FAILURE;
    }

    windowMenu->aboutToShow();
    mdiActions.clear();
    for (QAction* action : windowMenu->actions()) {
        if (action->property("originalMdiDocument").isValid())
            mdiActions.append(action);
    }
    if (mdiActions.size() != 2) return EXIT_FAILURE;
    mdiActions[1]->trigger();
    application.processEvents();
    frame.UpdateVisibilityInfo();
    if (frame.GetActiveDocument() != secondDocument
        || secondDocument->m_bNewContent
        || tabIconImage(frame.GetTabBar(), secondDocument)
               != normalTabIcon) {
        qWarning() << "tab new-content activation clear";
        return EXIT_FAILURE;
    }

    frame.ActivateDocument(&document);
    application.processEvents();
    if (frame.GetActiveDocument() != &document) {
        qWarning() << "part-once predecessor activation";
        return EXIT_FAILURE;
    }
    document.LoadDocData();
    int partCount = 0;
    delete secondDocument->m_proto;
    auto* countingProtocol = new CountingIrcProto(&partCount);
    countingProtocol->m_doc = secondDocument;
    secondDocument->m_proto = countingProtocol;
    countingProtocol->SetConnectionStatus(CX_INCHANNEL);
    secondDocument->OnLeave();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    if (partCount != 1
        || frame.GetMDIArea()->subWindowList().size() != 2
        || frame.GetTabBar()->FindTabNum(secondDocument) >= 0) {
        qWarning() << "part-once lifecycle" << partCount
                   << frame.GetMDIArea()->subWindowList().size();
        return EXIT_FAILURE;
    }

    bool memberPaneWidthFound = false;
    for (QLabel* label : frame.statusBar()->findChildren<QLabel*>()) {
        if (label->width() == originalResourceString(
                QStringLiteral("IDS_MEMBER_COUNT_WIDTH")).toInt()) {
            memberPaneWidthFound = true;
        }
    }
    if (!memberPaneWidthFound) qWarning() << "status width";
    theApp.m_pMainWnd = nullptr;
    return memberPaneWidthFound ? EXIT_SUCCESS : EXIT_FAILURE;
}
