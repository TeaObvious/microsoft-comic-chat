// Ported from v2.5-beta-1-modern/balloon.h. QFont/QPainter replace GDI font
// and CDC operations; class names, layout fields and virtual boundaries remain.

#pragma once

#include "format.h"
#include "pe.h"

#include <QFont>

class QPainter;

constexpr UCHAR FT_LEFT_JUSTIFY = 1;
constexpr int MAXLINES = 10;

class CBody;
class CPanel;
class CSpline;
class CTraj;
class QtPaintDC;

class CFormatInfo {
public:
    UCHAR m_nLines = 0;
    int m_rgiLengths[MAXLINES]{};
    int m_rgiWidths[MAXLINES]{};
    int m_iMaxWidth = 0;
    char* m_rgszStarts[MAXLINES]{};
    SRECT m_bbox{};
    int m_rgiLeftX[MAXLINES]{};
};

class CArrow {
public:
    CArrow() = default;
    CArrow(const CArrow& source);
    virtual ~CArrow() = default;
    virtual void Draw(QtPaintDC* dc, int x, int y, RECT* damage) = 0;
    virtual void GetPoints(SRECT* bbox, POINT& low, POINT& middle, POINT& high) = 0;
    virtual CArrow* Clone() = 0;

    POINT m_lo{}, m_hi{}, m_mid{};
};

class CFontInfo {
public:
    CFontInfo(QFont* font, COLORREF defaultForeground, short leading, short baseAdd);

    QFont* m_font = nullptr;
    COLORREF m_crDefaultForeColor = RGB(0, 0, 0);
    short m_leading = 0;
    short m_lineHeight = 0;
    short m_baseAdd = 0;
    short m_continuationWidth = 0;
    short m_topOffset = 0;
};

class CLabel : public CPanelElement {
public:
    CLabel(const CLabel& source);
    CLabel(const char* text, CFontInfo* fontInfo,
           CDWordArray* formatting = nullptr);
    ~CLabel() override;

    int BreakIntoLines(CFormatInfo& info);
    void ShiftLines(CFormatInfo& info);
    BOOL bURLHit(int leftX, int baseY, const QSize& size);
    int iDrawFormattedTextLine(QPainter& painter, int leftX, int baseY,
                               const char* chunk, int chunkLength,
                               WORD format, BOOL* urlHit);
    void DrawFormattedText(QPainter& painter, const CFormatInfo& info,
                           int startTop, int* url = nullptr);
    void Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage) override;
    void GetBBox(RECT* result) override;
    int AreaEstimate(int* length, int* lineHeight);
    int WidestWord();
    virtual int GetLeading() { return m_fontI->m_leading; }
    virtual char* SplitHeight(int height, CDWordArray** restFormatting,
                              char** urlStartInRest = nullptr);
    virtual void OnLButtonDown(POINT&, CPanel*) {}
    void CreateURLArray(const char* text, CDWordArray* formatting,
                        const char* urlStart, char*** urls);
    void GetFormatInfoCommon(CFormatInfo* info);

    CFontInfo* m_fontI = nullptr;
    char* m_str = nullptr;
    UCHAR m_format = 0;
    CDWordArray* m_prgdwFormatting = nullptr;

protected:
};

class CStarLabel final : public CLabel {
public:
    CStarLabel(const char* text, CFontInfo* fontInfo) : CLabel(text, fontInfo) {}
    void Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage) override;
};

class CHotLinkLabel final : public CLabel {
public:
    CHotLinkLabel(const CHotLinkLabel& source);
    CHotLinkLabel(const char* text, CFontInfo* fontInfo,
                  CDWordArray* formatting = nullptr);
    ~CHotLinkLabel() override;
    void OnLButtonDown(POINT& point, CPanel* panel) override;

    char** m_prgszURLs = nullptr;
};

class CBalloon : public CLabel {
public:
    CBalloon(const char* text, CFontInfo* fontInfo, CDWordArray* formatting,
             const char* urlStart);
    CBalloon(const CBalloon& source);
    ~CBalloon() override;

    virtual void DockAtTop(int height);
    BOOL Overlap(CBalloon* other);
    void GetCloudBBox(RECT* result);
    void GetCloudBBox(SRECT* result);
    virtual BOOL ComputeInternals() = 0;
    virtual void ComputeCloudBBox();
    BOOL SetBBox(int left, int bottom, int right, int top) override;
    void GetBBox(RECT* result) override;
    virtual void InMyCoords(SRECT* bbox);
    virtual void QueryRouteRgn(int otherToX, int& leftAllowance,
                               int& rightAllowance);
    virtual void SetRouteRgn(int otherToX, int left, int right);
    int GetType() override { return PE_BALLOON; }
    virtual char* SplitHeight(int height, CDWordArray** restFormatting,
                              char** urlStartInRest = nullptr) override = 0;
    virtual void DrawText(QPainter& painter);
    virtual void OnLButtonDown(POINT& point, CPanel* panel) override;
    virtual CBalloon* Clone() = 0;

    CBody* m_speaker = nullptr;
    CSpline* m_spline = nullptr;
    CFormatInfo* m_fInfo = nullptr;
    SRECT m_trueBox{};
    SRECT m_routeRgn{};
    CTraj* m_traj = nullptr;
    char** m_prgszURLs = nullptr;
};

class CBWoodringNormal : public CBalloon {
public:
    CBWoodringNormal(const char* text, CDWordArray* formatting,
                     const char* urlStart, BYTE dashed = 0);
    void Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage) override;
    virtual CSpline* CreateBalloonSpline(CFormatInfo& info);
    CSpline* GetBalloonSpline();
    BOOL ComputeInternals() override;
    CBalloon* Clone() override { return new CBWoodringNormal(*this); }
    virtual void AddArrow(CBalloon* balloon, CSpline* spline, CFormatInfo& info);
    virtual void SetBalloonTraj();
    char* SplitHeight(int height, CDWordArray** restFormatting,
                      char** urlStartInRest = nullptr) override;

    BYTE m_byteDashed = 0;
};

class CBWoodringWhisper final : public CBWoodringNormal {
public:
    CBWoodringWhisper(const char* text, CDWordArray* formatting,
                      const char* urlStart);
    CBalloon* Clone() override { return new CBWoodringWhisper(*this); }
};

class CBWoodringThink final : public CBWoodringNormal {
public:
    CBWoodringThink(const char* text, CDWordArray* formatting,
                    const char* urlStart);
    void Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage) override;
    CBalloon* Clone() override { return new CBWoodringThink(*this); }
    void AddArrow(CBalloon*, CSpline*, CFormatInfo&) override {}
};

class CBWoodringBox final : public CBWoodringNormal {
public:
    CBWoodringBox(const char* text, CDWordArray* formatting,
                  const char* urlStart, BYTE dashed = 0);
    CSpline* CreateBalloonSpline(CFormatInfo&) override { return nullptr; }
    void AddArrow(CBalloon*, CSpline*, CFormatInfo&) override {}
    void SetBalloonTraj() override;
    void ComputeCloudBBox() override;
    CBalloon* Clone() override { return new CBWoodringBox(*this); }
    void GetBBox(RECT* result) override;
    void QueryRouteRgn(int otherToX, int& leftAllowance,
                       int& rightAllowance) override;
    void SetRouteRgn(int, int, int) override {}
    int GetType() override { return PE_BALLOON | PE_BOX; }
};

double randfloat();
