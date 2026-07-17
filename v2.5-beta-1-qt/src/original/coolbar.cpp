// Ported from v2.5-beta-1-modern/coolbar.cpp and the generic CCoolBarEx
// band mechanics in chatbars.cpp. Qt replaces only rebar window messages.

#include "coolbar.h"

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace {
constexpr BYTE CBBSV_NEWLINE = 1;
constexpr int SavedBandSize = 5;

struct SavedBand {
    UINT id = 0;
    UINT length = 0;
    BYTE flags = 0;
};

void appendWord(QByteArray& output, UINT value)
{
    output.append(static_cast<char>(value & 0xff));
    output.append(static_cast<char>((value >> 8) & 0xff));
}

UINT readWord(const QByteArray& input, qsizetype offset)
{
    return static_cast<BYTE>(input[offset])
        | (static_cast<UINT>(static_cast<BYTE>(input[offset + 1])) << 8);
}

QList<SavedBand> decodeState(const QByteArray& input, BOOL* valid)
{
    QList<SavedBand> result;
    BOOL foundSentinel = FALSE;
    if (input.size() % SavedBandSize != 0) {
        if (valid) *valid = FALSE;
        return result;
    }
    for (qsizetype offset = 0; offset + SavedBandSize <= input.size();
         offset += SavedBandSize) {
        SavedBand band{readWord(input, offset), readWord(input, offset + 2),
                       static_cast<BYTE>(input[offset + 4])};
        if (band.id == 0) {
            foundSentinel = band.length == 0 && band.flags == 0;
            break;
        }
        result.append(band);
    }
    if (valid) *valid = foundSentinel;
    return result;
}
}

QAction* CCoolToolBarEx::GetButtonFromID(UINT id) const
{
    for (QAction* action : actions()) {
        if (action && action->property("originalCommandID").toUInt() == id)
            return action;
    }
    return nullptr;
}

void CCoolToolBarEx::ModifyButtonStyle(UINT id, DWORD add, DWORD remove)
{
    QAction* action = GetButtonFromID(id);
    if (!action) return;
    DWORD style = action->property("originalButtonStyle").toUInt();
    style = (style & ~remove) | add;
    action->setProperty("originalButtonStyle", style);
    action->setCheckable((style & TBSTYLE_CHECK) != 0);
}

void CCoolToolBarEx::SetButtonMenu(UINT id, QMenu* menu)
{
    QAction* action = GetButtonFromID(id);
    if (!action || !menu) return;
    action->setMenu(menu);
    action->setProperty("originalButtonStyle",
                        action->property("originalButtonStyle").toUInt()
                            | TBSTYLE_DROPDOWN);
    QTimer::singleShot(0, this, [this, action] {
        if (auto* button = qobject_cast<QToolButton*>(widgetForAction(action)))
            button->setPopupMode(QToolButton::MenuButtonPopup);
    });
}

CCoolBarEx::CCoolBarEx(QObject* parent)
    : CCoolBar(parent)
{
}

BOOL CCoolBarEx::Create(QMainWindow* parentWindow, const UINT* toolbarIDs,
                        const BOOL* visibility)
{
    if (!parentWindow || !toolbarIDs || !visibility) return FALSE;
    m_parentWnd = parentWindow;
    m_defaultIDs.clear();
    m_defaultVisibility.clear();
    for (int index = 0; toolbarIDs[index] != 0; ++index) {
        m_defaultIDs.append(toolbarIDs[index]);
        m_defaultVisibility.append(visibility[index]);
    }
    m_wholeVisible = std::any_of(
        m_defaultVisibility.cbegin(), m_defaultVisibility.cend(),
        [](BOOL shown) { return shown != FALSE; });
    return OnCreateBands();
}

BOOL CCoolBarEx::OnCreateBands()
{
    if (!m_parentWnd || m_defaultIDs.isEmpty()
        || m_defaultIDs.size() != m_defaultVisibility.size()) {
        return FALSE;
    }
    for (int index = 0; index < m_defaultIDs.size(); ++index) {
        const UINT id = m_defaultIDs[index];
        CCoolToolBarEx* toolbar = CreateToolBar(id);
        if (!toolbar) return FALSE;
        toolbar->m_resourceID = id;
        toolbar->m_bVisible = m_defaultVisibility[index];
        toolbar->setAllowedAreas(Qt::TopToolBarArea);
        toolbar->setFloatable(false);
        toolbar->setMovable(true);
        m_mapToolBars.insert(id, toolbar);
        OnPrepareToolBar(id, toolbar);
    }
    return AddToolBarBands();
}

void CCoolBarEx::OnPrepareToolBar(UINT, CCoolToolBarEx*)
{
}

BOOL CCoolBarEx::AddSingleBand(UINT id, CCoolToolBarEx* toolbar, int width,
                               BOOL breakBefore)
{
    if (!m_parentWnd || !toolbar) return FALSE;
    if (breakBefore) {
        m_parentWnd->addToolBarBreak(Qt::TopToolBarArea);
        m_breakBefore.insert(id);
    } else {
        m_breakBefore.remove(id);
    }
    m_parentWnd->addToolBar(Qt::TopToolBarArea, toolbar);
    if (width >= 0 && width != 0xffff) {
        m_bandLengths.insert(id, width);
        toolbar->resize(width, toolbar->sizeHint().height());
    } else {
        m_bandLengths.insert(id, toolbar->sizeHint().width());
    }
    toolbar->setVisible(m_wholeVisible && toolbar->m_bVisible);
    if (toolbar->m_bVisible) m_nLastShown = id;
    return TRUE;
}

BOOL CCoolBarEx::AddToolBarBands()
{
    m_bandOrder.clear();
    m_breakBefore.clear();
    for (UINT id : std::as_const(m_defaultIDs)) {
        CCoolToolBarEx* toolbar = m_mapToolBars.value(id);
        if (!toolbar || !AddSingleBand(id, toolbar)) return FALSE;
        m_bandOrder.append(id);
    }
    return TRUE;
}

int CCoolBarEx::FindBand(UINT id) const
{
    return m_bandOrder.indexOf(id);
}

void CCoolBarEx::ShowBar(UINT barID, BOOL show)
{
    CCoolToolBarEx* toolbar = m_mapToolBars.value(barID);
    if (!toolbar || toolbar->m_bVisible == show) return;
    toolbar->m_bVisible = show;
    toolbar->setVisible(m_wholeVisible && show);
    if (show) m_nLastShown = barID;
    else if (m_nLastShown == barID) m_nLastShown = 0;
}

void CCoolBarEx::SetWholeBarVisible(BOOL visible)
{
    m_wholeVisible = visible;
    for (CCoolToolBarEx* toolbar : std::as_const(m_mapToolBars)) {
        if (toolbar) toolbar->setVisible(visible && toolbar->m_bVisible);
    }
}

BOOL CCoolBarEx::IsBarShown(UINT barID) const
{
    CCoolToolBarEx* toolbar = m_mapToolBars.value(barID);
    return toolbar ? toolbar->m_bVisible : FALSE;
}

CCoolToolBarEx* CCoolBarEx::GetToolBarFromID(UINT barID) const
{
    return m_mapToolBars.value(barID);
}

BOOL CCoolBarEx::SaveStateToBuffer(QByteArray* bufferOut) const
{
    if (!bufferOut) return FALSE;
    bufferOut->clear();
    for (UINT id : m_bandOrder) {
        CCoolToolBarEx* toolbar = m_mapToolBars.value(id);
        if (!toolbar) return FALSE;
        int width = toolbar->width();
        if (width <= 0) width = m_bandLengths.value(id, toolbar->sizeHint().width());
        width = std::clamp(width, 0, 0xffff);
        appendWord(*bufferOut, id);
        appendWord(*bufferOut, static_cast<UINT>(width));
        bufferOut->append(static_cast<char>(
            m_breakBefore.contains(id) ? CBBSV_NEWLINE : 0));
    }
    bufferOut->append(QByteArray(SavedBandSize, '\0'));
    return TRUE;
}

BOOL CCoolBarEx::SaveStateToBuffer(BYTE** bufferOut, UINT* bufferSize) const
{
    if (!bufferOut || !bufferSize) return FALSE;
    QByteArray state;
    if (!SaveStateToBuffer(&state)) return FALSE;
    BYTE* bytes = static_cast<BYTE*>(std::malloc(state.size()));
    if (!bytes && !state.isEmpty()) return FALSE;
    if (!state.isEmpty()) std::memcpy(bytes, state.constData(), state.size());
    *bufferOut = bytes;
    *bufferSize = static_cast<UINT>(state.size());
    return TRUE;
}

BOOL CCoolBarEx::LoadStateFromBuffer(const BYTE* buffer)
{
    if (!buffer) return FALSE;
    QByteArray state;
    for (int record = 0; record <= m_mapToolBars.size(); ++record) {
        const char* bytes = reinterpret_cast<const char*>(
            buffer + record * SavedBandSize);
        state.append(bytes, SavedBandSize);
        if (buffer[record * SavedBandSize] == 0
            && buffer[record * SavedBandSize + 1] == 0) {
            break;
        }
    }
    return LoadStateFromBuffer(state);
}

BOOL CCoolBarEx::LoadStateFromBuffer(const QByteArray& buffer)
{
    if (buffer.isEmpty()) return FALSE;
    BOOL valid = FALSE;
    const QList<SavedBand> saved = decodeState(buffer, &valid);
    if (!valid || saved.isEmpty()) return FALSE;

    QSet<UINT> seen;
    for (const SavedBand& band : saved) {
        if (!m_mapToolBars.contains(band.id) || seen.contains(band.id))
            return FALSE;
        seen.insert(band.id);
    }

    for (CCoolToolBarEx* toolbar : std::as_const(m_mapToolBars)) {
        if (!toolbar) continue;
        m_parentWnd->removeToolBarBreak(toolbar);
        m_parentWnd->removeToolBar(toolbar);
    }
    m_bandOrder.clear();
    m_breakBefore.clear();
    for (const SavedBand& band : saved) {
        CCoolToolBarEx* toolbar = m_mapToolBars.value(band.id);
        if (!AddSingleBand(band.id, toolbar, static_cast<int>(band.length),
                           (band.flags & CBBSV_NEWLINE) != 0)) {
            return FALSE;
        }
        m_bandOrder.append(band.id);
    }
    return TRUE;
}
