// Ported from v2.5-beta-1-modern/utils.h.
//
// QWidget replaces the Win32 CBS_SIMPLE control boundary; the edit/list
// behaviour and the original class name stay in this module. FILEENUMSTRUCT
// and EnumFiles retain the recursive source file-enumeration boundary used by
// the Automation rule editor.

#pragma once

#include "wincompat.h"

#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QWidget>

class QLineEdit;
class QListWidget;

using FILEENUMADDFN = void (*)(qintptr context, const QString& path,
                               const QString& fileName, int fileType);

struct FILEENUMSTRUCT {
    const char* pszTypes = nullptr;
    FILEENUMADDFN pfnAdd = nullptr;
    qintptr lParam = 0;
    BOOL bRecursive = FALSE;
    const char* pszSubFilter = nullptr;
};

void EnumFiles(const QString& path, FILEENUMSTRUCT* fileEnum);
void MakeRectVisibleOnScreen(QRect* rect);

class CSimpleComboBox : public QWidget {
public:
    explicit CSimpleComboBox(QWidget* parent = nullptr);

    QLineEdit* lineEdit() const { return m_edit; }
    QListWidget* listWidget() const { return m_list; }

    void clear();
    int addItem(const QString& text, const QVariant& data = QVariant());
    void removeItem(int index);
    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    QString currentText() const;
    void setEditText(const QString& text);
    QString itemText(int index) const;
    QVariant itemData(int index) const;
    int findText(const QString& text,
                 Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive) const;
    void setMaxLength(int length);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void updateEnabledState();

    QLineEdit* m_edit = nullptr;
    QListWidget* m_list = nullptr;
};
