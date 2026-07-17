#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>

#include <cstdlib>
#include <cstdio>

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
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    theApp.InitVals();
    theApp.m_bComicView = true;
    theApp.InitializeComicsFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont, theApp.m_comicsColor));
    InitializeAvatars();

    QString avatarName;
    GetNextAvatarName(avatarName);
    REQUIRE(!avatarName.isEmpty());
    theApp.m_myCharacterName = avatarName;

    REQUIRE(CommunicationInits());
    CChatDoc document;
    SetChatDoc(&document);
    CPageView view;
    document.m_view = &view;

    const QString nick = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
    const QString channel = originalResourceString(QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString user = QString::fromUtf8(GetMyUserName());
    serverConn.ProcessMessage(
        QStringLiteral(":%1!%2@127.0.0.1 JOIN :%3").arg(nick, user, channel));

    REQUIRE(document.GetConnectionStatus() == CX_INCHANNEL);
    REQUIRE(document.m_pages.size() == 1);
    CPanel* titlePanel = document.m_pages.first()->m_panels.first();
    REQUIRE(titlePanel->m_elements.size() == 2);
    REQUIRE(g_puiSelf == nullptr);

    serverConn.ProcessMessage(
        QStringLiteral("353 %1 = %2 :%1").arg(nick, channel));
    REQUIRE(g_puiSelf != nullptr);
    REQUIRE(g_mapNickToPtr->size() == 1);
    REQUIRE(titlePanel->m_elements.size() == 2);

    serverConn.ProcessMessage(
        QStringLiteral("366 %1 %2").arg(nick, channel));
    REQUIRE(titlePanel->m_elements.size() == 4);
    REQUIRE(dynamic_cast<CBodyUnary*>(titlePanel->m_elements[2]) != nullptr);
    REQUIRE(dynamic_cast<CStarLabel*>(titlePanel->m_elements[3]) != nullptr);

    CAvatarX* avatar = MyAvatar();
    if (avatar) avatar->m_userInfo = nullptr;
    g_puiSelf = nullptr;
    g_mapNickToPtr->clear();
    SetChatDoc(nullptr);
    CommunicationCleanup();
    CUnitPanelPage::DestroyFonts();
    DestroyAvatars();
    return 0;
}
