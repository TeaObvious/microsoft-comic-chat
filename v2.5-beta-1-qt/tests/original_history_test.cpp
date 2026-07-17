#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "ircproto.h"
#include "ircsock.h"
#include "memblst.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>

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
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont,
                                     theApp.m_comicsColor));
    InitializeBackDrops();
    InitializeAvatars();

    QString selfAvatarName;
    QString otherSourceName;
    GetNextAvatarName(selfAvatarName);
    GetNextAvatarName(otherSourceName);
    REQUIRE(!selfAvatarName.isEmpty());
    REQUIRE(!otherSourceName.isEmpty());
    REQUIRE(selfAvatarName.compare(otherSourceName, Qt::CaseInsensitive) != 0);
    theApp.m_myCharacterName = selfAvatarName;

    REQUIRE(CommunicationInits());
    {
        CChatDoc document;
        SetChatDoc(&document);
        CMemberList members;
        document.m_memberList = &members;
        CPageView view;
        document.m_view = &view;

        const QString nick = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
        const QString channel = originalResourceString(QStringLiteral("IDS_DEFAULT_CHANNEL"));
        const QString user = QString::fromUtf8(GetMyUserName());
        serverConn.ProcessMessage(
            QStringLiteral(":%1!%2@127.0.0.1 JOIN :%3").arg(nick, user, channel));
        REQUIRE(document.m_history.size() == 2);
        REQUIRE(dynamic_cast<StartHistoryEntry*>(document.m_history.at(0)) != nullptr);
        REQUIRE(dynamic_cast<ChangeBackDropEntry*>(document.m_history.at(1)) != nullptr);
        REQUIRE(g_puiSelf == nullptr);

        serverConn.ProcessMessage(
            QStringLiteral("353 %1 = %2 :%1 %3").arg(nick, channel,
                                                       otherSourceName));
        REQUIRE(document.m_history.size() == 4);
        REQUIRE(dynamic_cast<JoinEntry*>(document.m_history.at(2)) != nullptr);
        REQUIRE(dynamic_cast<JoinEntry*>(document.m_history.at(3)) != nullptr);
        REQUIRE(g_puiSelf != nullptr);
        REQUIRE(document.m_allChannelPuis.size() == 2);

        serverConn.ProcessMessage(
            QStringLiteral("366 %1 %2").arg(nick, channel));
        REQUIRE(document.m_pages.first()->m_panels.first()->m_elements.size() == 6);

        AddAndExecute(new SayEntry(g_puiSelf, QStringLiteral("<Chr>"),
                                   NoFormattingSentinel()), &document);
        REQUIRE(document.m_history.size() == 5);
        REQUIRE(dynamic_cast<SayEntry*>(document.m_history.last()) != nullptr);

        const qsizetype usersBeforeReplay = document.m_allChannelPuis.size();
        view.SetPanelsWide(1);
        REQUIRE(document.m_allChannelPuis.size() == usersBeforeReplay);
        REQUIRE(!document.m_pages.isEmpty());
        REQUIRE(document.m_pages.first()->m_panels.first()->m_elements.size() == 6);

        QString conversation;
        QTextStream stream(&conversation, QIODevice::WriteOnly);
        document.ChatSaveConversation(stream);
        stream.flush();
        REQUIRE(conversation.startsWith(QStringLiteral("#CHATCONVERSATION\r\n")));
        REQUIRE(conversation.contains(QStringLiteral("starthistory\t")));
        REQUIRE(conversation.contains(QStringLiteral("backdrop\t")));
        REQUIRE(conversation.count(QStringLiteral("ejoin\t")) == 2);
        REQUIRE(conversation.contains(QStringLiteral("say\t")));
    }

    SetChatDoc(nullptr);
    CommunicationCleanup();
    CUnitPanelPage::DestroyFonts();
    DestroyBackDropArt();
    DestroyAvatars();
    return 0;
}
