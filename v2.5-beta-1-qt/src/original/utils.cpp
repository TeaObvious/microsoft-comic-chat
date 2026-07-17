// Ported from v2.5-beta-1-modern/utils.cpp: CSimpleComboBox.

#include "utils.h"

#include <QEvent>
#include <QGuiApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QResizeEvent>
#include <QScreen>

#include <limits>

void MakeRectVisibleOnScreen(QRect* rect)
{
    if (!rect) return;
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.isEmpty()) return;

    QScreen* nearest = nullptr;
    qint64 largestIntersection = 0;
    qint64 nearestDistance = std::numeric_limits<qint64>::max();
    for (QScreen* screen : screens) {
        const QRect work = screen->availableGeometry();
        const QRect intersection = rect->intersected(work);
        const qint64 intersectionArea = intersection.isEmpty() ? 0
            : static_cast<qint64>(intersection.width()) * intersection.height();
        if (intersectionArea > 0) {
            if (intersectionArea > largestIntersection) {
                nearest = screen;
                largestIntersection = intersectionArea;
            }
            continue;
        }
        if (largestIntersection > 0) continue;

        const int rectRight = rect->x() + rect->width();
        const int rectBottom = rect->y() + rect->height();
        const int workRight = work.x() + work.width();
        const int workBottom = work.y() + work.height();
        const qint64 dx = rectRight <= work.x()
            ? work.x() - rectRight
            : rect->x() >= workRight ? rect->x() - workRight : 0;
        const qint64 dy = rectBottom <= work.y()
            ? work.y() - rectBottom
            : rect->y() >= workBottom ? rect->y() - workBottom : 0;
        const qint64 distance = dx * dx + dy * dy;
        if (distance < nearestDistance) {
            nearest = screen;
            nearestDistance = distance;
        }
    }
    if (!nearest) return;

    const QRect work = nearest->availableGeometry();
    const int workRight = work.x() + work.width();
    const int workBottom = work.y() + work.height();
    int width = rect->width();
    int height = rect->height();
    if (rect->x() >= work.x() && rect->y() >= work.y()
        && rect->x() + width <= workRight
        && rect->y() + height <= workBottom) {
        return;
    }

    width = qMin(width, work.width());
    height = qMin(height, work.height());
    int x = rect->x();
    int y = rect->y();
    if (x + width > workRight) x = workRight - width;
    if (y + height > workBottom) y = workBottom - height;
    if (x < work.x()) x = work.x();
    if (y < work.y()) y = work.y();
    *rect = QRect(x, y, width, height);
}

CSimpleComboBox::CSimpleComboBox(QWidget* parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
    , m_list(new QListWidget(this))
{
    setFocusProxy(m_edit);
    m_list->setSortingEnabled(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int row) {
        if (row < 0) return;
        if (QListWidgetItem* item = m_list->item(row))
            m_edit->setText(item->text());
    });
    updateEnabledState();
}

void CSimpleComboBox::clear()
{
    m_list->clear();
    m_edit->clear();
}

int CSimpleComboBox::addItem(const QString& text, const QVariant& data)
{
    auto* item = new QListWidgetItem(text);
    item->setData(Qt::UserRole, data);
    m_list->addItem(item);
    m_list->sortItems(Qt::AscendingOrder);
    return m_list->row(item);
}

void CSimpleComboBox::removeItem(int index)
{
    delete m_list->takeItem(index);
}

int CSimpleComboBox::count() const
{
    return m_list->count();
}

int CSimpleComboBox::currentIndex() const
{
    return m_list->currentRow();
}

void CSimpleComboBox::setCurrentIndex(int index)
{
    m_list->setCurrentRow(index);
    if (index < 0) m_list->clearSelection();
}

QString CSimpleComboBox::currentText() const
{
    return m_edit->text();
}

void CSimpleComboBox::setEditText(const QString& text)
{
    m_edit->setText(text);
}

QString CSimpleComboBox::itemText(int index) const
{
    QListWidgetItem* item = m_list->item(index);
    return item ? item->text() : QString();
}

QVariant CSimpleComboBox::itemData(int index) const
{
    QListWidgetItem* item = m_list->item(index);
    return item ? item->data(Qt::UserRole) : QVariant();
}

int CSimpleComboBox::findText(const QString& text,
                              Qt::CaseSensitivity sensitivity) const
{
    for (int index = 0; index < m_list->count(); ++index) {
        if (itemText(index).compare(text, sensitivity) == 0) return index;
    }
    return -1;
}

void CSimpleComboBox::setMaxLength(int length)
{
    m_edit->setMaxLength(length);
}

void CSimpleComboBox::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const int editHeight = qMin(height(), m_edit->sizeHint().height());
    m_edit->setGeometry(0, 0, width(), editHeight);
    m_list->setGeometry(0, qMax(0, editHeight - 1), width(),
                        qMax(0, height() - editHeight + 1));
}

void CSimpleComboBox::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange
        || event->type() == QEvent::PaletteChange) {
        updateEnabledState();
    }
}

void CSimpleComboBox::updateEnabledState()
{
    m_edit->setEnabled(isEnabled());
    m_list->setEnabled(isEnabled());
}
