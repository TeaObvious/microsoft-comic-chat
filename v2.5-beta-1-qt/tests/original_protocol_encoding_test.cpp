#include "avatar.h"
#include "avatario.h"
#include "defines.h"
#include "protsupp.h"

#include <cmath>
#include <iostream>

namespace {
bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}
}

int main()
{
    bool okay = true;
    okay &= expect(IndexToByte(0) == static_cast<unsigned char>('0'), "IndexToByte(0)");
    okay &= expect(IndexToByte(17) == static_cast<unsigned char>('A'), "IndexToByte(17)");
    okay &= expect(ByteToIndex(static_cast<unsigned char>('0')) == 0, "ByteToIndex('0')");
    okay &= expect(ByteToIndex(static_cast<unsigned char>('A')) == 17, "ByteToIndex('A')");

    okay &= expect(SM2BM(SM_WHISPER) == BM_WHISPER, "SM2BM whisper");
    okay &= expect(SM2BM(SM_THINK) == BM_THINK, "SM2BM think");
    okay &= expect(SM2BM(SM_ACTION) == BM_ACTION, "SM2BM action");
    okay &= expect(SM2BM(0xff) == BM_SAY, "SM2BM default say");
    okay &= expect(BM2SM(BM_ACTION | BM_WHISPER) == SM_ACTION, "BM2SM action priority");
    okay &= expect(BM2SM(BM_SOUND | BM_THINK) == SM_ACTION, "BM2SM sound priority");
    okay &= expect(BM2SM(BM_WHISPER | BM_THINK) == SM_WHISPER, "BM2SM whisper priority");
    okay &= expect(BM2SM(BM_THINK) == SM_THINK, "BM2SM think");
    okay &= expect(BM2SM(BM_SAY) == SM_SAY, "BM2SM say");

    CEmotion special(1.0, EM_WAVE);
    unsigned char emotionByte = 0;
    unsigned char intensityByte = 0;
    EmotionToBytes(special, emotionByte, intensityByte);
    okay &= expect(emotionByte == IndexToByte(10), "EmotionToBytes special index");
    okay &= expect(intensityByte == IndexToByte(10), "EmotionToBytes intensity");

    CEmotion decoded;
    BytesToEmotion(decoded, 10, 10);
    okay &= expect(decoded.m_emotion == EM_WAVE, "BytesToEmotion special");
    okay &= expect(std::fabs(decoded.m_intensity - 1.0f) < 0.0001f,
                   "BytesToEmotion intensity");

    CEmotion unknown(0.3, 12345.0);
    EmotionToBytes(unknown, emotionByte, intensityByte);
    okay &= expect(emotionByte == IndexToByte(9), "unknown emotion falls back to neutral");
    okay &= expect(intensityByte == IndexToByte(3), "unknown emotion preserves intensity");

    return okay ? 0 : 1;
}

