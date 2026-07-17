// Ported from v2.5-beta-1-modern/pe.h. QPainter is the CDC boundary.

#pragma once

#include "bbox.h"

class QtPaintDC;

constexpr int PE_UNKNOWN = 0;
constexpr int PE_BALLOON = 1;
constexpr int PE_BOX = 2;

class CPanelElement {
public:
    CPanelElement() { m_bbox.Left = m_bbox.Right = -1; }
    CPanelElement(const CPanelElement& source);
    virtual ~CPanelElement() = default;
    virtual void Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage) = 0;
    virtual BOOL SetBBox(int left, int bottom, int right, int top);
    virtual void GetBBox(RECT* result);
    virtual int GetType() { return PE_UNKNOWN; }

    SRECT m_bbox;
};
