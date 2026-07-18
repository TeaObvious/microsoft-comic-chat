// Ported from v2.5-beta-1-modern/query.cpp.

#include "query.h"

#include "ccommon.h"
#include "notif.h"
#include "rules.h"

CCQuery::CCQuery(enumQueryPurpose qp, enumCommandType ct, enumDataType dt, void* pvData,
                 const QString& channelName, const QString& nicknameMask,
                 BOOL createPrUserMatch)
    : m_qp(qp)
    , m_ct(ct)
    , m_dt(dt)
    , m_pvData(pvData)
    , m_strChannelName(channelName)
    , m_strNicknameMask(nicknameMask)
{
    if (m_dt == dtRule && m_pvData) {
        static_cast<CCRule*>(m_pvData)->AddRef();
    } else if (m_dt == dtNotif && m_pvData) {
        static_cast<CCNotif*>(m_pvData)->AddRef();
    }
    if (createPrUserMatch && !m_strNicknameMask.isEmpty()) {
        m_pPrUserMatch = new PRUSERMATCH;
        QByteArray mask;
        if (!bWideToCodePage(QStringView(m_strNicknameMask), GetACP(),
                             &mask)) {
            mask = m_strNicknameMask.toLatin1();
        }
        bGetUserMatchFromMask(mask.constData(), m_pPrUserMatch);
    }
}

CCQuery::~CCQuery()
{
    delete m_pPrUserMatch;
    if (m_dt == dtRule && m_pvData) {
        static_cast<CCRule*>(m_pvData)->Release();
    } else if (m_dt == dtNotif && m_pvData) {
        static_cast<CCNotif*>(m_pvData)->Release();
    }
}

CQueryPtrList::~CQueryPtrList()
{
    FreeRemoveAll();
}

bool CQueryPtrList::bAddQuery(CCQuery* query)
{
    if (!query) {
        return false;
    }
    m_queries.append(query);
    return true;
}

void CQueryPtrList::FreeRemoveAll()
{
    qDeleteAll(m_queries);
    m_queries.clear();
}

bool CQueryPtrList::FreeRemoveAt(int index)
{
    if (index < 0 || index >= m_queries.size()) {
        return false;
    }
    delete m_queries.takeAt(index);
    return true;
}

CCQuery* CQueryPtrList::RemoveAt(int index)
{
    if (index < 0 || index >= m_queries.size()) return nullptr;
    return m_queries.takeAt(index);
}

CCQuery* CQueryPtrList::FindQuery(enumCommandType ct, int* index, LONG* rank)
{
    if (index) {
        *index = -1;
    }
    if (rank) {
        *rank = 0;
    }
    for (int i = 0; i < m_queries.size(); ++i) {
        if (m_queries[i]->GetCommandType() == ct) {
            if (index) {
                *index = i;
            }
            if (rank) {
                *rank = static_cast<LONG>(i + 1);
            }
            return m_queries[i];
        }
    }
    return nullptr;
}
