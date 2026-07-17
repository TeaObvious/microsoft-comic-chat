#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "pageview.h"
#include "paintdc.h"
#include "panel.h"
#include "protsupp.h"
#include "userinfo.h"

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <cstdlib>

namespace {

[[noreturn]] void fail()
{
    std::abort();
}

void require(bool condition)
{
    if (!condition) fail();
}

bool containsInk(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if ((line[x] & 0x00ffffffU) != 0x00ffffffU) return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    theApp.InitializeComicsFonts();
    CUnitPanelPage::SetUnitPanelWidth(3000); // COMFORTABLEPANELWIDTH in pageview.cpp
    CUnitPanelPage::SetUnitPanelHeight(3000);
    CUnitPanelPage::SetUnitPanelsPerRow(2);
    require(CUnitPanelPage::SetFonts(theApp.m_comicsFont, theApp.m_comicsColor));
    InitializeAvatars();

    QString originalAvatarName;
    GetNextAvatarName(originalAvatarName);
    require(!originalAvatarName.isEmpty());
    CAvatarX* avatar = GetAvatar3(originalAvatarName);
    require(avatar && avatar->m_icon && avatar->m_body);

    {
        CChatDoc document;
        SetChatDoc(&document);
        CUserInfo self(avatar->m_name);
        g_puiSelf = &self;
        document.m_puiSelf = &self;
        SetMyAvatar(avatar->m_avatarID, FALSE);
        require(MyAvatarID() == avatar->m_avatarID);

        document.AddNewPage();
        CPage* page = document.m_pages.first();
        const QByteArray title = document.GetComicsTitle().toUtf8();
        page->AddTitle(title.constData());
        require(page->m_panels.size() == 1);
        CPanel* titlePanel = page->m_panels.first();
        require(!titlePanel->m_hasBorder);
        require(titlePanel->m_backDrop.m_backID == 0);
        // No NAMES/member data: only title and source ID_STARRING label.
        require(titlePanel->m_elements.size() == 2);

        auto* starringLabel = dynamic_cast<CLabel*>(titlePanel->m_elements[1]);
        require(starringLabel && !starringLabel->m_prgdwFormatting);
        QImage labelOnly(300, 300, QImage::Format_RGB32);
        labelOnly.fill(Qt::white);
        QtPaintDC labelDc(&labelOnly, CUnitPanelPage::m_unitWidth,
                          CUnitPanelPage::m_unitHeight);
        RECT labelDamage{0, 0, CUnitPanelPage::m_unitWidth,
                         -CUnitPanelPage::m_unitHeight};
        starringLabel->Draw(&labelDc, nullptr, &labelDamage);
        require(containsInk(labelOnly));

        g_mapNickToPtr->insert(self.GetName(), &self);
        page->UpdateTitle();
        // One real mapped AVB supplies exactly icon + CStarLabel.
        require(titlePanel->m_elements.size() == 4);
        require(dynamic_cast<CBodyUnary*>(titlePanel->m_elements[2]) != nullptr);
        require(dynamic_cast<CStarLabel*>(titlePanel->m_elements[3]) != nullptr);

        // "..." is the source's own balloon continuation string; no invented
        // chat fixture is introduced for this structural production-path test.
        require(page->AddLine(avatar->m_avatarID, "...", BM_SAY, nullptr));
        require(page->m_panels.size() == 2);
        CPanel* messagePanel = page->m_panels.last();
        require(messagePanel->m_bodies.size() == 1);
        require(messagePanel->m_elements.size() == 1);
        auto* balloon = dynamic_cast<CBWoodringNormal*>(
            messagePanel->m_elements.first());
        require(balloon && !balloon->m_prgdwFormatting && balloon->m_fInfo);

        QImage textOnly(300, 300, QImage::Format_RGB32);
        textOnly.fill(Qt::white);
        QtPaintDC textDc(&textOnly, CUnitPanelPage::m_unitWidth,
                         CUnitPanelPage::m_unitHeight);
        {
            QPainter painter(&textOnly);
            textDc.configure(painter);
            painter.translate(balloon->m_bbox.Left, balloon->m_bbox.Top);
            balloon->DrawText(painter);
        }
        require(containsInk(textOnly));

        QImage retained(300, 300, QImage::Format_RGB32);
        retained.fill(Qt::white);
        QtPaintDC dc(&retained, CUnitPanelPage::m_unitWidth,
                     CUnitPanelPage::m_unitHeight);
        RECT damage{0, 0, CUnitPanelPage::m_unitWidth,
                    -CUnitPanelPage::m_unitHeight};
        messagePanel->Draw(&dc, nullptr, &damage);
        require(containsInk(retained));

        g_mapNickToPtr->clear();
        avatar->m_userInfo = nullptr;
        g_puiSelf = nullptr;
        document.m_puiSelf = nullptr;
        SetChatDoc(nullptr);
    }

    DestroyAvatars();
    CUnitPanelPage::DestroyFonts();
    return 0;
}
