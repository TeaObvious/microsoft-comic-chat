//=--------------------------------------------------------------------------=
// Doskey.Cpp: implementation of the original input-history ring.
//=--------------------------------------------------------------------------=

#include "doskey.h"

#include <QtGlobal>

CDosKey::CDosKey()
{
    m_iStartIndex = -1;
    m_iEndIndex = m_iCurIndex = 0;
    m_uMaxSize = g_uDefDosKeySize;
    m_bEndReached = TRUE;
}

CDosKey::~CDosKey()
{
    ResetContent();
}

void CDosKey::SetMaxSize(UINT uMaxSize)
{
    ResetContent();
    m_uMaxSize = uMaxSize;
}

void CDosKey::ResetContent()
{
    m_rgstrEntries.clear();
    for (CDWordArray*& formatting : m_rgpFormatting)
        FreeAndNullFormatting(&formatting);
    m_rgpFormatting.clear();

    m_iStartIndex = -1;
    m_iEndIndex = m_iCurIndex = 0;
    m_bEndReached = TRUE;
}

void CDosKey::SeekToEnd()
{
    m_iCurIndex = m_iEndIndex;
    m_bEndReached = TRUE;
}

QString CDosKey::StrGetNextEntry(CDWordArray** pprgdwFormatting)
{
    Q_ASSERT(pprgdwFormatting);
    const INT iIndex = static_cast<INT>((m_iCurIndex + 1) % m_uMaxSize);

    if (m_bEndReached || iIndex == m_iEndIndex) {
        m_iCurIndex = m_iEndIndex;
        m_bEndReached = TRUE;
        *pprgdwFormatting = nullptr;
        return {};
    }

    Q_ASSERT(iIndex < m_rgstrEntries.size());
    m_iCurIndex = iIndex;
    m_bEndReached = FALSE;
    *pprgdwFormatting = m_rgpFormatting.at(iIndex);
    return m_rgstrEntries.at(iIndex);
}

QString CDosKey::StrGetPrevEntry(CDWordArray** pprgdwFormatting)
{
    Q_ASSERT(pprgdwFormatting);

    if (m_iCurIndex == m_iStartIndex && !m_bEndReached) {
        Q_ASSERT(m_iStartIndex >= 0 && m_iStartIndex < m_rgstrEntries.size());
        Q_ASSERT(m_iStartIndex >= 0 && m_iStartIndex < m_rgpFormatting.size());
        *pprgdwFormatting = m_rgpFormatting.at(m_iStartIndex);
        return m_rgstrEntries.at(m_iStartIndex);
    }

    if (m_iStartIndex < 0) {
        *pprgdwFormatting = nullptr;
        return {};
    }

    // m_uMaxSize is unsigned in the original. Its usual arithmetic conversion
    // turns -1 into UINT_MAX before modulo, yielding the final ring slot.
    const INT iIndex = static_cast<INT>((m_iCurIndex - 1) % m_uMaxSize);
    m_iCurIndex = iIndex;
    m_bEndReached = FALSE;
    Q_ASSERT(iIndex >= 0 && iIndex < m_rgstrEntries.size());
    Q_ASSERT(iIndex >= 0 && iIndex < m_rgpFormatting.size());
    *pprgdwFormatting = m_rgpFormatting.at(iIndex);
    return m_rgstrEntries.at(iIndex);
}

BOOL CDosKey::bAppendEntry(const QString& strEntry, CDWordArray* prgdwFormatting)
{
    if (static_cast<UINT>(m_rgstrEntries.size()) < m_uMaxSize) {
        const INT iNewEntry = m_rgstrEntries.size();
        m_rgstrEntries.append(strEntry);
        Q_ASSERT(m_iEndIndex == iNewEntry);
        const INT iNewFormatting = m_rgpFormatting.size();
        m_rgpFormatting.append(CopyFormatting(prgdwFormatting));
        Q_ASSERT(m_iEndIndex == iNewFormatting);
        if (m_iStartIndex < 0) m_iStartIndex = 0;
    } else {
        Q_ASSERT(static_cast<UINT>(m_rgstrEntries.size()) == m_uMaxSize);
        Q_ASSERT(m_iEndIndex >= 0);
        Q_ASSERT(static_cast<UINT>(m_iEndIndex) < m_uMaxSize);
        m_rgstrEntries[m_iEndIndex] = strEntry;
        FreeAndNullFormatting(&m_rgpFormatting[m_iEndIndex]);
        m_rgpFormatting[m_iEndIndex] = CopyFormatting(prgdwFormatting);
        m_iStartIndex = static_cast<INT>((m_iStartIndex + 1) % m_uMaxSize);
    }

    m_iEndIndex = static_cast<INT>((m_iEndIndex + 1) % m_uMaxSize);
    m_iCurIndex = m_iEndIndex;
    m_bEndReached = TRUE;
    return TRUE;
}
