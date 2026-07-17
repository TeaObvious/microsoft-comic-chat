// Ported from v2.5-beta-1-modern/dib.h.
// QImage replaces CDIB's GDI allocation and blitting surface only.

#pragma once

#include "wincompat.h"

#include <QByteArray>
#include <QImage>
#include <QVector>

UINT DIBStorageWidth(UINT width, UINT bitCount);
int NumDIBColorEntries(const BITMAPINFOHEADER& header);

class CDIB {
public:
    CDIB() = default;
    virtual ~CDIB() = default;

    int GetWidth() const { return m_header.biWidth; }
    int GetHeight() const { return qAbs(m_header.biHeight); }
    int StorageWidth() const
    {
        return static_cast<int>(DIBStorageWidth(static_cast<UINT>(qAbs(m_header.biWidth)),
                                                m_header.biBitCount));
    }
    const BITMAPINFOHEADER& GetBitmapInfoHeader() const { return m_header; }
    const QByteArray& GetBits() const { return m_bits; }
    const QVector<QRgb>& GetColorTable() const { return m_colors; }
    const QImage& Image() const { return m_image; }
    bool IsValid() const { return !m_image.isNull() || (!m_bits.isEmpty() && m_header.biBitCount == 2); }

    BOOL Create(const BITMAPINFOHEADER& header, const QVector<QRgb>& colors,
                const QByteArray& bits);
    void Clear();

    // QImage is the Qt boundary replacing the original CDC target. Coordinates,
    // negative destination extents and the three GDI raster operations retain
    // CDIB::Draw semantics used by avatar/body composition.
    void Draw(QImage* target, int x, int y) const;
    void Draw(QImage* target, int x, int y, int destinationWidth,
              int destinationHeight, DWORD rasterOperation) const;
    void Draw(QImage* target, int destinationX, int destinationY,
              int destinationWidth, int destinationHeight,
              int sourceX, int sourceY, int sourceWidth, int sourceHeight,
              DWORD rasterOperation) const;

protected:
    BOOL BuildImage();

    BITMAPINFOHEADER m_header{};
    QVector<QRgb> m_colors;
    QByteArray m_bits;
    QImage m_image;
};
