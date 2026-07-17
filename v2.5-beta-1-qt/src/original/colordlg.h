// ColorDlg.h : Qt boundary for the original fixed 16-colour picker.

#pragma once

#include "wincompat.h"

#include <QDialog>
#include <QPoint>
#include <QWidget>

#include <array>

const int g_nColorWidth = 11;
const int g_nColorHeight = 11;
const int g_nMarginWidth = 10;
const int g_nMarginHeight = 10;
const int g_nIntervalWidth = 6;
const int g_nIntervalHeight = 6;

inline constexpr COLORREF clrTable[] = {
    RGB(0,0,0), RGB(128,0,0), RGB(0,128,0), RGB(128,128,0),
    RGB(0,0,128), RGB(128,0,128), RGB(0,128,128), RGB(128,128,128),
    RGB(192,192,192), RGB(255,0,0), RGB(0,255,0), RGB(255,255,0),
    RGB(0,0,255), RGB(255,0,255), RGB(0,255,255), RGB(255,255,255)
};

class CColorDlg;

class CColorStatic : public QWidget
{
public:
    explicit CColorStatic(QWidget* parent = nullptr);

    CColorDlg* m_pColorDlg = nullptr;
    QPoint m_position;
    SHORT m_iIndex = -1;

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
};

class CColorEdit : public QWidget
{
public:
    explicit CColorEdit(QWidget* parent = nullptr);

    CColorDlg* m_pColorDlg = nullptr;
    SHORT m_iIndex = -1;
};

class CColorDlg : public QDialog
{
public:
    explicit CColorDlg(LONG lInitialColor, QWidget* parent = nullptr);

    void SetCursorPos(const QPoint& point, SHORT index);
    BOOL GetSelectedColorRGB(COLORREF* color) const;
    void OnColorClick(UINT id);

    std::array<CColorStatic*, 16> m_color{};
    SHORT m_iIndex = -1;
    QPoint m_point;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void OnOK();
    void positionFrame(QWidget* frame, SHORT index);

    QWidget* m_cursor = nullptr;
    QWidget* m_selection = nullptr;
    SHORT m_nSelectedColor = -1;
};
