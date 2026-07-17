// Ported from v2.5-beta-1-modern/spline.cpp. MFC maps are replaced by QHash;
// matrix values, cache keys, knot duplication and rounding are unchanged.

#include "spline.h"

#include <QHash>
#include <QString>

#include <cstdio>
#include <cstdlib>

namespace {
QHash<WORD, MATRIX*> cardinalMatrixMap;
QHash<QString, MATRIX*> betaMatrixMap;
}

CSpline::CSpline(POINT controlPoints[], int count, BOOL isClosed)
    : closed(isClosed)
    , matrix(nullptr)
    , bezpts(nullptr)
    , nCps(count)
    , cps(new POINT[count])
{
    Q_ASSERT(count >= 2);
    for (int index = 0; index < nCps; ++index) {
        cps[index] = controlPoints[index];
    }
}

CSpline::CSpline(const CSpline& source)
    : closed(source.closed)
    , matrix(source.matrix)
    , bezpts(nullptr)
    , nCps(source.nCps)
    , cps(source.cps ? new POINT[source.nCps] : nullptr)
{
    for (int index = 0; cps && index < nCps; ++index) {
        cps[index] = source.cps[index];
    }
}

CSpline::~CSpline()
{
    delete[] cps;
    delete[] bezpts;
}

double CCardinal::defaultTension = 0.4;

CCardinal::CCardinal(POINT controlPoints[], int count, BOOL isClosed)
    : CSpline(controlPoints, count, isClosed)
    , tension(defaultTension)
{
    SetMatrix(tension);
    ComputeBezpts();
}

CCardinal::CCardinal(const CCardinal& source)
    : CSpline(source)
    , tension(source.tension)
{
    if (source.bezpts) {
        const int count = BezierCount();
        bezpts = new POINT[count];
        for (int index = 0; index < count; ++index) {
            bezpts[index] = source.bezpts[index];
        }
    }
}

double CBeta::defaultTension = 5.0;
double CBeta::defaultBias = 1.0;

CBeta::CBeta(POINT controlPoints[], int count, BOOL isClosed)
    : CSpline(controlPoints, count, isClosed)
    , tension(defaultTension)
    , bias(defaultBias)
{
    SetMatrix(tension, bias);
    ComputeBezpts();
}

CBeta::CBeta(const CBeta& source)
    : CSpline(source)
    , tension(source.tension)
    , bias(source.bias)
{
    if (source.bezpts) {
        const int count = BezierCount();
        bezpts = new POINT[count];
        for (int index = 0; index < count; ++index) {
            bezpts[index] = source.bezpts[index];
        }
    }
}

void CCardinal::SetMatrix(double newTension)
{
    const WORD key = static_cast<WORD>(static_cast<float>(newTension));
    if (cardinalMatrixMap.contains(key)) {
        matrix = cardinalMatrixMap.value(key);
        return;
    }
    matrix = static_cast<MATRIX*>(std::malloc(sizeof(MATRIX)));
    (*matrix)[0][1] = 2.0 - newTension;
    (*matrix)[0][2] = newTension - 2.0;
    (*matrix)[1][0] = 2.0 * newTension;
    (*matrix)[1][1] = newTension - 3.0;
    (*matrix)[1][2] = 3.0 - 2.0 * newTension;
    (*matrix)[3][1] = 1.0;
    (*matrix)[0][3] = (*matrix)[2][2] = newTension;
    (*matrix)[0][0] = (*matrix)[1][3] = (*matrix)[2][0] = -newTension;
    (*matrix)[2][1] = (*matrix)[2][3] = (*matrix)[3][0]
        = (*matrix)[3][2] = (*matrix)[3][3] = 0.0;
    cardinalMatrixMap.insert(key, matrix);
}

void CBeta::SetMatrix(double newTension, double newBias)
{
    const QString key = QStringLiteral("%1*%2")
                            .arg(newTension, 0, 'f', 6)
                            .arg(newBias, 0, 'f', 6);
    if (betaMatrixMap.contains(key)) {
        matrix = betaMatrixMap.value(key);
        return;
    }
    matrix = static_cast<MATRIX*>(std::malloc(sizeof(MATRIX)));
    const double bias2 = newBias * newBias;
    const double bias3 = newBias * bias2;
    const double divisor = 1.0
        / (newTension + 2.0 * bias3 + 4.0 * (bias2 + newBias) + 2.0);
    (*matrix)[0][0] = -2.0 * bias3;
    (*matrix)[0][1] = 2.0 * (newTension + bias3 + bias2 + newBias);
    (*matrix)[0][2] = -2.0 * (newTension + bias2 + newBias + 1.0);
    (*matrix)[1][0] = 6.0 * bias3;
    (*matrix)[1][1] = -3.0 * (newTension + 2.0 * (bias3 + bias2));
    (*matrix)[1][2] = 3.0 * (newTension + 2.0 * bias2);
    (*matrix)[2][0] = -6.0 * bias3;
    (*matrix)[2][1] = 6.0 * (bias3 - newBias);
    (*matrix)[2][2] = 6.0 * newBias;
    (*matrix)[3][0] = 2.0 * bias3;
    (*matrix)[3][1] = newTension + 4.0 * (bias2 + newBias);
    (*matrix)[0][3] = (*matrix)[3][2] = 2.0;
    (*matrix)[1][3] = (*matrix)[2][3] = (*matrix)[3][3] = 0.0;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            (*matrix)[row][column] *= divisor;
        }
    }
    betaMatrixMap.insert(key, matrix);
}

void DestroySplineMatrixCaches()
{
    for (MATRIX* cached : cardinalMatrixMap) {
        std::free(cached);
    }
    cardinalMatrixMap.clear();
    for (MATRIX* cached : betaMatrixMap) {
        std::free(cached);
    }
    betaMatrixMap.clear();
}

void CSpline::ComputeBezpts()
{
    const int knotCount = KnotCount();
    Q_ASSERT(knotCount >= 4);
    if (!bezpts) {
        bezpts = new POINT[BezierCount()];
    }
    int bezierIndex = 1;
    POINT knot0 = GetKnot(0);
    POINT knot1 = GetKnot(1);
    POINT knot2 = GetKnot(2);
    POINT knot3 = GetKnot(3);
    POINT cubic0, cubic1, cubic2, cubic3;
    POINT bezier0, bezier1, bezier2, bezier3;
    for (int index = 0;; ++index) {
        CvertsToCubic(knot0, knot1, knot2, knot3,
                      cubic0, cubic1, cubic2, cubic3);
        CubicToBezier(cubic0, cubic1, cubic2, cubic3,
                      bezier0, bezier1, bezier2, bezier3);
        if (index == 0) {
            bezpts[0] = bezier0;
        }
        bezpts[bezierIndex] = bezier1;
        bezpts[bezierIndex + 1] = bezier2;
        bezpts[bezierIndex + 2] = bezier3;
        if (index + 4 == knotCount) {
            return;
        }
        bezierIndex += 3;
        knot0 = knot1;
        knot1 = knot2;
        knot2 = knot3;
        knot3 = GetKnot(index + 4);
    }
}

void CSpline::CvertsToCubic(POINT& knot0, POINT& knot1, POINT& knot2, POINT& knot3,
                            POINT& cubic0, POINT& cubic1, POINT& cubic2, POINT& cubic3)
{
    POINT* knots[4] = {&knot0, &knot1, &knot2, &knot3};
    POINT* cubics[4] = {&cubic3, &cubic2, &cubic1, &cubic0};
    for (int row = 0; row < 4; ++row) {
        double x = 0.0;
        double y = 0.0;
        for (int column = 0; column < 4; ++column) {
            x += (*matrix)[row][column] * knots[column]->x;
            y += (*matrix)[row][column] * knots[column]->y;
        }
        cubics[row]->x = ROUND(x);
        cubics[row]->y = ROUND(y);
    }
}

void CSpline::CubicToBezier(POINT& cubic0, POINT& cubic1, POINT& cubic2, POINT& cubic3,
                            POINT& bezier0, POINT& bezier1, POINT& bezier2, POINT& bezier3)
{
    bezier0 = cubic0;
    bezier1.x = cubic0.x + ROUND((1.0 / 3.0) * cubic1.x);
    bezier1.y = cubic0.y + ROUND((1.0 / 3.0) * cubic1.y);
    bezier2.x = bezier1.x + ROUND((1.0 / 3.0) * (cubic1.x + cubic2.x));
    bezier2.y = bezier1.y + ROUND((1.0 / 3.0) * (cubic1.y + cubic2.y));
    bezier3.x = cubic0.x + cubic1.x + cubic2.x + cubic3.x;
    bezier3.y = cubic0.y + cubic1.y + cubic2.y + cubic3.y;
}

POINT CSpline::GetKnot(int index)
{
    if (closed) {
        if (index == 0) return cps[nCps - 1];
        if (index == nCps + 1) return cps[0];
        if (index == nCps + 2) return cps[1];
        return cps[index - 1];
    }
    const int duplicates = GetDups();
    if (index < duplicates) return cps[0];
    if (index >= nCps + duplicates - 2) return cps[nCps - 1];
    return cps[index - duplicates + 1];
}

POINT CSpline::ClosestPoint(POINT& toPoint, int* knotIndex)
{
    int minimumDistance = 10000000;
    POINT minimumPosition{};
    const int count = BezierCount();
    for (int index = 0; index < count - 1; index += 3) {
        int distance;
        POINT position;
        int_bezier_nearest_point(bezpts + index, toPoint, &distance, &position);
        if (distance < minimumDistance) {
            minimumDistance = distance;
            minimumPosition = position;
            *knotIndex = index / 3 + 2;
        }
    }
    return minimumPosition;
}

POINT CSpline::WalkHorizontalDistance(POINT&, int fromKnotIndex, int goalX,
                                      int& foundKnotIndex)
{
    const int count = BezierCount();
    POINT furthest{0, 0};
    POINT lastFurthest{-100000, -100000};
    foundKnotIndex = -1;
    int index = (fromKnotIndex - 2) * 3;
    for (int loop = 0; loop < count - 1; loop += 3) {
        if (index + 3 > count - 1) index = 0;
        if (walk_horizontal_dist(bezpts + index, goalX, furthest)) {
            foundKnotIndex = index / 3 + 2;
            return furthest;
        }
        if (furthest.x > lastFurthest.x) {
            foundKnotIndex = index / 3 + 2;
            lastFurthest = furthest;
        }
        index += 3;
    }
    Q_ASSERT(foundKnotIndex > 0);
    return lastFurthest;
}

void CSpline::Draw(QPainterPath* path)
{
    const int count = BezierCount();
    for (int index = 1; index < count; index += 3) {
        path->cubicTo(bezpts[index].x, bezpts[index].y,
                      bezpts[index + 1].x, bezpts[index + 1].y,
                      bezpts[index + 2].x, bezpts[index + 2].y);
    }
}

POINT CSpline::SegLo()
{
    return bezpts[0];
}

namespace {
int dash_sample(DPOINT* point, void* argument)
{
    POINT rounded = dpoint_to_point(*point);
    DashSeg(rounded, *static_cast<DASHINFO*>(argument));
    return FALSE;
}
}

void CSpline::Dash(DASHINFO& dash)
{
    const int count = BezierCount();
    for (int index = 0; index < count - 1; index += 3) {
        int_bezier_flatten(bezpts + index, dash_sample, &dash);
    }
}
