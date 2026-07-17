// Ported from v2.5-beta-1-modern/traj.h. QPainterPath replaces the active
// Win32 path in CDC; segment ordering and manual dash state are unchanged.

#pragma once

#include "wincompat.h"

#include <QList>
#include <QPainterPath>

class CGraphicalObj {
public:
    virtual ~CGraphicalObj() = default;
    virtual void Draw(QPainterPath* path) = 0;
};

struct DASHINFO {
    BOOL inDash;
    int partialDist;
    int arrayIndex;
    int* dashArray;
    int nIndices;
    POINT lastPoint;
    QPainterPath* path;
};

class CSeg : public CGraphicalObj {
public:
    virtual POINT SegLo() = 0;
    virtual void Dash(DASHINFO&) {}
    ~CSeg() override = default;
};

class CLine final : public CSeg {
public:
    CLine(POINT& low, POINT& high) : m_lo(low), m_hi(high) {}
    void Draw(QPainterPath* path) override;
    POINT SegLo() override { return m_lo; }
    void Dash(DASHINFO& dash) override;

    POINT m_lo;
    POINT m_hi;
};

class CArc final : public CSeg {
public:
    CArc(POINT& low, POINT& high, int altitude)
        : m_lo(low), m_hi(high), m_altitude(altitude) {}
    void Draw(QPainterPath* path) override;
    POINT SegLo() override { return m_lo; }
    void Dash(DASHINFO& dash) override;

    POINT m_lo;
    POINT m_hi;
    int m_altitude;
};

class CTraj final : public CGraphicalObj {
public:
    CTraj() = default;
    ~CTraj() override;
    void AddSeg(CSeg* segment) { m_segs.append(segment); }
    void Draw(QPainterPath* path) override;
    void Dash(QPainterPath* path);

    QList<CSeg*> m_segs;
    BOOL m_closed = FALSE;
};

void DashSeg(POINT& point, DASHINFO& dash);
void DrawArc2(QPainterPath* path, POINT& start, POINT& end, int altitude);
void DashArc2(DASHINFO& dash, POINT& start, POINT& end, int altitude);
