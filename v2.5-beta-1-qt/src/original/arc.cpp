// Ported from v2.5-beta-1-modern/arc.cpp. QPainterPath::cubicTo replaces
// CDC::PolyBezierTo; all arc construction and dash sampling math is unchanged.

#include "traj.h"

#include "vector2d.h"

#include <cmath>

namespace {
void ScanArcAux(QPainterPath* path, DPOINT& first, DPOINT& last, POINT& center,
                double radius, double angle)
{
    const double cosine = std::cos(angle / 2.0);
    const double tau = 4.0 * cosine / (3.0 * (cosine + 1.0));
    const double divisor = (first.x * last.y - first.y * last.x) / (radius * radius);
    DPOINT middle{(last.y - first.y) / divisor, (first.x - last.x) / divisor};
    const DPOINT scaledMiddle = point_scalmult(tau, middle);
    POINT controls[3];
    controls[0].x = ROUND((1.0 - tau) * first.x + scaledMiddle.x) + center.x;
    controls[0].y = ROUND((1.0 - tau) * first.y + scaledMiddle.y) + center.y;
    controls[1].x = ROUND((1.0 - tau) * last.x + scaledMiddle.x) + center.x;
    controls[1].y = ROUND((1.0 - tau) * last.y + scaledMiddle.y) + center.y;
    controls[2] = point_add(dpoint_to_point(last), center);
    path->cubicTo(controls[0].x, controls[0].y,
                  controls[1].x, controls[1].y,
                  controls[2].x, controls[2].y);
}

constexpr double ARCSTEP = VECTOR_PI / 2.0;

void ScanArc(QPainterPath* path, POINT& absoluteCenter, POINT& start, POINT& end,
             BOOL counterClockwise = TRUE)
{
    DPOINT first = point_to_dpoint(point_sub(start, absoluteCenter));
    const DPOINT finalPoint = point_to_dpoint(point_sub(end, absoluteCenter));
    DPOINT last;
    const double radius = point_magn(first);
    double trueAngle = angle_between_vecs(finalPoint, first);
    if (counterClockwise) trueAngle = -trueAngle;
    if (trueAngle <= 0.0) trueAngle += 2.0 * VECTOR_PI;
    double nextEnd = vector_to_angle(first);
    double step = counterClockwise ? ARCSTEP : -ARCSTEP;
    BOOL exit = FALSE;
    while (TRUE) {
        if (trueAngle > ARCSTEP) {
            nextEnd += step;
            last = point_scalmult(radius, angle_to_vector(nextEnd));
        } else {
            exit = TRUE;
            last = finalPoint;
            step = trueAngle;
        }
        ScanArcAux(path, first, last, absoluteCenter, radius, step);
        if (exit) break;
        first = last;
        trueAngle -= ARCSTEP;
    }
}
}

void DrawArc2(QPainterPath* path, POINT& start, POINT& end, int altitude)
{
    if (altitude < 1 && altitude > -1) {
        path->lineTo(end.x, end.y);
        return;
    }
    const POINT middle = point_scalmult(0.5, point_add(start, end));
    const POINT endToMiddle = point_sub(middle, end);
    const double endToMiddleDistance = point_magn(endToMiddle);
    const double radius = (endToMiddleDistance * endToMiddleDistance
                           + altitude * altitude) / (2.0 * altitude);
    const double middleToCenterDistance = radius - altitude;
    Q_ASSERT(std::fabs(radius) >= std::fabs(endToMiddleDistance));
    POINT middleToCenter{endToMiddle.y, -endToMiddle.x};
    middleToCenter = point_scalmult(
        middleToCenterDistance / point_magn(middleToCenter), middleToCenter);
    POINT absoluteCenter = point_add(point_add(end, endToMiddle), middleToCenter);
    ScanArc(path, absoluteCenter, start, end, altitude > 0);
}

void DashArc2(DASHINFO& dash, POINT& start, POINT& end, int altitude)
{
    if (altitude < 1 && altitude > -1) {
        DashSeg(end, dash);
        return;
    }
    const POINT middle = point_scalmult(0.5, point_add(start, end));
    const POINT endToMiddle = point_sub(middle, end);
    const double endToMiddleDistance = point_magn(endToMiddle);
    double radius = (endToMiddleDistance * endToMiddleDistance
                     + altitude * altitude) / (2.0 * altitude);
    const double middleToCenterDistance = radius - altitude;
    Q_ASSERT(std::fabs(radius) >= std::fabs(endToMiddleDistance));
    POINT middleToCenter{endToMiddle.y, -endToMiddle.x};
    middleToCenter = point_scalmult(
        middleToCenterDistance / point_magn(middleToCenter), middleToCenter);
    const POINT absoluteCenter = point_add(point_add(end, endToMiddle), middleToCenter);

    const int counterClockwise = altitude > 0;
    DPOINT first = point_to_dpoint(point_sub(start, absoluteCenter));
    const DPOINT finalPoint = point_to_dpoint(point_sub(end, absoluteCenter));
    DPOINT last;
    double trueAngle = angle_between_vecs(finalPoint, first);
    if (counterClockwise) trueAngle = -trueAngle;
    if (trueAngle <= 0.0) trueAngle += 2.0 * VECTOR_PI;
    double nextEnd = vector_to_angle(first);
    constexpr double sampleStep = 0.02;
    const double step = counterClockwise ? sampleStep : -sampleStep;
    radius = std::fabs(radius);
    BOOL exit = FALSE;
    while (TRUE) {
        if (trueAngle > sampleStep) {
            nextEnd += step;
            last = point_scalmult(radius, angle_to_vector(nextEnd));
        } else {
            exit = TRUE;
            last = finalPoint;
        }
        POINT sample = point_add(dpoint_to_point(last), absoluteCenter);
        DashSeg(sample, dash);
        if (exit) break;
        first = last;
        trueAngle -= sampleStep;
    }
}
