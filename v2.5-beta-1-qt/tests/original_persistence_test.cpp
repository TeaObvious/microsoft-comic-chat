#include "avatar.h"
#include "backdrop.h"
#include "actions.h"
#include "chat.h"
#include "chatdoc.h"
#include "childfrm.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "protsupp.h"
#include "resource.h"
#include "setupdlg.h"
#include "tabbar.h"
#include "textview.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QMessageBox>
#include <QMdiArea>
#include <QPointer>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QVariantMap>

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

QVariantMap settingsSnapshot(QSettings& settings)
{
    settings.sync();
    QVariantMap snapshot;
    for (const QString& setting : settings.allKeys())
        snapshot.insert(setting, settings.value(setting));
    return snapshot;
}

class CountingIrcProto final : public CIrcProto {
public:
    explicit CountingIrcProto(int* partCount)
        : m_partCount(partCount)
    {
    }

    void ChatPartChannel(CChatDoc*, bool) override
    {
        if (m_partCount) ++*m_partCount;
    }

private:
    int* m_partCount = nullptr;
};

class NonIrcProto final : public CIrcProto {
public:
    int GetType() const override { return PC_NM; }
};

class EnterInfoState {
public:
    EnterInfoState()
        : m_channel(g_enterInfo.m_strChannel)
        , m_prettyChannel(g_enterInfo.m_strPrettyChannel)
        , m_password(g_enterInfo.m_strPassword)
        , m_creationModes(g_enterInfo.m_strCreationModes)
        , m_topic(g_enterInfo.m_strTopic)
        , m_formatting(CopyFormatting(
              g_enterInfo.m_prgdwTopicFormatting))
        , m_modes(g_enterInfo.m_dwModes)
        , m_maximumUsers(g_enterInfo.m_dwMaxUsers)
        , m_setMode(g_enterInfo.m_bSetMode)
        , m_document(g_enterInfo.m_doc)
    {
    }

    ~EnterInfoState()
    {
        g_enterInfo.m_strChannel = m_channel;
        g_enterInfo.m_strPrettyChannel = m_prettyChannel;
        g_enterInfo.m_strPassword = m_password;
        g_enterInfo.m_strCreationModes = m_creationModes;
        g_enterInfo.m_strTopic = m_topic;
        FreeAndNullFormatting(&g_enterInfo.m_prgdwTopicFormatting);
        g_enterInfo.m_prgdwTopicFormatting = m_formatting;
        m_formatting = nullptr;
        g_enterInfo.m_dwModes = m_modes;
        g_enterInfo.m_dwMaxUsers = m_maximumUsers;
        g_enterInfo.m_bSetMode = m_setMode;
        g_enterInfo.m_doc = m_document;
    }

    EnterInfoState(const EnterInfoState&) = delete;
    EnterInfoState& operator=(const EnterInfoState&) = delete;

private:
    QString m_channel;
    QString m_prettyChannel;
    QString m_password;
    QString m_creationModes;
    QString m_topic;
    CDWordArray* m_formatting = nullptr;
    unsigned long m_modes = 0;
    unsigned long m_maximumUsers = 0;
    bool m_setMode = false;
    CChatDoc* m_document = nullptr;
};

void answerNextMessage(QMessageBox::StandardButton answer,
                       bool* observed,
                       const QString& objectName =
                           QStringLiteral("CChatDocSaveModified"))
{
    QTimer::singleShot(0, [answer, observed, objectName] {
        auto* message = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        REQUIRE(message != nullptr);
        if (!objectName.isEmpty())
            REQUIRE(message->objectName() == objectName);
        if (observed) *observed = true;
        message->done(answer);
    });
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

        // Both canonical primary-view destructors perform the short registry
        // save. Modern also detaches the document's raw view pointer before
        // the document can run DeleteContents during later teardown.
        frame.move(frame.x() + 7, frame.y() + 7);
        application.processEvents();
        const QRect pageShortGeometry = frame.normalGeometry().isValid()
            ? frame.normalGeometry() : frame.geometry();
        {
            CChatDoc detachedViewDocument;
            {
                CPageView detachedView(&detachedViewDocument, nullptr);
                detachedViewDocument.m_view = &detachedView;
            }
            REQUIRE(detachedViewDocument.m_view == nullptr);
        }
        stored.sync();
        REQUIRE(stored.value(key(QStringLiteral("XFrame"))).toInt()
                == pageShortGeometry.x());

        frame.move(frame.x() + 9, frame.y() + 9);
        application.processEvents();
        const QRect textShortGeometry = frame.normalGeometry().isValid()
            ? frame.normalGeometry() : frame.geometry();
        {
            CChatDoc detachedTextDocument;
            {
                CTextView detachedTextView(
                    &detachedTextDocument, nullptr);
                detachedTextDocument.m_textView =
                    &detachedTextView;
            }
            REQUIRE(detachedTextDocument.m_textView == nullptr);
        }
        stored.sync();
        REQUIRE(stored.value(key(QStringLiteral("XFrame"))).toInt()
                == textShortGeometry.x());

        stored.beginGroup(settingsRoot());
        QStringList keysBeforeChildState = stored.childKeys();
        stored.endGroup();
        keysBeforeChildState.sort();

        const DWORD savedFlags1 = theApp.m_flags1;
        const bool savedEmbedded = theApp.m_bEmbedded;
        int partCount = 0;
        auto* lifecycleDocument = new CChatDoc;
        lifecycleDocument->m_bComicView = false;
        delete lifecycleDocument->m_proto;
        auto* lifecycleProtocol = new CountingIrcProto(&partCount);
        lifecycleProtocol->m_doc = lifecycleDocument;
        lifecycleProtocol->m_strChannel = QStringLiteral("#lifecycle");
        lifecycleDocument->m_proto = lifecycleProtocol;
        REQUIRE(lifecycleDocument->OnNewDocument());
        CChildFrame* lifecycleFrame =
            frame.AddDocument(lifecycleDocument, true, true);
        REQUIRE(lifecycleFrame != nullptr);
        application.processEvents();

        // F1_MAXMDI is one global bit. Only a positioned, visible,
        // non-minimized, non-exiting, non-embedded child may update it.
        theApp.m_pExitingDoc = lifecycleDocument;
        lifecycleFrame->showNormal();
        application.processEvents();
        theApp.m_pExitingDoc = nullptr;
        theApp.m_flags1 &= ~DWORD(F1_MAXMDI);
        lifecycleFrame->showMaximized();
        application.processEvents();
        REQUIRE(theApp.m_flags1 & F1_MAXMDI);
        lifecycleFrame->showMinimized();
        application.processEvents();
        REQUIRE(theApp.m_flags1 & F1_MAXMDI);

        theApp.m_pExitingDoc = lifecycleDocument;
        lifecycleFrame->showNormal();
        application.processEvents();
        REQUIRE(theApp.m_flags1 & F1_MAXMDI);
        theApp.m_pExitingDoc = nullptr;

        lifecycleFrame->showMaximized();
        application.processEvents();
        REQUIRE(theApp.m_flags1 & F1_MAXMDI);
        theApp.m_flags1 &= ~DWORD(F1_MAXMDI);
        QEvent maximizedActivation(QEvent::WindowActivate);
        QCoreApplication::sendEvent(
            lifecycleFrame, &maximizedActivation);
        REQUIRE(theApp.m_flags1 & F1_MAXMDI);
        lifecycleFrame->showNormal();
        application.processEvents();
        REQUIRE(!(theApp.m_flags1 & F1_MAXMDI));
        theApp.m_flags1 |= F1_MAXMDI;
        QEvent restoredActivation(QEvent::WindowActivate);
        QCoreApplication::sendEvent(
            lifecycleFrame, &restoredActivation);
        REQUIRE(!(theApp.m_flags1 & F1_MAXMDI));
        theApp.m_flags1 |= F1_MAXMDI;
        lifecycleFrame->move(lifecycleFrame->pos() + QPoint(3, 2));
        application.processEvents();
        REQUIRE(!(theApp.m_flags1 & F1_MAXMDI));

        theApp.m_flags1 &= ~DWORD(F1_MAXMDI);
        theApp.m_bEmbedded = true;
        lifecycleFrame->showMaximized();
        application.processEvents();
        REQUIRE(!(theApp.m_flags1 & F1_MAXMDI));
        theApp.m_bEmbedded = false;

        theApp.m_flags1 |= F1_MAXMDI;
        lifecycleFrame->hide();
        application.processEvents();
        REQUIRE(theApp.m_flags1 & F1_MAXMDI);

        const DWORD roundTripFlags1 = savedFlags1 | F1_MAXMDI;
        const bool savedSaveViewMode = theApp.m_bSaveViewMode;
        theApp.m_flags1 = roundTripFlags1;
        theApp.m_bSaveViewMode = false;
        REQUIRE(theApp.SaveToReg(FALSE));
        theApp.m_bSaveViewMode = savedSaveViewMode;
        stored.sync();
        REQUIRE(stored.value(key(QStringLiteral("Flags1"))).toUInt()
                == roundTripFlags1);
        stored.beginGroup(settingsRoot());
        QStringList keysAfterChildState = stored.childKeys();
        stored.endGroup();
        keysAfterChildState.sort();
        REQUIRE(keysAfterChildState == keysBeforeChildState);
        theApp.m_flags1 &= ~DWORD(F1_MAXMDI);
        REQUIRE(theApp.LoadFromReg());
        REQUIRE(theApp.m_flags1 == roundTripFlags1);

        lifecycleDocument->LoadDocData();
        theApp.m_pDoc = lifecycleDocument;
        REQUIRE(LookupDoc(QStringLiteral("#lifecycle"))
                == lifecycleDocument);
        QPointer<CChildFrame> deferredFrame = lifecycleFrame;
        REQUIRE(frame.CloseDocument(lifecycleDocument));
        REQUIRE(lifecycleDocument->IsCloseStarted());
        REQUIRE(theApp.m_pExitingDoc == lifecycleDocument);
        REQUIRE(LookupDoc(QStringLiteral("#lifecycle")) == nullptr);
        REQUIRE(partCount == 1);
        QString exitingChannel = QStringLiteral("#LiFeCyClE");
        REQUIRE(bKeyEventParam(
            exitingChannel, kepMyActivatedRoom));
        lifecycleDocument->OnCloseDocument();
        REQUIRE(partCount == 1);

        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
        REQUIRE(deferredFrame.isNull());
        REQUIRE(partCount == 1);
        REQUIRE(theApp.m_pDoc == nullptr);
        QString closedChannel = QStringLiteral("#lifecycle");
        REQUIRE(!bKeyEventParam(closedChannel, kepMyActivatedRoom));
        theApp.m_pExitingDoc = nullptr;
        theApp.m_bEmbedded = savedEmbedded;
        theApp.m_flags1 = savedFlags1;
        theApp.m_pMainWnd = nullptr;
    }

    {
        // The first placement may maximize a zero-height/hidden MDI client,
        // but that placement itself must not rewrite F1_MAXMDI.
        const DWORD savedFlags1 = theApp.m_flags1;
        const bool savedComicView = theApp.m_bComicView;
        theApp.m_flags1 &= ~DWORD(F1_MAXMDI);
        theApp.m_bComicView = false;
        CMainFrame hiddenFrame;
        theApp.m_pMainWnd = &hiddenFrame;
        CChatDoc* initialDocument = hiddenFrame.CreateNewDocument();
        REQUIRE(initialDocument != nullptr);
        application.processEvents();
        REQUIRE(hiddenFrame.GetMDIArea()->activeSubWindow() != nullptr);
        REQUIRE(hiddenFrame.GetMDIArea()->activeSubWindow()->isMaximized());
        REQUIRE(!(theApp.m_flags1 & F1_MAXMDI));
        theApp.m_pMainWnd = nullptr;
        theApp.m_bComicView = savedComicView;
        theApp.m_flags1 = savedFlags1;
    }
    theApp.m_pExitingDoc = nullptr;

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

    {
        EnterInfoState enterInfoState;
        const QString savedService = theApp.m_strConnectedService;
        const int savedViewMode = g_iViewMode;
        const bool savedSaveViewMode = theApp.m_bSaveViewMode;
        const bool savedLoadUrl = theApp.m_bLoadURL;
        const BOOL savedPrompt = g_bCXPrompt;

        CChatDoc document;
        document.m_bComicView = false;
        SetChatDoc(&document);
        QString fileName;
        BOOL fileNew = FALSE;
        REQUIRE(theApp.ProcessShellCommand(
            QStringLiteral(
                "\"mic://irc.example/#Room__text___secret\""),
            &fileName, &fileNew));
        REQUIRE(fileNew);
        REQUIRE(fileName.isEmpty());
        REQUIRE(g_enterInfo.m_strChannel
                == EncodeChan(QStringLiteral("#Room")));
        REQUIRE(g_enterInfo.m_strPassword
                == QStringLiteral("secret"));
        REQUIRE(g_iViewMode == VM_TEXT);
        REQUIRE(!theApp.m_bSaveViewMode);
        REQUIRE(!g_bCXPrompt);
        REQUIRE(theApp.m_bLoadURL);
        REQUIRE(document.m_fileType == FT_CCR);

        REQUIRE(theApp.ProcessShellCommand(
            QStringLiteral("irc://irc.example/"),
            &fileName, &fileNew));
        REQUIRE(fileNew);
        REQUIRE(fileName.isEmpty());
        REQUIRE(g_enterInfo.m_strChannel.isEmpty());
        REQUIRE(g_enterInfo.m_strPassword.isEmpty());
        REQUIRE(theApp.m_bLoadURL);

        REQUIRE(theApp.ProcessShellCommand(
            QStringLiteral("mic://irc.example/"),
            &fileName, &fileNew));
        REQUIRE(fileNew);
        REQUIRE(fileName.isEmpty());
        REQUIRE(g_enterInfo.m_strChannel.isEmpty());
        REQUIRE(g_enterInfo.m_strPassword.isEmpty());
        REQUIRE(theApp.m_bLoadURL);

        REQUIRE(theApp.ProcessShellCommand(
            QStringLiteral("MIC://irc.example/#CaseSensitive"),
            &fileName, &fileNew));
        REQUIRE(!fileNew);
        REQUIRE(fileName
                == QStringLiteral("MIC://irc.example/#CaseSensitive"));
        REQUIRE(theApp.ProcessShellCommand(
            QStringLiteral("irc://missing-room-separator"),
            &fileName, &fileNew));
        REQUIRE(!fileNew);
        REQUIRE(fileName
                == QStringLiteral("irc://missing-room-separator"));

        SetChatDoc(nullptr);
        theApp.m_strConnectedService = savedService;
        g_iViewMode = savedViewMode;
        theApp.m_bSaveViewMode = savedSaveViewMode;
        theApp.m_bLoadURL = savedLoadUrl;
        g_bCXPrompt = savedPrompt;
    }

    {
        CChatDoc document;
        document.m_bComicView = false;
        REQUIRE(document.OnNewDocument());
        SetChatDoc(&document);
        document.SetModifiedFlag(true);
        SHORT keepServer = 0;
        BOOL prompt = FALSE;
        bool sawCancel = false;
        answerNextMessage(QMessageBox::Cancel, &sawCancel);
        REQUIRE(ChatInitialize(&keepServer, &prompt));
        REQUIRE(sawCancel);
        REQUIRE(prompt);
        REQUIRE(document.IsModified());
        REQUIRE(!document.IsCloseStarted());
        SetChatDoc(nullptr);
    }

    {
        const bool savedMainLoopReady = theApp.m_bMainLoopReady;
        const bool savedComicView = theApp.m_bComicView;
        const BOOL savedPrompt = g_bCXPrompt;
        const SHORT savedKeepServer = g_nCXKeepServer;
        theApp.m_bComicView = false;
        theApp.m_bMainLoopReady = true;
        g_bCXPrompt = TRUE;
        g_nCXKeepServer = 0;

        CMainFrame frame;
        theApp.m_pMainWnd = &frame;
        CChatDoc* document = frame.CreateNewDocument();
        REQUIRE(document != nullptr);
        frame.show();

        int setupDialogs = 0;
        QTimer dialogDriver;
        dialogDriver.setInterval(1);
        QObject::connect(&dialogDriver, &QTimer::timeout,
                         [&setupDialogs] {
            if (auto* setup = dynamic_cast<CSetupDlg*>(
                    QApplication::activeModalWidget())) {
                ++setupDialogs;
                setup->reject();
            }
        });
        dialogDriver.start();
        application.processEvents();
        dialogDriver.stop();
        REQUIRE(setupDialogs == 1);
        REQUIRE(document->m_puiSelf != nullptr);
        REQUIRE(!document->m_history.isEmpty());
        REQUIRE(!document->IsModified());
        REQUIRE(g_bCXPrompt);
        dialogDriver.start();
        theApp.ScheduleDocumentInitialize(document);
        application.processEvents();
        dialogDriver.stop();
        REQUIRE(setupDialogs == 1);

        int retryStage = 0;
        QTimer retryDriver;
        retryDriver.setInterval(1);
        QObject::connect(&retryDriver, &QTimer::timeout,
                         [&retryStage] {
            QWidget* modal = QApplication::activeModalWidget();
            if (auto* message = qobject_cast<QMessageBox*>(modal)) {
                if (retryStage == 0) {
                    retryStage = 1;
                    message->accept();
                }
            } else if (auto* setup =
                           dynamic_cast<CSetupDlg*>(modal)) {
                if (retryStage == 1) {
                    retryStage = 2;
                    setup->reject();
                }
            }
        });
        retryDriver.start();
        theApp.OnConnectError();
        retryDriver.stop();
        REQUIRE(retryStage == 2);
        REQUIRE(document->GetConnectionStatus()
                == CX_DISCONNECTED);
        REQUIRE(document->m_puiSelf != nullptr);

        theApp.m_bMainLoopReady = false;
        theApp.m_pMainWnd = nullptr;
        theApp.m_pExitingDoc = nullptr;
        g_bCXPrompt = savedPrompt;
        g_nCXKeepServer = savedKeepServer;
        theApp.m_bComicView = savedComicView;
        theApp.m_bMainLoopReady = savedMainLoopReady;
    }
    theApp.m_pExitingDoc = nullptr;
    SetChatDoc(nullptr);
    const bool savedIntegrationComicView = theApp.m_bComicView;
    theApp.m_bComicView = false;

    {
        CMainFrame frame;
        theApp.m_pMainWnd = &frame;
        frame.show();
        CChatDoc* document = frame.CreateNewDocument();
        REQUIRE(document != nullptr);
        application.processEvents();
        const int childCount =
            frame.GetMDIArea()->subWindowList().size();
        const int tab = frame.GetTabBar()->FindTabNum(document);
        CChatDoc* active = frame.GetActiveDocument();
        const QString savedProfile = theApp.m_myProfile;

        document->SetModifiedFlag(true);
        const QVariantMap settingsBeforeCancel = settingsSnapshot(stored);
        theApp.m_myProfile =
            QStringLiteral("must not persist from cancelled close");
        bool sawCancel = false;
        answerNextMessage(QMessageBox::Cancel, &sawCancel);
        REQUIRE(!frame.close());
        REQUIRE(sawCancel);
        REQUIRE(frame.isVisible());
        REQUIRE(!document->IsCloseStarted());
        REQUIRE(document->IsModified());
        REQUIRE(frame.GetActiveDocument() == active);
        REQUIRE(frame.GetMDIArea()->subWindowList().size()
                == childCount);
        REQUIRE(frame.GetTabBar()->FindTabNum(document) == tab);
        REQUIRE(settingsSnapshot(stored) == settingsBeforeCancel);

        QTemporaryDir doomedDirectory;
        REQUIRE(doomedDirectory.isValid());
        document->m_bComicView = false;
        const QString doomedPath =
            doomedDirectory.filePath(QStringLiteral("close.ccc"));
        REQUIRE(document->DoSave(doomedPath, true));
        document->SetModifiedFlag(true);
        REQUIRE(QFile::remove(doomedPath));
        REQUIRE(QDir().rmdir(doomedDirectory.path()));
        const QVariantMap settingsBeforeFailedSave =
            settingsSnapshot(stored);
        theApp.m_myProfile =
            QStringLiteral("must not persist from failed save");
        bool sawFailedSave = false;
        answerNextMessage(QMessageBox::Save, &sawFailedSave);
        REQUIRE(!frame.close());
        REQUIRE(sawFailedSave);
        REQUIRE(!document->IsCloseStarted());
        REQUIRE(document->IsModified());
        REQUIRE(frame.GetActiveDocument() == active);
        REQUIRE(settingsSnapshot(stored) == settingsBeforeFailedSave);
        theApp.m_myProfile = savedProfile;
        document->SetModifiedFlag(false);
        theApp.m_pMainWnd = nullptr;
        theApp.m_pExitingDoc = nullptr;
    }
    theApp.m_pExitingDoc = nullptr;
    SetChatDoc(nullptr);

    {
        CMainFrame frame;
        theApp.m_pMainWnd = &frame;
        frame.show();
        CChatDoc* closedBeforeReusePrompt =
            frame.CreateNewDocument();
        CChatDoc* reuse = frame.CreateNewDocument();
        REQUIRE(closedBeforeReusePrompt != nullptr);
        REQUIRE(reuse != nullptr);
        closedBeforeReusePrompt->SetModifiedFlag(false);
        reuse->SetModifiedFlag(true);
        const int childCount =
            frame.GetMDIArea()->subWindowList().size();
        QPointer<CChildFrame> closedFrame;
        for (QMdiSubWindow* window
             : frame.GetMDIArea()->subWindowList()) {
            auto* child = dynamic_cast<CChildFrame*>(window);
            if (child
                && child->GetDocument()
                    == closedBeforeReusePrompt) {
                closedFrame = child;
                break;
            }
        }
        REQUIRE(!closedFrame.isNull());
        bool sawCancel = false;
        answerNextMessage(QMessageBox::Cancel, &sawCancel);
        REQUIRE(!CChatDoc::CleanupExistingWindows());
        REQUIRE(sawCancel);
        REQUIRE(!reuse->IsCloseStarted());
        REQUIRE(reuse->IsModified());
        REQUIRE(closedBeforeReusePrompt->IsCloseStarted());
        REQUIRE(frame.GetMDIArea()->subWindowList().size()
                == childCount - 1);
        REQUIRE(frame.GetActiveDocument() == reuse);
        QCoreApplication::sendPostedEvents(
            nullptr, QEvent::DeferredDelete);
        REQUIRE(closedFrame.isNull());
        REQUIRE(frame.GetMDIArea()->subWindowList().size()
                == childCount - 1);
        reuse->SetModifiedFlag(false);
        theApp.m_pMainWnd = nullptr;
        theApp.m_pExitingDoc = nullptr;
    }
    theApp.m_pExitingDoc = nullptr;
    SetChatDoc(nullptr);

    {
        CMainFrame frame;
        theApp.m_pMainWnd = &frame;
        frame.show();
        auto* nonIrcDocument = new CChatDoc;
        nonIrcDocument->m_bComicView = false;
        delete nonIrcDocument->m_proto;
        auto* nonIrcProtocol = new NonIrcProto;
        nonIrcProtocol->m_doc = nonIrcDocument;
        nonIrcDocument->m_proto = nonIrcProtocol;
        REQUIRE(nonIrcDocument->OnNewDocument());
        REQUIRE(frame.AddDocument(
            nonIrcDocument, true, true) != nullptr);
        application.processEvents();
        nonIrcDocument->SetModifiedFlag(true);
        const int childCount =
            frame.GetMDIArea()->subWindowList().size();
        REQUIRE(nonIrcDocument->m_proto->GetType() != PC_IRC);
        bool unexpectedPrompt = false;
        QTimer unexpectedPromptTimer;
        unexpectedPromptTimer.setSingleShot(true);
        QObject::connect(
            &unexpectedPromptTimer, &QTimer::timeout,
            [&unexpectedPrompt] {
                auto* prompt = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget());
                if (!prompt) return;
                unexpectedPrompt = true;
                prompt->done(QMessageBox::Cancel);
            });
        unexpectedPromptTimer.start(0);
        const bool cleanupResult =
            CChatDoc::CleanupExistingWindows();
        unexpectedPromptTimer.stop();
        REQUIRE(cleanupResult);
        REQUIRE(!unexpectedPrompt);
        REQUIRE(!nonIrcDocument->IsCloseStarted());
        REQUIRE(nonIrcDocument->IsModified());
        REQUIRE(frame.GetMDIArea()->subWindowList().size()
                == childCount);
        nonIrcDocument->SetModifiedFlag(false);
        theApp.m_pMainWnd = nullptr;
        theApp.m_pExitingDoc = nullptr;
    }
    theApp.m_pExitingDoc = nullptr;
    SetChatDoc(nullptr);

    {
        CMainFrame frame;
        theApp.m_pMainWnd = &frame;
        frame.show();
        CChatDoc* first = frame.CreateNewDocument();
        CChatDoc* second = frame.CreateNewDocument();
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        first->SetModifiedFlag(true);
        second->SetModifiedFlag(true);
        const int childCount =
            frame.GetMDIArea()->subWindowList().size();
        bool sawCancel = false;
        answerNextMessage(QMessageBox::Cancel, &sawCancel);
        REQUIRE(!CChatDoc::CleanupExistingWindows());
        REQUIRE(sawCancel);
        REQUIRE(!first->IsCloseStarted());
        REQUIRE(!second->IsCloseStarted());
        REQUIRE(frame.GetMDIArea()->subWindowList().size()
                == childCount);
        first->SetModifiedFlag(false);
        second->SetModifiedFlag(false);
        theApp.m_pMainWnd = nullptr;
        theApp.m_pExitingDoc = nullptr;
    }
    theApp.m_pExitingDoc = nullptr;
    SetChatDoc(nullptr);

    {
        CMainFrame frame;
        theApp.m_pMainWnd = &frame;
        frame.show();
        CChatDoc* document = frame.CreateNewDocument();
        REQUIRE(document != nullptr);
        const QString channel =
            EncodeChan(QStringLiteral("#replacement"));
        document->m_proto->m_strChannel = channel;
        document->m_proto->SetConnectionStatus(CX_DISCONNECTED);
        document->SetModifiedFlag(true);
        application.processEvents();
        bool sawCancel = false;
        answerNextMessage(QMessageBox::Cancel, &sawCancel);
        REQUIRE(bSwitchToRoom(
            QStringLiteral("#replacement"), QString(),
            QString(), 0L, TRUE));
        REQUIRE(sawCancel);
        REQUIRE(!document->IsCloseStarted());
        REQUIRE(document->IsModified());
        REQUIRE(LookupDoc(channel) == document);
        REQUIRE(frame.GetActiveDocument() == document);
        document->SetModifiedFlag(false);
        theApp.m_pMainWnd = nullptr;
        theApp.m_pExitingDoc = nullptr;
    }
    theApp.m_pExitingDoc = nullptr;
    SetChatDoc(nullptr);

    {
        const DWORD savedFlags0 = theApp.m_flags0;
        theApp.m_flags0 |= F0_SHOWSTATUSWINDOW;
        CMainFrame frame;
        theApp.m_pMainWnd = &frame;
        CChatDoc* statusDocument = frame.CreateStatusWindow();
        REQUIRE(statusDocument != nullptr);
        frame.show();
        frame.ShowStatusWindow(true);
        application.processEvents();
        CChildFrame* statusChild = nullptr;
        for (QMdiSubWindow* window :
             frame.GetMDIArea()->subWindowList()) {
            auto* child = dynamic_cast<CChildFrame*>(window);
            if (child && child->GetDocument() == statusDocument) {
                statusChild = child;
                break;
            }
        }
        REQUIRE(statusChild != nullptr);
        REQUIRE(statusChild->isVisible());
        REQUIRE(!statusChild->close());
        application.processEvents();
        REQUIRE(!statusChild->isVisible());
        REQUIRE(!statusDocument->IsCloseStarted());
        REQUIRE(g_docs.contains(statusDocument));
        REQUIRE(!(theApp.m_flags0 & F0_SHOWSTATUSWINDOW));
        theApp.m_pMainWnd = nullptr;
        theApp.m_pExitingDoc = nullptr;
        theApp.m_flags0 = savedFlags0;
    }
    theApp.m_pExitingDoc = nullptr;
    SetChatDoc(nullptr);
    theApp.m_bComicView = savedIntegrationComicView;

    return 0;
}
