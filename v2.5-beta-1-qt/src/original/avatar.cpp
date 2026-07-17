// Ported from v2.5-beta-1-modern/avatar.cpp.
// Qt containers replace MFC arrays and QString replaces CString; pose/body
// selection, neutral fallback, indices, emotions and duplicate ownership keep
// the original control flow.

#include "avatar.h"

#include "avatario.h"
#include "bodycam.h"
#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "intl.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace {
QVector<CAvatarX*> avatars;
QByteArray screenNameBytes;
QStringList avatarNames;
int nextAvatarName = -1;
}

CAvatarX::CAvatarX() { Initialize(); }

void CAvatarX::Initialize()
{
    m_freeze = AF_UNFROZEN;
    m_style = 0;
    m_body = nullptr;
    m_userInfo = nullptr;
    m_avatarID = 0;
    m_origID = 0;
    m_flags = 0;
    m_nSends = 0;
    m_nCopies = 0;
    m_lastDir = FALSE;
    m_lastLeft = 0;
    m_lastRight = 0;
    m_iconIndex = -1;
    m_pStream = nullptr;
    m_arrPoses.reserve(16);
}

CAvatarX::CAvatarX(const CAvatarX& other)
{
    Initialize();
    m_name = QStringLiteral("%1#%2").arg(other.m_name).arg(other.m_nCopies);
    m_origID = other.m_avatarID;
    m_flags = other.m_flags;
    m_icon = other.m_icon;
}

CAvatarX::~CAvatarX()
{
    delete m_body;
    qDeleteAll(m_arrPoses);
    delete m_pStream;
}

CAvatarSimple::CAvatarSimple(const CAvatarSimple& other)
    : CAvatarX(other)
    , m_bodies(other.m_bodies)
    , m_lastBody(-1)
{
}

CAvatarSimple::~CAvatarSimple() = default;

CAvatarComplex::CAvatarComplex(const CAvatarComplex& other)
    : CAvatarX(other)
    , m_faces(other.m_faces)
    , m_torsos(other.m_torsos)
    , m_lastFace(-1)
    , m_lastTorso(-1)
{
}

void CAvatarX::GetScreenName(const char** screenName) const
{
    if (!screenName) {
        return;
    }
    if (auto* userInfo = static_cast<CUserInfo*>(m_userInfo)) {
        screenNameBytes = IntlTextFromQString(
            QStringView(userInfo->GetScreenName()));
    } else {
        screenNameBytes = IntlTextFromQString(QStringView(m_name));
    }
    *screenName = screenNameBytes.constData();
}

CPose* CAvatarX::GetPoseFromID(USHORT id)
{
    if (m_origID != 0) {
        CAvatarX* original = GetOriginalAvatar();
        return original ? original->GetPoseFromID(id) : nullptr;
    }
    if (id == 0 || id > static_cast<USHORT>(m_arrPoses.size())) {
        return nullptr;
    }
    CPose* pose = m_arrPoses[id - 1];
    if (pose->m_pdibs[0] || (m_pStream && pose->Load(m_pStream, &m_palette))) {
        return pose;
    }
    return nullptr;
}

BOOL CAvatarSimple::GetPoseFromID(USHORT id, CPose** pose)
{
    if (!pose) {
        return FALSE;
    }
    *pose = CAvatarX::GetPoseFromID(id);
    if (*pose) {
        return TRUE;
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (const RBODYREC& body : m_bodies) {
            if (pass != 0 || (body.emotion == EM_NEUTRAL && body.intensity == 0.0f)) {
                if ((*pose = CAvatarX::GetPoseFromID(body.poseID))) {
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

BOOL CAvatarComplex::GetPosesFromIDs(USHORT headID, USHORT torsoID, CPose** headPose,
                                     CPose** torsoPose)
{
    if (!headPose || !torsoPose) {
        return FALSE;
    }
    BOOL result = TRUE;
    *headPose = CAvatarX::GetPoseFromID(headID);
    if (!*headPose) {
        for (int pass = 0; pass < 2 && !*headPose; ++pass) {
            for (const FACEREC& face : m_faces) {
                if (pass != 0 || (face.emotion == EM_NEUTRAL && face.intensity == 0.0f)) {
                    if ((*headPose = CAvatarX::GetPoseFromID(face.poseID))) {
                        break;
                    }
                }
            }
        }
        if (!*headPose) {
            result = FALSE;
        }
    }

    *torsoPose = CAvatarX::GetPoseFromID(torsoID);
    if (!*torsoPose) {
        for (int pass = 0; pass < 2 && !*torsoPose; ++pass) {
            for (const BODYREC& torso : m_torsos) {
                if (pass != 0 || (torso.emotion == EM_NEUTRAL && torso.intensity == 0.0f)) {
                    if ((*torsoPose = CAvatarX::GetPoseFromID(torso.poseID))) {
                        break;
                    }
                }
            }
        }
        if (!*torsoPose) {
            return FALSE;
        }
    }
    return result;
}

void CAvatarSimple::GetBodyIndexFromEmotion(CEmotion& emotion, int& bodyIndex)
{
    double nearestAngle = 3 * PI;
    double intensityOfNearest = 2.0;
    bodyIndex = -1;
    if (emotion.m_emotion <= 2 * PI) {
        for (int index = 0; index < m_bodies.size(); ++index) {
            const double angle = std::fabs(subtract_angles(m_bodies[index].emotion,
                                                           emotion.m_emotion));
            if (angle <= nearestAngle) {
                const double delta = std::fabs(emotion.m_intensity - m_bodies[index].intensity);
                if (angle == nearestAngle && delta >= intensityOfNearest) {
                    continue;
                }
                nearestAngle = angle;
                intensityOfNearest = delta;
                bodyIndex = index;
            }
        }
    } else {
        for (int index = 0; index < m_bodies.size(); ++index) {
            if (emotion.m_emotion == m_bodies[index].emotion) {
                bodyIndex = index;
                break;
            }
        }
    }
}

void CAvatarComplex::GetHeadAndBodyFromEmotion(CEmotion& emotion, int& faceIndex,
                                               int& torsoIndex)
{
    double nearestAngle = 3 * PI;
    double intensityOfNearest = 2.0;
    faceIndex = torsoIndex = -1;
    if (emotion.m_emotion <= 2 * PI) {
        for (int index = 0; index < m_faces.size(); ++index) {
            const double angle = std::fabs(subtract_angles(m_faces[index].emotion,
                                                           emotion.m_emotion));
            if (angle <= nearestAngle) {
                const double delta = std::fabs(emotion.m_intensity - m_faces[index].intensity);
                if (angle == nearestAngle && delta >= intensityOfNearest) {
                    continue;
                }
                nearestAngle = angle;
                intensityOfNearest = delta;
                faceIndex = index;
            }
        }
    } else {
        for (int index = 0; index < m_torsos.size(); ++index) {
            if (emotion.m_emotion == m_torsos[index].emotion) {
                torsoIndex = index;
                break;
            }
        }
    }
}

CBody* CAvatarSimple::GetBodyFromEmotion(CEmotion& emotion)
{
    auto* body = new CBodySingle(m_avatarID);
    double intensityOfNearest = 2.0;
    int nearestIndex = -1;
    for (int offset = 0; offset < m_bodies.size(); ++offset) {
        const int index = (m_lastBody + 1 + offset) % m_bodies.size();
        if (m_bodies[index].emotion > 7) {
            continue;
        }
        const double angle = std::fabs(subtract_angles(m_bodies[index].emotion,
                                                       emotion.m_emotion));
        const bool firstNeutral = m_bodies[index].emotion == EM_NEUTRAL
            && m_bodies[index].intensity == 0.0f && nearestIndex == -1;
        if (angle < PI / NEMOTIONS || firstNeutral) {
            const double delta = firstNeutral && emotion.m_intensity > 0.0f
                ? 1.5
                : std::fabs(emotion.m_intensity - m_bodies[index].intensity);
            if (delta < intensityOfNearest) {
                intensityOfNearest = delta;
                nearestIndex = index;
            }
        }
    }
    if (nearestIndex < 0) {
        delete body;
        return nullptr;
    }
    SetBody(body, nearestIndex);
    return body;
}

CBody* CAvatarComplex::GetBodyFromEmotion(CEmotion& emotion)
{
    if (m_faces.isEmpty() || m_torsos.isEmpty()) {
        return nullptr;
    }
    auto* body = new CBodyDouble(m_avatarID);
    double nearestAngle = 3 * PI;
    double intensityOfNearest = 2.0;
    int nearestIndex = 0;
    for (int index = 0; index < m_faces.size(); ++index) {
        const double angle = std::fabs(subtract_angles(m_faces[index].emotion,
                                                       emotion.m_emotion));
        if (angle <= nearestAngle) {
            const double delta = std::fabs(emotion.m_intensity - m_faces[index].intensity);
            if (angle == nearestAngle && delta >= intensityOfNearest) {
                continue;
            }
            nearestAngle = angle;
            intensityOfNearest = delta;
            nearestIndex = index;
        }
    }
    SetFace(body, nearestIndex);

    intensityOfNearest = 2.0;
    nearestIndex = 0;
    for (int offset = 0; offset < m_torsos.size(); ++offset) {
        const int index = (m_lastTorso + 1 + offset) % m_torsos.size();
        if (m_torsos[index].emotion > 7) {
            continue;
        }
        const double angle = std::fabs(subtract_angles(m_torsos[index].emotion,
                                                       emotion.m_emotion));
        if (angle < PI / NEMOTIONS
            || (m_torsos[index].emotion == EM_NEUTRAL
                && m_torsos[index].intensity == 0.0f)) {
            const double delta = std::fabs(emotion.m_intensity - m_torsos[index].intensity);
            if (delta < intensityOfNearest) {
                intensityOfNearest = delta;
                nearestIndex = index;
            }
        }
    }
    SetTorso(body, nearestIndex);
    return body;
}

CBody* CAvatarSimple::GetBodyFromEmotion(CEmotionOpts& options)
{
    auto* body = new CBodySingle(m_avatarID);
    int found = -1;
    while (true) {
        UCHAR minimumPriority = 0;
        int bestIndex = 0;
        for (int index = 0; index < options.m_nOpts; ++index) {
            if (options.m_priorities[index] > minimumPriority) {
                bestIndex = index;
                minimumPriority = options.m_priorities[index];
            }
        }
        if (!minimumPriority) {
            break;
        }
        int bodyIndex = -1;
        GetBodyIndexFromEmotion(options.m_emotions[bestIndex], bodyIndex);
        options.m_priorities[bestIndex] = 0;
        if (bodyIndex >= 0 && found < 0) {
            SetBody(body, bodyIndex);
            found = bodyIndex;
            break;
        }
    }
    if (found < 0) {
        SetBodyNeutral(body);
    }
    return body;
}

CBody* CAvatarComplex::GetBodyFromEmotion(CEmotionOpts& options)
{
    auto* body = new CBodyDouble(m_avatarID);
    int foundFace = -1;
    int foundTorso = -1;
    while (true) {
        UCHAR minimumPriority = 0;
        int bestIndex = 0;
        for (int index = 0; index < options.m_nOpts; ++index) {
            if (options.m_priorities[index] > minimumPriority) {
                bestIndex = index;
                minimumPriority = options.m_priorities[index];
            }
        }
        if (!minimumPriority) {
            break;
        }
        int faceIndex = -1;
        int torsoIndex = -1;
        GetHeadAndBodyFromEmotion(options.m_emotions[bestIndex], faceIndex, torsoIndex);
        options.m_priorities[bestIndex] = 0;
        if (faceIndex >= 0 && foundFace < 0) {
            SetFace(body, faceIndex);
            foundFace = faceIndex;
        }
        if (torsoIndex >= 0 && foundTorso < 0) {
            SetTorso(body, torsoIndex);
            foundTorso = torsoIndex;
        }
        if (foundFace >= 0 && foundTorso >= 0) {
            break;
        }
    }
    if (foundFace < 0) {
        SetFaceNeutral(body);
    }
    if (foundTorso < 0) {
        SetTorsoNeutral(body);
    }
    return body;
}

void CAvatarSimple::SetBodyNeutral(CBodySingle* body)
{
    if (!body || m_bodies.isEmpty()) {
        return;
    }
    int candidate = m_lastBody;
    for (int index = 0; index < m_bodies.size(); ++index) {
        candidate = (candidate + 1) % m_bodies.size();
        if (m_bodies[candidate].emotion == EM_NEUTRAL
            && m_bodies[candidate].intensity == 0.0f) {
            SetBody(body, candidate);
            return;
        }
    }
    SetBody(body, 0);
}

void CAvatarComplex::SetFaceNeutral(CBodyDouble* body)
{
    if (!body || m_faces.isEmpty()) {
        return;
    }
    int candidate = m_lastFace;
    for (int index = 0; index < m_faces.size(); ++index) {
        candidate = (candidate + 1) % m_faces.size();
        if (m_faces[candidate].emotion == EM_NEUTRAL
            && m_faces[candidate].intensity == 0.0f) {
            SetFace(body, candidate);
            return;
        }
    }
    SetFace(body, 0);
}

void CAvatarComplex::SetTorsoNeutral(CBodyDouble* body)
{
    if (!body || m_torsos.isEmpty()) {
        return;
    }
    int candidate = m_lastTorso;
    for (int index = 0; index < m_torsos.size(); ++index) {
        candidate = (candidate + 1) % m_torsos.size();
        if (m_torsos[candidate].emotion == EM_NEUTRAL
            && m_torsos[candidate].intensity == 0.0f) {
            SetTorso(body, candidate);
            return;
        }
    }
    SetTorso(body, 0);
}

void CAvatarSimple::SetNeutral()
{
    CEmotion neutral(0.0, 0.0);
    UpdateBody(GetBodyFromEmotion(neutral));
}

void CAvatarComplex::SetNeutral()
{
    if (m_faces.isEmpty() || m_torsos.isEmpty()) {
        return;
    }
    auto* body = new CBodyDouble(m_avatarID);
    SetTorsoNeutral(body);
    SetFaceNeutral(body);
    UpdateBody(body);
}

void CAvatarX::UpdateBody(CBody* newBody)
{
    if (!newBody) {
        return;
    }
    if (newBody->IsSame(m_body)) {
        delete newBody;
        return;
    }
    delete m_body;
    m_body = newBody;
}

void CAvatarComplex::DifferentTorso(int torsoIndex)
{
    if (!m_body || m_body->GetClass() != BC_BODYDOUBLE || torsoIndex < 0
        || torsoIndex >= m_torsos.size()) {
        return;
    }
    auto* body = new CBodyDouble(*static_cast<CBodyDouble*>(m_body));
    body->m_torsoRec = m_torsos.data() + torsoIndex;
    UpdateBody(body);
}

void CEmotionOpts::Add(double emotion, double intensity, int priority, int flags)
{
    for (int index = 0; index < m_nOpts; ++index) {
        if (m_emotions[index].m_emotion == emotion) {
            if (flags & OVERRIDEBYPRIORITY) {
                if (m_priorities[index] < priority) {
                    m_priorities[index] = static_cast<UCHAR>(priority);
                    m_emotions[index].m_intensity = static_cast<float>(intensity);
                }
                return;
            }
            if (flags & ADDPRIORITY) {
                m_priorities[index] = static_cast<UCHAR>(
                    std::max(static_cast<int>(m_priorities[index]) + priority, 255));
                m_emotions[index].m_intensity = std::max(
                    m_emotions[index].m_intensity, static_cast<float>(intensity));
                return;
            }
        }
    }
    if (m_nOpts >= MAXEMOPTS) {
        return;
    }
    m_emotions[m_nOpts].m_emotion = static_cast<float>(emotion);
    m_emotions[m_nOpts].m_intensity = static_cast<float>(intensity);
    m_priorities[m_nOpts++] = static_cast<UCHAR>(priority);
}

BOOL CBodyDouble::IsSame(CBody* other)
{
    if (!other || GetClass() != other->GetClass()) {
        return FALSE;
    }
    auto* body = static_cast<CBodyDouble*>(other);
    return m_faceRec == body->m_faceRec && m_torsoRec == body->m_torsoRec;
}

BOOL CBodySingle::IsSame(CBody* other)
{
    if (!other || GetClass() != other->GetClass()) {
        return FALSE;
    }
    return GetPoseID() == static_cast<CBodySingle*>(other)->GetPoseID();
}

void CBodySingle::GetDimInfo(SHORT& xdim, SHORT& ydim, SHORT& normalHeight,
                             SHORT& headHeight, SHORT& faceX)
{
    auto* avatar = dynamic_cast<CAvatarSimple*>(GetAvatar(static_cast<USHORT>(m_avatarID)));
    CPose* pose = nullptr;
    if (avatar && avatar->GetPoseFromID(static_cast<USHORT>(GetPoseID()), &pose)
        && pose && pose->GetDrawing()) {
        xdim = static_cast<SHORT>(pose->GetDrawing()->GetWidth());
        ydim = static_cast<SHORT>(pose->GetDrawing()->GetHeight());
        headHeight = ydim / 2;
        normalHeight = 100;
    } else {
        xdim = ydim = 100;
        headHeight = 50;
        normalHeight = 100;
    }
    faceX = m_bodyRec ? m_bodyRec->faceX : 0;
    if (m_flip) {
        faceX = xdim - faceX;
    }
}

void CBodyDouble::GetDimInfo(SHORT& xdim, SHORT& ydim, SHORT& normalHeight,
                             SHORT& headHeight, SHORT& faceX)
{
    auto* avatar = dynamic_cast<CAvatarComplex*>(GetAvatar(static_cast<USHORT>(m_avatarID)));
    CPose* head = nullptr;
    CPose* torso = nullptr;
    int headWidth = 50;
    int headImageHeight = 50;
    int torsoWidth = 50;
    int torsoHeight = 50;
    if (avatar && m_faceRec && m_torsoRec
        && avatar->GetPosesFromIDs(m_faceRec->poseID, m_torsoRec->poseID, &head, &torso)
        && head && torso && head->GetDrawing() && torso->GetDrawing()) {
        headWidth = head->GetDrawing()->GetWidth();
        headImageHeight = head->GetDrawing()->GetHeight();
        torsoWidth = torso->GetDrawing()->GetWidth();
        torsoHeight = torso->GetDrawing()->GetHeight();
    }
    const int xOffset = m_torsoRec && m_faceRec
        ? m_torsoRec->xCX + m_faceRec->delta_xCX - m_faceRec->xCX
        : 0;
    const int yOffset = m_torsoRec && m_faceRec
        ? m_torsoRec->yCX + m_faceRec->delta_yCX - m_faceRec->yCX
        : 0;
    const int left = std::min(0, xOffset);
    const int right = std::max(torsoWidth, xOffset + headWidth);
    const int top = std::min(0, yOffset);
    headHeight = static_cast<SHORT>(yOffset + headImageHeight - top);
    const int bottom = std::max(torsoHeight, yOffset + headImageHeight);
    xdim = static_cast<SHORT>(right - left);
    ydim = static_cast<SHORT>(bottom - top);
    normalHeight = 100;
    faceX = static_cast<SHORT>((m_faceRec ? m_faceRec->faceX : 0) + xOffset - left);
    if (m_flip) {
        faceX = xdim - faceX;
    }
}

void CAvatarSimple::RecordBody(CBody* body)
{
    if (body && body->GetClass() == BC_BODYSINGLE) {
        m_lastBody = static_cast<SHORT>(
            static_cast<CBodySingle*>(body)->m_bodyRec - m_bodies.constData());
    }
}

void CAvatarComplex::RecordBody(CBody* body)
{
    if (body && body->GetClass() == BC_BODYDOUBLE) {
        auto* doubleBody = static_cast<CBodyDouble*>(body);
        m_lastFace = static_cast<SHORT>(doubleBody->m_faceRec - m_faces.constData());
        m_lastTorso = static_cast<SHORT>(doubleBody->m_torsoRec - m_torsos.constData());
    }
}

void CAvatarSimple::GetIndices(CHAR& faceIndex, CHAR& torsoIndex, BYTE& requested)
{
    auto* body = m_body && m_body->GetClass() == BC_BODYSINGLE
        ? static_cast<CBodySingle*>(m_body)
        : nullptr;
    faceIndex = 0;
    torsoIndex = body ? static_cast<CHAR>(body->m_bodyRec - m_bodies.constData()) : -1;
    requested = m_freeze != AF_UNFROZEN;
}

void CAvatarComplex::GetIndices(CHAR& faceIndex, CHAR& torsoIndex, BYTE& requested)
{
    auto* body = m_body && m_body->GetClass() == BC_BODYDOUBLE
        ? static_cast<CBodyDouble*>(m_body)
        : nullptr;
    faceIndex = body ? static_cast<CHAR>(body->m_faceRec - m_faces.constData()) : -1;
    torsoIndex = body ? static_cast<CHAR>(body->m_torsoRec - m_torsos.constData()) : -1;
    requested = m_freeze != AF_UNFROZEN;
}

void CAvatarSimple::SetIndices(CHAR, CHAR torsoIndex, BYTE requested)
{
    if (!m_body || m_body->GetClass() != BC_BODYSINGLE) {
        return;
    }
    auto* body = static_cast<CBodySingle*>(m_body);
    if (torsoIndex >= 0 && torsoIndex < m_bodies.size()) {
        body->m_bodyRec = m_bodies.data() + torsoIndex;
    }
    body->m_requested = requested;
}

void CAvatarComplex::SetIndices(CHAR faceIndex, CHAR torsoIndex, BYTE requested)
{
    if (!m_body || m_body->GetClass() != BC_BODYDOUBLE) {
        return;
    }
    auto* body = static_cast<CBodyDouble*>(m_body);
    if (faceIndex >= 0 && faceIndex < m_faces.size()) {
        body->m_faceRec = m_faces.data() + faceIndex;
    }
    if (torsoIndex >= 0 && torsoIndex < m_torsos.size()) {
        body->m_torsoRec = m_torsos.data() + torsoIndex;
    }
    body->m_requested = requested;
}

void CAvatarSimple::GetEmotions(CEmotion& face, CEmotion& torso)
{
    auto* body = m_body && m_body->GetClass() == BC_BODYSINGLE
        ? static_cast<CBodySingle*>(m_body)
        : nullptr;
    if (body && body->m_bodyRec) {
        face.m_emotion = body->m_bodyRec->emotion;
        face.m_intensity = body->m_bodyRec->intensity;
    }
    torso.m_emotion = torso.m_intensity = 0.0f;
}

void CAvatarComplex::GetEmotions(CEmotion& face, CEmotion& torso)
{
    auto* body = m_body && m_body->GetClass() == BC_BODYDOUBLE
        ? static_cast<CBodyDouble*>(m_body)
        : nullptr;
    if (!body || !body->m_faceRec || !body->m_torsoRec) {
        face = {};
        torso = {};
        return;
    }
    face.m_emotion = body->m_faceRec->emotion;
    face.m_intensity = body->m_faceRec->intensity;
    torso.m_emotion = body->m_torsoRec->emotion;
    torso.m_intensity = body->m_torsoRec->intensity;
}

void CAvatarSimple::SetEmotions(CEmotion& face, CEmotion&)
{
    if (!m_body || m_body->GetClass() != BC_BODYSINGLE || m_bodies.isEmpty()) {
        return;
    }
    auto* body = static_cast<CBodySingle*>(m_body);
    double bestDelta = 1000.0;
    int bestIndex = -1;
    int index = static_cast<int>(body->m_bodyRec - m_bodies.constData());
    const int endIndex = index;
    do {
        index = (index + 1) % m_bodies.size();
        if (m_bodies[index].emotion == face.m_emotion) {
            const float delta = std::fabs(m_bodies[index].intensity - face.m_intensity);
            if (delta < bestDelta) {
                bestDelta = delta;
                bestIndex = index;
            }
        }
    } while (index != endIndex);
    if (bestIndex >= 0) {
        body->m_bodyRec = m_bodies.data() + bestIndex;
    } else {
        SetBodyNeutral(body);
    }
}

void CAvatarComplex::SetEmotions(CEmotion& face, CEmotion& torso)
{
    if (!m_body || m_body->GetClass() != BC_BODYDOUBLE || m_faces.isEmpty()
        || m_torsos.isEmpty()) {
        return;
    }
    auto* body = static_cast<CBodyDouble*>(m_body);
    double bestDelta = 1000.0;
    int bestIndex = -1;
    int index = static_cast<int>(body->m_faceRec - m_faces.constData());
    const int endFace = index;
    do {
        index = (index + 1) % m_faces.size();
        if (m_faces[index].emotion == face.m_emotion) {
            const float delta = std::fabs(m_faces[index].intensity - face.m_intensity);
            if (delta < bestDelta) {
                bestDelta = delta;
                bestIndex = index;
            }
        }
    } while (index != endFace);
    if (bestIndex >= 0) {
        body->m_faceRec = m_faces.data() + bestIndex;
    } else {
        SetFaceNeutral(body);
    }

    bestDelta = 1000.0;
    bestIndex = -1;
    index = static_cast<int>(body->m_torsoRec - m_torsos.constData());
    const int endTorso = index;
    do {
        index = (index + 1) % m_torsos.size();
        if (m_torsos[index].emotion == torso.m_emotion) {
            const float delta = std::fabs(m_torsos[index].intensity - torso.m_intensity);
            if (delta < bestDelta) {
                bestDelta = delta;
                bestIndex = index;
            }
        }
    } while (index != endTorso);
    if (bestIndex >= 0) {
        body->m_torsoRec = m_torsos.data() + bestIndex;
    } else {
        SetTorsoNeutral(body);
    }
}

void CAvatarSimple::SetSequential(void* userInfo, int number)
{
    if (!userInfo || m_bodies.isEmpty()) {
        return;
    }
    const int body = number % m_bodies.size();
    auto* pui = static_cast<CUserInfo*>(userInfo);
    pui->m_udi.m_chExpr = static_cast<signed char>(body);
    pui->m_udi.m_chGest = static_cast<signed char>(body);
    pui->m_udi.m_bbCooked = TRUE;
}

void CAvatarComplex::SetSequential(void* userInfo, int)
{
    if (!userInfo || m_faces.isEmpty() || m_torsos.isEmpty()) {
        return;
    }
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    const int head = static_cast<int>((std::rand() / (RAND_MAX + 1.0)) * m_faces.size());
    const int torso = static_cast<int>((std::rand() / (RAND_MAX + 1.0)) * m_torsos.size());
    auto* pui = static_cast<CUserInfo*>(userInfo);
    pui->m_udi.m_chExpr = static_cast<signed char>(head);
    pui->m_udi.m_chGest = static_cast<signed char>(torso);
    pui->m_udi.m_bbCooked = TRUE;
}

CAvatarX* CAvatarSimple::DupAvatar()
{
    ++m_nCopies;
    return new CAvatarSimple(*this);
}

CAvatarX* CAvatarComplex::DupAvatar()
{
    ++m_nCopies;
    return new CAvatarComplex(*this);
}

CAvatarX* CAvatarX::IndexAvatar()
{
    if (avatars.isEmpty()) {
        InitializeAvatars();
    }
    m_avatarID = static_cast<USHORT>(avatars.size());
    avatars.append(this);
    SetNeutral();
    return this;
}

CAvatarX* CAvatarX::GetOriginalAvatar()
{
    CAvatarX* avatar = this;
    while (avatar && avatar->m_origID != 0) {
        avatar = GetAvatar(avatar->m_origID);
    }
    return avatar;
}

const char* CAvatarX::OriginalName()
{
    CAvatarX* avatar = GetOriginalAvatar();
    screenNameBytes = avatar ? avatar->m_name.toUtf8() : QByteArray();
    return avatar ? screenNameBytes.constData() : nullptr;
}

const char* CAvatarX::Url()
{
    CAvatarX* avatar = GetOriginalAvatar();
    if (!avatar) {
        return nullptr;
    }
    return avatar->m_newUrl.isEmpty() ? avatar->m_originalUrl.constData()
                                      : avatar->m_newUrl.constData();
}

const char* CAvatarX::Copyright()
{
    CAvatarX* avatar = GetOriginalAvatar();
    return avatar && !avatar->m_copyright.isEmpty() ? avatar->m_copyright.constData() : nullptr;
}

void InitializeAvatars()
{
    if (avatars.isEmpty()) {
        avatars.append(nullptr);
    }
}

void DestroyAvatars()
{
    for (int index = avatars.size() - 1; index >= 1; --index) {
        delete avatars[index];
    }
    avatars.clear();
    avatars.append(nullptr);
}

int GetAvatarUpperBound() { return avatars.size() - 1; }

CAvatarX* GetAvatar(USHORT avatarID)
{
    return avatarID < avatars.size() ? avatars[avatarID] : nullptr;
}

CAvatarX* GetAvatar(const QString& name)
{
    for (int index = 1; index < avatars.size(); ++index) {
        if (avatars[index] && avatars[index]->m_name.compare(name, Qt::CaseInsensitive) == 0) {
            return avatars[index];
        }
    }
    return nullptr;
}

CAvatarX* LoadAvatar(const QString& avatarName)
{
    CAvatarX* avatar = LoadAvatarInfo(avatarName);
    return avatar ? avatar->IndexAvatar() : nullptr;
}

CAvatarX* GetAvatar2(const QString& name)
{
    if (CAvatarX* avatar = GetAvatar(name)) {
        return avatar;
    }
    return LoadAvatar(name);
}

QStringList GetAllAvatarNames()
{
    if (avatarNames.isEmpty()) {
        avatarNames = OriginalAvatarNames();
    }
    return avatarNames;
}

void ResetAvatarNames()
{
    nextAvatarName = -1;
    avatarNames.clear();
}

void GetNextAvatarName(QString& avatarName)
{
    if (nextAvatarName == -1) {
        avatarNames = OriginalAvatarNames();
    }
    const int upperBound = avatarNames.size() - 1;
    if (upperBound == -1) {
        avatarName = QStringLiteral("_NoArt");
        return;
    }
    ++nextAvatarName;
    if (nextAvatarName > upperBound) {
        nextAvatarName = 0;
    }
    if (MyAvatar()
        && avatarNames[nextAvatarName].compare(MyAvatar()->m_name, Qt::CaseInsensitive) == 0
        && upperBound > 1) {
        GetNextAvatarName(avatarName);
        return;
    }
    avatarName = avatarNames[nextAvatarName];
}

CAvatarX* GetAvatar3(const QString& name, void* userInfo, BOOL randomIfNotFound)
{
    CAvatarX* original = nullptr;
    for (int index = 1; index < avatars.size(); ++index) {
        CAvatarX* avatar = avatars[index];
        if (!avatar || QString::fromUtf8(avatar->OriginalName()).compare(name, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (!avatar->m_userInfo || avatar->m_userInfo == userInfo) {
            avatar->m_flags &= ~OTHERMAPPED;
            return avatar;
        }
        if (avatar->m_origID == 0) {
            original = avatar;
        }
    }
    if (original) {
        CAvatarX* duplicate = original->DupAvatar();
        duplicate->IndexAvatar();
        duplicate->m_flags &= ~OTHERMAPPED;
        return duplicate;
    }
    if (CAvatarX* loaded = LoadAvatar(name)) {
        return loaded;
    }
    if (!randomIfNotFound) {
        return nullptr;
    }

    const QStringList names = GetAllAvatarNames();
    if (names.isEmpty()) {
        return nullptr;
    }
    nextAvatarName = (nextAvatarName + 1) % names.size();
    CAvatarX* replacement = GetAvatar3(names[nextAvatarName], userInfo, FALSE);
    if (replacement) {
        replacement->m_flags |= OTHERMAPPED;
    }
    return replacement;
}

CAvatarX* MyAvatar()
{
    return GetChatDoc() && GetChatDoc()->m_myAvatarID != 0
        ? GetAvatar(GetChatDoc()->m_myAvatarID)
        : nullptr;
}

unsigned int MyAvatarID() { return GetChatDoc() ? GetChatDoc()->m_myAvatarID : 0; }

const char* MyAvatarURL()
{
    CAvatarX* avatar = MyAvatar();
    return avatar ? avatar->Url() : nullptr;
}

void SetMyAvatarID(USHORT id)
{
    if (GetChatDoc()) {
        GetChatDoc()->m_myAvatarID = id;
    }
}

void SetMyAvatar(UINT avatarID, BOOL broadcast)
{
    CChatDoc* document = GetChatDoc();
    CAvatarX* avatar = GetAvatar(static_cast<USHORT>(avatarID));
    if (!document || !avatar || avatarID == MyAvatarID()) {
        return;
    }
    document->m_myAvatarID = static_cast<USHORT>(avatarID);
    if (g_puiSelf) {
        g_puiSelf->SetAvatarID(static_cast<USHORT>(avatarID));
        avatar->m_userInfo = g_puiSelf;
    }
    RefreshBodyCam(avatar);
    const QString originalName = QString::fromUtf8(avatar->OriginalName());
    SetMyCharacter(originalName);
    if (!theApp.m_bNoRefresh && broadcast && document->m_proto) {
        document->m_proto->ChatAnnounceNewAvatar(
            originalName, QString::fromUtf8(avatar->Url() ? avatar->Url() : ""));
    }
}

BOOL SetMyAvatar(const QString& avatarName, BOOL broadcast)
{
    CChatDoc* document = GetChatDoc();
    if (!document) {
        return FALSE;
    }
    document->m_myAvatarID = 0;
    CAvatarX* avatar = GetAvatar3(avatarName, g_puiSelf);
    if (!avatar) {
        return FALSE;
    }
    SetMyAvatar(avatar->m_avatarID, broadcast);
    return TRUE;
}

void ResetAvatar(int avatarID)
{
    CAvatarX* avatar = GetAvatar(static_cast<USHORT>(avatarID));
    if (!avatar) {
        return;
    }
    if (avatar->m_freeze == AF_TEMPFROZEN) {
        avatar->m_freeze = AF_UNFROZEN;
    }
    if (avatar->m_freeze == AF_UNFROZEN) {
        avatar->SetNeutral();
    }
}

BOOL NullAvatar() { return MyAvatar() == nullptr; }
