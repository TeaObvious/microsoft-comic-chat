// Ported from v2.5-beta-1-modern/panel.cpp. Qt containers and QtPaintDC replace
// MFC/GDI only; panel construction, avatar ordering, balloon placement and
// starring enumeration retain the original control flow and constants.

#include "panel.h"

#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "intl.h"
#include "originalassets.h"
#include "pageview.h"
#include "paintdc.h"
#include "protsupp.h"
#include "userinfo.h"
#include "vector2d.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int INFOMARGIN = 10;
constexpr double MAXINFOTEXTHEIGHT = .5;
constexpr int BR_SPEAKER = 0;
constexpr int BR_GOODIDEA = 2;
constexpr int MAXBDYPERFRAME = 20;
constexpr int ONELINETHRESHOLD = 500;
constexpr int MINHOOKHEIGHT = 100;
constexpr int ICONSIZE = 500;
constexpr int ICONSPACE = 100;
constexpr int BELOWSTARRING = 300;
constexpr int ROWHEIGHT = 500;
constexpr BOOL bZoomIn = TRUE;
constexpr int BALLOON_DOCK_DELTA = -20 + 40 + 70;

class CBodyRecord {
public:
    CBody* m_body = nullptr;
    QList<CUserInfo*> m_lookAts;
    UCHAR m_priority = 0;
};

void ForceFitBalloon(CBalloon* balloon, RECT& freeRect, char** rest,
                     CDWordArray** restFormatting, char** urlStartInRest)
{
    balloon->SetBBox(freeRect.left, freeRect.bottom, freeRect.right, freeRect.top);
    *rest = balloon->SplitHeight(freeRect.top - freeRect.bottom,
                                 restFormatting, urlStartInRest);
    if (balloon->m_bbox.Top > -250) balloon->DockAtTop(freeRect.top);
}

void DockRect(RECT& rect)
{
    rect.top += BALLOON_DOCK_DELTA;
    rect.bottom += BALLOON_DOCK_DELTA;
}

BOOL GetInterveningBBox(CBalloon* balloons[], int index, RECT& freeRect,
                        RECT& intervening)
{
    RECT cloudBox{};
    const int toPointX = balloons[index]->m_speaker->m_arrowX;
    int mostLeft = freeRect.left;
    int mostRight = freeRect.right;
    for (int i = 0; i < index; ++i) {
        int leftAllowance = 0;
        int rightAllowance = 0;
        balloons[i]->QueryRouteRgn(toPointX, leftAllowance, rightAllowance);
        mostLeft = std::max(leftAllowance, mostLeft);
        mostRight = std::min(rightAllowance, mostRight);
    }
    if (mostLeft > intervening.left || mostRight < intervening.right) {
        const int potentialClearance = mostRight - mostLeft;
        if (potentialClearance >= intervening.right - intervening.left) {
            const int delta = mostLeft > intervening.left
                ? mostLeft - intervening.left : mostRight - intervening.right;
            intervening.left += delta;
            intervening.right += delta;
        } else {
            intervening.left = mostLeft;
            intervening.right = mostRight;
        }
    }

    intervening.top = freeRect.top;
    for (int i = 0; i < index; ++i) {
        balloons[i]->GetCloudBBox(&cloudBox);
        if (cloudBox.right < intervening.left) {
            intervening.top = std::min<LONG>(intervening.top, cloudBox.top);
        } else {
            DockRect(cloudBox);
            intervening.top = std::min<LONG>(intervening.top, cloudBox.bottom);
        }
    }
    return TRUE;
}

int LowestPreviousBottom(CBalloon* balloons[], int index, int lowY)
{
    for (int i = 0; i < index; ++i)
        lowY = std::min(lowY, static_cast<int>(balloons[i]->m_bbox.Bottom));
    return lowY;
}

void AdjustRouteRgns(CBalloon* balloons[], int index)
{
    const int left = balloons[index]->m_routeRgn.Left;
    const int right = balloons[index]->m_routeRgn.Right;
    const int toX = balloons[index]->m_speaker->m_arrowX;
    for (int i = 0; i < index; ++i) balloons[i]->SetRouteRgn(toX, left, right);
}

int ComputeDisplacementPenalty(QList<CBodyRecord*>& bodyArray, int entries)
{
    int penalty = 0;
    for (int i = 0; i < entries; ++i) {
        CAvatarX* avatar = GetAvatar(
            static_cast<USHORT>(bodyArray[i]->m_body->m_avatarID));
        if (!avatar) continue;
        if (i > 0 && avatar->m_lastRight
                != bodyArray[i - 1]->m_body->m_avatarID) ++penalty;
        if (i < entries - 1 && avatar->m_lastLeft
                != bodyArray[i + 1]->m_body->m_avatarID) ++penalty;
    }
    return penalty;
}

int EvalPair(CBodyRecord& first, CBodyRecord& second, int deltaPlacement)
{
    int rating = 0;
    int desiredDirection;
    if (deltaPlacement > 0) desiredDirection = FALSE;
    else {
        desiredDirection = TRUE;
        deltaPlacement = -deltaPlacement;
    }

    CAvatarX* firstAvatar = GetAvatar(
        static_cast<USHORT>(first.m_body->m_avatarID));
    auto* firstUser = firstAvatar
        ? static_cast<CUserInfo*>(firstAvatar->m_userInfo) : nullptr;
    Q_ASSERT(firstUser);
    if (!firstUser) return rating;
    const int talkToCount = firstUser->m_udi.m_talkTos.size();
    if (talkToCount == 0) {
        if (first.m_body->m_flip != desiredDirection) rating += 4;
        if (second.m_body->m_flip == desiredDirection) rating += 2;
    } else {
        CAvatarX* secondAvatar = GetAvatar(
            static_cast<USHORT>(second.m_body->m_avatarID));
        void* secondUser = secondAvatar ? secondAvatar->m_userInfo : nullptr;
        for (CUserInfo* talkTo : firstUser->m_udi.m_talkTos) {
            if (talkTo == secondUser) {
                if (first.m_body->m_flip == desiredDirection)
                    rating += 4 * (deltaPlacement - 1);
                else rating += 40;
                if (second.m_body->m_flip == desiredDirection) rating += 4;
            }
        }
    }
    return rating;
}

void AddTalkTos(CBodyRecord bodies[], int& recordCount)
{
    const int initialCount = recordCount;
    for (int i = 0; i < initialCount; ++i) {
        CAvatarX* avatar = GetAvatar(
            static_cast<USHORT>(bodies[i].m_body->m_avatarID));
        auto* user = avatar ? static_cast<CUserInfo*>(avatar->m_userInfo) : nullptr;
        if (!user) continue;
        for (CUserInfo* talkTo : user->m_udi.m_talkTos) {
            if (recordCount >= 5) return;
            bool duplicate = false;
            for (int k = 0; k < recordCount; ++k) {
                CAvatarX* existingAvatar = GetAvatar(
                    static_cast<USHORT>(bodies[k].m_body->m_avatarID));
                if (existingAvatar && existingAvatar->m_userInfo == talkTo) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && talkTo) {
                CAvatarX* theirAvatar = GetAvatar(talkTo->GetAvatarID());
                if (theirAvatar) {
                    CEmotion neutral(0.0, 0.0);
                    bodies[recordCount].m_body = theirAvatar->GetBodyFromEmotion(neutral);
                    bodies[recordCount].m_body->m_requested = FALSE;
                    bodies[recordCount++].m_priority = BR_GOODIDEA;
                } else {
                    Q_ASSERT(false);
                }
            }
        }
    }
}

int EvalPlacement(QList<CBodyRecord*>& bodyArray, int placedCount,
                  CBodyRecord& body, int index, int& direction)
{
    bodyArray.insert(index, &body);
    const int penalty = ComputeDisplacementPenalty(bodyArray, placedCount + 1);
    int ratingRight = penalty;
    int ratingLeft = penalty;

    body.m_body->m_flip = FALSE;
    for (int i = 0; i <= placedCount; ++i) {
        for (int j = i + 1; j <= placedCount; ++j) {
            ratingRight += EvalPair(*bodyArray[i], *bodyArray[j], j - i)
                + EvalPair(*bodyArray[j], *bodyArray[i], i - j);
        }
    }

    body.m_body->m_flip = TRUE;
    for (int i = 0; i <= placedCount; ++i) {
        for (int j = i + 1; j <= placedCount; ++j) {
            ratingLeft += EvalPair(*bodyArray[i], *bodyArray[j], j - i)
                + EvalPair(*bodyArray[j], *bodyArray[i], i - j);
        }
    }
    bodyArray.removeAt(index);

    if (ratingRight < ratingLeft) {
        direction = FALSE;
        return ratingRight;
    }
    if (ratingRight > ratingLeft) {
        direction = TRUE;
        return ratingLeft;
    }
    CAvatarX* avatar = GetAvatar(static_cast<USHORT>(body.m_body->m_avatarID));
    direction = avatar ? avatar->m_lastDir : FALSE;
    return ratingRight;
}

void DoGreedyOrdering(CBodyRecord bodies[], int recordCount,
                      QList<CBodyRecord*>& bodyArray)
{
    int placedCount = 0;
    for (int i = 0; i < recordCount; ++i) {
        int bestRating = 1000;
        int bestPosition = 0;
        int bestDirection = FALSE;
        for (int j = 0; j <= placedCount; ++j) {
            int direction = FALSE;
            const int rating = EvalPlacement(bodyArray, placedCount,
                                             bodies[i], j, direction);
            if (rating < bestRating) {
                bestRating = rating;
                bestPosition = j;
                bestDirection = direction;
            }
        }
        bodies[i].m_body->m_flip = static_cast<UCHAR>(bestDirection);
        bodyArray.insert(bestPosition, bodies + i);
        ++placedCount;
    }
}

BOOL OrderAvatars(CBodyRecord bodies[], int& recordCount,
                  QList<CBodyRecord*>& placed)
{
    placed.clear();
    if (recordCount < 5) AddTalkTos(bodies, recordCount);
    DoGreedyOrdering(bodies, recordCount, placed);
    return TRUE;
}

void UpdateHysteresis(QList<CBodyRecord*>& placed, int placedCount)
{
    for (int i = 0; i < placedCount; ++i) {
        CBodyRecord* record = placed[i];
        CAvatarX* avatar = GetAvatar(
            static_cast<USHORT>(record->m_body->m_avatarID));
        if (!avatar) continue;
        avatar->m_lastDir = record->m_body->m_flip;
        if (i > 0) avatar->m_lastRight = static_cast<USHORT>(
            placed[i - 1]->m_body->m_avatarID);
        if (i < placedCount - 1) avatar->m_lastLeft = static_cast<USHORT>(
            placed[i + 1]->m_body->m_avatarID);
    }
}

BOOL Establishing()
{
    CChatDoc* document = GetChatDoc();
    if (!document || document->m_pages.isEmpty()) return TRUE;
    CPage* firstPage = document->m_pages.first();
    const int count = firstPage ? firstPage->m_panels.size() : 0;
    return count <= 1 || (!g_bNewedPanel && count <= 2);
}

} // namespace

int CUnitPanelPage::m_panelsPerRow = 2;
int CUnitPanelPage::m_printPanelsPerRow = 0;
int CUnitPanelPage::m_panelsPerColumn = -1;
int CUnitPanelPage::m_unitWidth = MINUNITPANELWIDTH - 1;
int CUnitPanelPage::m_unitHeight = MINUNITPANELHEIGHT - 1;
int CUnitPanelPage::m_hInterstice = 144;
int CUnitPanelPage::m_vInterstice = 144;
int CUnitPanel::m_borderWidth = 60;

BOOL CPanelElement::SetBBox(int left, int bottom, int right, int top)
{
    m_bbox.Top = static_cast<SHORT>(top);
    m_bbox.Left = static_cast<SHORT>(left);
    m_bbox.Bottom = static_cast<SHORT>(bottom);
    m_bbox.Right = static_cast<SHORT>(right);
    return TRUE;
}

QString GetRandomTitle()
{
    static int titleCount = -1;
    if (titleCount == -1) {
        titleCount = 0;
        while (!originalResourceString(
            QStringLiteral("IDS_TITLE%1").arg(titleCount + 1)).isEmpty()) ++titleCount;
    }
    if (titleCount < 1) return originalResourceString(QStringLiteral("IDS_NOTITLE"));
    int chosenTitle = static_cast<int>(randfloat() * titleCount);
    chosenTitle = std::min(chosenTitle, titleCount - 1);
    return originalResourceString(QStringLiteral("IDS_TITLE%1").arg(chosenTitle + 1));
}

void AddStarsAux(QList<CAvatarX*>& stars, int maxStars)
{
    if (!g_mapNickToPtr) return;
    for (CUserInfo* user : *g_mapNickToPtr) {
        if (!user) continue;
        CAvatarX* newAvatar = GetAvatar(user->GetAvatarID());
        if (!newAvatar || !newAvatar->m_icon) continue;
        const BOOL usDeparted = user->IsDeparted();
        if (user == g_puiSelf) {
            stars.insert(0, newAvatar);
            continue;
        }

        const int upper = stars.size() - 1;
        BOOL inserted = FALSE;
        for (int i = 1; i <= upper; ++i) {
            CAvatarX* avatar = stars[i];
            auto* theirUser = avatar
                ? static_cast<CUserInfo*>(avatar->m_userInfo) : nullptr;
            const BOOL themDeparted = theirUser ? theirUser->IsDeparted() : FALSE;
            if ((!usDeparted && themDeparted)
                || ((usDeparted == themDeparted)
                    && newAvatar->m_nSends > avatar->m_nSends)) {
                stars.insert(i, newAvatar);
                inserted = TRUE;
                break;
            }
        }
        if (upper <= maxStars - 1 && !inserted) stars.insert(upper + 1, newAvatar);
    }
}

CPanel::CPanel()
{
    m_seed = static_cast<unsigned int>(std::rand());
    m_hasBorder = TRUE;
    m_backDrop.m_backID = GetChatDoc()
        ? static_cast<unsigned short>(GetChatDoc()->GetBackDropID()) : 0;
}

CPanel::CPanel(const CPanel& source)
    : m_seed(source.m_seed)
    , m_hasBorder(source.m_hasBorder)
{
    m_backDrop.m_backID = source.m_backDrop.m_backID;
    for (CBody* body : source.m_bodies) m_bodies.append(body->Clone());
    for (CPanelElement* element : source.m_elements) {
        auto* oldBalloon = static_cast<CBalloon*>(element);
        CBalloon* newBalloon = oldBalloon->Clone();
        const int bodyIndex = source.m_bodies.indexOf(oldBalloon->m_speaker);
        if (bodyIndex >= 0 && bodyIndex < m_bodies.size())
            newBalloon->m_speaker = m_bodies[bodyIndex];
        m_elements.append(newBalloon);
    }
}

CPanel::~CPanel()
{
    qDeleteAll(m_elements);
    qDeleteAll(m_bodies);
}

CBody* CPanel::FetchSpeaker(UINT id)
{
    for (CBody* body : m_bodies) if (body->m_avatarID == id) return body;
    CAvatarX* avatar = GetAvatar(static_cast<USHORT>(id));
    if (!avatar || !avatar->m_body) return nullptr;
    CBody* body = avatar->m_body->Clone();
    avatar->RecordBody(body);
    m_bodies.append(body);
    return body;
}

BOOL CPanel::ReplaceBody(UINT id)
{
    for (int i = 0; i < m_bodies.size(); ++i) {
        CBody* oldBody = m_bodies[i];
        if (oldBody->m_avatarID != id) continue;
        CAvatarX* avatar = GetAvatar(static_cast<USHORT>(id));
        if (!avatar || !avatar->m_body) return FALSE;
        CBody* newBody = avatar->m_body->Clone();
        newBody->m_requested = TRUE;
        m_bodies[i] = newBody;
        avatar->RecordBody(newBody);
        for (CPanelElement* element : m_elements) {
            auto* balloon = static_cast<CBalloon*>(element);
            if (balloon->m_speaker->m_avatarID == id) balloon->m_speaker = newBody;
        }
        delete oldBody;
        return TRUE;
    }
    return FALSE;
}

BOOL CPanel::AvatarInPanel(UINT avatarId)
{
    for (CBody* body : m_bodies) if (body->m_avatarID == avatarId) return TRUE;
    return FALSE;
}

void CUnitPanel::Draw(QtPaintDC* dc, POINT*, RECT* damageRect)
{
    RECT panelRect{0, 0, CUnitPanelPage::m_unitWidth,
                   -CUnitPanelPage::m_unitHeight};
    RECT fullDamage = panelRect;
    if (!damageRect) damageRect = &fullDamage;
    m_backDrop.Draw(dc, &panelRect, damageRect);

    RECT itemBox{};
    for (CBody* body : m_bodies) {
        body->GetBBox(&itemBox);
        if (bbox_overlap(damageRect, &itemBox)) body->Draw(dc, nullptr, damageRect);
    }
    for (auto i = m_elements.crbegin(); i != m_elements.crend(); ++i) {
        (*i)->GetBBox(&itemBox);
        if (bbox_overlap(damageRect, &itemBox)) (*i)->Draw(dc, nullptr, damageRect);
    }
    if (m_hasBorder) DrawBorder(dc, &panelRect);
}

void CUnitPanel::DrawBorder(QtPaintDC* dc, RECT* rect)
{
    if (!dc || !dc->surface() || !rect) return;
    QPainter painter(dc->surface());
    dc->configure(painter);
    QPen pen(Qt::black);
    pen.setWidth(2 * m_borderWidth);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(rect->left, rect->bottom);
    path.lineTo(rect->left, rect->top);
    path.lineTo(rect->right, rect->top);
    path.lineTo(rect->right, rect->bottom);
    path.closeSubpath();
    painter.drawPath(path);
}

void CUnitPanel::LayoutAvatars()
{
    CBodyRecord bodyRecords[MAXBDYPERFRAME];
    QList<CBodyRecord*> placed;
    SHORT width[MAXBDYPERFRAME]{}, height[MAXBDYPERFRAME]{};
    SHORT normalHeight[MAXBDYPERFRAME]{}, top[MAXBDYPERFRAME]{};
    SHORT headHeight[MAXBDYPERFRAME]{};
    double arrowX[MAXBDYPERFRAME]{};
    SHORT bitArrowX = 0;
    int bodyCount = 0;
    int bodyWidth = 0;
    int maxNormal = 0;
    SHORT maxHeadHeight = 0;
    const int maxBodyHeight = static_cast<int>(CUnitPanelPage::m_unitHeight / 1.9);
    const int minMargin = 0;

    const QList<CBody*> oldBodies = m_bodies;
    for (CBody* body : oldBodies) {
        if (IsSpeaker(body)) {
            bodyRecords[bodyCount].m_body = body;
            bodyRecords[bodyCount++].m_priority = BR_SPEAKER;
        } else delete body;
    }
    m_bodies.clear();
    if (bodyCount == 0) return;

    OrderAvatars(bodyRecords, bodyCount, placed);
    for (int i = 0; i < bodyCount; ++i) {
        CBody* body = placed[i]->m_body;
        body->GetDimInfo(width[i], height[i], normalHeight[i],
                         headHeight[i], bitArrowX);
        arrowX[i] = static_cast<double>(bitArrowX) / width[i];
        maxNormal = std::max(maxNormal, static_cast<int>(normalHeight[i]));
    }

    for (int i = 0; i < bodyCount; ++i) {
        const int newHeight = ROUND(maxBodyHeight
            * (static_cast<float>(normalHeight[i]) / maxNormal));
        const float scaleRatio = static_cast<float>(newHeight) / height[i];
        height[i] = static_cast<SHORT>(newHeight);
        width[i] = static_cast<SHORT>(ROUND(scaleRatio * width[i]));
        top[i] = static_cast<SHORT>(-CUnitPanelPage::m_unitHeight + height[i]);
        headHeight[i] = static_cast<SHORT>(ROUND(scaleRatio * headHeight[i]));
        bodyWidth += width[i];
    }

    const int sumWidth = bodyWidth + (bodyCount + 1) * minMargin;
    double zoomFactor = 1.0;
    if (sumWidth > CUnitPanelPage::m_unitWidth) {
        const float reduction = static_cast<float>(CUnitPanelPage::m_unitWidth)
            / sumWidth;
        bodyWidth = 0;
        for (int i = 0; i < bodyCount; ++i) {
            height[i] = static_cast<SHORT>(ROUND(height[i] * reduction));
            width[i] = static_cast<SHORT>(ROUND(width[i] * reduction));
            top[i] = static_cast<SHORT>(-CUnitPanelPage::m_unitHeight + height[i]);
            bodyWidth += width[i];
        }
        AdjustArtToCoord(0, 1.0);
    } else if (bZoomIn && !Establishing()) {
        zoomFactor = static_cast<double>(CUnitPanelPage::m_unitWidth) / sumWidth;
        for (int i = 0; i < bodyCount; ++i)
            maxHeadHeight = std::max(maxHeadHeight, headHeight[i]);
        const double headFactor = static_cast<double>(maxBodyHeight)
            / (maxHeadHeight * 1.2);
        zoomFactor = std::min(zoomFactor, headFactor);
        if (zoomFactor < 1.1) zoomFactor = 1.0;
        bodyWidth = 0;
        for (int i = 0; i < bodyCount; ++i) {
            height[i] = static_cast<SHORT>(ROUND(height[i] * zoomFactor));
            width[i] = static_cast<SHORT>(ROUND(width[i] * zoomFactor));
            bodyWidth += width[i];
        }
    }
    AdjustArtToCoord(-CUnitPanelPage::m_unitHeight + maxBodyHeight, zoomFactor);

    const int margin = (CUnitPanelPage::m_unitWidth - bodyWidth) / (bodyCount + 1);
    int xOffset = margin;
    for (int i = 0; i < bodyCount; ++i) {
        CBody* body = placed[i]->m_body;
        m_bodies.append(body);
        body->SetBBox(xOffset, top[i] - height[i],
                      xOffset + width[i], top[i]);
        body->m_arrowX = static_cast<SHORT>(body->m_bbox.Left
            + ROUND(arrowX[i] * (body->m_bbox.Right - body->m_bbox.Left)));
        xOffset += width[i] + margin;
    }
    UpdateHysteresis(placed, bodyCount);
}

BOOL CUnitPanel::IsSpeaker(CBody* body)
{
    if (body->m_requested) return TRUE;
    const UINT avatarId = body->m_avatarID;
    for (CPanelElement* element : m_elements) {
        if (element->GetType() & PE_BALLOON) {
            auto* balloon = static_cast<CBalloon*>(element);
            if (balloon->m_speaker->m_avatarID == avatarId) return TRUE;
        }
    }
    return FALSE;
}

RECT CUnitPanel::GetBalloonRect()
{
    RECT rect{0, 0, CUnitPanelPage::m_unitWidth,
              -CUnitPanelPage::m_unitHeight / 2};
    if (m_hasBorder) {
        rect.left += m_borderWidth;
        rect.right -= m_borderWidth;
        rect.top -= m_borderWidth;
    }
    return rect;
}

BOOL CUnitPanel::LayoutBalloons(char** rest, CDWordArray** restFormatting,
                                char** urlStartInRest)
{
    CBalloon* balloons[10]{};
    int count = 0;
    *rest = nullptr;
    *urlStartInRest = nullptr;
    *restFormatting = nullptr;
    RECT freeRect = GetBalloonRect();
    std::srand(m_seed);
    for (CPanelElement* element : m_elements)
        balloons[count++] = static_cast<CBalloon*>(element);
    for (int i = 0; i < count; ++i) {
        if (!LayoutBalloon(balloons, count, i, freeRect)) {
            if (i == 0 && count == 1) {
                ForceFitBalloon(balloons[i], freeRect, rest,
                                restFormatting, urlStartInRest);
                return TRUE;
            }
            return FALSE;
        }
    }
    return TRUE;
}

void CUnitPanel::GetCloudEstimate(CBalloon* balloons[], int count, int index,
                                  RECT& freeRect, RECT& balloonRect)
{
    int length = 0;
    int lineHeight = 0;
    const int area = balloons[index]->AreaEstimate(&length, &lineHeight);
    const int maxWidth = freeRect.right - freeRect.left;
    int goalWidth;
    if (length <= ONELINETHRESHOLD) goalWidth = length;
    else {
        const int potentialHeight = LowestPreviousBottom(balloons, index, freeRect.top)
            - freeRect.bottom + MINHOOKHEIGHT;
        int minWidth = area / potentialHeight;
        minWidth = std::max(minWidth, balloons[index]->WidestWord());
        goalWidth = minWidth
            + static_cast<int>(randfloat() * (maxWidth - minWidth));
    }
    goalWidth = std::min(goalWidth + 200, maxWidth);
    goalWidth = std::min(goalWidth, length + 200);
    if (balloons[index]->GetType() & PE_BOX) balloonRect.left = freeRect.left;
    else {
        const int toPointX = balloons[index]->m_speaker->m_arrowX;
        const int leftLimit = toPointX - goalWidth;
        const int rightLimit = toPointX;
        int startX = leftLimit
            + static_cast<int>(randfloat() * (rightLimit - leftLimit));
        if (startX < freeRect.left) startX = freeRect.left;
        if (startX + goalWidth > freeRect.right) startX = freeRect.right - goalWidth;
        balloonRect.left = startX;
    }
    balloonRect.right = balloonRect.left + goalWidth;
}

BOOL CUnitPanel::LayoutBalloon(CBalloon* balloons[], int count, int index,
                               RECT& freeRect)
{
    RECT balloonRect{};
    GetCloudEstimate(balloons, count, index, freeRect, balloonRect);
    if (!GetInterveningBBox(balloons, index, freeRect, balloonRect)) return FALSE;
    CBalloon* balloon = balloons[index];
    if (!balloon->SetBBox(balloonRect.left, balloonRect.bottom,
                          balloonRect.right, balloonRect.top)) return FALSE;
    if (balloon->m_bbox.Top > -250) balloon->DockAtTop(freeRect.top);
    balloon->GetCloudBBox(&balloon->m_routeRgn);
    if (balloon->m_routeRgn.Bottom < freeRect.bottom + MINHOOKHEIGHT) return FALSE;
    AdjustRouteRgns(balloons, index);
    return TRUE;
}

void CUnitPanel::AdjustArtToCoord(int fixedY, double zoomFactor)
{
    if (m_backDrop.m_mode == BF_NOZOOM) zoomFactor = 1.0;
    const int logicalHeight = ROUND(CUnitPanelPage::m_unitHeight / zoomFactor);
    const int logicalWidth = ROUND(CUnitPanelPage::m_unitWidth / zoomFactor);
    const int newFixedY = ROUND(fixedY / zoomFactor);
    const int delta = fixedY - newFixedY;
    m_backDrop.SetBBox(0, -logicalHeight + delta, logicalWidth, delta);
}

void CUnitPanel::OnClickHotLink(UINT link, const char* linkText)
{
    Q_UNUSED(link);
    if (!linkText) return;
    const QString noCharacter = originalResourceString(
        QStringLiteral("IDS_NO_CHAR_HOTLINK"));
    if (IntlTextToQString(linkText).compare(noCharacter,
                                            Qt::CaseInsensitive) != 0) {
        return;
    }
    if (m_bodies.isEmpty()) return;
    CAvatarX* avatar = GetAvatar(
        static_cast<USHORT>(m_bodies.first()->m_avatarID));
    auto* pui = avatar ? static_cast<CUserInfo*>(avatar->m_userInfo) : nullptr;
    if (pui && !pui->IsAvatarReal() && g_bCanViewUnrated)
        theApp.StartDownloadingAvatar(pui, GetChatDoc(), TRUE);
}

CPage::~CPage()
{
    qDeleteAll(m_panels);
}

CPanel* CPage::RemoveLastPanel()
{
    return m_panels.isEmpty() ? nullptr : m_panels.takeLast();
}

QSize CUnitPanelPage::GetScrollPage()
{
    return QSize(m_unitWidth + m_vInterstice, m_unitHeight + m_hInterstice);
}

void CUnitPanelPage::RefreshLastPanel()
{
    const int panel = m_panels.size() - 1;
    if (panel >= 0) RefreshPanelN(panel);
}

void CUnitPanelPage::RefreshPanelN(int panel)
{
    if (m_doc && m_doc->m_view) m_doc->m_view->RefreshPanelN(panel);
}

BOOL CUnitPanelPage::AddPanel(CPanel* panel)
{
    m_panels.append(panel);
    RefreshLastPanel();
    return TRUE;
}

CBalloon* CUnitPanelPage::MakeBalloon(const char* message, USHORT modes,
                                      CDWordArray* formatting,
                                      const char* urlStart)
{
    switch (modes) {
    case BM_SAY:
        return new CBWoodringNormal(message, formatting, urlStart);
    case BM_WHISPER:
        return new CBWoodringWhisper(message, formatting, urlStart);
    case BM_THINK:
        return new CBWoodringThink(message, formatting, urlStart);
    case BM_ACTION:
    case BM_ACTION | BM_SAY:
    case BM_ACTION | BM_THINK:
    case BM_ACTION | BM_WHISPER:
        return new CBWoodringBox(message, formatting, urlStart,
                                modes & BM_WHISPER);
    default:
        Q_ASSERT(false);
        return new CBWoodringNormal(message, formatting, urlStart);
    }
}

BOOL CUnitPanelPage::AddLine(UINT id, const char* words, USHORT modes,
                             CDWordArray* formatting, const char* urlStart)
{
    if (modes == BM_ACTION) StartNewPanel();
    if (std::strcmp(words, "<Brk>") == 0) {
        StartNewPanel();
        return TRUE;
    }
    if (std::strcmp(words, "<Chr>") == 0) return AddReaction(id);

    extern BOOL g_bNewedPanel;
    g_bNewedPanel = FALSE;
    CPanel* oldPanel = m_panels.isEmpty() ? nullptr : m_panels.last();
    CPanel* newPanel;
    BOOL replaceLast;
    if (m_newPanel || (oldPanel && oldPanel->m_elements.size() >= 5)
        || m_panels.size() < 2 || (oldPanel && oldPanel->AvatarInPanel(id))) {
        newPanel = new CUnitPanel;
        m_newPanel = FALSE;
        replaceLast = FALSE;
        g_bNewedPanel = TRUE;
    } else {
        newPanel = oldPanel->Clone();
        replaceLast = TRUE;
    }

    CBalloon* newBalloon = MakeBalloon(words, modes, formatting, urlStart);
    if (!newBalloon) {
        delete newPanel;
        return FALSE;
    }
    newBalloon->m_speaker = newPanel->FetchSpeaker(id);
    if (!newBalloon->m_speaker) {
        delete newPanel;
        return FALSE;
    }
    newPanel->m_elements.append(newBalloon);
    newPanel->ReplaceBody(id);
    newPanel->LayoutAvatars();

    char* leftOver = nullptr;
    char* urlStartInLeftOver = nullptr;
    CDWordArray* leftOverFormatting = nullptr;
    if (!newPanel->LayoutBalloons(&leftOver, &leftOverFormatting,
                                  &urlStartInLeftOver)) {
        delete newPanel;
        StartNewPanel();
        AddLine(id, words, modes, formatting, urlStart);
    } else {
        if (replaceLast) {
            RemoveLastPanel();
            delete oldPanel;
        }
        AddPanel(newPanel);
        ResetAvatar(id);
        if (leftOver) {
            AddLine(id, leftOver, modes, leftOverFormatting, urlStartInLeftOver);
            std::free(leftOver);
            delete[] urlStartInLeftOver;
            FreeAndNullFormatting(&leftOverFormatting);
        }
    }
    return TRUE;
}

BOOL CUnitPanelPage::AddReaction(UINT id)
{
    CPanel* oldPanel = m_panels.isEmpty() ? nullptr : m_panels.last();
    CPanel* newPanel;
    BOOL replaceLast;
    if (m_newPanel || (oldPanel && oldPanel->m_bodies.size() >= 5)
        || m_panels.size() < 2) {
        newPanel = new CUnitPanel;
        m_newPanel = FALSE;
        replaceLast = FALSE;
    } else {
        newPanel = oldPanel->Clone();
        replaceLast = TRUE;
    }
    if (!newPanel->ReplaceBody(id)) newPanel->FetchSpeaker(id);
    newPanel->LayoutAvatars();

    char* leftOver = nullptr;
    char* urlStartInLeftOver = nullptr;
    CDWordArray* leftOverFormatting = nullptr;
    if (!newPanel->LayoutBalloons(&leftOver, &leftOverFormatting,
                                  &urlStartInLeftOver)) {
        delete newPanel;
        StartNewPanel();
        AddReaction(id);
    } else {
        if (replaceLast) {
            RemoveLastPanel();
            delete oldPanel;
        }
        AddPanel(newPanel);
        ResetAvatar(id);
    }
    Q_ASSERT(leftOverFormatting == nullptr);
    return TRUE;
}

void CUnitPanelPage::GetBBox(RECT* rect)
{
    rect->left = m_leftX;
    rect->top = m_topY;
    const int panelCount = m_panels.size();
    if (!panelCount) {
        rect->right = rect->left;
        rect->bottom = rect->top;
        return;
    }
    const int rows = (panelCount - 1) / m_panelsPerRow + 1;
    const int columns = std::min(panelCount, m_panelsPerRow);
    rect->right = m_leftX + columns * m_unitWidth
        + (columns - 1) * m_vInterstice;
    rect->bottom = m_topY - (rows * m_unitHeight
        + (rows - 1) * m_hInterstice);
}

void CUnitPanelPage::PageSizeInPanels(const SIZE& pageSize,
                                      int& panelsWide,
                                      int& panelsHigh) const
{
    // Keep the two original equations verbatim. In particular, the width
    // denominator uses m_hInterstice although horizontal placement uses
    // m_vInterstice.
    panelsHigh = (pageSize.cy + m_hInterstice)
        / (m_unitHeight + m_hInterstice);
    panelsWide = (pageSize.cx + m_vInterstice)
        / (m_unitWidth + m_hInterstice);
}

CUnitPanelPrintInfo CUnitPanelPage::PreparePrintDC(const SIZE& pageSize,
                                                   int pageNum)
{
    CUnitPanelPrintInfo result;
    PageSizeInPanels(pageSize, result.m_panelsWide, result.m_panelsHigh);
    if (result.m_panelsWide <= 0 || result.m_panelsHigh <= 0
        || pageNum <= 0) {
        m_printPanelsPerRow = 0;
        return result;
    }

    m_printPanelsPerRow = result.m_panelsWide;
    result.m_viewportOrigin.x = (pageSize.cx
        - (result.m_panelsWide * m_unitWidth
           + (result.m_panelsWide - 1) * m_vInterstice)) / 2;
    result.m_viewportOrigin.y = -(pageSize.cy
        - (result.m_panelsHigh * m_unitHeight
           + (result.m_panelsHigh - 1) * m_hInterstice)) / 2;
    result.m_startPanelRow = (pageNum - 1) * result.m_panelsHigh;
    result.m_firstPanel = result.m_startPanelRow * result.m_panelsWide;

    result.m_clipRect.left = result.m_viewportOrigin.x;
    result.m_clipRect.top = result.m_viewportOrigin.y;
    result.m_clipRect.right = result.m_clipRect.left
        + result.m_panelsWide * m_unitWidth
        + (result.m_panelsWide - 1) * m_vInterstice;
    result.m_clipRect.bottom = result.m_clipRect.top
        - result.m_panelsHigh * m_unitHeight
        - (result.m_panelsHigh - 1) * m_hInterstice;
    return result;
}

int CUnitPanelPage::GetPhysicalPageCount(const SIZE& pageSize) const
{
    int panelsWide = 0;
    int panelsHigh = 0;
    PageSizeInPanels(pageSize, panelsWide, panelsHigh);
    const int panelsPerPage = panelsWide * panelsHigh;
    if (panelsPerPage <= 0 || m_panels.isEmpty()) return 0;
    return static_cast<int>(std::ceil(
        static_cast<double>(m_panels.size()) / panelsPerPage));
}

void CUnitPanelPage::Draw(QPainter* painter,
                          const CUnitPanelPrintInfo& printInfo,
                          qreal pixelsPerTwipX, qreal pixelsPerTwipY,
                          const QPointF& pageOrigin)
{
    if (!painter || !painter->isActive()
        || printInfo.m_panelsWide <= 0
        || printInfo.m_panelsHigh <= 0
        || pixelsPerTwipX <= 0.0 || pixelsPerTwipY <= 0.0) {
        return;
    }

    const QRectF clip(
        pageOrigin.x() + printInfo.m_clipRect.left * pixelsPerTwipX,
        pageOrigin.y() - printInfo.m_clipRect.top * pixelsPerTwipY,
        (printInfo.m_clipRect.right - printInfo.m_clipRect.left)
            * pixelsPerTwipX,
        (printInfo.m_clipRect.top - printInfo.m_clipRect.bottom)
            * pixelsPerTwipY);
    painter->save();
    painter->setClipRect(clip.normalized(), Qt::IntersectClip);

    const int panelPixelWidth = std::max(
        1, static_cast<int>(std::ceil(m_unitWidth * pixelsPerTwipX)));
    const int panelPixelHeight = std::max(
        1, static_cast<int>(std::ceil(m_unitHeight * pixelsPerTwipY)));
    const int firstPanel = printInfo.m_firstPanel;
    const int panelLimit = std::min(
        static_cast<int>(m_panels.size()), firstPanel
            + printInfo.m_panelsWide * printInfo.m_panelsHigh);

    for (int panelIndex = firstPanel; panelIndex < panelLimit; ++panelIndex) {
        const int column = panelIndex % printInfo.m_panelsWide;
        const int row = panelIndex / printInfo.m_panelsWide
            - printInfo.m_startPanelRow;
        const qreal logicalX = printInfo.m_viewportOrigin.x
            + column * (m_unitWidth + m_vInterstice);
        const qreal logicalY = printInfo.m_viewportOrigin.y
            - row * (m_unitHeight + m_hInterstice);
        const QRectF target(
            pageOrigin.x() + logicalX * pixelsPerTwipX,
            pageOrigin.y() - logicalY * pixelsPerTwipY,
            m_unitWidth * pixelsPerTwipX,
            m_unitHeight * pixelsPerTwipY);
        if (!target.intersects(clip)) continue;

        QImage retained(panelPixelWidth, panelPixelHeight,
                        QImage::Format_RGB32);
        retained.fill(Qt::white);
        QtPaintDC dc(&retained, m_unitWidth, m_unitHeight, true);
        RECT damage{0, 0, m_unitWidth, -m_unitHeight};
        m_panels[panelIndex]->Draw(&dc, nullptr, &damage);
        painter->drawImage(target, retained);
    }
    painter->restore();
}

void CUnitPanelPage::AddTitle(const char* title)
{
    auto* titleLabel = new CLabel(title, m_fiTitle);
    titleLabel->SetBBox(0, -m_unitHeight / 2, m_unitWidth, -100);
    RECT border{};
    titleLabel->GetBBox(&border);
    const QByteArray starring = IntlTextFromQString(QStringView(
        originalResourceString(QStringLiteral("ID_STARRING"))));
    auto* starringLabel = new CLabel(starring.constData(), m_fiShout);
    starringLabel->SetBBox(0, -m_unitHeight, m_unitWidth, border.bottom);
    auto* newPanel = new CUnitPanel;
    newPanel->m_hasBorder = FALSE;
    newPanel->m_backDrop.m_backID = 0;
    newPanel->m_elements.append(titleLabel);
    newPanel->m_elements.append(starringLabel);
    starringLabel->GetBBox(&border);
    AddStars(newPanel, border.bottom);
    AddPanel(newPanel);
}

void CUnitPanelPage::UpdateTitle()
{
    if (m_panels.isEmpty()) {
        const QByteArray title = m_doc
            ? IntlTextFromQString(QStringView(m_doc->GetComicsTitle()))
            : QByteArray();
        AddTitle(title.constData());
        return;
    }
    auto* firstPanel = static_cast<CUnitPanel*>(m_panels.first());
    while (firstPanel->m_elements.size() > 2) delete firstPanel->m_elements.takeLast();
    auto* starring = static_cast<CLabel*>(firstPanel->m_elements.last());
    RECT border{};
    starring->GetBBox(&border);
    AddStars(firstPanel, border.bottom);
    RefreshPanelN(0);
}

void CUnitPanelPage::ShowInfo(USHORT avatarId, const char* info, char hotLinkChar)
{
    char* controlFull = ::strdup(info ? info : "");
    extern void Capitalize(char*);
    Capitalize(controlFull);
    auto* formatting = new CDWordArray;
    CDWordArray* restFormatting = nullptr;
    char* controlLess = SzControlLess(controlFull, formatting);
    if (hotLinkChar != 0)
        formatting = MarkHotLinks(formatting, controlLess, hotLinkChar);
    const int maxBoxHeight = static_cast<int>(m_unitHeight * MAXINFOTEXTHEIGHT);

    while (true) {
        auto* newPanel = new CUnitPanel;
        const int margin = newPanel->m_borderWidth + INFOMARGIN;
        char* rest = nullptr;
        CLabel* box = hotLinkChar != 0
            ? static_cast<CLabel*>(new CHotLinkLabel(controlLess, m_fiWNormal, formatting))
            : static_cast<CLabel*>(new CLabel(controlLess, m_fiWNormal, formatting));
        box->m_format |= FT_LEFT_JUSTIFY;
        newPanel->m_elements.append(box);
        const int top = static_cast<int>(-100 * (static_cast<float>(m_unitHeight) / 4860))
            - box->m_fontI->m_topOffset;
        const int bottom = top - maxBoxHeight;
        box->SetBBox(margin, bottom, m_unitWidth - margin, top);
        RECT border{};
        box->GetBBox(&border);
        if (border.bottom < bottom) {
            rest = box->SplitHeight(maxBoxHeight, &restFormatting);
            box->GetBBox(&border);
        }
        const int boxWidth = border.right - border.left;
        const int newLeft = (m_unitWidth - boxWidth) / 2;
        box->SetBBox(newLeft, border.bottom, newLeft + boxWidth, border.top);

        CAvatarX* avatar = GetAvatar(avatarId);
        if (avatar) {
            CEmotion neutral(0.0, 0.0);
            CBody* body = avatar->GetBodyFromEmotion(neutral);
            body->SetBBox(margin, -m_unitHeight, m_unitWidth - margin, border.bottom);
            newPanel->m_bodies.append(body);
        }
        newPanel->m_backDrop.m_backID = 0;
        AddPanel(newPanel);
        StartNewPanel();

        controlLess = rest;
        FreeAndNullFormatting(&formatting);
        if (!rest) break;
        formatting = restFormatting;
        restFormatting = nullptr;
    }
    std::free(controlFull);
}

void CUnitPanelPage::AddStars(CUnitPanel* panel, int topY)
{
    QList<CAvatarX*> stars;
    QList<CStarLabel*> labels;
    if (MyAvatarID() == 0) return;
    const int lineHeight = m_fiShout->m_lineHeight;
    const int rowHeight = std::max(ROWHEIGHT, lineHeight);
    topY -= BELOWSTARRING * m_unitHeight / 4860;
    const int maxStars = (m_unitHeight + topY) / rowHeight;
    topY -= rowHeight;
    AddStarsAux(stars, maxStars);
    const int starCount = std::min(maxStars, static_cast<int>(stars.size()));
    int maxWidth = 0;
    for (int i = 0; i < starCount; ++i) {
        const char* nickname = nullptr;
        stars[i]->GetScreenName(&nickname);
        auto* label = new CStarLabel(nickname, m_fiShout);
        label->SetBBox(0, -m_unitHeight, m_unitWidth, 0);
        RECT box{};
        label->GetBBox(&box);
        labels.append(label);
        maxWidth = std::max(maxWidth, static_cast<int>(box.right - box.left));
    }
    maxWidth += ICONSIZE + ICONSPACE;
    int iconOffset = (m_unitWidth - maxWidth) / 2;
    if (iconOffset < 0) iconOffset = 0;
    const int textOffset = iconOffset + ICONSIZE + ICONSPACE;
    const int iconVerticalDisplacement = (rowHeight - ICONSIZE) / 2;
    const int textVerticalDisplacement = (rowHeight - lineHeight) / 2;
    for (int i = 0; i < starCount; ++i) {
        labels[i]->m_format |= FT_LEFT_JUSTIFY;
        auto* body = new CBodyUnary(stars[i]->m_avatarID);
        body->m_bodyID = stars[i]->m_icon;
        body->SetBBox(iconOffset, topY + iconVerticalDisplacement,
                      iconOffset + ICONSIZE,
                      topY + ICONSIZE + iconVerticalDisplacement);
        panel->m_elements.append(body);
        labels[i]->SetBBox(textOffset, topY + textVerticalDisplacement,
                           m_unitWidth, topY + lineHeight + textVerticalDisplacement);
        panel->m_elements.append(labels[i]);
        topY -= rowHeight;
    }
}
