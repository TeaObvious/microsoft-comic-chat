// Ported from v2.5-beta-1-modern/avatario.cpp.

#include "avatario.h"

#include "avatar.h"
#include "protsupp.h"
#include "originalassets.h"

#include <QDir>
#include <QFileInfo>

CAvatarX* LoadAvatarInfo(const QString& avatarName)
{
    if (avatarName.isEmpty() || QFileInfo(avatarName).fileName() != avatarName) {
        return nullptr;
    }
    const QString path = originalComicArtPath(avatarName + QStringLiteral(".avb"));
    if (path.isEmpty()) {
        return nullptr;
    }

    auto* stream = new CAvatarFileStream(path);
    CAvatarX* avatar = CAvatarX::LoadAvatar(stream);
    if (!avatar) {
        delete stream;
        return nullptr;
    }
    avatar->SetStream(stream);
    avatar->SetNewName(avatarName);
    return avatar;
}

QStringList OriginalAvatarNames()
{
    QDir directory(QDir(originalAssetRoot()).filePath(QStringLiteral("comicart")));
    const QStringList files = directory.entryList({QStringLiteral("*.avb")}, QDir::Files,
                                                   QDir::NoSort);
    QStringList names;
    names.reserve(files.size());
    for (const QString& file : files) {
        names.append(QFileInfo(file).completeBaseName());
    }
    return names;
}

float emFloats[] = {
    0.0f,
    EM_HAPPY,
    EM_COY,
    EM_BORED,
    EM_SCARED,
    EM_SAD,
    EM_ANGRY,
    EM_SHOUT,
    EM_LAUGH,
    EM_NEUTRAL,
    EM_WAVE,
    EM_POINTOTHER,
    EM_POINTSELF,
    EM_DOUBLEPOINT,
    EM_SHRUG,
    EM_3QRWALK,
    EM_SIDEWALK,
    EM_3QFWALK,
};

void EmotionToBytes(CEmotion& em, unsigned char& emotion, unsigned char& intensity)
{
    unsigned char emVal = 9;
    const int count = static_cast<int>(sizeof(emFloats) / sizeof(float));
    for (int i = 1; i < count; ++i) {
        if (emFloats[i] == em.m_emotion) {
            emVal = static_cast<unsigned char>(i);
            break;
        }
    }
    const unsigned char inVal = static_cast<unsigned char>(em.m_intensity * 10.0f);
    emotion = IndexToByte(emVal);
    intensity = IndexToByte(inVal);
}

void BytesToEmotion(CEmotion& em, unsigned char emIndex, unsigned char inIndex)
{
    const int count = static_cast<int>(sizeof(emFloats) / sizeof(float));
    em.m_emotion = emIndex >= count ? EM_NEUTRAL : emFloats[emIndex];
    em.m_intensity = static_cast<float>(inIndex / 10.0);
}

float EmotionToFloat(int index)
{
    const int count = static_cast<int>(sizeof(emFloats) / sizeof(float));
    if (index < 0 || index >= count) {
        return 0.0f;
    }
    return emFloats[index];
}
