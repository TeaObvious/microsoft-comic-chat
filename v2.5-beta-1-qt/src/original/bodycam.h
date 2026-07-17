// Ported from v2.5-beta-1-modern/bodycam.h.
// QWidget/QPainter/QImage replace CWnd/CDC and the retained GDI bitmap. The
// original emotion geometry, event branches and CBody drawing entry points
// remain in this module.

#pragma once

#include "avatar.h"

#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QWidget>

class QImage;
class QPainter;

class CBodyCam : public QWidget {
public:
    explicit CBodyCam(QWidget* parent = nullptr);

    void EnableDoubleClick(BOOL enable) { m_bEnableDblClk = enable; }
    void CacheBullSide(int windowWidth);
    void DrawBullsEye(QPainter* painter, const QRect& clientRect);
    void DrawBullsEyeCons(QPainter* painter, const QRect& clientRect);
    void DrawCursor(const QPoint& point, QPainter* painter);
    QRect GetIconRect(int index) const;
    void SetEmotion(CEmotion& emotion) { m_emotion = emotion; }
    CEmotion GetEmotionFromPoint(const QPoint& point) const;
    QPoint GetPointFromEmotion(const CEmotion& emotion) const;
    void UpdateEmotion(CEmotion& emotion);
    QRect GetBodyRect() const;
    RECT DrawBody(QPainter* painter, CBody* body);
    void RefreshBody();

    CAvatarX* m_avatar = nullptr;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    const QString* StringFromEmotion(const CEmotion& emotion) const;

    BOOL m_mouseDown = FALSE;
    BOOL m_bullDisabled = TRUE;
    CEmotion m_emotion;
    QPoint m_cursorPos;
    QPoint m_bullsEye;
    short m_bullRadius = 0;
    short m_bullSide = 0;
    RECT m_bodyRect{};
    const QString* m_lastEmotionString = nullptr;
    BOOL m_bEnableDblClk = TRUE;
    BOOL m_bGainedFocusImplicitly = FALSE;
    QPointer<QWidget> m_hwndTookFocusFrom;
    static short m_cursorRadius;
    static short m_iconWidth;
    static short m_iconHeight;
};

CBodyCam* GetBodyCam();
void LoadEmotionStrings();
void UpdateEmotion(CEmotion& emotion);
void RefreshBodyCam(CAvatarX* avatar = nullptr);
void DetachBodyCamAvatar();
