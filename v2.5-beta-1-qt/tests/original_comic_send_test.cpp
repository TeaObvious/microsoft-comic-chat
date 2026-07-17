#include "avatar.h"
#include "avatario.h"
#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "ircsock.h"
#include "memblst.h"
#include "originalassets.h"
#include "panel.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "saywnd.h"
#include "userinfo.h"

#include <QApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QTimer>

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
    void SendMessageText(const QString& raw) override { sent.append(raw.toUtf8()); }
    void SendMessageBytes(const QByteArray& raw) override { sent.append(raw); }
    QList<QByteArray> sent;
};

QByteArray expectedAnnotation(CAvatarX* avatar, unsigned short modes,
                              const QByteArray& talkTo, bool parenthesized)
{
    CHAR faceIndex = 0;
    CHAR torsoIndex = 0;
    BYTE requested = 0;
    CEmotion face;
    CEmotion torso;
    BYTE faceEmotion = 0;
    BYTE faceIntensity = 0;
    BYTE torsoEmotion = 0;
    BYTE torsoIntensity = 0;
    avatar->GetIndices(faceIndex, torsoIndex, requested);
    avatar->GetEmotions(face, torso);
    EmotionToBytes(face, faceEmotion, faceIntensity);
    EmotionToBytes(torso, torsoEmotion, torsoIntensity);

    QByteArray result;
    if (parenthesized) result += '(';
    result += '#';
    result += CGESTUREPREFIX;
    result += static_cast<char>(IndexToByte(static_cast<BYTE>(torsoIndex)));
    result += static_cast<char>(torsoEmotion);
    result += static_cast<char>(torsoIntensity);
    result += CEXPRESSIONPREFIX;
    result += static_cast<char>(IndexToByte(static_cast<BYTE>(faceIndex)));
    result += static_cast<char>(faceEmotion);
    result += static_cast<char>(faceIntensity);
    if (requested) result += CREQUESTEDPREFIX;
    result += CMODEPREFIX;
    result += static_cast<char>(IndexToByte(BM2SM(modes)));
    if (!talkTo.isEmpty()) {
        result += CTALKTOPREFIX;
        result += talkTo;
    }
    if (parenthesized) result += ") ";
    return result;
}
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
    CAvatarX* avatar = GetAvatar3(avatarName);
    REQUIRE(avatar && avatar->m_body && avatar->m_icon);

    CChatDoc document;
    SetChatDoc(&document);
    delete document.m_proto;
    document.m_proto = nullptr;
    CapturingIrcProto protocol;
    document.m_proto = &protocol;
    protocol.m_doc = &document;
    protocol.m_strChannel = originalResourceString(QStringLiteral("IDS_DEFAULT_CHANNEL"));
    protocol.m_strPrettyChannel = protocol.m_strChannel;
    currentRoom = &protocol;
    protocol.SetConnectionStatus(CX_INCHANNEL);

    CUserInfo self(originalResourceString(QStringLiteral("IDS_DEFAULT_NICK")));
    CUserInfo selected(avatarName);
    g_puiSelf = &self;
    document.m_puiSelf = &self;
    SetMyAvatar(avatar->m_avatarID, FALSE);
    REQUIRE(self.GetAvatarID() == avatar->m_avatarID);
    AssignArbitraryAvatar(&selected);
    CAvatarX* selectedAvatar = GetAvatar(selected.GetAvatarID());
    REQUIRE(selectedAvatar != nullptr);

    CMemberList members;
    document.m_memberList = &members;
    members.AddUser(&self);
    members.AddUser(&selected);
    QListWidget* list = members.findChild<QListWidget*>();
    REQUIRE(list != nullptr);
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(Qt::UserRole).value<void*>() == &selected) {
            list->item(i)->setSelected(true);
        }
    }

    // The ellipsis is the original balloon continuation text used by panel.cpp.
    const QString sourceText = QStringLiteral("...");
    const QByteArray channel = protocol.m_strChannel.toUtf8();
    const QByteArray talkTo = selected.GetName().toUtf8();

    CSayWnd sayWindow;
    document.m_sayWnd = &sayWindow;
    sayWindow.GetSayEdit()->setPlainText(sourceText);
    protocol.sent.clear();
    protocol.SetConnectionStatus(CX_NOCHANNEL);
    bool sawIllegalToSend = false;
    QTimer::singleShot(0, [&sawIllegalToSend] {
        auto* message = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        if (!message) return;
        sawIllegalToSend = message->text()
            == originalResourceString(QStringLiteral("IDS_ILLEGAL_TO_SEND"));
        message->accept();
    });
    sayWindow.SendSayFromReturn();
    REQUIRE(sawIllegalToSend);
    REQUIRE(protocol.sent.isEmpty());
    REQUIRE(sayWindow.GetSayEdit()->toPlainText() == sourceText);

    protocol.SetConnectionStatus(CX_INCHANNEL);
    sayWindow.SendSayFromReturn();
    REQUIRE(!protocol.sent.isEmpty());
    REQUIRE(sayWindow.GetSayEdit()->toPlainText().isEmpty());
    protocol.sent.clear();

    protocol.m_pSock->m_bIrcXServer = true;
    const QByteArray ircxAnnotation = expectedAnnotation(avatar, BM_SAY, talkTo, false);
    REQUIRE(bChatSendText(sourceText, BM_SAY, true));
    REQUIRE(protocol.sent.size() == 2);
    REQUIRE(protocol.sent[0] == QByteArrayLiteral("DATA ") + channel
            + QByteArrayLiteral(" CCUDI1 :") + ircxAnnotation
            + QByteArrayLiteral("\r\n"));
    REQUIRE(protocol.sent[1] == QByteArrayLiteral("PRIVMSG ") + channel
            + QByteArrayLiteral(" :...\r\n"));
    REQUIRE(!document.m_pages.isEmpty());
    REQUIRE(document.m_pages.last()->m_panels.last()->m_elements.size() == 1);
    REQUIRE(dynamic_cast<CBWoodringNormal*>(
                document.m_pages.last()->m_panels.last()->m_elements.first()) != nullptr);

    protocol.sent.clear();
    protocol.m_pSock->m_bIrcXServer = false;
    const QByteArray ircAnnotation = expectedAnnotation(avatar, BM_SAY, talkTo, true);
    REQUIRE(bChatSendText(sourceText, BM_SAY, false));
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent[0] == QByteArrayLiteral("PRIVMSG ") + channel
            + QByteArrayLiteral(" :") + ircAnnotation + QByteArrayLiteral("...\r\n"));

    protocol.sent.clear();
    document.m_bComicView = false;
    REQUIRE(bChatSendText(sourceText, BM_THINK, false));
    QString thinkPrefix = originalResourceString(QStringLiteral("ID_THINK_PREFIX"));
    thinkPrefix.replace(QStringLiteral("%1"), QString());
    while (!thinkPrefix.isEmpty() && thinkPrefix.front().isSpace()) thinkPrefix.remove(0, 1);
    const QByteArray expectedThink = QByteArrayLiteral("PRIVMSG ") + channel
        + QByteArrayLiteral(" :\001ACTION ") + thinkPrefix.toUtf8()
        + sourceText.toUtf8() + '\001' + QByteArrayLiteral("\r\n");
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent[0] == expectedThink);

    protocol.sent.clear();
    document.m_bComicView = true;
    selected.SetExternal(true);
    g_rgpuiWhisperees = {&selected};
    REQUIRE(bChatSendText(sourceText, BM_WHISPER, false, nullptr, nullptr, true));
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent[0] == QByteArrayLiteral("PRIVMSG ") + talkTo
            + QByteArrayLiteral(" :...\r\n"));

    protocol.sent.clear();
    protocol.m_pSock->m_nMaxMsgLength = 96;
    const QString longSourceText = sourceText.repeated(40);
    REQUIRE(protocol.bChatSendToTarget(QString(), QString(), longSourceText,
                                       BM_SAY, false));
    REQUIRE(protocol.sent.size() > 1);
    const QByteArray wirePrefix = QByteArrayLiteral("PRIVMSG ") + channel
        + QByteArrayLiteral(" :");
    QByteArray reassembled;
    const int receivingPrefixLength = 2 + protocol.GetMyNickName().toUtf8().size()
        + QByteArray(GetMyUserName()).size() + 32;
    for (const QByteArray& line : protocol.sent) {
        REQUIRE(line.startsWith(wirePrefix));
        REQUIRE(line.endsWith(QByteArrayLiteral("\r\n")));
        REQUIRE(line.size() + receivingPrefixLength <= protocol.m_pSock->m_nMaxMsgLength);
        reassembled += line.mid(wirePrefix.size(),
                                line.size() - wirePrefix.size() - 2);
    }
    REQUIRE(reassembled == longSourceText.toUtf8());

    protocol.sent.clear();
    protocol.m_pSock->m_nMaxMsgLength = g_nDefaultIOBuff;
    const QString quotedSourceText = originalResourceString(
        QStringLiteral("ID_THINK_PREFIX")) + QStringLiteral("\r\n");
    REQUIRE(protocol.bChatSendToTarget(QString(), QString(), quotedSourceText,
                                       BM_SAY, false));
    QByteArray quotedBytes = originalResourceString(
        QStringLiteral("ID_THINK_PREFIX")).toUtf8();
    quotedBytes += QByteArrayLiteral("\020r\020n");
    REQUIRE(protocol.sent.size() == 1);
    REQUIRE(protocol.sent[0] == wirePrefix + quotedBytes + QByteArrayLiteral("\r\n"));

    g_rgpuiWhisperees.clear();
    document.m_memberList = nullptr;
    document.m_sayWnd = nullptr;
    selectedAvatar->m_userInfo = nullptr;
    avatar->m_userInfo = nullptr;
    g_puiSelf = nullptr;
    document.m_puiSelf = nullptr;
    SetChatDoc(nullptr);
    document.m_proto = nullptr;
    CUnitPanelPage::DestroyFonts();
    DestroyAvatars();
    return 0;
}
