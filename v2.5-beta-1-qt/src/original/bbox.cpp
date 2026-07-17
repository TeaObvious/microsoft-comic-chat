// Ported from v2.5-beta-1-modern/bbox.cpp without behavioural changes.

#include "bbox.h"

#include "vector2d.h"

#include <algorithm>

void adjust_bbox(RECT* bbox, int delta)
{
    bbox->left -= delta;
    bbox->bottom -= delta;
    bbox->right += delta;
    bbox->top += delta;
}

void bbox_around_pt(RECT* bbox, POINT* point, int delta)
{
    bbox->left = bbox->right = point->x;
    bbox->top = bbox->bottom = point->y;
    if (delta) {
        adjust_bbox(bbox, delta);
    }
}

void bbox_in_bbox(RECT* source, RECT* destination)
{
    destination->left = std::min(source->left, destination->left);
    destination->bottom = std::min(source->bottom, destination->bottom);
    destination->right = std::max(source->right, destination->right);
    destination->top = std::max(source->top, destination->top);
}

void include_pt_in_bbox(POINT* point, RECT* bbox)
{
    bbox->left = std::min(point->x, bbox->left);
    bbox->bottom = std::min(point->y, bbox->bottom);
    bbox->right = std::max(point->x, bbox->right);
    bbox->top = std::max(point->y, bbox->top);
}

void include_pt_in_bbox(POINT* point, SRECT* bbox)
{
    bbox->Left = static_cast<SHORT>(std::min<LONG>(point->x, bbox->Left));
    bbox->Bottom = static_cast<SHORT>(std::min<LONG>(point->y, bbox->Bottom));
    bbox->Right = static_cast<SHORT>(std::max<LONG>(point->x, bbox->Right));
    bbox->Top = static_cast<SHORT>(std::max<LONG>(point->y, bbox->Top));
}

BOOL inside_bbox(POINT* point, RECT* bbox)
{
    return point->x >= bbox->left && point->x <= bbox->right
        && point->y >= bbox->bottom && point->y <= bbox->top;
}

BOOL inside_bbox(POINT* point, SRECT* bbox)
{
    return point->x >= bbox->Left && point->x <= bbox->Right
        && point->y >= bbox->Bottom && point->y <= bbox->Top;
}

BOOL inside_bbox_tol(POINT* point, RECT* bbox, int tolerance)
{
    return point->x + tolerance >= bbox->left
        && point->x - tolerance <= bbox->right
        && point->y + tolerance >= bbox->bottom
        && point->y - tolerance <= bbox->top;
}

BOOL bbox_overlap(RECT* bbox1, RECT* bbox2)
{
    return !(bbox1->left > bbox2->right || bbox2->left > bbox1->right
             || bbox1->bottom > bbox2->top || bbox2->bottom > bbox1->top);
}

BOOL bbox_within_bbox(RECT* bbox1, RECT* bbox2)
{
    POINT point1{bbox1->right, bbox1->top};
    // Preserve the original assignment to bbox2->bottom.
    POINT point2{bbox1->left, bbox2->bottom};
    return inside_bbox_tol(&point1, bbox2, 0) && inside_bbox_tol(&point2, bbox2, 0);
}

BOOL is_empty(RECT* bbox)
{
    return bbox->left > bbox->right || bbox->bottom > bbox->top;
}

void make_empty(RECT* bbox)
{
    bbox->left = bbox->bottom = LARGEINTEGER;
    bbox->right = bbox->top = -LARGEINTEGER;
}

void make_empty(SRECT* bbox)
{
    bbox->Left = bbox->Bottom = LARGESHORT;
    bbox->Right = bbox->Top = -LARGESHORT;
}

BOOL bbox_intersect(RECT* bbox1, RECT* bbox2, RECT* result)
{
    result->left = std::max(bbox1->left, bbox2->left);
    result->right = std::min(bbox1->right, bbox2->right);
    result->top = std::min(bbox1->top, bbox2->top);
    result->bottom = std::max(bbox1->bottom, bbox2->bottom);
    // The original returns is_empty(result), despite the function name.
    return is_empty(result);
}

RECT SRECTToRECT(SRECT& source)
{
    return {source.Left, source.Top, source.Right, source.Bottom};
}
