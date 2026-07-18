// Ported from the build-selected v2.5-beta-1-modern ccommon.cpp and
// artifacts/inc/ccommon.h.

#pragma once

#include "wincompat.h"

#include <QByteArray>
#include <QString>
#include <QStringView>

inline constexpr SHORT g_nMaxLengthSmall = 31;
inline constexpr SHORT g_nMaxLength = 255;
inline constexpr SHORT g_nMaxChanBuff = 200;

inline constexpr char g_szAnon[] = "ANON";
inline constexpr char g_szMSN[] = "MSN";
inline constexpr char g_szDPA[] = "DPA";
inline constexpr char g_szNTLM[] = "NTLM";
inline constexpr char g_szNoMachine[] = "NoMachine";
inline constexpr char g_szEmpty[] = "";
inline constexpr char g_szLF[] = "\n";
inline constexpr char g_szCR[] = "\r";
inline constexpr char g_szCRLF[] = "\r\n";
inline constexpr char g_szSpace[] = " ";

inline constexpr char g_chEOS = '\0';
inline constexpr char g_chComma = ',';
inline constexpr char g_chDash = '-';
inline constexpr char g_chSpace = ' ';
inline constexpr char g_chBell = '\a';
inline constexpr char g_chTab = '\t';
inline constexpr char g_chBackSlash = '\\';
inline constexpr char g_chLF = '\n';
inline constexpr char g_chCR = '\r';
inline constexpr char g_chExtNckPfx = '\'';
inline constexpr char g_chExtChnPfx = '%';
inline constexpr char g_chGblChnPfx = '#';
inline constexpr char g_chLclChnPfx = '&';
inline constexpr char g_chLLQuoteIRCX = '\\';
inline constexpr char g_chAtSign = '@';
inline constexpr char g_chLLQuoteCTCP = 0x10;
inline constexpr char g_chTransparent = 0x01;

// Values returned by the Win32 font charset API used by the original build.
inline constexpr BYTE ANSI_CHARSET = 0;
inline constexpr BYTE DEFAULT_CHARSET = 1;
inline constexpr BYTE SYMBOL_CHARSET = 2;
inline constexpr BYTE SHIFTJIS_CHARSET = 128;
inline constexpr BYTE HANGEUL_CHARSET = 129;
inline constexpr BYTE GB2312_CHARSET = 134;
inline constexpr BYTE CHINESEBIG5_CHARSET = 136;
inline constexpr BYTE GREEK_CHARSET = 161;
inline constexpr BYTE TURKISH_CHARSET = 162;
inline constexpr BYTE HEBREW_CHARSET = 177;
inline constexpr BYTE ARABIC_CHARSET = 178;
inline constexpr BYTE BALTIC_CHARSET = 186;
inline constexpr BYTE RUSSIAN_CHARSET = 204;
inline constexpr BYTE THAI_CHARSET = 222;
inline constexpr BYTE EASTEUROPE_CHARSET = 238;

BOOL bExtendedString(const QByteArray& input);
BOOL bExtendedWideString(QStringView input);
BOOL bExtendedChannelName(const QByteArray& channelName,
                          BOOL acceptUpperAnsi = FALSE);
BOOL bExtendedWideChannelName(QStringView channelName);
BOOL bExtendedNickname(const QByteArray& nickname);
BOOL bExtendedWideNickname(QStringView nickname);

BOOL bConvertWideStringToUTF8(QStringView input, INT inputLength,
                              QByteArray* output, INT* outputLength,
                              BOOL nickname = FALSE,
                              BOOL channelName = FALSE,
                              BOOL postProcess = TRUE,
                              BOOL escapeWildcards = FALSE);
BOOL bConvertUTF8StringToWide(const QByteArray& input, INT inputLength,
                              QString* output, INT* outputLength,
                              BOOL nickname = FALSE,
                              BOOL channelName = FALSE,
                              BOOL postProcess = TRUE);
const CHAR* SzNextUTF8Char(const CHAR* input);

BOOL bSB2DBKatakana(const QByteArray& input, QByteArray* output,
                    BOOL* changed);
BOOL bConvertString(BOOL incomingText, BYTE characterSet,
                    const QByteArray& input, QByteArray* output,
                    BOOL* changed);
BOOL bWideToCharacterSet(QStringView input, BYTE characterSet,
                         QByteArray* output);
BOOL bCharacterSetToWide(const QByteArray& input, BYTE characterSet,
                         QString* output);
BOOL bWideToCodePage(QStringView input, UINT codePage, QByteArray* output);
BOOL bCodePageToWide(const QByteArray& input, UINT codePage, QString* output);

BOOL bDataToString(const QByteArray& source, QByteArray* destination,
                   BOOL afterColon);
BOOL bStringToData(const QByteArray& source, QByteArray* destination);
BOOL bLowLevelQuoting(CHAR quotingChar, BOOL treatAsByteArray,
                      const QByteArray& source, QByteArray* destination,
                      BOOL* freeDestination,
                      BOOL removeCarriageReturns = FALSE);
BOOL bLowLevelUnquoting(CHAR quotingChar, BOOL treatAsByteArray,
                        const QByteArray& source, QByteArray* destination);
