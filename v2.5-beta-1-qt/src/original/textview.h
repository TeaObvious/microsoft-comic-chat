// Ported from v2.5-beta-1-modern/textview.h. QTextEdit replaces only the
// RichEdit window; CTextView/CTextEdit names and TextCore call boundaries stay.

#pragma once

#include "textcore.h"

#include <QRectF>
#include <QTextEdit>
#include <QWidget>

class CChatDoc;
class CUserInfo;
class QContextMenuEvent;
class QFocusEvent;
class QKeyEvent;
class QMenu;
class QMouseEvent;
class QPoint;
class QPainter;
class QPrinter;
class QTextDocument;
class QTextEdit;

class CTextView;

class CTextEdit : public QTextEdit {
public:
    explicit CTextEdit(CTextView* owner);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    CTextView* m_owner = nullptr;
};

class CTextView : public QWidget {
public:
    explicit CTextView(CChatDoc* document, QWidget* parent = nullptr);
    ~CTextView() override;

    void TextLine(CUserInfo* puiSender, const char* senderNickname,
                  const char* receiver, const char* line, USHORT modes,
                  BYTE cooked = 0, CDWordArray* formatting = nullptr,
                  char highlightType = -1);
    void ShowInfo(CUserInfo* pui, const char* info);
    void ClearTextView();
    void DisplayPart(const char* nick, char highlightType = -1);
    void DisplayJoin(const char* nick, char highlightType = -1);
    void DisplayNickChange(CUserInfo* pui, const char* oldNick);
    void SetURLBrowser(BOOL newBrowser);
    CChatDoc* GetDocument() const { return m_document; }
    BOOL OnPreparePrinting(QPrinter* printer) const;
    void OnBeginPrinting(QPrinter* printer);
    void OnPrint(QPrinter* printer, QPainter* painter, UINT pageNumber);
    void OnEndPrinting(QPrinter* printer);
    long lPrintPage(QPrinter* printer, QPainter* painter,
                    UINT currentPage, BOOL display);
    void PrintFooter(QPainter* painter, const QRectF& pageRect,
                     UINT pageNumber) const;

    CTextEdit* m_pRichEdit = nullptr;
    BOOL m_bFirstTime = TRUE;
    QFont* m_pFooterFont = nullptr;
    int m_lineHeight = 0;
    QFont* m_fontText = nullptr;
    CTextCore m_textCore;
    QTextDocument* m_printDocument = nullptr;
    QRectF m_printTextRect;
    int m_printPageCount = 0;

protected:
    virtual int LoadContextMenu(QMenu& menu);
    void ShowContextMenu(const QPoint& globalPosition);

private:
    friend class CTextEdit;

    void ExecuteContextCommand(const QString& commandIdentifier);
    void PreparePrintDocument(QPrinter* printer);

    CChatDoc* m_document = nullptr;
};

void InitializeTextCore(CTextCore* textCore, BOOL resetOld = FALSE,
                        BOOL resetSay = FALSE);
void InitializeTextCores(BOOL resetOld, BOOL resetSay = FALSE);
CTextView* GetTextView();
BOOL WriteRTF(QTextEdit* richEdit, const QString& fileName);
