// Ported from v2.5-beta-1-modern/intl.h and the active part of intl.c.
// QPainter replaces HDC; the code-page tables and byte algorithms remain in
// the original module.

#pragma once

#include "wincompat.h"

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QStringView>

class CDWordArray;
class QPainter;

inline constexpr int ICHRCNV_NONE = 0;
inline constexpr int ICHRCNV_AUTO = 1;
inline constexpr int ICHRCNV_JIS = 2;
inline constexpr int ICHRCNV_EUCJP = 3;
inline constexpr int ICHRCNV_SJIS = 4;
inline constexpr int CP_AUTODETECT = -1;
inline constexpr DWORD ELEFLAG_NOBREAK = 0x00000100;

struct SCRIPTINFO {
    int iLangID = 0;
    int iEncodeID = 0;
    int iCp = 1252;
    int iInetCp = 1252;
    int iChrCnv = ICHRCNV_NONE;
    BOOL (*IsWrapUp)(BOOL dbcs, const char* text) = nullptr;
    BOOL (*IsWrapDown)(BOOL dbcs, const char* text) = nullptr;
};

extern "C" {
BOOL IsDBCSLeadByteEx(int codePage, BYTE character);
BOOL IsStringBreakable(SCRIPTINFO* mime, const char* position,
                       const char* start, int length);
char* FindBreakableCharForFE(SCRIPTINFO* mime, char* start, int length,
                             BOOL* wrapupAtTop);
char* CharNextEx(int codePage, const char* position);
BOOL IsTrailByte(int codePage, const char* start, const char* position);
char* CharPrevEx(int codePage, const char* start, const char* position);
int AdjustPunctuation(SCRIPTINFO* mime, const char* start,
                      const char* position, int* breakOffset, int length);

BOOL IsWrapSbcs(BOOL dbcs, const char* text);
BOOL IsWrapDown932(BOOL dbcs, const char* text);
BOOL IsWrapUp932(BOOL dbcs, const char* text);
BOOL IsWrapDown949(BOOL dbcs, const char* text);
BOOL IsWrapUp949(BOOL dbcs, const char* text);
BOOL IsWrapDown950(BOOL dbcs, const char* text);
BOOL IsWrapUp950(BOOL dbcs, const char* text);
BOOL IsWrapDown936(BOOL dbcs, const char* text);
BOOL IsWrapUp936(BOOL dbcs, const char* text);

BOOL FindSubStringForINTLThatFits(
    void* mime, QPainter* painter, const char* string, int byteLength,
    CDWordArray* formatting, int* bytesFit, BOOL* hasBlankOrAlike,
    QSize* size, int maximumExtent);

void* GetMime();
void SetMime(DWORD characterSet);
int iBytesofChar(BYTE character);
BYTE GetCorrectCharSet();
}

QString IntlTextToQString(const char* bytes, int length = -1);
QByteArray IntlTextFromQString(QStringView text);
QByteArray IntlTextFromUtf8(const char* bytes, int length = -1);
CDWordArray* IntlFormattingFromUtf8(const char* utf8,
                                    const CDWordArray* formatting);

