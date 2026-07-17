#include "balloon.h"
#include "chat.h"
#include "chatdoc.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QScrollBar>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail() { std::abort(); }

void requireAt(bool condition, int line)
{
    if (!condition) {
        std::fprintf(stderr, "require failed at line %d\n", line);
        fail();
    }
}

#define REQUIRE(condition) requireAt((condition), __LINE__)

QImage renderViewport(CPageView& view)
{
    QImage result(view.viewport()->size(), QImage::Format_RGB32);
    result.fill(Qt::white);
    QPainter painter(&result);
    view.viewport()->render(&painter);
    return result;
}

QPoint borderPoint(const QImage& image)
{
    const int scanHeight = std::min(12, image.height());
    for (int y = 0; y < scanHeight; ++y) {
        for (int x = 12; x + 12 < image.width(); ++x) {
            if (qGray(image.pixel(x, y)) < 64) return QPoint(x, y);
        }
    }
    return QPoint(-1, -1);
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    theApp.InitVals();
    theApp.m_bComicView = true;
    theApp.InitializeComicsFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont,
                                     theApp.m_comicsColor));
    CUnitPanelPage::SetUnitPanelWidth(MINUNITPANELWIDTH);
    CUnitPanelPage::SetUnitPanelHeight(MINUNITPANELHEIGHT);
    CUnitPanelPage::SetUnitPanelsPerRow(1);

    CChatDoc document;
    SetChatDoc(&document);
    document.DestroyPages();

    auto* headPage = new CUnitPanelPage(&document);
    auto* headPanel = new CUnitPanel;
    headPanel->m_backDrop.m_backID = 0;
    REQUIRE(headPage->AddPanel(headPanel));
    document.m_pages.append(headPage);

    CPageView view;
    document.m_view = &view;
    view.resize(180, 120);
    view.show();
    application.processEvents();
    view.RefreshPanelN(0);
    application.processEvents();

    const QImage headOnly = renderViewport(view);
    const QPoint blackBorder = borderPoint(headOnly);
    REQUIRE(blackBorder.x() >= 0);

    auto* followingPage = new CUnitPanelPage(&document);
    auto* followingFirstPanel = new CUnitPanel;
    followingFirstPanel->m_backDrop.m_backID = 0;
    followingFirstPanel->m_hasBorder = FALSE;
    REQUIRE(followingPage->AddPanel(followingFirstPanel));
    for (int panel = 1; panel < 3; ++panel) {
        auto* structuralPanel = new CUnitPanel;
        structuralPanel->m_backDrop.m_backID = 0;
        structuralPanel->m_hasBorder = FALSE;
        REQUIRE(followingPage->AddPanel(structuralPanel));
    }
    document.m_pages.append(followingPage);
    view.RefreshPanelN(2);
    application.processEvents();

    // OnDraw walks both pages. The second page is still at the original
    // unfinished (0,0) position and therefore covers the first page here.
    const QImage bothPages = renderViewport(view);
    REQUIRE(qGray(bothPages.pixel(blackBorder)) > 240);

    const int panelPixels = static_cast<int>(std::ceil(
        CUnitPanelPage::m_unitHeight * view.logicalDpiY() / 1440.0));
    REQUIRE(view.verticalScrollBar()->maximum() > panelPixels * 2);

    const QByteArray labelText = originalResourceString(
        QStringLiteral("ID_STARRING")).toLocal8Bit();
    auto* headLabel = new CLabel(labelText.constData(), nullptr);
    auto* followingLabel = new CLabel(labelText.constData(), nullptr);
    REQUIRE(headLabel->SetBBox(100, -1000, 1000, -100));
    REQUIRE(followingLabel->SetBBox(100, -1000, 1000, -100));
    headPanel->m_elements.append(headLabel);
    followingFirstPanel->m_elements.append(followingLabel);

    view.verticalScrollBar()->setValue(0);
    const qreal pixelsPerTwipX = view.logicalDpiX() / 1440.0;
    const qreal pixelsPerTwipY = view.logicalDpiY() / 1440.0;
    POINT point{
        static_cast<LONG>(std::lround(500 * pixelsPerTwipX)),
        static_cast<LONG>(std::lround(500 * pixelsPerTwipY))};
    POINT panelPoint{};
    void* hitPanel = nullptr;
    REQUIRE(view.FindLabelUnderPoint(point, panelPoint, hitPanel) == headLabel);
    REQUIRE(hitPanel == headPanel);

    document.m_view = nullptr;
    SetChatDoc(nullptr);
    CUnitPanelPage::DestroyFonts();
    return 0;
}
