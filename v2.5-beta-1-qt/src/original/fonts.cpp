// Ported from v2.5-beta-1-modern/fonts.cpp. QFont replaces LOGFONT/CFont;
// resource-selected family, logical heights and leading adjustments remain.

#include "panel.h"

#include "chat.h"
#include "defines.h"

#include <QFontInfo>

#include <algorithm>

QFont* CUnitPanelPage::m_fontBalloon = nullptr;
QFont* CUnitPanelPage::m_fontWhisper = nullptr;
QFont* CUnitPanelPage::m_fontTitle = nullptr;
QFont* CUnitPanelPage::m_fontShout = nullptr;

CFontInfo* CUnitPanelPage::m_fiWNormal = nullptr;
CFontInfo* CUnitPanelPage::m_fiWWhisper = nullptr;
CFontInfo* CUnitPanelPage::m_fiTitle = nullptr;
CFontInfo* CUnitPanelPage::m_fiShout = nullptr;

QList<QFont*> CUnitPanelPage::m_fonts;
QList<CFontInfo*> CUnitPanelPage::m_fontInfos;

BOOL CUnitPanelPage::SetFonts(const QFont& sourceFont, COLORREF textColor)
{
    m_fontBalloon = new QFont(sourceFont);
    m_fonts.prepend(m_fontBalloon);

    theApp.m_comicsFont = *m_fontBalloon;
    const QString physicalFaceName = QFontInfo(*m_fontBalloon).family();

    m_fontWhisper = new QFont(sourceFont);
    m_fonts.prepend(m_fontWhisper);
    // The startup font follows the original Western/default-character-set
    // branch. Locale-specific charset replacement remains a documented MFC
    // boundary until GetCorrectCharSet has a Qt counterpart.
    m_fontWhisper->setItalic(true);

    const double reduction = qAbs(sourceFont.pixelSize()) / 180.0;
    const int doVerticalKern = physicalFaceName == QLatin1String("Comic Sans MS") ? 1 : 0;
    m_fiWNormal = new CFontInfo(m_fontBalloon, textColor,
        static_cast<int>(-40 * reduction * doVerticalKern),
        static_cast<int>(30 * reduction * doVerticalKern));
    m_fontInfos.prepend(m_fiWNormal);
    m_fiWWhisper = new CFontInfo(m_fontWhisper, textColor,
        static_cast<int>(-40 * reduction * doVerticalKern),
        static_cast<int>(30 * reduction * doVerticalKern));
    m_fontInfos.prepend(m_fiWWhisper);

    return UpdateTitleFonts();
}

BOOL CUnitPanelPage::UpdateTitleFonts()
{
    if (!m_fiWNormal) return FALSE;

    const float reduction = static_cast<float>(m_unitWidth) / 4860.0F;
    int fontHeight = static_cast<int>(nFontHeightTitle * reduction);
    fontHeight = std::min(fontHeight,
                          static_cast<int>(1.2 * theApp.m_iFontHeightBalloon));

    m_fontTitle = new QFont(theApp.m_comicsFont);
    m_fontTitle->setPixelSize(qAbs(fontHeight));
    m_fonts.prepend(m_fontTitle);
    const int doVerticalKern = QFontInfo(*m_fontTitle).family()
        == QLatin1String("Comic Sans MS") ? 1 : 0;

    fontHeight = static_cast<int>(nFontHeightShout * reduction);
    fontHeight = std::min(fontHeight, theApp.m_iFontHeightBalloon);
    m_fontShout = new QFont(theApp.m_comicsFont);
    m_fontShout->setPixelSize(qAbs(fontHeight));
    m_fonts.prepend(m_fontShout);

    m_fiTitle = new CFontInfo(m_fontTitle, theApp.m_comicsColor,
        static_cast<int>(-220 * reduction * doVerticalKern),
        static_cast<int>(120 * reduction * doVerticalKern));
    m_fiShout = new CFontInfo(m_fontShout, theApp.m_comicsColor, 0, 0);
    m_fontInfos.prepend(m_fiTitle);
    m_fontInfos.prepend(m_fiShout);
    return TRUE;
}

void CUnitPanelPage::DestroyFonts()
{
    m_fontBalloon = nullptr;
    m_fontWhisper = nullptr;
    m_fontTitle = nullptr;
    m_fontShout = nullptr;
    m_fiWNormal = nullptr;
    m_fiWWhisper = nullptr;
    m_fiTitle = nullptr;
    m_fiShout = nullptr;

    qDeleteAll(m_fonts);
    m_fonts.clear();
    qDeleteAll(m_fontInfos);
    m_fontInfos.clear();
}

void CUnitPanelPage::SetUnitPanelWidth(int width)
{
    m_unitWidth = width;
    UpdateTitleFonts();
}
