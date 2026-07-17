#include "bbox.h"
#include "format.h"
#include "spline.h"
#include "traj.h"

#include <QPainterPath>

#include <cstring>
#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        ++failures;
    }
}
}

int main()
{
    check(wBold == 0x0100 && wItalic == 0x0200 && wUnderline == 0x0400
              && wFixedPitch == 0x0800 && wSymbol == 0x1000
              && wForeground == 0x2000 && wBackground == 0x4000
              && wLink == 0x8000,
          "format bits differ from artifacts/inc/format.h");

    char controlled[] = {chCtlBold, 'A', chCtlBold, 'B', '\0'};
    CDWordArray formatting;
    check(std::strcmp(SzControlLess(controlled, &formatting), "AB") == 0,
          "SzControlLess text differs");
    check(formatting.GetSize() == 2
              && formatting.GetAt(0) == MAKELONG(wBold, 0)
              && formatting.GetAt(1) == MAKELONG(0, 1),
          "SzControlLess DWORD layout differs");
    char* restored = SzControlFull(controlled, &formatting);
    const char expectedControlled[] = {chCtlBold, 'A', chCtlBold, 'B', '\0'};
    check(std::strcmp(restored, expectedControlled) == 0,
          "SzControlFull does not invert the source control sequence");
    delete[] restored;

    CDWordArray* pulled = PullFormattingOffsets(&formatting, 1);
    check(pulled && pulled->GetSize() == 2
              && pulled->GetAt(0) == MAKELONG(wBold, 0)
              && pulled->GetAt(1) == MAKELONG(0, 0),
          "PullFormattingOffsets differs from original offset semantics");
    FreeAndNullFormatting(&pulled);
    check(GetColorCode(RGB(255, 0, 0)) == 4
              && GetRBGColor(4) == RGB(255, 0, 0)
              && GetColorCode(RGB(1, 2, 3)) == 1,
          "IRC color table differs");

    RECT box{10, 40, 30, 20};
    adjust_bbox(&box, 5);
    check(box.left == 5 && box.bottom == 15 && box.right == 35 && box.top == 45,
          "adjust_bbox differs");
    RECT first{0, 10, 10, 0};
    RECT second{5, 15, 15, 5};
    RECT intersection{};
    check(bbox_intersect(&first, &second, &intersection) == FALSE
              && intersection.left == 5 && intersection.right == 10
              && intersection.bottom == 5 && intersection.top == 10,
          "bbox_intersect original return convention differs");
    RECT disjoint{20, 30, 30, 20};
    check(bbox_intersect(&first, &disjoint, &intersection) == TRUE,
          "bbox_intersect must return is_empty(result)");

    POINT low{0, 0};
    POINT high{250, 0};
    CTraj trajectory;
    trajectory.AddSeg(new CLine(low, high));
    QPainterPath dashed;
    trajectory.Dash(&dashed);
    check(dashed.elementCount() == 4
              && dashed.elementAt(0).x == 0 && dashed.elementAt(1).x == 100
              && dashed.elementAt(2).x == 200 && dashed.elementAt(3).x == 250,
          "trajectory 100/100 dash sequence differs");

    POINT cardinalPoints[] = {{0, 0}, {100, 0}};
    CCardinal cardinal(cardinalPoints, 2, FALSE);
    check(cardinal.BezierCount() == 4
              && cardinal.bezpts[0].x == 0
              && cardinal.bezpts[1].x == 13
              && cardinal.bezpts[2].x == 86
              && cardinal.bezpts[3].x == 100,
          "cardinal matrix/knot conversion differs");
    DestroySplineMatrixCaches();

    return failures == 0 ? 0 : 1;
}
