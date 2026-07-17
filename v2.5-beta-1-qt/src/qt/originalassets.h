// Qt-only helper for resolving files from v2.5-beta-1-modern/res.

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

enum class OriginalMenuItemType {
    Command,
    Popup,
    Separator
};

struct OriginalMenuItem {
    OriginalMenuItemType type = OriginalMenuItemType::Separator;
    QString text;
    QString commandIdentifier;
    QStringList flags;
    QList<OriginalMenuItem> children;
};

struct OriginalToolbarItem {
    bool separator = false;
    QString commandIdentifier;
};

struct OriginalToolbarResource {
    int buttonWidth = 0;
    int buttonHeight = 0;
    QList<OriginalToolbarItem> items;
};

struct OriginalAccelerator {
    QString key;
    QString commandIdentifier;
    bool virtualKey = false;
    bool alt = false;
    bool control = false;
    bool shift = false;
    bool noInvert = false;
};

struct OriginalDialogControl {
    QString type;
    QString text;
    QString identifier;
    QString style;
    bool visible = true;
    int x = -1;
    int y = -1;
    int width = -1;
    int height = -1;
    QStringList fields;
};

struct OriginalDialogResource {
    QString caption;
    QString fontFamily;
    int fontPointSize = -1;
    int x = -1;
    int y = -1;
    int width = -1;
    int height = -1;
    QList<OriginalDialogControl> controls;
};

QString originalAssetRoot();
QString originalArtifactsRoot();
QString originalAssetPath(const QString& relativePath);
QString originalArtifactPath(const QString& relativePath);
QString originalResourcePath(const QString& fileName);
QString originalComicArtPath(const QString& fileName);
QString originalArtPackPath(const QString& fileName);
QString originalArtPackArchivePath(const QString& fileName);

// Resolves a file declaration such as `IDR_MAINFRAME BITMAP ...` directly
// from chat.rc. resourceType is the RC type token (for example BITMAP/ICON).
QString originalFileResourcePath(const QString& resourceIdentifier,
                                 const QString& resourceType);

// Qt replacement for CString::LoadString. The value is read directly from
// the authoritative chat.rc; it is not a translated or generated copy.
QString originalResourceString(const QString& identifier);
QString originalResourceString(int identifier);
QString originalTextViewResourceString(const QString& identifier);
QString originalDialogCaption(const QString& resourceIdentifier);
OriginalDialogResource originalDialogResource(
    const QString& resourceIdentifier);
QString originalDialogControlText(const QString& resourceIdentifier,
                                  const QString& controlIdentifier,
                                  int occurrence = 0);
// Decodes string entries written by an RC DLGINIT block (for example the
// exact Alt+0..Alt+9 entries of IDD_AUTOMATION_PAGE).
QStringList originalDialogInitStrings(const QString& resourceIdentifier,
                                      const QString& controlIdentifier);

// Qt replacement for CMenu::LoadMenu text lookup. The command label is read
// directly from the authoritative chat.rc resource declaration.
QString originalMenuItemText(const QString& commandIdentifier);

// Qt replacements for CMenu::LoadMenu, CToolBar::LoadToolBar and
// LoadAccelerators. They parse the authoritative declarations in chat.rc and
// retain the original symbolic command identifiers. MFC standard command IDs
// come from the external afxres.h in the Windows build and are deliberately
// not assigned invented numeric values here.
QList<OriginalMenuItem> originalMenuResource(const QString& resourceIdentifier);
OriginalToolbarResource originalToolbarResource(const QString& resourceIdentifier);
QList<OriginalAccelerator> originalAcceleratorResource(const QString& resourceIdentifier);
