// Ported from v2.5-beta-1-modern/pageview.h. QAbstractScrollArea replaces
// CScrollView; the document-owned CPage/CPanel graph remains authoritative.

#pragma once

#include "panel.h"
#include "wincompat.h"

#include <QAbstractScrollArea>
#include <QRectF>

class CChatDoc;
class CLabel;
class CPanel;
class CUserInfo;
class QEvent;
class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;
class QFont;
class QImage;
class QPainter;
class QPrinter;

class CPageView final : public QAbstractScrollArea {
public:
    explicit CPageView(QWidget* parent = nullptr);
    CPageView(CChatDoc* document, QWidget* parent);
    ~CPageView() override;

    void ResetExistingPanels(BOOL addPage = TRUE);
    void UpdateTitle();
    void ClearHistory();
    void RefreshPanelN(int panel);
    void SetPanelsWide(int wide, int forcedPanelWidth = 0);
    int GetProspectivePanelWidth(int wide) const;
    int FitPanelsWide() const;
    BOOL AtBottom() const;
    void UpdateScroll();
    void ScrollToBottom();
    unsigned int FindAvatarUnderPoint(POINT point);
    void* FindLabelUnderPoint(POINT point, POINT& panelPoint,
                              void*& panel);
    BOOL OnPreparePrinting(QPrinter* printer) const;
    void OnBeginPrinting(QPrinter* printer);
    void OnPrepareDC(QPrinter* printer, UINT pageNumber);
    void OnPrint(QPrinter* printer, QPainter* painter, UINT pageNumber);
    void OnEndPrinting(QPrinter* printer);
    void PrintFooter(QPainter* painter, const QRectF& pageRect,
                     UINT pageNumber, qreal dpiX, qreal dpiY) const;
    int GetPhysicalPageCount(QPrinter* printer) const;
    QImage* GetPrintRetainedPanel() const { return m_printRetainedPanel; }
    void FreeRetainedPanelP();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void OnContextMenu(QContextMenuEvent* event);

private:
    QRect panelDeviceRect(int panel) const;
    BOOL panelPointFromDevice(POINT point, int panel, POINT& panelPoint) const;
    void updateScrollRanges();
    void ScheduleAutoFitPanels();
    void AutoFitPanels();
    qreal logicalUnitsPerPixelX() const;
    qreal logicalUnitsPerPixelY() const;

    CChatDoc* m_doc = nullptr;
    BOOL m_bFirstTime = TRUE;
    BOOL m_bAtBottom = TRUE;
    BOOL m_bAutoFitting = FALSE;
    BOOL m_bAutoFitPending = FALSE;
    QFont* m_footerFont = nullptr;
    QImage* m_printRetainedPanel = nullptr;
    CPage* m_printPage = nullptr;
    CUnitPanelPrintInfo m_printInfo;
    SIZE m_printPageSize{};
    UINT m_printPhysicalPage = 0;
};

extern BOOL g_bNewedPanel;
extern CUserInfo* mousedPui;
void UpdateTitle(CChatDoc* document);
