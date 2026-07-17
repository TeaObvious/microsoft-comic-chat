// Ported from v2.5-beta-1-modern/spline.h.

#pragma once

#include "traj.h"
#include "vector2d.h"

using MATRIX = double[4][4];

class CSpline : public CSeg {
public:
    CSpline(POINT controlPoints[], int count, BOOL isClosed = FALSE);
    CSpline(const CSpline& source);
    ~CSpline() override;

    virtual int GetDups() = 0;
    void ComputeBezpts();
    virtual int KnotCount() = 0;
    int BezierCount() { return (3 * KnotCount()) - 8; }
    POINT GetKnot(int index);
    void CvertsToCubic(POINT&, POINT&, POINT&, POINT&, POINT&, POINT&, POINT&, POINT&);
    void CubicToBezier(POINT&, POINT&, POINT&, POINT&, POINT&, POINT&, POINT&, POINT&);
    POINT ClosestPoint(POINT& point, int* bezierIndex);
    POINT WalkHorizontalDistance(POINT& fromPoint, int fromKnotIndex, int goalX,
                                 int& foundKnotIndex);
    void Draw(QPainterPath* path) override;
    void Dash(DASHINFO& dash) override;
    POINT SegLo() override;
    virtual CSpline* Clone() = 0;

    BOOL closed;
    MATRIX* matrix;
    POINT* bezpts;
    int nCps;
    POINT* cps;
};

class CCardinal final : public CSpline {
public:
    CCardinal(const CCardinal& source);
    CCardinal(POINT controlPoints[], int count, BOOL isClosed = FALSE);
    void SetMatrix(double newTension);
    int GetDups() override { return 2; }
    int KnotCount() override { return closed ? nCps + 3 : nCps + 2; }
    CSpline* Clone() override { return new CCardinal(*this); }

    static double defaultTension;
    double tension;
};

class CBeta final : public CSpline {
public:
    CBeta(const CBeta& source);
    CBeta(POINT controlPoints[], int count, BOOL isClosed = FALSE);
    void SetMatrix(double newTension, double newBias);
    int GetDups() override { return 3; }
    int KnotCount() override { return closed ? nCps + 3 : nCps + 4; }
    CSpline* Clone() override { return new CBeta(*this); }

    static double defaultTension;
    static double defaultBias;
    double tension;
    double bias;
};

void DestroySplineMatrixCaches();
void split_bezier(BEZIER* source, BEZIER* left, BEZIER* right);
BOOL inside_bbox_tol(DPOINT* point, BOUNDBOX* bbox, double tolerance);
int flat_bezier(BEZIER* bezier);
int subdivide(BEZIER* bezier, int (*callback)(DPOINT*, void*), void* argument,
              double delta);
int walk_path(int count, BEZIER* beziers, int (*callback)(DPOINT*, void*),
              void* argument);
void int_bezier_nearest_point(POINT* bezierPoints, POINT& given, int* distance,
                              POINT* found);
void int_bezier_flatten(POINT* bezierPoints, int (*callback)(DPOINT*, void*),
                        void* argument);
BOOL walk_horizontal_dist(POINT* bezierPoints, int goalX, POINT& furthest);
