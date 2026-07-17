// Ported from v2.5-beta-1-modern/chatsrv.cpp.
// The Registry, resolver, CAsyncSocket and MFC controls are the only replaced
// boundaries. No service, server or credential data is synthesized here.

#include "chatsrv.h"

#include "chat.h"
#include "ircproto.h"
#include "ircsock.h"
#include "originalassets.h"
#include "resource.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHostInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace {

constexpr auto kParentPath = "Software/Microsoft/Microsoft Comic Chat";
constexpr auto kGroupMarker = ".Exists";

QString currentEncryptionUser()
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) user = QStringLiteral("GenericUser");
    return user;
}

DWORD currentFileTimeLow()
{
    constexpr quint64 epochDifferenceMs = 11644473600000ULL;
    const quint64 fileTime =
        (static_cast<quint64>(QDateTime::currentMSecsSinceEpoch())
         + epochDifferenceMs) * 10000ULL;
    return static_cast<DWORD>(fileTime & 0xffffffffULL);
}

WORD readWord(const BYTE* source)
{
    return static_cast<WORD>(source[0])
        | static_cast<WORD>(static_cast<WORD>(source[1]) << 8U);
}

DWORD readDword(const BYTE* source)
{
    return static_cast<DWORD>(source[0])
        | (static_cast<DWORD>(source[1]) << 8U)
        | (static_cast<DWORD>(source[2]) << 16U)
        | (static_cast<DWORD>(source[3]) << 24U);
}

void writeWord(BYTE* destination, WORD value)
{
    destination[0] = static_cast<BYTE>(value & 0xffU);
    destination[1] = static_cast<BYTE>((value >> 8U) & 0xffU);
}

void writeDword(BYTE* destination, DWORD value)
{
    destination[0] = static_cast<BYTE>(value & 0xffU);
    destination[1] = static_cast<BYTE>((value >> 8U) & 0xffU);
    destination[2] = static_cast<BYTE>((value >> 16U) & 0xffU);
    destination[3] = static_cast<BYTE>((value >> 24U) & 0xffU);
}

// Exact algorithm from v2.5-beta-1-modern/utils.cpp. Windows account lookup
// and FILETIME acquisition are replaced by their Qt/Linux equivalents only.
QByteArray encEncodeData(const QByteArray& source)
{
    const UINT decryptedSize = static_cast<UINT>(source.size());
    QByteArray output(static_cast<qsizetype>(2U * decryptedSize
                                             + sizeof(DWORD)), '\0');
    const QByteArray user = currentEncryptionUser().toLocal8Bit();
    const UINT userLength = static_cast<UINT>(qMax<qsizetype>(1, user.size()));
    BYTE* destination = reinterpret_cast<BYTE*>(output.data());
    const BYTE* input = reinterpret_cast<const BYTE*>(source.constData());
    DWORD time = currentFileTimeLow();
    BYTE low = 0;
    WORD previous = 0;
    DWORD checksum = 0;
    for (UINT i = decryptedSize; i > 0; --i) {
        const BYTE userByte = static_cast<BYTE>(
            user.isEmpty() ? 'G' : user.at(i % userLength));
        WORD value = static_cast<WORD>(*(input++)) ^ userByte;
        value = static_cast<WORD>(((value & 0x00ccU) >> 2U)
                                  | ((value & 0x0033U) << 8U));
        value = static_cast<WORD>(value | (LOWORD(time) & 0xccccU));
        value ^= low ? LOWORD(time) : HIWORD(time);
        previous = static_cast<WORD>(previous ^ value);
        writeWord(destination + sizeof(DWORD)
                      + (decryptedSize - i) * sizeof(WORD), previous);
        checksum += previous;
        low ^= 1U;
        time += 3495364871U;
    }
    writeDword(destination, time ^ checksum);
    return output;
}

BOOL encDecodeData(QByteArray* destination, const QByteArray& source,
                   UINT decryptedSize)
{
    if (!destination
        || source.size() < static_cast<qsizetype>(2U * decryptedSize
                                                  + sizeof(DWORD))) {
        return FALSE;
    }
    destination->fill('\0', static_cast<qsizetype>(decryptedSize));
    const BYTE* input = reinterpret_cast<const BYTE*>(source.constData());
    const QString userName = currentEncryptionUser();
    const QByteArray user = userName.toLocal8Bit();
    const UINT userLength = static_cast<UINT>(qMax<qsizetype>(1, user.size()));
    DWORD checksum = 0;
    WORD any = 0;
    for (UINT i = 0; i < decryptedSize; ++i) {
        const WORD encoded = readWord(input + sizeof(DWORD)
                                      + i * sizeof(WORD));
        any = static_cast<WORD>(any | encoded);
        checksum += encoded;
    }
    if (!any) return FALSE;

    DWORD time = readDword(input) ^ checksum;
    for (UINT i = 0; i < decryptedSize; ++i)
        time -= 3495364871U;

    BYTE low = 0;
    WORD previous = 0;
    BYTE* output = reinterpret_cast<BYTE*>(destination->data());
    for (UINT i = decryptedSize; i > 0; --i) {
        const WORD encoded = readWord(input + sizeof(DWORD)
                                      + (decryptedSize - i) * sizeof(WORD));
        WORD value = static_cast<WORD>(encoded ^ previous);
        previous = encoded;
        value ^= low ? LOWORD(time) : HIWORD(time);
        const BYTE userByte = static_cast<BYTE>(
            user.isEmpty() ? 'G' : user.at(i % userLength));
        *(output++) = static_cast<BYTE>(
            (((value & 0x3300U) >> 8U) | ((value & 0x0033U) << 2U))
            ^ userByte);
        low ^= 1U;
        time += 3495364871U;
    }
    return TRUE;
}

QString readCString(const BYTE*& cursor, int& remaining)
{
    const BYTE* start = cursor;
    int length = 0;
    while (length < remaining && start[length] != 0) ++length;
    QString result;
    if (length > 0) {
        result = QString::fromLocal8Bit(
            reinterpret_cast<const char*>(start), length);
    }
    cursor += length;
    remaining -= length;
    if (remaining > 0) {
        ++cursor;
        --remaining;
    }
    return result;
}

bool sameNullable(const QString& left, const QString& right)
{
    if (left.isNull() != right.isNull()) return false;
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

QString normalizedOptional(const QString& value)
{
    return value.isEmpty() ? QString() : value;
}

} // namespace

// =================================================================================
// CChatServer implementation

CChatServer::CChatServer(const QString& name, const QByteArray& data)
    : m_pszName(name)
{
    SetDefaultSettings();
    if (!data.isEmpty()) ReadFromData(data);
}

void CChatServer::SetDefaultSettings()
{
    m_pszUserName = QString();
    m_pszPassword = QString();
    m_pszSecurityPackages = QString();
    m_nPort = 6667;
    m_nAuthenticationType = authtypeNone;
    m_bRememberPassword = FALSE;
}

void CChatServer::FreeSettings()
{
    m_pszUserName = QString();
    m_pszPassword = QString();
    m_pszSecurityPackages = QString();
}

void CChatServer::ReadFromData(const QByteArray& data)
{
    FreeSettings();
    SetDefaultSettings();
    const BYTE* cursor = reinterpret_cast<const BYTE*>(data.constData());
    int remaining = data.size();
    while (remaining > 0) {
        const BYTE type = *(cursor++);
        --remaining;
        switch (type) {
        case datatypePort:
            if (remaining >= 2) {
                m_nPort = readWord(cursor);
                cursor += 2;
                remaining -= 2;
            } else {
                remaining = 0;
            }
            break;
        case datatypeAuthenticationType:
            if (remaining >= 1) {
                m_nAuthenticationType = *cursor;
                ++cursor;
                --remaining;
            }
            break;
        case datatypeUserName:
        case datatypeUserPassword: {
            int encryptionType = 0;
            if (remaining >= 1) {
                encryptionType = *cursor;
                ++cursor;
                --remaining;
            }
            QString value;
            if (encryptionType == 0) {
                value = readCString(cursor, remaining);
            } else if (encryptionType == 1) {
                if (remaining >= 68) {
                    QByteArray decoded;
                    if (encDecodeData(&decoded,
                                      QByteArray(
                                          reinterpret_cast<const char*>(cursor),
                                          68), 32)) {
                        const int end = decoded.indexOf('\0');
                        value = QString::fromLocal8Bit(
                            decoded.constData(), end < 0 ? decoded.size() : end);
                    }
                    cursor += 68;
                    remaining -= 68;
                } else {
                    remaining = 0;
                }
            }
            if (!value.isEmpty()) {
                if (type == datatypeUserName) m_pszUserName = value;
                else m_pszPassword = value;
            }
            break;
        }
        case datatypeRememberPassword:
            if (remaining >= 1) {
                m_bRememberPassword = *cursor;
                ++cursor;
                --remaining;
            }
            // Deliberate source fall-through: the bytes following the remember
            // flag are parsed as a security-package string.
            [[fallthrough]];
        case datatypeSecurityPkg: {
            const QString value = readCString(cursor, remaining);
            if (!value.isEmpty()) m_pszSecurityPackages = value;
            break;
        }
        default:
            break;
        }
    }
}

BOOL CChatServer::WriteToData(QByteArray* dataOut) const
{
    if (!dataOut) return FALSE;
    QByteArray data;
    if (m_nPort != 6667) {
        data.append(static_cast<char>(datatypePort));
        data.append(static_cast<char>(m_nPort & 0xffU));
        data.append(static_cast<char>((m_nPort >> 8U) & 0xffU));
    }
    if (m_nAuthenticationType != authtypeNone) {
        data.append(static_cast<char>(datatypeAuthenticationType));
        data.append(static_cast<char>(m_nAuthenticationType));
    }
    if (m_nAuthenticationType == authtypePlainText) {
        if (!m_pszUserName.isEmpty()) {
            data.append(static_cast<char>(datatypeUserName));
            data.append('\0');
            data.append(m_pszUserName.toLocal8Bit());
            data.append('\0');
        }
        if (m_bRememberPassword && !m_pszPassword.isEmpty()) {
            QByteArray plain(32, '\0');
            const QByteArray password = m_pszPassword.toLocal8Bit();
            const qsizetype copyLength = qMin<qsizetype>(31, password.size());
            std::memcpy(plain.data(), password.constData(),
                        static_cast<std::size_t>(copyLength));
            data.append(static_cast<char>(datatypeUserPassword));
            data.append(static_cast<char>(1));
            data.append(encEncodeData(plain));
        }
        if (m_bRememberPassword) {
            data.append(static_cast<char>(datatypeRememberPassword));
            data.append(static_cast<char>(1));
        }
    } else if (m_nAuthenticationType == authtypeCustomPackages
               && !m_pszSecurityPackages.isEmpty()) {
        data.append(static_cast<char>(datatypeSecurityPkg));
        data.append(m_pszSecurityPackages.toLocal8Bit());
        data.append('\0');
    }
    *dataOut = data;
    return TRUE;
}

BOOL CChatServer::WriteToRegistry()
{
    if (!m_pGroup || !m_pGroup->m_pOwner) return FALSE;
    QByteArray data;
    if (!WriteToData(&data)) return FALSE;
    return m_pGroup->m_pOwner->WriteServerData(
        m_pGroup->m_pszName, m_pszName, data);
}

BOOL CChatServer::ResolveSocketAddress()
{
    if (!m_sockaddr.isNull()) return TRUE;
    QString server;
    int ignoredPort = 0;
    TranslateServerNameToServerAndPort(m_pszName, &server, &ignoredPort);
    QHostAddress direct;
    if (direct.setAddress(server) && direct.protocol() == QAbstractSocket::IPv4Protocol) {
        m_sockaddr = direct;
        m_bResolveFailed = FALSE;
        return TRUE;
    }
    const QHostInfo host = QHostInfo::fromName(server);
    for (const QHostAddress& address : host.addresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            m_sockaddr = address;
            m_bResolveFailed = FALSE;
            return TRUE;
        }
    }
    m_bResolveFailed = TRUE;
    return FALSE;
}

// =================================================================================
// CChatServerGroup implementation

CChatServerGroup::CChatServerGroup(const QString& name,
                                   CChatServiceList* owner)
    : m_pszName(name)
    , m_pOwner(owner)
{
}

BOOL CChatServerGroup::ContainsServer(const QString& server)
{
    if (m_bIsRead) return FindServer(server) != nullptr;
    if (!m_pOwner) return FALSE;
    return m_pOwner->StoredServerNames(m_pszName).contains(
        server, Qt::CaseSensitive);
}

CChatServer* CChatServerGroup::FindServer(const QString& server)
{
    if (!m_bIsRead && !ReadFromRegistry()) return nullptr;
    for (const auto& candidate : m_listServers) {
        if (candidate->m_pszName == server) return candidate.get();
    }
    return nullptr;
}

BOOL CChatServerGroup::ReadFromRegistry()
{
    if (m_bIsRead) return TRUE;
    if (!m_pOwner) return FALSE;
    m_listServers.clear();
    m_pszLastServer = m_pOwner->StoredLastServer(m_pszName);
    const QStringList names = m_pOwner->StoredServerNames(m_pszName);
    for (const QString& name : names) {
        auto server = std::make_unique<CChatServer>(
            name, m_pOwner->ReadServerData(m_pszName, name));
        server->m_pGroup = this;
        // Original RegEnumValue loop inserts each entry at the head.
        m_listServers.insert(m_listServers.begin(), std::move(server));
    }
    m_bIsRead = TRUE;
    return TRUE;
}

CChatServer* CChatServerGroup::CreateServer(const QString& name, int port)
{
    if (!m_bIsRead && !ReadFromRegistry()) return nullptr;
    if (FindServer(name)) return nullptr;
    auto server = std::make_unique<CChatServer>(name);
    server->m_nPort = static_cast<UINT>(port);
    server->m_pGroup = this;
    CChatServer* result = server.get();
    if (!result->WriteToRegistry()) return nullptr;
    m_listServers.push_back(std::move(server));
    return result;
}

BOOL CChatServerGroup::DestroyServer(CChatServer* server)
{
    if (!server || !m_pOwner) return FALSE;
    const auto found = std::find_if(
        m_listServers.begin(), m_listServers.end(),
        [server](const auto& item) { return item.get() == server; });
    if (found == m_listServers.end()) return FALSE;
    if (!m_pOwner->RemoveServerData(m_pszName, server->m_pszName))
        return FALSE;
    m_listServers.erase(found);
    return TRUE;
}

BOOL CChatServerGroup::EnumServers(CChatServer*& server)
{
    if (!m_bIsRead && !ReadFromRegistry()) return FALSE;
    if (m_listServers.empty()) {
        server = nullptr;
        return FALSE;
    }
    if (!server) {
        server = m_listServers.front().get();
        return TRUE;
    }
    const auto found = std::find_if(
        m_listServers.begin(), m_listServers.end(),
        [server](const auto& item) { return item.get() == server; });
    if (found == m_listServers.end() || std::next(found) == m_listServers.end()) {
        server = nullptr;
        return FALSE;
    }
    server = std::next(found)->get();
    return TRUE;
}

void CChatServerGroup::SetLastAccessedServer(CChatServer* server)
{
    if (!server || !m_pOwner) return;
    m_pOwner->WriteLastServer(m_pszName, server->m_pszName);
    m_pszLastServer = server->m_pszName;
}

BOOL CChatServerGroup::IsEmpty()
{
    return (!m_bIsRead && !ReadFromRegistry()) || m_listServers.empty();
}

int CChatServerGroup::GetServerCount()
{
    if (!m_bIsRead) ReadFromRegistry();
    return static_cast<int>(m_listServers.size());
}

// =================================================================================
// CChatService implementation

CChatService::CChatService(const QString& service)
{
    if (service.startsWith(QStringLiteral("//"))) {
        const QString value = service.mid(2);
        const qsizetype slash = value.indexOf(QLatin1Char('/'));
        if (slash >= 0)
            CommonConstruct(value.left(slash), value.mid(slash + 1));
        else
            CommonConstruct(value, QString());
    } else {
        CommonConstruct(QString(), service);
    }
}

CChatService::CChatService(const QString& group, const QString& server)
{
    CommonConstruct(group, server);
}

void CChatService::CommonConstruct(const QString& group,
                                   const QString& server)
{
    m_pszGroup = (!group.isEmpty()
                  && group.compare(g_szGroupUnassociated,
                                   Qt::CaseInsensitive) != 0)
        ? group : QString();
    m_pszServer = !server.isEmpty() ? server : QString();
}

QString CChatService::GetDisplayName() const
{
    return !m_pszServer.isNull() ? m_pszServer : m_pszGroup;
}

void CChatService::FormatAsServiceName(QString& out) const
{
    if (!m_pszGroup.isNull()) {
        out = m_pszServer.isNull()
            ? QStringLiteral("//%1").arg(m_pszGroup)
            : QStringLiteral("//%1/%2").arg(m_pszGroup, m_pszServer);
    } else if (!m_pszServer.isNull()) {
        out = m_pszServer;
    } else {
        out.clear();
    }
}

QString CChatService::FormatAsServiceName() const
{
    QString result;
    FormatAsServiceName(result);
    return result;
}

void TranslateServerNameToServerAndPort(const QString& server,
                                        QString* translatedServer,
                                        int* port)
{
    if (!translatedServer || !port) return;
    const qsizetype colon = server.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        *translatedServer = server.left(colon);
        bool ok = false;
        *port = server.mid(colon + 1).toInt(&ok);
        if (!ok || *port < 6000 || *port > 7000) *port = 6667;
    } else {
        *translatedServer = server;
        *port = 6667;
    }
}

// =================================================================================
// CChatServiceList implementation

CChatServiceList::CChatServiceList(const QString& settingsFile)
    : m_settingsFile(settingsFile)
{
}

CChatServiceList::~CChatServiceList() = default;

std::unique_ptr<QSettings> CChatServiceList::OpenSettings() const
{
    if (!m_settingsFile.isEmpty()) {
        return std::make_unique<QSettings>(m_settingsFile,
                                            QSettings::IniFormat);
    }
    return std::make_unique<QSettings>(QSettings::IniFormat,
                                        QSettings::UserScope,
                                        QStringLiteral("Microsoft"),
                                        QStringLiteral("Microsoft Comic Chat"));
}

QString CChatServiceList::ParentPath() const
{
    return QString::fromLatin1(kParentPath);
}

QString CChatServiceList::ServicesPath() const
{
    return ParentPath() + QLatin1Char('/') + g_szServicesRegName;
}

QString CChatServiceList::ServiceListPath() const
{
    return ServicesPath() + QLatin1Char('/') + g_szServicesList;
}

QString CChatServiceList::GroupPath(const QString& group) const
{
    return ServicesPath() + QLatin1Char('/') + group;
}

void CChatServiceList::EnsureGroupStorage(const QString& group)
{
    auto settings = OpenSettings();
    settings->setValue(GroupPath(group) + QLatin1Char('/')
                           + QString::fromLatin1(kGroupMarker), true);
    settings->sync();
}

QStringList CChatServiceList::StoredServerNames(const QString& group) const
{
    auto settings = OpenSettings();
    settings->beginGroup(GroupPath(group));
    QStringList names = settings->childKeys();
    settings->endGroup();
    names.erase(std::remove_if(names.begin(), names.end(),
        [](const QString& name) { return name.startsWith(QLatin1Char('.')); }),
        names.end());
    return names;
}

QByteArray CChatServiceList::ReadServerData(const QString& group,
                                            const QString& server) const
{
    auto settings = OpenSettings();
    return settings->value(GroupPath(group) + QLatin1Char('/') + server)
        .toByteArray();
}

BOOL CChatServiceList::WriteServerData(const QString& group,
                                       const QString& server,
                                       const QByteArray& data)
{
    auto settings = OpenSettings();
    settings->setValue(GroupPath(group) + QLatin1Char('/') + server, data);
    settings->sync();
    return settings->status() == QSettings::NoError;
}

BOOL CChatServiceList::RemoveServerData(const QString& group,
                                        const QString& server)
{
    auto settings = OpenSettings();
    settings->remove(GroupPath(group) + QLatin1Char('/') + server);
    settings->sync();
    return settings->status() == QSettings::NoError;
}

QString CChatServiceList::StoredLastServer(const QString& group) const
{
    auto settings = OpenSettings();
    return settings->value(GroupPath(group)
                           + QStringLiteral("/.LastServer")).toString();
}

void CChatServiceList::WriteLastServer(const QString& group,
                                       const QString& server)
{
    auto settings = OpenSettings();
    settings->setValue(GroupPath(group)
                           + QStringLiteral("/.LastServer"), server);
    settings->sync();
}

BOOL CChatServiceList::ReadFromRegistry()
{
    m_listServices.clear();
    m_listSrvGroups.clear();
    m_nOldReadCount = 0;
    QString oldSettings;
    auto settings = OpenSettings();

    const QString migratedKey = ParentPath()
        + QStringLiteral("/ServersMigrated");
    if (!settings->contains(migratedKey)) {
        const QString oldKey = ParentPath() + QStringLiteral("/ServerList");
        oldSettings = settings->contains(oldKey)
            ? settings->value(oldKey).toString()
            : originalResourceString(QStringLiteral("IDS_DEFAULT_SERVERLIST"));
    }

    settings->beginGroup(ServiceListPath());
    for (int index = 0;; ++index) {
        const QString key = QString::number(index);
        if (!settings->contains(key)) break;
        const QString value = settings->value(key).toString();
        if (value.isEmpty()) break;
        m_listServices.push_back(std::make_unique<CChatService>(value));
        ++m_nOldReadCount;
    }
    settings->endGroup();

    settings->beginGroup(ServicesPath());
    const QStringList groups = settings->childGroups();
    settings->endGroup();
    for (const QString& group : groups) {
        if (!group.isEmpty() && !group.startsWith(QLatin1Char('.'))) {
            m_listSrvGroups.push_back(
                std::make_unique<CChatServerGroup>(group, this));
        }
    }

    if (!oldSettings.isEmpty()) {
        const QStringList servers = oldSettings.split(
            QLatin1Char(';'), Qt::KeepEmptyParts);
        for (QString server : servers) {
            server = server.trimmed();
            if (!server.isEmpty()) AddOldServer(server);
        }
        WriteIfChanged();
    }

    QString prepopulated = settings->value(
        ParentPath() + QStringLiteral("/PrepopulatedServers"),
        QStringLiteral("servers.cfg")).toString();
    if (!prepopulated.isEmpty()
        && prepopulated.compare(QStringLiteral("none"),
                                Qt::CaseInsensitive) != 0) {
        const QString file = QDir(originalAssetRoot()).filePath(prepopulated);
        if (!ImportFromFile(file)) return FALSE;
        settings->setValue(ParentPath()
                               + QStringLiteral("/PrepopulatedServers"),
                           QStringLiteral("none"));
    }

    settings->sync();
    m_bSvcListModified = FALSE;
    return settings->status() == QSettings::NoError;
}

BOOL CChatServiceList::WriteToRegistry()
{
    auto settings = OpenSettings();
    settings->beginGroup(ServiceListPath());
    int index = 0;
    for (const auto& service : m_listServices) {
        settings->setValue(QString::number(index++),
                           service->FormatAsServiceName());
    }
    for (int old = index; old < m_nOldReadCount; ++old)
        settings->remove(QString::number(old));
    settings->endGroup();
    settings->sync();
    if (settings->status() != QSettings::NoError) return FALSE;
    m_nOldReadCount = index;
    m_bSvcListModified = FALSE;
    return TRUE;
}

BOOL CChatServiceList::WriteIfChanged()
{
    return m_bSvcListModified ? WriteToRegistry() : FALSE;
}

void CChatServiceList::MarkServersMigrated()
{
    auto settings = OpenSettings();
    settings->setValue(ParentPath()
                           + QStringLiteral("/ServersMigrated"), true);
    settings->sync();
}

BOOL CChatServiceList::EnumGroups(CChatServerGroup*& group,
                                  BOOL& unassociatedGroup)
{
    if (m_listSrvGroups.empty()) {
        group = nullptr;
        return FALSE;
    }
    if (!group) {
        group = m_listSrvGroups.front().get();
    } else {
        const auto found = std::find_if(
            m_listSrvGroups.begin(), m_listSrvGroups.end(),
            [group](const auto& item) { return item.get() == group; });
        if (found == m_listSrvGroups.end()
            || std::next(found) == m_listSrvGroups.end()) {
            group = nullptr;
            return FALSE;
        }
        group = std::next(found)->get();
    }
    unassociatedGroup = group->m_pszName.compare(
        g_szGroupUnassociated, Qt::CaseInsensitive) == 0;
    return TRUE;
}

BOOL CChatServiceList::EnumServices(CChatService*& service)
{
    if (m_listServices.empty()) {
        service = nullptr;
        return FALSE;
    }
    if (!service) {
        service = m_listServices.front().get();
        return TRUE;
    }
    const auto found = std::find_if(
        m_listServices.begin(), m_listServices.end(),
        [service](const auto& item) { return item.get() == service; });
    if (found == m_listServices.end()
        || std::next(found) == m_listServices.end()) {
        service = nullptr;
        return FALSE;
    }
    service = std::next(found)->get();
    return TRUE;
}

CChatServerGroup* CChatServiceList::FindGroup(const QString& name) const
{
    for (const auto& group : m_listSrvGroups) {
        if (group->m_pszName.compare(name, Qt::CaseInsensitive) == 0)
            return group.get();
    }
    return nullptr;
}

CChatService* CChatServiceList::FindService(const QString& group,
                                            const QString& server) const
{
    for (const auto& service : m_listServices) {
        if (sameNullable(service->GetGroup(), group)
            && sameNullable(service->GetServer(), server)) {
            return service.get();
        }
    }
    return nullptr;
}

CChatService* CChatServiceList::FindServiceForServer(
    const QString& server) const
{
    for (const auto& service : m_listServices) {
        if (!service->GetServer().isNull()
            && service->GetServer().compare(server,
                                            Qt::CaseInsensitive) == 0) {
            return service.get();
        }
    }
    return nullptr;
}

CChatServerGroup* CChatServiceList::CreateGroup(const QString& name)
{
    if (CChatServerGroup* existing = FindGroup(name)) return existing;
    auto group = std::make_unique<CChatServerGroup>(name, this);
    CChatServerGroup* result = group.get();
    EnsureGroupStorage(name);
    m_listSrvGroups.push_back(std::move(group));
    if (name.compare(g_szGroupUnassociated,
                     Qt::CaseInsensitive) != 0) {
        m_listServices.push_back(
            std::make_unique<CChatService>(name, QString()));
        m_bSvcListModified = TRUE;
    }
    return result;
}

BOOL CChatServiceList::DestroyGroup(CChatServerGroup* group)
{
    if (!group) return FALSE;
    const auto found = std::find_if(
        m_listSrvGroups.begin(), m_listSrvGroups.end(),
        [group](const auto& item) { return item.get() == group; });
    if (found == m_listSrvGroups.end()) return FALSE;
    auto settings = OpenSettings();
    settings->remove(GroupPath(group->m_pszName));
    settings->sync();
    if (settings->status() != QSettings::NoError) return FALSE;
    m_listSrvGroups.erase(found);
    return TRUE;
}

CChatService* CChatServiceList::CreateService(const QString& group,
                                              const QString& server)
{
    auto service = std::make_unique<CChatService>(group, server);
    CChatService* result = service.get();
    m_listServices.insert(m_listServices.begin(), std::move(service));
    m_bSvcListModified = TRUE;
    return result;
}

void CChatServiceList::DestroyService(CChatService* service)
{
    const auto found = std::find_if(
        m_listServices.begin(), m_listServices.end(),
        [service](const auto& item) { return item.get() == service; });
    if (found == m_listServices.end()) return;
    m_listServices.erase(found);
    m_bSvcListModified = TRUE;
}

void CChatServiceList::MoveServiceToTop(CChatService* service)
{
    const auto found = std::find_if(
        m_listServices.begin(), m_listServices.end(),
        [service](const auto& item) { return item.get() == service; });
    if (found == m_listServices.end() || found == m_listServices.begin())
        return;
    std::unique_ptr<CChatService> moved = std::move(*found);
    m_listServices.erase(found);
    m_listServices.insert(m_listServices.begin(), std::move(moved));
    m_bSvcListModified = TRUE;
}

void CChatServiceList::RemoveReferences(const QString& group,
                                        const QString& server)
{
    const QString normalizedGroup =
        group.compare(g_szGroupUnassociated, Qt::CaseInsensitive) == 0
        ? QString() : group;
    const bool allServers = server.isNull();
    const auto oldSize = m_listServices.size();
    m_listServices.erase(std::remove_if(
        m_listServices.begin(), m_listServices.end(),
        [&](const auto& service) {
            return sameNullable(service->GetGroup(), normalizedGroup)
                && (allServers
                    || service->GetServer().compare(
                           server, Qt::CaseInsensitive) == 0);
        }), m_listServices.end());
    if (oldSize != m_listServices.size()) m_bSvcListModified = TRUE;
}

BOOL CChatServiceList::GetGroupForOldServer(const QString& server,
                                            QString& groupOut)
{
    const qsizetype colon = server.indexOf(QLatin1Char(':'));
    const QString host = colon >= 0 ? server.left(colon) : server;
    for (const auto& group : m_listSrvGroups) {
        if (group->ContainsServer(server)) {
            groupOut = group->m_pszName;
            return TRUE;
        }
        if (colon >= 0 && group->ContainsServer(host)) {
            groupOut = group->m_pszName;
            return FALSE;
        }
    }

    const QString lower = host.toLower();
    if (lower.endsWith(QStringLiteral("microsoft.com"))) {
        groupOut = originalResourceString(
            QStringLiteral("IDS_PREDEFGROUP_MS"));
    } else if (lower.startsWith(QStringLiteral("chat"))
               && lower.endsWith(QStringLiteral("msn.com"))) {
        groupOut = originalResourceString(
            QStringLiteral("IDS_PREDEFGROUP_MSN"));
    } else if (lower.endsWith(QStringLiteral("msn.com"))) {
        groupOut = originalResourceString(
            QStringLiteral("IDS_PREDEFGROUP_MS"));
    } else {
        groupOut = g_szGroupUnassociated;
    }
    return FALSE;
}

BOOL CChatServiceList::AddOldServer(const QString& server)
{
    if (server.isEmpty()) return FALSE;
    QString translated;
    int port = 6667;
    TranslateServerNameToServerAndPort(server, &translated, &port);
    Q_UNUSED(translated);

    if (FindServiceForServer(server)) return FALSE;

    QString groupName;
    const BOOL alreadyInGroup = GetGroupForOldServer(server, groupName);
    if (!alreadyInGroup) {
        CChatServerGroup* group = CreateGroup(groupName);
        if (group && !group->FindServer(server))
            group->CreateServer(server, port);
    }
    if (groupName == g_szGroupUnassociated) {
        m_listServices.push_back(
            std::make_unique<CChatService>(groupName, server));
        m_bSvcListModified = TRUE;
    }
    // The original function returns FALSE even after adding.
    return FALSE;
}

void CChatServiceList::GetServiceNameFromDisplayName(
    const QString& displayName, QString& service)
{
    if (displayName.startsWith(QLatin1Char('/'))) {
        service = displayName;
        return;
    }
    for (const auto& candidate : m_listServices) {
        if (candidate->GetDisplayName().compare(
                displayName, Qt::CaseInsensitive) == 0) {
            candidate->FormatAsServiceName(service);
            return;
        }
    }
    if (FindGroup(displayName)) {
        service = QStringLiteral("//%1").arg(displayName);
        return;
    }
    QString group;
    GetGroupForOldServer(displayName, group);
    service = group == g_szGroupUnassociated
        ? displayName
        : QStringLiteral("//%1/%2").arg(group, displayName);
}

BOOL CChatServiceList::ImportFromFile(const QString& fileName)
{
    if (!QFileInfo::exists(fileName)) return TRUE;
    QSettings source(fileName, QSettings::IniFormat);
    const QStringList groups = source.childGroups();
    for (const QString& groupName : groups) {
        if (groupName.compare(g_szGroupUnassociated,
                              Qt::CaseInsensitive) == 0
            || groupName.startsWith(QLatin1Char('.'))
            || groupName.startsWith(QLatin1Char('{'))
            || groupName.contains(QLatin1Char('/'))
            || groupName.contains(QLatin1Char('\\'))
            || groupName.size() > 100) {
            continue;
        }
        CChatServerGroup* group = FindGroup(groupName);
        if (!group) group = CreateGroup(groupName);
        if (!group) return FALSE;
        source.beginGroup(groupName);
        const QStringList servers = source.childKeys();
        source.endGroup();
        for (const QString& serverName : servers) {
            if (serverName.startsWith(QLatin1Char('.'))
                || serverName.contains(QLatin1Char('/'))
                || serverName.contains(QLatin1Char('\\'))) {
                continue;
            }
            if (!group->FindServer(serverName)) {
                QString translated;
                int port = 6667;
                TranslateServerNameToServerAndPort(
                    serverName, &translated, &port);
                group->CreateServer(translated, port);
            }
        }
    }
    return source.status() == QSettings::NoError;
}

// =================================================================================
// CChatServiceUI implementation

struct CChatServiceUI::GroupState {
    QString name;
    CChatServerGroup* original = nullptr;
    BOOL added = FALSE;
    BOOL removed = FALSE;
};

struct CChatServiceUI::ServerState {
    QString name;
    CChatServer* original = nullptr;
    GroupState* group = nullptr;
    ServerProps props;
    BOOL added = FALSE;
    BOOL removed = FALSE;
    BOOL changed = FALSE;
};

CChatServiceUI::CChatServiceUI() = default;

CChatServiceUI::~CChatServiceUI()
{
    Reset(TRUE);
}

const CChatServiceUI::ServerProps&
CChatServiceUI::ServerProps::operator=(const ServerProps& data)
{
    if (this != &data) {
        m_nPort = data.m_nPort;
        m_nAuthenticationType = data.m_nAuthenticationType;
        m_strUserName = data.m_strUserName;
        m_strPassword = data.m_strPassword;
        m_strSecurityPackages = data.m_strSecurityPackages;
        m_bRememberPassword = data.m_bRememberPassword;
        // Original operator deliberately does not copy m_pGroupIn.
    }
    return *this;
}

const CChatServiceUI::ServerProps&
CChatServiceUI::ServerProps::operator=(const CChatServer& server)
{
    m_nPort = server.m_nPort;
    m_nAuthenticationType = server.m_nAuthenticationType;
    m_strUserName = server.m_pszUserName;
    m_strPassword = server.m_pszPassword;
    m_strSecurityPackages = server.m_pszSecurityPackages;
    m_bRememberPassword = server.m_bRememberPassword;
    return *this;
}

void CChatServiceUI::SetServiceList(CChatServiceList* list)
{
    if (!list || m_pSvcList) return;
    m_pSvcList = list;
    BuildState();
}

void CChatServiceUI::BuildState()
{
    m_groups.clear();
    m_servers.clear();
    if (!m_pSvcList) return;
    for (const auto& originalGroup : m_pSvcList->m_listSrvGroups) {
        auto group = std::make_unique<GroupState>();
        group->name = originalGroup->m_pszName;
        group->original = originalGroup.get();
        GroupState* groupHandle = group.get();
        m_groups.push_back(std::move(group));

        CChatServer* originalServer = nullptr;
        while (originalGroup->EnumServers(originalServer)) {
            auto server = std::make_unique<ServerState>();
            server->name = originalServer->m_pszName;
            server->original = originalServer;
            server->group = groupHandle;
            server->props = *originalServer;
            server->props.m_pGroupIn = originalGroup.get();
            m_servers.push_back(std::move(server));
        }
    }
    m_bChangesMade = FALSE;
}

void CChatServiceUI::Reset(BOOL onDestruction)
{
    m_groups.clear();
    m_servers.clear();
    m_bChangesMade = FALSE;
    if (!onDestruction && m_pSvcList) BuildState();
}

void CChatServiceUI::Revert()
{
    Reset(FALSE);
}

CChatServiceUI::GroupState* CChatServiceUI::GroupFromHandle(
    HCHATSRVGROUP group) const
{
    for (const auto& state : m_groups)
        if (state.get() == group) return state.get();
    return nullptr;
}

CChatServiceUI::ServerState* CChatServiceUI::ServerFromHandle(
    HCHATSERVER server) const
{
    for (const auto& state : m_servers)
        if (state.get() == server) return state.get();
    return nullptr;
}

QList<CChatServiceUI::GroupState*> CChatServiceUI::CurrentGroups() const
{
    QList<GroupState*> result;
    for (const auto& group : m_groups)
        if (!group->removed) result.append(group.get());
    return result;
}

QList<CChatServiceUI::ServerState*> CChatServiceUI::CurrentServers(
    GroupState* group) const
{
    QList<ServerState*> result;
    for (const auto& server : m_servers)
        if (server->group == group && !server->removed)
            result.append(server.get());
    return result;
}

HCHATSRVGROUP CChatServiceUI::EnumGroups(HCHATSRVGROUP& position,
                                         BOOL& unassociatedGroup)
{
    const QList<GroupState*> groups = CurrentGroups();
    int index = 0;
    if (position) {
        index = groups.indexOf(static_cast<GroupState*>(position));
        if (index < 0) {
            position = nullptr;
            return nullptr;
        }
        ++index;
    }
    if (index >= groups.size()) {
        position = nullptr;
        return nullptr;
    }
    GroupState* group = groups.at(index);
    position = group;
    unassociatedGroup = group->name.compare(
        g_szGroupUnassociated, Qt::CaseInsensitive) == 0;
    return group;
}

HCHATSERVER CChatServiceUI::EnumServersInGroup(HCHATSRVGROUP groupHandle,
                                               HCHATSERVER& position)
{
    GroupState* group = GroupFromHandle(groupHandle);
    if (!group) {
        position = nullptr;
        return nullptr;
    }
    const QList<ServerState*> servers = CurrentServers(group);
    int index = 0;
    if (position) {
        index = servers.indexOf(static_cast<ServerState*>(position));
        if (index < 0) {
            position = nullptr;
            return nullptr;
        }
        ++index;
    }
    if (index >= servers.size()) {
        position = nullptr;
        return nullptr;
    }
    ServerState* server = servers.at(index);
    position = server;
    return server;
}

QString CChatServiceUI::GetGroupName(HCHATSRVGROUP group) const
{
    GroupState* state = GroupFromHandle(group);
    return state ? state->name : QString();
}

QString CChatServiceUI::GetServerName(HCHATSERVER server) const
{
    ServerState* state = ServerFromHandle(server);
    return state ? state->name : QString();
}

void CChatServiceUI::GetServerProps(HCHATSERVER server,
                                    ServerProps& data) const
{
    ServerState* state = ServerFromHandle(server);
    if (!state) return;
    data = state->props;
    data.m_pGroupIn = state->props.m_pGroupIn;
}

BOOL CChatServiceUI::IsGroupEmpty(HCHATSRVGROUP group)
{
    GroupState* state = GroupFromHandle(group);
    return !state || CurrentServers(state).isEmpty();
}

BOOL CChatServiceUI::SetServerProps(HCHATSRVGROUP groupHandle,
                                    HCHATSERVER serverHandle,
                                    const ServerProps& data)
{
    GroupState* group = GroupFromHandle(groupHandle);
    ServerState* server = ServerFromHandle(serverHandle);
    if (!group || !server || server->removed || group->removed) return FALSE;
    server->props = data;
    server->props.m_pGroupIn = group->original;
    server->changed = TRUE;
    m_bChangesMade = TRUE;
    return TRUE;
}

HCHATSERVER CChatServiceUI::AddServer(HCHATSRVGROUP groupHandle,
                                      const QString& serverName, int port)
{
    GroupState* group = GroupFromHandle(groupHandle);
    if (!group || group->removed) return nullptr;
    auto server = std::make_unique<ServerState>();
    server->name = serverName;
    server->group = group;
    server->added = TRUE;
    server->changed = TRUE;
    server->props.m_nPort = static_cast<UINT>(port);
    server->props.m_nAuthenticationType = CChatServer::authtypeNone;
    server->props.m_bRememberPassword = FALSE;
    server->props.m_pGroupIn = group->original;
    ServerState* result = server.get();
    m_servers.push_back(std::move(server));
    m_bChangesMade = TRUE;
    return result;
}

BOOL CChatServiceUI::RemoveServer(HCHATSRVGROUP groupHandle,
                                  HCHATSERVER serverHandle)
{
    GroupState* group = GroupFromHandle(groupHandle);
    ServerState* server = ServerFromHandle(serverHandle);
    if (!group || !server || server->group != group || server->removed)
        return FALSE;
    server->removed = TRUE;
    m_bChangesMade = TRUE;
    return TRUE;
}

HCHATSRVGROUP CChatServiceUI::AddGroup(const QString& groupName)
{
    for (GroupState* group : CurrentGroups()) {
        if (group->name.compare(groupName, Qt::CaseInsensitive) == 0)
            return group;
    }
    auto group = std::make_unique<GroupState>();
    group->name = groupName;
    group->added = TRUE;
    GroupState* result = group.get();
    m_groups.push_back(std::move(group));
    m_bChangesMade = TRUE;
    return result;
}

BOOL CChatServiceUI::RemoveGroup(HCHATSRVGROUP groupHandle)
{
    GroupState* group = GroupFromHandle(groupHandle);
    if (!group || group->removed) return FALSE;
    group->removed = TRUE;
    for (const auto& server : m_servers) {
        if (server->group == group) server->removed = TRUE;
    }
    m_bChangesMade = TRUE;
    return TRUE;
}

BOOL CChatServiceUI::Apply()
{
    if (!m_bChangesMade) return TRUE;
    if (!m_pSvcList) return FALSE;

    // Destroy removed groups first.
    for (const auto& state : m_groups) {
        if (!state->removed || !state->original) continue;
        m_pSvcList->RemoveReferences(state->original->m_pszName, QString());
        if (!m_pSvcList->DestroyGroup(state->original)) return FALSE;
        state->original = nullptr;
    }

    // Create added groups that were not subsequently removed.
    for (const auto& state : m_groups) {
        if (!state->added || state->removed) continue;
        state->original = m_pSvcList->CreateGroup(state->name);
        if (!state->original) return FALSE;
    }

    // Destroy removed existing servers in surviving groups.
    for (const auto& state : m_servers) {
        if (!state->removed || !state->original || !state->group
            || state->group->removed) continue;
        m_pSvcList->RemoveReferences(state->group->name,
                                     state->original->m_pszName);
        if (!state->group->original
            || !state->group->original->DestroyServer(state->original)) {
            return FALSE;
        }
        state->original = nullptr;
    }

    // Create added servers.
    for (const auto& state : m_servers) {
        if (!state->added || state->removed || !state->group
            || state->group->removed || !state->group->original) continue;
        state->original = state->group->original->CreateServer(
            state->name, 6667);
        if (!state->original) {
            state->original = state->group->original->FindServer(state->name);
            if (!state->original) return FALSE;
        }
        if (state->group->name.compare(
                g_szGroupUnassociated, Qt::CaseSensitive) == 0) {
            m_pSvcList->CreateService(QString(), state->name);
        }
    }

    // Save all changed properties after every new server has a real handle.
    for (const auto& state : m_servers) {
        if (!state->changed || state->removed || !state->original
            || !state->group || state->group->removed) continue;
        CChatServer* server = state->original;
        server->FreeSettings();
        server->m_nPort = state->props.m_nPort;
        server->m_nAuthenticationType = state->props.m_nAuthenticationType;
        server->m_pszUserName = normalizedOptional(
            state->props.m_strUserName);
        server->m_pszPassword = normalizedOptional(
            state->props.m_strPassword);
        server->m_pszSecurityPackages = normalizedOptional(
            state->props.m_strSecurityPackages);
        server->m_bRememberPassword = state->props.m_bRememberPassword;
        if (!server->WriteToRegistry()) return FALSE;
    }

    m_pSvcList->WriteIfChanged();
    BuildState();
    return TRUE;
}

// =================================================================================
// CChatServiceConnector implementation

class CChatServiceConnector::Socket {
public:
    Socket(CChatServiceConnector* parent, int id)
        : m_pParent(parent)
        , m_nID(id)
        , m_socket(new QTcpSocket)
    {
        QObject::connect(m_socket, &QTcpSocket::connected, m_socket,
                         [this] {
            if (!m_bConnecting || !m_pParent) return;
            m_bConnecting = FALSE;
            m_pParent->SocketConnected(this);
        });
        QObject::connect(
            m_socket, &QTcpSocket::errorOccurred, m_socket,
            [this](QAbstractSocket::SocketError) {
                if (!m_bConnecting || !m_pParent) return;
                m_bConnecting = FALSE;
                m_pParent->SocketFailed(this);
            });
    }

    ~Socket()
    {
        if (!m_socket) return;
        QObject::disconnect(m_socket, nullptr, nullptr, nullptr);
        m_socket->abort();
        delete m_socket;
    }

    BOOL Connect(int serverIndex)
    {
        if (!m_pParent || serverIndex < 0
            || serverIndex >= m_pParent->m_connections.size()) return FALSE;
        CChatServer* server =
            m_pParent->m_connections.at(serverIndex).pServer;
        if (!server || server->m_sockaddr.isNull()) return FALSE;
        m_nServer = serverIndex;
        m_bConnecting = TRUE;
        m_socket->connectToHost(server->m_sockaddr,
                                static_cast<quint16>(server->m_nPort));
        return TRUE;
    }

    QTcpSocket* TakeSocket()
    {
        QTcpSocket* result = m_socket;
        if (result) QObject::disconnect(result, nullptr, nullptr, nullptr);
        m_socket = nullptr;
        return result;
    }

    CChatServiceConnector* m_pParent = nullptr;
    int m_nID = -1;
    int m_nServer = -1;
    BOOL m_bConnecting = FALSE;
    QTcpSocket* m_socket = nullptr;
};

CChatServiceConnector::CChatServiceConnector() = default;

CChatServiceConnector::~CChatServiceConnector()
{
    Cleanup();
}

QVector<BYTE> CChatServiceConnector::ConnectionStatuses() const
{
    QVector<BYTE> result;
    result.reserve(m_connections.size());
    for (const SrvConnection& connection : m_connections)
        result.append(connection.byStatus);
    return result;
}

void CChatServiceConnector::ResolveConnection(int connectionIndex,
                                              quint64 generation)
{
    if (connectionIndex < 0 || connectionIndex >= m_connections.size())
        return;
    CChatServer* server = m_connections[connectionIndex].pServer;
    if (!server) {
        m_connections[connectionIndex].byStatus = srvconnFailed;
        return;
    }

    QString host;
    int ignoredPort = 0;
    TranslateServerNameToServerAndPort(server->m_pszName, &host,
                                       &ignoredPort);
    QHostAddress direct;
    if (direct.setAddress(host)
        && direct.protocol() == QAbstractSocket::IPv4Protocol) {
        server->m_sockaddr = direct;
        server->m_bResolveFailed = FALSE;
        m_connections[connectionIndex].byStatus = srvconnNotAttempted;
        return;
    }

    QHostInfo::lookupHost(host, m_context,
        [this, connectionIndex, generation, server](const QHostInfo& info) {
            if (generation != m_generation
                || connectionIndex < 0
                || connectionIndex >= m_connections.size()
                || m_connections[connectionIndex].pServer != server) {
                return;
            }
            for (const QHostAddress& address : info.addresses()) {
                if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                    server->m_sockaddr = address;
                    server->m_bResolveFailed = FALSE;
                    m_connections[connectionIndex].byStatus =
                        srvconnNotAttempted;
                    return;
                }
            }
            server->m_sockaddr.clear();
            server->m_bResolveFailed = TRUE;
            m_connections[connectionIndex].byStatus = srvconnFailed;
        });
}

BOOL CChatServiceConnector::BeginConnectToService(const QString& service)
{
    if (!m_pSvcList || !m_sockets.empty()) return FALSE;

    if (service.isNull()) {
        bool anyRemaining = false;
        for (const SrvConnection& connection : m_connections) {
            if (connection.byStatus == srvconnNotAttempted
                || connection.byStatus == srvconnUnresolved) {
                anyRemaining = true;
                break;
            }
        }
        if (!anyRemaining) return FALSE;
    } else {
        Cleanup();
        m_strSvc = service;
        CChatService parsed(service);
        CChatServerGroup* group = nullptr;

        if (!parsed.GetServer().isNull()) {
            const QString groupName = !parsed.GetGroup().isNull()
                ? parsed.GetGroup() : g_szGroupUnassociated;
            group = m_pSvcList->FindGroup(groupName);
            if (!group) group = m_pSvcList->CreateGroup(groupName);
            if (!group) return FALSE;
            m_pConnectingGroup = group;
            CChatServer* server = group->FindServer(parsed.GetServer());
            if (!server) {
                QString translated;
                int port = 6667;
                TranslateServerNameToServerAndPort(
                    parsed.GetServer(), &translated, &port);
                Q_UNUSED(translated);
                server = group->CreateServer(parsed.GetServer(), port);
            }
            if (!server) return FALSE;
            m_connections.resize(1);
            m_connections[0].pServer = server;
            m_connections[0].byStatus = srvconnUnresolved;
        } else {
            group = m_pSvcList->FindGroup(parsed.GetGroup());
            m_pConnectingGroup = group;
            if (!group) return FALSE;
            const int count = group->GetServerCount();
            if (count == 0) return FALSE;
            m_connections.resize(count);
            std::srand(static_cast<unsigned int>(
                QDateTime::currentMSecsSinceEpoch()));
            CChatServer* server = nullptr;
            while (group->EnumServers(server)) {
                int slot = 0;
                if (count != 1) {
                    const int originalSlot = std::rand() % (count - 1) + 1;
                    slot = originalSlot;
                    while (m_connections[slot].pServer) {
                        ++slot;
                        if (slot == count) slot = 1;
                        if (slot == originalSlot) {
                            slot = 0;
                            break;
                        }
                    }
                }
                m_connections[slot].pServer = server;
                m_connections[slot].byStatus = srvconnUnresolved;
            }
        }

        ++m_generation;
        m_context = new QObject;
        for (int index = 0; index < m_connections.size(); ++index) {
            CChatServer* server = m_connections[index].pServer;
            if (server && !server->m_sockaddr.isNull()) {
                m_connections[index].byStatus = srvconnNotAttempted;
            } else {
                if (server && server->m_bResolveFailed)
                    server->m_bResolveFailed = FALSE;
                ResolveConnection(index, m_generation);
            }
        }
    }

    const int socketCount = qMin(5, m_connections.size());
    m_sockets.reserve(socketCount);
    for (int index = 0; index < socketCount; ++index)
        m_sockets.push_back(std::make_unique<Socket>(this, index));
    m_nActiveSockets = 0;
    return !m_sockets.empty();
}

int CChatServiceConnector::AssignSocket(int socketIndex)
{
    if (socketIndex < 0
        || socketIndex >= static_cast<int>(m_sockets.size())
        || m_sockets[socketIndex]->m_bConnecting) return 1;
    if (!AssignSocketToNewServer(socketIndex)) {
        Cleanup();
        return 0;
    }
    return m_sockets[socketIndex]->m_bConnecting ? 1 : -1;
}

BOOL CChatServiceConnector::AssignSocketToNewServer(int socketIndex)
{
    if (m_connections.isEmpty()) return FALSE;
    int current = -1;
    BOOL anyUnresolved = FALSE;
    for (int index = 0; index < m_connections.size(); ++index) {
        if (m_connections[index].byStatus == srvconnNotAttempted) {
            current = index;
            break;
        }
        if (m_connections[index].byStatus == srvconnUnresolved)
            anyUnresolved = TRUE;
    }
    if (current < 0)
        return m_nActiveSockets != 0 || anyUnresolved;
    if (socketIndex < 0
        || socketIndex >= static_cast<int>(m_sockets.size())) return FALSE;
    if (!m_sockets[socketIndex]->Connect(current)) {
        m_connections[current].byStatus = srvconnFailed;
        return AssignSocketToNewServer(socketIndex);
    }
    m_connections[current].byStatus = srvconnAttempting;
    ++m_nActiveSockets;
    return TRUE;
}

void CChatServiceConnector::SocketFailed(Socket* socket)
{
    if (!socket || socket->m_nServer < 0
        || socket->m_nServer >= m_connections.size()) return;
    if (m_nActiveSockets > 0) --m_nActiveSockets;
    m_connections[socket->m_nServer].byStatus = srvconnFailed;
}

void CChatServiceConnector::SocketConnected(Socket* socket)
{
    if (!socket || socket->m_nServer < 0
        || socket->m_nServer >= m_connections.size()) return;
    if (m_nActiveSockets > 0) --m_nActiveSockets;
    SrvConnection& connection = m_connections[socket->m_nServer];
    connection.byStatus = srvconnConnected;
    m_pConnectingServer = connection.pServer;
    if (!m_pConnectingServer) return;

    theApp.OnConnectConnected();
    serverConn.SetAuthentication(
        m_pConnectingServer->m_nAuthenticationType,
        m_pConnectingServer->m_pszUserName,
        m_pConnectingServer->m_pszPassword,
        m_pConnectingServer->m_pszSecurityPackages);
    QTcpSocket* connectedSocket = socket->TakeSocket();
    serverConn.AdoptSocket(connectedSocket);
    serverConn.OnConnect();
    Cleanup(FALSE);
}

void CChatServiceConnector::Cleanup(BOOL cleanupServerList)
{
    if (cleanupServerList) {
        m_sockets.clear();
        m_deleteableSockets.clear();
    } else {
        for (auto& socket : m_sockets)
            m_deleteableSockets.push_back(std::move(socket));
        m_sockets.clear();
        if (m_context) {
            QTimer::singleShot(0, m_context, [this] {
                m_deleteableSockets.clear();
            });
        }
    }
    m_nActiveSockets = 0;
    if (cleanupServerList) {
        ++m_generation;
        delete m_context;
        m_context = nullptr;
        m_connections.clear();
    }
}

// =================================================================================
// CChatServiceComboBox implementation

CChatServiceComboBox::CChatServiceComboBox(QWidget* parent)
    : QComboBox(parent)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
}

void CChatServiceComboBox::Fill(BOOL nonEmptyGroupsOnly)
{
    if (!m_pSvcList) return;
    const int baseCount = count();
    QList<CChatService*> groups;
    QList<CChatService*> servers;
    CChatService* service = nullptr;
    while (m_pSvcList->EnumServices(service)) {
        if (service->GetServer().isNull()) {
            if (nonEmptyGroupsOnly) {
                CChatServerGroup* group = m_pSvcList->FindGroup(
                    service->GetGroup());
                if (!group || group->IsEmpty()) continue;
            }
            groups.append(service);
        } else {
            servers.append(service);
        }
    }

    const QIcon serverIcon(originalFileResourcePath(
        QStringLiteral("IDI_CONNECT_SRV"), QStringLiteral("ICON")));
    const QIcon networkIcon(originalFileResourcePath(
        QStringLiteral("IDI_CONNECT_NET"), QStringLiteral("ICON")));
    int insertion = baseCount;
    for (CChatService* group : groups) {
        insertItem(insertion++, networkIcon, group->GetDisplayName(),
                   QVariant::fromValue<qulonglong>(
                       reinterpret_cast<quintptr>(group)));
        setItemData(insertion - 1, flagGroupEntry, Qt::UserRole + 1);
    }
    for (CChatService* server : servers) {
        addItem(serverIcon, server->GetDisplayName(),
                QVariant::fromValue<qulonglong>(
                    reinterpret_cast<quintptr>(server)));
        setItemData(count() - 1, flagServerEntry, Qt::UserRole + 1);
    }
}

CChatService* CChatServiceComboBox::GetServiceAt(int index) const
{
    if (index < 0 || index >= count()) return nullptr;
    const quintptr pointer = static_cast<quintptr>(
        itemData(index).toULongLong());
    return reinterpret_cast<CChatService*>(pointer);
}

// =================================================================================
// CChatPasswordDialog implementation

namespace {

class PasswordDluMapper {
public:
    explicit PasswordDluMapper(const QFont& font)
    {
        const QFontMetrics metrics(font);
        const QString alphabet = QStringLiteral(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        m_baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
        m_baseY = qMax(1, metrics.height());
    }
    int x(int value) const { return (value * m_baseX + 2) / 4; }
    int y(int value) const { return (value * m_baseY + 4) / 8; }
    QRect rect(const OriginalDialogControl& control) const
    {
        return QRect(x(control.x), y(control.y),
                     x(control.width), y(control.height));
    }
private:
    int m_baseX = 1;
    int m_baseY = 1;
};

const OriginalDialogControl* passwordControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

void placePasswordControl(QWidget* widget,
                          const OriginalDialogResource& dialog,
                          const PasswordDluMapper& mapper,
                          const QString& identifier, int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = passwordControl(
            dialog, identifier, occurrence)) {
        widget->setGeometry(mapper.rect(*control));
    }
}

} // namespace

CChatPasswordDialog::CChatPasswordDialog(const QString& serverName,
                                         const QString& userName,
                                         BOOL rememberPassword,
                                         QWidget* parent)
    : QDialog(parent)
    , m_bRememberPassword(rememberPassword)
{
    const QString id = QStringLiteral("IDD_PASSWORD");
    const OriginalDialogResource dialog = originalDialogResource(id);
    QFont dialogFont(dialog.fontFamily);
    if (dialog.fontPointSize > 0)
        dialogFont.setPointSize(dialog.fontPointSize);
    setFont(dialogFont);
    setObjectName(id);
    setWindowTitle(dialog.caption);
    const PasswordDluMapper mapper(dialogFont);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    for (int occurrence = 0; occurrence < 4; ++occurrence) {
        const OriginalDialogControl* control = passwordControl(
            dialog, QStringLiteral("IDC_STATIC"), occurrence);
        if (!control) break;
        auto* label = new QLabel(control->text, this);
        label->setGeometry(mapper.rect(*control));
    }
    auto* server = new QLabel(serverName, this);
    placePasswordControl(server, dialog, mapper,
                         QStringLiteral("IDC_SERVERNAME"));
    auto* user = new QLineEdit(userName, this);
    user->setReadOnly(true);
    placePasswordControl(user, dialog, mapper,
                         QStringLiteral("IDC_USERNAME"));
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    placePasswordControl(m_password, dialog, mapper,
                         QStringLiteral("IDC_PASSWORD"));
    m_remember = new QCheckBox(originalDialogControlText(
        id, QStringLiteral("IDC_REMEMBER_PASSWORD")), this);
    m_remember->setChecked(rememberPassword);
    placePasswordControl(m_remember, dialog, mapper,
                         QStringLiteral("IDC_REMEMBER_PASSWORD"));
    auto* ok = new QPushButton(originalDialogControlText(
        id, QStringLiteral("IDOK")), this);
    ok->setDefault(true);
    placePasswordControl(ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(originalDialogControlText(
        id, QStringLiteral("IDCANCEL")), this);
    placePasswordControl(cancel, dialog, mapper,
                         QStringLiteral("IDCANCEL"));
    connect(ok, &QPushButton::clicked, this,
            &CChatPasswordDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_password->setFocus();
}

void CChatPasswordDialog::accept()
{
    m_strPassword = m_password->text();
    m_bRememberPassword = m_remember->isChecked();
    QDialog::accept();
}
