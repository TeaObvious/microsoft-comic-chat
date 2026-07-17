// Port of the build-selected artifacts-modern/core/textview.cpp. QTextEdit is
// the replacement boundary for the RichEdit HWND; message types, resources,
// buffer policy, formatting transitions and call order remain in this module.

#include "textcore.h"

#include "originalassets.h"

#include <QApplication>
#include <QFontDatabase>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>

#include <algorithm>
#include <cstring>

namespace {
QColor qColor(COLORREF color)
{
    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
}

QTextCharFormat colorFormat(COLORREF color)
{
    QTextCharFormat format;
    format.setForeground(qColor(color));
    return format;
}

QString applyArguments(QString value, const QStringList& arguments)
{
    for (int index = 0; index < arguments.size(); ++index)
        value.replace(QStringLiteral("%") + QString::number(index + 1),
                      arguments[index]);
    return value;
}

QString textViewStringForType(MSG_TYPE type)
{
    static const std::array<const char*, mtEndEnum> identifiers = {
        "IDS_NORMAL_HEADER", "IDS_WHISPER_HEADER", "IDS_THOUGHT_HEADER",
        "IDS_BROADCAST_HEADER", "IDS_ACTION_HEADER", "IDS_PRIVATE_HEADER",
        "IDS_EXCHAN_HEADER", "IDS_JOIN_CHATROOM_ACTION",
        "IDS_LEAVE_CHATROOM_ACTION", nullptr, nullptr, nullptr,
        "IDS_GETINFO_INFO", "IDS_STATUSCHANGED_INFO", "IDS_ALIASCHANGED_INFO",
        "IDS_TOPICCHANGED_INFO", "IDS_KICKED_INFO", "IDS_REALNAME_INFO"
    };
    const char* identifier = identifiers.at(static_cast<size_t>(type));
    return identifier
        ? originalTextViewResourceString(QString::fromLatin1(identifier))
        : QString();
}

QString textViewStringForMember(MEMBER_STATUS member)
{
    static const std::array<const char*, msEndEnum> identifiers = {
        "IDS_HOST_HEADER", "IDS_PART_HEADER", "IDS_SPEC_HEADER", "IDS_ROOM_HEADER"
    };
    return originalTextViewResourceString(
        QString::fromLatin1(identifiers.at(static_cast<size_t>(member))));
}

QTextCharFormat richEditFormat(const CHARFORMAT& source)
{
    QTextCharFormat target;
    if (source.dwMask & CFM_FACE) {
        const char* const end = std::find(
            source.szFaceName, source.szFaceName + LF_FACESIZE, '\0');
        if (end != source.szFaceName) {
            target.setFontFamily(QString::fromLatin1(
                source.szFaceName,
                static_cast<qsizetype>(end - source.szFaceName)));
        }
    }
    if ((source.dwMask & CFM_SIZE) && source.yHeight > 0)
        target.setFontPointSize(source.yHeight / 20.0);
    if (source.dwMask & CFM_OFFSET) {
        const qreal baseline = source.yHeight
            ? 100.0 * source.yOffset / source.yHeight : 0.0;
        target.setBaselineOffset(baseline);
    }
    if ((source.dwMask & CFM_COLOR)
        && !(source.dwEffects & CFE_AUTOCOLOR)) {
        target.setForeground(qColor(source.crTextColor));
    }
    if (source.dwMask & CFM_BOLD) {
        target.setFontWeight((source.dwEffects & CFE_BOLD)
                                 ? QFont::Bold : QFont::Normal);
    }
    if (source.dwMask & CFM_ITALIC)
        target.setFontItalic(source.dwEffects & CFE_ITALIC);
    if (source.dwMask & CFM_UNDERLINE)
        target.setFontUnderline(source.dwEffects & CFE_UNDERLINE);
    if (source.dwMask & CFM_STRIKEOUT)
        target.setFontStrikeOut(source.dwEffects & CFE_STRIKEOUT);
    return target;
}
}

CTextCore::CTextCore()
{
    m_cfFont.setFont(QApplication::font());
    m_cfFont.setForeground(QColor(0, 0, 0));
    ZeroMessageStrings();
    bSetDefaultMsgTypeProperties();
    bSetDefaultHighlightFormats();
}

CTextCore::~CTextCore()
{
    if (m_ownsTextView && m_textView) delete m_textView;
    ClearDefaultMsgTypeProperties();
    ClearDefaultHighlightFormats();
    ClearMessageStrings();
}

BOOL CTextCore::bCreateTextViewWindow(const QString& name, const QRect& geometry,
                                      QWidget* parent)
{
    if (m_textView) return FALSE;
    auto* textView = new QTextEdit(parent);
    textView->setObjectName(name);
    textView->setGeometry(geometry);
    textView->setReadOnly(true);
    m_ownsTextView = true;
    return AttachTextViewHWnd(textView);
}

BOOL CTextCore::AttachTextViewHWnd(QTextEdit* textView)
{
    if (!textView || m_textView) return FALSE;
    m_textView = textView;
    m_textView->setReadOnly(true);
    m_textView->setAcceptRichText(true);
    m_textView->setUndoRedoEnabled(false);
    m_cfFont.setFont(m_textView->font());
    m_cfFont.setForeground(QColor(0, 0, 0));
    m_dwBuffMaxSize = 32767;
    bReSetMessageStrings();
    dwClearTextViewBuffer(0);
    return TRUE;
}

QTextEdit* CTextCore::DetachTextViewHWnd()
{
    QTextEdit* result = m_textView;
    m_textView = nullptr;
    m_ownsTextView = false;
    return result;
}

BOOL CTextCore::bSetTextViewBufferMaxSize(DWORD length)
{
    if (!m_textView) return FALSE;
    m_dwBuffMaxSize = length;
    return TRUE;
}

DWORD CTextCore::dwGetTextViewBuffer(QString* buffer) const
{
    if (!m_textView || !buffer) return 0;
    *buffer = m_textView->toPlainText();
    return static_cast<DWORD>(buffer->size());
}

DWORD CTextCore::dwClearTextViewBuffer(DWORD minimumCut)
{
    if (!m_textView) return static_cast<DWORD>(-1);
    if (minimumCut == 0) {
        const DWORD oldSize = m_dwBuffSize;
        m_textView->clear();
        m_dwBuffSize = 0;
        return oldSize;
    }

    QTextCursor saved = m_textView->textCursor();
    const bool restoreSelection = saved.hasSelection()
        || saved.position() != static_cast<int>(m_dwBuffSize);
    const bool scroll = endInView();
    minimumCut = std::max<DWORD>(minimumCut,
        static_cast<DWORD>(m_fBuffCutOff * m_dwBuffMaxSize));

    const QString history = m_textView->toPlainText();
    const int newline = history.indexOf(QLatin1Char('\n'),
                                        static_cast<int>(minimumCut));
    if (newline < 0 || newline > static_cast<int>(minimumCut) + 2046) {
        const DWORD oldSize = m_dwBuffSize;
        m_textView->clear();
        m_dwBuffSize = 0;
        return oldSize;
    }

    const int cut = newline + 1;
    QTextCursor remove(m_textView->document());
    remove.setPosition(0);
    remove.setPosition(cut, QTextCursor::KeepAnchor);
    remove.removeSelectedText();
    m_dwBuffSize = static_cast<DWORD>(m_textView->toPlainText().size());

    if (scroll) {
        QTextCursor end(m_textView->document());
        end.movePosition(QTextCursor::End);
        m_textView->setTextCursor(end);
        m_textView->verticalScrollBar()->setValue(
            m_textView->verticalScrollBar()->maximum());
    } else if (restoreSelection) {
        const int start = qMax(0, saved.selectionStart() - cut);
        const int end = qMax(0, saved.selectionEnd() - cut);
        saved.setPosition(start);
        saved.setPosition(end, QTextCursor::KeepAnchor);
        m_textView->setTextCursor(saved);
    }
    return static_cast<DWORD>(cut);
}

DWORD CTextCore::dwGetSelectedTextSize() const
{
    return m_textView
        ? static_cast<DWORD>(m_textView->textCursor().selectedText().size()) : 0;
}

DWORD CTextCore::dwGetSelectedText(QString* buffer) const
{
    if (!m_textView || !buffer) return 0;
    *buffer = m_textView->textCursor().selectedText();
    return static_cast<DWORD>(buffer->size());
}

BOOL CTextCore::bSetInsertBlank(short insertBlank)
{
    if (insertBlank < TEXT_VIEW_BLANK_MIN || insertBlank > TEXT_VIEW_BLANK_MAX)
        return FALSE;
    m_nInsertBlank = insertBlank;
    return TRUE;
}

BOOL CTextCore::bSetAutoScroll(short autoScroll)
{
    if (autoScroll < TEXT_VIEW_AUTOSCROLL_MIN
        || autoScroll > TEXT_VIEW_AUTOSCROLL_MAX) return FALSE;
    m_nAutoScroll = autoScroll;
    return TRUE;
}

BOOL CTextCore::bSetTextViewDefaultFormat(QTextCharFormat* format)
{
    if (!format) return FALSE;
    m_cfFont = *format;
    return TRUE;
}

BOOL CTextCore::bGetTextViewDefaultFormat(QTextCharFormat** format)
{
    if (!format) return FALSE;
    *format = &m_cfFont;
    return TRUE;
}

BOOL CTextCore::bSetDefaultMessageFormat(QTextCharFormat* format,
                                         MSG_TYPE type, BOOL header)
{
    if (type < 0 || type > mtURL || (header && type >= mtBeginActions))
        return FALSE;
    if (type == mtURL) m_URLMsgTypeProp.CharFormat = format ? *format : colorFormat(RGB(0,0,255));
    else if (type < mtBeginInfo) {
        if (header) m_HeadMsgTypeProp.CharFormat = format ? *format : colorFormat(RGB(0,0,255));
        else m_TextMsgTypeProp.CharFormat = format ? *format : colorFormat(RGB(0,0,0));
    } else m_InfoMsgTypeProp.CharFormat = format ? *format : colorFormat(RGB(0,128,128));
    return TRUE;
}

BOOL CTextCore::bSetDefaultHighlightFormat(QTextCharFormat* format, SHORT index)
{
    if (index < 0 || index >= g_nHighlightedFormats) return FALSE;
    if (format) m_rgcfDefHighlights[index] = *format;
    return TRUE;
}

BOOL CTextCore::bSetMessageFormat(QTextCharFormat* format, MSG_TYPE type,
                                  MEMBER_STATUS member, BOOL header)
{
    if (type < 0 || type > mtURL || member < 0 || member >= msEndEnum
        || (header && type >= mtBeginActions)) return FALSE;
    if (type == mtURL) {
        m_URLMsgTypeProp.CharFormat = format ? *format : colorFormat(RGB(0,0,255));
        return TRUE;
    }
    if (type < mtBeginInfo) {
        auto& slot = header
            ? m_pMsgTypePropHead[member][type]
            : m_pMsgTypePropText[member][type];
        if (!format) slot.reset();
        else slot = MSG_TYPE_PROP{*format};
    } else {
        auto& slot = m_pMsgTypePropInfo[type - mtBeginInfo];
        if (!format) slot.reset();
        else slot = MSG_TYPE_PROP{*format};
    }
    return TRUE;
}

BOOL CTextCore::bSetMessageFormat(CHARFORMAT* format, MSG_TYPE type,
                                  MEMBER_STATUS member, BOOL header)
{
    if (!format) {
        return bSetMessageFormat(static_cast<QTextCharFormat*>(nullptr),
                                 type, member, header);
    }
    QTextCharFormat converted = richEditFormat(*format);
    return bSetMessageFormat(&converted, type, member, header);
}

BOOL CTextCore::bSetHighlightFormat(QTextCharFormat* format, SHORT index)
{
    if (index < 0 || index >= g_nHighlightedFormats) return FALSE;
    if (!format) m_rgpcfHighlights[index].reset();
    else m_rgpcfHighlights[index] = *format;
    return TRUE;
}

BOOL CTextCore::bSetHighlightFormat(CHARFORMAT* format, SHORT index)
{
    if (!format) {
        return bSetHighlightFormat(static_cast<QTextCharFormat*>(nullptr),
                                   index);
    }
    QTextCharFormat converted = richEditFormat(*format);
    return bSetHighlightFormat(&converted, index);
}

BOOL CTextCore::bGetDefaultMessageFormat(QTextCharFormat** format,
                                         MSG_TYPE type, BOOL header)
{
    if (!format || type < 0 || type > mtURL
        || (header && type >= mtBeginActions)) return FALSE;
    if (type == mtURL) *format = &m_URLMsgTypeProp.CharFormat;
    else if (type < mtBeginInfo)
        *format = header ? &m_HeadMsgTypeProp.CharFormat : &m_TextMsgTypeProp.CharFormat;
    else *format = &m_InfoMsgTypeProp.CharFormat;
    return TRUE;
}

BOOL CTextCore::bGetDefaultHighlightFormat(QTextCharFormat** format, SHORT index)
{
    if (!format || index < 0 || index >= g_nHighlightedFormats) return FALSE;
    *format = &m_rgcfDefHighlights[index];
    return TRUE;
}

BOOL CTextCore::bGetMessageFormat(QTextCharFormat** format, MSG_TYPE type,
                                  MEMBER_STATUS member, BOOL header)
{
    if (!format || type < 0 || type > mtURL || member < 0 || member >= msEndEnum
        || (header && type >= mtBeginActions)) return FALSE;
    if (type == mtURL) {
        *format = &m_URLMsgTypeProp.CharFormat;
        return TRUE;
    }
    if (type < mtBeginInfo) {
        auto& slot = header
            ? m_pMsgTypePropHead[member][type]
            : m_pMsgTypePropText[member][type];
        *format = slot ? &slot->CharFormat : nullptr;
    } else {
        auto& slot = m_pMsgTypePropInfo[type - mtBeginInfo];
        *format = slot ? &slot->CharFormat : nullptr;
    }
    return *format != nullptr;
}

BOOL CTextCore::bGetHighlightFormat(QTextCharFormat** format, SHORT index)
{
    if (!format || index < 0 || index >= g_nHighlightedFormats) return FALSE;
    *format = m_rgpcfHighlights[index] ? &*m_rgpcfHighlights[index] : nullptr;
    return *format != nullptr;
}

QTextCharFormat CTextCore::effectiveFormat(MSG_TYPE type, MEMBER_STATUS member,
                                           BOOL header) const
{
    QTextCharFormat result;
    if (type == mtURL) result = m_URLMsgTypeProp.CharFormat;
    else if (type < mtBeginInfo) {
        result = header ? m_HeadMsgTypeProp.CharFormat : m_TextMsgTypeProp.CharFormat;
        const auto& slot = header
            ? m_pMsgTypePropHead[member][type]
            : m_pMsgTypePropText[member][type];
        if (slot) result.merge(slot->CharFormat);
    } else {
        result = m_InfoMsgTypeProp.CharFormat;
        const auto& slot = m_pMsgTypePropInfo[type - mtBeginInfo];
        if (slot) result.merge(slot->CharFormat);
    }
    QTextCharFormat combined = m_cfFont;
    combined.merge(result);
    return combined;
}

QTextCharFormat CTextCore::formattedChunk(const QTextCharFormat& base,
                                          WORD formatting) const
{
    QTextCharFormat result = base;
    result.setFontWeight((formatting & wBold) ? QFont::Bold : QFont::Normal);
    result.setFontItalic(formatting & wItalic);
    result.setFontUnderline(formatting & wUnderline);
    const short fixed = nGetSpecialFontIndex(TRUE);
    const short symbol = nGetSpecialFontIndex(FALSE);
    if ((formatting & wFixedPitch) && fixed >= 0) {
        result.setFontFamily(QString::fromLatin1(FIXEDPITCHFACENAMES[fixed]));
        result.setFontFixedPitch(true);
    } else if ((formatting & wSymbol) && symbol >= 0) {
        result.setFontFamily(QString::fromLatin1(SYMBOLFACENAMES[symbol]));
        result.setFontFixedPitch(false);
    } else {
        result.setFontFamily(m_cfFont.font().family());
        result.setFontFixedPitch(m_cfFont.font().fixedPitch());
    }
    if (formatting & wForeground) {
        COLORREF color = GetRBGColor(static_cast<BYTE>((formatting >> 4) & 0x0f));
        result.setForeground(color == RGB(255,255,255)
                                 ? m_cfFont.foreground() : QBrush(qColor(color)));
    } else result.setForeground(base.foreground());
    return result;
}

QString CTextCore::sourceText(const char* text, DWORD length) const
{
    if (!text) return {};
    const int bytes = length ? static_cast<int>(length)
                             : static_cast<int>(std::strlen(text));
    return QString::fromUtf8(text, bytes);
}

bool CTextCore::endInView() const
{
    if (!m_textView) return true;
    const QScrollBar* bar = m_textView->verticalScrollBar();
    return bar->value() + bar->pageStep() >= bar->maximum();
}

BOOL CTextCore::bAutoScrollTextView(BOOL justCheckEndInView)
{
    if (!m_textView) return FALSE;
    const bool endVisible = endInView();
    m_bLastAutoScroll = endVisible;
    if (justCheckEndInView) return endVisible;
    if (!endVisible)
        m_textView->verticalScrollBar()->setValue(
            m_textView->verticalScrollBar()->maximum());
    return TRUE;
}

BOOL CTextCore::bCanAdd2Buffer(DWORD messageLength)
{
    if (m_dwBuffSize + messageLength <= m_dwBuffMaxSize) return TRUE;
    if (messageLength > m_dwBuffMaxSize) return FALSE;
    return dwClearTextViewBuffer(messageLength + m_dwBuffSize
                                 - m_dwBuffMaxSize + 1) != static_cast<DWORD>(-1);
}

BOOL CTextCore::bSetIndent(LONG indent)
{
    if (!m_textView) return FALSE;
    QTextCursor cursor = m_textView->textCursor();
    QTextBlockFormat block = cursor.blockFormat();
    const qreal dpi = m_textView->logicalDpiX() > 0
        ? m_textView->logicalDpiX() : 96.0;
    block.setLeftMargin((indent + 72) * dpi / 1440.0);
    cursor.setBlockFormat(block);
    m_textView->setTextCursor(cursor);
    return TRUE;
}

INT CTextCore::iDisplayMsgText(const char* text, DWORD textLength,
                               MSG_TYPE type, MEMBER_STATUS member,
                               BOOL showURLs, BOOL informFull, BOOL append,
                               LONG indent, QTextCharFormat* charFormat,
                               INT highlightIndex, DWORD* formatting,
                               INT formatCount)
{
    Q_UNUSED(showURLs);
    if (!m_textView || !text) return -1;
    const QString value = sourceText(text, textLength);
    QTextCursor saved = m_textView->textCursor();
    const bool restoreSelection = saved.hasSelection()
        || saved.position() != static_cast<int>(m_dwBuffSize);
    bool scroll = false;
    if (m_nAutoScroll == TEXT_VIEW_AUTOSCROLL_ALWAYS) scroll = endInView();
    else if (m_nAutoScroll == TEXT_VIEW_AUTOSCROLL_NEVER) scroll = false;
    else if (!restoreSelection
             || ((m_nAutoScroll & TEXT_VIEW_AUTOSCROLL_NOSELECT) && !saved.hasSelection())
             || ((m_nAutoScroll & TEXT_VIEW_AUTOSCROLL_NOMIDDLE) && saved.hasSelection()))
        scroll = endInView();

    QTextCursor cursor(m_textView->document());
    cursor.movePosition(QTextCursor::End);
    m_textView->setTextCursor(cursor);
    if (!append) {
        if (m_dwBuffSize) {
            if (m_bHeaderSeparate || !m_bHeader) cursor.insertText(QStringLiteral("\n"));
            else {
                QTextCharFormat tabFormat = cursor.charFormat();
                tabFormat.setFontUnderline(false);
                cursor.insertText(QStringLiteral("  "), tabFormat);
            }
            if (((m_nInsertBlank & TEXT_VIEW_BLANK_ALWAYS) && !m_bHeader)
                || ((type != m_mtLastMsgType)
                    && (m_nInsertBlank & TEXT_VIEW_BLANK_DIFFTYPES)))
                cursor.insertText(QStringLiteral("\n"));
        }
        m_textView->setTextCursor(cursor);
        if (m_bHeaderSeparate || !m_bHeader) bSetIndent(indent);
        if (m_bHeader) m_bHeader = FALSE;
        cursor = m_textView->textCursor();
        cursor.movePosition(QTextCursor::End);
    }

    QTextCharFormat base;
    if (highlightIndex >= 0 && highlightIndex < g_nHighlightedFormats) {
        base = m_rgpcfHighlights[highlightIndex]
            ? *m_rgpcfHighlights[highlightIndex] : m_rgcfDefHighlights[highlightIndex];
        QTextCharFormat combined = m_cfFont;
        combined.merge(base);
        base = combined;
    } else if (charFormat) {
        base = m_cfFont;
        base.merge(*charFormat);
    } else {
        base = effectiveFormat(type, member, FALSE);
    }
    m_mtLastMsgType = type;

    if (!formatting) {
        cursor.insertText(value, base);
    } else if (formatCount > 0) {
        int currentOffset = 0;
        WORD currentWord = 0;
        bool transparency = false;
        QTextCharFormat chunk = formattedChunk(base, currentWord);
        for (int index = 0; index < formatCount; ++index) {
            const int nextOffset = qMin<int>(HIWORD(formatting[index]), value.size());
            if (nextOffset > currentOffset) {
                QString part = value.mid(currentOffset, nextOffset - currentOffset);
                if (transparency) part.fill(QChar(0x01));
                cursor.insertText(part, chunk);
            }
            currentWord = LOWORD(formatting[index]);
            chunk = formattedChunk(base, currentWord);
            transparency = (currentWord & wForeground)
                && (currentWord & wBackground)
                && (((currentWord >> 4) & 0x0f) == (currentWord & 0x0f));
            currentOffset = nextOffset;
        }
        QString finalPart = value.mid(currentOffset);
        if (transparency) finalPart.fill(QChar(0x01));
        cursor.insertText(finalPart, chunk);
    } else {
        cursor.insertText(value, base);
    }

    m_dwBuffSize = static_cast<DWORD>(m_textView->toPlainText().size());
    int result = informFull && bIsTextViewBufferGettingFull() ? 1 : 0;
    if (restoreSelection) m_textView->setTextCursor(saved);
    else m_textView->setTextCursor(cursor);
    if (scroll)
        m_textView->verticalScrollBar()->setValue(
            m_textView->verticalScrollBar()->maximum());
    m_bLastAutoScroll = scroll;
    return result;
}

INT CTextCore::iDisplayMemberStatus(const char* nickname, DWORD nameLength,
                                    MSG_TYPE type, MEMBER_STATUS member,
                                    QTextCharFormat* format, INT highlightIndex)
{
    if (!nickname || (type != mtJoin && type != mtLeave)) return -1;
    const QString name = sourceText(nickname, nameLength);
    const QString text = applyArguments(m_szMsgType[type],
                                        {m_szMembStatus[member], name});
    if (!bCanAdd2Buffer(static_cast<DWORD>(text.size()))) return -1;
    const QByteArray bytes = text.toUtf8();
    return iDisplayMsgText(bytes.constData(), bytes.size(), type, member,
                           FALSE, TRUE, FALSE, 0, format, highlightIndex);
}

INT CTextCore::iDisplayMsgHeader(DWORD messageToFollow,
                                 const char* from, DWORD fromLength,
                                 const char* to, DWORD toLength,
                                 MSG_TYPE type, MEMBER_STATUS member,
                                 QTextCharFormat* format, INT highlightIndex)
{
    if (!from || type < 0 || type >= mtBeginActions) return -1;
    const QString fromText = sourceText(from, fromLength);
    QString text;
    if (type == mtWhisper || type == mtExChan) {
        if (!to) return -1;
        text = applyArguments(m_szMsgType[type],
            {m_szMembStatus[member], fromText, sourceText(to, toLength)});
    } else {
        text = applyArguments(m_szMsgType[type],
                              {m_szMembStatus[member], fromText});
    }
    const DWORD expected = static_cast<DWORD>(text.size()) + messageToFollow
        + (m_dwBuffSize ? 1 : 0) + 1
        + (m_nInsertBlank != TEXT_VIEW_BLANK_NEVER ? 1 : 0);
    if (!bCanAdd2Buffer(expected)) return -1;
    QTextCharFormat local = format ? *format : effectiveFormat(type, member, TRUE);
    m_bCallHeader = TRUE;
    const QByteArray bytes = text.toUtf8();
    const INT result = iDisplayMsgText(bytes.constData(), bytes.size(), type, member,
                                       FALSE, TRUE, FALSE, 0, &local,
                                       highlightIndex);
    m_bCallHeader = FALSE;
    m_bHeader = TRUE;
    return result;
}

INT CTextCore::iDisplayAction(const char* from, DWORD fromLength,
                              const char* action, DWORD actionLength,
                              MEMBER_STATUS member, BOOL showURLs, LONG indent,
                              QTextCharFormat* format, INT highlightIndex,
                              DWORD* formatting, INT formatCount)
{
    if (!action) return -1;
    const QString status = m_szMembStatus[member];
    const QString actionText = sourceText(action, actionLength);
    const QString fromText = from ? sourceText(from, fromLength) : QString();
    const QString fullAction = from
        ? fromText + QLatin1Char(' ') + actionText : actionText;
    const QString text = applyArguments(m_szMsgType[mtAction],
                                        {status, fullAction});
    if (!bCanAdd2Buffer(static_cast<DWORD>(text.size()))) return -1;
    QVector<DWORD> local;
    if (formatting && formatCount > 0) {
        local.reserve(formatCount);
        const SHORT delta = static_cast<SHORT>(status.size()
            + (from ? fromText.size() + 1 : 0));
        for (int index = 0; index < formatCount; ++index)
            local.append(MAKELONG(LOWORD(formatting[index]),
                                  HIWORD(formatting[index]) + delta));
    }
    const QByteArray bytes = text.toUtf8();
    return iDisplayMsgText(bytes.constData(), bytes.size(), mtAction, member,
                           showURLs, TRUE, FALSE, indent, format, highlightIndex,
                           local.isEmpty() ? nullptr : local.data(), local.size());
}

INT CTextCore::iDisplayInfo(const char* from, DWORD fromLength,
                            const char* to, DWORD toLength,
                            const char* info, DWORD infoLength,
                            MSG_TYPE type, MEMBER_STATUS member,
                            QTextCharFormat* format, INT highlightIndex,
                            DWORD* formatting, INT formatCount)
{
    if (type < mtBeginInfo || type >= mtEndEnum) return -1;
    const QString fromText = from ? sourceText(from, fromLength) : QString();
    const QString toText = to ? sourceText(to, toLength) : QString();
    const QString infoText = info ? sourceText(info, infoLength) : QString();
    QStringList arguments;
    int formattingDelta = 0;
    switch (type) {
    case mtGetInfo:
        arguments = {fromText, toText, infoText};
        formattingDelta = fromText.size() + toText.size();
        break;
    case mtStatusChange: {
        static const std::array<const char*, 3> ids = {
            "IDS_HOSTSTATUS_INFO", "IDS_PARTSTATUS_INFO", "IDS_SPECSTATUS_INFO"
        };
        if (member >= msRoom) return -1;
        arguments = {toText, originalTextViewResourceString(
            QString::fromLatin1(ids[member]))};
        break;
    }
    case mtAliasChange: arguments = {fromText, toText}; break;
    case mtTopicChange: arguments = {infoText}; break;
    case mtGetRealname: arguments = {toText, infoText}; break;
    case mtKicked:
        arguments = {fromText, toText,
            info ? infoText : originalTextViewResourceString(
                QStringLiteral("IDS_NOKICKREASON_INFO"))};
        break;
    default: return -1;
    }
    const QString text = applyArguments(m_szMsgType[type], arguments);
    if (!bCanAdd2Buffer(static_cast<DWORD>(text.size()))) return -1;
    QVector<DWORD> local;
    if (formatting && formatCount > 0) {
        for (int index = 0; index < formatCount; ++index)
            local.append(MAKELONG(LOWORD(formatting[index]),
                                  HIWORD(formatting[index]) + formattingDelta));
    }
    QTextCharFormat localFormat = format ? *format : effectiveFormat(type, member, FALSE);
    const QByteArray bytes = text.toUtf8();
    return iDisplayMsgText(bytes.constData(), bytes.size(), type, member,
                           FALSE, TRUE, FALSE, 0, &localFormat, highlightIndex,
                           local.isEmpty() ? nullptr : local.data(), local.size());
}

BOOL CTextCore::bHandleLink(const QString& link)
{
    Q_UNUSED(link);
    // The original delegates to CUrlRec. url.* and urlfind.cpp are audited and
    // ported separately; no QDesktopServices replacement is inferred here.
    return FALSE;
}

BOOL CTextCore::bSetDefaultMsgTypeProperties()
{
    ClearDefaultMsgTypeProperties();
    m_URLMsgTypeProp.CharFormat = colorFormat(RGB(0,0,255));
    m_URLMsgTypeProp.CharFormat.setFontUnderline(true);
    m_URLMsgTypeProp.CharFormat.setAnchor(true);
    m_HeadMsgTypeProp.CharFormat = colorFormat(RGB(0,0,255));
    m_TextMsgTypeProp.CharFormat = colorFormat(RGB(0,0,0));
    m_InfoMsgTypeProp.CharFormat = colorFormat(RGB(0,128,128));

    for (int member = 0; member < msEndEnum; ++member) {
        QTextCharFormat format = colorFormat(RGB(0,128,0));
        format.setFontWeight(QFont::Bold);
        bSetMessageFormat(&format, mtJoin, static_cast<MEMBER_STATUS>(member));
        format.setForeground(qColor(RGB(255,0,0)));
        bSetMessageFormat(&format, mtLeave, static_cast<MEMBER_STATUS>(member));

        format = colorFormat(RGB(255,0,255));
        bSetMessageFormat(&format, mtBroadcast, static_cast<MEMBER_STATUS>(member));
        format = colorFormat(RGB(0,0,128));
        bSetMessageFormat(&format, mtThought, static_cast<MEMBER_STATUS>(member), TRUE);
        format = colorFormat(RGB(128,0,128));
        format.setFontItalic(true);
        bSetMessageFormat(&format, mtAction, static_cast<MEMBER_STATUS>(member));
        format = colorFormat(RGB(128,0,0));
        format.setFontUnderline(true);
        bSetMessageFormat(&format, mtWhisper, static_cast<MEMBER_STATUS>(member), TRUE);
        format = colorFormat(RGB(128,128,128));
        bSetMessageFormat(&format, mtPrivate, static_cast<MEMBER_STATUS>(member), TRUE);
        bSetMessageFormat(&format, mtExChan, static_cast<MEMBER_STATUS>(member), TRUE);
    }
    return TRUE;
}

BOOL CTextCore::bSetDefaultHighlightFormats()
{
    static const std::array<COLORREF, 4> colors = {
        RGB(128,128,128), RGB(128,128,0), RGB(0,255,255), RGB(255,0,255)
    };
    for (int group = 0; group < 4; ++group) {
        for (int parity = 0; parity < 2; ++parity) {
            QTextCharFormat format = colorFormat(colors[group]);
            format.setFontWeight(QFont::Bold);
            if (parity == 0) format.setFontPointSize(12);
            const int index = group * 2 + parity;
            m_rgcfDefHighlights[index] = format;
            m_rgpcfHighlights[index].reset();
        }
    }
    return TRUE;
}

BOOL CTextCore::bAddMSMsgFormat(QTextCharFormat* format,
                                MEMBER_STATUS member, BOOL header)
{
    if (!format || member < 0 || member >= msEndEnum) return FALSE;
    const int end = header ? mtBeginActions : mtEndEnum;
    for (int type = end - 1; type >= 0; --type) {
        if (!header && type >= mtBeginInfo) continue;
        QTextCharFormat* existing = nullptr;
        if (bGetMessageFormat(&existing, static_cast<MSG_TYPE>(type), member, header))
            existing->merge(*format);
        else {
            QTextCharFormat combined = header
                ? m_HeadMsgTypeProp.CharFormat : m_TextMsgTypeProp.CharFormat;
            combined.merge(*format);
            bSetMessageFormat(&combined, static_cast<MSG_TYPE>(type), member, header);
        }
    }
    return TRUE;
}

BOOL CTextCore::bReSetDefaultMsgTypeProperties(BOOL reset)
{
    ClearDefaultMsgTypeProperties();
    return !reset || bSetDefaultMsgTypeProperties();
}

BOOL CTextCore::bReSetDefaultHighlightFormats(BOOL reset)
{
    ClearDefaultHighlightFormats();
    return !reset || bSetDefaultHighlightFormats();
}

void CTextCore::ClearDefaultMsgTypeProperties()
{
    for (auto& members : m_pMsgTypePropHead)
        for (auto& slot : members) slot.reset();
    for (auto& members : m_pMsgTypePropText)
        for (auto& slot : members) slot.reset();
    for (auto& slot : m_pMsgTypePropInfo) slot.reset();
}

void CTextCore::ClearDefaultHighlightFormats()
{
    for (auto& slot : m_rgpcfHighlights) slot.reset();
}

BOOL CTextCore::bReSetMessageStrings()
{
    ClearMessageStrings();
    ZeroMessageStrings();
    for (int type = 0; type < mtEndEnum; ++type)
        m_szMsgType[type] = textViewStringForType(static_cast<MSG_TYPE>(type));
    for (int member = 0; member < msEndEnum; ++member)
        m_szMembStatus[member] = textViewStringForMember(
            static_cast<MEMBER_STATUS>(member));
    return TRUE;
}

void CTextCore::ClearMessageStrings()
{
    for (QString& value : m_szMsgType) value.clear();
    for (QString& value : m_szMembStatus) value.clear();
}

void CTextCore::ZeroMessageStrings()
{
    ClearMessageStrings();
}

BOOL CTextCore::bGetMessageString(const QString** string, DWORD* length,
                                  MSG_TYPE type, MEMBER_STATUS member)
{
    if (!string || (type >= 0 && member >= 0) || type >= mtEndEnum
        || member >= msEndEnum) return FALSE;
    if (type >= 0) *string = &m_szMsgType[type];
    else if (member >= 0) *string = &m_szMembStatus[member];
    else return FALSE;
    if (length) *length = static_cast<DWORD>((*string)->size());
    return TRUE;
}

BOOL CTextCore::bSetMessageString(const QString& string, DWORD length,
                                  MSG_TYPE type, MEMBER_STATUS member)
{
    if ((type >= 0 && member >= 0) || type >= mtEndEnum || member >= msEndEnum)
        return FALSE;
    const QString value = length == static_cast<DWORD>(-1)
        ? string : string.left(static_cast<int>(length));
    if (type >= 0) m_szMsgType[type] = value;
    else if (member >= 0) m_szMembStatus[member] = value;
    else return FALSE;
    return TRUE;
}
