// Ported from v2.5-beta-1-modern/splinutl.cpp without algorithm changes.

#include "spline.h"

#include <cmath>

void split_bezier(BEZIER* source, BEZIER* left, BEZIER* right)
{
    left->p0 = source->p0;
    left->p1 = point_scalmult(0.5, point_add(source->p0, source->p1));
    DPOINT temporary = point_scalmult(0.5, point_add(source->p1, source->p2));
    left->p2 = point_scalmult(0.5, point_add(left->p1, temporary));
    right->p3 = source->p3;
    right->p2 = point_scalmult(0.5, point_add(source->p2, source->p3));
    right->p1 = point_scalmult(0.5, point_add(temporary, right->p2));
    left->p3 = right->p0 = point_scalmult(0.5, point_add(left->p2, right->p1));
}

BOOL inside_bbox_tol(DPOINT* point, BOUNDBOX* bbox, double tolerance)
{
    if (point->x + tolerance < bbox->xmin || point->x - tolerance > bbox->xmax
        || point->y + tolerance < bbox->ymin || point->y - tolerance > bbox->ymax) {
        return FALSE;
    }
    return TRUE;
}

double epsilon = 1.0;

int flat_bezier(BEZIER* bezier)
{
    BOUNDBOX bbox;
    bbox.xmin = std::min(bezier->p0.x, bezier->p3.x);
    bbox.xmax = std::max(bezier->p0.x, bezier->p3.x);
    bbox.ymin = std::min(bezier->p0.y, bezier->p3.y);
    bbox.ymax = std::max(bezier->p0.y, bezier->p3.y);
    if (!inside_bbox_tol(&bezier->p1, &bbox, 0.5 * epsilon)
        || !inside_bbox_tol(&bezier->p2, &bbox, 0.5 * epsilon)) {
        return FALSE;
    }
    const DPOINT first = point_sub(bezier->p1, bezier->p0);
    const DPOINT second = point_sub(bezier->p2, bezier->p0);
    const DPOINT delta = point_sub(bezier->p3, bezier->p0);
    const double dx = std::fabs(delta.x);
    const double dy = std::fabs(delta.y);
    if (dx + dy < epsilon) return TRUE;
    if (dy < dx) {
        const double dyOverDx = delta.y / delta.x;
        return std::fabs(second.y - second.x * dyOverDx) < epsilon
            && std::fabs(first.y - first.x * dyOverDx) < epsilon;
    }
    const double dxOverDy = delta.x / delta.y;
    return std::fabs(second.x - second.y * dxOverDy) < epsilon
        && std::fabs(first.x - first.y * dxOverDy) < epsilon;
}

int subdivide(BEZIER* bezier, int (*callback)(DPOINT*, void*), void* argument,
              double delta)
{
    if (flat_bezier(bezier)) {
        const double length = point_dist(bezier->p0, bezier->p3);
        if (length > SMALLNUMBER) {
            const double step = delta / length;
            for (double alpha = 0.0; alpha <= 1.0; alpha += step) {
                DPOINT point = point_add(point_scalmult(alpha, bezier->p3),
                                         point_scalmult(1.0 - alpha, bezier->p0));
                if (callback(&point, argument)) return TRUE;
            }
        }
        return callback(&bezier->p3, argument);
    }
    BEZIER left;
    BEZIER right;
    split_bezier(bezier, &left, &right);
    return subdivide(&left, callback, argument, delta)
        || subdivide(&right, callback, argument, delta);
}

int walk_path(int count, BEZIER* beziers, int (*callback)(DPOINT*, void*),
              void* argument)
{
    for (int index = 0; index < count; ++index) {
        if (subdivide(&beziers[index], callback, argument, epsilon)) return TRUE;
    }
    return FALSE;
}

namespace {
struct nearinfo {
    double dist;
    DPOINT given_pt;
    DPOINT found_pt;
};

int cb_nearest(DPOINT* point, void* argument)
{
    auto* information = static_cast<nearinfo*>(argument);
    const double distance = std::fabs(point->x - information->given_pt.x)
        + std::fabs(point->y - information->given_pt.y);
    if (distance < information->dist) {
        information->dist = distance;
        information->found_pt = *point;
    }
    return FALSE;
}

void spline_nearest_point(BEZIER beziers[], int count, DPOINT* givenPoint,
                          double* distance, DPOINT* foundPoint)
{
    nearinfo information;
    information.given_pt = *givenPoint;
    information.dist = LARGENUMBER;
    walk_path(count, beziers, cb_nearest, &information);
    *distance = information.dist;
    *foundPoint = information.found_pt;
}

int flatten(BEZIER* bezier, int (*callback)(DPOINT*, void*), void* argument)
{
    if (flat_bezier(bezier)) {
        return callback(&bezier->p3, argument);
    }
    BEZIER left;
    BEZIER right;
    split_bezier(bezier, &left, &right);
    return flatten(&left, callback, argument) || flatten(&right, callback, argument);
}

int cb_beyond_deltaX(DPOINT* point, void* argument)
{
    auto* information = static_cast<nearinfo*>(argument);
    if (point->x > information->found_pt.x) {
        information->found_pt = *point;
    }
    return point->x >= information->given_pt.x;
}
}

void int_bezier_nearest_point(POINT* bezierPoints, POINT& given, int* distance,
                              POINT* found)
{
    BEZIER bezier{
        point_to_dpoint(bezierPoints[0]), point_to_dpoint(bezierPoints[1]),
        point_to_dpoint(bezierPoints[2]), point_to_dpoint(bezierPoints[3]),
    };
    DPOINT givenPoint = point_to_dpoint(given);
    DPOINT foundPoint;
    double foundDistance;
    spline_nearest_point(&bezier, 1, &givenPoint, &foundDistance, &foundPoint);
    *distance = static_cast<int>(foundDistance);
    found->x = static_cast<int>(foundPoint.x);
    found->y = static_cast<int>(foundPoint.y);
}

void int_bezier_flatten(POINT* bezierPoints, int (*callback)(DPOINT*, void*),
                        void* argument)
{
    BEZIER bezier{
        point_to_dpoint(bezierPoints[0]), point_to_dpoint(bezierPoints[1]),
        point_to_dpoint(bezierPoints[2]), point_to_dpoint(bezierPoints[3]),
    };
    flatten(&bezier, callback, argument);
}

BOOL walk_horizontal_dist(POINT* bezierPoints, int goalX, POINT& furthest)
{
    BEZIER bezier{
        point_to_dpoint(bezierPoints[0]), point_to_dpoint(bezierPoints[1]),
        point_to_dpoint(bezierPoints[2]), point_to_dpoint(bezierPoints[3]),
    };
    nearinfo information{};
    information.found_pt.x = -1000000;
    information.given_pt.x = goalX;
    const BOOL found = walk_path(1, &bezier, cb_beyond_deltaX, &information);
    furthest = dpoint_to_point(information.found_pt);
    return found;
}
