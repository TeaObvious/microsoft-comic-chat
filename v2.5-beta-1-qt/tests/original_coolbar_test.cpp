#include "chat.h"
#include "chatbars.h"
#include "originalassets.h"
#include "resource.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QToolButton>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
[[noreturn]] void fail() { std::abort(); }

void requireAt(bool condition, int line)
{
    if (!condition) {
        std::fprintf(stderr, "require failed at line %d\n", line);
        fail();
    }
}

#define REQUIRE(condition) requireAt((condition), __LINE__)

UINT wordAt(const QByteArray& bytes, qsizetype offset)
{
    return static_cast<BYTE>(bytes[offset])
        | (static_cast<UINT>(static_cast<BYTE>(bytes[offset + 1])) << 8);
}

void appendRecord(QByteArray& bytes, UINT id, UINT length, BYTE flags)
{
    bytes.append(static_cast<char>(id & 0xff));
    bytes.append(static_cast<char>((id >> 8) & 0xff));
    bytes.append(static_cast<char>(length & 0xff));
    bytes.append(static_cast<char>((length >> 8) & 0xff));
    bytes.append(static_cast<char>(flags));
}

QAction* commandAction(CCoolToolBarEx* toolbar, UINT id)
{
    return toolbar ? toolbar->GetButtonFromID(id) : nullptr;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.m_iShowBars = SB_TOOLBAR_ANY | SB_STATUSBAR;

    QMainWindow window;
    const QList<OriginalMenuItem> mainMenu = originalMenuResource(
        QStringLiteral("IDR_MAINFRAME"));
    REQUIRE(mainMenu.size() > 6);
    QMenu favorites(mainMenu[6].text, &window);
    QString invoked;

    CChatToolBar coolbar(&window);
    REQUIRE(coolbar.Create(
        &window, FALSE,
        [](CCoolToolBarEx* toolbar, const QString& command,
           const QIcon& icon) {
            QAction* action = toolbar->addAction(
                icon, originalResourceString(command).section(
                    QLatin1Char('\n'), 1, 1));
            action->setData(command);
            return action;
        },
        [&invoked](const QString& command) { invoked = command; },
        [&favorites] { return &favorites; }));

    window.resize(900, 500);
    window.show();
    application.processEvents();

    CCoolToolBarEx* main = coolbar.GetToolBarFromID(IDR_MAINFRAME);
    CCoolToolBarEx* member = coolbar.GetToolBarFromID(IDR_USERTOOLBAR);
    CCoolToolBarEx* text = coolbar.GetToolBarFromID(IDR_TEXTTOOLBAR);
    REQUIRE(main && member && text);
    REQUIRE(main->actions().size() == 13);
    REQUIRE(member->actions().size() == 8);
    REQUIRE(text->actions().size() == 7);
    REQUIRE(main->objectName() == QStringLiteral("IDR_MAINFRAME"));
    REQUIRE(member->objectName() == QStringLiteral("IDR_USERTOOLBAR"));
    REQUIRE(text->objectName() == QStringLiteral("IDR_TEXTTOOLBAR"));

    QAction* comics = commandAction(main, ID_VIEW_COMICS);
    QAction* plainText = commandAction(main, ID_VIEW_TEXT);
    QAction* favoriteButton = commandAction(
        main, ID_FAVORITES_OPENFAVORITES);
    REQUIRE(comics && comics->isCheckable());
    REQUIRE(plainText && plainText->isCheckable());
    REQUIRE(comics->actionGroup() != nullptr);
    REQUIRE(comics->actionGroup() == plainText->actionGroup());
    REQUIRE(comics->actionGroup()->isExclusive());
    REQUIRE(favoriteButton && favoriteButton->menu() == &favorites);
    REQUIRE((favoriteButton->property("originalButtonStyle").toUInt()
             & TBSTYLE_DROPDOWN) != 0);
    auto* favoriteToolButton = qobject_cast<QToolButton*>(
        main->widgetForAction(favoriteButton));
    REQUIRE(favoriteToolButton != nullptr);
    REQUIRE(favoriteToolButton->popupMode() == QToolButton::MenuButtonPopup);
    REQUIRE((commandAction(main, ID_CHATROOM_LIST)
                 ->property("originalButtonStyle").toUInt()
             & TBSTYLE_GROUP) != 0);
    REQUIRE(commandAction(member, ID_AWAY_TOGGLE)->isCheckable());
    REQUIRE(commandAction(text, ID_SWITCHBOLD)->isCheckable());
    REQUIRE(commandAction(text, ID_SWITCHITALIC)->isCheckable());
    REQUIRE(commandAction(text, ID_SWITCHUNDERLINED)->isCheckable());
    REQUIRE(commandAction(text, ID_SWITCHFIXEDPITCH)->isCheckable());
    REQUIRE(commandAction(text, ID_SWITCHSYMBOL)->isCheckable());
    REQUIRE(!commandAction(text, ID_SETFONT)->isCheckable());
    REQUIRE(!commandAction(text, ID_SETCOLOR)->isCheckable());

    std::unique_ptr<QMenu> context(coolbar.CreateContextMenu());
    REQUIRE(context->actions().size() == 3);
    REQUIRE(context->actions()[0]->text()
            == originalMenuItemText(QStringLiteral("ID_VIEW_TOOLBAR_MAIN")));
    REQUIRE(context->actions()[1]->text()
            == originalMenuItemText(QStringLiteral("ID_VIEW_TOOLBAR_MEMBER")));
    REQUIRE(context->actions()[2]->text()
            == originalMenuItemText(QStringLiteral("ID_VIEW_TOOLBAR_TEXT")));
    REQUIRE(context->actions()[0]->isChecked());
    REQUIRE(context->actions()[1]->isChecked());
    REQUIRE(context->actions()[2]->isChecked());
    context->actions()[1]->trigger();
    REQUIRE(invoked == QStringLiteral("ID_VIEW_TOOLBAR_MEMBER"));

    coolbar.ToggleBar(CHAT_TOOLBAR_MEMBER);
    REQUIRE((theApp.m_iShowBars & SB_TOOLBAR_MEMBER) == 0);
    REQUIRE(!coolbar.IsBarShown(IDR_USERTOOLBAR));
    REQUIRE(member->isHidden());
    coolbar.ToggleBar(CHAT_TOOLBAR_MEMBER);
    REQUIRE((theApp.m_iShowBars & SB_TOOLBAR_MEMBER) != 0);
    REQUIRE(coolbar.IsBarShown(IDR_USERTOOLBAR));

    coolbar.ToggleBar(CHAT_TOOLBAR_WHOLE);
    REQUIRE((theApp.m_iShowBars & SB_TOOLBAR_ANY) == 0);
    REQUIRE(!coolbar.IsWholeBarVisible());
    REQUIRE(main->isHidden() && member->isHidden() && text->isHidden());
    coolbar.ToggleBar(CHAT_TOOLBAR_WHOLE);
    REQUIRE((theApp.m_iShowBars & SB_TOOLBAR_ANY) == SB_TOOLBAR_ANY);
    REQUIRE(coolbar.IsWholeBarVisible());

    QByteArray saved;
    REQUIRE(coolbar.SaveStateToBuffer(&saved));
    REQUIRE(saved.size() == 20);
    REQUIRE(wordAt(saved, 0) == IDR_MAINFRAME);
    REQUIRE(wordAt(saved, 5) == IDR_USERTOOLBAR);
    REQUIRE(wordAt(saved, 10) == IDR_TEXTTOOLBAR);
    REQUIRE(saved.mid(15, 5) == QByteArray(5, '\0'));

    BYTE* raw = nullptr;
    UINT rawSize = 0;
    REQUIRE(coolbar.SaveStateToBuffer(&raw, &rawSize));
    REQUIRE(rawSize == static_cast<UINT>(saved.size()));
    REQUIRE(std::memcmp(raw, saved.constData(), saved.size()) == 0);
    std::free(raw);

    QByteArray reordered;
    appendRecord(reordered, IDR_TEXTTOOLBAR, 111, 0);
    appendRecord(reordered, IDR_MAINFRAME, 222, 1);
    appendRecord(reordered, IDR_USERTOOLBAR, 333, 0);
    appendRecord(reordered, 0, 0, 0);
    REQUIRE(coolbar.LoadStateFromBuffer(reordered));
    REQUIRE(coolbar.FindBand(IDR_TEXTTOOLBAR) == 0);
    REQUIRE(coolbar.FindBand(IDR_MAINFRAME) == 1);
    REQUIRE(coolbar.FindBand(IDR_USERTOOLBAR) == 2);
    REQUIRE(window.toolBarBreak(main));

    QByteArray restored;
    REQUIRE(coolbar.SaveStateToBuffer(&restored));
    REQUIRE(wordAt(restored, 0) == IDR_TEXTTOOLBAR);
    REQUIRE(wordAt(restored, 5) == IDR_MAINFRAME);
    REQUIRE(wordAt(restored, 10) == IDR_USERTOOLBAR);
    REQUIRE(static_cast<BYTE>(restored[9]) == 1);
    REQUIRE(restored.mid(15, 5) == QByteArray(5, '\0'));

    QByteArray invalid;
    appendRecord(invalid, 0xfffe, 1, 0);
    appendRecord(invalid, 0, 0, 0);
    REQUIRE(!coolbar.LoadStateFromBuffer(invalid));
    REQUIRE(coolbar.FindBand(IDR_TEXTTOOLBAR) == 0);
    return 0;
}
