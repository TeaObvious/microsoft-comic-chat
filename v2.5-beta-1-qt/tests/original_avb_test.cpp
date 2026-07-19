#include "avatar.h"
#include "avbfile.h"
#include "avatario.h"
#include "backdrop.h"
#include "chat.h"
#include "originalassets.h"
#include "protsupp.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <iostream>

namespace {
bool failure(const QString& path, const char* message)
{
    std::cerr << path.toStdString() << ": " << message << '\n';
    return false;
}

bool loadAvatarFile(const QString& path)
{
    auto* stream = new CAvatarFileStream(path);
    CAvatarX* avatar = CAvatarX::LoadAvatar(stream);
    if (!avatar) {
        delete stream;
        return failure(path, "CAvatarX::LoadAvatar failed");
    }
    avatar->SetStream(stream);
    avatar->SetNewName(QFileInfo(path).completeBaseName());
    if (avatar->GetPoseCount() <= 0 || avatar->m_icon == INVALID_POSE_ID) {
        delete avatar;
        return failure(path, "missing pose/icon records");
    }
    avatar->IndexAvatar();
    if (!avatar->m_body) {
        return failure(path, "SetNeutral did not select an original body record");
    }
    CPose* icon = avatar->GetIconPose();
    if (!icon || !icon->GetDrawing() || !icon->GetDrawing()->IsValid()
        || icon->GetDrawing()->Image().isNull()) {
        return failure(path, "lazy icon pose load failed");
    }
    QImage bodyImage(320, 240, QImage::Format_RGB32);
    bodyImage.fill(Qt::white);
    RECT bodyArea{0, 0, bodyImage.width(), bodyImage.height()};
    const RECT bodyBounds = avatar->m_body->DrawBody(&bodyImage, bodyArea, TRUE);
    if (bodyBounds.left == bodyBounds.right || bodyBounds.top == bodyBounds.bottom) {
        return failure(path, "original body geometry is empty");
    }
    bool containsDrawing = false;
    for (int y = 0; y < bodyImage.height() && !containsDrawing; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(bodyImage.constScanLine(y));
        for (int x = 0; x < bodyImage.width(); ++x) {
            if ((line[x] & 0x00ffffffU) != 0x00ffffffU) {
                containsDrawing = true;
                break;
            }
        }
    }
    if (!containsDrawing) {
        return failure(path, "original body drawing produced no pixels");
    }
    return true;
}

bool loadDibFile(const QString& path)
{
    CAvatarFileStream stream(path);
    if (!stream.Open()) {
        return failure(path, "stream open failed");
    }
    CAvatarDIB dib;
    const bool result = dib.Load(&stream);
    stream.Close();
    if (!result || !dib.IsValid() || dib.Image().isNull()) {
        return failure(path, "CAvatarDIB::Load failed");
    }
    return true;
}

bool loadBackdropFile(const QString& path)
{
    CAvatarFileStream stream(path);
    CChatBackdrop* backdrop = CChatBackdrop::LoadBackdrop(&stream);
    if (!backdrop) {
        return failure(path, "CChatBackdrop::LoadBackdrop failed");
    }
    const bool valid = backdrop->GetDrawing() && backdrop->GetDrawing()->IsValid()
        && !backdrop->GetDrawing()->Image().isNull();
    delete backdrop;
    return valid || failure(path, "backdrop drawing is invalid");
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    theApp.InitVals();
    InitializeAvatars();

    bool okay = true;
    int avatarCount = 0;
    const QString root = originalAssetRoot();
    const QStringList avatarDirectories = {
        QDir(root).filePath(QStringLiteral("comicart")),
        QDir(root).filePath(QStringLiteral("artpack1")),
        QDir(root).filePath(QStringLiteral("artpack1/archive")),
    };
    for (const QString& directoryPath : avatarDirectories) {
        QDir directory(directoryPath);
        const QStringList files = directory.entryList({QStringLiteral("*.avb")}, QDir::Files,
                                                       QDir::Name);
        for (const QString& file : files) {
            ++avatarCount;
            okay &= loadAvatarFile(directory.filePath(file));
        }
    }
    if (avatarCount != 45) {
        std::cerr << "expected 45 AVB files, got " << avatarCount << '\n';
        okay = false;
    }

    int dibCount = 0;
    const QStringList dibDirectories = {
        QDir(root).filePath(QStringLiteral("res")),
        QDir(root).filePath(QStringLiteral("artpack1/archive")),
    };
    for (const QString& directoryPath : dibDirectories) {
        QDir directory(directoryPath);
        const QStringList files = directory.entryList(
            {QStringLiteral("*.bmp"), QStringLiteral("*.dib"), QStringLiteral("*.rle")},
            QDir::Files, QDir::Name);
        for (const QString& file : files) {
            ++dibCount;
            okay &= loadDibFile(directory.filePath(file));
        }
    }
    if (dibCount != 43) {
        std::cerr << "expected 43 directly stored DIB/BMP/RLE files, got " << dibCount << '\n';
        okay = false;
    }

    int backdropCount = 0;
    const QStringList backdropDirectories = {
        QDir(root).filePath(QStringLiteral("comicart")),
        QDir(root).filePath(QStringLiteral("artpack1")),
    };
    for (const QString& directoryPath : backdropDirectories) {
        QDir directory(directoryPath);
        const QStringList files = directory.entryList({QStringLiteral("*.bgb")}, QDir::Files,
                                                       QDir::Name);
        for (const QString& file : files) {
            ++backdropCount;
            okay &= loadBackdropFile(directory.filePath(file));
        }
    }
    if (backdropCount != 9) {
        std::cerr << "expected 9 BGB files, got " << backdropCount << '\n';
        okay = false;
    }

    DestroyAvatars();

    SetArtDir("ARTPACK1");
    const QString artPackDirectory = QFileInfo(
        QDir(root).filePath(QStringLiteral("artpack1"))).canonicalFilePath();
    const QStringList artPackAvatars = OriginalAvatarNames();
    const QStringList artPackBackdrops = OriginalBackdropNames();
    if (!ArtDirsOK()
        || theApp.GetAvatarDir() != artPackDirectory
        || theApp.GetBackDropDir() != artPackDirectory
        || !artPackAvatars.contains(QStringLiteral("kevin"),
                                    Qt::CaseInsensitive)
        || artPackAvatars.contains(QStringLiteral("anna"),
                                   Qt::CaseInsensitive)
        || !artPackBackdrops.contains(QStringLiteral("den.bgb"),
                                      Qt::CaseInsensitive)
        || !artPackBackdrops.contains(QStringLiteral("volcano.bgb"),
                                      Qt::CaseInsensitive)) {
        okay &= failure(artPackDirectory,
                        "SetArtDir did not select the original artpack1 inventory");
    }

    CAvatarX* artPackAvatar = LoadAvatarInfo(QStringLiteral("KEVIN"));
    if (!artPackAvatar) {
        okay &= failure(artPackDirectory,
                        "case-insensitive Win32 AVB lookup was not preserved");
    }
    delete artPackAvatar;
    CChatBackdrop* artPackBackdrop = LoadBackdropInfo(
        QStringLiteral("DEN.BGB"));
    if (!artPackBackdrop) {
        okay &= failure(artPackDirectory,
                        "case-insensitive Win32 BGB lookup was not preserved");
    }
    delete artPackBackdrop;

    SetArtDir("ComicArt");
    const QString comicArtDirectory = QFileInfo(
        QDir(root).filePath(QStringLiteral("comicart"))).canonicalFilePath();
    const QStringList comicArtAvatars = OriginalAvatarNames();
    if (!ArtDirsOK()
        || theApp.GetAvatarDir() != comicArtDirectory
        || theApp.GetBackDropDir() != comicArtDirectory
        || !comicArtAvatars.contains(QStringLiteral("anna"),
                                     Qt::CaseInsensitive)
        || comicArtAvatars.contains(QStringLiteral("kevin"),
                                    Qt::CaseInsensitive)) {
        okay &= failure(comicArtDirectory,
                        "SetArtDir did not restore the original ComicArt inventory");
    }

    SetArtDir("res");
    if (ArtDirsOK()) {
        okay &= failure(theApp.GetAvatarDir(),
                        "ArtDirsOK accepted a directory without an AVB file");
    }
    SetArtDir("ComicArt");

    return okay ? 0 : 1;
}
