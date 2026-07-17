// Ported from v2.5-beta-1-modern/backdrop.h. Qt replaces only the drawing,
// filesystem and cache containers; backdrop IDs and panel geometry are the
// original interfaces.

#pragma once

#include "avbfile.h"
#include "pe.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

class QtPaintDC;

class CChatBackdrop {
public:
    CChatBackdrop() = default;
    virtual ~CChatBackdrop() { delete m_pDIB; }

    static CChatBackdrop* LoadBackdrop(CAvatarStream* stream);
    virtual BOOL Load(CAvatarStream* stream);
    BOOL LoadFromBmp(CAvatarStream* stream);

    CAvatarDIB* GetDrawing() const { return m_pDIB; }
    const char* Url() const
    {
        return m_newUrl.isEmpty() ? m_originalUrl.constData() : m_newUrl.constData();
    }
    const char* Copyright() const { return m_copyright.constData(); }

    CAvatarDIB* m_pDIB = nullptr;
    QByteArray m_originalUrl;
    QByteArray m_newUrl;
    QByteArray m_copyright;
};

constexpr UCHAR BF_NOZOOM = 1;

class CBackDropArt {
public:
    CBackDropArt() = default;
    ~CBackDropArt();

    CChatBackdrop* m_backdrop = nullptr;
    SRECT m_worldCoords{};
};

class CBackDrop final : public CPanelElement {
public:
    CBackDrop();

    unsigned short m_backID = 0;
    UCHAR m_mode = 0;

    void Draw(QtPaintDC* dc, RECT* panelBox, RECT* damageBox);
    void Draw(QtPaintDC*, POINT*, RECT*) override {}
};

QStringList OriginalBackdropNames();
CChatBackdrop* LoadBackdropInfo(const QString& backdropName);
int GetAllBackDropNames();
int SetBackDropAux(const char* backdropName, const char* realName = nullptr);
void SetBackDrop(const char* backdropName, const char* backdropUrl = nullptr);
void InitializeBackDrops();
void DestroyBackDropArt();
CBackDropArt* BackDropArtFromBackID(UINT backdropId, BOOL toScreen);
CBackDropArt* GetBackDropArtFromID(unsigned short backdropId, BOOL toScreen);
void FlushBackDropFromID(unsigned short backdropId, BOOL toScreen = TRUE);
void FlushBackDropCache(BOOL useScreenCache = TRUE);
const char* GetCurrentBackDropName();
int GetCurrentBackDropID();
const char* GetBackDropNameFromID(UINT backdropId);
const char* GetBackDropURLFromID(UINT backdropId);
