#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "ircproto.h"
#include "ircsock.h"
#include "memblst.h"
#include "originalassets.h"
#include "protsupp.h"
#include "saywnd.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QListWidget>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

class CapturingIrcProto final : public CIrcProto {
public:
    void SendMessageText(const QString& raw) override { sent.append(raw); }
    void SendMessageBytes(const QByteArray& raw) override
    {
        sent.append(QString::fromUtf8(raw));
    }
    QList<QString> sent;
};

QListWidgetItem* itemForUser(QListWidget* list, CUserInfo* pui)
{
    for (int index = 0; index < list->count(); ++index) {
        QListWidgetItem* item = list->item(index);
        if (item->data(Qt::UserRole).value<void*>() == pui) return item;
    }
    return nullptr;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    REQUIRE(CommunicationInits());
    InitializeAvatars();

    QString otherNick;
    GetNextAvatarName(otherNick);
    REQUIRE(!otherNick.isEmpty());
    const QString selfNick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString channel = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString identity = otherNick + QLatin1Char('@') + server;

    CChatDoc document;
    document.m_bComicView = false;
    SetChatDoc(&document);
    delete document.m_proto;
    document.m_proto = nullptr;
    CapturingIrcProto protocol;
    document.m_proto = &protocol;
    protocol.m_doc = &document;
    protocol.m_strChannel = channel;
    protocol.SetConnectionStatus(CX_INCHANNEL);
    document.LoadDocData();

    CMemberList memberList;
    document.m_memberList = &memberList;
    CUserInfo self(selfNick, QString::fromUtf8(GetMyUserName()));
    self.ComicUser(true);
    CUserInfo other(otherNick, identity);
    other.ComicUser(true);
    document.m_puiSelf = &self;
    g_puiSelf = &self;
    document.m_mapNickToPtr.insert(selfNick.toLower(), &self);
    document.m_mapNickToPtr.insert(otherNick.toLower(), &other);
    document.m_allChannelPuis.append(&self);
    document.m_allChannelPuis.append(&other);
    memberList.AddUser(&self);
    memberList.AddUser(&other);

    auto* list = qobject_cast<QListWidget*>(memberList.FocusWidget());
    REQUIRE(list != nullptr);
    QListWidgetItem* otherItem = itemForUser(list, &other);
    REQUIRE(otherItem != nullptr);
    list->setCurrentItem(otherItem);
    otherItem->setSelected(true);
    REQUIRE(document.SelectedMemberCount() == 1);
    REQUIRE(document.GetSingleSelectedMember() == &other);

    BOOL checked = TRUE;
    REQUIRE(document.OnUpdateMemberGetinfo());
    REQUIRE(document.OnUpdateMemberIgnore(&checked));
    REQUIRE(!checked);
    REQUIRE(document.OnUpdateAddToNotifs());
    REQUIRE(document.OnUpdateGetidentity());
    REQUIRE(document.OnUpdate1SelectionNotSelf());
    REQUIRE(document.OnUpdateComicUserNotSelf());
    REQUIRE(document.OnUpdateVisitHomepage());
    REQUIRE(document.OnUpdateAdminBan());
    REQUIRE(document.OnUpdateInvite());
    REQUIRE(!document.OnUpdateMakeadmin(&checked));
    REQUIRE(!checked);

    document.m_bIconMembers = false;
    checked = FALSE;
    REQUIRE(document.OnUpdateViewList(FALSE, &checked));
    REQUIRE(checked);
    checked = TRUE;
    REQUIRE(!document.OnUpdateViewIcon(&checked));
    REQUIRE(!checked);
    checked = TRUE;
    REQUIRE(document.OnUpdateViewComics(&checked));
    REQUIRE(!checked);
    checked = FALSE;
    REQUIRE(document.OnUpdateViewText(&checked));
    REQUIRE(checked);
    protocol.m_dwModes |= CM_NOFORMAT;
    REQUIRE(!document.OnUpdateViewComics());
    protocol.m_dwModes &= ~DWORD(CM_NOFORMAT);

    CSayWnd::SetDefaultButtons(0);
    CSayWnd sayWindow;
    document.m_sayWnd = &sayWindow;
    memberList.show();
    sayWindow.show();
    application.processEvents();
    const QString sourceCharacter = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK")).left(1);
    REQUIRE(!sourceCharacter.isEmpty());
    sayWindow.GetSayEdit()->clear();
    list->setFocus();
    QKeyEvent memberCharacter(QEvent::KeyPress, 0, Qt::NoModifier,
                              sourceCharacter);
    QApplication::sendEvent(list, &memberCharacter);
    REQUIRE(sayWindow.GetSayEdit()->toPlainText() == sourceCharacter);
    list->setFocus();
    QKeyEvent memberBackTab(QEvent::KeyPress, Qt::Key_Backtab,
                            Qt::ShiftModifier);
    QApplication::sendEvent(list, &memberBackTab);
    REQUIRE(QApplication::focusWidget() == sayWindow.GetSayEdit());

    document.OnMemberGetinfo();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :# GetInfo\r\n").arg(otherNick));
    REQUIRE(other.IsRequestInfo(RF_PROFILE));

    const qsizetype historyBeforeIdentity = document.m_history.size();
    const qsizetype sentBeforeIdentity = protocol.sent.size();
    document.OnGetidentity();
    REQUIRE(protocol.sent.size() == sentBeforeIdentity);
    REQUIRE(document.m_history.size() == historyBeforeIdentity + 1);
    auto* identityEntry = dynamic_cast<GetInfoEntry*>(document.m_history.last());
    REQUIRE(identityEntry != nullptr);
    QString expectedIdentity = originalResourceString(
        QStringLiteral("IDS_REPORT_IDENT2"));
    expectedIdentity.replace(QStringLiteral("%1"), other.GetScreenName());
    expectedIdentity.replace(QStringLiteral("%2"), otherNick);
    expectedIdentity.replace(QStringLiteral("%3"), server);
    REQUIRE(identityEntry->m_info == expectedIdentity);

    other.SetFullName(QString());
    document.OnGetidentity();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("WHOIS %1\r\n").arg(otherNick));
    REQUIRE(serverConn.m_queries.FindQuery(ctWhoIs) != nullptr);
    serverConn.ProcessMessage(
        QStringLiteral("311 %1 %2 %2 %3 :%4")
            .arg(selfNick, otherNick, server,
                 originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME"))));
    identityEntry = dynamic_cast<GetInfoEntry*>(document.m_history.last());
    REQUIRE(identityEntry != nullptr);
    REQUIRE(identityEntry->m_info == expectedIdentity);
    serverConn.ProcessMessage(
        QStringLiteral("318 %1 %2").arg(selfNick, otherNick));
    REQUIRE(serverConn.m_queries.FindQuery(ctWhoIs) == nullptr);
    other.SetFullName(identity);

    document.OnGetVersion();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :\001VERSION\001\r\n")
                   .arg(otherNick));
    document.OnGetLocaltime();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :\001TIME\001\r\n")
                   .arg(otherNick));
    document.OnSendEmail();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :\001EMAIL\001\r\n")
                   .arg(otherNick));
    document.OnVisitHomepage();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :\001URL\001\r\n")
                   .arg(otherNick));
    document.OnPingUser();
    REQUIRE(protocol.sent.takeLast().startsWith(
        QStringLiteral("PRIVMSG %1 :\001PING ").arg(otherNick)));
    REQUIRE(other.CheckFlag(UF_REQUESTPING));

    document.OnMemberIgnore();
    REQUIRE(other.Ignored());
    REQUIRE(IsIgnored(identity));
    checked = FALSE;
    REQUIRE(document.OnUpdateMemberIgnore(&checked));
    REQUIRE(checked);
    document.OnMemberIgnore();
    REQUIRE(!other.Ignored());
    REQUIRE(!IsIgnored(identity));

    // OnDblClick obtains the source's single selected member instead of
    // acting on the signal's clicked item while a multiple selection exists.
    protocol.sent.clear();
    QListWidgetItem* selfItem = itemForUser(list, &self);
    REQUIRE(selfItem != nullptr);
    selfItem->setSelected(true);
    list->itemDoubleClicked(otherItem);
    REQUIRE(protocol.sent.isEmpty());
    list->clearSelection();
    list->setCurrentItem(otherItem);
    otherItem->setSelected(true);
    list->itemDoubleClicked(otherItem);
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("PRIVMSG %1 :# GetInfo\r\n").arg(otherNick));

    const QStringList avatarNames = GetAllAvatarNames();
    REQUIRE(avatarNames.size() >= 14);
    std::vector<std::unique_ptr<CUserInfo>> sourceUsers;
    sourceUsers.reserve(14);
    for (INT index = 0; index < 12; ++index) {
        const QString& avatarName = avatarNames.at(index);
        auto user = std::make_unique<CUserInfo>(
            avatarName, avatarName + QLatin1Char('@') + server);
        user->ComicUser(true);
        CAvatarX* avatar = GetAvatar3(avatarName, user.get(), FALSE);
        REQUIRE(avatar != nullptr);
        SetUserAvatarID(user.get(), avatar->m_avatarID);
        document.m_allChannelPuis.append(user.get());
        memberList.AddUser(user.get());
        sourceUsers.push_back(std::move(user));
    }

    sourceUsers[1]->SetOperator(true);
    sourceUsers[2]->SetFlag(UF_SPECTATOR, true);
    sourceUsers[3]->SetOperator(true);
    sourceUsers[3]->SetFlag(UF_AWAY, true);
    sourceUsers[3]->Ignore(true);
    sourceUsers[4]->SetOperator(true);
    sourceUsers[4]->SetFlag(UF_AWAY, true);
    for (INT index = 0; index < 5; ++index)
        memberList.AddUser(sourceUsers[index].get());

    document.OnViewListAux();
    const int sourceStatusImages[] = {0, 1, 2, 3, 4};
    for (INT index = 0; index < 5; ++index) {
        QListWidgetItem* item = itemForUser(list, sourceUsers[index].get());
        REQUIRE(item != nullptr);
        REQUIRE(item->data(CMemberList::StatusImageRole).toInt()
                == sourceStatusImages[index]);
        REQUIRE(item->data(CMemberList::StateImageRole).toInt() == 0);
        REQUIRE(item->icon().pixmap(16, 16).toImage()
                == theApp.m_StatusIcons.at(sourceStatusImages[index])
                       .pixmap(16, 16).toImage());
    }

    theApp.m_bDoTest = true;
    memberList.Sort();
    REQUIRE(itemForUser(list, sourceUsers[1].get())->text()
            == QLatin1Char('@') + sourceUsers[1]->GetScreenName());
    REQUIRE(itemForUser(list, sourceUsers[2].get())->text()
            == QLatin1Char('>') + sourceUsers[2]->GetScreenName());
    theApp.m_bDoTest = false;
    memberList.Sort();

    document.m_bComicView = true;
    document.OnViewIcon();
    REQUIRE(list->iconSize() == QSize(58, 40));
    for (INT index = 0; index < 5; ++index) {
        QListWidgetItem* item = itemForUser(list, sourceUsers[index].get());
        REQUIRE(item != nullptr);
        REQUIRE(item->data(CMemberList::StateImageRole).toInt()
                == sourceStatusImages[index] + 1);
        REQUIRE(item->data(CMemberList::AvatarImageRole).toInt() >= 0);
        REQUIRE(!item->icon().isNull());
    }

    QListWidgetItem* normalItem = itemForUser(list, sourceUsers[0].get());
    REQUIRE(normalItem != nullptr);
    list->clearSelection();
    normalItem->setSelected(true);
    list->setCurrentItem(normalItem, QItemSelectionModel::NoUpdate);
    UpdateSpectators(&document, TRUE);
    REQUIRE(sourceUsers[0]->IsSpectator());
    REQUIRE(normalItem->data(CMemberList::StatusImageRole).toInt() == 2);
    REQUIRE(normalItem->isSelected());
    UpdateSpectators(&document, FALSE);
    REQUIRE(!sourceUsers[0]->IsSpectator());
    REQUIRE(normalItem->data(CMemberList::StatusImageRole).toInt() == 0);
    REQUIRE(normalItem->isSelected());
    sourceUsers[2]->SetFlag(UF_SPECTATOR, true);
    memberList.AddUser(sourceUsers[2].get());

    QStringList sortedAvatarNames = avatarNames;
    std::sort(sortedAvatarNames.begin(), sortedAvatarNames.end(),
              [](const QString& left, const QString& right) {
                  return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    auto quotedUser = std::make_unique<CUserInfo>(
        QString(QLatin1Char('"')) + sortedAvatarNames.last()
            + QLatin1Char('"'),
        sortedAvatarNames.last() + QLatin1Char('@') + server);
    CAvatarX* quotedAvatar = GetAvatar3(sortedAvatarNames.last(),
                                        quotedUser.get(), FALSE);
    REQUIRE(quotedAvatar != nullptr);
    SetUserAvatarID(quotedUser.get(), quotedAvatar->m_avatarID);
    auto lowUser = std::make_unique<CUserInfo>(
        sortedAvatarNames.first(),
        sortedAvatarNames.first() + QLatin1Char('@') + server);
    CAvatarX* lowAvatar = GetAvatar3(sortedAvatarNames.first(),
                                     lowUser.get(), FALSE);
    REQUIRE(lowAvatar != nullptr);
    SetUserAvatarID(lowUser.get(), lowAvatar->m_avatarID);
    CUserInfo* quotedPtr = quotedUser.get();
    CUserInfo* lowPtr = lowUser.get();
    sourceUsers.push_back(std::move(quotedUser));
    sourceUsers.push_back(std::move(lowUser));
    document.m_allChannelPuis.append(quotedPtr);
    document.m_allChannelPuis.append(lowPtr);
    memberList.AddUser(quotedPtr);
    memberList.AddUser(lowPtr);
    REQUIRE(FindMemberListIndex(lowPtr, &document)
            < FindMemberListIndex(quotedPtr, &document));
    REQUIRE(FindMemberListIndex(sourceUsers[1].get(), &document)
            < FindMemberListIndex(sourceUsers[0].get(), &document));
    REQUIRE(FindMemberListIndex(sourceUsers[0].get(), &document)
            < FindMemberListIndex(sourceUsers[2].get(), &document));

    list->clearSelection();
    for (INT index = 0; index < 12; ++index) {
        QListWidgetItem* item = itemForUser(list, sourceUsers[index].get());
        REQUIRE(item != nullptr);
        item->setSelected(true);
    }
    QList<CUserInfo*> whisperSelections;
    GetSelectedPuis(whisperSelections);
    REQUIRE(whisperSelections.size() == 11);
    MListTalkTosToPuiself(&self);
    REQUIRE(self.m_udi.m_talkTos.size() == 12);

    self.SelectInMemberList(&self, TRUE, FALSE);
    REQUIRE(document.SelectedMemberCount() == 1);
    REQUIRE(document.GetSingleSelectedMember() == &self);
    other.SelectInMemberList(&other, TRUE, TRUE);
    REQUIRE(document.SelectedMemberCount() == 2);
    REQUIRE(memberList.currentUser() == &other);
    other.SelectInMemberList(&other, FALSE, FALSE);
    REQUIRE(document.SelectedMemberCount() == 0);

    theApp.m_bShowIdentity = true;
    REQUIRE(other.GetQualifiedName()
            == QStringLiteral("%1 (%2)").arg(otherNick, identity));
    theApp.m_bShowIdentity = false;
    REQUIRE(other.GetQualifiedName() == otherNick);
    theApp.m_bShowIdentity = true;

    list->clearSelection();
    otherItem->setSelected(true);
    list->setCurrentItem(otherItem);
    self.SetOperator(true);
    protocol.m_dwModes = CM_MODERATED;
    document.OnMakeadmin();
    REQUIRE(protocol.sent.takeLast()
            == QStringLiteral("MODE %1 +o %2\r\n").arg(channel, otherNick));
    other.SetOperator(true);
    document.OnMakespeaker();
    REQUIRE(protocol.sent.takeFirst()
            == QStringLiteral("MODE %1 -o %2\r\n").arg(channel, otherNick));
    REQUIRE(protocol.sent.takeFirst()
            == QStringLiteral("MODE %1 +v %2\r\n").arg(channel, otherNick));
    document.OnMakespectator();
    REQUIRE(protocol.sent.takeFirst()
            == QStringLiteral("MODE %1 -o %2\r\n").arg(channel, otherNick));
    REQUIRE(protocol.sent.takeFirst()
            == QStringLiteral("MODE %1 -v %2\r\n").arg(channel, otherNick));
    self.SetOperator(false);
    other.SetOperator(false);

    theApp.m_iAutoPage = -1;
    document.OnAddToNotifs();
    REQUIRE(theApp.m_iAutoPage == 1);
    theApp.m_iAutoPage = -1;

    const QList<OriginalMenuItem> memberMenu = originalMenuResource(
        QStringLiteral("IDR_IRC_MEMBER"));
    REQUIRE(memberMenu.size() == 1);
    REQUIRE(memberMenu.first().type == OriginalMenuItemType::Popup);
    REQUIRE(memberMenu.first().children.first().commandIdentifier
            == QStringLiteral("ID_MEMBER_GETINFO"));
    REQUIRE(memberMenu.first().children.first().text
            == originalMenuItemText(QStringLiteral("ID_MEMBER_GETINFO")));

    document.m_memberList = nullptr;
    document.m_sayWnd = nullptr;
    document.m_puiSelf = nullptr;
    document.m_mapNickToPtr.clear();
    document.m_allChannelPuis.clear();
    g_puiSelf = nullptr;
    SetChatDoc(nullptr);
    document.m_proto = nullptr;
    DestroyAvatars();
    CommunicationCleanup();
    return EXIT_SUCCESS;
}
