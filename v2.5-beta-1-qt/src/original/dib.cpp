// Ported from v2.5-beta-1-modern/dib.cpp.
// The DIB storage calculations and bottom-up scanline interpretation are kept;
// QImage owns the transient drawing surface in place of GDI objects.

#include "dib.h"

#include <QtEndian>

#include <cstring>

UINT DIBStorageWidth(UINT width, UINT bitCount)
{
    return ((width * bitCount + 31U) & ~31U) / 8U;
}

int NumDIBColorEntries(const BITMAPINFOHEADER& header)
{
    if (header.biClrUsed != 0) {
        return static_cast<int>(header.biClrUsed);
    }
    switch (header.biBitCount) {
    case 1:
        return 2;
    case 2:
        return 4;
    case 4:
        return 16;
    case 8:
        return 256;
    default:
        return 0;
    }
}

void CDIB::Clear()
{
    m_header = {};
    m_colors.clear();
    m_bits.clear();
    m_image = {};
}

BOOL CDIB::Create(const BITMAPINFOHEADER& header, const QVector<QRgb>& colors,
                  const QByteArray& bits)
{
    Clear();
    if (header.biWidth <= 0 || header.biHeight == 0 || header.biBitCount == 0) {
        return FALSE;
    }
    const qsizetype expected = static_cast<qsizetype>(DIBStorageWidth(
        static_cast<UINT>(header.biWidth), header.biBitCount)) * qAbs(header.biHeight);
    // CAvatarDIB::Load in the original retains bfSize-bfOffBits verbatim.
    // Several bundled BMPs contain two trailing bytes after the final padded
    // scanline, so only a too-short buffer is invalid here.
    if (header.biCompression == BI_RGB && bits.size() < expected) {
        return FALSE;
    }
    m_header = header;
    m_colors = colors;
    m_bits = bits;
    return BuildImage();
}

BOOL CDIB::BuildImage()
{
    const int width = m_header.biWidth;
    const int height = qAbs(m_header.biHeight);
    const int sourceStride = StorageWidth();
    const bool bottomUp = m_header.biHeight > 0;

    if (m_header.biCompression != BI_RGB) {
        return FALSE;
    }

    if (m_header.biBitCount == 2) {
        // The original CPose::ConvertMasksCommon consumes the packed 2-bpp
        // storage before a drawable one-bit image is required.
        return TRUE;
    }

    if (m_header.biBitCount == 1) {
        m_image = QImage(width, height, QImage::Format_Mono);
        QVector<QRgb> table = m_colors;
        if (table.isEmpty()) {
            table = {qRgb(0, 0, 0), qRgb(255, 255, 255)};
        }
        m_image.setColorTable(table);
        for (int y = 0; y < height; ++y) {
            const int sourceY = bottomUp ? height - 1 - y : y;
            std::memcpy(m_image.scanLine(y), m_bits.constData() + sourceY * sourceStride,
                        qMin(sourceStride, m_image.bytesPerLine()));
        }
        return TRUE;
    }

    if (m_header.biBitCount == 4 || m_header.biBitCount == 8) {
        m_image = QImage(width, height, QImage::Format_Indexed8);
        m_image.setColorTable(m_colors);
        for (int y = 0; y < height; ++y) {
            const int sourceY = bottomUp ? height - 1 - y : y;
            const auto* source = reinterpret_cast<const BYTE*>(m_bits.constData() + sourceY * sourceStride);
            BYTE* target = m_image.scanLine(y);
            if (m_header.biBitCount == 8) {
                std::memcpy(target, source, width);
            } else {
                for (int x = 0; x < width; ++x) {
                    const BYTE packed = source[x / 2];
                    target[x] = (x & 1) ? static_cast<BYTE>(packed & 0x0fU)
                                        : static_cast<BYTE>(packed >> 4U);
                }
            }
        }
        return TRUE;
    }

    if (m_header.biBitCount == 24) {
        m_image = QImage(width, height, QImage::Format_RGB888);
        for (int y = 0; y < height; ++y) {
            const int sourceY = bottomUp ? height - 1 - y : y;
            const auto* source = reinterpret_cast<const BYTE*>(m_bits.constData() + sourceY * sourceStride);
            BYTE* target = m_image.scanLine(y);
            for (int x = 0; x < width; ++x) {
                target[x * 3] = source[x * 3 + 2];
                target[x * 3 + 1] = source[x * 3 + 1];
                target[x * 3 + 2] = source[x * 3];
            }
        }
        return TRUE;
    }

    if (m_header.biBitCount == 32) {
        m_image = QImage(width, height, QImage::Format_ARGB32);
        for (int y = 0; y < height; ++y) {
            const int sourceY = bottomUp ? height - 1 - y : y;
            std::memcpy(m_image.scanLine(y), m_bits.constData() + sourceY * sourceStride,
                        width * 4);
        }
        return TRUE;
    }

    return FALSE;
}

void CDIB::Draw(QImage* target, int x, int y) const
{
    Draw(target, x, y, GetWidth(), GetHeight(), SRCCOPY);
}

void CDIB::Draw(QImage* target, int x, int y, int destinationWidth,
               int destinationHeight, DWORD rasterOperation) const
{
    Draw(target, x, y, destinationWidth, destinationHeight,
         0, 0, GetWidth(), GetHeight(), rasterOperation);
}

void CDIB::Draw(QImage* target, int x, int y, int destinationWidth,
                int destinationHeight, int sourceX, int sourceY,
                int sourceWidth, int sourceHeight, DWORD rasterOperation) const
{
    if (!target || target->isNull() || m_image.isNull()
        || destinationWidth == 0 || destinationHeight == 0
        || sourceWidth == 0 || sourceHeight == 0) {
        return;
    }

    const int drawWidth = qAbs(destinationWidth);
    const int drawHeight = qAbs(destinationHeight);
    const int normalizedSourceX = sourceWidth < 0 ? sourceX + sourceWidth : sourceX;
    const int normalizedSourceY = sourceHeight < 0 ? sourceY + sourceHeight : sourceY;
    QImage source = m_image.copy(normalizedSourceX, normalizedSourceY,
                                 qAbs(sourceWidth), qAbs(sourceHeight))
                        .convertToFormat(QImage::Format_RGB32)
                        .scaled(drawWidth, drawHeight, Qt::IgnoreAspectRatio,
                                Qt::FastTransformation);
    const bool mirrorX = (sourceWidth < 0) != (destinationWidth < 0);
    const bool mirrorY = (sourceHeight < 0) != (destinationHeight < 0);
    if (mirrorX || mirrorY) {
        source.mirror(mirrorX, mirrorY);
    }
    const int destinationX = destinationWidth < 0 ? x + destinationWidth : x;
    const int destinationY = destinationHeight < 0 ? y + destinationHeight : y;

    if (target->format() != QImage::Format_RGB32
        && target->format() != QImage::Format_ARGB32
        && target->format() != QImage::Format_ARGB32_Premultiplied) {
        *target = target->convertToFormat(QImage::Format_RGB32);
    }

    const int startX = qMax(0, destinationX);
    const int startY = qMax(0, destinationY);
    const int endX = qMin(target->width(), destinationX + drawWidth);
    const int endY = qMin(target->height(), destinationY + drawHeight);
    for (int targetY = startY; targetY < endY; ++targetY) {
        auto* targetLine = reinterpret_cast<QRgb*>(target->scanLine(targetY));
        const auto* sourceLine = reinterpret_cast<const QRgb*>(
            source.constScanLine(targetY - destinationY));
        for (int targetX = startX; targetX < endX; ++targetX) {
            const quint32 sourceRgb = sourceLine[targetX - destinationX] & 0x00ffffffU;
            const quint32 destinationRgb = targetLine[targetX] & 0x00ffffffU;
            quint32 result = sourceRgb;
            if (rasterOperation == SRCAND) {
                result = sourceRgb & destinationRgb;
            } else if (rasterOperation == MERGEPAINT) {
                result = ((~sourceRgb) & 0x00ffffffU) | destinationRgb;
            } else if (rasterOperation != SRCCOPY) {
                return;
            }
            targetLine[targetX] = 0xff000000U | result;
        }
    }
}
