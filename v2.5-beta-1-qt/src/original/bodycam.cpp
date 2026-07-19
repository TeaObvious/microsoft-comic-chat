// Ported from v2.5-beta-1-modern/bodycam.cpp.
// QPainter and a transient QImage replace CDC and the retained DIB section.
// Emotion geometry, body rectangles, raster-operation order and input branches
// follow the original functions in this file.

#include "bodycam.h"

#include "backdrop.h"
#include "chat.h"
#include "chatdoc.h"
#include "memblst.h"
#include "protsupp.h"
#include "resource.h"
#include "paintdc.h"
#include "originalassets.h"
#include "saywnd.h"
#include "vector2d.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace {
QString emotionName[9];
QPointer<CBodyCam> characterSelectionBodyCam;

const QVector<QImage>& bodyCamIcons()
{
    static const QVector<QImage> icons = [] {
        const QStringList files = {
            QStringLiteral("fc_hap_l.bmp"),
            QStringLiteral("fc_coy_l.bmp"),
            QStringLiteral("fc_bor_l.bmp"),
            QStringLiteral("fc_sca_l.bmp"),
            QStringLiteral("fc_sad_l.bmp"),
            QStringLiteral("fc_ang_l.bmp"),
            QStringLiteral("fc_sho_l.bmp"),
            QStringLiteral("fc_laf_l.bmp"),
        };
        QVector<QImage> result;
        result.reserve(files.size());
        for (const QString& file : files) {
            CAvatarFileStream stream(originalResourcePath(file));
            CChatBackdrop* bitmap = CChatBackdrop::LoadBackdrop(&stream);
            result.append(bitmap && bitmap->GetDrawing()
                              ? bitmap->GetDrawing()->Image()
                              : QImage());
            delete bitmap;
        }
        return result;
    }();
    return icons;
}

RECT emptyRect()
{
    return {0, 0, 0, 0};
}
}

void LoadEmotionStrings()
{
    static const QString identifiers[] = {
        QStringLiteral("ID_EM_HAPPY"),
        QStringLiteral("ID_EM_COY"),
        QStringLiteral("ID_EM_BORED"),
        QStringLiteral("ID_EM_SCARED"),
        QStringLiteral("ID_EM_SAD"),
        QStringLiteral("ID_EM_ANGRY"),
        QStringLiteral("ID_EM_SHOUT"),
        QStringLiteral("ID_EM_LAUGH"),
        QStringLiteral("ID_EM_NEUTRAL"),
    };
    for (int index = 0; index < 9; ++index) {
        emotionName[index] = originalResourceString(identifiers[index]);
    }
}

short CBodyCam::m_cursorRadius = 5;
short CBodyCam::m_iconWidth = 20;
short CBodyCam::m_iconHeight = 26;

CBodyCam::CBodyCam(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    m_emotion.Set(0.0, 0.0);
    if (emotionName[0].isNull()) {
        LoadEmotionStrings();
    }
}

void CBodyCam::CacheBullSide(int windowWidth)
{
    m_bullSide = static_cast<short>(qMin(windowWidth, 159));
    if (m_bullSide < 93) {
        m_bullDisabled = TRUE;
        m_bullSide = 0;
    } else {
        m_bullDisabled = FALSE;
    }
}

void CBodyCam::DrawBullsEye(QPainter* painter, const QRect& clientRect)
{
    const int width = clientRect.width();
    const int halfSide = m_bullSide / 2;
    m_bullsEye.setX((clientRect.left() + clientRect.right() + 1) / 2);
    m_bullsEye.setY(clientRect.bottom() + 1 - halfSide);
    m_bullRadius = static_cast<short>(halfSide - m_cursorRadius - m_iconHeight);

    if (m_bullDisabled) {
        return;
    }

    painter->fillRect(clientRect.left(), clientRect.bottom() + 1 - m_bullSide,
                      width, m_bullSide, QColor(210, 210, 210));
    painter->setPen(Qt::black);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(m_bullsEye, m_bullRadius, m_bullRadius);
    painter->drawLine(m_bullsEye.x(), m_bullsEye.y() - 5,
                      m_bullsEye.x(), m_bullsEye.y() + 5);
    painter->drawLine(m_bullsEye.x() - 5, m_bullsEye.y(),
                      m_bullsEye.x() + 5, m_bullsEye.y());
    m_bullRadius = static_cast<short>(m_bullRadius - m_cursorRadius);
}

QRect CBodyCam::GetIconRect(int index) const
{
    const int offsetFromEye = m_bullRadius + 2 * m_cursorRadius + m_iconHeight / 2;
    const double angle = 2.0 * PI * index / NEMOTIONS;
    const POINT vector = dpoint_to_point(point_scalmult(offsetFromEye, angle_to_vector(angle)));
    const QPoint center(m_bullsEye.x() + vector.x, m_bullsEye.y() + vector.y);
    return QRect(center.x() - m_iconWidth / 2, center.y() - m_iconHeight / 2,
                 m_iconWidth, m_iconHeight);
}

void CBodyCam::DrawBullsEyeCons(QPainter* painter, const QRect&)
{
    const QVector<QImage>& icons = bodyCamIcons();
    for (int index = 0; index < NEMOTIONS; ++index) {
        if (index < icons.size() && !icons[index].isNull()) {
            painter->drawImage(GetIconRect(index).topLeft(), icons[index]);
        }
    }
}

void CBodyCam::DrawCursor(const QPoint& point, QPainter* painter)
{
    painter->setPen(Qt::black);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(point, m_cursorRadius, m_cursorRadius);
}

CEmotion CBodyCam::GetEmotionFromPoint(const QPoint& point) const
{
    CEmotion emotion;
    const POINT vector{point.x() - m_bullsEye.x(), point.y() - m_bullsEye.y()};
    if (m_bullRadius <= 0) {
        return emotion;
    }
    emotion.m_intensity = static_cast<float>(point_magn(vector) / m_bullRadius);
    emotion.m_intensity = std::min(emotion.m_intensity, 1.0f);
    if (emotion.m_intensity < 0.2f) {
        emotion.m_intensity = 0.0f;
    }
    emotion.m_emotion = emotion.m_intensity == 0.0f
        ? 0.0f
        : static_cast<float>(vector_to_angle(vector));
    return emotion;
}

QPoint CBodyCam::GetPointFromEmotion(const CEmotion& emotion) const
{
    DPOINT vector = angle_to_vector(emotion.m_emotion);
    vector = point_scalmult(m_bullRadius * emotion.m_intensity, vector);
    const POINT rounded = dpoint_to_point(vector);
    return QPoint(m_bullsEye.x() + rounded.x, m_bullsEye.y() + rounded.y);
}

const QString* CBodyCam::StringFromEmotion(const CEmotion& emotion) const
{
    if (emotion.m_intensity == 0.0f) {
        return &emotionName[8];
    }
    if (emotion.m_emotion >= 7 * PI / 8 || emotion.m_emotion < -7 * PI / 8) {
        return &emotionName[4];
    }
    if (emotion.m_emotion <= -5 * PI / 8) {
        return &emotionName[5];
    }
    if (emotion.m_emotion <= -3 * PI / 8) {
        return &emotionName[6];
    }
    if (emotion.m_emotion <= -PI / 8) {
        return &emotionName[7];
    }
    if (emotion.m_emotion > 5 * PI / 8) {
        return &emotionName[3];
    }
    if (emotion.m_emotion > 3 * PI / 8) {
        return &emotionName[2];
    }
    if (emotion.m_emotion > PI / 8) {
        return &emotionName[1];
    }
    return &emotionName[0];
}

void CBodyCam::UpdateEmotion(CEmotion& emotion)
{
    const QPoint newPosition = GetPointFromEmotion(emotion);
    if (newPosition == m_cursorPos) {
        return;
    }
    m_cursorPos = newPosition;
    m_emotion = emotion;
    if (m_avatar) {
        m_avatar->UpdateBody(m_avatar->GetBodyFromEmotion(emotion));
    }
    if (m_mouseDown) {
        const QString* emotionString = StringFromEmotion(emotion);
        if (emotionString != m_lastEmotionString) {
            QString status = originalResourceString(QStringLiteral("ID_EMOTION_IS"));
            status.replace(QStringLiteral("%1"), *emotionString);
            theApp.SetStatusPaneString(0, status);
            m_lastEmotionString = emotionString;
        }
    }
    update();
}

QRect CBodyCam::GetBodyRect() const
{
    return QRect(0, 0, width(), qMax(0, height() - m_bullSide));
}

RECT CBodyCam::DrawBody(QPainter* painter, CBody* body)
{
    const QRect bodyRect = GetBodyRect();
    if (!painter || !body || bodyRect.height() <= 0 || bodyRect.width() <= 0) {
        return emptyRect();
    }

    QImage retained(bodyRect.width(), bodyRect.height(), QImage::Format_RGB32);
    retained.fill(Qt::white);
    RECT clientRect{-1000, 0, bodyRect.width() + 1000, bodyRect.height()};
    RECT result = body->DrawBody(&retained, clientRect, FALSE);
    painter->drawImage(bodyRect.topLeft(), retained);
    return result;
}

void CBodyCam::RefreshBody()
{
    update();
}

bool CBodyCam::event(QEvent* event)
{
    // Original OnGetDlgCode returns DLGC_WANTALLKEYS only for the main
    // BodyCam. Intercept Tab before QWidget performs its own focus traversal;
    // the embedded Character-page preview retains normal dialog traversal.
    if (event->type() == QEvent::KeyPress && m_forcedDelete) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            keyPressEvent(key);
            return true;
        }
    }
    if (event->type() == QEvent::ToolTip && !m_bullDisabled) {
        const auto* help = static_cast<QHelpEvent*>(event);
        for (int index = 0; index < NEMOTIONS; ++index) {
            if (GetIconRect(index).contains(help->pos())) {
                QToolTip::showText(help->globalPos(), emotionName[index], this,
                                   GetIconRect(index));
                return true;
            }
        }
        QToolTip::hideText();
    }
    return QWidget::event(event);
}

void CBodyCam::paintEvent(QPaintEvent*)
{
    CacheBullSide(width());
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    DrawBullsEye(&painter, rect());
    if (!m_bullDisabled) {
        DrawBullsEyeCons(&painter, rect());
        m_cursorPos = GetPointFromEmotion(m_emotion);
        DrawCursor(m_cursorPos, &painter);
    }
    if (m_avatar && m_avatar->m_body) {
        m_bodyRect = DrawBody(&painter, m_avatar->m_body);
    }
    if (hasFocus() && !m_bGainedFocusImplicitly) {
        painter.setPen(QPen(Qt::black, 1, Qt::DotLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void CBodyCam::mousePressEvent(QMouseEvent* event)
{
    m_hwndTookFocusFrom = QApplication::focusWidget();
    m_bGainedFocusImplicitly = !hasFocus();
    setFocus();
    if (m_bullDisabled || event->position().y() < height() - m_bullSide) {
        return;
    }
    m_mouseDown = TRUE;
    grabMouse();
    CEmotion emotion = GetEmotionFromPoint(event->position().toPoint());
    UpdateEmotion(emotion);
    if (m_avatar && m_avatar->m_freeze == AF_UNFROZEN) {
        m_avatar->m_freeze = AF_TEMPFROZEN;
    }
}

void CBodyCam::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_bEnableDblClk
        && (!currentRoom || currentRoom->GetConnectionStatus() != CX_CONNECTING)
        && (m_bullDisabled || event->position().y() < height() - m_bullSide)) {
        theApp.DoOptionsDialog(TRUE, IDD_CHARACTERPAGE);
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CBodyCam::mouseMoveEvent(QMouseEvent* event)
{
    if (m_mouseDown) {
        CEmotion emotion = GetEmotionFromPoint(event->position().toPoint());
        UpdateEmotion(emotion);
    }
}

void CBodyCam::mouseReleaseEvent(QMouseEvent*)
{
    m_mouseDown = FALSE;
    if (QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
    if (CChatDoc* document = GetChatDoc()) {
        if (document->GetConnectionStatus() == CX_INCHANNEL) {
            document->ResetStatus();
        } else {
            document->ResetStatus();
        }
    }
    if (m_bGainedFocusImplicitly && hasFocus() && m_hwndTookFocusFrom) {
        m_hwndTookFocusFrom->setFocus();
    }
    m_bGainedFocusImplicitly = FALSE;
    m_hwndTookFocusFrom = nullptr;
}

void CBodyCam::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        if (m_forcedDelete) {
            if (!m_bGainedFocusImplicitly) {
                CChatDoc* document = GetChatDoc();
                if (document) {
                    const BOOL backward = event->key() == Qt::Key_Backtab
                        || event->modifiers().testFlag(Qt::ShiftModifier);
                    document->CycleFocus(CHATFOCUS_EMOTIONWND, backward);
                }
            }
            event->accept();
        } else {
            QWidget::keyPressEvent(event);
        }
        return;
    }

    if (!event->text().isEmpty()) {
        ForwardToSayWnd(event->text().front().unicode());
        event->accept();
        return;
    }

    if (m_mouseDown || m_bullDisabled) {
        event->accept();
        return;
    }
    const int key = event->key();
    if (key != Qt::Key_Up && key != Qt::Key_Down && key != Qt::Key_Left
        && key != Qt::Key_Right && key != Qt::Key_Home) {
        QWidget::keyPressEvent(event);
        return;
    }

    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    if (!control && key != Qt::Key_Home) {
        static const float magnitudes[3] = {0.0f, 0.5f, 1.0f};
        static const float angles[8] = {
            -PI, -3 * PI / 4, -PI / 2, -PI / 4,
            0.0f, PI / 4, PI / 2, 3 * PI / 4,
        };
        CEmotion emotion = m_emotion;
        float* current = nullptr;
        const float* choices = nullptr;
        int choiceCount = 0;
        bool decrease = false;
        bool circular = false;
        if (key == Qt::Key_Up || key == Qt::Key_Down) {
            current = &emotion.m_intensity;
            choices = magnitudes;
            choiceCount = 3;
            decrease = key == Qt::Key_Down;
        } else {
            current = &emotion.m_emotion;
            choices = angles;
            choiceCount = 8;
            decrease = key == Qt::Key_Left;
            circular = true;
        }

        int goTo = circular ? (decrease ? choiceCount - 1 : 1)
                            : (decrease ? choiceCount - 2 : choiceCount);
        for (int index = 0; index < choiceCount; ++index) {
            if (*current < choices[index]) {
                goTo = decrease ? index - 1 : index;
                break;
            }
            if (*current == choices[index]) {
                goTo = decrease ? index - 1 : index + 1;
                break;
            }
        }
        if (goTo == -1) {
            goTo = circular ? choiceCount - 1 : 0;
        } else if (goTo == choiceCount) {
            goTo = circular ? 0 : choiceCount - 1;
        }
        *current = choices[goTo];
        UpdateEmotion(emotion);
        return;
    }

    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const QRect bullRect(0, height() - m_bullSide, width(), m_bullSide);
    QPoint current = GetPointFromEmotion(m_emotion);
    const QPoint old = current;
    const int step = current == m_bullsEye ? (m_bullRadius + 4) / 5 : 1;
    if (key == Qt::Key_Up) {
        current.setY(shift ? bullRect.top() : current.y() - step);
    } else if (key == Qt::Key_Left) {
        current.setX(shift ? bullRect.left() : current.x() - step);
    } else if (key == Qt::Key_Down) {
        current.setY(shift ? bullRect.bottom() + 1 : current.y() + step);
    } else if (key == Qt::Key_Right) {
        current.setX(shift ? bullRect.right() + 1 : current.x() + step);
    } else if (key == Qt::Key_Home) {
        current = m_bullsEye;
    }
    if (old != current) {
        CEmotion emotion = GetEmotionFromPoint(current);
        UpdateEmotion(emotion);
    }
}

void CBodyCam::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_forcedDelete) {
        event->ignore();
        return;
    }
    QMenu menu(this);
    QAction* freeze = menu.addAction(
        originalMenuItemText(QStringLiteral("ID_BODYCONTEXT_FREEZE")));
    freeze->setCheckable(true);
    freeze->setChecked(m_avatar && m_avatar->m_freeze == AF_FROZEN);
    QAction* sendExpression = menu.addAction(
        originalMenuItemText(QStringLiteral("ID_BODYCONTEXT_SENDEXPRESSION")));
    const QPoint popupPoint = event->reason() == QContextMenuEvent::Keyboard
        ? mapToGlobal(rect().center()) : event->globalPos();
    QAction* selected = menu.exec(popupPoint);
    if (selected == freeze) {
        OnBodycontextFreeze();
    } else if (selected == sendExpression) {
        OnBodycontextSendexpression();
    }
    event->accept();
}

void CBodyCam::OnBodycontextFreeze()
{
    if (!m_avatar) return;
    m_avatar->m_freeze = m_avatar->m_freeze == AF_FROZEN
        ? AF_UNFROZEN : AF_FROZEN;
}

void CBodyCam::OnBodycontextSendexpression()
{
    if (!bLegalToSend()) return;
    bChatSendText(QStringLiteral("<Chr>"), BM_SAY);
}

void CBodyCam::resizeEvent(QResizeEvent* event)
{
    CacheBullSide(event->size().width());
    m_bodyRect = {0, 0, event->size().width(), event->size().height()};
    QWidget::resizeEvent(event);
}

void CBodyDouble::FlipBodyBox(RECT& fullRect, RECT& headRect, RECT& torsoRect)
{
    const int headWidth = headRect.right - headRect.left;
    const int torsoWidth = torsoRect.right - torsoRect.left;
    headRect.left = fullRect.right - (headRect.left - fullRect.left);
    headRect.right = headRect.left - headWidth;
    torsoRect.left = fullRect.right - (torsoRect.left - fullRect.left);
    torsoRect.right = torsoRect.left - torsoWidth;
}

void CBodyDouble::Draw(QtPaintDC* dc, POINT*, RECT*)
{
    if (!dc || !dc->surface()) return;
    const RECT logical = SRECTToRECT(m_bbox);
    const QRect mapped = dc->mapRect(logical);
    RECT target{mapped.left(), mapped.top(), mapped.right() + 1, mapped.bottom() + 1};
    DrawBody(dc->surface(), target, TRUE);
}

RECT CBodyDouble::DrawBody(QImage* target, RECT& clientRect, BOOL drawNimbus)
{
    RECT fullRect = emptyRect();
    auto* avatar = static_cast<CAvatarComplex*>(GetAvatar(static_cast<USHORT>(m_avatarID)));
    if (!avatar || !m_faceRec || !m_torsoRec) {
        return fullRect;
    }
    CPose* headPose = nullptr;
    CPose* torsoPose = nullptr;
    if (!avatar->GetPosesFromIDs(m_faceRec->poseID, m_torsoRec->poseID,
                                 &headPose, &torsoPose)
        || !headPose || !torsoPose || !headPose->GetDrawing()
        || !torsoPose->GetDrawing()) {
        return fullRect;
    }

    RECT headRect{};
    RECT torsoRect{};
    GetBodyBox(headPose, torsoPose, clientRect, fullRect, headRect, torsoRect);
    if (m_flip) {
        FlipBodyBox(fullRect, headRect, torsoRect);
    }

    const auto drawDib = [target](CAvatarDIB* dib, const RECT& destination, DWORD operation) {
        if (dib) {
            dib->Draw(target, destination.left, destination.top,
                      destination.right - destination.left,
                      destination.bottom - destination.top, operation);
        }
    };
    if (drawNimbus) {
        drawDib(torsoPose->GetAura(), torsoRect, MERGEPAINT);
        drawDib(headPose->GetAura(), headRect, MERGEPAINT);
    }
    if (avatar->m_flags & TORSOFIRST) {
        if (avatar->m_flags & TORSOMASK) {
            drawDib(torsoPose->GetMask(), torsoRect, MERGEPAINT);
        }
        drawDib(torsoPose->GetDrawing(), torsoRect, SRCAND);
    }
    if (avatar->m_flags & HEADMASK) {
        drawDib(headPose->GetMask(), headRect, MERGEPAINT);
    }
    drawDib(headPose->GetDrawing(), headRect, SRCAND);
    if (!(avatar->m_flags & TORSOFIRST)) {
        if (avatar->m_flags & TORSOMASK) {
            drawDib(torsoPose->GetMask(), torsoRect, MERGEPAINT);
        }
        drawDib(torsoPose->GetDrawing(), torsoRect, SRCAND);
    }
    return fullRect;
}

void CBodyDouble::GetBodyBox(CPose* headPose, CPose* torsoPose, RECT& clientRect,
                             RECT& fullRect, RECT& headRect, RECT& torsoRect)
{
    const int xOffset = m_torsoRec->xCX + m_faceRec->delta_xCX - m_faceRec->xCX;
    const int yOffset = m_torsoRec->yCX + m_faceRec->delta_yCX - m_faceRec->yCX;
    RECT bitRect;
    bitRect.left = qMin(0, xOffset);
    bitRect.right = qMax(torsoPose->GetDrawing()->GetWidth(),
                         xOffset + headPose->GetDrawing()->GetWidth());
    bitRect.top = qMin(0, yOffset);
    bitRect.bottom = qMax(torsoPose->GetDrawing()->GetHeight(),
                          yOffset + headPose->GetDrawing()->GetHeight());

    const int bitWidth = bitRect.right - bitRect.left;
    const int bitHeight = bitRect.bottom - bitRect.top;
    const int heightSign = clientRect.bottom > clientRect.top ? 1 : -1;
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = heightSign * (clientRect.bottom - clientRect.top);
    if (bitWidth <= 0 || bitHeight <= 0 || clientWidth <= 0 || clientHeight <= 0) {
        fullRect = headRect = torsoRect = emptyRect();
        return;
    }
    const double scale = qMin(static_cast<double>(clientWidth) / bitWidth,
                              static_cast<double>(clientHeight) / bitHeight);
    const int fullHeight = ROUND(scale * bitHeight);
    const int fullWidth = ROUND(scale * bitWidth);
    fullRect.left = clientRect.left + (clientWidth - fullWidth) / 2;
    fullRect.top = clientRect.top + (clientHeight - fullHeight);
    fullRect.bottom = fullRect.top + heightSign * fullHeight;
    fullRect.right = fullRect.left + fullWidth;

    headRect.left = ROUND((xOffset - bitRect.left) * scale) + fullRect.left;
    headRect.right = headRect.left + ROUND(headPose->GetDrawing()->GetWidth() * scale) + 1;
    headRect.top = ROUND((yOffset - bitRect.top) * scale) + fullRect.top;
    headRect.bottom = headRect.top + heightSign
        * (ROUND(headPose->GetDrawing()->GetHeight() * scale) + 1);
    torsoRect.left = ROUND((0 - bitRect.left) * scale) + fullRect.left;
    torsoRect.right = torsoRect.left + ROUND(torsoPose->GetDrawing()->GetWidth() * scale) + 1;
    torsoRect.top = ROUND((0 - bitRect.top) * scale) * heightSign + fullRect.top;
    torsoRect.bottom = torsoRect.top + heightSign
        * (ROUND(torsoPose->GetDrawing()->GetHeight() * scale) + 1);
}

void CBodySingle::FlipBodyBox(RECT& fullRect)
{
    std::swap(fullRect.left, fullRect.right);
}

void CBodySingle::Draw(QtPaintDC* dc, POINT*, RECT*)
{
    if (!dc || !dc->surface()) return;
    const RECT logical = SRECTToRECT(m_bbox);
    const QRect mapped = dc->mapRect(logical);
    RECT target{mapped.left(), mapped.top(), mapped.right() + 1, mapped.bottom() + 1};
    DrawBody(dc->surface(), target, TRUE);
}

RECT CBodySingle::DrawBody(QImage* target, RECT& clientRect, BOOL drawNimbus)
{
    RECT fullRect = emptyRect();
    auto* avatar = static_cast<CAvatarSimple*>(GetAvatar(static_cast<USHORT>(m_avatarID)));
    if (!avatar) {
        return fullRect;
    }
    CPose* pose = nullptr;
    if (!avatar->GetPoseFromID(static_cast<USHORT>(GetPoseID()), &pose)
        || !pose || !pose->GetDrawing()) {
        return fullRect;
    }
    GetBodyBox(pose, clientRect, fullRect);
    if (m_flip) {
        FlipBodyBox(fullRect);
    }
    if (drawNimbus && pose->GetAura()) {
        pose->GetAura()->Draw(target, fullRect.left, fullRect.top,
                              fullRect.right - fullRect.left,
                              fullRect.bottom - fullRect.top, MERGEPAINT);
    }
    pose->GetDrawing()->Draw(target, fullRect.left, fullRect.top,
                             fullRect.right - fullRect.left,
                             fullRect.bottom - fullRect.top, SRCAND);
    return fullRect;
}

void CBodySingle::GetBodyBox(CPose* pose, RECT& clientRect, RECT& fullRect)
{
    const int bitWidth = pose->GetDrawing()->GetWidth();
    const int bitHeight = pose->GetDrawing()->GetHeight();
    const int heightSign = clientRect.bottom > clientRect.top ? 1 : -1;
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = heightSign * (clientRect.bottom - clientRect.top);
    if (bitWidth <= 0 || bitHeight <= 0 || clientWidth <= 0 || clientHeight <= 0) {
        fullRect = emptyRect();
        return;
    }
    const double widthScale = static_cast<double>(clientWidth) / bitWidth;
    const double heightScale = static_cast<double>(clientHeight) / bitHeight;
    int fullHeight;
    int fullWidth;
    if (widthScale <= heightScale) {
        fullWidth = clientWidth;
        fullHeight = static_cast<int>(widthScale * bitHeight);
    } else {
        fullHeight = clientHeight;
        fullWidth = static_cast<int>(heightScale * bitWidth);
    }
    fullRect.left = clientRect.left + (clientWidth - fullWidth) / 2;
    fullRect.top = clientRect.top + (clientHeight - fullHeight);
    fullRect.bottom = fullRect.top + heightSign * fullHeight;
    fullRect.right = fullRect.left + fullWidth;
}

CBodyCam* GetBodyCam()
{
    CChatDoc* document = GetChatDoc();
    return document ? document->m_bodyCam : nullptr;
}

CBodyCam* GetCharSelBodyCam()
{
    return characterSelectionBodyCam;
}

void SetCharSelBodyCam(CBodyCam* bodyCam)
{
    characterSelectionBodyCam = bodyCam;
}

void UpdateEmotion(CEmotion& emotion)
{
    if (CBodyCam* bodyCam = GetBodyCam()) {
        bodyCam->UpdateEmotion(emotion);
    }
}

void RefreshBodyCam(CAvatarX* avatar)
{
    CBodyCam* bodyCam = GetBodyCam();
    if (!bodyCam) {
        return;
    }
    if (avatar) {
        bodyCam->m_avatar = avatar;
    }
    if (!theApp.m_bNoRefresh) {
        bodyCam->RefreshBody();
    }
}

void DetachBodyCamAvatar()
{
    if (CBodyCam* bodyCam = GetBodyCam()) {
        bodyCam->m_avatar = nullptr;
        bodyCam->update();
    }
}

BOOL RefreshBodyPreview(CAvatarX* avatar)
{
    CBodyCam* bodyCam = GetCharSelBodyCam();
    if (!bodyCam || bodyCam->m_avatar != avatar) return FALSE;
    bodyCam->RefreshBody();
    return TRUE;
}
