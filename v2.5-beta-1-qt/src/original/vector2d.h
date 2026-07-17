// Ported from v2.5-beta-1-modern/vector2d.h.

#pragma once

#include "wincompat.h"

constexpr double VECTOR_PI = 3.14159265358979323846;
constexpr double LARGENUMBER = 1.e24;
constexpr double SMALLNUMBER = 1.e-24;
constexpr int LARGEINTEGER = 100000000;
constexpr short LARGESHORT = 31000;

inline int ROUND(double value)
{
    return static_cast<int>(value > 0.0 ? value + 0.5 : value - 0.5);
}

struct DPOINT {
    double x;
    double y;
};

struct BOUNDBOX {
    double xmin;
    double xmax;
    double ymin;
    double ymax;
};

struct BEZIER {
    DPOINT p0;
    DPOINT p1;
    DPOINT p2;
    DPOINT p3;
};

DPOINT point_sub(DPOINT pt1, DPOINT pt2);
DPOINT point_add(DPOINT pt1, DPOINT pt2);
DPOINT point_scalmult(double scalar, DPOINT point);
double point_dot(DPOINT pt1, DPOINT pt2);
double point_dist(DPOINT pt1, DPOINT pt2);
double point_distsq(DPOINT pt1, DPOINT pt2);
double point_magn(DPOINT point);
DPOINT point_norm(DPOINT point);
double vector_to_angle(DPOINT vector);

POINT point_sub(POINT pt1, POINT pt2);
POINT point_add(POINT pt1, POINT pt2);
POINT point_scalmult(double scalar, POINT point);
double point_dot(POINT pt1, POINT pt2);
double point_dist(POINT pt1, POINT pt2);
double point_distsq(POINT pt1, POINT pt2);
int manhattan_dist(POINT pt1, POINT pt2);
double point_magn(POINT point);
POINT point_norm(POINT point);
double vector_to_angle(POINT vector);
DPOINT point_to_dpoint(POINT point);
POINT dpoint_to_point(DPOINT point);

double degrees_to_rads(double degrees);
DPOINT angle_to_vector(double angle);
double value_to_angle(double value);
double add_angles(double angle1, double angle2);
double subtract_angles(double angle1, double angle2);
double angle_between_vecs(DPOINT vector1, DPOINT vector2);
