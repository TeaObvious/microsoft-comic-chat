#include "originalsettings.h"

#include "defines.h"

QString originalSettingsRoot()
{
    QString root = QString::fromLatin1(szRootRegKeyName);
    return root.replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString originalSettingsSubKey(const char* originalSubKey)
{
    QString subKey = QString::fromLatin1(originalSubKey);
    subKey.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (subKey.startsWith(QLatin1Char('/'))) subKey.remove(0, 1);
    return subKey.isEmpty()
        ? originalSettingsRoot()
        : originalSettingsRoot() + QLatin1Char('/') + subKey;
}

QSettings originalUserSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("Microsoft"),
                     QStringLiteral("Microsoft Comic Chat"));
}
