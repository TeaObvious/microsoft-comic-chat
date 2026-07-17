// Ported from v2.5-beta-1-modern/traj.cpp.

#include "traj.h"

#include "vector2d.h"

namespace {
int dashArray[] = {100, 100};
}

void DashSeg(POINT& thisPoint, DASHINFO& dash)
{
    int nextDistance;
    int distanceLimit = dash.dashArray[dash.arrayIndex];
    while (TRUE) {
        nextDistance = manhattan_dist(dash.lastPoint, thisPoint);
        if (nextDistance + dash.partialDist < dash.dashArray[dash.arrayIndex]) {
            break;
        }
        const POINT deltaVector = point_sub(thisPoint, dash.lastPoint);
        const DPOINT normalized{
            static_cast<double>(deltaVector.x) / nextDistance,
            static_cast<double>(deltaVector.y) / nextDistance,
        };
        const POINT interpolated = point_add(
            dash.lastPoint,
            dpoint_to_point(point_scalmult(
                static_cast<double>(distanceLimit - dash.partialDist), normalized)));
        if (dash.inDash) {
            dash.path->lineTo(interpolated.x, interpolated.y);
        } else {
            dash.path->moveTo(interpolated.x, interpolated.y);
        }
        dash.lastPoint = interpolated;
        dash.inDash = !dash.inDash;
        dash.partialDist = 0;
        dash.arrayIndex = (dash.arrayIndex + 1) % dash.nIndices;
        distanceLimit = dash.dashArray[dash.arrayIndex];
    }
    dash.partialDist += nextDistance;
    if (dash.inDash) {
        dash.path->lineTo(thisPoint.x, thisPoint.y);
    }
    dash.lastPoint = thisPoint;
}

CTraj::~CTraj()
{
    qDeleteAll(m_segs);
}

void CTraj::Draw(QPainterPath* path)
{
    BOOL firstSegment = TRUE;
    for (CSeg* segment : m_segs) {
        if (firstSegment) {
            const POINT low = segment->SegLo();
            path->moveTo(low.x, low.y);
            firstSegment = FALSE;
        }
        segment->Draw(path);
    }
    if (m_closed) {
        path->closeSubpath();
    }
}

void CTraj::Dash(QPainterPath* path)
{
    BOOL firstSegment = TRUE;
    DASHINFO dash{};
    for (CSeg* segment : m_segs) {
        if (firstSegment) {
            dash.lastPoint = segment->SegLo();
            path->moveTo(dash.lastPoint.x, dash.lastPoint.y);
            firstSegment = FALSE;
            dash.inDash = TRUE;
            dash.partialDist = 0;
            dash.arrayIndex = 0;
            dash.path = path;
            dash.nIndices = 2;
            dash.dashArray = dashArray;
        }
        segment->Dash(dash);
    }
}

void CLine::Draw(QPainterPath* path)
{
    path->lineTo(m_hi.x, m_hi.y);
}

void CLine::Dash(DASHINFO& dash)
{
    DashSeg(m_hi, dash);
}

void CArc::Draw(QPainterPath* path)
{
    DrawArc2(path, m_lo, m_hi, m_altitude);
}

void CArc::Dash(DASHINFO& dash)
{
    DashArc2(dash, m_lo, m_hi, m_altitude);
}
