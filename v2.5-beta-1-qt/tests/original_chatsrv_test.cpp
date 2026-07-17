#include "chat.h"
#include "chatsrv.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "proppage.h"
#include "utils.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFontMetrics>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}

#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

constexpr auto kParentPath = "Software/Microsoft/Microsoft Comic Chat";

void prepareSettings(const QString& file, bool migrated,
                     const QString& oldServerList = QString())
{
    QSettings settings(file, QSettings::IniFormat);
    settings.clear();
    settings.setValue(QString::fromLatin1(kParentPath)
                          + QStringLiteral("/PrepopulatedServers"),
                      QStringLiteral("none"));
    if (migrated) {
        settings.setValue(QString::fromLatin1(kParentPath)
                              + QStringLiteral("/ServersMigrated"), true);
    }
    if (!oldServerList.isNull()) {
        settings.setValue(QString::fromLatin1(kParentPath)
                              + QStringLiteral("/ServerList"), oldServerList);
    }
    settings.sync();
    REQUIRE(settings.status() == QSettings::NoError);
}

QStringList sourceServers()
{
    return originalResourceString(QStringLiteral("IDS_DEFAULT_SERVERLIST"))
        .split(QLatin1Char(';'), Qt::SkipEmptyParts);
}

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
    const int baseX = qMax(1,
        (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
    const int baseY = qMax(1, metrics.height());
    const auto x = [baseX](int dlu) { return (dlu * baseX + 2) / 4; };
    const auto y = [baseY](int dlu) { return (dlu * baseY + 4) / 8; };
    return {x(control.x), y(control.y),
            x(control.width), y(control.height)};
}

int findText(const QComboBox* combo, const QString& text)
{
    for (int index = 0; index < combo->count(); ++index) {
        if (combo->itemText(index).compare(text, Qt::CaseInsensitive) == 0)
            return index;
    }
    return -1;
}

unsigned int byteAt(const QByteArray& data, qsizetype offset)
{
    return static_cast<unsigned char>(data.at(offset));
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    REQUIRE(CommunicationInits());

    const QString groupName = originalResourceString(
        QStringLiteral("IDS_PREDEFGROUP_MS"));
    const QString secondGroupName = originalResourceString(
        QStringLiteral("IDS_PREDEFGROUP_MSN"));
    const QString userName = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString password = originalResourceString(
        QStringLiteral("IDS_DEFAULT_CHANNEL"));
    const QStringList servers = sourceServers();
    REQUIRE(!groupName.isEmpty() && !secondGroupName.isEmpty()
            && !userName.isEmpty() && !password.isEmpty()
            && servers.size() >= 2);

    // CChatService preserves the three source forms and NULL/empty semantics.
    {
        CChatService group(QStringLiteral("//") + groupName);
        REQUIRE(group.GetGroup() == groupName);
        REQUIRE(group.GetServer().isNull());
        REQUIRE(group.GetDisplayName() == groupName);
        REQUIRE(group.FormatAsServiceName()
                == QStringLiteral("//") + groupName);

        CChatService groupedServer(
            QStringLiteral("//") + groupName + QLatin1Char('/') + servers[0]);
        REQUIRE(groupedServer.GetGroup() == groupName);
        REQUIRE(groupedServer.GetServer() == servers[0]);
        REQUIRE(groupedServer.GetDisplayName() == servers[0]);

        CChatService server(servers[0]);
        REQUIRE(server.GetGroup().isNull());
        REQUIRE(server.GetServer() == servers[0]);
        REQUIRE(server.FormatAsServiceName() == servers[0]);

        CChatService unassociated(g_szGroupUnassociated, servers[0]);
        REQUIRE(unassociated.GetGroup().isNull());
        REQUIRE(unassociated.GetServer() == servers[0]);

        QString translated;
        int port = 0;
        TranslateServerNameToServerAndPort(
            servers[0] + QStringLiteral(":6000"), &translated, &port);
        REQUIRE(translated == servers[0] && port == 6000);
        TranslateServerNameToServerAndPort(
            servers[0] + QStringLiteral(":5999"), &translated, &port);
        REQUIRE(translated == servers[0] && port == 6667);
    }

    // Binary server records retain source order, sizes and the source fall-through.
    {
        CChatServer source(servers[0]);
        source.m_nPort = 6000;
        source.m_nAuthenticationType = CChatServer::authtypePlainText;
        source.m_pszUserName = userName;
        source.m_pszPassword = password;
        source.m_bRememberPassword = TRUE;
        QByteArray data;
        REQUIRE(source.WriteToData(&data));

        const QByteArray encodedUser = userName.toLocal8Bit();
        qsizetype offset = 0;
        REQUIRE(byteAt(data, offset++) == CChatServer::datatypePort);
        REQUIRE(byteAt(data, offset++) == (6000 & 0xff));
        REQUIRE(byteAt(data, offset++) == ((6000 >> 8) & 0xff));
        REQUIRE(byteAt(data, offset++)
                == CChatServer::datatypeAuthenticationType);
        REQUIRE(byteAt(data, offset++) == CChatServer::authtypePlainText);
        REQUIRE(byteAt(data, offset++) == CChatServer::datatypeUserName);
        REQUIRE(byteAt(data, offset++) == 0);
        REQUIRE(data.mid(offset, encodedUser.size()) == encodedUser);
        offset += encodedUser.size();
        REQUIRE(byteAt(data, offset++) == 0);
        REQUIRE(byteAt(data, offset++) == CChatServer::datatypeUserPassword);
        REQUIRE(byteAt(data, offset++) == 1);
        offset += 68;
        REQUIRE(byteAt(data, offset++)
                == CChatServer::datatypeRememberPassword);
        REQUIRE(byteAt(data, offset++) == 1);
        REQUIRE(offset == data.size());

        CChatServer restored(servers[0], data);
        REQUIRE(restored.m_nPort == 6000);
        REQUIRE(restored.m_nAuthenticationType
                == CChatServer::authtypePlainText);
        REQUIRE(restored.m_pszUserName == userName);
        REQUIRE(restored.m_pszPassword == password);
        REQUIRE(restored.m_bRememberPassword);

        const QByteArray package = secondGroupName.toLocal8Bit();
        QByteArray fallThrough;
        fallThrough.append(static_cast<char>(
            CChatServer::datatypeRememberPassword));
        fallThrough.append(static_cast<char>(1));
        fallThrough.append(package);
        fallThrough.append('\0');
        CChatServer sourceFallThrough(servers[0], fallThrough);
        REQUIRE(sourceFallThrough.m_bRememberPassword);
        REQUIRE(sourceFallThrough.m_pszSecurityPackages == secondGroupName);

        CChatServer custom(servers[1]);
        custom.m_nAuthenticationType =
            CChatServer::authtypeCustomPackages;
        custom.m_pszSecurityPackages = secondGroupName;
        REQUIRE(custom.WriteToData(&data));
        REQUIRE(byteAt(data, 0)
                == CChatServer::datatypeAuthenticationType);
        REQUIRE(byteAt(data, 1)
                == CChatServer::authtypeCustomPackages);
        REQUIRE(byteAt(data, 2) == CChatServer::datatypeSecurityPkg);
        CChatServer restoredCustom(servers[1], data);
        REQUIRE(restoredCustom.m_pszSecurityPackages == secondGroupName);
    }

    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());

    // Fresh migration uses the authoritative default list only in its source branch.
    {
        const QString freshFile = temporary.filePath(QStringLiteral("fresh.ini"));
        prepareSettings(freshFile, false);
        CChatServiceList fresh(freshFile);
        REQUIRE(fresh.ReadFromRegistry());
        CChatServerGroup* group = fresh.FindGroup(groupName);
        REQUIRE(group != nullptr);
        REQUIRE(group->GetServerCount() == servers.size());
        for (const QString& server : servers)
            REQUIRE(group->FindServer(server) != nullptr);

        const QString migratedFile = temporary.filePath(
            QStringLiteral("migrated.ini"));
        prepareSettings(migratedFile, true);
        CChatServiceList migrated(migratedFile);
        REQUIRE(migrated.ReadFromRegistry());
        REQUIRE(migrated.Groups().empty());
        REQUIRE(migrated.Services().empty());

        const QString oldFile = temporary.filePath(QStringLiteral("old.ini"));
        prepareSettings(oldFile, false, servers[0]);
        CChatServiceList old(oldFile);
        REQUIRE(old.ReadFromRegistry());
        group = old.FindGroup(groupName);
        REQUIRE(group != nullptr);
        REQUIRE(group->GetServerCount() == 1);
        REQUIRE(group->FindServer(servers[0]) != nullptr);
        REQUIRE(group->FindServer(servers[1]) == nullptr);
    }

    // Persistence, case rules, lazy group loading and service order.
    {
        const QString file = temporary.filePath(QStringLiteral("model.ini"));
        prepareSettings(file, true);
        CChatServiceList list(file);
        REQUIRE(list.ReadFromRegistry());
        CChatServerGroup* group = list.CreateGroup(groupName);
        REQUIRE(group != nullptr);
        CChatServer* server = group->CreateServer(servers[0], 6000);
        REQUIRE(server != nullptr);
        REQUIRE(group->FindServer(servers[0].toUpper()) == nullptr);
        REQUIRE(list.FindGroup(groupName.toLower()) == group);

        CChatService* groupService = list.FindService(groupName, QString());
        REQUIRE(groupService != nullptr);
        CChatService* serverService = list.CreateService(QString(), servers[0]);
        REQUIRE(serverService != nullptr);
        CChatService* first = nullptr;
        REQUIRE(list.EnumServices(first) && first == serverService);
        list.MoveServiceToTop(groupService);
        first = nullptr;
        REQUIRE(list.EnumServices(first) && first == groupService);
        REQUIRE(list.WriteIfChanged());

        CChatServiceList restored(file);
        REQUIRE(restored.ReadFromRegistry());
        group = restored.FindGroup(groupName);
        REQUIRE(group != nullptr && !group->IsRead());
        REQUIRE(group->ContainsServer(servers[0]));
        REQUIRE(!group->IsRead());
        REQUIRE(group->FindServer(servers[0]) != nullptr);
        REQUIRE(group->IsRead());
    }

    // CChatServiceUI keeps changes isolated until Apply and discards Revert.
    {
        const QString file = temporary.filePath(QStringLiteral("ui.ini"));
        prepareSettings(file, true);
        CChatServiceList list(file);
        REQUIRE(list.ReadFromRegistry());
        REQUIRE(list.CreateGroup(groupName) != nullptr);

        {
            CChatServiceUI ui;
            ui.SetServiceList(&list);
            HCHATSRVGROUP added = ui.AddGroup(secondGroupName);
            REQUIRE(added != nullptr);
            REQUIRE(ui.AddServer(added, servers[0], 6000) != nullptr);
            REQUIRE(list.FindGroup(secondGroupName) == nullptr);
            ui.Revert();
            REQUIRE(list.FindGroup(secondGroupName) == nullptr);
        }

        CChatServiceUI ui;
        ui.SetServiceList(&list);
        HCHATSRVGROUP added = ui.AddGroup(secondGroupName);
        HCHATSERVER addedServer = ui.AddServer(added, servers[0], 6000);
        REQUIRE(added != nullptr && addedServer != nullptr);
        CChatServiceUI::ServerProps props;
        props.m_nPort = 6000;
        props.m_nAuthenticationType = CChatServer::authtypePlainText;
        props.m_strUserName = userName;
        props.m_strPassword = password;
        props.m_bRememberPassword = TRUE;
        REQUIRE(ui.SetServerProps(added, addedServer, props));
        REQUIRE(ui.Apply());
        CChatServerGroup* group = list.FindGroup(secondGroupName);
        REQUIRE(group != nullptr);
        CChatServer* server = group->FindServer(servers[0]);
        REQUIRE(server != nullptr && server->m_nPort == 6000);
        REQUIRE(server->m_nAuthenticationType
                == CChatServer::authtypePlainText);
        REQUIRE(server->m_pszUserName == userName);
        REQUIRE(server->m_pszPassword == password);
        REQUIRE(server->m_bRememberPassword);
    }

    // The direct-resource service combo keeps networks before servers and ICOs direct.
    {
        const QString file = temporary.filePath(QStringLiteral("combo.ini"));
        prepareSettings(file, true);
        CChatServiceList list(file);
        REQUIRE(list.ReadFromRegistry());
        REQUIRE(list.CreateGroup(groupName) != nullptr);
        CChatService* serverService = list.CreateService(QString(), servers[0]);
        CChatServiceComboBox combo;
        combo.SetServiceList(&list);
        combo.Fill();
        REQUIRE(combo.count() == 2);
        REQUIRE(combo.itemText(0) == groupName);
        REQUIRE(combo.itemText(1) == servers[0]);
        REQUIRE(!combo.itemIcon(0).isNull());
        REQUIRE(!combo.itemIcon(1).isNull());
        REQUIRE(combo.itemData(0, Qt::UserRole + 1).toInt()
                == CChatServiceComboBox::flagGroupEntry);
        REQUIRE(combo.itemData(1, Qt::UserRole + 1).toInt()
                == CChatServiceComboBox::flagServerEntry);
        REQUIRE(combo.GetServiceAt(1) == serverService);
        REQUIRE(originalFileResourcePath(
                    QStringLiteral("IDI_CONNECT_SRV"),
                    QStringLiteral("ICON"))
                == originalResourcePath(QStringLiteral("tosrv.ico")));
        REQUIRE(originalFileResourcePath(
                    QStringLiteral("IDI_CONNECT_NET"),
                    QStringLiteral("ICON"))
                == originalResourcePath(QStringLiteral("tonet.ico")));
    }

    // CServersPage is the source DLU page, uses CBS_SIMPLE behaviour and is atomic.
    {
        const QString file = temporary.filePath(QStringLiteral("page.ini"));
        prepareSettings(file, true);
        CChatServiceList list(file);
        REQUIRE(list.ReadFromRegistry());
        CChatServerGroup* group = list.CreateGroup(groupName);
        REQUIRE(group != nullptr);
        REQUIRE(group->CreateServer(servers[0], 6000) != nullptr);

        {
            CServersPage page(nullptr, &list);
            page.show();
            application.processEvents();
            const OriginalDialogResource resource = originalDialogResource(
                QStringLiteral("IDD_SERVERSPAGE"));
            REQUIRE(resource.width == 252 && resource.height == 218);
            REQUIRE(page.layout() == nullptr);
            REQUIRE(page.size() == QSize(
                resourceRect(OriginalDialogControl{
                    {}, {}, {}, {}, true, 0, 0,
                    resource.width, resource.height, {}}, page.font()).size()));

            auto* groups = page.findChild<QComboBox*>(
                QStringLiteral("IDC_SRVGROUP_COMBO"));
            auto* simpleWidget = page.findChild<QWidget*>(
                QStringLiteral("IDC_SERVERLIST"));
            auto* simple = dynamic_cast<CSimpleComboBox*>(simpleWidget);
            auto* port = page.findChild<QSpinBox*>(
                QStringLiteral("IDC_SERVER_PORT_NW"));
            auto* user = page.findChild<QLineEdit*>(
                QStringLiteral("IDC_SRV_USERNAME"));
            auto* pass = page.findChild<QLineEdit*>(
                QStringLiteral("IDC_SRV_PASSWORD"));
            auto* addGroup = page.findChild<QPushButton*>(
                QStringLiteral("IDC_SRVGROUP_ADD"));
            REQUIRE(groups && simple && port && user && pass && addGroup);
            REQUIRE(groups->geometry() == resourceRect(
                *resourceControl(resource,
                                 QStringLiteral("IDC_SRVGROUP_COMBO")),
                page.font()));
            REQUIRE(simple->geometry() == resourceRect(
                *resourceControl(resource, QStringLiteral("IDC_SERVERLIST")),
                page.font()));
            REQUIRE(simple->listWidget()->isVisible());
            REQUIRE(groups->lineEdit()->maxLength() == 40);
            REQUIRE(simple->lineEdit()->maxLength() == 64);
            REQUIRE(user->maxLength() == 30 && pass->maxLength() == 30);
            REQUIRE(port->minimum() == 6000 && port->maximum() == 7000);
            REQUIRE(addGroup->text() == originalDialogControlText(
                QStringLiteral("IDD_SERVERSPAGE"),
                QStringLiteral("IDC_SRVGROUP_ADD")));

            const int groupIndex = findText(groups, groupName);
            const int noneIndex = findText(groups,
                originalResourceString(QStringLiteral("IDS_UNASSOCIATED_GROUP")));
            REQUIRE(groupIndex >= 0 && noneIndex >= 0);
            REQUIRE(!groups->itemIcon(groupIndex).isNull());
            REQUIRE(groups->itemIcon(noneIndex).isNull());

            groups->setCurrentIndex(-1);
            groups->setEditText(secondGroupName);
            groups->lineEdit()->textEdited(secondGroupName);
            REQUIRE(addGroup->isEnabled());
            addGroup->click();
            REQUIRE(list.FindGroup(secondGroupName) == nullptr);
        }
        REQUIRE(list.FindGroup(secondGroupName) == nullptr);
        REQUIRE(list.FindGroup(g_szGroupUnassociated) == nullptr);

        {
            CServersPage page(nullptr, &list);
            auto* groups = page.findChild<QComboBox*>(
                QStringLiteral("IDC_SRVGROUP_COMBO"));
            auto* addGroup = page.findChild<QPushButton*>(
                QStringLiteral("IDC_SRVGROUP_ADD"));
            REQUIRE(groups && addGroup);
            groups->setCurrentIndex(-1);
            groups->setEditText(secondGroupName);
            groups->lineEdit()->textEdited(secondGroupName);
            REQUIRE(addGroup->isEnabled());
            addGroup->click();
            REQUIRE(page.apply());
        }
        REQUIRE(list.FindGroup(secondGroupName) != nullptr);
        REQUIRE(list.FindGroup(g_szGroupUnassociated) != nullptr);
    }

    // Password and options pages retain their original resources/order.
    {
        CChatPasswordDialog passwordDialog(servers[0], userName, TRUE);
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_PASSWORD"));
        REQUIRE(passwordDialog.objectName() == QStringLiteral("IDD_PASSWORD"));
        REQUIRE(passwordDialog.windowTitle() == resource.caption);
        REQUIRE(passwordDialog.findChild<QLineEdit*>(
                    QStringLiteral("IDC_PASSWORD")) != nullptr);
        auto* remember = passwordDialog.findChild<QCheckBox*>(
            QStringLiteral("IDC_REMEMBER_PASSWORD"));
        REQUIRE(remember && remember->isChecked());

        COptionsDialog options;
        QTabWidget* tabs = options.findChild<QTabWidget*>();
        REQUIRE(tabs != nullptr && tabs->count() > 0);
        REQUIRE(tabs->tabText(tabs->count() - 1)
                == originalDialogCaption(QStringLiteral("IDD_SERVERSPAGE")));
    }

    // Resolver/socket replacement keeps the source five-candidate state machine.
    {
        const QString file = temporary.filePath(
            QStringLiteral("connector.ini"));
        prepareSettings(file, true);
        CChatServiceList list(file);
        REQUIRE(list.ReadFromRegistry());
        CChatServerGroup* group = list.CreateGroup(groupName);
        REQUIRE(group != nullptr);
        for (int index = 0; index < 6; ++index) {
            const int port = 6000 + index;
            const QString endpoint = QStringLiteral("127.0.0.1:%1").arg(port);
            REQUIRE(group->CreateServer(endpoint, port) != nullptr);
        }

        CChatServiceConnector connector;
        connector.SetServiceList(&list);
        REQUIRE(connector.BeginConnectToService(
            QStringLiteral("//") + groupName));
        REQUIRE(connector.GetNumServers() == 6);
        REQUIRE(connector.GetNumSockets() == 5);
        const QVector<BYTE> statuses = connector.ConnectionStatuses();
        REQUIRE(statuses.size() == 6);
        for (BYTE status : statuses) {
            REQUIRE(status == CChatServiceConnector::srvconnNotAttempted);
        }
        connector.Cleanup(FALSE);
        REQUIRE(connector.BeginConnectToService(QString()));
        REQUIRE(connector.GetNumSockets() == 5);
        connector.Cleanup();
    }

    // A local-only socket proves winner hand-off and the source OnConnect query.
    {
        QTcpServer listener;
        quint16 listenerPort = 0;
        for (quint16 port = 6000; port <= 7000; ++port) {
            if (listener.listen(QHostAddress::AnyIPv4, port)) {
                listenerPort = port;
                break;
            }
        }
        if (listenerPort == 0) {
            std::fprintf(stderr, "loopback winner check skipped: %s\n",
                         listener.errorString().toLocal8Bit().constData());
        } else {
            REQUIRE(listenerPort >= 6000 && listenerPort <= 7000);

            const QString file = temporary.filePath(
                QStringLiteral("winner.ini"));
            prepareSettings(file, true);
            CChatServiceList list(file);
            REQUIRE(list.ReadFromRegistry());
            CChatServerGroup* group = list.CreateGroup(groupName);
            REQUIRE(group != nullptr);
            const QString localAddress = QStringLiteral("127.0.0.1");
            CChatServer* winningServer = group->CreateServer(
                localAddress, listenerPort);
            REQUIRE(winningServer != nullptr);

            CChatServiceConnector connector;
            connector.SetServiceList(&list);
            GetIrcProto()->SetConnectionStatus(CX_CONNECTING);
            REQUIRE(connector.BeginConnectToService(
                QStringLiteral("//") + groupName + QLatin1Char('/')
                + localAddress));
            REQUIRE(connector.GetNumSockets() == 1);
            REQUIRE(connector.AssignSocket(0) == 1);

            QElapsedTimer timeout;
            timeout.start();
            while (timeout.elapsed() < 2000
                   && connector.GetConnectingServer() != winningServer) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            REQUIRE(connector.GetConnectingServer() == winningServer);
            REQUIRE(connector.GetConnectingServerGroup() == group);
            REQUIRE(connector.GetNumSockets() == 0);
            REQUIRE(connector.ConnectionStatuses().size() == 1);
            REQUIRE(connector.ConnectionStatuses().first()
                    == CChatServiceConnector::srvconnConnected);
            REQUIRE(listener.hasPendingConnections());

            QTcpSocket* peer = listener.nextPendingConnection();
            REQUIRE(peer != nullptr);
            timeout.restart();
            while (timeout.elapsed() < 2000 && peer->bytesAvailable() == 0)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            const QByteArray wire = peer->readAll();
            if (wire != QByteArray("MODE ISIRCX\r\n")) {
                std::fprintf(stderr, "winner wire size: %lld\n",
                             static_cast<long long>(wire.size()));
            }
            REQUIRE(wire == QByteArray("MODE ISIRCX\r\n"));

            serverConn.Disconnect();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            serverConn.Reset();
            GetIrcProto()->SetConnectionStatus(CX_DISCONNECTED);
            connector.Cleanup();
            peer->deleteLater();
            listener.close();
        }
    }

    CommunicationCleanup();

    return 0;
}
