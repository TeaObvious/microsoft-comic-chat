// Ported from v2.5-beta-1-modern/chatsrv.h.
// QSettings, QHostInfo and QTcpSocket replace only Registry, resolver and
// WinSock/MFC boundaries. Service names, records, state transitions and the
// original class/function boundaries remain here.

#pragma once

#include "wincompat.h"

#include <QByteArray>
#include <QComboBox>
#include <QDialog>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

class CChatServiceList;
class CChatServerGroup;
class QObject;
class QLineEdit;
class QTcpSocket;

inline const QString g_szServicesRegName = QStringLiteral("Servers");
inline const QString g_szServicesList = QStringLiteral(".SvcList");
inline const QString g_szGroupUnassociated =
    QStringLiteral("{Unassociated}");

class CChatServer {
public:
    enum AuthType {
        authtypeNone = 0,
        authtypePlainText = 1,
        authtypeServerPackages = 2,
        authtypeCustomPackages = 3,
    };
    enum DataType {
        datatypePort = 0x01,
        datatypeAuthenticationType = 0x02,
        datatypeUserName = 0x03,
        datatypeUserPassword = 0x04,
        datatypeSecurityPkg = 0x05,
        datatypeRememberPassword = 0x06,
    };

    explicit CChatServer(const QString& name,
                         const QByteArray& data = QByteArray());

    void SetDefaultSettings();
    void FreeSettings();
    BOOL ResolveSocketAddress();
    void ReadFromData(const QByteArray& data);
    BOOL WriteToData(QByteArray* dataOut) const;
    BOOL WriteToRegistry();

    QString m_pszName;
    UINT m_nPort = 6667;
    UINT m_nAuthenticationType = authtypeNone;
    QString m_pszUserName;
    QString m_pszPassword;
    QString m_pszSecurityPackages;
    BOOL m_bRememberPassword = FALSE;
    QHostAddress m_sockaddr;
    BOOL m_bResolveFailed = FALSE;

private:
    friend class CChatServerGroup;
    friend class CChatServiceUI;
    CChatServerGroup* m_pGroup = nullptr;
};

class CChatServerGroup {
public:
    explicit CChatServerGroup(const QString& name,
                              CChatServiceList* owner = nullptr);

    BOOL ContainsServer(const QString& server);
    CChatServer* FindServer(const QString& server);
    CChatServer* CreateServer(const QString& name, int port = 6667);
    BOOL DestroyServer(CChatServer* server);
    BOOL IsEmpty();
    int GetServerCount();
    BOOL EnumServers(CChatServer*& server);
    void SetLastAccessedServer(CChatServer* server);

    QString m_pszName;
    QString m_pszLastServer;

    BOOL IsRead() const { return m_bIsRead; }

private:
    friend class CChatServer;
    friend class CChatServiceList;
    friend class CChatServiceUI;
    BOOL ReadFromRegistry();

    CChatServiceList* m_pOwner = nullptr;
    BOOL m_bIsRead = FALSE;
    std::vector<std::unique_ptr<CChatServer>> m_listServers;
};

class CChatService {
public:
    explicit CChatService(const QString& service);
    CChatService(const QString& group, const QString& server);

    const QString& GetGroup() const { return m_pszGroup; }
    const QString& GetServer() const { return m_pszServer; }
    QString GetDisplayName() const;
    void FormatAsServiceName(QString& out) const;
    QString FormatAsServiceName() const;

private:
    void CommonConstruct(const QString& group, const QString& server);
    QString m_pszGroup;
    QString m_pszServer;
};

void TranslateServerNameToServerAndPort(const QString& server,
                                        QString* translatedServer,
                                        int* port);

class CChatServiceList {
public:
    explicit CChatServiceList(const QString& settingsFile = QString());
    ~CChatServiceList();

    BOOL ReadFromRegistry();
    BOOL WriteToRegistry();
    BOOL WriteIfChanged();
    CChatServerGroup* CreateGroup(const QString& name);
    BOOL DestroyGroup(CChatServerGroup* group);
    BOOL EnumGroups(CChatServerGroup*& group, BOOL& unassociatedGroup);
    BOOL EnumServices(CChatService*& service);
    CChatServerGroup* FindGroup(const QString& name) const;
    CChatService* FindService(const QString& group,
                              const QString& server) const;
    CChatService* FindServiceForServer(const QString& server) const;
    void MoveServiceToTop(CChatService* service);
    void DestroyService(CChatService* service);
    CChatService* CreateService(const QString& group,
                                const QString& server);
    void GetServiceNameFromDisplayName(const QString& displayName,
                                       QString& service);
    void RemoveReferences(const QString& group, const QString& server);
    BOOL ImportFromFile(const QString& fileName);

    const std::vector<std::unique_ptr<CChatService>>& Services() const
    {
        return m_listServices;
    }
    const std::vector<std::unique_ptr<CChatServerGroup>>& Groups() const
    {
        return m_listSrvGroups;
    }
    QString SettingsFile() const { return m_settingsFile; }
    void MarkServersMigrated();

private:
    friend class CChatServer;
    friend class CChatServerGroup;
    friend class CChatServiceUI;

    std::unique_ptr<class QSettings> OpenSettings() const;
    QString ParentPath() const;
    QString ServicesPath() const;
    QString ServiceListPath() const;
    QString GroupPath(const QString& group) const;
    BOOL GetGroupForOldServer(const QString& server, QString& groupOut);
    BOOL AddOldServer(const QString& server);
    QByteArray ReadServerData(const QString& group,
                              const QString& server) const;
    BOOL WriteServerData(const QString& group, const QString& server,
                         const QByteArray& data);
    BOOL RemoveServerData(const QString& group, const QString& server);
    QStringList StoredServerNames(const QString& group) const;
    QString StoredLastServer(const QString& group) const;
    void WriteLastServer(const QString& group, const QString& server);
    void EnsureGroupStorage(const QString& group);

    std::vector<std::unique_ptr<CChatService>> m_listServices;
    std::vector<std::unique_ptr<CChatServerGroup>> m_listSrvGroups;
    int m_nOldReadCount = 0;
    BOOL m_bSvcListModified = FALSE;
    QString m_settingsFile;
};

using HCHATSRVGROUP = void*;
using HCHATSERVER = void*;

class CChatServiceUI {
public:
    struct ServerProps {
        const ServerProps& operator=(const ServerProps& data);
        const ServerProps& operator=(const CChatServer& server);

        UINT m_nPort = 6667;
        UINT m_nAuthenticationType = CChatServer::authtypeNone;
        QString m_strUserName;
        QString m_strPassword;
        QString m_strSecurityPackages;
        BOOL m_bRememberPassword = FALSE;
        CChatServerGroup* m_pGroupIn = nullptr;
    };

    CChatServiceUI();
    ~CChatServiceUI();

    void SetServiceList(CChatServiceList* list);
    HCHATSRVGROUP EnumGroups(HCHATSRVGROUP& position,
                             BOOL& unassociatedGroup);
    HCHATSERVER EnumServersInGroup(HCHATSRVGROUP group,
                                   HCHATSERVER& position);
    QString GetGroupName(HCHATSRVGROUP group) const;
    QString GetServerName(HCHATSERVER server) const;
    void GetServerProps(HCHATSERVER server, ServerProps& data) const;
    BOOL IsGroupEmpty(HCHATSRVGROUP group);

    BOOL SetServerProps(HCHATSRVGROUP group, HCHATSERVER server,
                        const ServerProps& data);
    HCHATSERVER AddServer(HCHATSRVGROUP group, const QString& server,
                          int port);
    BOOL RemoveServer(HCHATSRVGROUP group, HCHATSERVER server);
    HCHATSRVGROUP AddGroup(const QString& group);
    BOOL RemoveGroup(HCHATSRVGROUP group);

    BOOL Apply();
    void Revert();
    BOOL ChangesMade() const { return m_bChangesMade; }

private:
    struct GroupState;
    struct ServerState;

    void Reset(BOOL onDestruction = FALSE);
    void BuildState();
    GroupState* GroupFromHandle(HCHATSRVGROUP group) const;
    ServerState* ServerFromHandle(HCHATSERVER server) const;
    QList<GroupState*> CurrentGroups() const;
    QList<ServerState*> CurrentServers(GroupState* group) const;

    BOOL m_bChangesMade = FALSE;
    CChatServiceList* m_pSvcList = nullptr;
    std::vector<std::unique_ptr<GroupState>> m_groups;
    std::vector<std::unique_ptr<ServerState>> m_servers;
};

class CChatServiceConnector {
public:
    enum SrvConnectionStatus {
        srvconnUnresolved = 0,
        srvconnNotAttempted = 1,
        srvconnAttempting = 2,
        srvconnFailed = 3,
        srvconnConnected = 4,
    };

    CChatServiceConnector();
    ~CChatServiceConnector();

    void SetServiceList(CChatServiceList* serviceList)
    {
        m_pSvcList = serviceList;
    }
    BOOL IsConnecting() const { return !m_connections.isEmpty(); }
    int GetNumServers() const { return m_connections.size(); }
    BOOL BeginConnectToService(const QString& service);
    int AssignSocket(int socket);
    int GetNumSockets() const
    {
        return static_cast<int>(m_sockets.size());
    }
    void Cleanup(BOOL cleanupServerList = TRUE);
    CChatServerGroup* GetConnectingServerGroup() const
    {
        return m_pConnectingGroup;
    }
    CChatServer* GetConnectingServer() const
    {
        return m_pConnectingServer;
    }
    QVector<BYTE> ConnectionStatuses() const;

    QString m_strSvc;

private:
    struct SrvConnection {
        CChatServer* pServer = nullptr;
        BYTE byStatus = srvconnUnresolved;
    };
    class Socket;

    BOOL AssignSocketToNewServer(int socket);
    void ResolveConnection(int connection, quint64 generation);
    void SocketConnected(Socket* socket);
    void SocketFailed(Socket* socket);

    CChatServiceList* m_pSvcList = nullptr;
    QVector<SrvConnection> m_connections;
    std::vector<std::unique_ptr<Socket>> m_sockets;
    // Mirrors the source m_mapDeleteableSockets lifetime barrier: a Socket
    // must survive until its own connected/error callback has returned.
    std::vector<std::unique_ptr<Socket>> m_deleteableSockets;
    int m_nActiveSockets = 0;
    CChatServerGroup* m_pConnectingGroup = nullptr;
    CChatServer* m_pConnectingServer = nullptr;
    QObject* m_context = nullptr;
    quint64 m_generation = 0;
};

class CChatServiceComboBox : public QComboBox {
public:
    explicit CChatServiceComboBox(QWidget* parent = nullptr);

    void SetServiceList(CChatServiceList* serviceList)
    {
        m_pSvcList = serviceList;
    }
    void Fill(BOOL nonEmptyGroupsOnly = FALSE);
    CChatService* GetServiceAt(int index) const;

    enum Flags {
        flagtypeEntry = 0x03,
        flagUserEntry = 0x00,
        flagServerEntry = 0x01,
        flagGroupEntry = 0x02,
    };

private:
    CChatServiceList* m_pSvcList = nullptr;
};

class CChatPasswordDialog : public QDialog {
public:
    CChatPasswordDialog(const QString& serverName,
                        const QString& userName,
                        BOOL rememberPassword,
                        QWidget* parent = nullptr);

    QString m_strPassword;
    BOOL m_bRememberPassword = FALSE;

protected:
    void accept() override;

private:
    QLineEdit* m_password = nullptr;
    class QCheckBox* m_remember = nullptr;
};
