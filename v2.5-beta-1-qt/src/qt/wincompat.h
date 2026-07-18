// Qt/Linux representation of the fixed-width Win32 data types used by the
// v2.5-beta-1-modern file formats. This file contains no product behaviour.

#pragma once

#include <QtGlobal>

#include <cstdint>
#include <cstdio>

using BYTE = std::uint8_t;
using UCHAR = std::uint8_t;
using CHAR = char;
using WORD = std::uint16_t;
using USHORT = std::uint16_t;
using SHORT = std::int16_t;
using DWORD = std::uint32_t;
using ULONG = std::uint32_t;
using LONG = std::int32_t;
using UINT = unsigned int;
using INT = int;
using BOOL = int;
using COLORREF = std::uint32_t;

constexpr BOOL FALSE = 0;
constexpr BOOL TRUE = 1;

// Qt replacement for the Win32 process ANSI-code-page query.
UINT GetACP();

// ANSI LOGFONT/CHARFORMAT records used by the original font code. LOGFONT is
// kept as a compatibility value type so format.cpp and txtfntdg.cpp can retain
// their original conversion functions; Qt fonts are only the drawing/UI
// boundary.
constexpr int LF_FACESIZE = 32;

struct LOGFONT {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    CHAR lfFaceName[LF_FACESIZE];
};

static_assert(sizeof(LOGFONT) == 60);

constexpr DWORD CFM_BOLD = 0x00000001U;
constexpr DWORD CFM_ITALIC = 0x00000002U;
constexpr DWORD CFM_UNDERLINE = 0x00000004U;
constexpr DWORD CFM_STRIKEOUT = 0x00000008U;
constexpr DWORD CFM_PROTECTED = 0x00000010U;
constexpr DWORD CFM_LINK = 0x00000020U;
constexpr DWORD CFM_CHARSET = 0x08000000U;
constexpr DWORD CFM_OFFSET = 0x10000000U;
constexpr DWORD CFM_FACE = 0x20000000U;
constexpr DWORD CFM_COLOR = 0x40000000U;
constexpr DWORD CFM_SIZE = 0x80000000U;

constexpr DWORD CFE_BOLD = 0x00000001U;
constexpr DWORD CFE_ITALIC = 0x00000002U;
constexpr DWORD CFE_UNDERLINE = 0x00000004U;
constexpr DWORD CFE_STRIKEOUT = 0x00000008U;
constexpr DWORD CFE_PROTECTED = 0x00000010U;
constexpr DWORD CFE_LINK = 0x00000020U;
constexpr DWORD CFE_AUTOCOLOR = CFM_COLOR;

struct CHARFORMAT {
    UINT cbSize;
    DWORD dwMask;
    DWORD dwEffects;
    LONG yHeight;
    LONG yOffset;
    COLORREF crTextColor;
    BYTE bCharSet;
    BYTE bPitchAndFamily;
    CHAR szFaceName[LF_FACESIZE];
};

static_assert(sizeof(CHARFORMAT) == 60);

constexpr COLORREF RGB(BYTE red, BYTE green, BYTE blue)
{
    return static_cast<COLORREF>(red)
        | (static_cast<COLORREF>(green) << 8U)
        | (static_cast<COLORREF>(blue) << 16U);
}

constexpr BYTE GetRValue(COLORREF color) { return static_cast<BYTE>(color & 0xffU); }
constexpr BYTE GetGValue(COLORREF color) { return static_cast<BYTE>((color >> 8U) & 0xffU); }
constexpr BYTE GetBValue(COLORREF color) { return static_cast<BYTE>((color >> 16U) & 0xffU); }
constexpr WORD LOWORD(DWORD value) { return static_cast<WORD>(value & 0xffffU); }
constexpr WORD HIWORD(DWORD value) { return static_cast<WORD>((value >> 16U) & 0xffffU); }
constexpr BYTE LOBYTE(WORD value) { return static_cast<BYTE>(value & 0xffU); }
constexpr BYTE HIBYTE(WORD value) { return static_cast<BYTE>((value >> 8U) & 0xffU); }
constexpr DWORD MAKELONG(WORD low, WORD high)
{
    return static_cast<DWORD>(low) | (static_cast<DWORD>(high) << 16U);
}

constexpr DWORD BI_RGB = 0;
constexpr DWORD BI_RLE8 = 1;
constexpr DWORD BI_RLE4 = 2;

// Raster operations used by the original CDIB::Draw call sites.
constexpr DWORD SRCCOPY = 0x00CC0020U;
constexpr DWORD SRCAND = 0x008800C6U;
constexpr DWORD MERGEPAINT = 0x00BB0226U;

struct POINT {
    LONG x;
    LONG y;
};

struct SIZE {
    LONG cx;
    LONG cy;
};

struct RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
};

#pragma pack(push, 1)
struct BITMAPFILEHEADER {
    WORD bfType;
    DWORD bfSize;
    WORD bfReserved1;
    WORD bfReserved2;
    DWORD bfOffBits;
};

struct BITMAPCOREHEADER {
    DWORD bcSize;
    WORD bcWidth;
    WORD bcHeight;
    WORD bcPlanes;
    WORD bcBitCount;
};

struct BITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
};

struct RGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
};

struct RGBTRIPLE {
    BYTE rgbtBlue;
    BYTE rgbtGreen;
    BYTE rgbtRed;
};
#pragma pack(pop)

static_assert(sizeof(BITMAPFILEHEADER) == 14);
static_assert(sizeof(BITMAPCOREHEADER) == 12);
static_assert(sizeof(BITMAPINFOHEADER) == 40);
static_assert(sizeof(RGBQUAD) == 4);
