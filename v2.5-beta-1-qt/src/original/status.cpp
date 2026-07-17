// Ported from v2.5-beta-1-modern/status.cpp.

#include "status.h"

#include "chatdoc.h"
#include "ircsock.h"
#include "originalassets.h"

#include <QColor>
#include <QMenu>
#include <QTextCharFormat>

namespace {
void AppendStatusItems(QMenu& menu, const QList<OriginalMenuItem>& items)
{
    for (const OriginalMenuItem& item : items) {
        if (item.type == OriginalMenuItemType::Separator) {
            menu.addSeparator();
        } else if (item.type == OriginalMenuItemType::Popup) {
            AppendStatusItems(menu, item.children);
        } else {
            QAction* action = menu.addAction(item.text);
            action->setData(item.commandIdentifier);
        }
    }
}
}

CStatusView* GetStatusView()
{
    for (CChatDoc* document : g_docs) {
        if (document && document->m_bStatusView && document->m_textView) {
            return static_cast<CStatusView*>(document->m_textView);
        }
    }
    return nullptr;
}

void AddToStatus(CIrcPrint& ircPrint, const QString& line,
                 CDWordArray* formatting)
{
    static BOOL newLine = FALSE;
    CStatusView* status = GetStatusView();
    if (!status) return;

    QString pretty;
    switch (ircPrint.m_iType) {
    case PT_NOTINIT:
        return;
    case PT_LASTSTRING:
    {
        QString message = ircPrint.m_szMessage;
        if (!message.isEmpty()) message.remove(0, 1);
        const qsizetype colon = message.indexOf(QLatin1Char(':'));
        if (colon < 0) return;
        pretty = message.mid(colon + 1);
        break;
    }
    case PT_OFFSET:
    {
        pretty = ircPrint.m_szMessage;
        qsizetype begin = 0;
        for (BYTE index = 0; index < ircPrint.m_offset; ++index) {
            begin = pretty.indexOf(QLatin1Char(' '), begin);
            if (begin < 0) return;
            ++begin;
        }
        pretty = pretty.mid(begin);
        break;
    }
    case PT_NONE:
        newLine = newLine || ircPrint.m_bNewLine;
        return;
    case PT_WHOLESTRING:
        pretty = ircPrint.m_szMessage;
        break;
    default:
        return;
    }

    const qsizetype carriageReturn = pretty.indexOf(QLatin1Char('\r'));
    if (carriageReturn >= 0) pretty.truncate(carriageReturn);
    if (newLine) {
        pretty.prepend(QLatin1Char('\n'));
        if (formatting) PushFormattingOffsets(formatting, 1);
    }
    newLine = ircPrint.m_bNewLine;

    QTextCharFormat format;
    format.setForeground(QColor(GetRValue(ircPrint.m_crTextColor),
                                GetGValue(ircPrint.m_crTextColor),
                                GetBValue(ircPrint.m_crTextColor)));
    const QByteArray bytes = pretty.toUtf8();
    status->m_textCore.iDisplayInfo(
        nullptr, 0, "", 0, bytes.constData(), bytes.size(),
        mtGetInfo, msParticipant, &format, -1,
        formatting && formatting->GetSize() ? formatting->GetData() : nullptr,
        formatting ? formatting->GetSize() : 0);
    if (status->GetDocument()) status->GetDocument()->RegisterNewContent();
    Q_UNUSED(line);
}

CStatusView::CStatusView(CChatDoc* document, QWidget* parent)
    : CTextView(document, parent)
{
    m_textCore.bSetInsertBlank(TEXT_VIEW_BLANK_NEVER);
}

int CStatusView::LoadContextMenu(QMenu& menu)
{
    AppendStatusItems(menu,
        originalMenuResource(QStringLiteral("IDR_STATUSVIEW")));
    return 0;
}
