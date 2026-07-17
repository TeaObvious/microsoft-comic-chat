#include "actions.h"
#include "chat.h"
#include "ircproto.h"
#include "ircsock.h"
#include "notipage.h"
#include "originalassets.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "userlist.h"

#include <QApplication>
#include <QComboBox>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}
#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

const OriginalDialogControl* resourceControl(
    const OriginalDialogResource& dialog, const QString& identifier)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier == identifier) return &control;
    }
    return nullptr;
}

QRect resourceRect(const OriginalDialogControl& control, const QFont& font)
{
    const QFontMetrics metrics(font);
    const QString alphabet = QStringLiteral(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
    const int baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
    const int baseY = qMax(1, metrics.height());
    const auto x = [baseX](int dlu) { return (dlu * baseX + 2) / 4; };
    const auto y = [baseY](int dlu) { return (dlu * baseY + 4) / 8; };
    return {x(control.x), y(control.y),
            x(control.width), y(control.height)};
}

CUser* sourceUser(const QString& nick, const QString& identity,
                  const QString& fullName, const QString& room)
{
    auto* user = new CUser;
    user->m_strNickname = nick;
    user->m_strIdentity = identity;
    user->m_strFullName = fullName;
    user->m_strRoom = room;
    user->m_strPrettyRoom = DecodeChan(room);
    return user;
}

void removeDefinitions(CCDynaNotifs& notifications)
{
    while (!notifications.GetNotifsArray().isEmpty())
        REQUIRE(notifications.bRemoveNotif(nullptr, 0));
}

QString userCountText(int count)
{
    QString text = originalResourceString(QStringLiteral("IDS_NUM_USERS"));
    text.replace(QStringLiteral("%d"), QString::number(count));
    return text;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    REQUIRE(CommunicationInits());

    const QString defaultNick = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString defaultServer = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString defaultRealName = originalResourceString(
        QStringLiteral("IDS_DEFAULT_REALNAME"));
    const QString encodedRoom = EncodeChan(originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL")));
    const QString userName = QString::fromUtf8(GetMyUserName());
    const QString identity = userName + QLatin1Char('@') + defaultServer;
    const QString otherNick = QString::fromLatin1(
        g_rgIrcCmd[cmdidWho].szCmd);
    REQUIRE(!defaultNick.isEmpty() && !defaultServer.isEmpty()
            && !defaultRealName.isEmpty() && !encodedRoom.isEmpty()
            && !userName.isEmpty() && !otherNick.isEmpty());

    {
        CCDynaNotifs definitions;
        definitions = theApp.m_dynaNotifs;
        definitions.SetStartUpIdent(identity);
        CNotificationsPage page;
        page.SetDynaNotifs(&definitions);
        page.show();
        application.processEvents();

        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_NOTIFICATIONS"));
        REQUIRE(resource.width == 252 && resource.height == 218);
        REQUIRE(page.layout() == nullptr);
        auto* list = dynamic_cast<CNotifsListCtrl*>(
            page.findChild<QTreeWidget*>(QStringLiteral("IDC_LSTNOTIFS")));
        REQUIRE(list != nullptr);
        REQUIRE(list->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_LSTNOTIFS")),
            page.font()));
        REQUIRE(list->headerItem()->text(0) == originalResourceString(
            QStringLiteral("IDS_NICKARG_LABEL")));
        REQUIRE(list->headerItem()->text(3) == originalResourceString(
            QStringLiteral("IDS_NETARG_LABEL")));

        auto* nickOperator = page.findChild<QComboBox*>(
            QStringLiteral("IDC_CMBNICKOP"));
        auto* userOperator = page.findChild<QComboBox*>(
            QStringLiteral("IDC_CMBUSEROP"));
        auto* hostOperator = page.findChild<QComboBox*>(
            QStringLiteral("IDC_CMBHOSTOP"));
        auto* nickEdit = page.findChild<QLineEdit*>(
            QStringLiteral("IDC_NICKARG"));
        auto* userEdit = page.findChild<QLineEdit*>(
            QStringLiteral("IDC_USERARG"));
        auto* hostEdit = page.findChild<QLineEdit*>(
            QStringLiteral("IDC_HOSTARG"));
        auto* network = page.findChild<QComboBox*>(
            QStringLiteral("IDC_CMBNETARG"));
        auto* add = page.findChild<QPushButton*>(
            QStringLiteral("IDC_ADDNOTIF"));
        auto* modify = page.findChild<QPushButton*>(
            QStringLiteral("IDC_MODIFYNOTIF"));
        auto* remove = page.findChild<QPushButton*>(
            QStringLiteral("IDC_DELETENOTIF"));
        REQUIRE(nickOperator && userOperator && hostOperator && nickEdit
                && userEdit && hostEdit && network && add && modify && remove);
        REQUIRE(nickOperator->count() == 5);
        REQUIRE(nickOperator->itemText(g_uAny)
                == originalResourceString(QStringLiteral("IDS_OP_ANY")));
        REQUIRE(nickOperator->itemText(g_uEndsWith)
                == originalResourceString(QStringLiteral("IDS_OP_ENDSWITH")));
        REQUIRE(network->itemText(0) == originalResourceString(
            IDS_KEY_EVENT_PARAM0 + static_cast<UINT>(kepAny)));
        REQUIRE(userOperator->currentIndex() == g_uEquals);
        REQUIRE(hostOperator->currentIndex() == g_uEquals);
        REQUIRE(userEdit->text() == userName);
        REQUIRE(hostEdit->text() == defaultServer);
        REQUIRE(nickEdit->maxLength() == static_cast<int>(g_uMaxNotifParamLength));
        REQUIRE(network->lineEdit()->maxLength()
                == static_cast<int>(g_uMaxNetArgLength));

        QString invalid = QStringLiteral("!");
        int position = 0;
        REQUIRE(nickEdit->validator()->validate(invalid, position)
                == QValidator::Invalid);
        invalid = QStringLiteral("@");
        position = 0;
        REQUIRE(nickEdit->validator()->validate(invalid, position)
                == QValidator::Invalid);

        REQUIRE(add->isEnabled());
        add->click();
        REQUIRE(definitions.GetNotifsArray().size() == 1);
        CCNotif* definition = definitions.GetNotifsArray().first();
        REQUIRE(definition->bActive());
        REQUIRE(definition->GetOperator(g_uNickname) == g_uAny);
        REQUIRE(definition->GetOperator(g_uUserName) == g_uEquals);
        REQUIRE(definition->GetParam(g_uUserName) == userName);
        REQUIRE(definition->GetParam(g_uHostName) == defaultServer);
        REQUIRE(definition->GetParam(g_uNetName) == network->itemText(0));
        REQUIRE(list->topLevelItem(0)->text(0)
                == QString::fromLatin1(g_szAnyReplacement));

        QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(list, &space);
        REQUIRE(!definition->bActive());

        userEdit->setText(otherNick);
        REQUIRE(modify->isEnabled());
        modify->click();
        REQUIRE(definitions.GetNotifsArray().size() == 1);
        definition = definitions.GetNotifsArray().first();
        REQUIRE(!definition->bActive());
        REQUIRE(definition->GetParam(g_uUserName) == otherNick);

        page.SortNotifs(g_uHostName, FALSE);
        REQUIRE((definitions.GetFlags() >> 12) == g_uHostName);
        REQUIRE(definitions.GetFlags() & g_wSortDescending);

        page.OnOK();
        REQUIRE(theApp.m_dynaNotifs.GetNotifsArray().size() == 1);
        REQUIRE(!theApp.m_dynaNotifs.GetNotifsArray().first()->bActive());
        remove->click();
        REQUIRE(definitions.GetNotifsArray().isEmpty());
        removeDefinitions(theApp.m_dynaNotifs);
    }

    theApp.m_dynaNotifs.bRemoveAllUsers();
    DestroyNotificationBox();
    {
        CUser* first = sourceUser(otherNick, identity, defaultRealName,
                                  encodedRoom);
        REQUIRE(theApp.m_dynaNotifs.bAddNotificationUser(first));
        first->Release();
        REQUIRE(theApp.m_dynaNotifs.GetModifiedUsersCount() == 1);
        REQUIRE(bDisplayNotifications(&theApp.m_dynaNotifs));
        REQUIRE(theApp.m_dynaNotifs.GetModifiedUsersCount() == 0);

        CNotificationUsers* usersWindow = GetNotifBox();
        REQUIRE(usersWindow != nullptr);
        application.processEvents();
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_NOTIFICATIONUSERS"));
        REQUIRE(resource.width == 262 && resource.height == 111);
        REQUIRE(usersWindow->layout() == nullptr);
        REQUIRE(usersWindow->windowTitle() == resource.caption);
        auto* list = usersWindow->findChild<QTreeWidget*>(
            QStringLiteral("IDC_LSTNOTIFICATIONUSERS"));
        auto* count = usersWindow->findChild<QLabel*>(
            QStringLiteral("IDC_NOTIFCOUNT"));
        auto* updateTime = usersWindow->findChild<QLabel*>(
            QStringLiteral("IDC_NOTIFTIME"));
        auto* whisper = usersWindow->findChild<QPushButton*>(
            QStringLiteral("IDC_NOTIFWHISPER"));
        auto* invite = usersWindow->findChild<QPushButton*>(
            QStringLiteral("IDC_NOTIFINVITE"));
        auto* join = usersWindow->findChild<QPushButton*>(
            QStringLiteral("IDC_NOTIFJOIN"));
        auto* clear = usersWindow->findChild<QPushButton*>(
            QStringLiteral("IDC_NOTIFCLEAR"));
        auto* define = usersWindow->findChild<QPushButton*>(
            QStringLiteral("IDC_DEFINENOTIF"));
        REQUIRE(list && count && updateTime && whisper && invite && join
                && clear && define);
        const QRect baseList = resourceRect(
            *resourceControl(resource,
                             QStringLiteral("IDC_LSTNOTIFICATIONUSERS")),
            usersWindow->font());
        REQUIRE(list->geometry().width() > baseList.width());
        REQUIRE(list->geometry().height() > baseList.height());
        REQUIRE(list->headerItem()->text(1) == originalResourceString(
            QStringLiteral("ID_UL_IDENT_LABEL")));
        REQUIRE(list->topLevelItemCount() == 1);
        REQUIRE(list->topLevelItem(0)->text(0) == otherNick);
        REQUIRE(list->topLevelItem(0)->text(1) == identity);
        REQUIRE(count->text() == userCountText(1));
        REQUIRE(updateTime->text().startsWith(
            originalResourceString(QStringLiteral("IDS_NOTIF_TIMELABEL"))
                .section(QStringLiteral("%s"), 0, 0)));
        list->setCurrentItem(list->topLevelItem(0));
        list->topLevelItem(0)->setSelected(true);
        application.processEvents();
        REQUIRE(whisper->isEnabled());
        REQUIRE(!invite->isEnabled());
        REQUIRE(join->isEnabled());
        REQUIRE(clear->isEnabled());
        REQUIRE(define->isEnabled());

        CUser changed;
        changed.m_strNickname = otherNick;
        changed.m_strIdentity = identity;
        REQUIRE(theApp.m_dynaNotifs.bModifyNotificationUser(
            &changed, 0, g_wConnected));
        REQUIRE(theApp.m_dynaNotifs.GetModifiedUsersCount() == 1);
        REQUIRE(bDisplayNotifications(&theApp.m_dynaNotifs));
        CUser* stored = static_cast<CUser*>(
            theApp.m_dynaNotifs.GetNotifUsersArray()->GetAt(0));
        REQUIRE(stored != nullptr);
        REQUIRE(!(stored->GetFlags() & g_wConnected));
        REQUIRE(stored->m_strRoom.isEmpty());
        REQUIRE(list->topLevelItemCount() == 1);
        REQUIRE(!whisper->isEnabled());
        REQUIRE(!join->isEnabled());

        clear->click();
        REQUIRE(list->topLevelItemCount() == 0);
        REQUIRE(theApp.m_dynaNotifs.GetNotifUsersArray()->GetSize() == 0);
        REQUIRE(count->text() == userCountText(0));
    }

    DestroyNotificationBox();
    theApp.m_dynaNotifs.bRemoveAllUsers();
    CommunicationCleanup();
    return EXIT_SUCCESS;
}
