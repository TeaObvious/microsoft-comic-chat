// ColorDlg.cpp : implementation of the original fixed colour table.

#include "colordlg.h"

#include "originalassets.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace {
constexpr UINT IDC_COLOR1 = 1135;

QColor colorFromRef(COLORREF color)
{
    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
}
}

CColorStatic::CColorStatic(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void CColorStatic::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pColorDlg && m_pColorDlg->m_point != event->position().toPoint()) {
        m_pColorDlg->m_point = event->position().toPoint();
        m_pColorDlg->SetCursorPos(m_position, m_iIndex);
    }
    QWidget::mouseMoveEvent(event);
}

void CColorStatic::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pColorDlg)
        m_pColorDlg->OnColorClick(IDC_COLOR1 + static_cast<UINT>(m_iIndex));
    QWidget::mousePressEvent(event);
}

void CColorStatic::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), colorFromRef(clrTable[m_iIndex]));
    painter.setPen(Qt::black);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

CColorEdit::CColorEdit(QWidget* parent)
    : QWidget(parent)
{
    hide();
}

CColorDlg::CColorDlg(LONG lInitialColor, QWidget* parent)
    : QDialog(parent, Qt::Tool | Qt::Dialog)
{
    setWindowTitle(originalDialogCaption(QStringLiteral("IDD_CHOOSECOLOR")));
    setModal(true);
    setFixedSize(2 * g_nMarginWidth + 8 * g_nColorWidth + 7 * g_nIntervalWidth,
                 2 * g_nMarginHeight + 2 * g_nColorHeight + g_nIntervalHeight);

    if (lInitialColor >= 0) {
        for (SHORT index = 0; index < 16; ++index) {
            if (clrTable[index] == static_cast<COLORREF>(lInitialColor)) {
                m_iIndex = m_nSelectedColor = index;
                break;
            }
        }
    }

    m_selection = new QWidget(this);
    m_selection->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_selection->setStyleSheet(QStringLiteral("background:#ffffff;border:1px solid #808080;"));
    m_selection->hide();
    m_cursor = new QWidget(this);
    m_cursor->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_cursor->setStyleSheet(QStringLiteral("background:#c0c0c0;border:1px solid #808080;"));
    m_cursor->hide();

    for (SHORT index = 0; index < 16; ++index) {
        const int column = index % 8;
        const int row = index / 8;
        const int left = g_nMarginWidth
            + (g_nColorWidth + g_nIntervalWidth) * column;
        const int top = g_nMarginHeight
            + row * (g_nColorHeight + g_nIntervalHeight);
        auto* color = new CColorStatic(this);
        color->m_pColorDlg = this;
        color->m_position = QPoint(left, top);
        color->m_iIndex = index;
        color->setGeometry(left, top, g_nColorWidth, g_nColorHeight);
        m_color[index] = color;
    }

    m_cursor->lower();
    m_selection->lower();
    if (m_nSelectedColor != -1) positionFrame(m_selection, m_nSelectedColor);
    setFocus(Qt::OtherFocusReason);
}

void CColorDlg::positionFrame(QWidget* frame, SHORT index)
{
    if (!frame || index < 0 || index >= 16) return;
    const QPoint point = m_color[index]->m_position;
    frame->setGeometry(point.x() - g_nIntervalWidth / 2,
                       point.y() - g_nIntervalHeight / 2,
                       g_nIntervalWidth + g_nColorWidth,
                       g_nIntervalHeight + g_nColorHeight);
    frame->show();
    frame->lower();
}

void CColorDlg::SetCursorPos(const QPoint& point, SHORT index)
{
    if (index < 0 || index >= 16) return;
    m_cursor->setGeometry(point.x() - g_nIntervalWidth / 2,
                          point.y() - g_nIntervalHeight / 2,
                          g_nIntervalWidth + g_nColorWidth,
                          g_nIntervalHeight + g_nColorHeight);
    m_cursor->show();
    m_cursor->lower();
    m_iIndex = index;
}

BOOL CColorDlg::GetSelectedColorRGB(COLORREF* color) const
{
    Q_ASSERT(color);
    if (!color || m_nSelectedColor < 0) return FALSE;
    Q_ASSERT(m_nSelectedColor < 16);
    *color = clrTable[m_nSelectedColor];
    return TRUE;
}

void CColorDlg::OnColorClick(UINT id)
{
    m_nSelectedColor = static_cast<SHORT>(id - IDC_COLOR1);
    done(static_cast<int>(id));
}

void CColorDlg::OnOK()
{
    m_nSelectedColor = m_iIndex;
    accept();
}

void CColorDlg::keyPressEvent(QKeyEvent* event)
{
    SHORT newIndex = 16;
    switch (event->key()) {
    case Qt::Key_Home: newIndex = 0; break;
    case Qt::Key_End: newIndex = 15; break;
    case Qt::Key_Left:
        if (m_iIndex == 0) newIndex = 7;
        else if (m_iIndex == 8) newIndex = 15;
        else newIndex = static_cast<SHORT>(m_iIndex - 1);
        break;
    case Qt::Key_Right:
        if (m_iIndex == 7) newIndex = 0;
        else if (m_iIndex == 15) newIndex = 8;
        else newIndex = static_cast<SHORT>(m_iIndex + 1);
        break;
    case Qt::Key_Up:
    case Qt::Key_Down:
        newIndex = m_iIndex > 7
            ? static_cast<SHORT>(m_iIndex - 8)
            : static_cast<SHORT>(m_iIndex + 8);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        OnOK();
        return;
    default:
        QDialog::keyPressEvent(event);
        return;
    }
    if (newIndex < 16)
        SetCursorPos(m_color[newIndex]->m_position, newIndex);
    event->accept();
}
