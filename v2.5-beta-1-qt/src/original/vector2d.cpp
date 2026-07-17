// Ported from v2.5-beta-1-modern/vector2d.cpp.

#include "vector2d.h"

#include <cmath>
#include <cstdlib>

DPOINT point_sub(DPOINT pt1, DPOINT pt2) { return {pt1.x - pt2.x, pt1.y - pt2.y}; }
DPOINT point_add(DPOINT pt1, DPOINT pt2) { return {pt1.x + pt2.x, pt1.y + pt2.y}; }
DPOINT point_scalmult(double scalar, DPOINT point)
{
    point.x *= scalar;
    point.y *= scalar;
    return point;
}
double point_dot(DPOINT pt1, DPOINT pt2) { return pt1.x * pt2.x + pt1.y * pt2.y; }
double point_dist(DPOINT pt1, DPOINT pt2) { return std::sqrt(point_distsq(pt1, pt2)); }
double point_distsq(DPOINT pt1, DPOINT pt2)
{
    const double x = pt1.x - pt2.x;
    const double y = pt1.y - pt2.y;
    return x * x + y * y;
}
double point_magn(DPOINT point) { return std::sqrt(point.x * point.x + point.y * point.y); }
DPOINT point_norm(DPOINT point)
{
    const double magnitude = point_magn(point);
    return magnitude < SMALLNUMBER ? DPOINT{0.0, 0.0} : point_scalmult(1.0 / magnitude, point);
}
double vector_to_angle(DPOINT vector)
{
    return (std::fabs(vector.x) < SMALLNUMBER && std::fabs(vector.y) < SMALLNUMBER)
        ? 0.0
        : std::atan2(vector.y, vector.x);
}

POINT point_sub(POINT pt1, POINT pt2) { return {pt1.x - pt2.x, pt1.y - pt2.y}; }
POINT point_add(POINT pt1, POINT pt2) { return {pt1.x + pt2.x, pt1.y + pt2.y}; }
POINT point_scalmult(double scalar, POINT point)
{
    point.x = static_cast<LONG>(point.x * scalar);
    point.y = static_cast<LONG>(point.y * scalar);
    return point;
}
double point_dot(POINT pt1, POINT pt2) { return pt1.x * pt2.x + pt1.y * pt2.y; }
double point_dist(POINT pt1, POINT pt2) { return std::sqrt(point_distsq(pt1, pt2)); }
double point_distsq(POINT pt1, POINT pt2)
{
    const double x = pt1.x - pt2.x;
    const double y = pt1.y - pt2.y;
    return x * x + y * y;
}
int manhattan_dist(POINT pt1, POINT pt2)
{
    return std::abs(pt1.x - pt2.x) + std::abs(pt1.y - pt2.y);
}
double point_magn(POINT point)
{
    return std::sqrt(static_cast<double>(point.x * point.x + point.y * point.y));
}
POINT point_norm(POINT point)
{
    const double magnitude = point_magn(point);
    return magnitude < SMALLNUMBER ? POINT{0, 0} : point_scalmult(1.0 / magnitude, point);
}
double vector_to_angle(POINT vector)
{
    return (std::abs(vector.x) < SMALLNUMBER && std::abs(vector.y) < SMALLNUMBER)
        ? 0.0
        : std::atan2(static_cast<double>(vector.y), static_cast<double>(vector.x));
}
DPOINT point_to_dpoint(POINT point)
{
    return {static_cast<double>(point.x), static_cast<double>(point.y)};
}
POINT dpoint_to_point(DPOINT point)
{
    return {ROUND(point.x), ROUND(point.y)};
}

double degrees_to_rads(double degrees) { return degrees * (VECTOR_PI / 180.0); }
DPOINT angle_to_vector(double angle) { return {std::cos(angle), std::sin(angle)}; }
double value_to_angle(double value)
{
    if (value > -VECTOR_PI && value <= VECTOR_PI) {
        return value;
    }
    double temporary = value / (2 * VECTOR_PI);
    temporary = (temporary - static_cast<int>(temporary)) * 2 * VECTOR_PI;
    if (temporary > VECTOR_PI) {
        return temporary - 2 * VECTOR_PI;
    }
    if (temporary <= -VECTOR_PI) {
        return temporary + 2 * VECTOR_PI;
    }
    return temporary;
}
double add_angles(double angle1, double angle2) { return value_to_angle(angle1 + angle2); }
double subtract_angles(double angle1, double angle2) { return value_to_angle(angle1 - angle2); }
double angle_between_vecs(DPOINT vector1, DPOINT vector2)
{
    return subtract_angles(vector_to_angle(vector2), vector_to_angle(vector1));
}
