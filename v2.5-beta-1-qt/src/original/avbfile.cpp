// Ported from v2.5-beta-1-modern/avbfile.cpp.
// QFile and QImage replace the CRT/Win32 drawing boundary. Record parsing,
// offset handling, palette selection and compressed-buffer validation follow
// the original functions of the same names.

#include "avbfile.h"

#include "avatar.h"
#include "avatario.h"
#include "backdrop.h"

#include <QImageReader>

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace {
const QVector<QRgb> MonochromePalette = {qRgb(255, 255, 255), qRgb(0, 0, 0)};
const QVector<QRgb> MaskedMonoPalette = {
    qRgb(255, 255, 255), qRgb(0, 0, 0), qRgb(128, 0, 0), qRgb(0, 0, 128)};

bool readExact(CAvatarStream* stream, void* destination, UINT byteCount)
{
    return stream && stream->Read(destination, byteCount) == byteCount;
}
}

BOOL CAvatarStream::ReadString(QByteArray& value, UINT maximumBytes)
{
    value.clear();
    if (maximumBytes == 0) {
        return FALSE;
    }
    for (UINT remaining = maximumBytes; remaining > 1; --remaining) {
        char character = '\0';
        if (Read(&character, sizeof(character)) != sizeof(character)) {
            value.clear();
            return FALSE;
        }
        if (character == '\0') {
            return TRUE;
        }
        value.append(character);
    }

    // The original reader always leaves a terminating zero in its caller's
    // buffer. A non-terminated on-disk string is accepted only up to that same
    // limit; no extra stream byte is consumed.
    return TRUE;
}

BOOL CAvatarStream::AllocAndReadCompressedBuffer(QByteArray& data)
{
    struct Sizes {
        DWORD dwUncompressedSize;
        DWORD dwCompressedSize;
    } sizes{};

    data.clear();
    if (!readExact(this, &sizes, sizeof(sizes))) {
        return FALSE;
    }
    if (sizes.dwUncompressedSize == 0) {
        return TRUE;
    }
    if (sizes.dwUncompressedSize > MAX_COMPRESSBUFFERSIZE
        || sizes.dwCompressedSize > MAX_COMPRESSBUFFERSIZE) {
        return FALSE;
    }

    QByteArray compressed(static_cast<qsizetype>(sizes.dwCompressedSize), Qt::Uninitialized);
    if (!readExact(this, compressed.data(), sizes.dwCompressedSize)) {
        return FALSE;
    }

    data.resize(static_cast<qsizetype>(sizes.dwUncompressedSize));
    uLongf destinationSize = sizes.dwUncompressedSize;
    const int result = ::uncompress(reinterpret_cast<Bytef*>(data.data()), &destinationSize,
                                    reinterpret_cast<const Bytef*>(compressed.constData()),
                                    sizes.dwCompressedSize);
    if (result != Z_OK) {
        data.clear();
        return FALSE;
    }
    data.resize(static_cast<qsizetype>(destinationSize));
    return TRUE;
}

CAvatarFileStream::CAvatarFileStream(const QString& fileName, BOOL write)
    : m_fileName(fileName)
    , m_file(fileName)
{
    Q_ASSERT(!write);
}

CAvatarFileStream::CAvatarFileStream(const char* fileName, BOOL write)
    : CAvatarFileStream(QString::fromLocal8Bit(fileName ? fileName : ""), write)
{
}

CAvatarFileStream::~CAvatarFileStream()
{
    if (m_openCount > 0) {
        m_openCount = 1;
        Close();
    }
}

BOOL CAvatarFileStream::Open()
{
    if (m_openCount > 0) {
        ++m_openCount;
        return TRUE;
    }
    if (!m_file.open(QIODevice::ReadOnly)) {
        return FALSE;
    }
    m_openCount = 1;
    return TRUE;
}

BOOL CAvatarFileStream::Close()
{
    if (m_openCount == 0) {
        return FALSE;
    }
    if (m_openCount > 1) {
        --m_openCount;
        return TRUE;
    }
    m_file.close();
    m_openCount = 0;
    return TRUE;
}

UINT CAvatarFileStream::Read(void* data, UINT byteCount)
{
    if (!m_file.isOpen()) {
        return std::numeric_limits<UINT>::max();
    }
    const qint64 count = m_file.read(static_cast<char*>(data), byteCount);
    return count < 0 ? std::numeric_limits<UINT>::max() : static_cast<UINT>(count);
}

long CAvatarFileStream::GetPosition()
{
    if (!m_file.isOpen()) {
        return AVSTREAM_ERROR;
    }
    const qint64 position = m_file.pos();
    return position > std::numeric_limits<long>::max() ? AVSTREAM_ERROR
                                                        : static_cast<long>(position);
}

BOOL CAvatarFileStream::SetPosition(long position, int origin)
{
    if (!m_file.isOpen()) {
        return FALSE;
    }
    qint64 target = position;
    if (origin == SEEK_CUR) {
        target += m_file.pos();
    } else if (origin == SEEK_END) {
        target += m_file.size();
    } else if (origin != SEEK_SET) {
        return FALSE;
    }
    return target >= 0 && m_file.seek(target);
}

BOOL CAvatarPalette::SetFrom(const QVector<QRgb>& colors)
{
    m_colors = colors;
    return TRUE;
}

BOOL CAvatarPalette::Read(CAvatarStream* stream)
{
    if (!m_colors.isEmpty()) {
        return FALSE;
    }
    AVBINT16 entries = 0;
    if (!stream->Read16(&entries) || entries > MAX_PALETTE_SIZE) {
        return FALSE;
    }
    m_colors.reserve(entries);
    for (AVBINT16 index = 0; index < entries; ++index) {
        BYTE triplet[3]{};
        if (!readExact(stream, triplet, sizeof(triplet))) {
            m_colors.clear();
            return FALSE;
        }
        // COLORREF is laid out R,G,B in the original little-endian read.
        m_colors.append(qRgb(triplet[0], triplet[1], triplet[2]));
    }
    return TRUE;
}

BOOL CAvatarDIB::Load(CAvatarStream* stream)
{
    const long fileStart = stream ? stream->GetPosition() : AVSTREAM_ERROR;
    if (fileStart == AVSTREAM_ERROR) {
        return FALSE;
    }

    BITMAPFILEHEADER fileHeader{};
    if (!readExact(stream, &fileHeader, sizeof(fileHeader)) || fileHeader.bfType != 0x4d42
        || fileHeader.bfSize < fileHeader.bfOffBits
        || fileHeader.bfOffBits < sizeof(BITMAPFILEHEADER) + sizeof(DWORD)) {
        return FALSE;
    }

    BITMAPINFOHEADER header{};
    if (!readExact(stream, &header, sizeof(header))) {
        return FALSE;
    }

    bool isPresentationManager = false;
    if (header.biSize != sizeof(BITMAPINFOHEADER)) {
        if (header.biSize != sizeof(BITMAPCOREHEADER)
            || !stream->SetPosition(fileStart + sizeof(BITMAPFILEHEADER), SEEK_SET)) {
            return FALSE;
        }
        BITMAPCOREHEADER core{};
        if (!readExact(stream, &core, sizeof(core))) {
            return FALSE;
        }
        isPresentationManager = true;
        header = {};
        header.biSize = sizeof(BITMAPINFOHEADER);
        header.biWidth = core.bcWidth;
        header.biHeight = core.bcHeight;
        header.biPlanes = core.bcPlanes;
        header.biBitCount = core.bcBitCount;
        header.biCompression = BI_RLE4;
    }

    if (header.biWidth <= 0 || header.biHeight == 0 || header.biBitCount == 0) {
        return FALSE;
    }

    const int colorCount = NumDIBColorEntries(header);
    QVector<QRgb> colors;
    colors.reserve(colorCount);
    for (int index = 0; index < colorCount; ++index) {
        if (isPresentationManager) {
            RGBTRIPLE triple{};
            if (!readExact(stream, &triple, sizeof(triple))) {
                return FALSE;
            }
            colors.append(qRgb(triple.rgbtRed, triple.rgbtGreen, triple.rgbtBlue));
        } else {
            RGBQUAD quad{};
            if (!readExact(stream, &quad, sizeof(quad))) {
                return FALSE;
            }
            colors.append(qRgb(quad.rgbRed, quad.rgbGreen, quad.rgbBlue));
        }
    }

    const DWORD bitSize = fileHeader.bfSize - fileHeader.bfOffBits;
    if (bitSize > static_cast<DWORD>(std::numeric_limits<int>::max())
        || !stream->SetPosition(fileStart + static_cast<long>(fileHeader.bfOffBits), SEEK_SET)) {
        return FALSE;
    }
    QByteArray bits(static_cast<qsizetype>(bitSize), Qt::Uninitialized);
    if (!readExact(stream, bits.data(), bitSize)) {
        return FALSE;
    }

    if (header.biCompression == BI_RGB) {
        return Create(header, colors, bits);
    }

    // Original CDIB::ConvertToNonRLE delegates decoding to GDI. QImage is the
    // platform replacement for that one boundary; the source bytes are read
    // directly from the original stream and are never persisted in another format.
    if (!stream->SetPosition(fileStart, SEEK_SET)) {
        return FALSE;
    }
    QByteArray completeFile(static_cast<qsizetype>(fileHeader.bfSize), Qt::Uninitialized);
    if (!readExact(stream, completeFile.data(), fileHeader.bfSize)) {
        return FALSE;
    }
    const QImage decoded = QImage::fromData(completeFile, "BMP");
    if (decoded.isNull()) {
        return FALSE;
    }
    m_header = header;
    m_header.biCompression = BI_RGB;
    m_header.biBitCount = 32;
    m_colors.clear();
    m_bits.clear();
    m_image = decoded.convertToFormat(QImage::Format_ARGB32);
    return TRUE;
}

BOOL CAvatarFileImage::SetProperPosition(CAvatarStream* stream)
{
    if (!m_pImage || m_pImage->m_dwStreamOffset == std::numeric_limits<DWORD>::max()) {
        return TRUE;
    }
    return stream->SetPosition(static_cast<long>(m_pImage->m_dwStreamOffset), SEEK_SET);
}

BOOL CAvatarFileImage::GetProperPalette(CAvatarStream* stream, CAvatarPalette* palette)
{
    switch (m_pImage->m_byPaletteType) {
    case AIP_NOPALETTE:
        return TRUE;
    case AIP_GLOBALPALETTE:
        return m_pImage->m_pGlobalPalette
            && palette->SetFrom(m_pImage->m_pGlobalPalette->m_colors);
    case AIP_LOCALPALETTE: {
        AVBINT16 record[2]{};
        return readExact(stream, record, sizeof(record)) && record[0] == AK_COLORPALETTE
            && palette->Read(stream);
    }
    case AIP_MONOCHROME:
        return palette->SetFrom(MonochromePalette);
    case AIP_MASKEDMONO:
    case AIP_DUALMASK:
        return palette->SetFrom(MaskedMonoPalette);
    default:
        return FALSE;
    }
}

BOOL CAvatarFileDIBImage::Read(CAvatarStream* stream)
{
    if (!m_pImage || m_pImage->m_pDib || m_pImage->m_byPaletteType != AIP_NOPALETTE
        || !SetProperPosition(stream)) {
        return FALSE;
    }
    auto* dib = new CAvatarDIB;
    if (!dib->Load(stream)) {
        delete dib;
        return FALSE;
    }
    m_pImage->m_pDib = dib;
    return TRUE;
}

BOOL CAvatarFileZlibImage::Read(CAvatarStream* stream)
{
    if (!m_pImage || m_pImage->m_pDib || m_pImage->m_byPaletteType == AIP_NOPALETTE
        || !SetProperPosition(stream)) {
        return FALSE;
    }

    CAvatarPalette palette;
    if (!GetProperPalette(stream, &palette)) {
        return FALSE;
    }

    AVBINT32 headerSize = 0;
    if (!stream->Read32(&headerSize) || headerSize < sizeof(BITMAPINFOHEADER)
        || headerSize > sizeof(BITMAPINFOHEADER) * 6U) {
        return FALSE;
    }

    QByteArray headerBytes(static_cast<qsizetype>(headerSize), '\0');
    std::memcpy(headerBytes.data(), &headerSize, sizeof(headerSize));
    if (!readExact(stream, headerBytes.data() + sizeof(headerSize),
                   headerSize - sizeof(headerSize))) {
        return FALSE;
    }

    BITMAPINFOHEADER header{};
    std::memcpy(&header, headerBytes.constData(), sizeof(header));
    if (header.biWidth <= 0 || header.biHeight == 0 || header.biBitCount == 0
        || header.biCompression != BI_RGB) {
        return FALSE;
    }

    QByteArray bitmapData;
    if (!stream->AllocAndReadCompressedBuffer(bitmapData)) {
        return FALSE;
    }
    const qsizetype expected = static_cast<qsizetype>(DIBStorageWidth(
        static_cast<UINT>(header.biWidth), header.biBitCount)) * qAbs(header.biHeight);
    if (bitmapData.size() != expected) {
        return FALSE;
    }

    auto* dib = new CAvatarDIB;
    if (!dib->Create(header, palette.m_colors, bitmapData)) {
        delete dib;
        return FALSE;
    }
    m_pImage->m_pDib = dib;
    return TRUE;
}

namespace {
void AdjustOffset(DWORD& offset, long adjustment)
{
    if (offset != 0) {
        offset = static_cast<DWORD>(static_cast<long>(offset) + adjustment);
    }
}

BOOL ReadAvFileString(CAvatarStream* stream, QByteArray& destination, int maximumLength)
{
    return maximumLength > 0
        && stream->ReadString(destination, static_cast<UINT>(maximumLength));
}

constexpr int MAX_AVATAR_NAME = 60;
constexpr int MAX_URL = 512;
constexpr int MAX_COPYRIGHT = 256;
}

CAvatarX* CAvatarX::LoadAvatar(CAvatarStream* stream)
{
    long resourceAdjustment = 0;
    if (!stream || !stream->Open()) {
        return nullptr;
    }

    AVATARHEADER header{};
    if (!readExact(stream, &header, sizeof(header))
        || (header.nMagicNum != AF_MAGICNUM && header.nMagicNum != AF_MAGICNUM_NEW)) {
        stream->Close();
        return nullptr;
    }

    CAvatarX* avatar = nullptr;
    switch (header.nType) {
    case AT_COMPLEX:
        avatar = new CAvatarComplex;
        break;
    case AT_SIMPLE:
        avatar = new CAvatarSimple;
        break;
    default:
        stream->Close();
        return nullptr;
    }

    if (HIWORD(header.nVersion) != 0) {
        delete avatar;
        stream->Close();
        return nullptr;
    }

    while (true) {
        AVBINT16 tag = 0;
        AVBINT16 size = 0;
        if (!stream->Read16(&tag)) {
            delete avatar;
            stream->Close();
            return nullptr;
        }
        if (tag >= AK_ICON_NEW && !stream->Read16(&size)) {
            delete avatar;
            stream->Close();
            return nullptr;
        }
        if (tag == AK_STARTDATA) {
            break;
        }
        if (!avatar->HandleLoadTag(stream, tag, size, resourceAdjustment)) {
            delete avatar;
            stream->Close();
            return nullptr;
        }
    }

    stream->Close();
    avatar->m_arrPoses.squeeze();
    return avatar;
}

BOOL CAvatarX::HandleLoadTag(CAvatarStream* stream, AVBINT16 tag, AVBINT16 size,
                             long& resourceAdjustment)
{
    switch (tag) {
    case AK_NAME: {
        QByteArray name;
        if (!ReadAvFileString(stream, name, MAX_AVATAR_NAME)) {
            return FALSE;
        }
        m_name = QString::fromLatin1(name);
        return TRUE;
    }
    case AK_ORIGINAL_URL:
        return ReadAvFileString(stream, m_originalUrl, MAX_URL);
    case AK_OVERRIDE_URL:
        return ReadAvFileString(stream, m_newUrl, MAX_URL);
    case AK_COPYRIGHT:
        return ReadAvFileString(stream, m_copyright, MAX_COPYRIGHT);
    case AK_STYLE: {
        AVBINT16 style = 0;
        if (!stream->Read16(&style)) {
            return FALSE;
        }
        m_style = static_cast<UCHAR>(style);
        return TRUE;
    }
    case AK_FLAGS: {
        AVBINT16 flags = 0;
        if (!stream->Read16(&flags)) {
            return FALSE;
        }
        m_flags = static_cast<UCHAR>(flags);
        return TRUE;
    }
    case AK_ICON:
    case AK_ICON_NEW: {
        AVATARICONDATA iconData{};
        if (tag == AK_ICON) {
            if (!stream->Read32(&iconData.dwOffset)) {
                return FALSE;
            }
            iconData.byFormat = AIF_DIB;
            iconData.byPalette = AIP_NOPALETTE;
        } else if (!readExact(stream, &iconData, sizeof(iconData))) {
            return FALSE;
        }
        AdjustOffset(iconData.dwOffset, resourceAdjustment);
        m_icon = CreatePose(stream, iconData.dwOffset, iconData.byFormat,
                            iconData.byPalette);
        return m_icon != INVALID_POSE_ID;
    }
    case AK_COLORPALETTE:
        return m_palette.Read(stream);
    case AK_OFFSET_ADJUSTMENT: {
        std::int32_t adjustment = 0;
        if (!stream->Read32(reinterpret_cast<AVBINT32*>(&adjustment))) {
            return FALSE;
        }
        resourceAdjustment += adjustment;
        return TRUE;
    }
    default:
        return tag >= AK_ICON_NEW && stream->SetPosition(size, SEEK_CUR);
    }
}

USHORT CAvatarX::CreatePose(CAvatarStream* stream, DWORD offset, BYTE format,
                            BYTE paletteType)
{
    DWORD offsets[3] = {offset, 0, 0};
    BYTE formats[3] = {format, 0, 0};
    BYTE paletteTypes[3] = {paletteType, 0, 0};
    return CreatePoseWithMask(stream, offsets, formats, paletteTypes);
}

USHORT CAvatarX::CreatePoseWithMask(CAvatarStream*, DWORD* offsets, BYTE* formats,
                                    BYTE* paletteTypes)
{
    if (!offsets || !formats || !paletteTypes
        || m_arrPoses.size() >= std::numeric_limits<USHORT>::max()) {
        return INVALID_POSE_ID;
    }
    auto* pose = new CPose(offsets, formats, paletteTypes);
    m_arrPoses.append(pose);
    return static_cast<USHORT>(m_arrPoses.size());
}

BOOL CAvatarSimple::HandleLoadTag(CAvatarStream* stream, AVBINT16 tag, AVBINT16 size,
                                  long& resourceAdjustment)
{
    switch (tag) {
    case AK_NBODIES:
    case AK_NBODIES2:
        return LoadBodyRecs(stream, tag == AK_NBODIES, resourceAdjustment);
    default:
        return CAvatarX::HandleLoadTag(stream, tag, size, resourceAdjustment);
    }
}

BOOL CAvatarSimple::LoadBodyRecs(CAvatarStream* stream, BOOL oldTag,
                                 long& resourceAdjustment)
{
    AVBINT16 count = 0;
    if (!stream->Read16(&count)) {
        return FALSE;
    }
    m_bodies.resize(count);
    AVBINT32 previousImageOffset = 0;
    for (AVBINT16 index = 0; index < count; ++index) {
        AVATARBODYDATA data{};
        const UINT recordSize = oldTag ? sizeof(data.olddata) : sizeof(data.newdata);
        if (!readExact(stream, &data, recordSize)) {
            m_bodies.clear();
            return FALSE;
        }
        if (data.newdata.dwImageOffset != previousImageOffset) {
            AdjustOffset(data.newdata.dwImageOffset, resourceAdjustment);
            AdjustOffset(data.newdata.dwMaskOffset, resourceAdjustment);
            AdjustOffset(data.newdata.dwAuraOffset, resourceAdjustment);
            m_bodies[index].poseID = CreatePoseWithMask(
                stream, &data.newdata.dwImageOffset, &data.newdata.byImageFormat,
                &data.newdata.byImagePaletteType);
            if (m_bodies[index].poseID == INVALID_POSE_ID) {
                m_bodies.clear();
                return FALSE;
            }
            previousImageOffset = data.newdata.dwImageOffset;
        } else {
            if (index == 0) {
                m_bodies.clear();
                return FALSE;
            }
            m_bodies[index].poseID = m_bodies[index - 1].poseID;
        }
        m_bodies[index].emotion = EmotionToFloat(data.newdata.nEmotion);
        m_bodies[index].intensity = data.newdata.byIntensity / 255.0f;
        m_bodies[index].faceX = static_cast<UCHAR>(data.newdata.x);
        m_bodies[index].faceY = static_cast<UCHAR>(data.newdata.y);
    }
    return TRUE;
}

BOOL CAvatarComplex::HandleLoadTag(CAvatarStream* stream, AVBINT16 tag, AVBINT16 size,
                                   long& resourceAdjustment)
{
    switch (tag) {
    case AK_NFACES:
    case AK_NFACES2:
        return LoadFaceRecs(stream, tag == AK_NFACES, resourceAdjustment);
    case AK_NTORSOS:
    case AK_NTORSOS2:
        return LoadTorsoRecs(stream, tag == AK_NTORSOS, resourceAdjustment);
    default:
        return CAvatarX::HandleLoadTag(stream, tag, size, resourceAdjustment);
    }
}

BOOL CAvatarComplex::LoadFaceRecs(CAvatarStream* stream, BOOL oldTag,
                                  long& resourceAdjustment)
{
    AVBINT16 count = 0;
    if (!stream->Read16(&count)) {
        return FALSE;
    }
    m_faces.resize(count);
    AVBINT32 previousImageOffset = 0;
    for (AVBINT16 index = 0; index < count; ++index) {
        AVATARFACEDATA data{};
        const UINT recordSize = oldTag ? sizeof(data.olddata) : sizeof(data.newdata);
        if (!readExact(stream, &data, recordSize)) {
            m_faces.clear();
            return FALSE;
        }
        if (data.newdata.dwImageOffset != previousImageOffset) {
            AdjustOffset(data.newdata.dwImageOffset, resourceAdjustment);
            AdjustOffset(data.newdata.dwMaskOffset, resourceAdjustment);
            AdjustOffset(data.newdata.dwAuraOffset, resourceAdjustment);
            m_faces[index].poseID = CreatePoseWithMask(
                stream, &data.newdata.dwImageOffset, &data.newdata.byImageFormat,
                &data.newdata.byImagePaletteType);
            if (m_faces[index].poseID == INVALID_POSE_ID) {
                m_faces.clear();
                return FALSE;
            }
            previousImageOffset = data.newdata.dwImageOffset;
        } else {
            if (index == 0) {
                m_faces.clear();
                return FALSE;
            }
            m_faces[index].poseID = m_faces[index - 1].poseID;
        }
        m_faces[index].emotion = EmotionToFloat(data.newdata.nEmotion);
        m_faces[index].intensity = data.newdata.byIntensity / 255.0f;
        m_faces[index].xCX = static_cast<SHORT>(data.newdata.cx);
        m_faces[index].yCX = static_cast<SHORT>(data.newdata.cy);
        m_faces[index].delta_xCX = static_cast<SHORT>(data.newdata.cxDelta);
        m_faces[index].delta_yCX = static_cast<SHORT>(data.newdata.cyDelta);
        m_faces[index].faceX = static_cast<UCHAR>(data.newdata.x);
        m_faces[index].faceY = static_cast<UCHAR>(data.newdata.y);
    }
    return TRUE;
}

BOOL CAvatarComplex::LoadTorsoRecs(CAvatarStream* stream, BOOL oldTag,
                                   long& resourceAdjustment)
{
    AVBINT16 count = 0;
    if (!stream->Read16(&count)) {
        return FALSE;
    }
    m_torsos.resize(count);
    AVBINT32 previousImageOffset = 0;
    for (AVBINT16 index = 0; index < count; ++index) {
        AVATARTORSODATA data{};
        const UINT recordSize = oldTag ? sizeof(data.olddata) : sizeof(data.newdata);
        if (!readExact(stream, &data, recordSize)) {
            m_torsos.clear();
            return FALSE;
        }
        if (data.newdata.dwImageOffset != previousImageOffset) {
            AdjustOffset(data.newdata.dwImageOffset, resourceAdjustment);
            AdjustOffset(data.newdata.dwMaskOffset, resourceAdjustment);
            AdjustOffset(data.newdata.dwAuraOffset, resourceAdjustment);
            m_torsos[index].poseID = CreatePoseWithMask(
                stream, &data.newdata.dwImageOffset, &data.newdata.byImageFormat,
                &data.newdata.byImagePaletteType);
            if (m_torsos[index].poseID == INVALID_POSE_ID) {
                m_torsos.clear();
                return FALSE;
            }
            previousImageOffset = data.newdata.dwImageOffset;
        } else {
            if (index == 0) {
                m_torsos.clear();
                return FALSE;
            }
            m_torsos[index].poseID = m_torsos[index - 1].poseID;
        }
        m_torsos[index].emotion = EmotionToFloat(data.newdata.nEmotion);
        m_torsos[index].intensity = data.newdata.byIntensity / 255.0f;
        m_torsos[index].xCX = static_cast<SHORT>(data.newdata.cx);
        m_torsos[index].yCX = static_cast<SHORT>(data.newdata.cy);
    }
    return TRUE;
}

CPose::CPose(const DWORD* offsets, const BYTE* formats, const BYTE* paletteTypes)
{
    std::copy_n(offsets, 3, m_dwOffsets);
    std::copy_n(formats, 3, m_byFormats);
    std::copy_n(paletteTypes, 3, m_byPaletteTypes);
    if (m_byPaletteTypes[0] == AIP_MASKEDMONO) {
        m_dwOffsets[1] = m_dwOffsets[2] = 0;
    } else if (m_byPaletteTypes[1] == AIP_DUALMASK && m_dwOffsets[1] != 0) {
        m_dwOffsets[2] = 0;
    }
}

CPose::~CPose()
{
    for (CAvatarDIB*& dib : m_pdibs) {
        delete dib;
        dib = nullptr;
    }
}

BOOL CPose::Load(CAvatarStream* stream, CAvatarPalette* globalPalette)
{
    if (m_pdibs[0]) {
        return TRUE;
    }
    if (!stream || !stream->Open()) {
        return FALSE;
    }

    for (int index = 0; index < 3; ++index) {
        if (m_dwOffsets[index] == 0) {
            continue;
        }
        AVATARIMAGE image;
        image.m_dwStreamOffset = m_dwOffsets[index];
        image.m_byFormat = m_byFormats[index];
        image.m_byPaletteType = m_byPaletteTypes[index];
        image.m_pGlobalPalette = globalPalette;
        image.m_pDib = nullptr;
        BOOL result = FALSE;
        switch (image.m_byFormat) {
        case AIF_DIB:
            result = CAvatarFileDIBImage(&image).Read(stream);
            break;
        case AIF_LZDEFLATE:
            result = CAvatarFileZlibImage(&image).Read(stream);
            break;
        default:
            break;
        }
        if (!result) {
            stream->Close();
            return FALSE;
        }
        m_pdibs[index] = image.m_pDib;
    }
    stream->Close();

    if (m_byPaletteTypes[0] == AIP_MASKEDMONO && m_pdibs[0]) {
        return ConvertFromMaskedMono(m_pdibs[0]);
    }
    if (m_byPaletteTypes[1] == AIP_DUALMASK && m_pdibs[1]) {
        return ConvertFromDualMask(m_pdibs[1]);
    }
    return TRUE;
}

BOOL CPose::ConvertFromMaskedMono(CAvatarDIB* source)
{
    CAvatarDIB* destinations[3]{};
    if (!ConvertMasksCommon(source, destinations, 3)) {
        return FALSE;
    }
    m_pdibs[0] = destinations[0];
    m_pdibs[1] = destinations[1];
    m_pdibs[2] = destinations[2];
    delete source;
    return TRUE;
}

BOOL CPose::ConvertFromDualMask(CAvatarDIB* source)
{
    CAvatarDIB* destinations[2]{};
    if (!ConvertMasksCommon(source, destinations, 2)) {
        return FALSE;
    }
    m_pdibs[1] = destinations[0];
    m_pdibs[2] = destinations[1];
    delete source;
    return TRUE;
}

BOOL CPose::ConvertMasksCommon(CAvatarDIB* source, CAvatarDIB** output, int count)
{
    if (!source || !output || (count != 2 && count != 3)
        || source->GetBitmapInfoHeader().biBitCount != 2) {
        return FALSE;
    }

    BITMAPINFOHEADER header = source->GetBitmapInfoHeader();
    header.biBitCount = 1;
    header.biCompression = BI_RGB;
    header.biSizeImage = 0;
    header.biClrUsed = 2;
    header.biClrImportant = 2;

    const int width = header.biWidth;
    const int height = qAbs(header.biHeight);
    const int sourceStride = source->StorageWidth();
    const int destinationStride = static_cast<int>(DIBStorageWidth(width, 1));
    QVector<QByteArray> bits(count, QByteArray(destinationStride * height, '\0'));
    const QByteArray& sourceBits = source->GetBits();
    if (sourceBits.size() != sourceStride * height) {
        return FALSE;
    }

    for (int y = 0; y < height; ++y) {
        const auto* sourceLine = reinterpret_cast<const BYTE*>(
            sourceBits.constData() + y * sourceStride);
        for (int x = 0; x < width; ++x) {
            const BYTE packed = sourceLine[x / 4];
            const int shift = 6 - 2 * (x & 3);
            const BYTE pair = static_cast<BYTE>((packed >> shift) & 0x03U);
            const BYTE destinationMask = static_cast<BYTE>(0x80U >> (x & 7));
            BYTE* first = reinterpret_cast<BYTE*>(bits[0].data() + y * destinationStride);
            BYTE* second = reinterpret_cast<BYTE*>(bits[1].data() + y * destinationStride);
            if (pair & 0x01U) {
                first[x / 8] |= destinationMask;
            }
            if (pair & 0x02U) {
                second[x / 8] |= destinationMask;
            }
            if (count == 3) {
                BYTE* aura = reinterpret_cast<BYTE*>(bits[2].data() + y * destinationStride);
                if (pair != 0) {
                    aura[x / 8] |= destinationMask;
                }
                // Preserve the original workaround: image pixels may only be
                // black where the mask is black.
                if ((pair & 0x03U) != 0x03U) {
                    first[x / 8] &= static_cast<BYTE>(~destinationMask);
                }
            }
        }
    }

    for (int index = 0; index < count; ++index) {
        output[index] = new CAvatarDIB;
        if (!output[index]->Create(header, MonochromePalette, bits[index])) {
            for (int cleanup = 0; cleanup <= index; ++cleanup) {
                delete output[cleanup];
                output[cleanup] = nullptr;
            }
            return FALSE;
        }
    }
    return TRUE;
}

CChatBackdrop* CChatBackdrop::LoadBackdrop(CAvatarStream* stream)
{
    if (!stream || !stream->Open()) {
        return nullptr;
    }
    AVBINT16 magic = 0;
    if (!stream->Read16(&magic) || !stream->SetPosition(-2, SEEK_CUR)) {
        stream->Close();
        return nullptr;
    }

    auto* backdrop = new CChatBackdrop;
    BOOL loaded = FALSE;
    switch (magic) {
    case 0x4d42:
        loaded = backdrop->LoadFromBmp(stream);
        break;
    case AF_MAGICNUM_NEW:
        loaded = backdrop->Load(stream);
        break;
    default:
        break;
    }
    stream->Close();
    if (!loaded) {
        delete backdrop;
        return nullptr;
    }
    return backdrop;
}

BOOL CChatBackdrop::LoadFromBmp(CAvatarStream* stream)
{
    if (m_pDIB) {
        return FALSE;
    }
    auto* dib = new CAvatarDIB;
    if (!dib->Load(stream)) {
        delete dib;
        return FALSE;
    }
    m_pDIB = dib;
    return TRUE;
}

BOOL CChatBackdrop::Load(CAvatarStream* stream)
{
    if (m_pDIB) {
        return FALSE;
    }
    long resourceAdjustment = 0;
    AVATARHEADER header{};
    if (!readExact(stream, &header, sizeof(header)) || header.nType != AT_BACKDROP
        || HIWORD(header.nVersion) != 0) {
        return FALSE;
    }

    while (true) {
        AVBINT16 tag = 0;
        AVBINT16 size = 0;
        if (!stream->Read16(&tag) || tag == AK_STARTDATA || tag < AK_ICON_NEW
            || !stream->Read16(&size)) {
            return FALSE;
        }

        BOOL handled = FALSE;
        switch (tag) {
        case AK_ORIGINAL_URL:
            handled = TRUE;
            if (!ReadAvFileString(stream, m_originalUrl, MAX_URL)) {
                return FALSE;
            }
            break;
        case AK_OVERRIDE_URL:
            handled = TRUE;
            if (!ReadAvFileString(stream, m_newUrl, MAX_URL)) {
                return FALSE;
            }
            break;
        case AK_COPYRIGHT:
            handled = TRUE;
            if (!ReadAvFileString(stream, m_copyright, MAX_COPYRIGHT)) {
                return FALSE;
            }
            break;
        case AK_OFFSET_ADJUSTMENT: {
            handled = TRUE;
            std::int32_t adjustment = 0;
            if (!stream->Read32(reinterpret_cast<AVBINT32*>(&adjustment))) {
                return FALSE;
            }
            resourceAdjustment += adjustment;
            break;
        }
        default:
            break;
        }

        if (tag == AK_BACKDROP) {
            DWORD offsets[3]{};
            BYTE formats[3]{};
            BYTE paletteTypes[3]{};
            if (!stream->Read32(&offsets[0]) || !stream->Read8(&formats[0])
                || !stream->Read8(&paletteTypes[0])
                || (paletteTypes[0] != AIP_LOCALPALETTE
                    && paletteTypes[0] != AIP_NOPALETTE)) {
                return FALSE;
            }
            AdjustOffset(offsets[0], resourceAdjustment);
            CPose pose(offsets, formats, paletteTypes);
            if (!pose.Load(stream, nullptr)) {
                return FALSE;
            }
            m_pDIB = pose.m_pdibs[0];
            pose.m_pdibs[0] = nullptr;
            break;
        }

        if (!handled && !stream->SetPosition(size, SEEK_CUR)) {
            return FALSE;
        }
    }
    return TRUE;
}
