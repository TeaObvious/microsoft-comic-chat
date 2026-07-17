#include "notif.h"
#include "originalassets.h"
#include "originalsettings.h"
#include "rules.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}

#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

QString resourceRuleSetName(const QString& resourceID)
{
    const QString stored = originalResourceString(resourceID);
    const qsizetype separator = stored.indexOf(QLatin1Char('|'));
    REQUIRE(separator >= 0);
    return stored.mid(separator + 1);
}

QByteArray serializedRule(CCRule* rule)
{
    REQUIRE(rule != nullptr);
    std::array<char, g_uMaxSerializedRule> buffer{};
    const INT length = rule->Serialize(buffer.data(), buffer.size());
    REQUIRE(length > 0);
    return QByteArray(buffer.data(), length);
}

CCNotif* originalResourceNotification()
{
    const QString nickname = originalResourceString(
        QStringLiteral("IDS_DEFAULT_NICK"));
    const QString host = originalResourceString(
        QStringLiteral("IDS_DEFAULT_SERVER"));
    const QString any = originalResourceString(
        IDS_KEY_EVENT_PARAM0 + kepAny);
    REQUIRE(!nickname.isEmpty() && !host.isEmpty() && !any.isEmpty());

    auto* notification = new CCNotif;
    notification->SetOperator(g_uNickname, g_uEquals);
    notification->SetOperator(g_uUserName, g_uAny);
    notification->SetOperator(g_uHostName, g_uEquals);
    notification->SetParam(g_uNickname, nickname);
    notification->SetParam(g_uUserName, QString());
    notification->SetParam(g_uHostName, host);
    notification->SetParam(g_uNetName, any);
    notification->Activate();
    return notification;
}

QByteArray serializedNotification(CCNotif* notification)
{
    REQUIRE(notification != nullptr);
    std::array<char, g_uMaxSerializedNotif> buffer{};
    const INT length = notification->Serialize(buffer.data(), buffer.size());
    REQUIRE(length > 0);
    return QByteArray(buffer.data(), length);
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir settingsDirectory;
    REQUIRE(settingsDirectory.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());

    const QString root = QStringLiteral(
        "Software/Microsoft/Microsoft Comic Chat");
    const QString rulesPath = root + QStringLiteral("/RuleSets");
    const QString notificationsPath = root
        + QStringLiteral("/Notifications");
    REQUIRE(originalSettingsRoot() == root);
    REQUIRE(originalSettingsSubKey(g_szRuleSetsSubKey) == rulesPath);
    REQUIRE(originalSettingsSubKey(g_szNotificationsSubKey)
            == notificationsPath);

    QSettings settings = originalUserSettings();
    settings.clear();
    settings.sync();

    CCRulesData sourceRulesData;
    CCDynaRules sourceRules;
    sourceRules.SetRulesData(&sourceRulesData);
    REQUIRE(sourceRules.bLoadRulesFromResource());

    const QString samplesName = resourceRuleSetName(
        QStringLiteral("IDS_SAMPLES_RULESET"));
    const QString generalName = resourceRuleSetName(
        QStringLiteral("IDS_GENERAL_RULESET"));
    CCRuleSet* samples = sourceRules.GetRuleSetFromName(samplesName);
    CCRuleSet* general = sourceRules.GetRuleSetFromName(generalName);
    REQUIRE(samples && general);
    REQUIRE(samples->GetRulesArray().size() == 7);

    const QByteArray firstRule = serializedRule(
        samples->GetRulesArray().first());
    settings.setValue(rulesPath + QLatin1Char('/') + samplesName
                          + QStringLiteral("/99"),
                      firstRule);
    settings.setValue(rulesPath + QLatin1Char('/')
                          + QString::fromLatin1(g_szRuleSetFlags),
                      static_cast<quint32>(MAKELONG(g_wActive, g_wVersion)));
    settings.sync();

    REQUIRE(sourceRules.bSaveRulesToReg());
    settings.sync();
    settings.beginGroup(rulesPath);
    const QStringList storedRuleSets = settings.childGroups();
    REQUIRE(storedRuleSets.size() == 2);
    REQUIRE(storedRuleSets.contains(samplesName));
    REQUIRE(storedRuleSets.contains(generalName));
    REQUIRE(settings.contains(QString::fromLatin1(g_szRuleSetFlags)));
    settings.endGroup();

    settings.beginGroup(rulesPath + QLatin1Char('/') + samplesName);
    REQUIRE(settings.value(QString::fromLatin1(g_szRuleSetFlags)).toUInt()
            == MAKELONG(samples->wGetFlags() & g_wActive, g_wVersion));
    REQUIRE(!settings.contains(QStringLiteral("99")));
    for (INT index = 0; index < samples->GetRulesArray().size(); ++index) {
        REQUIRE(settings.value(QString::number(index)).toByteArray()
                == serializedRule(samples->GetRulesArray().at(index)));
    }
    settings.endGroup();

    settings.beginGroup(rulesPath + QLatin1Char('/') + generalName);
    REQUIRE(settings.value(QString::fromLatin1(g_szRuleSetFlags)).toUInt()
            == MAKELONG(general->wGetFlags() & g_wActive, g_wVersion));
    REQUIRE(settings.childKeys().size() == 1);
    settings.endGroup();

    CCRulesData loadedRulesData;
    CCDynaRules loadedRules;
    loadedRules.SetRulesData(&loadedRulesData);
    REQUIRE(loadedRules.bLoadRulesFromReg());
    REQUIRE(loadedRules.GetRuleSetsArray().size() == 2);
    CCRuleSet* loadedSamples = loadedRules.GetRuleSetFromName(samplesName);
    CCRuleSet* loadedGeneral = loadedRules.GetRuleSetFromName(generalName);
    REQUIRE(loadedSamples && loadedGeneral);
    REQUIRE(loadedSamples->wGetFlags() == (samples->wGetFlags() & g_wActive));
    REQUIRE(loadedGeneral->wGetFlags() == (general->wGetFlags() & g_wActive));
    REQUIRE(loadedSamples->GetRulesArray().size()
            == samples->GetRulesArray().size());
    for (INT index = 0; index < loadedSamples->GetRulesArray().size(); ++index) {
        REQUIRE(serializedRule(loadedSamples->GetRulesArray().at(index))
                == serializedRule(samples->GetRulesArray().at(index)));
    }
    REQUIRE(loadedRules.bLoadRulesFromReg());
    REQUIRE(loadedRules.GetRuleSetsArray().size() == 2);

    settings.setValue(rulesPath + QLatin1Char('/') + samplesName
                          + QLatin1Char('/')
                          + QString::fromLatin1(g_szRuleSetFlags),
                      static_cast<quint32>(MAKELONG(
                          g_wActive, g_wVersion + 1)));
    settings.sync();
    CCRulesData wrongVersionRulesData;
    CCDynaRules wrongVersionRules;
    wrongVersionRules.SetRulesData(&wrongVersionRulesData);
    REQUIRE(wrongVersionRules.bLoadRulesFromReg());
    REQUIRE(!wrongVersionRules.GetRuleSetFromName(samplesName));
    REQUIRE(wrongVersionRules.GetRuleSetFromName(generalName));

    settings.remove(rulesPath);
    settings.sync();
    CCRulesData missingRulesData;
    CCDynaRules missingRules;
    missingRules.SetRulesData(&missingRulesData);
    REQUIRE(missingRules.bLoadRulesFromReg());
    REQUIRE(missingRules.GetRuleSetsArray().isEmpty());

    CCDynaNotifs sourceNotifications;
    CCNotif* sourceNotification = originalResourceNotification();
    const QByteArray notificationBytes = serializedNotification(
        sourceNotification);
    REQUIRE(sourceNotifications.bAddNotif(sourceNotification));
    const WORD notificationFlags = static_cast<WORD>(
        (g_uHostName << 12) | g_wSortDescending);
    sourceNotifications.SetFlags(notificationFlags);

    settings.setValue(notificationsPath + QStringLiteral("/0"),
                      notificationBytes);
    settings.setValue(notificationsPath + QStringLiteral("/1"),
                      notificationBytes);
    settings.setValue(notificationsPath + QStringLiteral("/9"),
                      notificationBytes);
    settings.setValue(notificationsPath + QLatin1Char('/')
                          + QString::fromLatin1(g_szNotificationFlags),
                      static_cast<quint32>(MAKELONG(0, g_wVersion)));
    settings.sync();

    REQUIRE(sourceNotifications.bSaveNotifsToReg());
    settings.sync();
    REQUIRE(settings.value(notificationsPath + QStringLiteral("/0"))
            .toByteArray() == notificationBytes);
    REQUIRE(!settings.contains(notificationsPath + QStringLiteral("/1")));
    // Four old values make the source delete numeric names 1, 2 and 3 only.
    REQUIRE(settings.contains(notificationsPath + QStringLiteral("/9")));
    REQUIRE(settings.value(notificationsPath + QLatin1Char('/')
                               + QString::fromLatin1(g_szNotificationFlags))
                .toUInt()
            == MAKELONG(notificationFlags, g_wVersion));

    settings.remove(notificationsPath + QStringLiteral("/9"));
    settings.sync();
    CCDynaNotifs loadedNotifications;
    REQUIRE(loadedNotifications.bLoadNotifsFromReg());
    REQUIRE(loadedNotifications.GetFlags() == notificationFlags);
    REQUIRE(loadedNotifications.GetNotifsArray().size() == 1);
    REQUIRE(*loadedNotifications.GetNotifsArray().first()
            == *sourceNotification);
    REQUIRE(loadedNotifications.GetNotifUsersArray()->GetSize() == 0);
    REQUIRE(loadedNotifications.GetWhosCount() == 0);
    REQUIRE(loadedNotifications.GetUpdateCount() == 0);
    REQUIRE(loadedNotifications.GetModifiedUsersCount() == 0);
    REQUIRE(loadedNotifications.bLoadNotifsFromReg());
    REQUIRE(loadedNotifications.GetNotifsArray().size() == 2);

    settings.setValue(notificationsPath + QLatin1Char('/')
                          + QString::fromLatin1(g_szNotificationFlags),
                      static_cast<quint32>(MAKELONG(
                          notificationFlags, g_wVersion + 1)));
    settings.sync();
    CCDynaNotifs wrongVersionNotifications;
    REQUIRE(wrongVersionNotifications.bLoadNotifsFromReg());
    REQUIRE(wrongVersionNotifications.GetFlags() == 0);
    REQUIRE(wrongVersionNotifications.GetNotifsArray().size() == 1);

    QByteArray overlongNotification = notificationBytes;
    overlongNotification.append('\0');
    settings.setValue(notificationsPath + QStringLiteral("/0"),
                      overlongNotification);
    settings.sync();
    CCDynaNotifs overlongNotifications;
    REQUIRE(overlongNotifications.bLoadNotifsFromReg());
    REQUIRE(overlongNotifications.GetNotifsArray().isEmpty());

    settings.remove(notificationsPath);
    settings.sync();
    CCDynaNotifs missingNotifications;
    REQUIRE(missingNotifications.bLoadNotifsFromReg());
    REQUIRE(missingNotifications.GetNotifsArray().isEmpty());
    return 0;
}
