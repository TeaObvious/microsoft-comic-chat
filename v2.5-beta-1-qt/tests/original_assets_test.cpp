#include "originalassets.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdlib>

namespace {
bool checkDirectory(const QString& relativeDirectory, int expectedFileCount)
{
    const QDir directory(QDir(originalAssetRoot()).filePath(relativeDirectory));
    const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    if (files.size() != expectedFileCount) {
        return false;
    }

    for (const QFileInfo& file : files) {
        const QString relativePath = relativeDirectory + QLatin1Char('/') + file.fileName();
        if (originalAssetPath(relativePath) != file.canonicalFilePath()) {
            return false;
        }
    }
    return true;
}
}

int main()
{
    if (originalAssetRoot().isEmpty()) {
        return EXIT_FAILURE;
    }
    const QString comicFontPath = originalV1SharedPath(
        QStringLiteral("comic.ttf"));
    QFile comicFont(comicFontPath);
    if (comicFontPath.isEmpty() || !comicFont.open(QIODevice::ReadOnly)
        || comicFont.size() != 63040
        || QCryptographicHash::hash(comicFont.readAll(),
                                    QCryptographicHash::Sha256).toHex()
            != QByteArrayLiteral(
                "08e336a641ef44f0a6c745a52c64ba10ad58a8aad631e3db79c98cb703b9893f")) {
        return EXIT_FAILURE;
    }
    if (!checkDirectory(QStringLiteral("res"), 53)
        || !checkDirectory(QStringLiteral("comicart"), 32)
        || !checkDirectory(QStringLiteral("artpack1"), 12)
        || !checkDirectory(QStringLiteral("artpack1/archive"), 12)) {
        return EXIT_FAILURE;
    }

    if (originalResourcePath(QStringLiteral("toolbar.bmp"))
            != originalAssetPath(QStringLiteral("res/toolbar.bmp"))) {
        return EXIT_FAILURE;
    }
    if (originalFileResourcePath(QStringLiteral("IDR_MAINFRAME"), QStringLiteral("BITMAP"))
            != originalResourcePath(QStringLiteral("toolbar.bmp"))
        || originalFileResourcePath(QStringLiteral("IDB_TABS"), QStringLiteral("BITMAP"))
            != originalResourcePath(QStringLiteral("tabbar.bmp"))
        || originalFileResourcePath(QStringLiteral("IDB_TIKI"), QStringLiteral("BITMAP"))
            != originalResourcePath(QStringLiteral("tiki2.bmp"))
        || originalFileResourcePath(QStringLiteral("IDR_MAINFRAME"), QStringLiteral("ICON"))
            != originalResourcePath(QStringLiteral("chat.ico"))) {
        return EXIT_FAILURE;
    }
    if (originalComicArtPath(QStringLiteral("tiki.avb"))
            != originalAssetPath(QStringLiteral("comicart/tiki.avb"))) {
        return EXIT_FAILURE;
    }
    if (originalComicArtPath(QStringLiteral("room.bgb"))
            != originalAssetPath(QStringLiteral("comicart/room.bgb"))) {
        return EXIT_FAILURE;
    }
    if (originalArtPackPath(QStringLiteral("bolo.avb"))
            != originalAssetPath(QStringLiteral("artpack1/bolo.avb"))) {
        return EXIT_FAILURE;
    }
    if (originalArtPackArchivePath(QStringLiteral("bolo.avb"))
            != originalAssetPath(QStringLiteral("artpack1/archive/bolo.avb"))) {
        return EXIT_FAILURE;
    }

    if (!originalAssetPath(QStringLiteral("../source-overview.md")).isEmpty()
        || !originalAssetPath(QStringLiteral("res/does-not-exist.bmp")).isEmpty()
        || !originalV1SharedPath(QStringLiteral("../comic.ttf")).isEmpty()) {
        return EXIT_FAILURE;
    }
    if (originalResourceString(QStringLiteral("ID_EM_HAPPY")) != QStringLiteral("Happy")
        || originalResourceString(QStringLiteral("ID_EMOTION_IS")) != QStringLiteral("Emotion is %1")
        || originalResourceString(QStringLiteral("ID_RULE_SHOUT"))
            != QStringLiteral("AllCaps(\"\");9\nFindString(\"!!!\");9")
        || originalResourceString(QStringLiteral("ID_RULE_LAUGH"))
            != QStringLiteral("CheckWord*(\"ROTFL\");11\nCheckWord*(\"LOL\");11\nFindString*(\"HEHE\");11")
        || originalResourceString(QStringLiteral("ID_RULE_ANGRY"))
            != QStringLiteral("\"\"")
        || originalMenuItemText(QStringLiteral("ID_BODYCONTEXT_FREEZE")) != QStringLiteral("&Frozen")
        || originalMenuItemText(QStringLiteral("ID_BODYCONTEXT_SENDEXPRESSION"))
            != QStringLiteral("&Send Expression")) {
        return EXIT_FAILURE;
    }

    const QList<OriginalMenuItem> mainMenu = originalMenuResource(QStringLiteral("IDR_MAINFRAME"));
    if (mainMenu.size() != 9
        || mainMenu[0].type != OriginalMenuItemType::Popup
        || mainMenu[0].text != QStringLiteral("&File")
        || mainMenu[0].children.size() != 12
        || mainMenu[0].children[0].commandIdentifier != QStringLiteral("ID_SESSION_CONNECT")
        || mainMenu[0].children[1].commandIdentifier != QStringLiteral("ID_FILE_OPEN")
        || mainMenu[0].children[11].commandIdentifier != QStringLiteral("ID_APP_EXIT")
        || mainMenu[2].text != QStringLiteral("&View")
        || mainMenu[2].children[0].type != OriginalMenuItemType::Popup
        || mainMenu[2].children[0].children[1].commandIdentifier
            != QStringLiteral("ID_VIEW_TOOLBAR_MEMBER")
        || mainMenu[8].children.last().commandIdentifier != QStringLiteral("ID_APP_ABOUT")) {
        return EXIT_FAILURE;
    }

    const QList<OriginalMenuItem> bodyContext = originalMenuResource(
        QStringLiteral("IDR_BODYCONTEXT"));
    if (bodyContext.size() != 1
        || bodyContext.first().type != OriginalMenuItemType::Popup
        || bodyContext.first().text != QStringLiteral("&BodyContext")
        || bodyContext.first().children.size() != 2
        || bodyContext.first().children[0].type
            != OriginalMenuItemType::Command
        || bodyContext.first().children[0].text
            != QStringLiteral("&Frozen")
        || bodyContext.first().children[0].commandIdentifier
            != QStringLiteral("ID_BODYCONTEXT_FREEZE")
        || bodyContext.first().children[1].type
            != OriginalMenuItemType::Command
        || bodyContext.first().children[1].text
            != QStringLiteral("&Send Expression")
        || bodyContext.first().children[1].commandIdentifier
            != QStringLiteral("ID_BODYCONTEXT_SENDEXPRESSION")) {
        return EXIT_FAILURE;
    }

    const OriginalToolbarResource mainToolbar = originalToolbarResource(
        QStringLiteral("IDR_MAINFRAME"));
    const OriginalToolbarResource textToolbar = originalToolbarResource(
        QStringLiteral("IDR_TEXTTOOLBAR"));
    const OriginalToolbarResource userToolbar = originalToolbarResource(
        QStringLiteral("IDR_USERTOOLBAR"));
    if (mainToolbar.buttonWidth != 16 || mainToolbar.buttonHeight != 16
        || mainToolbar.items.size() != 13
        || mainToolbar.items[0].commandIdentifier != QStringLiteral("ID_SESSION_CONNECT")
        || !mainToolbar.items[5].separator
        || mainToolbar.items.last().commandIdentifier
            != QStringLiteral("ID_FAVORITES_OPENFAVORITES")
        || textToolbar.items.size() != 7
        || textToolbar.items[0].commandIdentifier != QStringLiteral("ID_SETFONT")
        || textToolbar.items.last().commandIdentifier != QStringLiteral("ID_SWITCHSYMBOL")
        || userToolbar.items.size() != 8
        || !userToolbar.items[4].separator
        || userToolbar.items.last().commandIdentifier != QStringLiteral("ID_START_NETMEETING")) {
        return EXIT_FAILURE;
    }

    const QList<OriginalAccelerator> mainAccelerators = originalAcceleratorResource(
        QStringLiteral("IDR_MAINFRAME"));
    const QList<OriginalAccelerator> rtfAccelerators = originalAcceleratorResource(
        QStringLiteral("IDR_RTFACCEL"));
    const QList<OriginalAccelerator> whisperAccelerators = originalAcceleratorResource(
        QStringLiteral("IDR_WHISPERACCEL"));
    if (mainAccelerators.size() != 31
        || mainAccelerators[0].key != QStringLiteral("0")
        || mainAccelerators[0].commandIdentifier != QStringLiteral("ID_MACRO_A0")
        || !mainAccelerators[0].virtualKey || !mainAccelerators[0].alt
        || mainAccelerators.last().key != QStringLiteral("Z")
        || mainAccelerators.last().commandIdentifier != QStringLiteral("ID_EDIT_UNDO")
        || !mainAccelerators.last().control
        || rtfAccelerators.size() != 6
        || rtfAccelerators[0].commandIdentifier != QStringLiteral("ID_SWITCHFIXEDPITCH")
        || whisperAccelerators.size() != 3
        || whisperAccelerators.last().commandIdentifier != QStringLiteral("ID_WHISPER_SOUND")) {
        return EXIT_FAILURE;
    }

    const OriginalDialogResource setupDialog = originalDialogResource(
        QStringLiteral("IDD_SETUPDIALOG"));
    if (setupDialog.caption != QStringLiteral("Connect")
        || setupDialog.width != 252 || setupDialog.height != 218
        || setupDialog.fontPointSize != 8
        || setupDialog.fontFamily != QStringLiteral("MS Sans Serif")
        || originalDialogControlText(QStringLiteral("IDD_SETUPDIALOG"),
                                     QStringLiteral("IDC_STATIC"), 0)
            != QStringLiteral("Welcome to Microsoft Chat.  You can specify chat server connection information here, and optionally adjust your Personal Information from the next tab.  ")
        || originalDialogControlText(QStringLiteral("IDD_SETUPDIALOG"),
                                     QStringLiteral("IDC_CONCHAN"))
            != QStringLiteral("&Go to chat room:")
        || originalDialogControlText(QStringLiteral("IDD_SETUPDIALOG"),
                                     QStringLiteral("IDC_STATIC"), 1)
            != QStringLiteral("&Favorites:")
        || originalDialogControlText(QStringLiteral("IDD_PERSONALPAGE_IRC"),
                                     QStringLiteral("IDC_STATIC"), 4)
            != QStringLiteral("&Brief description of yourself:")
        || originalDialogControlText(QStringLiteral("IDD_CHANNELPROP"),
                                     QStringLiteral("IDC_MODERATED"))
            != QStringLiteral("&Moderated")
        || originalDialogCaption(QStringLiteral("IDD_CHANNELCREATE"))
            != QStringLiteral("Create Chat Room")) {
        return EXIT_FAILURE;
    }

    const OriginalDialogResource aboutDialog = originalDialogResource(
        QStringLiteral("IDD_ABOUTBOX"));
    if (aboutDialog.caption != QStringLiteral("About Microsoft Chat")
        || aboutDialog.width != 279 || aboutDialog.height != 137
        || aboutDialog.fontPointSize != 8
        || aboutDialog.fontFamily != QStringLiteral("Comic Sans MS")
        || aboutDialog.controls.size() != 8
        || originalDialogControlText(QStringLiteral("IDD_ABOUTBOX"),
                                     QStringLiteral("IDC_VERSION"))
            != QStringLiteral("%1 %2 (%3)")
        || originalDialogControlText(QStringLiteral("IDD_ABOUTBOX"),
                                     QStringLiteral("IDC_WARNING"))
            != QStringLiteral("To be replaced by IDS_WARNING_TEXT")
        || originalDialogControlText(QStringLiteral("IDD_ABOUTBOX"),
                                     QStringLiteral("IDC_CORP"))
            != QStringLiteral("Microsoft Corporation")
        || originalDialogControlText(QStringLiteral("IDD_ABOUTBOX"),
                                     QStringLiteral("IDC_USER"))
            != QStringLiteral("User")
        || originalDialogControlText(QStringLiteral("IDD_ABOUTBOX"),
                                     QStringLiteral("IDC_LICENSE"))
            != QStringLiteral("This product is licensed to")
        || aboutDialog.controls.last().identifier
            != QStringLiteral("IDC_TIKI")
        || aboutDialog.controls.last().text != QStringLiteral("203")
        || !aboutDialog.controls.last().style.contains(
            QStringLiteral("SS_BITMAP"))
        || aboutDialog.controls.last().x != 0
        || aboutDialog.controls.last().y != 0
        || aboutDialog.controls.last().width != 280
        || aboutDialog.controls.last().height != 137) {
        return EXIT_FAILURE;
    }

    const QStringList automationKeys = originalDialogInitStrings(
        QStringLiteral("IDD_AUTOMATION_PAGE"), QStringLiteral("IDC_KEY"));
    if (automationKeys.size() != 10
        || automationKeys.first() != QStringLiteral("Alt+0")
        || automationKeys.last() != QStringLiteral("Alt+9")) {
        return EXIT_FAILURE;
    }
    const OriginalDialogResource channelProperties = originalDialogResource(
        QStringLiteral("IDD_CHANNELPROP"));
    bool auditoriumHidden = false;
    bool noWhispersHidden = false;
    for (const OriginalDialogControl& control : channelProperties.controls) {
        if (control.identifier == QLatin1String("IDC_AUDITORIUM"))
            auditoriumHidden = !control.visible;
        if (control.identifier == QLatin1String("IDC_NOWHISPERS"))
            noWhispersHidden = !control.visible;
    }
    if (!auditoriumHidden || !noWhispersHidden) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
