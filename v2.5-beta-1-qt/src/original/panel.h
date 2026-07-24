// Ported from v2.5-beta-1-modern/panel.h. Qt containers replace MFC lists;
// panel/page ownership, virtual boundaries and original method names remain.

#pragma once

#include "backdrop.h"
#include "balloon.h"
#include "format.h"
#include "pe.h"

#include <QFont>
#include <QList>
#include <QPointF>
#include <QSize>
#include <QString>

class CBody;
class CAvatarX;
class CChatDoc;
class CPageView;
class QtPaintDC;
class QPainter;

// Qt representation of the state established by the original
// CUnitPanelPage::PreparePrintDC. Coordinates remain MM_TWIPS coordinates:
// x grows right and y grows up from the printable page's upper-left corner.
struct CUnitPanelPrintInfo {
    int m_panelsWide = 0;
    int m_panelsHigh = 0;
    int m_startPanelRow = 0;
    int m_firstPanel = 0;
    POINT m_viewportOrigin{};
    RECT m_clipRect{};
};

class CDamage {
public:
    RECT m_g{};
};

class CEFrame : public CPanelElement {
public:
    short m_frameType = 0;
};

class CPanel {
public:
    CPanel();
    CPanel(const CPanel& source);
    virtual ~CPanel();

    virtual void Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage) = 0;
    virtual void LayoutAvatars() = 0;
    virtual BOOL LayoutBalloons(char** rest, CDWordArray** restFormatting,
                                char** urlStartInRest) = 0;
    virtual BOOL LayoutBalloon(CBalloon* balloons[], int count, int index,
                               RECT& freeRect) = 0;
    virtual RECT GetBalloonRect() = 0;
    CBody* FetchSpeaker(UINT id);
    BOOL ReplaceBody(UINT id);
    virtual BOOL AvatarInPanel(UINT avatarId);
    virtual CPanel* Clone() = 0;
    virtual void OnClickHotLink(UINT, const char*) {}

    QList<CPanelElement*> m_elements;
    QList<CBody*> m_bodies;
    unsigned int m_seed = 0;
    BOOL m_hasBorder = TRUE;
    CBackDrop m_backDrop;
};

class CUnitPanel final : public CPanel {
public:
    static int m_borderWidth;

    CUnitPanel() = default;
    CUnitPanel(const CUnitPanel& source) : CPanel(source) {}
    void Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage) override;
    void DrawBorder(QtPaintDC* dc, RECT* rect);
    void LayoutAvatars() override;
    BOOL LayoutBalloons(char** rest, CDWordArray** restFormatting,
                        char** urlStartInRest) override;
    BOOL LayoutBalloon(CBalloon* balloons[], int count, int index,
                       RECT& freeRect) override;
    void RearrangeBalloons(CBalloon*[], int, RECT&) {}
    RECT GetBalloonRect() override;
    CPanel* Clone() override { return new CUnitPanel(*this); }
    void GetCloudEstimate(CBalloon* balloons[], int count, int index,
                          RECT& freeRect, RECT& balloonRect);
    BOOL IsSpeaker(CBody* body);
    void AdjustArtToCoord(int fixedY, double zoomFactor);
    void OnClickHotLink(UINT link, const char* linkText) override;
};

class CPage {
public:
    CPage() = default;
    virtual ~CPage();
    CPanel* RemoveLastPanel();
    virtual BOOL AddPanel(CPanel* panel) = 0;
    virtual BOOL AddLine(UINT id, const char* text, USHORT modes,
                         CDWordArray* formatting,
                         const char* urlStart = nullptr) = 0;
    virtual void RefreshLastPanel() = 0;
    virtual void RefreshPanelN(int panel) = 0;
    virtual void AddTitle(const char* title) = 0;
    virtual void UpdateTitle() = 0;
    virtual void ShowInfo(USHORT avatarId, const char* info,
                          char hotLinkChar) = 0;
    virtual void GetBBox(RECT* result) = 0;
    virtual void Draw(CPageView* view, QPainter* painter,
                      const CUnitPanelPrintInfo& printInfo,
                      qreal pixelsPerTwipX, qreal pixelsPerTwipY,
                      const QPointF& pageOrigin) = 0;
    virtual CUnitPanelPrintInfo PreparePrintDC(const SIZE& pageSize,
                                               int pageNum) = 0;
    virtual int GetPhysicalPageCount(const SIZE& pageSize) const = 0;
    virtual void StartNewPanel() { m_newPanel = TRUE; }

    short m_pageType = 0;
    RECT m_boundary{};
    QList<CPanel*> m_panels;
    RECT m_bbox{};
    BOOL m_newPanel = TRUE;
};

class CUnitPanelPage final : public CPage {
public:
    static int m_panelsPerRow;
    static int m_panelsPerColumn;
    static int m_printPanelsPerRow;
    static int m_unitWidth;
    static int m_unitHeight;
    static int m_hInterstice;
    static int m_vInterstice;
    static QFont* m_fontBalloon;
    static QFont* m_fontWhisper;
    static QFont* m_fontTitle;
    static QFont* m_fontShout;
    static CFontInfo* m_fiWNormal;
    static CFontInfo* m_fiWWhisper;
    static CFontInfo* m_fiTitle;
    static CFontInfo* m_fiShout;
    static QList<QFont*> m_fonts;
    static QList<CFontInfo*> m_fontInfos;

    explicit CUnitPanelPage(CChatDoc* document = nullptr) : m_doc(document) {}

    BOOL AddPanel(CPanel* panel) override;
    BOOL AddLine(UINT id, const char* text, USHORT modes,
                 CDWordArray* formatting = nullptr,
                 const char* urlStart = nullptr) override;
    void RefreshLastPanel() override;
    void RefreshPanelN(int panel) override;
    void AddTitle(const char* title) override;
    void UpdateTitle() override;
    void ShowInfo(USHORT avatarId, const char* info, char hotLinkChar) override;
    void AddStars(CUnitPanel* panel, int topY);
    void GetBBox(RECT* result) override;
    void Draw(CPageView* view, QPainter* painter,
              const CUnitPanelPrintInfo& printInfo,
              qreal pixelsPerTwipX, qreal pixelsPerTwipY,
              const QPointF& pageOrigin) override;
    CUnitPanelPrintInfo PreparePrintDC(const SIZE& pageSize,
                                       int pageNum) override;
    int GetPhysicalPageCount(const SIZE& pageSize) const override;
    void PageSizeInPanels(const SIZE& pageSize, int& panelsWide,
                          int& panelsHigh) const;
    BOOL AddReaction(UINT id);
    CBalloon* MakeBalloon(const char* message, USHORT modes,
                          CDWordArray* formatting = nullptr,
                          const char* urlStart = nullptr);
    static QSize GetScrollPage();
    static int GetUnitPanelWidth() { return m_unitWidth; }
    static int GetUnitPanelHeight() { return m_unitHeight; }
    static int GetUnitPanelsPerRow() { return m_panelsPerRow; }
    static void SetUnitPanelWidth(int width);
    static void SetUnitPanelHeight(int height) { m_unitHeight = height; }
    static void SetUnitPanelsPerRow(int count) { m_panelsPerRow = count; }
    static BOOL SetFonts(const QFont& font, COLORREF textColor);
    static BOOL UpdateTitleFonts();
    static void DestroyFonts();

    int m_topY = 0;
    int m_leftX = 0;
    CChatDoc* m_doc = nullptr;
};

constexpr int MINUNITPANELWIDTH = 2300;
constexpr int MINUNITPANELHEIGHT = MINUNITPANELWIDTH;

void AddStarsAux(QList<CAvatarX*>& stars, int maxStars);
QString GetRandomTitle();
