// Ported from v2.5-beta-1-modern/rtfctrl.h.

#pragma once

#include "doskey.h"

#include <QPoint>
#include <QString>
#include <QTextEdit>

#include <functional>

class QAction;
class QContextMenuEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;

class CAccelTable
{
public:
    explicit CAccelTable(const QString& resourceIdentifier)
        : m_resourceIdentifier(resourceIdentifier) {}

    QString Lookup(const QKeyEvent* event) const;

protected:
    QString m_resourceIdentifier;
};

class CRtfCtrl : public QTextEdit
{
public:
    explicit CRtfCtrl(QWidget* parent = nullptr);
    ~CRtfCtrl() override;

    static SHORT m_nFixedPitchIndex;
    static SHORT m_nSymbolIndex;

    BOOL m_bAcceptMultiLine = TRUE;
    BOOL m_bSelectAll = TRUE;
    BOOL m_bColorWnd = FALSE;
    BOOL m_bRerouteMenuInit = FALSE;
    UINT m_nPopupMenuIndex = 1;
    CDWordArray* m_prgdwFormatting = nullptr;
    QFont* m_pFont = nullptr;
    COLORREF m_crTextColor = RGB(0, 0, 0);
    QString m_strText;
    CDosKey* m_pDosKey = nullptr;
    WORD m_wMenuFormatStyles = 0;

    void DefineDefaultCharFormat();
    void UseDefaultCharFormat(BOOL updateSelection = FALSE);
    void SwitchSelectionFormat(WORD format);
    void MatchButtonsToSelection();
    void ShowFormattingPopUp(LONG x, LONG y);
    void ShowFormattingPopUp(const QPoint& globalPoint);
    void SetDosKey(CDosKey* dosKey) { m_pDosKey = dosKey; }
    CDosKey* GetDosKey() const { return m_pDosKey; }
    void SetAcceptMultiLine(BOOL acceptMultiLine) { m_bAcceptMultiLine = acceptMultiLine; }
    BOOL bSetWindowFormattedText(const QString& text, CDWordArray* formatting);
    BOOL bSetTextColor(COLORREF textColor);
    BOOL bSetIndent(LONG indent);
    BOOL bShowDosKeyEntry(BOOL previous);
    WORD wGetConsistentFormats() const;
    bool IsEmpty() const;

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QTextCharFormat defaultCharFormat() const;
    QTextCharFormat charFormatAt(int position) const;
    void executeCommand(const QString& commandIdentifier);
    bool selectionHasProperty(const std::function<bool(const QTextCharFormat&)>& predicate) const;

    QFont m_font;
};
