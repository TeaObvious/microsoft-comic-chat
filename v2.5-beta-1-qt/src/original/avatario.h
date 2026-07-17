// Ported from v2.5-beta-1-modern/avatario.h.

#pragma once

class CEmotion;
class CAvatarX;

#include <QString>
#include <QStringList>

extern float emFloats[];
CAvatarX* LoadAvatarInfo(const QString& avatarName);
QStringList OriginalAvatarNames();
void EmotionToBytes(CEmotion& emotion, unsigned char& emotionByte, unsigned char& intensityByte);
void BytesToEmotion(CEmotion& emotion, unsigned char emotionIndex, unsigned char intensityIndex);
float EmotionToFloat(int index);
