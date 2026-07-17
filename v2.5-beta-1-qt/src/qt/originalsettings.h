// Qt-only adapter for the HKCU registry boundary used by the original code.
// It supplies only the shared QSettings backend and normalized original key
// path; all persisted values and behavior remain in their original modules.

#pragma once

#include <QSettings>
#include <QString>

QString originalSettingsRoot();
QString originalSettingsSubKey(const char* originalSubKey);
QSettings originalUserSettings();
