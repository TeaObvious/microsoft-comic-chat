// Ported from v2.5-beta-1-modern/query.h.

#pragma once

#include "ccomp.h"

#include <QList>
#include <QString>

class CCRule;
class CCNotif;

enum enumQueryPurpose {
    qpBanDlg,
    qpComSetChannelMode,
    qpComSetUserMode,
    qpCreatePics,
    qpGetIdent,
    qpIgnoreIdent,
    qpInitialLUsersMOTD,
    qpInitialMode,
    qpInitialNames,
    qpInitialTopic,
    qpInitialWho,
    qpIrcX,
    qpIsIrcX,
    qpJoinBackUrl,
    qpJoinPics,
    qpKickDlg,
    qpListMembers,
    qpLUsersMOTD,
    qpOnConnectEvent,
    qpOnDisconnectEvent,
    qpOnNewRoomEvent,
    qpOnNotification,
    qpRoomListDlg,
    qpSetClient,
    qpSetInvisible,
    qpSetTopic,
    qpSetVisible,
    qpUserListDlg,
    qpMax
};

enum enumCommandType {
    ctGetChannelMode,
    ctIrcX,
    ctList,
    ctListX,
    ctLUsersMOTD,
    ctModeIsIrcX,
    ctNames,
    ctPropGet,
    ctPropSet,
    ctSetChannelMode,
    ctSetUserMode,
    ctTopic,
    ctWho,
    ctWhoIs,
    ctMax
};

enum enumDataType {
    dtFlags,
    dtNotif,
    dtRule,
    dtUser,
    dtMax
};

class CCQuery {
public:
    CCQuery() = default;
    CCQuery(enumQueryPurpose qp, enumCommandType ct, enumDataType dt, void* pvData,
            const QString& channelName, const QString& nicknameMask,
            BOOL createPrUserMatch = FALSE);
    ~CCQuery();

    void SetQueryPurpose(enumQueryPurpose qp) { m_qp = qp; }
    void SetCommandType(enumCommandType ct) { m_ct = ct; }
    void SetDataType(enumDataType dt) { m_dt = dt; }
    void SetData(void* data) { m_pvData = data; }
    void SetChannelName(const QString& channelName)
        { m_strChannelName = channelName; }
    void SetNicknameMask(const QString& nicknameMask)
        { m_strNicknameMask = nicknameMask; }
    enumQueryPurpose GetQueryPurpose() const { return m_qp; }
    enumCommandType GetCommandType() const { return m_ct; }
    enumDataType GetDataType() const { return m_dt; }
    QString GetChannelName() const { return m_strChannelName; }
    QString GetNicknameMask() const { return m_strNicknameMask; }
    void* GetData() const { return m_pvData; }
    PPRUSERMATCH GetPrUserMatch() const { return m_pPrUserMatch; }

private:
    enumQueryPurpose m_qp = qpMax;
    enumCommandType m_ct = ctMax;
    enumDataType m_dt = dtMax;
    void* m_pvData = nullptr;
    QString m_strChannelName;
    QString m_strNicknameMask;
    PPRUSERMATCH m_pPrUserMatch = nullptr;
};

class CQueryPtrList {
public:
    ~CQueryPtrList();

    bool bAddQuery(CCQuery* query);
    void FreeRemoveAll();
    bool FreeRemoveAt(int index);
    CCQuery* RemoveAt(int index);
    CCQuery* FindQuery(enumCommandType ct, int* index = nullptr,
                       LONG* rank = nullptr);

private:
    QList<CCQuery*> m_queries;
};
