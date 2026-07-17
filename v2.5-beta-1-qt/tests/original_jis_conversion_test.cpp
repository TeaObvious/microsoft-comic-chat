#include "fechrcnv.h"

#include <array>
#include <cstring>
#include <iostream>

namespace {
bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

template<std::size_t N>
bool bytesEqual(const UCHAR* actual, const std::array<UCHAR, N>& expected)
{
    return std::memcmp(actual, expected.data(), N) == 0;
}
}

int main()
{
    bool okay = true;

    std::array<UCHAR, 2> jisCharacter{};
    const std::array<UCHAR, 2> shiftJisCharacter{0x82, 0xa0};
    ShiftJISChar_to_JISChar(
        const_cast<UCHAR*>(shiftJisCharacter.data()), jisCharacter.data());
    okay &= expect(jisCharacter == std::array<UCHAR, 2>{0x24, 0x22},
                   "single Shift-JIS character conversion");

    std::array<UCHAR, 2> shiftJisRoundtrip{};
    JISChar_to_ShiftJISChar(jisCharacter.data(), shiftJisRoundtrip.data());
    okay &= expect(shiftJisRoundtrip == std::array<UCHAR, 2>{0x82, 0x7d},
                   "single JIS helper precedence quirk");

    std::array<UCHAR, 3> shiftJisInput{0x82, 0xa0, 0x00};
    const int jisRequired = ShiftJIS_to_JIS(
        shiftJisInput.data(), -1, nullptr, 0);
    okay &= expect(jisRequired == 9, "Shift-JIS required size");
    std::array<UCHAR, 9> jisOutput{};
    okay &= expect(ShiftJIS_to_JIS(shiftJisInput.data(), -1,
                                   jisOutput.data(), jisOutput.size()) == 9,
                   "Shift-JIS conversion length");
    const std::array<UCHAR, 9> expectedJis{
        ESC, KANJI_IN_1ST_CHAR, KANJI_IN_2ND_CHAR1,
        0x24, 0x22,
        ESC, KANJI_OUT_1ST_CHAR, KANJI_OUT_2ND_CHAR2,
        0x00
    };
    okay &= expect(jisOutput == expectedJis,
                   "IRC-standard JIS Kanji in/out bytes");
    okay &= expect(ShiftJIS_to_JIS(shiftJisInput.data(), -1,
                                   jisOutput.data(), 8) == -1,
                   "Shift-JIS short destination");

    CONV_CONTEXT context{};
    const int shiftJisRequired = JIS_to_ShiftJIS(
        &context, jisOutput.data(), jisOutput.size(), nullptr, 0);
    okay &= expect(shiftJisRequired == 3, "JIS required size");
    context = {};
    std::array<UCHAR, 3> convertedShiftJis{};
    okay &= expect(JIS_to_ShiftJIS(
                       &context, jisOutput.data(), jisOutput.size(),
                       convertedShiftJis.data(), convertedShiftJis.size()) == 3,
                   "JIS conversion length");
    okay &= expect(convertedShiftJis == shiftJisInput,
                   "JIS/Shift-JIS roundtrip");
    context = {};
    okay &= expect(JIS_to_ShiftJIS(
                       &context, jisOutput.data(), jisOutput.size(),
                       convertedShiftJis.data(), 2) == -1,
                   "JIS short destination");

    std::array<UCHAR, 2> kanaInput{0xa6, 0x00};
    std::array<UCHAR, 4> kanaJis{};
    okay &= expect(ShiftJIS_to_JIS(kanaInput.data(), -1, kanaJis.data(),
                                   kanaJis.size()) == 4,
                   "Kana conversion length");
    okay &= expect(kanaJis == std::array<UCHAR, 4>{SO, 0x26, SI, 0x00},
                   "Kana SO/SI bytes");
    context = {};
    std::array<UCHAR, 2> kanaRoundtrip{};
    okay &= expect(JIS_to_ShiftJIS(&context, kanaJis.data(), kanaJis.size(),
                                   kanaRoundtrip.data(), kanaRoundtrip.size()) == 2,
                   "Kana reverse length");
    okay &= expect(kanaRoundtrip == kanaInput, "Kana roundtrip");

    std::array<UCHAR, 4> mixedInput{'A', 0x82, 0xa0, 0x00};
    const int mixedRequired = ShiftJIS_to_JIS(
        mixedInput.data(), -1, nullptr, 0);
    std::array<UCHAR, 16> mixedJis{};
    const int mixedWritten = ShiftJIS_to_JIS(
        mixedInput.data(), -1, mixedJis.data(), mixedJis.size());
    okay &= expect(mixedWritten == mixedRequired,
                   "mixed size-only/write agreement");

    UCHAR* allocatedJis = nullptr;
    const int allocatedJisLength = OurShiftJIS_to_JIS(
        mixedInput.data(), -1, &allocatedJis, 0);
    okay &= expect(allocatedJisLength == mixedRequired && allocatedJis,
                   "autoallocated JIS wrapper");
    okay &= expect(bytesEqual(allocatedJis,
                              std::array<UCHAR, 10>{
                                  'A', ESC, KANJI_IN_1ST_CHAR,
                                  KANJI_IN_2ND_CHAR1, 0x24, 0x22,
                                  ESC, KANJI_OUT_1ST_CHAR,
                                  KANJI_OUT_2ND_CHAR2, 0x00}),
                   "autoallocated JIS bytes");

    UCHAR* allocatedShiftJis = nullptr;
    const int allocatedShiftJisLength = OurJIS_to_ShiftJIS(
        allocatedJis, allocatedJisLength, &allocatedShiftJis, 0);
    okay &= expect(allocatedShiftJisLength == static_cast<int>(mixedInput.size())
                       && allocatedShiftJis,
                   "autoallocated Shift-JIS wrapper");
    okay &= expect(std::memcmp(allocatedShiftJis, mixedInput.data(),
                               mixedInput.size()) == 0,
                   "autoallocated wrapper roundtrip");

    delete[] allocatedJis;
    delete[] allocatedShiftJis;

    return okay ? 0 : 1;
}
