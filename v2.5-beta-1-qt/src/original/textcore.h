// Qt boundary for artifacts/inc/textview.h, included by the original
// v2.5-beta-1-modern/textcore.cpp.

#pragma once

#include "format.h"
#include "msgtype.h"
#include "wincompat.h"

#include <QPointer>
#include <QRect>
#include <QString>
#include <QTextCharFormat>

#include <array>
#include <optional>

class QTextEdit;
class QWidget;

constexpr float TEXT_VIEW_BUFFER_INFORM_FULL = 0.9F;
constexpr float TEXT_VIEW_BUFFER_CUTOFF = 0.2F;
constexpr short TEXT_VIEW_BLANK_NEVER = 0x00;
constexpr short TEXT_VIEW_BLANK_DIFFTYPES = 0x01;
constexpr short TEXT_VIEW_BLANK_ALWAYS = 0x02;
constexpr short TEXT_VIEW_BLANK_MIN = TEXT_VIEW_BLANK_NEVER;
constexpr short TEXT_VIEW_BLANK_MAX = TEXT_VIEW_BLANK_ALWAYS;
constexpr short TEXT_VIEW_AUTOSCROLL_NEVER = 0x00;
constexpr short TEXT_VIEW_AUTOSCROLL_NOSELECT = 0x01;
constexpr short TEXT_VIEW_AUTOSCROLL_NOMIDDLE = 0x02;
constexpr short TEXT_VIEW_AUTOSCROLL_ALWAYS = 0x04;
constexpr short TEXT_VIEW_AUTOSCROLL_MIN = TEXT_VIEW_AUTOSCROLL_NEVER;
constexpr short TEXT_VIEW_AUTOSCROLL_MAX = TEXT_VIEW_AUTOSCROLL_ALWAYS;
constexpr short DEFAULT_INDENT = 205;
constexpr short g_nHeaderTabLen = 2;
constexpr short g_nHighlightedFormats = 8;

struct MSG_TYPE_PROP
{
    QTextCharFormat CharFormat;
};

class CTextCore
{
public:
    CTextCore();
    ~CTextCore();

    BOOL bCreateTextViewWindow(const QString& name, const QRect& geometry,
                               QWidget* parent);
    BOOL AttachTextViewHWnd(QTextEdit* textView);
    QTextEdit* GetTextViewHWnd() const { return m_textView; }
    QTextEdit* DetachTextViewHWnd();

    void SetTextViewBufferParams(float cutoff, float full)
        { m_fBuffCutOff = cutoff; m_fBuffFull = full; }
    BOOL bSetTextViewBufferMaxSize(DWORD length);
    DWORD dwGetTextViewBufferMaxSize() const { return m_dwBuffMaxSize; }
    DWORD dwGetTextViewBufferSize() const { return m_dwBuffSize; }
    DWORD dwGetTextViewBuffer(QString* buffer) const;
    DWORD dwClearTextViewBuffer(DWORD minimumCut = 0);
    DWORD dwGetSelectedTextSize() const;
    DWORD dwGetSelectedText(QString* buffer) const;
    BOOL bIsTextViewBufferGettingFull() const
        { return m_dwBuffSize >= m_fBuffFull * m_dwBuffMaxSize; }

    BOOL bSetTextViewDefaultFormat(QTextCharFormat* format);
    BOOL bGetTextViewDefaultFormat(QTextCharFormat** format);
    BOOL bSetDefaultMessageFormat(QTextCharFormat* format, MSG_TYPE type,
                                  BOOL header);
    BOOL bSetDefaultHighlightFormat(QTextCharFormat* format, SHORT index);
    BOOL bSetMessageFormat(QTextCharFormat* format, MSG_TYPE type,
                           MEMBER_STATUS member = msParticipant,
                           BOOL header = FALSE);
    BOOL bSetMessageFormat(CHARFORMAT* format, MSG_TYPE type,
                           MEMBER_STATUS member = msParticipant,
                           BOOL header = FALSE);
    BOOL bSetHighlightFormat(QTextCharFormat* format, SHORT index);
    BOOL bSetHighlightFormat(CHARFORMAT* format, SHORT index);
    BOOL bGetDefaultMessageFormat(QTextCharFormat** format, MSG_TYPE type,
                                  BOOL header);
    BOOL bGetDefaultHighlightFormat(QTextCharFormat** format, SHORT index);
    BOOL bGetMessageFormat(QTextCharFormat** format, MSG_TYPE type,
                           MEMBER_STATUS member = msParticipant,
                           BOOL header = FALSE);
    BOOL bGetHighlightFormat(QTextCharFormat** format, SHORT index);

    BOOL bGetMessageString(const QString** string, DWORD* length,
                           MSG_TYPE type, MEMBER_STATUS member = MEMBER_STATUS(-1));
    BOOL bSetMessageString(const QString& string, DWORD length,
                           MSG_TYPE type, MEMBER_STATUS member = MEMBER_STATUS(-1));

    void SetHeaderSeparate(BOOL separate) { m_bHeaderSeparate = separate; }
    void SetDBCSSystem(BOOL dbcs) { m_bDBCSSystem = dbcs; }
    BOOL bSetInsertBlank(short insertBlank);
    short nGetInsertBlank() const { return m_nInsertBlank; }
    BOOL bSetAutoScroll(short autoScroll);
    short nGetAutoScroll() const { return m_nAutoScroll; }
    BOOL bGetLastAutoScroll() const { return m_bLastAutoScroll; }
    BOOL bAutoScrollTextView(BOOL justCheckEndInView = FALSE);

    INT iDisplayMemberStatus(const char* nickname, DWORD nameLength,
                             MSG_TYPE type, MEMBER_STATUS member = msParticipant,
                             QTextCharFormat* format = nullptr,
                             INT highlightIndex = -1);
    INT iDisplayMsgHeader(DWORD messageToFollow,
                          const char* from, DWORD fromLength,
                          const char* to, DWORD toLength,
                          MSG_TYPE type = mtNormal,
                          MEMBER_STATUS member = msParticipant,
                          QTextCharFormat* format = nullptr,
                          INT highlightIndex = -1);
    INT iDisplayMsgText(const char* text, DWORD textLength,
                        MSG_TYPE type = mtNormal,
                        MEMBER_STATUS member = msParticipant,
                        BOOL showURLs = TRUE,
                        BOOL informFull = TRUE,
                        BOOL append = FALSE,
                        LONG indent = DEFAULT_INDENT,
                        QTextCharFormat* format = nullptr,
                        INT highlightIndex = -1,
                        DWORD* formatting = nullptr,
                        INT formatCount = 0);
    INT iDisplayAction(const char* from, DWORD fromLength,
                       const char* action, DWORD actionLength,
                       MEMBER_STATUS member = msParticipant,
                       BOOL showURLs = TRUE,
                       LONG indent = 0,
                       QTextCharFormat* format = nullptr,
                       INT highlightIndex = -1,
                       DWORD* formatting = nullptr,
                       INT formatCount = 0);
    INT iDisplayInfo(const char* from, DWORD fromLength,
                     const char* to, DWORD toLength,
                     const char* info, DWORD infoLength,
                     MSG_TYPE type,
                     MEMBER_STATUS member = msParticipant,
                     QTextCharFormat* format = nullptr,
                     INT highlightIndex = -1,
                     DWORD* formatting = nullptr,
                     INT formatCount = 0);

    BOOL bHandleLink(const QString& link);
    void SetURLBrowser(BOOL newBrowser = TRUE) { m_bNewBrowser = newBrowser; }
    BOOL GetURLBrowser() const { return m_bNewBrowser; }
    BOOL bReSetDefaultMsgTypeProperties(BOOL reset = TRUE);
    BOOL bReSetDefaultHighlightFormats(BOOL reset = TRUE);
    BOOL bAddMSMsgFormat(QTextCharFormat* format, MEMBER_STATUS member,
                         BOOL header);
    BOOL bReSetMessageStrings();

private:
    BOOL bCanAdd2Buffer(DWORD messageLength);
    BOOL bSetIndent(LONG indent);
    BOOL bSetDefaultMsgTypeProperties();
    BOOL bSetDefaultHighlightFormats();
    void ClearDefaultMsgTypeProperties();
    void ClearDefaultHighlightFormats();
    void ClearMessageStrings();
    void ZeroMessageStrings();
    QTextCharFormat effectiveFormat(MSG_TYPE type, MEMBER_STATUS member,
                                    BOOL header) const;
    QTextCharFormat formattedChunk(const QTextCharFormat& base,
                                   WORD formatting) const;
    QString sourceText(const char* text, DWORD length) const;
    bool endInView() const;

    QPointer<QTextEdit> m_textView;
    bool m_ownsTextView = false;
    DWORD m_dwBuffSize = 0;
    DWORD m_dwBuffMaxSize = 0;
    float m_fBuffCutOff = TEXT_VIEW_BUFFER_CUTOFF;
    float m_fBuffFull = TEXT_VIEW_BUFFER_INFORM_FULL;
    QTextCharFormat m_cfFont;
    short m_nInsertBlank = TEXT_VIEW_BLANK_NEVER;
    short m_nAutoScroll = TEXT_VIEW_AUTOSCROLL_ALWAYS;
    BOOL m_bLastAutoScroll = TRUE;
    BOOL m_bHeader = FALSE;
    BOOL m_bCallHeader = FALSE;
    BOOL m_bHeaderSeparate = TRUE;
    BOOL m_bDBCSSystem = FALSE;
    BOOL m_bNewBrowser = TRUE;
    MSG_TYPE m_mtLastMsgType = mtURL;

    std::array<std::array<std::optional<MSG_TYPE_PROP>, mtBeginActions>, msEndEnum>
        m_pMsgTypePropHead;
    std::array<std::array<std::optional<MSG_TYPE_PROP>, mtBeginInfo>, msEndEnum>
        m_pMsgTypePropText;
    std::array<std::optional<MSG_TYPE_PROP>, mtEndEnum - mtBeginInfo>
        m_pMsgTypePropInfo;
    MSG_TYPE_PROP m_URLMsgTypeProp;
    MSG_TYPE_PROP m_HeadMsgTypeProp;
    MSG_TYPE_PROP m_TextMsgTypeProp;
    MSG_TYPE_PROP m_InfoMsgTypeProp;
    std::array<std::optional<QTextCharFormat>, g_nHighlightedFormats>
        m_rgpcfHighlights;
    std::array<QTextCharFormat, g_nHighlightedFormats> m_rgcfDefHighlights;
    std::array<QString, mtEndEnum> m_szMsgType;
    std::array<QString, msEndEnum> m_szMembStatus;
};
