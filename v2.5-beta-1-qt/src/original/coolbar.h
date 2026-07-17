// Ported from v2.5-beta-1-modern/coolbar.h. QMainWindow's top toolbar area
// replaces the Win32 rebar; band ownership and original method boundaries
// remain in CCoolBar/CCoolBarEx.

#pragma once

#include "wincompat.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QToolBar>

class QAction;
class QMainWindow;
class QMenu;

// Values used by the original common-controls toolbar styles.
constexpr DWORD TBSTYLE_CHECK = 0x02;
constexpr DWORD TBSTYLE_GROUP = 0x04;
constexpr DWORD TBSTYLE_CHECKGROUP = TBSTYLE_CHECK | TBSTYLE_GROUP;
constexpr DWORD TBSTYLE_DROPDOWN = 0x08;

class CCoolToolBar : public QToolBar {
public:
    using QToolBar::QToolBar;
};

class CCoolToolBarEx : public CCoolToolBar {
public:
    using CCoolToolBar::CCoolToolBar;

    void ModifyButtonStyle(UINT id, DWORD add, DWORD remove = 0);
    QAction* GetButtonFromID(UINT id) const;
    void SetButtonMenu(UINT id, QMenu* menu);

    BOOL m_bVisible = TRUE;
    UINT m_resourceID = 0;
};

class CCoolBar : public QObject {
public:
    explicit CCoolBar(QObject* parent = nullptr) : QObject(parent) {}
    ~CCoolBar() override = default;

protected:
    QMainWindow* m_parentWnd = nullptr;
};

class CCoolBarEx : public CCoolBar {
public:
    explicit CCoolBarEx(QObject* parent = nullptr);
    ~CCoolBarEx() override = default;

    BOOL Create(QMainWindow* parentWindow, const UINT* toolbarIDs,
                const BOOL* visibility);
    BOOL SaveStateToBuffer(BYTE** bufferOut, UINT* bufferSize) const;
    BOOL SaveStateToBuffer(QByteArray* bufferOut) const;
    BOOL LoadStateFromBuffer(const BYTE* buffer);
    BOOL LoadStateFromBuffer(const QByteArray& buffer);
    void ShowBar(UINT barID, BOOL show = TRUE);
    BOOL IsBarShown(UINT barID) const;
    CCoolToolBarEx* GetToolBarFromID(UINT barID) const;
    int FindBand(UINT id) const;
    void SetWholeBarVisible(BOOL visible);
    BOOL IsWholeBarVisible() const { return m_wholeVisible; }

protected:
    virtual BOOL OnCreateBands();
    virtual CCoolToolBarEx* CreateToolBar(UINT id) = 0;
    virtual void OnPrepareToolBar(UINT id, CCoolToolBarEx* toolbar);
    BOOL AddToolBarBands();
    BOOL AddSingleBand(UINT id, CCoolToolBarEx* toolbar, int width = -1,
                       BOOL breakBefore = FALSE);
    void RestoreBandMinSize(UINT, CCoolToolBarEx*) {}

    QList<UINT> m_defaultIDs;
    QList<BOOL> m_defaultVisibility;
    QHash<UINT, CCoolToolBarEx*> m_mapToolBars;
    QList<UINT> m_bandOrder;
    QSet<UINT> m_breakBefore;
    QHash<UINT, int> m_bandLengths;
    UINT m_nLastShown = 0;
    BOOL m_wholeVisible = TRUE;
};
