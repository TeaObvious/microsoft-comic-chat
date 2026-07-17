// Ported from v2.5-beta-1-modern/avatar.h.
// MFC containers and GDI drawing handles are replaced mechanically; avatar
// records, class boundaries, emotion selection and lazy pose semantics remain.

#pragma once

#include "avbfile.h"
#include "pe.h"
#include "vector2d.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

constexpr int NEMOTIONS = 8;
constexpr float PI = static_cast<float>(VECTOR_PI);
constexpr float EM_HAPPY = 0 * 2 * PI / 8;
constexpr float EM_COY = 1 * 2 * PI / 8;
constexpr float EM_BORED = 2 * 2 * PI / 8;
constexpr float EM_SCARED = 3 * 2 * PI / 8;
constexpr float EM_SAD = 4 * 2 * PI / 8;
constexpr float EM_ANGRY = 5 * 2 * PI / 8;
constexpr float EM_SHOUT = 6 * 2 * PI / 8;
constexpr float EM_LAUGH = 7 * 2 * PI / 8;
constexpr float EM_NEUTRAL = 0.0f;
constexpr float EM_WAVE = 1001.0f;
constexpr float EM_POINTOTHER = 1002.0f;
constexpr float EM_POINTSELF = 1003.0f;
constexpr float EM_DOUBLEPOINT = 1004.0f;
constexpr float EM_SHRUG = 1005.0f;
constexpr float EM_3QRWALK = 1006.0f;
constexpr float EM_SIDEWALK = 1007.0f;
constexpr float EM_3QFWALK = 1008.0f;

constexpr UCHAR AF_UNFROZEN = 1;
constexpr UCHAR AF_TEMPFROZEN = 2;
constexpr UCHAR AF_FROZEN = 3;
constexpr UCHAR HEADMASK = 1;
constexpr UCHAR TORSOMASK = 2;
constexpr UCHAR TORSOFIRST = 4;
constexpr UCHAR OTHERMAPPED = 8;
constexpr UCHAR AVUSAGE_FREE = 0;
constexpr UCHAR AVUSAGE_ORIGINAL = 1;

class CPose {
public:
    CPose(const DWORD* offsets, const BYTE* formats, const BYTE* paletteTypes);
    ~CPose();

    BOOL Load(CAvatarStream* stream, CAvatarPalette* globalPalette);
    BOOL ConvertFromMaskedMono(CAvatarDIB* source);
    BOOL ConvertFromDualMask(CAvatarDIB* source);
    BOOL ConvertMasksCommon(CAvatarDIB* source, CAvatarDIB** output, int count);

    CAvatarDIB* GetDrawing() const { return m_pdibs[0]; }
    CAvatarDIB* GetMask() const { return m_pdibs[1]; }
    CAvatarDIB* GetAura() const { return m_pdibs[2]; }

    DWORD m_dwOffsets[3]{};
    BYTE m_byFormats[3]{};
    BYTE m_byPaletteTypes[3]{};
    CAvatarDIB* m_pdibs[3]{};
};

class CEmotion {
public:
    CEmotion() = default;
    CEmotion(double intensity, double emotion)
        : m_intensity(static_cast<float>(intensity))
        , m_emotion(static_cast<float>(emotion))
    {
    }
    void Set(double intensity, double emotion)
    {
        m_intensity = static_cast<float>(intensity);
        m_emotion = static_cast<float>(emotion);
    }

    float m_intensity = 0.0f;
    float m_emotion = 0.0f;
};

constexpr int MAXEMOPTS = 10;
constexpr int OVERRIDEBYPRIORITY = 1;
constexpr int ADDPRIORITY = 2;

class CEmotionOpts {
public:
    void Add(int emotion, double intensity, int priority, int flags = OVERRIDEBYPRIORITY)
    {
        Add(static_cast<double>(emotion), intensity, priority, flags);
    }
    void Add(double emotion, double intensity, int priority, int flags = OVERRIDEBYPRIORITY);

    UCHAR m_nOpts = 0;
    CEmotion m_emotions[MAXEMOPTS];
    UCHAR m_priorities[MAXEMOPTS]{};
};

constexpr UCHAR BC_BODYDOUBLE = 1;
constexpr UCHAR BC_BODYSINGLE = 2;

#pragma pack(push, 1)
struct FACEREC {
    USHORT poseID;
    float emotion;
    float intensity;
    SHORT xCX;
    SHORT yCX;
    SHORT delta_xCX;
    SHORT delta_yCX;
    UCHAR faceX;
    UCHAR faceY;
};

struct BODYREC {
    USHORT poseID;
    float emotion;
    float intensity;
    SHORT xCX;
    SHORT yCX;
};

struct RBODYREC {
    USHORT poseID;
    float emotion;
    float intensity;
    UCHAR faceX;
    UCHAR faceY;
};
#pragma pack(pop)

class CBody : public CPanelElement {
public:
    CBody() = default;
    explicit CBody(unsigned int avatarID)
        : m_avatarID(avatarID)
        , m_requested(TRUE)
    {
    }
    CBody(const CBody&) = default;
    virtual ~CBody() = default;

    virtual BOOL IsSame(CBody* other) = 0;
    virtual UCHAR GetClass() = 0;
    virtual RECT DrawBody(QImage* target, RECT& clientArea, BOOL drawNimbus) = 0;
    virtual CBody* Clone() = 0;
    virtual void GetDimInfo(SHORT& xdim, SHORT& ydim, SHORT& normalHeight,
                            SHORT& headHeight, SHORT& faceX) = 0;

    UINT m_avatarID = 0;
    UCHAR m_flip = FALSE;
    UCHAR m_requested = TRUE;
    SHORT m_arrowX = 0;
};

class CBodyDouble final : public CBody {
public:
    CBodyDouble() = default;
    explicit CBodyDouble(unsigned int avatarID) : CBody(avatarID) {}
    CBodyDouble(const CBodyDouble&) = default;
    BOOL IsSame(CBody* other) override;
    void Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage) override;
    UCHAR GetClass() override { return BC_BODYDOUBLE; }
    RECT DrawBody(QImage* target, RECT& clientArea, BOOL drawNimbus) override;
    CBody* Clone() override { return new CBodyDouble(*this); }
    void GetDimInfo(SHORT& xdim, SHORT& ydim, SHORT& normalHeight,
                    SHORT& headHeight, SHORT& faceX) override;

    FACEREC* m_faceRec = nullptr;
    BODYREC* m_torsoRec = nullptr;

    void GetBodyBox(CPose* head, CPose* body, RECT& clientRect, RECT& fullRect,
                    RECT& headRect, RECT& torsoRect);
    void FlipBodyBox(RECT& fullRect, RECT& headRect, RECT& torsoRect);
};

class CBodySingle : public CBody {
public:
    CBodySingle() = default;
    explicit CBodySingle(unsigned int avatarID) : CBody(avatarID) {}
    CBodySingle(const CBodySingle&) = default;
    BOOL IsSame(CBody* other) override;
    void Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage) override;
    UCHAR GetClass() override { return BC_BODYSINGLE; }
    RECT DrawBody(QImage* target, RECT& clientArea, BOOL drawNimbus) override;
    CBody* Clone() override { return new CBodySingle(*this); }
    void GetDimInfo(SHORT& xdim, SHORT& ydim, SHORT& normalHeight,
                    SHORT& headHeight, SHORT& faceX) override;
    virtual SHORT GetPoseID() const { return m_bodyRec ? static_cast<SHORT>(m_bodyRec->poseID) : 0; }

    void GetBodyBox(CPose* pose, RECT& clientRect, RECT& fullRect);
    void FlipBodyBox(RECT& fullRect);

    RBODYREC* m_bodyRec = nullptr;
};

class CBodyUnary final : public CBodySingle {
public:
    using CBodySingle::CBodySingle;
    CBodyUnary(const CBodyUnary&) = default;
    CBody* Clone() override { return new CBodyUnary(*this); }
    SHORT GetPoseID() const override { return static_cast<SHORT>(m_bodyID); }
    USHORT m_bodyID = 0;
};

class CAvatarX {
public:
    CAvatarX();
    CAvatarX(const CAvatarX& other);
    virtual ~CAvatarX();

    virtual CBody* GetBodyFromEmotion(CEmotion& emotion) = 0;
    virtual CBody* GetBodyFromEmotion(CEmotionOpts& options) = 0;
    virtual void DifferentTorso(int) {}
    virtual void SetNeutral() = 0;
    virtual void SetSequential(void* userInfo, int number) = 0;
    virtual void RecordBody(CBody* body) = 0;
    virtual void GetIndices(CHAR& faceIndex, CHAR& torsoIndex, BYTE& requested) = 0;
    virtual void SetIndices(CHAR faceIndex, CHAR torsoIndex, BYTE requested) = 0;
    virtual void GetEmotions(CEmotion& face, CEmotion& torso) = 0;
    virtual void SetEmotions(CEmotion& face, CEmotion& torso) = 0;
    virtual CAvatarX* DupAvatar() = 0;
    virtual BOOL HandleLoadTag(CAvatarStream* stream, AVBINT16 tag, AVBINT16 size,
                               long& resourceAdjustment);

    void Initialize();
    void UpdateBody(CBody* body);
    void GetScreenName(const char** name) const;
    CAvatarX* IndexAvatar();
    CAvatarX* GetOriginalAvatar();
    const char* OriginalName();
    const char* Url();
    const char* Copyright();
    void SetNewName(const QString& name) { m_name = name; }
    void SetStream(CAvatarStream* stream) { m_pStream = stream; }
    CPose* GetIconPose() { return GetPoseFromID(m_icon); }
    int GetPoseCount() const { return m_arrPoses.size(); }

    static CAvatarX* LoadAvatar(CAvatarStream* stream);
    USHORT CreatePose(CAvatarStream* stream, DWORD offset, BYTE format, BYTE paletteType);
    USHORT CreatePoseWithMask(CAvatarStream* stream, DWORD* offsets, BYTE* formats,
                              BYTE* paletteTypes);

    USHORT m_avatarID = 0;
    USHORT m_origID = 0;
    QString m_name;
    UCHAR m_style = 0;
    UCHAR m_freeze = AF_UNFROZEN;
    UCHAR m_flags = 0;
    UCHAR m_lastDir = FALSE;
    CBody* m_body = nullptr;
    USHORT m_icon = 0;
    USHORT m_lastRight = 0;
    USHORT m_lastLeft = 0;
    USHORT m_nSends = 0;
    USHORT m_nCopies = 0;
    SHORT m_iconIndex = -1;
    void* m_userInfo = nullptr;
    QByteArray m_originalUrl;
    QByteArray m_newUrl;
    QByteArray m_copyright;
    CAvatarPalette m_palette;
    CAvatarStream* m_pStream = nullptr;
    QVector<CPose*> m_arrPoses;

protected:
    CPose* GetPoseFromID(USHORT id);
};

class CAvatarSimple final : public CAvatarX {
public:
    CAvatarSimple() { m_lastBody = -1; }
    CAvatarSimple(const CAvatarSimple& other);
    ~CAvatarSimple() override;

    CBody* GetBodyFromEmotion(CEmotion& emotion) override;
    CBody* GetBodyFromEmotion(CEmotionOpts& options) override;
    void SetNeutral() override;
    void SetSequential(void* userInfo, int number) override;
    void RecordBody(CBody* body) override;
    void GetIndices(CHAR& faceIndex, CHAR& torsoIndex, BYTE& requested) override;
    void SetIndices(CHAR faceIndex, CHAR torsoIndex, BYTE requested) override;
    void GetEmotions(CEmotion& face, CEmotion& torso) override;
    void SetEmotions(CEmotion& face, CEmotion& torso) override;
    CAvatarX* DupAvatar() override;
    BOOL HandleLoadTag(CAvatarStream* stream, AVBINT16 tag, AVBINT16 size,
                       long& resourceAdjustment) override;
    BOOL LoadBodyRecs(CAvatarStream* stream, BOOL oldTag, long& resourceAdjustment);
    BOOL GetPoseFromID(USHORT id, CPose** pose);
    void GetBodyIndexFromEmotion(CEmotion& emotion, int& bodyIndex);
    void SetBodyNeutral(CBodySingle* body);
    void SetBody(CBodySingle* body, int index) { body->m_bodyRec = m_bodies.data() + index; }

    QVector<RBODYREC> m_bodies;
    SHORT m_lastBody = -1;
};

class CAvatarComplex final : public CAvatarX {
public:
    CAvatarComplex() { m_lastFace = m_lastTorso = -1; }
    CAvatarComplex(const CAvatarComplex& other);

    CBody* GetBodyFromEmotion(CEmotion& emotion) override;
    CBody* GetBodyFromEmotion(CEmotionOpts& options) override;
    void DifferentTorso(int torsoIndex) override;
    void SetNeutral() override;
    void SetSequential(void* userInfo, int number) override;
    void RecordBody(CBody* body) override;
    void GetIndices(CHAR& faceIndex, CHAR& torsoIndex, BYTE& requested) override;
    void SetIndices(CHAR faceIndex, CHAR torsoIndex, BYTE requested) override;
    void GetEmotions(CEmotion& face, CEmotion& torso) override;
    void SetEmotions(CEmotion& face, CEmotion& torso) override;
    CAvatarX* DupAvatar() override;
    BOOL HandleLoadTag(CAvatarStream* stream, AVBINT16 tag, AVBINT16 size,
                       long& resourceAdjustment) override;
    BOOL LoadFaceRecs(CAvatarStream* stream, BOOL oldTag, long& resourceAdjustment);
    BOOL LoadTorsoRecs(CAvatarStream* stream, BOOL oldTag, long& resourceAdjustment);
    BOOL GetPosesFromIDs(USHORT headID, USHORT torsoID, CPose** headPose,
                         CPose** torsoPose);
    void GetHeadAndBodyFromEmotion(CEmotion& emotion, int& faceIndex, int& torsoIndex);
    void SetFaceNeutral(CBodyDouble* body);
    void SetTorsoNeutral(CBodyDouble* body);
    void SetFace(CBodyDouble* body, int index) { body->m_faceRec = m_faces.data() + index; }
    void SetTorso(CBodyDouble* body, int index) { body->m_torsoRec = m_torsos.data() + index; }

    QVector<FACEREC> m_faces;
    QVector<BODYREC> m_torsos;
    SHORT m_lastFace = -1;
    SHORT m_lastTorso = -1;
};

void InitializeAvatars();
void DestroyAvatars();
int GetAvatarUpperBound();
CAvatarX* LoadAvatar(const QString& avatarName);
CAvatarX* GetAvatar(USHORT avatarID);
CAvatarX* GetAvatar(const QString& name);
CAvatarX* GetAvatar2(const QString& name);
CAvatarX* GetAvatar3(const QString& name, void* userInfo = nullptr,
                     BOOL randomIfNotFound = TRUE);
CAvatarX* MyAvatar();
unsigned int MyAvatarID();
const char* MyAvatarURL();
void SetMyAvatarID(USHORT id);
void SetMyAvatar(UINT avatarID, BOOL broadcast = TRUE);
BOOL SetMyAvatar(const QString& avatarName, BOOL broadcast = TRUE);
QStringList GetAllAvatarNames();
void ResetAvatarNames();
void GetNextAvatarName(QString& avatarName);
void ResetAvatar(int avatarID);
BOOL NullAvatar();
