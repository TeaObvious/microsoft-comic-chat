// Ported from v2.5-beta-1-modern/chatview.h.

#pragma once

#include "spltchat.h"
#include "wincompat.h"

#include <QWidget>

class CChatDoc;
class CMemberList;
class CPageView;
class CSayWnd;
class CBodyCam;
class QSplitter;
class QSplitterHandle;
class QPainter;
class QPrinter;

class CFixedSplitter : public CSplitSay {
public:
    explicit CFixedSplitter(QWidget* parent = nullptr);
    ~CFixedSplitter() override = default;

protected:
    QSplitterHandle* createHandle() override;
};

class CChatView : public QWidget {
public:
    explicit CChatView(CChatDoc* doc, QWidget* parent = nullptr);
    ~CChatView() override;

    void CreateComicView(bool doUpdate);
    void CreateTextView(bool doUpdate);
    void CreateStatusView();
    QWidget* GetPrimaryView() const;
    BOOL OnPreparePrinting(QPrinter* printer);
    void OnBeginPrinting(QPrinter* printer);
    void OnEndPrinting(QPrinter* printer);
    void OnPrint(QPrinter* printer, QPainter* painter, UINT pageNumber);
    void OnPrepareDC(QPrinter* printer, UINT pageNumber);
    int GetPhysicalPageCount(QPrinter* printer);
    BOOL OnFilePrint(QPrinter* printer, BOOL direct = FALSE);
    BOOL Print(QPrinter* printer);

private:
    void clearLayout();

    CChatDoc* m_doc = nullptr;
    QSplitter* m_wndSplitter = nullptr;
    CFixedSplitter* m_wndLSplitter = nullptr;
    CSplitChat* m_wndRSplitter = nullptr;
};
