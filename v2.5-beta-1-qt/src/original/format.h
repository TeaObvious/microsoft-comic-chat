// Ported from the format.h selected by v2.5-beta-1-modern/chat.mak
// (../artifacts/inc/format.h). The DWORD layout and public function names are
// unchanged; CDWordArray is the Qt container boundary for MFC's array class.

#pragma once

#include "wincompat.h"

#include <QSize>
#include <QString>
#include <QVector>

class QFont;
class QPainter;
class QTextEdit;

constexpr char chCtlBold = 0x02;
constexpr char chCtlColor = 0x03;
constexpr char chCtlLink = 0x0c;
constexpr char chCtlFixedPitchFont = 0x11;
constexpr char chCtlSymbol = 0x12;
constexpr char chCtlItalic = 0x16;
constexpr char chCtlUnderline = 0x1f;

constexpr WORD wBold = 0x0100;
constexpr WORD wItalic = 0x0200;
constexpr WORD wUnderline = 0x0400;
constexpr WORD wFixedPitch = 0x0800;
constexpr WORD wSymbol = 0x1000;
constexpr WORD wForeground = 0x2000;
constexpr WORD wBackground = 0x4000;
constexpr WORD wLink = 0x8000;

inline constexpr const char* FIXEDPITCHFACENAMES[] = {
    "Courier", "Courier New", "LinePrinter", "Terminal", "Fixedsys"
};
inline constexpr const char* SYMBOLFACENAMES[] = {
    "Symbol", "Wingdings", "HM Phonetic", "Marlett"
};
constexpr short FIXEDPITCHNUMBER = 5;
constexpr short SYMBOLNUMBER = 4;
constexpr short MAX_URL_INTEXT = 16;

class CDWordArray {
public:
    int GetSize() const { return m_values.size(); }
    int GetUpperBound() const { return m_values.size() - 1; }
    DWORD GetAt(int index) const { return m_values.at(index); }
    void SetAt(int index, DWORD value) { m_values[index] = value; }
    void Add(DWORD value) { m_values.append(value); }
    void InsertAt(int index, DWORD value) { m_values.insert(index, value); }
    void RemoveAt(int index) { m_values.removeAt(index); }
    void RemoveAll() { m_values.clear(); }
    DWORD* GetData() { return m_values.data(); }
    const DWORD* GetData() const { return m_values.constData(); }

private:
    QVector<DWORD> m_values;
};

struct STRFMT {
    CHAR* szString = nullptr;
    CDWordArray* prgdwFormatting = nullptr;
};

extern COLORREF linkColor;

BYTE GetColorCode(COLORREF color);
COLORREF GetRBGColor(BYTE code);
short nResettingSequence(const char* input, char* resetSequence);
short nFillFormatting(char* formatting, WORD currentFormat, WORD nextFormat,
                      char firstFormattedChar);
const char* SzSkipOneFormat(const char* input, WORD* format);
char* SzControlLess(char* input, CDWordArray* formatting);
char* SzControlFull(const char* input, CDWordArray* formatting);
char* SzReplaceFormattedString(const char* controlLessReplaceWhat,
                               const char* controlLessReplaceBy,
                               const char* controlLessReplaceIn,
                               CDWordArray* replaceByFormatting,
                               CDWordArray* replaceInFormatting,
                               UINT flags);
QSize GetFormattedTextExtent(QPainter* painter, const char* input, DWORD byteLength,
                             CDWordArray* formatting);
CDWordArray* AddFormat(CDWordArray* formatting, DWORD element);
CDWordArray* InsertFormat(CDWordArray* formatting, BOOL addFormat, WORD format,
                         WORD offset);
CDWordArray* CopyFormatting(CDWordArray* formatting);
CDWordArray* CopyLinksFormatting(CDWordArray* formatting);
void FreeAndNullFormatting(CDWordArray** formatting);
BOOL bFormattingsEqual(CDWordArray* first, CDWordArray* second);
CDWordArray* CutFormattingArray(CDWordArray* formatting, SHORT newStringLength);
CDWordArray* PullFormattingOffsets(CDWordArray* formatting, SHORT deltaOffset);
void PushFormattingOffsets(CDWordArray* formatting, SHORT deltaOffset);
void PushFormattingOffsetsDW(DWORD* formatting, INT count, SHORT deltaOffset);
CDWordArray* IdentifyURLs(CDWordArray* formatting, const char* message);
CDWordArray* MarkHotLinks(CDWordArray* formatting, char* message, char identifier);
BOOL bURLPresent(CDWordArray* formatting);
BOOL bSizorPresent(CDWordArray* formatting);
BOOL FLaunchBrowser(const char* url);
short FFixedPitchFont(const QString& faceName);
short FSymbolFont(const QString& faceName);
short nGetSpecialFontIndex(BOOL fixedPitchFont);
BOOL bLOGFONTToCHARFORMAT(LOGFONT* logFont, COLORREF color, DWORD mask,
                          CHARFORMAT* charFormat);
CDWordArray* PRGDWGetFormatting(const QTextEdit* richEdit,
                                const QFont* defaultFont,
                                COLORREF defaultColor);
