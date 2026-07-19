// Ported from v2.5-beta-1-modern/balloon.cpp. The Woodring geometry, line
// layout, tail routing and formatting offsets are retained. QPainter and QFont
// are the replacement boundary for CDC/CFont only.

#include "balloon.h"

#include "avatar.h"
#include "chat.h"
#include "ccommon.h"
#include "intl.h"
#include "panel.h"
#include "paintdc.h"
#include "spline.h"
#include "traj.h"
#include "vector2d.h"

#include <QByteArray>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int XBOXDELTA = 90;
constexpr int YBOXDELTA = 50;
constexpr int MINROUTEWIDTH = 300;
constexpr int BUBBLEHEIGHT = 150;
constexpr int INTERBUBBLE = 100;
constexpr int ENDBUBBLEWIDTH = 400;
constexpr int VWAVEHEIGHT = 70;
constexpr int VWAVEINTERVAL = 300;
constexpr int HWAVEHEIGHT = 70;
constexpr int HWAVEINTERVAL = 300;
constexpr int MAXPTS = 150;
constexpr int THRESH1 = -70;
constexpr int THRESH2 = 70;
constexpr int XBORDER = 100;
constexpr int YBORDER = 40;
constexpr int TOPBORDER = -20;
constexpr int MAXLEFTSHIFT = 0;
constexpr int MAXCENTERSHIFT = 0;
constexpr int LARGEDELTA = 350;
constexpr int SMALLDELTA = 150;
constexpr int MINTAILHEIGHT = 100;
constexpr int BORDERFUDGE = 400;
constexpr int FAREAST_TOPOFFSET = 50;
constexpr const char* continuation1 = "...";
constexpr const char* continuation2 = "...";
POINT mouseDownPt{};

struct RANGE {
    int start;
    int end;
    int x;
    int y;
};

QColor qColor(COLORREF color)
{
    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
}

bool isSpace(char value)
{
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

bool isPrintable(char value)
{
    if (GetMime() && static_cast<unsigned char>(value) >= 0x80U) return true;
    return std::isprint(static_cast<unsigned char>(value)) != 0;
}

char* nextChar(char* position)
{
    if (!position || !*position) return position;
    return position + (GetMime()
        ? iBytesofChar(static_cast<BYTE>(*position)) : 1);
}

void drawUprightText(QPainter& painter, int x, int top, const QFont& font,
                     const QColor& color, const char* bytes, int length)
{
    if (length <= 0) return;
    painter.save();
    painter.setFont(font);
    painter.setPen(color);
    const QFontMetrics metrics(font);
    painter.translate(x, top - metrics.ascent());
    painter.scale(1.0, -1.0);
    painter.drawText(QPointF(0, 0), IntlTextToQString(bytes, length));
    painter.restore();
}

QFont formattedFont(const QFont& base, WORD format)
{
    QFont result = base;
    result.setBold(format & wBold);
    result.setItalic(format & wItalic);
    result.setUnderline(format & wUnderline);
    const QStringList families = QFontDatabase::families();
    const auto firstInstalled = [&families](const char* const* names, int count) {
        for (int index = 0; index < count; ++index) {
            const QString wanted = QString::fromLatin1(names[index]);
            for (const QString& family : families) {
                if (family.compare(wanted, Qt::CaseInsensitive) == 0) return family;
            }
        }
        return QString();
    };
    static const QString fixed = firstInstalled(FIXEDPITCHFACENAMES, FIXEDPITCHNUMBER);
    static const QString symbol = firstInstalled(SYMBOLFACENAMES, SYMBOLNUMBER);
    if ((format & wFixedPitch) && !fixed.isEmpty()) {
        result.setFamily(fixed);
        result.setStyleHint(QFont::TypeWriter);
    } else if ((format & wSymbol) && !symbol.isEmpty()) {
        result.setFamily(symbol);
    }
    return result;
}

QSize measure(CFontInfo* fontInfo, const char* text, int length,
              CDWordArray* formatting)
{
    QImage scratch(1, 1, QImage::Format_RGB32);
    QPainter painter(&scratch);
    painter.setFont(*fontInfo->m_font);
    return GetFormattedTextExtent(&painter, text, static_cast<DWORD>(length), formatting);
}

char* GetNextStart(char* string)
{
    while (*string && isSpace(*string)) string = nextChar(string);
    return string;
}

char* GetNextEnd(char* string)
{
    while (*string && isSpace(*string)) string = nextChar(string);
    while (*string && !isSpace(*string)) string = nextChar(string);
    return string;
}

BOOL UpcomingReturn(char* string)
{
    while (*string && isSpace(*string)) {
        if (*string == '\n') return TRUE;
        string = nextChar(string);
    }
    return FALSE;
}

void ForceLineBreak(CFontInfo* fontInfo, char* string, CDWordArray* formatting,
                    int maximumWidth, int& length, int& width, int& height)
{
    width = height = length = 0;
    while (TRUE) {
        const int lastCharacterWidth = GetMime()
            ? iBytesofChar(static_cast<BYTE>(string[length])) : 1;
        length += lastCharacterWidth;
        const QSize extent = measure(fontInfo, string, length, formatting);
        if (string[length] && extent.width() <= maximumWidth) {
            width = extent.width();
            height = std::max(height, extent.height());
        } else {
            length -= lastCharacterWidth;
            return;
        }
    }
}

char* FindFurthestLineBreakIntl(CFontInfo* fontInfo, int maximumWidth,
                                char* string, CDWordArray* formatting,
                                int& length, int& width, int& height)
{
    const int bytes = static_cast<int>(std::strlen(string));
    const QSize fullExtent = measure(fontInfo, string, bytes, formatting);
    if (fullExtent.width() <= maximumWidth) {
        width = fullExtent.width();
        height = fullExtent.height();
        length = bytes;
        return string + bytes;
    }

    QImage scratch(1, 1, QImage::Format_RGB32);
    QPainter painter(&scratch);
    painter.setFont(*fontInfo->m_font);
    BOOL hasBlankOrAlike = FALSE;
    QSize extent;
    if (!FindSubStringForINTLThatFits(
            GetMime(), &painter, string, bytes, formatting, &length,
            &hasBlankOrAlike, &extent, maximumWidth)) {
        ForceLineBreak(fontInfo, string, formatting, maximumWidth,
                       length, width, height);
        return string + length;
    }

    char* position = string;
    char* firstBreak = nullptr;
    while (position < string + length) {
        if (isSpace(*position)) {
            if (!firstBreak) firstBreak = position;
        } else {
            firstBreak = nullptr;
        }
        position = nextChar(position);
    }
    if (!firstBreak) {
        width = extent.width();
        height = extent.height();
        return string + length;
    }

    const QSize trimmed = measure(
        fontInfo, string, static_cast<int>(firstBreak - string), formatting);
    width = trimmed.width();
    height = trimmed.height();
    length = static_cast<int>(firstBreak - string);
    return firstBreak;
}

char* FindFurthestLineBreak(CFontInfo* fontInfo, int maximumWidth, char* string,
                            CDWordArray* formatting, int& width, int& height)
{
    if (GetMime()) {
        int length = 0;
        return FindFurthestLineBreakIntl(fontInfo, maximumWidth, string,
                                         formatting, length, width, height);
    }
    char* lastEnd;
    char* lineEnd = string;
    while (TRUE) {
        lastEnd = lineEnd;
        lineEnd = GetNextEnd(lineEnd);
        const int length = static_cast<int>(lineEnd - string);
        const QSize extent = measure(fontInfo, string, length, formatting);
        if (extent.width() <= maximumWidth) {
            width = extent.width();
            height = extent.height();
            if (!*lineEnd) return lineEnd;
        } else {
            if (lastEnd == string) {
                int forcedLength;
                ForceLineBreak(fontInfo, string, formatting, maximumWidth,
                               forcedLength, width, height);
                lastEnd = string + forcedLength;
            }
            return lastEnd;
        }
    }
}

int BreakIntoLines(CFontInfo* fontInfo, int maximumWidth, char* string,
                   CDWordArray* formatting, char* starts[], int lengths[],
                   int widths[])
{
    int lines = 0;
    int thisLength = 0;
    int lastLength = 0;
    int lastWidth = 0;
    int lastHeight = 0;
    char* lineEnd = string;
    char* originalStart = string;
    while (TRUE) {
        lineEnd = GetNextEnd(lineEnd);
        lastLength = thisLength;
        thisLength = static_cast<int>(lineEnd - string);
        CDWordArray* pulled = PullFormattingOffsets(
            formatting, static_cast<SHORT>(string - originalStart));
        const QSize extent = measure(fontInfo, string, thisLength, pulled);
        FreeAndNullFormatting(&pulled);
        const BOOL foundReturn = UpcomingReturn(lineEnd);
        if (extent.width() <= maximumWidth && !foundReturn) {
            if (!*lineEnd) {
                starts[lines] = string;
                lengths[lines] = thisLength;
                widths[lines++] = extent.width();
                return lines;
            }
            lastWidth = extent.width();
            lastHeight = extent.height();
            continue;
        }

        if (lastLength == 0 && extent.width() > maximumWidth) {
            pulled = PullFormattingOffsets(formatting,
                static_cast<SHORT>(string - originalStart));
            if (GetMime()) {
                FindFurthestLineBreakIntl(
                    fontInfo, maximumWidth, string, pulled, lastLength,
                    lastWidth, lastHeight);
            } else {
                ForceLineBreak(fontInfo, string, pulled, maximumWidth,
                               lastLength, lastWidth, lastHeight);
            }
            FreeAndNullFormatting(&pulled);
        } else if (foundReturn && extent.width() <= maximumWidth) {
            lastLength = thisLength;
            lastWidth = extent.width();
            lastHeight = extent.height();
        }
        starts[lines] = string;
        lengths[lines] = lastLength;
        widths[lines++] = lastWidth;
        string = lineEnd = GetNextStart(string + lastLength);
        if (!*string || lines >= MAXLINES) return lines;
        thisLength = 0;
    }
}

void BreakSpline(CSpline* spline, int x, int y, double openingFactor)
{
    POINT left, leftNearest, rightNearest;
    int leftKnotIndex, rightKnotIndex;
    const int oldCount = spline->nCps;
    const int gapWidth = static_cast<int>(80 * openingFactor);
    left.x = x - gapWidth;
    left.y = y;
    leftNearest = spline->ClosestPoint(left, &leftKnotIndex);
    rightNearest = spline->WalkHorizontalDistance(
        leftNearest, leftKnotIndex, leftNearest.x + 2 * gapWidth, rightKnotIndex);
    auto* newPoints = new POINT[oldCount + 2];
    newPoints[0] = rightNearest;
    for (int index = 1; index <= oldCount; ++index) {
        newPoints[index] = spline->cps[(rightKnotIndex + index - 2 + oldCount) % oldCount];
    }
    const int newCount = oldCount + 2
        - (rightKnotIndex - leftKnotIndex + oldCount) % oldCount;
    newPoints[newCount - 1] = leftNearest;
    delete[] spline->cps;
    spline->cps = newPoints;
    spline->nCps = newCount;
    delete[] spline->bezpts;
    spline->bezpts = nullptr;
    spline->closed = FALSE;
    spline->ComputeBezpts();
}

void GetFilters(CFormatInfo& info, RANGE left[], RANGE right[], int& leftCount,
                int& rightCount)
{
    leftCount = rightCount = 0;
    left[0].x = info.m_rgiLeftX[0];
    right[0].x = info.m_rgiLeftX[0] + info.m_rgiWidths[0];
    left[0].start = right[0].start = 0;
    for (int index = 1; index < info.m_nLines; ++index) {
        const int thisLeft = info.m_rgiLeftX[index];
        const int thisRight = thisLeft + info.m_rgiWidths[index];
        const int leftDelta = thisLeft - left[leftCount].x;
        const int rightDelta = thisRight - right[rightCount].x;
        if (leftDelta <= THRESH1) {
            left[leftCount].end = index - 1;
            left[++leftCount].start = index;
            left[leftCount].x = thisLeft;
        } else if (leftDelta <= 0) {
            left[leftCount].x = thisLeft;
        } else if (leftDelta >= THRESH2) {
            const int nextLeft = index + 1 < info.m_nLines
                ? info.m_rgiLeftX[index + 1] : thisLeft;
            if (nextLeft - left[leftCount].x >= THRESH2) {
                left[leftCount].end = index - 1;
                left[++leftCount].start = index;
                left[leftCount].x = std::min(thisLeft, nextLeft);
            }
        }
        if (rightDelta >= -THRESH1) {
            right[rightCount].end = index - 1;
            right[++rightCount].start = index;
            right[rightCount].x = thisRight;
        } else if (rightDelta >= 0) {
            right[rightCount].x = thisRight;
        } else if (rightDelta <= -THRESH2) {
            const int nextRight = index + 1 < info.m_nLines
                ? info.m_rgiLeftX[index + 1] + info.m_rgiWidths[index + 1]
                : thisRight;
            if (nextRight - right[rightCount].x <= -THRESH2) {
                right[rightCount].end = index - 1;
                right[++rightCount].start = index;
                right[rightCount].x = std::max(thisRight, nextRight);
            }
        }
    }
    left[leftCount++].end = right[rightCount++].end = info.m_nLines - 1;
}

int PermuteFilters(CFontInfo& fontInfo, RANGE left[], RANGE right[],
                   int leftCount, int rightCount)
{
    int baseY = 0;
    int lastX = LARGEINTEGER;
    for (int index = 0; index < leftCount; ++index) {
        left[index].x -= XBORDER;
        if (index == 0) {
            left[index].y = baseY + TOPBORDER + YBORDER + fontInfo.m_topOffset;
        } else if (left[index].x < lastX) {
            left[index].y = baseY + YBORDER;
        } else {
            left[index].y = baseY - YBORDER - fontInfo.m_baseAdd;
        }
        baseY -= (left[index].end - left[index].start + 1) * fontInfo.m_lineHeight;
        lastX = left[index].x;
    }
    baseY = 0;
    lastX = -LARGEINTEGER;
    for (int index = 0; index < rightCount; ++index) {
        right[index].x += XBORDER;
        if (index == 0) {
            right[index].y = baseY + TOPBORDER + YBORDER + fontInfo.m_topOffset;
        } else if (right[index].x > lastX) {
            right[index].y = baseY + YBORDER;
        } else {
            right[index].y = baseY - YBORDER - fontInfo.m_baseAdd;
        }
        baseY -= (right[index].end - right[index].start + 1) * fontInfo.m_lineHeight;
        lastX = right[index].x;
    }
    return baseY - TOPBORDER - YBORDER - fontInfo.m_baseAdd;
}

void AddWavies(POINT& first, POINT& second, POINT* points, int& count,
               int waveDiameter, int interval)
{
    const double distance = point_dist(first, second);
    const double waves = distance / interval;
    if (waves < 2) return;
    const int waveCount = static_cast<int>(waves);
    const double waveLength = distance / waveCount;
    const DPOINT unit = point_scalmult(
        1.0 / distance,
        point_sub(point_to_dpoint(second), point_to_dpoint(first)));
    const POINT increment = dpoint_to_point(point_scalmult(waveLength, unit));
    const DPOINT normal{unit.y, -unit.x};
    const POINT extra = dpoint_to_point(point_scalmult(waveDiameter, normal));
    POINT base = first;
    for (int index = 0; index < waveCount - 1; ++index) {
        base = point_add(base, increment);
        points[count++] = !(index & 1) ? point_add(base, extra) : base;
    }
}
}

double randfloat()
{
    return static_cast<double>(std::rand()) / RAND_MAX;
}

void Capitalize(char* string)
{
    if (!string || !*string) return;
    const int length = static_cast<int>(std::strlen(string));
    QByteArray source(string, length);
    BYTE characterSet = theApp.m_charSet;

    if (characterSet == GREEK_CHARSET) {
        for (char& value : source) {
            UCHAR byte = static_cast<UCHAR>(value);
            if (byte == 220 || byte == 162) byte = 193;
            else if (byte == 221 || byte == 184) byte = 197;
            else if (byte == 222 || byte == 185) byte = 199;
            else if (byte == 223 || byte == 186) byte = 201;
            else if (byte == 252 || byte == 188) byte = 207;
            else if (byte == 253 || byte == 190) byte = 213;
            else if (byte == 254 || byte == 191) byte = 217;
            else if (byte == 242) byte = 211;
            else if (byte == 192) byte = 218;
            else if (byte == 224) byte = 219;
            value = static_cast<char>(byte);
        }
    } else if (characterSet == TURKISH_CHARSET) {
        for (char& value : source) {
            const UCHAR byte = static_cast<UCHAR>(value);
            if (byte == 253) value = static_cast<char>(73);
            else if (byte == 105) value = static_cast<char>(221);
        }
    }

    QString decoded;
    QByteArray upper;
    if (characterSet == GREEK_CHARSET || characterSet == RUSSIAN_CHARSET
        || characterSet == TURKISH_CHARSET) {
        if (!bCharacterSetToWide(source, characterSet, &decoded)
            || !bWideToCharacterSet(QStringView(decoded.toUpper()),
                                    characterSet, &upper)) {
            return;
        }
    } else {
        upper = IntlTextFromQString(QStringView(
            IntlTextToQString(source.constData(), source.size()).toUpper()));
    }
    if (upper.size() == length) std::memcpy(string, upper.constData(), length);
}

CFontInfo::CFontInfo(QFont* font, COLORREF defaultForeground, short leading,
                     short baseAdd)
    : m_font(font)
    , m_crDefaultForeColor(defaultForeground)
{
    const QFontMetrics metrics(*font);
    m_leading = static_cast<short>(leading + metrics.leading());
    m_baseAdd = static_cast<short>(baseAdd - metrics.leading());
    m_topOffset = leading ? 0 : FAREAST_TOPOFFSET;
    m_lineHeight = static_cast<short>(metrics.height() + m_leading);
    m_continuationWidth = static_cast<short>(metrics.horizontalAdvance(continuation1));
}

CArrow::CArrow(const CArrow& source)
    : m_lo(source.m_lo), m_hi(source.m_hi), m_mid(source.m_mid)
{
}

CPanelElement::CPanelElement(const CPanelElement& source)
    : m_bbox(source.m_bbox)
{
}

void CPanelElement::GetBBox(RECT* result)
{
    result->left = m_bbox.Left;
    result->top = m_bbox.Top;
    result->right = m_bbox.Right;
    result->bottom = m_bbox.Bottom;
}

CLabel::CLabel(const char* text, CFontInfo* fontInfo, CDWordArray* formatting)
    : m_fontI(fontInfo)
    , m_str(::strdup(text ? text : ""))
    , m_prgdwFormatting((theApp.m_flags1 & F1_RTFCOMIC)
          ? CopyFormatting(formatting) : CopyLinksFormatting(formatting))
{
}

CLabel::CLabel(const CLabel& source)
    : CPanelElement(source)
    , m_fontI(source.m_fontI)
    , m_str(source.m_str ? ::strdup(source.m_str) : nullptr)
    , m_format(source.m_format)
    , m_prgdwFormatting(CopyFormatting(source.m_prgdwFormatting))
{
}

CLabel::~CLabel()
{
    std::free(m_str);
    FreeAndNullFormatting(&m_prgdwFormatting);
}

int CLabel::BreakIntoLines(CFormatInfo& info)
{
    const int desiredWidth = m_bbox.Right - m_bbox.Left;
    info.m_nLines = static_cast<UCHAR>(::BreakIntoLines(
        m_fontI, desiredWidth, m_str, m_prgdwFormatting,
        info.m_rgszStarts, info.m_rgiLengths, info.m_rgiWidths));
    info.m_iMaxWidth = 0;
    for (int index = 0; index < info.m_nLines; ++index) {
        info.m_iMaxWidth = std::max(info.m_iMaxWidth, info.m_rgiWidths[index]);
    }
    info.m_bbox.Top = m_bbox.Top;
    if (m_format & FT_LEFT_JUSTIFY) {
        info.m_bbox.Left = m_bbox.Left;
        info.m_bbox.Right = static_cast<SHORT>(m_bbox.Left + info.m_iMaxWidth);
    } else {
        info.m_bbox.Left = static_cast<SHORT>(
            (desiredWidth - info.m_iMaxWidth) / 2 + m_bbox.Left);
        info.m_bbox.Right = static_cast<SHORT>(info.m_bbox.Left + info.m_iMaxWidth);
    }
    info.m_bbox.Bottom = static_cast<SHORT>(
        info.m_bbox.Top - info.m_nLines * m_fontI->m_lineHeight - m_fontI->m_baseAdd);
    return info.m_nLines;
}

int CLabel::AreaEstimate(int* length, int* lineHeight)
{
    const QSize extent = measure(m_fontI, m_str, std::strlen(m_str), m_prgdwFormatting);
    *length = extent.width();
    *lineHeight = m_fontI->m_lineHeight;
    return static_cast<int>(1.3 * extent.width() * (extent.height() + *lineHeight));
}

int CLabel::WidestWord()
{
    int maximum = 0;
    char* start = m_str;
    while (TRUE) {
        while (*start && !isPrintable(*start)) start = nextChar(start);
        if (!*start) break;
        char* end = start;
        while (*end && isPrintable(*end)) end = nextChar(end);
        CDWordArray* pulled = PullFormattingOffsets(
            m_prgdwFormatting, static_cast<SHORT>(start - m_str));
        maximum = std::max(maximum,
            measure(m_fontI, start, static_cast<int>(end - start + 1), pulled).width());
        FreeAndNullFormatting(&pulled);
        if (!*end) break;
        start = nextChar(end);
    }
    return maximum;
}

void CLabel::ShiftLines(CFormatInfo& info)
{
    if (m_format & FT_LEFT_JUSTIFY) {
        for (int index = 0; index < info.m_nLines; ++index) {
            const int limit = info.m_iMaxWidth - info.m_rgiWidths[index];
            info.m_rgiLeftX[index] = static_cast<int>(
                randfloat() * std::min(MAXLEFTSHIFT, limit));
        }
    } else {
        for (int index = 0; index < info.m_nLines; ++index) {
            const int limit = (info.m_iMaxWidth - info.m_rgiWidths[index]) / 2;
            const int shift = static_cast<int>((randfloat() * 2.0 - 1.0)
                * std::min(MAXCENTERSHIFT, limit));
            info.m_rgiLeftX[index] = (info.m_bbox.Right - info.m_bbox.Left
                - info.m_rgiWidths[index]) / 2 + shift;
        }
    }
}

char* CLabel::SplitHeight(int height, CDWordArray** restFormatting,
                          char** urlStartInRest)
{
    if (restFormatting) *restFormatting = nullptr;
    if (urlStartInRest) *urlStartInRest = nullptr;
    char* starts[MAXLINES];
    int lengths[MAXLINES], widths[MAXLINES];
    const int desiredWidth = m_bbox.Right - m_bbox.Left;
    const int lines = ::BreakIntoLines(m_fontI, desiredWidth, m_str,
        m_prgdwFormatting, starts, lengths, widths);
    const int maximumLines = height / m_fontI->m_lineHeight;
    if (lines < maximumLines || maximumLines < 1) return nullptr;
    CDWordArray* pulled = PullFormattingOffsets(m_prgdwFormatting,
        static_cast<SHORT>(starts[maximumLines - 1] - m_str));
    int dummyWidth = 0, dummyHeight = 0;
    char* end = FindFurthestLineBreak(m_fontI,
        desiredWidth - m_fontI->m_continuationWidth,
        starts[maximumLines - 1], pulled, dummyWidth, dummyHeight);
    FreeAndNullFormatting(&pulled);
    const int copyLength = static_cast<int>(end - m_str);
    auto* newText = static_cast<char*>(std::malloc(
        copyLength + std::strlen(continuation1) + 1));
    std::memcpy(newText, m_str, copyLength);
    std::strcpy(newText + copyLength, continuation1);
    char* restStart = m_str + copyLength;
    auto* rest = static_cast<char*>(std::malloc(
        std::strlen(restStart) + std::strlen(continuation2) + 1));
    std::strcpy(rest, continuation2);
    std::strcat(rest, restStart);
    if (restFormatting) {
        *restFormatting = PullFormattingOffsets(
            m_prgdwFormatting, static_cast<SHORT>(restStart - m_str));
    }
    std::free(m_str);
    m_str = newText;
    if (m_prgdwFormatting) {
        m_prgdwFormatting = CutFormattingArray(m_prgdwFormatting,
                                               static_cast<SHORT>(copyLength));
        if (m_prgdwFormatting) {
            m_prgdwFormatting->Add(MAKELONG(0, static_cast<WORD>(copyLength)));
        }
    }
    if (restFormatting) {
        PushFormattingOffsets(*restFormatting,
                              static_cast<SHORT>(std::strlen(continuation2)));
    }
    return rest;
}

void CLabel::GetFormatInfoCommon(CFormatInfo* info)
{
    info->m_bbox = m_bbox;
    const int desiredWidth = m_bbox.Right - m_bbox.Left;
    info->m_nLines = static_cast<UCHAR>(::BreakIntoLines(
        m_fontI, desiredWidth, m_str, m_prgdwFormatting,
        info->m_rgszStarts, info->m_rgiLengths, info->m_rgiWidths));
    for (int index = 0; index < info->m_nLines; ++index) {
        info->m_rgiLeftX[index] = (m_format & FT_LEFT_JUSTIFY)
            ? m_bbox.Left
            : m_bbox.Left + (CUnitPanelPage::m_unitWidth - info->m_rgiWidths[index]) / 2;
    }
}

int CLabel::iDrawFormattedTextLine(QPainter& painter, int leftX, int baseY,
                                  const char* chunk, int chunkLength,
                                  WORD format, BOOL* urlHit)
{
    const QFont font = formattedFont(*m_fontI->m_font, format);
    const QFontMetrics metrics(font);
    const QSize size(metrics.horizontalAdvance(
                         IntlTextToQString(chunk, chunkLength)),
                     metrics.height());
    BOOL transparent = FALSE;
    QColor color = qColor(m_fontI->m_crDefaultForeColor);
    if (format & wLink) {
        color = qColor(linkColor);
    } else if (format & wForeground) {
        const COLORREF foreground = GetRBGColor((format >> 4) & 0x000f);
        if (foreground != RGB(255, 255, 255)) color = qColor(foreground);
        transparent = (format & wBackground)
            && (((format >> 4) & 0x000f) == (format & 0x000f));
    }
    if (urlHit && *urlHit) {
        *urlHit = (format & wLink) && bURLHit(leftX, baseY, size);
    } else if (!transparent) {
        drawUprightText(painter, leftX,
            baseY - (m_fontI->m_lineHeight - metrics.height()) / 2,
            font, color, chunk, chunkLength);
    }
    return size.width();
}

BOOL CLabel::bURLHit(int leftX, int baseY, const QSize& size)
{
    RECT rectangle{};
    rectangle.left = m_bbox.Left + leftX;
    rectangle.top = m_bbox.Top + baseY;
    rectangle.right = rectangle.left + size.width();
    rectangle.bottom = rectangle.top - size.height();
    return inside_bbox(&mouseDownPt, &rectangle);
}

void CLabel::DrawFormattedText(QPainter& painter, const CFormatInfo& info,
                               int startTop, int* hitUrl)
{
    if (hitUrl) *hitUrl = -1;
    if (!m_prgdwFormatting || m_prgdwFormatting->GetSize() < 1
        || info.m_nLines < 1) {
        return;
    }

    char* chunkStart = m_str;
    int chunkLength = 0;
    int leftX = info.m_rgiLeftX[0];
    const int stringLength = static_cast<int>(std::strlen(m_str));
    int baseY = startTop;
    int urlIndex = -1;
    int currentInLine = 0;
    int currentLine = 0;
    BOOL inUrl = FALSE;
    BOOL urlHit = hitUrl != nullptr;
    const int formattingUpper = m_prgdwFormatting->GetUpperBound();

    DWORD element = m_prgdwFormatting->GetAt(0);
    WORD offset = HIWORD(element);

    // The original walks the leading unformatted range before iterating the
    // formatting boundaries. Keep that ordering: line wrapping can skip a
    // blank even though its formatting transition still has to be observed.
    if (offset > 0) {
        while (currentLine < info.m_nLines
               && info.m_rgszStarts[currentLine] - m_str + currentInLine
                      < offset) {
            chunkStart = info.m_rgszStarts[currentLine];
            chunkLength = std::min<int>(
                offset + m_str - info.m_rgszStarts[currentLine],
                info.m_rgiLengths[currentLine]);
            leftX += iDrawFormattedTextLine(painter, leftX, baseY,
                                            chunkStart, chunkLength, 0,
                                            &urlHit);
            if (chunkLength == info.m_rgiLengths[currentLine]) {
                ++currentLine;
                currentInLine = 0;
                if (currentLine < info.m_nLines)
                    leftX = info.m_rgiLeftX[currentLine];
                baseY -= m_fontI->m_lineHeight;
            } else {
                currentInLine = chunkLength;
            }
        }
    }

    for (int formattingIndex = 0;
         formattingIndex <= formattingUpper; ++formattingIndex) {
        element = m_prgdwFormatting->GetAt(formattingIndex);
        const WORD format = LOWORD(element);
        offset = HIWORD(element);

        if ((format & wLink) && !inUrl) ++urlIndex;
        inUrl = (format & wLink) != 0;

        const int formatLength = formattingIndex < formattingUpper
            ? HIWORD(m_prgdwFormatting->GetAt(formattingIndex + 1)) - offset
            : stringLength - offset;
        const char* formatEnd = m_str + offset + formatLength;

        while (currentLine < info.m_nLines
               && chunkStart + chunkLength < formatEnd) {
            chunkStart = info.m_rgszStarts[currentLine] + currentInLine;
            chunkLength = std::min<int>(
                formatEnd - chunkStart,
                info.m_rgiLengths[currentLine] - currentInLine);
            if (chunkLength > 0) {
                urlHit = hitUrl != nullptr;
                leftX += iDrawFormattedTextLine(
                    painter, leftX, baseY, chunkStart, chunkLength, format,
                    &urlHit);
                currentInLine += chunkLength;
                if (urlHit && hitUrl) {
                    *hitUrl = urlIndex;
                    return;
                }
            }
            if (currentInLine >= info.m_rgiLengths[currentLine]) {
                ++currentLine;
                currentInLine = 0;
                if (currentLine < info.m_nLines)
                    leftX = info.m_rgiLeftX[currentLine];
                baseY -= m_fontI->m_lineHeight;
            }
        }
    }
}

void CLabel::Draw(QtPaintDC* dc, POINT*, RECT*)
{
    if (!dc || !dc->surface() || !m_fontI) return;
    CFormatInfo info;
    GetFormatInfoCommon(&info);
    QPainter painter(dc->surface());
    dc->configure(painter);
    if (m_prgdwFormatting && m_prgdwFormatting->GetSize()) {
        DrawFormattedText(painter, info, m_bbox.Top);
        return;
    }

    int baseY = m_bbox.Top;
    for (int index = 0; index < info.m_nLines; ++index) {
        drawUprightText(painter, info.m_rgiLeftX[index], baseY,
                        *m_fontI->m_font,
                        qColor(m_fontI->m_crDefaultForeColor),
                        info.m_rgszStarts[index], info.m_rgiLengths[index]);
        baseY -= m_fontI->m_lineHeight;
    }
}

void CLabel::GetBBox(RECT* result)
{
    CFormatInfo info;
    BreakIntoLines(info);
    result->left = info.m_bbox.Left;
    result->top = info.m_bbox.Top;
    result->right = info.m_bbox.Right;
    result->bottom = info.m_bbox.Bottom;
}

void CStarLabel::Draw(QtPaintDC* dc, POINT*, RECT*)
{
    if (!dc || !dc->surface()) return;
    QPainter painter(dc->surface());
    dc->configure(painter);
    const QFontMetrics metrics(*m_fontI->m_font);
    const QString text = metrics.elidedText(IntlTextToQString(m_str),
        Qt::ElideRight, m_bbox.Right - m_bbox.Left);
    const QByteArray bytes = IntlTextFromQString(QStringView(text));
    drawUprightText(painter, m_bbox.Left, m_bbox.Top, *m_fontI->m_font,
                    qColor(m_fontI->m_crDefaultForeColor),
                    bytes.constData(), bytes.size());
}

CHotLinkLabel::CHotLinkLabel(const CHotLinkLabel& source)
    : CLabel(source)
{
}

CHotLinkLabel::CHotLinkLabel(const char* text, CFontInfo* fontInfo,
                             CDWordArray* formatting)
    : CLabel(text, fontInfo, formatting)
{
    CreateURLArray(text, m_prgdwFormatting, nullptr, &m_prgszURLs);
}

CHotLinkLabel::~CHotLinkLabel()
{
    if (!m_prgszURLs) return;
    for (int index = 0; index < MAX_URL_INTEXT; ++index) delete[] m_prgszURLs[index];
    delete[] m_prgszURLs;
}

void CHotLinkLabel::OnLButtonDown(POINT& point, CPanel* panel)
{
    if (!m_prgdwFormatting || !bURLPresent(m_prgdwFormatting)) return;
    int url = -1;
    mouseDownPt = point;
    CFormatInfo info;
    const SRECT rectangle = m_bbox;
    m_bbox.Right -= m_bbox.Left;
    m_bbox.Left = 0;
    GetFormatInfoCommon(&info);
    m_bbox = rectangle;
    QImage scratch(1, 1, QImage::Format_RGB32);
    QPainter painter(&scratch);
    DrawFormattedText(painter, info, 0, &url);
    if (url >= 0 && m_prgszURLs && m_prgszURLs[url] && panel)
        panel->OnClickHotLink(static_cast<UINT>(url), m_prgszURLs[0]);
}

CBalloon::CBalloon(const char* text, CFontInfo* fontInfo,
                   CDWordArray* formatting, const char* urlStart)
    : CLabel(text, fontInfo, formatting)
{
    m_trueBox.Left = m_trueBox.Right = m_trueBox.Top = m_trueBox.Bottom = -1;
    CreateURLArray(m_str, m_prgdwFormatting, urlStart, &m_prgszURLs);
}

CBalloon::CBalloon(const CBalloon& source)
    : CLabel(source)
    , m_fInfo(source.m_fInfo ? new CFormatInfo(*source.m_fInfo) : nullptr)
    , m_trueBox(source.m_trueBox)
    , m_spline(source.m_spline ? source.m_spline->Clone() : nullptr)
{
}

CBalloon::~CBalloon()
{
    delete m_fInfo;
    delete m_spline;
    delete m_traj;
    if (m_prgszURLs) {
        for (int index = 0; index < MAX_URL_INTEXT; ++index) delete[] m_prgszURLs[index];
        delete[] m_prgszURLs;
    }
}

void CBalloon::GetBBox(RECT* result)
{
    GetCloudBBox(result);
    if (m_speaker) result->bottom = std::min<LONG>(result->bottom,
                                                   m_speaker->m_bbox.Top + 200);
}

void CBalloon::DockAtTop(int height)
{
    const int oldHeight = m_bbox.Top - m_bbox.Bottom;
    m_bbox.Top = static_cast<SHORT>(height + TOPBORDER);
    m_bbox.Bottom = static_cast<SHORT>(m_bbox.Top - oldHeight);
}

void CBalloon::DrawText(QPainter& painter)
{
    if (!m_fInfo || !m_fontI || !m_fontI->m_font) return;
    if (m_prgdwFormatting && m_prgdwFormatting->GetSize()) {
        DrawFormattedText(painter, *m_fInfo, 0);
        return;
    }

    int baseY = 0;
    for (int index = 0; index < m_fInfo->m_nLines; ++index) {
        drawUprightText(painter, m_fInfo->m_rgiLeftX[index], baseY,
                        *m_fontI->m_font,
                        qColor(m_fontI->m_crDefaultForeColor),
                        m_fInfo->m_rgszStarts[index],
                        m_fInfo->m_rgiLengths[index]);
        baseY -= m_fontI->m_lineHeight;
    }
}

void CBalloon::OnLButtonDown(POINT& point, CPanel*)
{
    if (!m_prgdwFormatting || !bURLPresent(m_prgdwFormatting) || !m_fInfo)
        return;
    int url = -1;
    mouseDownPt = point;
    QImage scratch(1, 1, QImage::Format_RGB32);
    QPainter painter(&scratch);
    DrawFormattedText(painter, *m_fInfo, 0, &url);
    if (url >= 0 && m_prgszURLs && m_prgszURLs[url])
        FLaunchBrowser(m_prgszURLs[url]);
}

void CLabel::CreateURLArray(const char* text, CDWordArray* formatting,
                            const char* urlStart, char*** urls)
{
    if (!bURLPresent(formatting)) return;
    *urls = new char*[MAX_URL_INTEXT]{};
    int urlCount = -1;
    BOOL inUrl = FALSE;
    const char* urlBegin = nullptr;
    for (int index = 0; index <= formatting->GetUpperBound(); ++index) {
        const DWORD element = formatting->GetAt(index);
        const WORD format = LOWORD(element);
        if (!inUrl && (format & wLink)) {
            inUrl = TRUE;
            ++urlCount;
            urlBegin = text + HIWORD(element);
        } else if (inUrl && !(format & wLink)) {
            inUrl = FALSE;
            const char* urlEnd = text + HIWORD(element);
            if (urlCount < MAX_URL_INTEXT) {
                const char* source = (urlCount == 0 && urlStart) ? urlStart : urlBegin;
                const int length = (urlCount == 0 && urlStart)
                    ? static_cast<int>(std::strlen(urlStart))
                    : static_cast<int>(urlEnd - urlBegin);
                (*urls)[urlCount] = new char[length + 1];
                std::memcpy((*urls)[urlCount], source, length);
                (*urls)[urlCount][length] = '\0';
            }
        }
    }
    if (inUrl && urlCount < MAX_URL_INTEXT) {
        const char* source = (urlCount == 0 && urlStart) ? urlStart : urlBegin;
        (*urls)[urlCount] = new char[std::strlen(source) + 1];
        std::strcpy((*urls)[urlCount], source);
    }
}

void CBalloon::QueryRouteRgn(int otherToX, int& leftAllowance,
                             int& rightAllowance)
{
    const int toX = m_speaker->m_arrowX;
    if (otherToX > toX) {
        leftAllowance = std::max(toX, static_cast<int>(m_routeRgn.Left) + MINROUTEWIDTH);
        rightAllowance = LARGEINTEGER;
    } else {
        leftAllowance = -LARGEINTEGER;
        rightAllowance = std::min(toX, static_cast<int>(m_routeRgn.Right) - MINROUTEWIDTH);
    }
}

void CBalloon::SetRouteRgn(int otherToX, int left, int right)
{
    const int toX = m_speaker->m_arrowX;
    if (otherToX > toX) m_routeRgn.Right = std::min<int>(m_routeRgn.Right, left);
    else m_routeRgn.Left = std::max<int>(m_routeRgn.Left, right);
}

BOOL CBalloon::Overlap(CBalloon* other)
{
    RECT first, second;
    GetCloudBBox(&first);
    other->GetCloudBBox(&second);
    return bbox_overlap(&first, &second);
}

BOOL CBalloon::SetBBox(int left, int bottom, int right, int top)
{
    if (m_bbox.Right - m_bbox.Left != right - left
        || m_bbox.Top - m_bbox.Bottom != top - bottom) {
        m_bbox.Left = 0;
        m_bbox.Right = static_cast<SHORT>((right - left) - 2 * XBORDER);
        m_bbox.Top = 0;
        if (!ComputeInternals()) return FALSE;
        bottom = top + m_trueBox.Bottom - m_trueBox.Top;
    }
    m_bbox.Left = static_cast<SHORT>(left - m_trueBox.Left);
    m_bbox.Right = static_cast<SHORT>(right - m_trueBox.Left);
    m_bbox.Top = static_cast<SHORT>(top - m_trueBox.Top);
    m_bbox.Bottom = static_cast<SHORT>(bottom - m_trueBox.Top);
    return TRUE;
}

void CBalloon::InMyCoords(SRECT* bbox)
{
    bbox->Left -= m_bbox.Left;
    bbox->Right -= m_bbox.Left;
    bbox->Top -= m_bbox.Top;
    bbox->Bottom -= m_bbox.Top;
}

void CBalloon::ComputeCloudBBox()
{
    make_empty(&m_trueBox);
    for (int index = 0; index < m_spline->nCps; ++index) {
        include_pt_in_bbox(&m_spline->cps[index], &m_trueBox);
    }
}

void CBalloon::GetCloudBBox(RECT* result)
{
    result->left = m_trueBox.Left + m_bbox.Left;
    result->top = m_trueBox.Top + m_bbox.Top;
    result->right = m_trueBox.Right + m_bbox.Left;
    result->bottom = m_trueBox.Bottom + m_bbox.Top;
}

void CBalloon::GetCloudBBox(SRECT* result)
{
    result->Left = static_cast<SHORT>(m_trueBox.Left + m_bbox.Left);
    result->Top = static_cast<SHORT>(m_trueBox.Top + m_bbox.Top);
    result->Right = static_cast<SHORT>(m_trueBox.Right + m_bbox.Left);
    result->Bottom = static_cast<SHORT>(m_trueBox.Bottom + m_bbox.Top);
}

CBWoodringNormal::CBWoodringNormal(const char* text, CDWordArray* formatting,
                                   const char* urlStart, BYTE dashed)
    : CBalloon(text, CUnitPanelPage::m_fiWNormal, formatting, urlStart)
    , m_byteDashed(dashed)
{
    Capitalize(m_str);
}

void CBWoodringNormal::AddArrow(CBalloon* balloon, CSpline* spline,
                                CFormatInfo& info)
{
    POINT left, right, bottom, bottom2, top2;
    SRECT* route = &balloon->m_routeRgn;
    bottom2.x = balloon->m_speaker->m_arrowX;
    bottom2.y = balloon->m_speaker->m_bbox.Top + 200;
    bottom.x = bottom2.x - balloon->m_bbox.Left;
    bottom.y = bottom2.y - balloon->m_bbox.Top;
    SRECT cloud;
    GetCloudBBox(&cloud);
    int breakX = ((route->Left + route->Right) / 2) - balloon->m_bbox.Left;
    const int bottomStart = m_fInfo->m_rgiLeftX[m_fInfo->m_nLines - 1];
    const int bottomEnd = bottomStart + m_fInfo->m_rgiWidths[m_fInfo->m_nLines - 1];
    if (breakX < bottomStart
        && bottomStart + balloon->m_bbox.Left < route->Right - LARGEDELTA) {
        breakX = bottomStart + SMALLDELTA;
    } else if (breakX > bottomEnd
               && bottomEnd + balloon->m_bbox.Left > route->Left + LARGEDELTA) {
        breakX = bottomEnd - SMALLDELTA;
    }
    top2.x = breakX + balloon->m_bbox.Left;
    top2.y = cloud.Bottom;
    if (top2.y - bottom2.y < MINTAILHEIGHT) {
        bottom2.y = top2.y - MINTAILHEIGHT;
        bottom.y = bottom2.y - balloon->m_bbox.Top;
    }
    double angle = vector_to_angle(point_sub(top2, bottom2));
    if (std::fabs(angle) - PI / 2.0 > PI / 4.0) {
        angle = angle > 3 * PI / 4.0 ? 3 * PI / 4.0 : PI / 4.0;
        const int heightDelta = top2.y - bottom2.y;
        breakX = static_cast<int>(std::cos(angle) * heightDelta
                                  + bottom2.x - balloon->m_bbox.Left);
    }
    BreakSpline(spline, breakX, info.m_bbox.Bottom, 1.0);
    left = spline->cps[spline->nCps - 1];
    right = spline->cps[0];
    top2.y = (left.y + right.y) / 2 + balloon->m_bbox.Top;
    top2.x = (left.x + right.x) / 2 + balloon->m_bbox.Left;
    const int tailLength = static_cast<int>(point_dist(top2, bottom2));
    const int altitude = static_cast<int>(0.05 * tailLength);
    const int sign = bottom.x > left.x ? 1 : -1;
    m_traj->AddSeg(new CArc(left, bottom, sign * altitude));
    m_traj->AddSeg(new CArc(bottom, right, -sign * altitude));
}

char* CBWoodringNormal::SplitHeight(int height, CDWordArray** restFormatting,
                                    char** urlStartInRest)
{
    if (restFormatting) *restFormatting = nullptr;
    if (urlStartInRest) *urlStartInRest = nullptr;
    const int maximumLines = (height - BORDERFUDGE) / m_fontI->m_lineHeight;
    if (!m_fInfo || maximumLines >= m_fInfo->m_nLines || maximumLines < 1) return nullptr;
    m_fInfo->m_nLines = static_cast<UCHAR>(maximumLines);
    CDWordArray* pulled = PullFormattingOffsets(m_prgdwFormatting,
        static_cast<SHORT>(m_fInfo->m_rgszStarts[maximumLines - 1] - m_str));
    int dummyWidth = 0, dummyHeight = 0;
    char* end = FindFurthestLineBreak(m_fontI,
        m_fInfo->m_bbox.Right - m_fInfo->m_bbox.Left - m_fontI->m_continuationWidth,
        m_fInfo->m_rgszStarts[maximumLines - 1], pulled, dummyWidth, dummyHeight);
    FreeAndNullFormatting(&pulled);
    int copyLength = static_cast<int>(end - m_str);
    const int continuationLength = std::strlen(continuation1);
    if (copyLength <= continuationLength
        && std::strncmp(m_str, continuation1, continuationLength) == 0
        && m_str[continuationLength]) {
        copyLength = continuationLength + 1;
    }
    auto* newText = static_cast<char*>(std::malloc(copyLength + continuationLength + 1));
    std::memcpy(newText, m_str, copyLength);
    std::strcpy(newText + copyLength, continuation1);
    char* restStart = GetNextStart(m_str + copyLength);
    auto* rest = static_cast<char*>(std::malloc(
        std::strlen(restStart) + std::strlen(continuation2) + 1));
    std::strcpy(rest, continuation2);
    std::strcat(rest, restStart);
    if (restFormatting) {
        *restFormatting = PullFormattingOffsets(
            m_prgdwFormatting, static_cast<SHORT>(restStart - m_str));
    }
    std::free(m_str);
    m_str = newText;
    if (m_prgdwFormatting) {
        BOOL firstRestCharacterNotUrl = FALSE;
        m_prgdwFormatting = CutFormattingArray(
            m_prgdwFormatting, static_cast<SHORT>(copyLength + 1));
        if (m_prgdwFormatting) {
            const DWORD last = m_prgdwFormatting->GetAt(
                m_prgdwFormatting->GetUpperBound());
            firstRestCharacterNotUrl = !(LOWORD(last) & wLink)
                && HIWORD(last) == copyLength;
        }
        m_prgdwFormatting = CutFormattingArray(m_prgdwFormatting,
                                               static_cast<SHORT>(copyLength));
        if (m_prgdwFormatting) {
            const DWORD last = m_prgdwFormatting->GetAt(
                m_prgdwFormatting->GetUpperBound());
            if ((LOWORD(last) & wLink) && !firstRestCharacterNotUrl
                && urlStartInRest && restFormatting && *restFormatting
                && m_prgszURLs) {
                DWORD lastUrlStart = 0;
                BOOL inUrl = FALSE;
                BOOL foundUrlStart = FALSE;
                for (int index = 0;
                     index <= m_prgdwFormatting->GetUpperBound(); ++index) {
                    const DWORD element = m_prgdwFormatting->GetAt(index);
                    if (!inUrl && (LOWORD(element) & wLink)) {
                        inUrl = TRUE;
                        foundUrlStart = TRUE;
                        lastUrlStart = element;
                    } else if (inUrl && !(LOWORD(element) & wLink)) {
                        inUrl = FALSE;
                    }
                }

                int firstUrlEndIndex = -1;
                DWORD firstUrlEnd = 0;
                for (int index = 0;
                     index <= (*restFormatting)->GetUpperBound(); ++index) {
                    const DWORD element = (*restFormatting)->GetAt(index);
                    if (!(LOWORD(element) & wLink)) {
                        firstUrlEndIndex = index;
                        firstUrlEnd = element;
                        break;
                    }
                }

                if (foundUrlStart && firstUrlEndIndex > 0) {
                    QByteArray partialUrl(m_str + HIWORD(lastUrlStart));
                    const int firstContinuationLength =
                        static_cast<int>(std::strlen(continuation1));
                    if (partialUrl.size() >= firstContinuationLength) {
                        partialUrl.chop(firstContinuationLength);
                    }
                    partialUrl.append(
                        rest + std::strlen(continuation2),
                        HIWORD(firstUrlEnd));

                    for (int index = 0; index < MAX_URL_INTEXT; ++index) {
                        if (!m_prgszURLs[index]) continue;
                        QByteArray candidate(m_prgszURLs[index]);
                        Capitalize(candidate.data());
                        if (!candidate.contains(partialUrl)) continue;
                        const int length = static_cast<int>(
                            std::strlen(m_prgszURLs[index]));
                        *urlStartInRest = new char[length + 1];
                        std::memcpy(*urlStartInRest, m_prgszURLs[index],
                                    static_cast<size_t>(length + 1));
                        break;
                    }
                }
            }
            m_prgdwFormatting->Add(MAKELONG(0, static_cast<WORD>(copyLength)));
        }
    }
    if (restFormatting) {
        PushFormattingOffsets(*restFormatting,
                              static_cast<SHORT>(std::strlen(continuation2)));
    }
    --m_bbox.Left;
    SetBBox(m_bbox.Left + m_trueBox.Left + 1,
            m_bbox.Bottom + m_trueBox.Top,
            m_bbox.Right + m_trueBox.Left,
            m_bbox.Top + m_trueBox.Top);
    return rest;
}

CSpline* CBWoodringNormal::CreateBalloonSpline(CFormatInfo& info)
{
    RANGE leftFilters[20], rightFilters[20];
    POINT points[MAXPTS], next, current;
    int leftCount, rightCount, count = 0;
    GetFilters(info, leftFilters, rightFilters, leftCount, rightCount);
    int lastY = PermuteFilters(*m_fontI, leftFilters, rightFilters,
                               leftCount, rightCount);
    const int finalY = lastY;
    for (int index = 0; index < leftCount; ++index) {
        current.x = next.x = leftFilters[index].x;
        current.y = leftFilters[index].y;
        if (index > 0) AddWavies(points[count - 1], current, points, count,
                                  HWAVEHEIGHT, HWAVEINTERVAL);
        points[count++] = current;
        next.y = index == leftCount - 1 ? finalY : leftFilters[index + 1].y;
        AddWavies(points[count - 1], next, points, count,
                  VWAVEHEIGHT, VWAVEINTERVAL);
        points[count++] = next;
    }
    for (int index = rightCount - 1; index >= 0; --index) {
        current.x = next.x = rightFilters[index].x;
        current.y = lastY;
        AddWavies(points[count - 1], current, points, count,
                  HWAVEHEIGHT, HWAVEINTERVAL);
        points[count++] = current;
        next.x = current.x;
        lastY = next.y = rightFilters[index].y;
        AddWavies(points[count - 1], next, points, count,
                  VWAVEHEIGHT, VWAVEINTERVAL);
        points[count++] = next;
    }
    AddWavies(points[count - 1], points[0], points, count,
              HWAVEHEIGHT, HWAVEINTERVAL);
    return new CBeta(points, count, TRUE);
}

CSpline* CBWoodringNormal::GetBalloonSpline()
{
    CSpline* result = m_spline->Clone();
    AddArrow(this, result, *m_fInfo);
    return result;
}

void CBWoodringNormal::SetBalloonTraj()
{
    delete m_traj;
    m_traj = new CTraj;
    CSpline* spline = m_spline->Clone();
    m_traj->AddSeg(spline);
    AddArrow(this, spline, *m_fInfo);
    m_traj->m_closed = TRUE;
}

BOOL CBWoodringNormal::ComputeInternals()
{
    if (!m_fInfo) m_fInfo = new CFormatInfo;
    if (!BreakIntoLines(*m_fInfo)) return FALSE;
    ShiftLines(*m_fInfo);
    delete m_spline;
    m_spline = CreateBalloonSpline(*m_fInfo);
    ComputeCloudBBox();
    return TRUE;
}

void CBWoodringNormal::Draw(QtPaintDC* dc, POINT*, RECT*)
{
    if (!dc || !dc->surface() || !m_fInfo) return;
    if (!m_traj) SetBalloonTraj();
    QPainter painter(dc->surface());
    dc->configure(painter);
    painter.translate(m_bbox.Left, m_bbox.Top);
    QPainterPath path;
    m_traj->Draw(&path);
    painter.setPen(QPen(m_byteDashed ? Qt::white : Qt::black,
                        m_byteDashed ? 100 : 28));
    painter.setBrush(Qt::white);
    painter.drawPath(path);
    if (m_byteDashed) {
        QPainterPath dashes;
        m_traj->Dash(&dashes);
        painter.setPen(QPen(Qt::black, 28));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(dashes);
    }
    DrawText(painter);
}

CBWoodringWhisper::CBWoodringWhisper(const char* text, CDWordArray* formatting,
                                     const char* urlStart)
    : CBWoodringNormal(text, formatting, urlStart, 1)
{
    m_fontI = CUnitPanelPage::m_fiWWhisper;
}

CBWoodringThink::CBWoodringThink(const char* text, CDWordArray* formatting,
                                 const char* urlStart)
    : CBWoodringNormal(text, formatting, urlStart, 0)
{
}

void CBWoodringThink::Draw(QtPaintDC* dc, POINT* upperLeft, RECT* damage)
{
    CBWoodringNormal::Draw(dc, upperLeft, damage);
    if (!dc || !dc->surface() || !m_fInfo || !m_speaker) return;
    POINT entry{(m_routeRgn.Left + m_routeRgn.Right) / 2,
                m_fInfo->m_bbox.Bottom + m_bbox.Top};
    POINT tail{m_speaker->m_arrowX, m_speaker->m_bbox.Top + 200};
    const int deltaY = entry.y - tail.y;
    if (deltaY < 0) return;
    const int bubbleCount = (deltaY + INTERBUBBLE) / (BUBBLEHEIGHT + INTERBUBBLE);
    if (bubbleCount < 0) return;
    const int spacing = bubbleCount > 1
        ? (deltaY - BUBBLEHEIGHT * bubbleCount) / (bubbleCount - 1) : 0;
    const DPOINT delta = point_norm(point_to_dpoint(point_sub(entry, tail)));
    POINT start = point_add(tail,
        dpoint_to_point(point_scalmult(BUBBLEHEIGHT / 2.0, delta)));
    const POINT increment = dpoint_to_point(
        point_scalmult(static_cast<double>(BUBBLEHEIGHT + spacing), delta));
    const int widthDelta = bubbleCount > 1
        ? (ENDBUBBLEWIDTH - BUBBLEHEIGHT) / (2 * (bubbleCount - 1)) : 0;
    QPainter painter(dc->surface());
    dc->configure(painter);
    painter.setPen(QPen(Qt::black, 28));
    painter.setBrush(Qt::white);
    int adjustment = 0;
    for (int index = 0; index < bubbleCount; ++index) {
        QRectF ellipse(start.x - BUBBLEHEIGHT / 2 - adjustment,
                       start.y + BUBBLEHEIGHT / 2,
                       BUBBLEHEIGHT + 2 * adjustment, -BUBBLEHEIGHT);
        painter.drawEllipse(ellipse.normalized());
        start = point_add(start, increment);
        adjustment += widthDelta;
    }
}

CBWoodringBox::CBWoodringBox(const char* text, CDWordArray* formatting,
                             const char* urlStart, BYTE dashed)
    : CBWoodringNormal(text, formatting, urlStart, dashed)
{
    m_format |= FT_LEFT_JUSTIFY;
}

void CBWoodringBox::SetBalloonTraj()
{
    delete m_traj;
    m_traj = new CTraj;
    const SRECT* box = &m_fInfo->m_bbox;
    POINT first{box->Left - XBOXDELTA, box->Bottom - YBOXDELTA};
    POINT second{first.x, box->Top + YBOXDELTA};
    POINT third{box->Right + XBOXDELTA, second.y};
    POINT fourth{third.x, first.y};
    m_traj->AddSeg(new CLine(first, second));
    m_traj->AddSeg(new CLine(second, third));
    m_traj->AddSeg(new CLine(third, fourth));
    m_traj->AddSeg(new CLine(fourth, first));
    m_traj->m_closed = TRUE;
}

void CBWoodringBox::GetBBox(RECT* result)
{
    GetCloudBBox(result);
}

void CBWoodringBox::ComputeCloudBBox()
{
    m_trueBox.Left = static_cast<SHORT>(m_fInfo->m_bbox.Left - XBOXDELTA);
    m_trueBox.Right = static_cast<SHORT>(m_fInfo->m_bbox.Right + XBOXDELTA);
    m_trueBox.Bottom = static_cast<SHORT>(m_fInfo->m_bbox.Bottom - YBOXDELTA);
    m_trueBox.Top = static_cast<SHORT>(m_fInfo->m_bbox.Top + YBOXDELTA);
}

void CBWoodringBox::QueryRouteRgn(int, int& leftAllowance, int& rightAllowance)
{
    rightAllowance = LARGEINTEGER;
    leftAllowance = -LARGEINTEGER;
}
