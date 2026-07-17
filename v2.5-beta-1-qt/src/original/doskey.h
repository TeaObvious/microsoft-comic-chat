//=--------------------------------------------------------------------------=
// DosKey.H
//=--------------------------------------------------------------------------=
// Qt container boundary for v2.5-beta-1-modern/doskey.h.

#pragma once

#include "format.h"

#include <QString>
#include <QVector>

const UINT g_uDefDosKeySize = 64;

class CDosKey
{
public:
    CDosKey();
    virtual ~CDosKey();

    void ResetContent();
    void SetMaxSize(UINT uMaxSize);
    void SeekToEnd();

    QString StrGetNextEntry(CDWordArray** pprgdwFormatting);
    QString StrGetPrevEntry(CDWordArray** pprgdwFormatting);

    BOOL bAppendEntry(const QString& strEntry, CDWordArray* prgdwFormatting);

protected:
    QVector<QString> m_rgstrEntries;
    QVector<CDWordArray*> m_rgpFormatting;
    UINT m_uMaxSize;
    INT m_iStartIndex;
    INT m_iEndIndex;
    INT m_iCurIndex;
    BOOL m_bEndReached;
};
