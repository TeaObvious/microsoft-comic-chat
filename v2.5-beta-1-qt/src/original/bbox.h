// Ported from v2.5-beta-1-modern/bbox.h.

#pragma once

#include "wincompat.h"

struct SRECT {
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
};

void adjust_bbox(RECT* bbox, int delta);
void bbox_around_pt(RECT* bbox, POINT* point, int delta = 0);
void bbox_in_bbox(RECT* source, RECT* destination);
void include_pt_in_bbox(POINT* point, RECT* bbox);
void include_pt_in_bbox(POINT* point, SRECT* bbox);
BOOL inside_bbox(POINT* point, RECT* bbox);
BOOL inside_bbox(POINT* point, SRECT* bbox);
BOOL inside_bbox_tol(POINT* point, RECT* bbox, int tolerance);
BOOL bbox_overlap(RECT* bbox1, RECT* bbox2);
BOOL bbox_within_bbox(RECT* bbox1, RECT* bbox2);
BOOL is_empty(RECT* bbox);
void make_empty(RECT* bbox);
void make_empty(SRECT* bbox);
BOOL bbox_intersect(RECT* bbox1, RECT* bbox2, RECT* result);
RECT SRECTToRECT(SRECT& source);
