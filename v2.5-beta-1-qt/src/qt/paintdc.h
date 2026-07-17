// Qt-only replacement boundary for the original MM_TWIPS CDC used by comic
// panels. Product geometry stays in original logical coordinates: x grows
// right, y grows upward, and a panel occupies (0,0)..(width,-height).

#pragma once

#include "wincompat.h"

#include <QImage>
#include <QPainter>
#include <QRect>
#include <QTransform>

class QtPaintDC {
public:
    QtPaintDC(QImage* surface, int logicalWidth, int logicalHeight,
              bool printing = false)
        : m_surface(surface)
        , m_logicalWidth(logicalWidth)
        , m_logicalHeight(logicalHeight)
        , m_printing(printing)
    {
    }

    QImage* surface() const { return m_surface; }
    int logicalWidth() const { return m_logicalWidth; }
    int logicalHeight() const { return m_logicalHeight; }
    bool isPrinting() const { return m_printing; }

    QTransform transform() const
    {
        if (!m_surface || m_logicalWidth == 0 || m_logicalHeight == 0) return {};
        return QTransform(static_cast<qreal>(m_surface->width()) / m_logicalWidth,
                          0.0, 0.0,
                          -static_cast<qreal>(m_surface->height()) / m_logicalHeight,
                          0.0, 0.0);
    }

    void configure(QPainter& painter) const
    {
        painter.setTransform(transform());
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
    }

    QPoint mapPoint(const POINT& point) const
    {
        return transform().map(QPointF(point.x, point.y)).toPoint();
    }

    QRect mapRect(const RECT& rect) const
    {
        const QPoint first = transform().map(QPointF(rect.left, rect.top)).toPoint();
        const QPoint second = transform().map(QPointF(rect.right, rect.bottom)).toPoint();
        return QRect(first, second).normalized();
    }

private:
    QImage* m_surface = nullptr;
    int m_logicalWidth = 0;
    int m_logicalHeight = 0;
    bool m_printing = false;
};
