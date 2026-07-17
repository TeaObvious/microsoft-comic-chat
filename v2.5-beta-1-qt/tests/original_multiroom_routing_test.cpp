#include "avatar.h"
#include "backdrop.h"
#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "panel.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>

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

class CapturingIrcProto final : public CIrcProto {
public:
    void SendMessageText(const QString& raw) override { sent.append(raw); }
    void SendMessageBytes(const QByteArray& raw) override
    {
        sent.append(QString::fromUtf8(raw));
    }
    QList<QString> sent;
};
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

    const QString sourceServerList = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVERLIST"));
    REQUIRE(sourceServerList.contains(QLatin1Char(';')));
    QString quotedKeyString;
    REQUIRE(ChangeKeyString(quotedKeyString, QStringLiteral("bk"),
                            &sourceServerList, 255));
    REQUIRE(quotedKeyString
            == QStringLiteral("bk=\"") + sourceServerList + QLatin1Char('"'));
    QString readValue;
    REQUIRE(GetValueFromKeyString(quotedKeyString, QStringLiteral("bk"),
                                  readValue));
    REQUIRE(readValue == sourceServerList);
    QString remainingKeyString = quotedKeyString;
    QString enumeratedKey;
    QString enumeratedValue;
    REQUIRE(EnumKeyString(remainingKeyString, enumeratedKey,
                          enumeratedValue));
    REQUIRE(enumeratedKey == QStringLiteral("bk"));
    REQUIRE(enumeratedValue == sourceServerList);
    REQUIRE(remainingKeyString.isEmpty());
    QString sourceDeletionQuirk = quotedKeyString;
    REQUIRE(ChangeKeyString(sourceDeletionQuirk, QStringLiteral("bk"),
                            nullptr, 255));
    REQUIRE(sourceDeletionQuirk == quotedKeyString);

    QString firstSourceName;
    QString secondSourceName;
    GetNextAvatarName(firstSourceName);
    GetNextAvatarName(secondSourceName);
    REQUIRE(!firstSourceName.isEmpty());
    REQUIRE(!secondSourceName.isEmpty());
    REQUIRE(firstSourceName.compare(secondSourceName,
                                    Qt::CaseInsensitive) != 0);
    theApp.m_myCharacterName = firstSourceName;

    const QString channelOne = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QString channelTwo = EncodeChan(channelOne.mid(1));
    const QString selfNick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString server = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    REQUIRE(!channelOne.isEmpty());
    REQUIRE(!channelTwo.isEmpty());
    REQUIRE(channelOne.compare(channelTwo, Qt::CaseInsensitive) != 0);

    CChatDoc firstDocument;
    CChatDoc secondDocument;
    delete firstDocument.m_proto;
    firstDocument.m_proto = nullptr;
    delete secondDocument.m_proto;
    secondDocument.m_proto = nullptr;
    CapturingIrcProto firstProtocol;
    CapturingIrcProto secondProtocol;

    firstDocument.m_proto = &firstProtocol;
    firstProtocol.m_doc = &firstDocument;
    firstProtocol.m_strChannel = channelOne;
    firstProtocol.m_strPrettyChannel = DecodeChan(channelOne);
    firstProtocol.SetConnectionStatus(CX_INCHANNEL);
    firstDocument.InitHistory();

    secondDocument.m_proto = &secondProtocol;
    secondProtocol.m_doc = &secondDocument;
    secondProtocol.m_strChannel = channelTwo;
    secondProtocol.m_strPrettyChannel = DecodeChan(channelTwo);
    secondProtocol.SetConnectionStatus(CX_INCHANNEL);
    secondDocument.InitHistory();

    SetChatDoc(&secondDocument);
    auto* secondRoomSelf = new CUserInfo(
        QLatin1Char('@') + selfNick,
        QString::fromUtf8(GetMyUserName()) + QLatin1Char('@') + server);
    AddAndExecute(new JoinEntry(secondRoomSelf), &secondDocument);
    REQUIRE(secondDocument.m_puiSelf == secondRoomSelf);
    REQUIRE(secondRoomSelf->GetAvatarID() != 0);

    SetChatDoc(&firstDocument);
    const int firstHistoryBeforeJoin = firstDocument.m_history.size();
    const int secondHistoryBeforeJoin = secondDocument.m_history.size();
    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%1@%2 JOIN :%3")
            .arg(firstSourceName, server, channelTwo));
    REQUIRE(firstDocument.m_history.size() == firstHistoryBeforeJoin);
    REQUIRE(secondDocument.m_history.size() == secondHistoryBeforeJoin + 1);
    REQUIRE(LookupPui(firstSourceName, &firstDocument) == nullptr);
    CUserInfo* secondRoomUser = LookupPui(firstSourceName, &secondDocument);
    REQUIRE(secondRoomUser != nullptr);
    REQUIRE(secondRoomUser->GetFullName()
            == firstSourceName + QLatin1Char('@') + server);
    REQUIRE(GetChatDoc() == &firstDocument);

    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1 MODE %2 +m")
            .arg(server, channelTwo));
    REQUIRE((firstProtocol.m_dwModes & CM_MODERATED) == 0);
    REQUIRE(secondProtocol.m_dwModes & CM_MODERATED);

    const QString sourceTopic = originalResourceString(
        QStringLiteral("ID_STARRING"));
    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1 TOPIC %2 :%3")
            .arg(server, channelTwo, sourceTopic));
    REQUIRE(firstProtocol.m_strTopic.isEmpty());
    REQUIRE(secondProtocol.m_strTopic == sourceTopic);

    QString sourceMessage = originalResourceString(
        QStringLiteral("IDS_DEFAULTGREETING"));
    sourceMessage.replace(QStringLiteral("%1"), firstSourceName);
    sourceMessage.replace(QStringLiteral("%2"), channelTwo);
    const int firstHistoryBeforeMessage = firstDocument.m_history.size();
    const int secondHistoryBeforeMessage = secondDocument.m_history.size();
    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%1@%2 PRIVMSG %3 :%4")
            .arg(firstSourceName, server, selfNick, sourceMessage));
    REQUIRE(firstDocument.m_history.size() == firstHistoryBeforeMessage);
    REQUIRE(secondDocument.m_history.size() == secondHistoryBeforeMessage + 1);
    auto* privateSay = dynamic_cast<SayEntry*>(secondDocument.m_history.last());
    REQUIRE(privateSay != nullptr);
    REQUIRE(privateSay->m_mesg == sourceMessage);

    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%1@%2 DATA %3 CCUDI1 :# Appears as %4")
            .arg(firstSourceName, server, selfNick, secondSourceName));
    REQUIRE(secondRoomUser->IsComicUser());
    CAvatarX* announcedAvatar = GetAvatar(secondRoomUser->GetAvatarID());
    REQUIRE(announcedAvatar != nullptr);
    REQUIRE(announcedAvatar->m_name.compare(secondSourceName,
                                            Qt::CaseInsensitive) == 0);

    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%1@%2 NICK :%3")
            .arg(firstSourceName, server, secondSourceName));
    REQUIRE(LookupPui(firstSourceName, &secondDocument) == nullptr);
    REQUIRE(LookupPui(secondSourceName, &secondDocument) == secondRoomUser);
    REQUIRE(LookupPui(secondSourceName, &firstDocument) == nullptr);

    const int firstHistoryBeforeQuit = firstDocument.m_history.size();
    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1!%1@%2 QUIT :%3")
            .arg(secondSourceName, server, sourceTopic));
    REQUIRE(firstDocument.m_history.size() == firstHistoryBeforeQuit);
    REQUIRE(secondRoomUser->IsDeparted());

    REQUIRE(firstProtocol.m_pSock == secondProtocol.m_pSock);
    QString sourceBackdropName;
    QString changedBackdropName;
    for (const QString& name : OriginalBackdropNames()) {
        if (name.endsWith(QStringLiteral(".bgb"), Qt::CaseInsensitive)) {
            if (sourceBackdropName.isEmpty()) sourceBackdropName = name;
            else {
                changedBackdropName = name;
                break;
            }
        }
    }
    REQUIRE(!sourceBackdropName.isEmpty());
    REQUIRE(!changedBackdropName.isEmpty());
    CChatBackdrop* sourceBackdrop = LoadBackdropInfo(sourceBackdropName);
    REQUIRE(sourceBackdrop != nullptr);
    const QString sourceBackdropUrl = QString::fromUtf8(sourceBackdrop->Url());
    delete sourceBackdrop;

    const QString sourceBackdropProperty = sourceBackdropName
        + QLatin1Char(',') + sourceBackdropUrl;
    QString expectedClientData;
    REQUIRE(ChangeKeyString(expectedClientData, QStringLiteral("bk"),
                            &sourceBackdropProperty, 255));
    SetChatDoc(&firstDocument);
    firstProtocol.m_pSock->m_bIrcXServer = true;
    const int firstHistoryBeforePropertyList = firstDocument.m_history.size();
    REQUIRE(firstProtocol.bExecuteQuery(qpJoinBackUrl, ctPropGet, dtMax,
                                        nullptr, channelOne, QString()));
    REQUIRE(serverConn.m_queries.FindQuery(ctPropGet) != nullptr);
    REQUIRE(!firstProtocol.sent.isEmpty());
    REQUIRE(firstProtocol.sent.last()
            == QStringLiteral("PROP %1 CLIENT\r\n").arg(channelOne));
    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1 818 %2 %3 CLIENT :%4")
            .arg(server, selfNick, channelOne, expectedClientData));
    REQUIRE(firstProtocol.m_strClientData == expectedClientData);
    REQUIRE(firstDocument.m_history.size()
            == firstHistoryBeforePropertyList + 1);
    auto* listedBackdropEntry = dynamic_cast<ChangeBackDropEntry*>(
        firstDocument.m_history.last());
    REQUIRE(listedBackdropEntry != nullptr);
    REQUIRE(listedBackdropEntry->m_backName == sourceBackdropName);
    REQUIRE(listedBackdropEntry->m_backURL == sourceBackdropUrl);
    REQUIRE(serverConn.m_queries.FindQuery(ctPropGet) != nullptr);
    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1 819 %2 %3 :%4")
            .arg(server, selfNick, channelOne, sourceTopic));
    REQUIRE(serverConn.m_queries.FindQuery(ctPropGet) == nullptr);

    secondProtocol.m_pSock->m_bIrcXServer = true;
    secondRoomSelf->SetOwner(true);
    const int secondHistoryBeforeProperty = secondDocument.m_history.size();
    REQUIRE(secondProtocol.ChangeProperty(secondRoomSelf,
                                          QStringLiteral("bk"),
                                          &sourceBackdropProperty));
    REQUIRE(!secondProtocol.sent.isEmpty());
    REQUIRE(secondProtocol.sent.last()
            == QStringLiteral("PROP %1 CLIENT :%2\r\n")
                   .arg(channelTwo, expectedClientData));
    REQUIRE(serverConn.m_queries.FindQuery(ctPropSet) != nullptr);

    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1 PROP %2 CLIENT :%3")
            .arg(server, channelTwo, expectedClientData));
    REQUIRE(serverConn.m_queries.FindQuery(ctPropSet) == nullptr);
    REQUIRE(secondProtocol.m_strClientData == expectedClientData);
    REQUIRE(secondDocument.m_history.size() == secondHistoryBeforeProperty);

    CChatBackdrop* changedBackdrop = LoadBackdropInfo(changedBackdropName);
    REQUIRE(changedBackdrop != nullptr);
    const QString changedBackdropUrl = QString::fromUtf8(changedBackdrop->Url());
    delete changedBackdrop;
    const QString changedBackdropProperty = changedBackdropName
        + QLatin1Char(',') + changedBackdropUrl;
    QString changedClientData;
    REQUIRE(ChangeKeyString(changedClientData, QStringLiteral("bk"),
                            &changedBackdropProperty, 255));
    firstProtocol.m_pSock->ProcessMessage(
        QStringLiteral(":%1 PROP %2 CLIENT :%3")
            .arg(server, channelTwo, changedClientData));
    REQUIRE(secondProtocol.m_strClientData == changedClientData);
    REQUIRE(secondDocument.m_history.size()
            == secondHistoryBeforeProperty + 1);
    auto* backdropEntry = dynamic_cast<ChangeBackDropEntry*>(
        secondDocument.m_history.last());
    REQUIRE(backdropEntry != nullptr);
    REQUIRE(backdropEntry->m_backName == changedBackdropName);
    REQUIRE(backdropEntry->m_backURL == changedBackdropUrl);

    SetChatDoc(nullptr);
    firstDocument.m_proto = nullptr;
    secondDocument.m_proto = nullptr;
    DestroyExternalUserInfos();
    CUnitPanelPage::DestroyFonts();
    DestroyAvatars();
    DestroyBackDropArt();
    return 0;
}
