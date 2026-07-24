#include "avatar.h"
#include "backdrop.h"
#include "chat.h"
#include "chatdoc.h"
#include "mainfrm.h"
#include "originalassets.h"
#include "panel.h"
#include "protsupp.h"
#include "resource.h"

#include <QApplication>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextEdit>

#include "textcore.h"

#include <array>
#include <cstring>
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

QString settingsRoot()
{
    QString root = QString::fromLatin1(szRootRegKeyName);
    return root.replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString key(const QString& name)
{
    return settingsRoot() + QLatin1Char('/') + name;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir settingsDirectory;
    REQUIRE(settingsDirectory.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDirectory.path());

    QApplication application(argc, argv);
    QSettings stored(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("Microsoft"),
                     QStringLiteral("Microsoft Comic Chat"));
    stored.clear();
    stored.sync();

    theApp.InitVals();
    theApp.InitializeFonts();
    const QFont comicsFont = theApp.m_comicsFont;
    const COLORREF comicsColor = theApp.m_comicsColor;
    std::array<CHARFORMAT, NFONTS> expectedTextFonts{};
    const std::array<COLORREF, NREGULARFONTS> regularColors = {
        RGB(0, 128, 0), RGB(0, 0, 255), RGB(0, 0, 0),
        RGB(128, 0, 0), RGB(128, 128, 128), RGB(0, 0, 128),
        RGB(0, 128, 128), RGB(128, 0, 128), RGB(255, 0, 255),
        RGB(255, 0, 0)
    };
    for (int index = 0; index < NREGULARFONTS; ++index) {
        CHARFORMAT& format = expectedTextFonts[index];
        format.cbSize = sizeof(CHARFORMAT);
        format.dwMask = CFM_COLOR | CFM_BOLD;
        format.dwEffects = index == 0 || index == 9 ? CFE_BOLD : 0;
        format.crTextColor = regularColors[index];
        format.bCharSet = ANSI_CHARSET;
    }
    const std::array<COLORREF, 4> highlightColors = {
        RGB(128, 128, 128), RGB(128, 128, 0),
        RGB(0, 255, 255), RGB(255, 0, 255)
    };
    for (int index = 0; index < NHIGHLIGHTEDFONTS; ++index) {
        CHARFORMAT& format = expectedTextFonts[NREGULARFONTS + index];
        format.cbSize = sizeof(CHARFORMAT);
        format.dwMask = CFM_COLOR | CFM_BOLD;
        format.dwEffects = CFE_BOLD;
        if ((index & 1) == 0) {
            format.dwMask |= CFM_SIZE;
            format.yHeight = 240;
        }
        format.crTextColor = highlightColors[index / 2];
        format.bCharSet = ANSI_CHARSET;
    }
    const QByteArray expectedRegularBytes(
        reinterpret_cast<const char*>(expectedTextFonts.data()),
        sizeof(CHARFORMAT) * NREGULARFONTS);
    const QByteArray expectedHighlightBytes(
        reinterpret_cast<const char*>(
            expectedTextFonts.data() + NREGULARFONTS),
        sizeof(CHARFORMAT) * NHIGHLIGHTEDFONTS);
    theApp.m_iShowBars = SB_TOOLBAR_ANY | SB_STATUSBAR;
    theApp.m_bComicView = true;
    theApp.m_bSaveViewMode = true;
    theApp.m_bIconMembers = false;
    theApp.m_bPrompt = true;
    theApp.m_bAcceptWhispers = false;
    theApp.m_bAutoDownloadAvatars = true;
    theApp.m_bAutoDownloadBackdrops = false;
    theApp.m_bShowArrivals = false;
    theApp.m_bAllowInvites = false;
    theApp.m_bAllowFileTX = false;
    theApp.m_bPlaySounds = false;
    theApp.m_bAcceptNMCalls = false;
    theApp.m_bShowIdentity = false;
    theApp.m_bListRegistered = true;
    theApp.m_textSpacing = TEXT_VIEW_BLANK_ALWAYS;
    theApp.m_iHostHighlight = HH_BOLD_HEADERS;
    theApp.m_iOnConnectAction = CA_ROOMLIST;
    theApp.m_iGreetingType = AGT_WHISPER;
    SetSendComicsData(false);

    const QString profile = originalResourceString(
        QStringLiteral("ID_DEFAULT_PROFILE"));
    const QString awayMessage = originalResourceString(
        QStringLiteral("IDS_DFLTAWAYMSG"));
    REQUIRE(!profile.isEmpty());
    REQUIRE(!awayMessage.isEmpty());
    theApp.m_myProfile = profile;
    theApp.m_strAwayMessage = awayMessage;

    QString character;
    GetNextAvatarName(character);
    REQUIRE(!character.isEmpty());
    REQUIRE(character != QStringLiteral("_NoArt"));
    const QStringList backdrops = OriginalBackdropNames();
    REQUIRE(!backdrops.isEmpty());
    const QString backdrop = backdrops.first();
    theApp.m_myCharacterName = character;
    theApp.m_lastBackDrop = backdrop;

    const OriginalDialogResource whisperDialog = originalDialogResource(
        QStringLiteral("IDD_WHISPERBOX"));
    const OriginalDialogResource notifsDialog = originalDialogResource(
        QStringLiteral("IDD_NOTIFICATIONS"));
    REQUIRE(whisperDialog.width > 0 && whisperDialog.height > 0);
    REQUIRE(notifsDialog.width > 0 && notifsDialog.height > 0);
    theApp.m_rectWhisper = QRect(0, 0, whisperDialog.width,
                                whisperDialog.height);
    theApp.m_rectNotifs = QRect(0, 0, notifsDialog.width,
                               notifsDialog.height);

    const int panelWidth = CUnitPanelPage::GetUnitPanelWidth();
    const int panelHeight = CUnitPanelPage::GetUnitPanelHeight();
    const int panelsPerRow = CUnitPanelPage::GetUnitPanelsPerRow();
    theApp.m_uFloodInterval = 8;
    theApp.m_uFloodCount = 8;
    theApp.m_uFloodFlags = FLOOD_IGNORE;
    theApp.m_dynaRules.SetFloodParams(8, 8);

    const QStringList macroKeys = originalDialogInitStrings(
        QStringLiteral("IDD_AUTOMATION_PAGE"), QStringLiteral("IDC_KEY"));
    REQUIRE(macroKeys.size() == NMACROS);
    theApp.m_macros[0].m_bDefined = TRUE;
    theApp.m_macros[0].m_strName = macroKeys.first();
    theApp.m_macros[0].m_strValue = awayMessage;
    for (INT index = 1; index < NMACROS; ++index)
        theApp.m_macros[index] = CMacro{};

    QByteArray expectedMacro((MAX_FORMATTINGPERBYTE + 1) * MAX_INPUTLEN, '\0');
    const INT expectedMacroSize = theApp.m_macros[0].Serialize(
        expectedMacro.data(), expectedMacro.size());
    REQUIRE(expectedMacroSize > 0);
    expectedMacro.resize(expectedMacroSize);

    QByteArray shortToolbarState;
    int shortX = 0;
    {
        CMainFrame frame;
        theApp.m_pMainWnd = &frame;
        frame.setGeometry(40, 50, 640, 480);
        frame.show();
        application.processEvents();
        REQUIRE(theApp.SaveToReg(TRUE));
        stored.sync();

        REQUIRE(stored.contains(key(QStringLiteral("XFrame"))));
        REQUIRE(stored.contains(key(QStringLiteral("YFrame"))));
        REQUIRE(stored.contains(key(QStringLiteral("CXFrame"))));
        REQUIRE(stored.contains(key(QStringLiteral("CYFrame"))));
        REQUIRE(stored.contains(key(QStringLiteral("Maximized"))));
        REQUIRE(stored.contains(key(QStringLiteral("ShowBars"))));
        REQUIRE(stored.contains(key(QStringLiteral("ToolBarState"))));
        REQUIRE(!stored.contains(key(QStringLiteral("UPNLWidth"))));
        REQUIRE(!stored.contains(key(QStringLiteral("Name"))));
        shortX = stored.value(key(QStringLiteral("XFrame"))).toInt();
        shortToolbarState = stored.value(
            key(QStringLiteral("ToolBarState"))).toByteArray();
        REQUIRE(shortToolbarState.size() == 20);

        frame.move(frame.x() + 1, frame.y() + 1);
        REQUIRE(theApp.SaveToReg(FALSE));
        stored.sync();
        REQUIRE(stored.value(key(QStringLiteral("XFrame"))).toInt() == shortX);
        REQUIRE(stored.value(key(QStringLiteral("ToolBarState"))).toByteArray()
                == shortToolbarState);
        theApp.m_pMainWnd = nullptr;
    }

    REQUIRE(stored.value(key(QStringLiteral("UPNLWidth"))).toInt()
            == panelWidth);
    REQUIRE(stored.value(key(QStringLiteral("UPNLHeight"))).toInt()
            == panelHeight);
    REQUIRE(stored.value(key(QStringLiteral("UnitsWide"))).toInt()
            == panelsPerRow);
    REQUIRE(stored.value(key(QStringLiteral("Character"))).toString()
            == character);
    REQUIRE(stored.value(key(QStringLiteral("Backdrop"))).toString()
            == backdrop);
    REQUIRE(stored.value(key(QStringLiteral("Profile"))).toString()
            == profile);
    REQUIRE(stored.value(key(QStringLiteral("ComicsData"))).toBool() == false);
    REQUIRE(stored.value(key(QStringLiteral("MemberListStyle"))).toBool()
            == false);
    REQUIRE(stored.value(key(QStringLiteral("FloodControl"))).toInt()
            == (8 | (8 << 8) | (FLOOD_IGNORE << 16)));
    REQUIRE(stored.value(key(QStringLiteral("RulesControl"))).toInt()
            == (8 | (8 << 8)));
    REQUIRE(!stored.contains(key(QStringLiteral("NoMIDI"))));
    REQUIRE(stored.contains(key(QStringLiteral("TextFonts"))));
    REQUIRE(stored.value(key(QStringLiteral("TextFonts"))).toByteArray()
            .isEmpty());
    REQUIRE(stored.contains(key(QStringLiteral("HighlightedTextFonts"))));
    REQUIRE(stored.value(key(QStringLiteral("HighlightedTextFonts")))
            .toByteArray().isEmpty());
    REQUIRE(stored.value(key(QStringLiteral("Macros/0"))).toByteArray()
            == expectedMacro);

    QString storedGreeting = theApp.m_strGreetingMesg;
    REQUIRE(bReplaceMacroTokens(storedGreeting, FALSE));
    REQUIRE(stored.value(key(QStringLiteral("AutoGreeting"))).toString()
            == storedGreeting);
    REQUIRE(storedGreeting.contains(QString::fromLatin1(szUserToken)));
    REQUIRE(storedGreeting.contains(QString::fromLatin1(szRoomToken)));
    REQUIRE(bReplaceMacroTokens(storedGreeting, TRUE));
    REQUIRE(storedGreeting == theApp.m_strGreetingMesg);

    // These three source conditions preserve the old values instead of
    // deleting or replacing them.
    theApp.m_myProfile.clear();
    theApp.m_bComicView = false;
    theApp.m_bSaveViewMode = false;
    theApp.m_myCharacterName.clear();
    theApp.m_lastBackDrop.clear();
    std::memcpy(theApp.m_cfArray, expectedTextFonts.data(),
                sizeof(theApp.m_cfArray));
    theApp.m_bCfInitialized = true;
    theApp.m_bCfHLInitialized = true;
    REQUIRE(theApp.SaveToReg(FALSE));
    stored.sync();
    REQUIRE(stored.value(key(QStringLiteral("Profile"))).toString()
            == profile);
    REQUIRE(stored.value(key(QStringLiteral("Character"))).toString()
            == character);
    REQUIRE(stored.value(key(QStringLiteral("Backdrop"))).toString()
            == backdrop);
    REQUIRE(stored.value(key(QStringLiteral("ShowComicView"))).toBool());
    REQUIRE(stored.value(key(QStringLiteral("TextFonts"))).toByteArray()
            == expectedRegularBytes);
    REQUIRE(stored.value(key(QStringLiteral("HighlightedTextFonts")))
            .toByteArray() == expectedHighlightBytes);

    stored.setValue(key(QStringLiteral("ShowBars")), SB_TOOLBAR);
    stored.setValue(key(QStringLiteral("NoMIDI")), true);
    stored.sync();

    CUnitPanelPage::SetUnitPanelWidth(panelWidth + 1);
    CUnitPanelPage::SetUnitPanelHeight(panelHeight + 1);
    CUnitPanelPage::SetUnitPanelsPerRow(panelsPerRow + 1);
    theApp.m_macros[0] = CMacro{};
    theApp.m_pbCoolBarState.clear();
    theApp.m_bIconMembers = true;
    theApp.m_myProfile.clear();
    theApp.m_bPrompt = false;
    theApp.m_bAcceptWhispers = true;
    theApp.m_bAutoDownloadAvatars = false;
    theApp.m_bAutoDownloadBackdrops = true;
    theApp.m_bShowArrivals = true;
    theApp.m_bAllowInvites = true;
    theApp.m_bAllowFileTX = true;
    theApp.m_bPlaySounds = true;
    theApp.m_bAcceptNMCalls = true;
    theApp.m_bShowIdentity = true;
    theApp.m_bListRegistered = false;
    theApp.m_textSpacing = TEXT_VIEW_BLANK_NEVER;
    theApp.m_iHostHighlight = HH_BOLD_HEADERS | HH_BOLD_MESSAGES;
    theApp.m_iOnConnectAction = CA_JOINROOM;
    theApp.m_iGreetingType = AGT_NONE;
    theApp.m_rectWhisper = QRect();
    theApp.m_rectNotifs = QRect();
    theApp.m_comicsFont = QFont();
    theApp.m_comicsColor = RGB(1, 1, 1);
    std::memset(theApp.m_cfArray, 0, sizeof(theApp.m_cfArray));
    theApp.m_bCfInitialized = false;
    theApp.m_bCfHLInitialized = false;
    theApp.m_textColor = RGB(255, 0, 255);
    theApp.m_uFloodInterval = 1;
    theApp.m_uFloodCount = 1;
    theApp.m_uFloodFlags = 0;
    theApp.m_dynaRules.SetFloodParams(1, 1);
    SetSendComicsData(true);
    REQUIRE(theApp.LoadFromReg());

    REQUIRE(CUnitPanelPage::GetUnitPanelWidth() == panelWidth);
    REQUIRE(CUnitPanelPage::GetUnitPanelHeight() == panelHeight);
    REQUIRE(CUnitPanelPage::GetUnitPanelsPerRow() == panelsPerRow);
    REQUIRE(theApp.m_pbCoolBarState == shortToolbarState);
    REQUIRE(theApp.m_iShowBars
            == (SB_TOOLBAR | SB_TOOLBAR_ANY | SB_TOOLBAR_OLDREAD));
    REQUIRE(theApp.m_bNoMIDI);
    REQUIRE(!theApp.m_bIconMembers);
    REQUIRE(theApp.m_bPrompt);
    REQUIRE(!theApp.m_bAcceptWhispers);
    REQUIRE(theApp.m_bAutoDownloadAvatars);
    REQUIRE(!theApp.m_bAutoDownloadBackdrops);
    REQUIRE(!theApp.m_bShowArrivals);
    REQUIRE(!theApp.m_bAllowInvites);
    REQUIRE(!theApp.m_bAllowFileTX);
    REQUIRE(!theApp.m_bPlaySounds);
    REQUIRE(!theApp.m_bAcceptNMCalls);
    REQUIRE(!theApp.m_bShowIdentity);
    REQUIRE(theApp.m_bListRegistered);
    REQUIRE(theApp.m_textSpacing == TEXT_VIEW_BLANK_ALWAYS);
    REQUIRE(theApp.m_iHostHighlight == HH_BOLD_HEADERS);
    REQUIRE(theApp.m_iOnConnectAction == CA_ROOMLIST);
    REQUIRE(theApp.m_iGreetingType == AGT_WHISPER);
    REQUIRE(theApp.m_rectWhisper
            == QRect(0, 0, whisperDialog.width, whisperDialog.height));
    REQUIRE(theApp.m_rectNotifs
            == QRect(0, 0, notifsDialog.width, notifsDialog.height));
    REQUIRE(theApp.m_comicsFont == comicsFont);
    REQUIRE(theApp.m_comicsColor == comicsColor);
    REQUIRE(theApp.m_bCfInitialized);
    REQUIRE(theApp.m_bCfHLInitialized);
    REQUIRE(std::memcmp(theApp.m_cfArray, expectedTextFonts.data(),
                        sizeof(theApp.m_cfArray)) == 0);
    REQUIRE(theApp.m_textColor
            == expectedTextFonts[2].crTextColor);
    REQUIRE(theApp.m_uFloodInterval == 8);
    REQUIRE(theApp.m_uFloodCount == 8);
    REQUIRE(theApp.m_uFloodFlags == FLOOD_IGNORE);
    REQUIRE(theApp.m_dynaRules.GetFloodingInterval() == 8);
    REQUIRE(theApp.m_dynaRules.GetFloodingOccurrences() == 8);
    REQUIRE(!GetSendComicsData());
    REQUIRE(theApp.m_myProfile == profile);
    REQUIRE(theApp.m_myCharacterName == character);
    REQUIRE(theApp.m_lastBackDrop == backdrop);
    REQUIRE(theApp.m_strGreetingMesg
            == originalResourceString(QStringLiteral("IDS_DEFAULTGREETING"))
                   .replace(QStringLiteral("%1"), originalResourceString(
                       QStringLiteral("IDS_USERVARIABLE")))
                   .replace(QStringLiteral("%2"), originalResourceString(
                       QStringLiteral("IDS_ROOMVARIABLE"))));
    REQUIRE(theApp.m_macros[0].m_bDefined);
    REQUIRE(theApp.m_macros[0].m_strName == macroKeys.first());
    REQUIRE(theApp.m_macros[0].m_strValue == awayMessage);
    REQUIRE(theApp.m_flags0 & F0_ALREADYRUN);

    {
        CChatDoc document;
        REQUIRE(!document.m_bIconMembers);
        REQUIRE(!document.m_bLastMemberView);
        document.OnViewIcon();
        REQUIRE(document.m_bIconMembers);
        REQUIRE(theApp.m_bIconMembers);
        document.OnViewListAux();
        REQUIRE(!document.m_bIconMembers);
        REQUIRE(theApp.m_bIconMembers);
        document.OnViewList();
        REQUIRE(!theApp.m_bIconMembers);
    }

    stored.setValue(key(QStringLiteral("TextFonts")),
                    QByteArray(expectedRegularBytes.size() - 1, '\0'));
    stored.setValue(key(QStringLiteral("HighlightedTextFonts")),
                    expectedHighlightBytes);
    stored.sync();
    std::memset(theApp.m_cfArray, 0, sizeof(theApp.m_cfArray));
    theApp.m_bCfInitialized = false;
    theApp.m_bCfHLInitialized = false;
    theApp.m_textColor = RGB(255, 0, 0);
    REQUIRE(theApp.LoadFromReg());
    REQUIRE(!theApp.m_bCfInitialized);
    REQUIRE(theApp.m_bCfHLInitialized);
    REQUIRE(std::memcmp(&theApp.m_cfArray[NREGULARFONTS],
                        expectedTextFonts.data() + NREGULARFONTS,
                        sizeof(CHARFORMAT) * NHIGHLIGHTEDFONTS) == 0);
    REQUIRE(theApp.m_textColor == RGB(255, 0, 0));

    stored.setValue(key(QStringLiteral("TextFonts")), expectedRegularBytes);
    stored.setValue(key(QStringLiteral("HighlightedTextFonts")),
                    QByteArray(expectedHighlightBytes.size() + 1, '\0'));
    stored.sync();
    std::memset(theApp.m_cfArray, 0, sizeof(theApp.m_cfArray));
    theApp.m_bCfInitialized = false;
    theApp.m_bCfHLInitialized = false;
    theApp.m_textColor = RGB(255, 0, 0);
    REQUIRE(theApp.LoadFromReg());
    REQUIRE(theApp.m_bCfInitialized);
    REQUIRE(!theApp.m_bCfHLInitialized);
    REQUIRE(std::memcmp(theApp.m_cfArray, expectedTextFonts.data(),
                        sizeof(CHARFORMAT) * NREGULARFONTS) == 0);
    REQUIRE(theApp.m_textColor
            == expectedTextFonts[2].crTextColor);
    return 0;
}
