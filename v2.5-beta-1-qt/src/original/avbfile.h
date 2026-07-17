// Ported from v2.5-beta-1-modern/avbfile.h.
// QFile/QImage/zlib replace FILE/GDI DLL boundaries; packed records, values,
// offsets and read order remain the original AVB/BGB format.

#pragma once

#include "dib.h"

#include <QFile>
#include <QString>
#include <QVector>

#include <cstdint>

using AVBINT8 = UCHAR;
using AVBINT16 = USHORT;
using AVBINT32 = ULONG;

constexpr AVBINT16 AF_MAGICNUM = 0x81;
constexpr AVBINT16 AF_MAGICNUM_NEW = 0x8181;
constexpr AVBINT16 AT_SIMPLE = 1;
constexpr AVBINT16 AT_COMPLEX = 2;
constexpr AVBINT16 AT_BACKDROP = 3;
constexpr AVBINT16 AVATAR_CURRENT_VERSION = 2;
constexpr USHORT INVALID_POSE_ID = 0;
constexpr long AVSTREAM_ERROR = -1L;
constexpr ULONG MAX_COMPRESSBUFFERSIZE = 2048UL * 1024UL;
constexpr AVBINT16 MAX_PALETTE_SIZE = 2048;

enum AVATARIMAGEFORMAT {
    AIF_DIB = 0,
    AIF_LZDEFLATE = 1,
};

enum AVATARIMAGEPALETTE {
    AIP_NOPALETTE = 0,
    AIP_GLOBALPALETTE = 1,
    AIP_LOCALPALETTE = 2,
    AIP_MONOCHROME = 3,
    AIP_MASKEDMONO = 4,
    AIP_DUALMASK = 5,
};

#pragma pack(push, 1)
struct AVATARHEADER {
    AVBINT16 nMagicNum;
    AVBINT16 nType;
    AVBINT16 nVersion;
};

struct AVATARICONDATA {
    AVBINT32 dwOffset;
    AVBINT8 byFormat;
    AVBINT8 byPalette;
};

union AVATARBODYDATA {
    struct {
        AVBINT32 dwImageOffset;
        AVBINT32 dwMaskOffset;
        AVBINT32 dwAuraOffset;
        AVBINT16 nEmotion;
        BYTE byIntensity;
        AVBINT16 x;
        AVBINT16 y;
        BYTE byPadding[16];
    } olddata;
    struct {
        AVBINT32 dwImageOffset;
        AVBINT32 dwMaskOffset;
        AVBINT32 dwAuraOffset;
        AVBINT16 nEmotion;
        BYTE byIntensity;
        AVBINT16 x;
        AVBINT16 y;
        BYTE byImageFormat;
        BYTE byMaskFormat;
        BYTE byAuraFormat;
        BYTE byImagePaletteType;
        BYTE byMaskPaletteType;
        BYTE byAuraPaletteType;
    } newdata;
};

union AVATARFACEDATA {
    struct {
        AVBINT32 dwImageOffset;
        AVBINT32 dwMaskOffset;
        AVBINT32 dwAuraOffset;
        AVBINT16 nEmotion;
        BYTE byIntensity;
        AVBINT16 cx;
        AVBINT16 cy;
        AVBINT16 cxDelta;
        AVBINT16 cyDelta;
        AVBINT16 x;
        AVBINT16 y;
        BYTE byPadding[16];
    } olddata;
    struct {
        AVBINT32 dwImageOffset;
        AVBINT32 dwMaskOffset;
        AVBINT32 dwAuraOffset;
        AVBINT16 nEmotion;
        BYTE byIntensity;
        AVBINT16 cx;
        AVBINT16 cy;
        AVBINT16 cxDelta;
        AVBINT16 cyDelta;
        AVBINT16 x;
        AVBINT16 y;
        BYTE byImageFormat;
        BYTE byMaskFormat;
        BYTE byAuraFormat;
        BYTE byImagePaletteType;
        BYTE byMaskPaletteType;
        BYTE byAuraPaletteType;
    } newdata;
};

union AVATARTORSODATA {
    struct {
        AVBINT32 dwImageOffset;
        AVBINT32 dwMaskOffset;
        AVBINT32 dwAuraOffset;
        AVBINT16 nEmotion;
        BYTE byIntensity;
        AVBINT16 cx;
        AVBINT16 cy;
        BYTE byPadding[16];
    } olddata;
    struct {
        AVBINT32 dwImageOffset;
        AVBINT32 dwMaskOffset;
        AVBINT32 dwAuraOffset;
        AVBINT16 nEmotion;
        BYTE byIntensity;
        AVBINT16 cx;
        AVBINT16 cy;
        BYTE byImageFormat;
        BYTE byMaskFormat;
        BYTE byAuraFormat;
        BYTE byImagePaletteType;
        BYTE byMaskPaletteType;
        BYTE byAuraPaletteType;
    } newdata;
};
#pragma pack(pop)

static_assert(sizeof(AVATARHEADER) == 6);
static_assert(sizeof(AVATARICONDATA) == 6);
static_assert(sizeof(AVATARBODYDATA) == 35);
static_assert(sizeof(AVATARFACEDATA) == 43);
static_assert(sizeof(AVATARTORSODATA) == 35);

enum AVATARRECORDTYPE {
    AK_NAME = 1,
    AK_FLAGS = 2,
    AK_ICON = 3,
    AK_NFACES = 4,
    AK_NTORSOS = 5,
    AK_STARTDATA = 6,
    AK_ENDDATA = 7,
    AK_STYLE = 8,
    AK_NBODIES = 9,
    AK_NFACES2 = 10,
    AK_NTORSOS2 = 11,
    AK_NBODIES2 = 12,
    AK_ICON_NEW = 256,
    AK_COLORPALETTE = 257,
    AK_BACKDROP = 258,
    AK_COPYRIGHT = 259,
    AK_ORIGINAL_URL = 260,
    AK_OVERRIDE_URL = 261,
    AK_USAGE_FLAGS = 262,
    AK_OFFSET_ADJUSTMENT = 263,
};

class CAvatarStream {
public:
    virtual ~CAvatarStream() = default;

    BOOL Read32(AVBINT32* value) { return Read(value, sizeof(*value)) == sizeof(*value); }
    BOOL Read16(AVBINT16* value) { return Read(value, sizeof(*value)) == sizeof(*value); }
    BOOL Read8(AVBINT8* value) { return Read(value, sizeof(*value)) == sizeof(*value); }
    BOOL ReadString(QByteArray& value, UINT maximumBytes);
    BOOL AllocAndReadCompressedBuffer(QByteArray& data);

    virtual BOOL Open() = 0;
    virtual BOOL Close() = 0;
    virtual UINT Read(void* data, UINT byteCount) = 0;
    virtual long GetPosition() = 0;
    virtual BOOL SetPosition(long position, int origin) = 0;
};

class CAvatarFileStream final : public CAvatarStream {
public:
    explicit CAvatarFileStream(const QString& fileName, BOOL write = FALSE);
    explicit CAvatarFileStream(const char* fileName, BOOL write = FALSE);
    ~CAvatarFileStream() override;

    BOOL Open() override;
    BOOL Close() override;
    UINT Read(void* data, UINT byteCount) override;
    long GetPosition() override;
    BOOL SetPosition(long position, int origin) override;
    const QString& FileName() const { return m_fileName; }

private:
    QString m_fileName;
    UINT m_openCount = 0;
    QFile m_file;
};

class CAvatarPalette {
public:
    BOOL SetFrom(const QVector<QRgb>& colors);
    BOOL Read(CAvatarStream* stream);

    QVector<QRgb> m_colors;
};

class CAvatarDIB : public CDIB {
public:
    BOOL Load(CAvatarStream* stream);
};

struct AVATARIMAGE {
    DWORD m_dwStreamOffset = 0;
    BYTE m_byFormat = 0;
    BYTE m_byPaletteType = 0;
    CAvatarPalette* m_pGlobalPalette = nullptr;
    CAvatarDIB* m_pDib = nullptr;
};

class CAvatarFileImage {
public:
    explicit CAvatarFileImage(AVATARIMAGE* image) : m_pImage(image) {}
    virtual ~CAvatarFileImage() = default;
    virtual BOOL Read(CAvatarStream* stream) = 0;

protected:
    BOOL SetProperPosition(CAvatarStream* stream);
    BOOL GetProperPalette(CAvatarStream* stream, CAvatarPalette* palette);
    AVATARIMAGE* m_pImage;
};

class CAvatarFileDIBImage final : public CAvatarFileImage {
public:
    using CAvatarFileImage::CAvatarFileImage;
    BOOL Read(CAvatarStream* stream) override;
};

class CAvatarFileZlibImage final : public CAvatarFileImage {
public:
    using CAvatarFileImage::CAvatarFileImage;
    BOOL Read(CAvatarStream* stream) override;
};
