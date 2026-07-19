// Ported from v2.5-beta-1-modern/backdrop.cpp. Bundled BMP/BGB files are read
// directly; no converted or substitute image is produced.

#include "backdrop.h"

#include "chat.h"
#include "chatdoc.h"
#include "dib.h"
#include "originalassets.h"
#include "paintdc.h"
#include "vector2d.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPainter>

#include <vector>

namespace {

struct BDFileRec {
    QByteArray filename;
    QByteArray realName;
    QByteArray url;
    unsigned short backID = 0;
    short xdim = 315;
    short ydim = 315;
    short worldLeft = 0;
    short worldTop = 0;
    short worldRight = 4860;
    short worldBottom = -4860;
    short normHeight = 100;
};

std::vector<BDFileRec*> backRecS;
std::vector<BDFileRec*> backRecP;
QHash<unsigned short, CBackDropArt*> backMapS;
QHash<unsigned short, CBackDropArt*> backMapP;
QStringList backdropFiles;
QByteArray currentBackdropName;

BDFileRec* recordAt(UINT id)
{
    if (id >= backRecS.size()) return nullptr;
    return backRecS[id];
}

QString localBackdropFile(const QString& name)
{
    if (name.isEmpty() || QFileInfo(name).fileName() != name) return {};
    return originalFileInDirectoryPath(theApp.GetBackDropDir(), name);
}

} // namespace

QStringList OriginalBackdropNames()
{
    QDir directory(theApp.GetBackDropDir());
    QStringList result;
    // Original order: enumerate every BMP, then every BGB, without sorting.
    const QFileInfoList files = directory.entryInfoList(QDir::Files,
                                                        QDir::NoSort);
    for (const QString& suffix : {QStringLiteral("bmp"), QStringLiteral("bgb")}) {
        for (const QFileInfo& file : files) {
            if (file.suffix().compare(suffix, Qt::CaseInsensitive) == 0)
                result.append(file.fileName());
        }
    }
    return result;
}

CChatBackdrop* LoadBackdropInfo(const QString& backdropName)
{
    const QString path = localBackdropFile(backdropName);
    if (path.isEmpty()) return nullptr;
    CAvatarFileStream stream(path);
    return CChatBackdrop::LoadBackdrop(&stream);
}

int GetAllBackDropNames()
{
    backdropFiles = OriginalBackdropNames();
    return backdropFiles.size();
}

void InitializeBackDrops()
{
    if (backRecS.empty()) {
        backRecS.push_back(nullptr);
        backRecP.push_back(nullptr);
    }
}

int SetBackDropAux(const char* backdropName, const char* realName)
{
    InitializeBackDrops();
    if (!backdropName || !*backdropName) return 0;

    auto* record = new BDFileRec;
    record->filename = QByteArray(backdropName);
    if (realName) record->realName = QByteArray(realName);
    record->backID = static_cast<unsigned short>(backRecS.size());
    backRecS.push_back(record);
    return record->backID;
}

void SetBackDrop(const char* backdropName, const char* /*backdropUrl*/)
{
    if (!backdropName || !*backdropName) return;
    InitializeBackDrops();

    QByteArray requested(backdropName);
    int backdropIndex = -1;
    for (std::size_t i = 1; i < backRecS.size(); ++i) {
        const BDFileRec* record = backRecS[i];
        if (!record) continue;
        const QByteArray visibleName = record->realName.isEmpty()
            ? record->filename : record->realName;
        if (requested.compare(visibleName, Qt::CaseInsensitive) == 0) {
            backdropIndex = static_cast<int>(i);
            break;
        }
    }

    if (backdropIndex == -1) {
        QString fileName = QString::fromLocal8Bit(requested);
        bool exists = false;
        if (!fileName.contains(QLatin1Char('.'))) {
            for (const QString& extension : {QStringLiteral(".bmp"), QStringLiteral(".bgb")}) {
                const QString candidate = fileName + extension;
                if (!localBackdropFile(candidate).isEmpty()) {
                    fileName = candidate;
                    exists = true;
                    break;
                }
            }
        } else {
            exists = !localBackdropFile(fileName).isEmpty();
        }

        if (!exists) {
            // The original downloader boundary is not available yet. Its local
            // failure path retains an existing backdrop, otherwise selects the
            // first directly enumerated original backdrop.
            CChatDoc* document = GetChatDoc();
            if (document && document->GetBackDropID() != 0) return;
            if (GetAllBackDropNames() == 0) return;
            fileName = backdropFiles.first();
        }
        const QByteArray localName = QFile::encodeName(fileName);
        backdropIndex = SetBackDropAux(localName.constData());
    }

    if (CChatDoc* document = GetChatDoc()) document->SetBackDropID(backdropIndex);
    const BDFileRec* record = recordAt(static_cast<UINT>(backdropIndex));
    if (!record) return;
    currentBackdropName = record->realName.isEmpty() ? record->filename : record->realName;
    theApp.m_lastBackDrop = QString::fromLocal8Bit(currentBackdropName);
}

CBackDropArt::~CBackDropArt()
{
    delete m_backdrop;
}

CBackDropArt* BackDropArtFromBackID(UINT backdropId, BOOL /*toScreen*/)
{
    BDFileRec* record = recordAt(backdropId);
    if (!record) return nullptr;

    auto* art = new CBackDropArt;
    art->m_backdrop = LoadBackdropInfo(QString::fromLocal8Bit(record->filename));
    if (!art->m_backdrop) {
        delete art;
        return nullptr;
    }
    art->m_worldCoords.Left = record->worldLeft;
    art->m_worldCoords.Top = record->worldTop;
    art->m_worldCoords.Right = record->worldRight;
    art->m_worldCoords.Bottom = record->worldBottom;
    return art;
}

CBackDropArt* GetBackDropArtFromID(unsigned short backdropId, BOOL toScreen)
{
    if (!backdropId) return nullptr;
    QHash<unsigned short, CBackDropArt*>& cache = toScreen ? backMapS : backMapP;
    const auto found = cache.constFind(backdropId);
    if (found != cache.cend()) return found.value();
    CBackDropArt* art = BackDropArtFromBackID(backdropId, toScreen);
    cache.insert(backdropId, art);
    return art;
}

void FlushBackDropFromID(unsigned short backdropId, BOOL toScreen)
{
    QHash<unsigned short, CBackDropArt*>& cache = toScreen ? backMapS : backMapP;
    delete cache.take(backdropId);
}

void FlushBackDropCache(BOOL useScreenCache)
{
    QHash<unsigned short, CBackDropArt*>& cache = useScreenCache ? backMapS : backMapP;
    qDeleteAll(cache);
    cache.clear();
}

void DestroyBackDropArt()
{
    FlushBackDropCache(TRUE);
    FlushBackDropCache(FALSE);
    for (std::size_t i = 1; i < backRecS.size(); ++i) delete backRecS[i];
    backRecS.resize(backRecS.empty() ? 0 : 1);
    backRecP.resize(backRecP.empty() ? 0 : 1);
}

CBackDrop::CBackDrop()
{
    m_bbox.Left = 0;
    m_bbox.Top = 0;
    m_bbox.Right = 4860;
    m_bbox.Bottom = -4860;
}

void CBackDrop::Draw(QtPaintDC* dc, RECT* panelRect, RECT*)
{
    if (!dc || !dc->surface() || !panelRect) return;
    CBackDropArt* art = GetBackDropArtFromID(m_backID, !dc->isPrinting());
    CDIB* drawing = art && art->m_backdrop ? art->m_backdrop->GetDrawing() : nullptr;
    const QRect destination = dc->mapRect(*panelRect);

    if (!drawing) {
        QPainter painter(dc->surface());
        painter.fillRect(destination, Qt::white);
        return;
    }

    const int panelWidth = panelRect->right - panelRect->left;
    const int panelHeight = panelRect->bottom - panelRect->top;
    if (!panelWidth || !panelHeight) return;
    const int sourceLeft = ROUND((static_cast<double>(m_bbox.Left) / panelWidth)
                                 * drawing->GetWidth());
    const int sourceTop = ROUND((static_cast<double>(m_bbox.Top) / panelHeight)
                                * drawing->GetHeight());
    const int sourceRight = ROUND((static_cast<double>(m_bbox.Right) / panelWidth)
                                  * drawing->GetWidth());
    const int sourceBottom = ROUND((static_cast<double>(m_bbox.Bottom) / panelHeight)
                                   * drawing->GetHeight());
    drawing->Draw(dc->surface(), destination.left(), destination.top(),
                  destination.width(), destination.height(), sourceLeft, sourceTop,
                  sourceRight - sourceLeft, sourceBottom - sourceTop, SRCCOPY);
}

const char* GetCurrentBackDropName()
{
    currentBackdropName = theApp.m_lastBackDrop.toLocal8Bit();
    return currentBackdropName.constData();
}

int GetCurrentBackDropID()
{
    if (theApp.m_lastBackDrop.isEmpty()) return 0;
    const QByteArray name = theApp.m_lastBackDrop.toLocal8Bit();
    for (std::size_t i = 1; i < backRecS.size(); ++i) {
        const BDFileRec* record = backRecS[i];
        if (!record) continue;
        const QByteArray visibleName = record->realName.isEmpty()
            ? record->filename : record->realName;
        if (name.compare(visibleName, Qt::CaseInsensitive) == 0)
            return static_cast<int>(i);
    }
    return 0;
}

const char* GetBackDropNameFromID(UINT backdropId)
{
    BDFileRec* record = recordAt(backdropId);
    if (!record) return nullptr;
    return record->realName.isEmpty() ? record->filename.constData()
                                      : record->realName.constData();
}

const char* GetBackDropURLFromID(UINT backdropId)
{
    BDFileRec* record = recordAt(backdropId);
    if (!record || !record->realName.isEmpty()) return nullptr;
    CBackDropArt* art = BackDropArtFromBackID(backdropId, TRUE);
    if (!art || !art->m_backdrop) {
        delete art;
        return nullptr;
    }
    record->url = art->m_backdrop->Url();
    delete art;
    return record->url.isEmpty() ? nullptr : record->url.constData();
}
