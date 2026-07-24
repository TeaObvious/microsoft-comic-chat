// Ported from v2.5-beta-1-modern/rtfctrl.cpp.

#include "rtfctrl.h"

#include "chat.h"
#include "colordlg.h"
#include "format.h"
#include "mainfrm.h"
#include "originalassets.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QTextBlockFormat>
#include <QTextCursor>

#include <functional>

namespace {
CAccelTable accelRTF(QStringLiteral("IDR_RTFACCEL"));
bool specialFontsInitialized = false;

QColor qColor(COLORREF color)
{
    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
}

QFont defaultApplicationFont()
{
    return QApplication::font();
}

QString acceleratorText(const OriginalAccelerator& accelerator)
{
    QStringList parts;
    if (accelerator.control) parts.append(QStringLiteral("Ctrl"));
    if (accelerator.alt) parts.append(QStringLiteral("Alt"));
    if (accelerator.shift) parts.append(QStringLiteral("Shift"));
    parts.append(accelerator.key);
    return parts.join(QLatin1Char('+'));
}
}

SHORT CRtfCtrl::m_nFixedPitchIndex = -1;
SHORT CRtfCtrl::m_nSymbolIndex = -1;

QString CAccelTable::Lookup(const QKeyEvent* event) const
{
    if (!event || event->isAutoRepeat()) return {};
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    for (const OriginalAccelerator& accelerator
         : originalAcceleratorResource(m_resourceIdentifier)) {
        if (accelerator.control != modifiers.testFlag(Qt::ControlModifier)
            || accelerator.shift != modifiers.testFlag(Qt::ShiftModifier)
            || accelerator.alt != modifiers.testFlag(Qt::AltModifier))
            continue;
        if (accelerator.key.size() == 1
            && event->key() == accelerator.key.front().toUpper().unicode())
            return accelerator.commandIdentifier;
    }
    return {};
}

CRtfCtrl::CRtfCtrl(QWidget* parent)
    : QTextEdit(parent)
{
    if (!specialFontsInitialized) {
        m_nFixedPitchIndex = nGetSpecialFontIndex(TRUE);
        m_nSymbolIndex = nGetSpecialFontIndex(FALSE);
        specialFontsInitialized = true;
    }
    setAcceptRichText(true);
    m_crTextColor = RGB(0, 0, 0);
    DefineDefaultCharFormat();
    UseDefaultCharFormat();
}

CRtfCtrl::~CRtfCtrl()
{
    FreeAndNullFormatting(&m_prgdwFormatting);
}

void CRtfCtrl::DefineDefaultCharFormat()
{
    m_font = defaultApplicationFont();
    m_pFont = &m_font;
}

QTextCharFormat CRtfCtrl::defaultCharFormat() const
{
    QTextCharFormat format;
    format.setFont(m_pFont ? *m_pFont : font());
    format.setForeground(qColor(m_crTextColor));
    return format;
}

void CRtfCtrl::UseDefaultCharFormat(BOOL updateSelection)
{
    const QTextCharFormat format = defaultCharFormat();
    setCurrentFont(format.font());
    setTextColor(qColor(m_crTextColor));
    if (updateSelection) {
        QTextCursor cursor = textCursor();
        cursor.mergeCharFormat(format);
        setTextCursor(cursor);
    }
}

BOOL CRtfCtrl::bSetIndent(LONG indent)
{
    QTextCursor cursor = textCursor();
    QTextBlockFormat format = cursor.blockFormat();
    const qreal dpi = logicalDpiX() > 0 ? logicalDpiX() : 96.0;
    format.setLeftMargin(indent * dpi / 1440.0);
    cursor.setBlockFormat(format);
    setTextCursor(cursor);
    return TRUE;
}

BOOL CRtfCtrl::bShowDosKeyEntry(BOOL previous)
{
    if (!m_pDosKey) return FALSE;
    CDWordArray* formatting = nullptr;
    const QString text = previous
        ? m_pDosKey->StrGetPrevEntry(&formatting)
        : m_pDosKey->StrGetNextEntry(&formatting);
    const BOOL result = bSetWindowFormattedText(text, formatting);
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
    return result;
}

QTextCharFormat CRtfCtrl::charFormatAt(int position) const
{
    const QString text = toPlainText();
    if (text.isEmpty()) return currentCharFormat();
    QTextCursor cursor(document());
    const int bounded = qBound(0, position, text.size() - 1);
    cursor.setPosition(bounded);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    return cursor.charFormat();
}

bool CRtfCtrl::selectionHasProperty(
    const std::function<bool(const QTextCharFormat&)>& predicate) const
{
    const QTextCursor selection = textCursor();
    if (!selection.hasSelection()) return predicate(currentCharFormat());
    for (int position = selection.selectionStart();
         position < selection.selectionEnd(); ++position) {
        if (!predicate(charFormatAt(position))) return false;
    }
    return true;
}

WORD CRtfCtrl::wGetConsistentFormats() const
{
    WORD format = 0;
    if (selectionHasProperty([](const QTextCharFormat& value) {
            return value.fontWeight() >= QFont::Bold;
        })) format |= wBold;
    if (selectionHasProperty([](const QTextCharFormat& value) {
            return value.fontItalic();
        })) format |= wItalic;
    if (selectionHasProperty([](const QTextCharFormat& value) {
            return value.fontUnderline();
        })) format |= wUnderline;
    if (m_nFixedPitchIndex >= 0
        && selectionHasProperty([](const QTextCharFormat& value) {
            return FFixedPitchFont(value.fontFamily()) == m_nFixedPitchIndex;
        })) format |= wFixedPitch;

    // Preserve the original source's index expression: the availability test
    // uses m_nSymbolIndex, while the face comparison uses m_nFixedPitchIndex.
    const SHORT originalSymbolCheckIndex = m_nFixedPitchIndex;
    if (m_nSymbolIndex >= 0 && originalSymbolCheckIndex >= 0
        && originalSymbolCheckIndex < SYMBOLNUMBER
        && selectionHasProperty([originalSymbolCheckIndex](const QTextCharFormat& value) {
            return value.fontFamily().compare(
                QString::fromLatin1(SYMBOLFACENAMES[originalSymbolCheckIndex]),
                Qt::CaseInsensitive) == 0;
        })) format |= wSymbol;
    return format;
}

void CRtfCtrl::SwitchSelectionFormat(WORD selectedFormat)
{
    Q_ASSERT(selectedFormat);
    const WORD consistent = wGetConsistentFormats();
    QTextCharFormat format;

    if (selectedFormat & wForeground) {
        LONG initialColor = -1;
        const QTextCursor selection = textCursor();
        const QTextCharFormat first = selection.hasSelection()
            ? charFormatAt(selection.selectionStart()) : currentCharFormat();
        const QColor firstColor = first.foreground().color();
        const bool sameColor = selectionHasProperty([firstColor](const QTextCharFormat& value) {
            return value.foreground().color() == firstColor;
        });
        if (sameColor && firstColor.isValid())
            initialColor = static_cast<LONG>(RGB(firstColor.red(), firstColor.green(), firstColor.blue()));

        CColorDlg dialog(initialColor, this);
        m_bColorWnd = TRUE;
        m_bSelectAll = FALSE;
        dialog.exec();
        m_bColorWnd = FALSE;
        COLORREF color;
        if (dialog.GetSelectedColorRGB(&color)) {
            // COLOR_WINDOW is fixed white in the required Windows 98 palette.
            format.setForeground(color == RGB(255, 255, 255)
                                     ? QColor(0, 0, 0) : qColor(color));
        }
    }
    if (selectedFormat & wBold)
        format.setFontWeight((consistent & wBold) ? QFont::Normal : QFont::Bold);
    if (selectedFormat & wItalic)
        format.setFontItalic(!(consistent & wItalic));
    if (selectedFormat & wUnderline)
        format.setFontUnderline(!(consistent & wUnderline));

    const QFont selectedDefault = theApp.m_bComicView
        ? theApp.m_comicsFont : theApp.m_textFont;
    QFont ordinary = selectedDefault;
    if (FFixedPitchFont(ordinary.family()) >= 0
        || FSymbolFont(ordinary.family()) >= 0
        || ordinary.fixedPitch()) {
        ordinary = defaultApplicationFont();
    }
    if ((selectedFormat & wFixedPitch) && m_nFixedPitchIndex >= 0) {
        if (!(consistent & wFixedPitch)) {
            format.setFontFamily(QString::fromLatin1(
                FIXEDPITCHFACENAMES[m_nFixedPitchIndex]));
            format.setFontFixedPitch(true);
        } else {
            format.setFontFamily(ordinary.family());
            format.setFontFixedPitch(false);
        }
    }
    if ((selectedFormat & wSymbol) && m_nSymbolIndex >= 0) {
        if (!(consistent & wSymbol)) {
            format.setFontFamily(QString::fromLatin1(SYMBOLFACENAMES[m_nSymbolIndex]));
            format.setFontFixedPitch(false);
        } else {
            format.setFontFamily(ordinary.family());
            format.setFontFixedPitch(false);
        }
    }
    if (!format.isEmpty()) mergeCurrentCharFormat(format);
    MatchButtonsToSelection();
}

void CRtfCtrl::MatchButtonsToSelection()
{
    m_wMenuFormatStyles = wGetConsistentFormats();
}

void CRtfCtrl::executeCommand(const QString& commandIdentifier)
{
    if (commandIdentifier == QLatin1String("ID_EDIT_UNDO")) undo();
    else if (commandIdentifier == QLatin1String("ID_EDIT_CUT")) cut();
    else if (commandIdentifier == QLatin1String("ID_EDIT_COPY")) copy();
    else if (commandIdentifier == QLatin1String("ID_EDIT_PASTE")) paste();
    else if (commandIdentifier == QLatin1String("ID_EDIT_DELETE")) {
        QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) cursor.removeSelectedText();
        else cursor.deleteChar();
    } else if (commandIdentifier == QLatin1String("ID_SETCOLOR")) SwitchSelectionFormat(wForeground);
    else if (commandIdentifier == QLatin1String("ID_SWITCHBOLD")) SwitchSelectionFormat(wBold);
    else if (commandIdentifier == QLatin1String("ID_SWITCHITALIC")) SwitchSelectionFormat(wItalic);
    else if (commandIdentifier == QLatin1String("ID_SWITCHUNDERLINED")) SwitchSelectionFormat(wUnderline);
    else if (commandIdentifier == QLatin1String("ID_SWITCHFIXEDPITCH")) SwitchSelectionFormat(wFixedPitch);
    else if (commandIdentifier == QLatin1String("ID_SWITCHSYMBOL")) SwitchSelectionFormat(wSymbol);
}

void CRtfCtrl::ShowFormattingPopUp(LONG x, LONG y)
{
    ShowFormattingPopUp(mapToGlobal(QPoint(static_cast<int>(x), static_cast<int>(y))));
}

void CRtfCtrl::ShowFormattingPopUp(const QPoint& globalPoint)
{
    const QList<OriginalMenuItem> roots = originalMenuResource(QStringLiteral("IDR_FORMATTING"));
    if (m_nPopupMenuIndex >= static_cast<UINT>(roots.size())) return;
    const OriginalMenuItem& root = roots.at(static_cast<int>(m_nPopupMenuIndex));
    QMenu menu(this);
    m_wMenuFormatStyles = wGetConsistentFormats();
    for (const OriginalMenuItem& item : root.children) {
        if (item.type == OriginalMenuItemType::Separator) {
            menu.addSeparator();
            continue;
        }
        if (item.type != OriginalMenuItemType::Command) continue;
        QAction* action = menu.addAction(item.text);
        action->setData(item.commandIdentifier);
        const QString id = item.commandIdentifier;
        if (id == QLatin1String("ID_EDIT_UNDO")) action->setEnabled(document()->isUndoAvailable());
        else if (id == QLatin1String("ID_EDIT_CUT") || id == QLatin1String("ID_EDIT_COPY")
                 || id == QLatin1String("ID_EDIT_DELETE")) action->setEnabled(textCursor().hasSelection());
        else if (id == QLatin1String("ID_EDIT_PASTE"))
            action->setEnabled(QApplication::clipboard()->mimeData()->hasText());
        WORD bit = 0;
        if (id == QLatin1String("ID_SWITCHBOLD")) bit = wBold;
        else if (id == QLatin1String("ID_SWITCHITALIC")) bit = wItalic;
        else if (id == QLatin1String("ID_SWITCHUNDERLINED")) bit = wUnderline;
        else if (id == QLatin1String("ID_SWITCHFIXEDPITCH")) bit = wFixedPitch;
        else if (id == QLatin1String("ID_SWITCHSYMBOL")) bit = wSymbol;
        if (bit) {
            action->setCheckable(true);
            action->setChecked(m_wMenuFormatStyles & bit);
        }
    }
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->ConfigureContextMenu(&menu);
    QAction* selected = menu.exec(globalPoint);
    if (selected) executeCommand(selected->data().toString());
}

BOOL CRtfCtrl::bSetWindowFormattedText(const QString& text, CDWordArray* formatting)
{
    clear();
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::Start);
    QTextCharFormat chunk = defaultCharFormat();
    WORD currentFormat = 0;
    int currentOffset = 0;
    const auto applyWord = [&](WORD word, QTextCharFormat* target) {
        QFont value = target->font();
        value.setBold(word & wBold);
        value.setItalic(word & wItalic);
        value.setUnderline(word & wUnderline);
        if ((word & wFixedPitch) && m_nFixedPitchIndex >= 0) {
            value.setFamily(QString::fromLatin1(FIXEDPITCHFACENAMES[m_nFixedPitchIndex]));
            value.setFixedPitch(true);
        } else if ((word & wSymbol) && m_nSymbolIndex >= 0) {
            value.setFamily(QString::fromLatin1(SYMBOLFACENAMES[m_nSymbolIndex]));
            value.setFixedPitch(false);
        } else if (m_pFont) {
            value.setFamily(m_pFont->family());
            value.setFixedPitch(m_pFont->fixedPitch());
        }
        target->setFont(value);
        if (word & wForeground) {
            COLORREF color = GetRBGColor(static_cast<BYTE>((word >> 4) & 0x000f));
            target->setForeground(color == RGB(255,255,255)
                                      ? qColor(m_crTextColor) : qColor(color));
        } else {
            target->setForeground(qColor(m_crTextColor));
        }
    };

    if (formatting && formatting->GetSize() && m_pFont) {
        for (int index = 0; index < formatting->GetSize(); ++index) {
            const DWORD element = formatting->GetAt(index);
            const int nextOffset = qMin<int>(HIWORD(element), text.size());
            if (nextOffset > currentOffset)
                cursor.insertText(text.mid(currentOffset, nextOffset - currentOffset), chunk);
            currentFormat = LOWORD(element);
            applyWord(currentFormat, &chunk);
            currentOffset = nextOffset;
        }
    }
    if (currentOffset < text.size()) cursor.insertText(text.mid(currentOffset), chunk);
    setTextCursor(cursor);
    setCurrentCharFormat(chunk);
    return TRUE;
}

BOOL CRtfCtrl::bSetTextColor(COLORREF textColor)
{
    m_crTextColor = textColor;
    QTextCharFormat format;
    format.setForeground(qColor(textColor));
    mergeCurrentCharFormat(format);
    return TRUE;
}

bool CRtfCtrl::IsEmpty() const
{
    return toPlainText().trimmed().isEmpty();
}

void CRtfCtrl::keyPressEvent(QKeyEvent* event)
{
    if (m_pDosKey) {
        if (event->key() == Qt::Key_Up && textCursor().blockNumber() == 0) {
            bShowDosKeyEntry(TRUE);
            return;
        }
        if (event->key() == Qt::Key_Down
            && textCursor().blockNumber() == document()->blockCount() - 1) {
            bShowDosKeyEntry(FALSE);
            return;
        }
        m_pDosKey->SeekToEnd();
    }
    const QString command = accelRTF.Lookup(event);
    if (!command.isEmpty()) {
        executeCommand(command);
        return;
    }
    if (!m_bAcceptMultiLine && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
        return;
    QTextEdit::keyPressEvent(event);
}

void CRtfCtrl::focusInEvent(QFocusEvent* event)
{
    if (m_bSelectAll) selectAll();
    QTextEdit::focusInEvent(event);
}

void CRtfCtrl::focusOutEvent(QFocusEvent* event)
{
    if (!m_bColorWnd) m_bSelectAll = TRUE;
    QTextEdit::focusOutEvent(event);
}

void CRtfCtrl::mousePressEvent(QMouseEvent* event)
{
    m_bSelectAll = FALSE;
    QTextEdit::mousePressEvent(event);
}

void CRtfCtrl::contextMenuEvent(QContextMenuEvent* event)
{
    ShowFormattingPopUp(event->globalPos());
}
