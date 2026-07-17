// Ported from v2.5-beta-1-modern/setupdlg.cpp.
// QDialog/QWidget replace the MFC property sheet and edit controls. The page
// split, resource text, validation, defaults and commit order follow source.

#include "setupdlg.h"

#include "actions.h"
#include "chat.h"
#include "chatbars.h"
#include "defines.h"
#include "ircproto.h"
#include "mainfrm.h"
#include "originalassets.h"
#include "originalsettings.h"
#include "panel.h"
#include "proppage.h"
#include "protsupp.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShowEvent>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <array>
#include <cstring>

namespace {
const char* utf8Pointer(const QString& value, QByteArray& storage)
{
    storage = value.toUtf8();
    return storage.constData();
}

class DialogUnitMapper {
public:
    explicit DialogUnitMapper(const QFont& font)
    {
        const QFontMetrics metrics(font);
        const QString alphabet = QStringLiteral(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        m_baseX = qMax(1, (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
        m_baseY = qMax(1, metrics.height());
    }

    int x(int dlu) const { return (dlu * m_baseX + 2) / 4; }
    int y(int dlu) const { return (dlu * m_baseY + 4) / 8; }
    QRect rect(const OriginalDialogControl& control) const
    {
        return {x(control.x), y(control.y),
                x(control.width), y(control.height)};
    }

private:
    int m_baseX = 1;
    int m_baseY = 1;
};

const OriginalDialogControl* dialogControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

void placeDialogControl(QWidget* widget, const OriginalDialogResource& dialog,
                        const DialogUnitMapper& mapper,
                        const QString& identifier, int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = dialogControl(
            dialog, identifier, occurrence)) {
        widget->setGeometry(mapper.rect(*control));
        widget->setVisible(control->visible);
    }
}

void SaveMacros(QSettings& settings)
{
    settings.beginGroup(originalSettingsRoot()
                        + QStringLiteral("/Macros"));
    std::array<char, (MAX_FORMATTINGPERBYTE + 1) * MAX_INPUTLEN> buffer{};
    for (INT index = 0; index < NMACROS; ++index) {
        const QString key = QString::number(index);
        if (!theApp.m_macros[index].m_bDefined) {
            settings.remove(key);
            continue;
        }
        const INT size = theApp.m_macros[index].Serialize(
            buffer.data(), static_cast<INT>(buffer.size()));
        if (size >= 0)
            settings.setValue(key, QByteArray(buffer.data(), size));
    }
    settings.endGroup();
}

void LoadMacros(QSettings& settings)
{
    settings.beginGroup(originalSettingsRoot()
                        + QStringLiteral("/Macros"));
    constexpr qsizetype maximum =
        (MAX_FORMATTINGPERBYTE + 1) * MAX_INPUTLEN;
    for (INT index = 0; index < NMACROS; ++index) {
        const QString key = QString::number(index);
        if (!settings.contains(key)) continue;
        const QByteArray stored = settings.value(key).toByteArray();
        if (stored.isEmpty() || stored.size() > maximum) continue;
        const qsizetype firstEnd = stored.indexOf('\0');
        if (firstEnd < 0 || firstEnd + 1 >= stored.size()) continue;
        if (stored.indexOf('\0', firstEnd + 1) < 0) continue;
        theApp.m_macros[index].UnSerialize(stored.constData());
    }
    settings.endGroup();
}
}

class CSetupPage final : public QWidget {
public:
    explicit CSetupPage(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_favorites(new QComboBox(this))
        , m_server(new CChatServiceComboBox(this))
        , m_joinRoom(new QRadioButton(originalDialogControlText(
              QStringLiteral("IDD_SETUPDIALOG"),
              QStringLiteral("IDC_CONCHAN")), this))
        , m_listRooms(new QRadioButton(originalDialogControlText(
              QStringLiteral("IDD_SETUPDIALOG"),
              QStringLiteral("IDC_LISTCHAN")), this))
        , m_connectOnly(new QRadioButton(originalDialogControlText(
              QStringLiteral("IDD_SETUPDIALOG"),
              QStringLiteral("IDC_CONNECTONLY")), this))
        , m_channel(new QLineEdit(this))
    {
        m_favorites->setEnabled(false);
        m_server->setEditable(true);
        m_server->lineEdit()->setMaxLength(100);
        m_strService = theApp.m_strConnectedService;
        CChatService selectedService(m_strService);
        QString serverDisplay = selectedService.GetDisplayName();
        if (serverDisplay.isEmpty()) {
            CChatService* firstService = nullptr;
            if (theApp.m_listChatServices.EnumServices(firstService)) {
                serverDisplay = firstService->GetDisplayName();
                firstService->FormatAsServiceName(m_strService);
            }
        }
        m_server->SetServiceList(&theApp.m_listChatServices);
        m_server->Fill(TRUE);
        const int selectedIndex = m_server->findText(
            serverDisplay, Qt::MatchExactly | Qt::MatchCaseSensitive);
        if (selectedIndex >= 0) m_server->setCurrentIndex(selectedIndex);
        else m_server->setEditText(serverDisplay);
        m_channel->setText(theApp.m_myChannel);
        m_channel->setMaxLength(MAX_IRCXCHANNAME);
        m_channel->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[^,\\s\\r\\n\\a]*")), m_channel));

        switch (theApp.m_iOnConnectAction) {
        case CA_ROOMLIST: m_listRooms->setChecked(true); break;
        case CA_NOACTION: m_connectOnly->setChecked(true); break;
        default: m_joinRoom->setChecked(true); break;
        }

        auto* layout = new QGridLayout(this);
        auto* welcome = new QLabel(originalDialogControlText(
            QStringLiteral("IDD_SETUPDIALOG"),
            QStringLiteral("IDC_STATIC"), 0), this);
        welcome->setWordWrap(true);
        layout->addWidget(welcome, 0, 0, 1, 3);
        if (theApp.m_bComicView) {
            auto* comicWelcome = new QLabel(originalDialogControlText(
                QStringLiteral("IDD_SETUPDIALOG"),
                QStringLiteral("IDC_COMICSWELCOME")), this);
            comicWelcome->setWordWrap(true);
            layout->addWidget(comicWelcome, 1, 0, 1, 3);
        }
        layout->addWidget(new QLabel(originalDialogControlText(
            QStringLiteral("IDD_SETUPDIALOG"),
            QStringLiteral("IDC_STATIC"), 1), this), 2, 0);
        layout->addWidget(m_favorites, 3, 0, 1, 3);
        layout->addWidget(new QLabel(originalDialogControlText(
            QStringLiteral("IDD_SETUPDIALOG"),
            QStringLiteral("IDC_STATIC"), 2), this), 4, 0);
        layout->addWidget(m_server, 5, 0, 1, 3);
        auto* separator = new QFrame(this);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        layout->addWidget(separator, 6, 0, 1, 3);
        layout->addWidget(m_joinRoom, 7, 0, 1, 2);
        layout->addWidget(m_channel, 7, 2);
        layout->addWidget(m_listRooms, 8, 0, 1, 3);
        layout->addWidget(m_connectOnly, 9, 0, 1, 3);
        layout->setRowStretch(10, 1);

        auto updateChannel = [this] {
            m_channel->setEnabled(m_joinRoom->isChecked());
        };
        connect(m_joinRoom, &QRadioButton::toggled, this, updateChannel);
        connect(m_listRooms, &QRadioButton::toggled, this, updateChannel);
        connect(m_connectOnly, &QRadioButton::toggled, this, updateChannel);
        connect(m_server, &QComboBox::currentIndexChanged, this,
                [this](int index) {
            CChatService* service = m_server->GetServiceAt(index);
            if (service) service->FormatAsServiceName(m_strService);
            else m_strService.clear();
        });
        connect(m_server->lineEdit(), &QLineEdit::textEdited, this,
                [this](const QString&) { m_strService.clear(); });
        updateChannel();
    }

    QString server() const { return m_server->currentText(); }
    QString channel() const { return m_channel->text(); }
    int onConnectAction() const
    {
        if (m_listRooms->isChecked()) return CA_ROOMLIST;
        if (m_connectOnly->isChecked()) return CA_NOACTION;
        return CA_JOINROOM;
    }
    bool validate()
    {
        const QString serverText = server();
        if (serverText.isEmpty() || serverText.startsWith(QLatin1Char('.'))
            || serverText.startsWith(QLatin1Char('/'))) {
            m_server->setFocus();
            return false;
        }
        if (onConnectAction() == CA_JOINROOM && channel().isEmpty()) {
            m_channel->setFocus();
            return false;
        }
        return true;
    }
    void apply()
    {
        ChatSetServer(m_strService.isEmpty() ? server() : m_strService);
        ChatSetChannel(channel());
        theApp.m_iOnConnectAction = onConnectAction();
    }

private:
    QComboBox* m_favorites = nullptr;
    CChatServiceComboBox* m_server = nullptr;
    QRadioButton* m_joinRoom = nullptr;
    QRadioButton* m_listRooms = nullptr;
    QRadioButton* m_connectOnly = nullptr;
    QLineEdit* m_channel = nullptr;
    QString m_strService;
};

void CChatApp::InitVals()
{
    m_charSet = ANSI_CHARSET;
    m_flags0 = 0;
    m_flags1 = ~DWORD{0};
    m_bVIPMode = false;
    m_bPlaySounds = true;
    m_bAcceptWhispers = true;
    m_bAllowInvites = true;
    m_bAllowFileTX = true;
    m_bNoMIDI = false;
    m_bAcceptNMCalls = true;
    m_bShowIdentity = true;
    m_bIconMembers = true;
    m_bAutoDownloadAvatars = false;
    m_bAutoDownloadBackdrops = true;
    m_bSaveViewMode = true;
    m_bAway = false;
    m_bAwayPrompt = false;
    m_bInSearch = false;
    m_bListRegistered = false;
    m_bLoginNotifsShown = false;
    m_pRoomList = nullptr;
    m_pUserList = nullptr;
    m_rectWhisper = QRect();
    m_rectNotifs = QRect();
    m_iAutoPage = -1;
    m_iGreetingType = AGT_NONE;
    m_uFloodFlags = FLOOD_IGNORE;
    m_uFloodCount = 8;
    m_uFloodInterval = 8;
    m_myRealName = originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME"));
    m_myChannel = originalResourceString(QStringLiteral("IDS_DEFAULT_CHANNEL"));
    m_myName = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
    m_myNick = m_myName;
    m_strEmail.clear();
    m_strHomePage.clear();
    m_strFavoritesDir.clear();
    m_strChatRooms.clear();
    m_strFileTXDir.clear();
    m_soundPath.clear();

    m_strGreetingMesg = originalResourceString(QStringLiteral("IDS_DEFAULTGREETING"));
    m_strGreetingMesg.replace(QStringLiteral("%1"),
                              originalResourceString(QStringLiteral("IDS_USERVARIABLE")));
    m_strGreetingMesg.replace(QStringLiteral("%2"),
                              originalResourceString(QStringLiteral("IDS_ROOMVARIABLE")));

    m_dynaRules.SetEKPFunction(bKeyEventParam);
    m_dynaRules.SetERPFunction(bRndEventParam);
    m_dynaRules.SetGEKPFunction(StrGetKeyEventParam);
    m_dynaRules.SetGAKPFunction(StrGetKeyActionParam);
    m_dynaRules.SetExecuteActionFunction(bExecuteAction);
    m_dynaRules.SetRuleFailureFunction(bReportRuleFailure);
    m_dynaRules.SetDaemonQueryFunction(bRuleDaemonQuery);
    m_dynaRules.SetRulesData(&m_rulesData);
    m_dynaRules.SetDelayedRules(&m_delayedRules);
    m_delayedRules.SetExecuteActionFunction(bExecuteAction);
    m_dynaNotifs.SetDisplayNotificationsFunction(bDisplayNotifications);
    m_dynaNotifs.SetSignalNewUpdateFunction(bSignalNewUpdate);
    m_dynaNotifs.SetDaemonQueryFunction(bNotifDaemonQuery);
}

BOOL CChatApp::LoadFromReg()
{
    InitVals();

    QSettings settings = originalUserSettings();
    settings.beginGroup(originalSettingsRoot());

    auto readInt = [&settings](const char* name, int& value) {
        const QString key = QString::fromLatin1(name);
        if (settings.contains(key)) value = settings.value(key).toInt();
    };
    auto readBool = [&settings](const char* name, bool& value) {
        const QString key = QString::fromLatin1(name);
        if (settings.contains(key)) value = settings.value(key).toBool();
    };
    auto readString = [&settings](const char* name, QString& value) {
        const QString key = QString::fromLatin1(name);
        if (settings.contains(key)) value = settings.value(key).toString();
    };

    readInt("XFrame", m_xFrame);
    readInt("YFrame", m_yFrame);
    readInt("CXFrame", m_cxFrame);
    readInt("CYFrame", m_cyFrame);
    readInt("Maximized", m_maxedFrame);

    int value = 0;
    if (settings.contains(QStringLiteral("UPNLWidth"))) {
        value = settings.value(QStringLiteral("UPNLWidth")).toInt();
        CUnitPanelPage::SetUnitPanelWidth(value);
    }
    if (settings.contains(QStringLiteral("UPNLHeight"))) {
        value = settings.value(QStringLiteral("UPNLHeight")).toInt();
        CUnitPanelPage::SetUnitPanelHeight(value);
    }
    if (settings.contains(QStringLiteral("UnitsWide"))) {
        value = settings.value(QStringLiteral("UnitsWide")).toInt();
        CUnitPanelPage::SetUnitPanelsPerRow(value);
    }

    readString("FavoritesDir", m_strFavoritesDir);
    readString("LastFavorite", m_strChatRooms);
    readString("FileTXDir", m_strFileTXDir);
    readString("IRCServer", m_strConnectedService);
    readString("IRCChannel", m_myChannel);

    if (settings.contains(QStringLiteral("Name"))) {
        const QString name = settings.value(QStringLiteral("Name"))
                                 .toString().left(MAX_NICKINPUT);
        m_myName = name;
        m_myNick = name;
    }
    if (settings.contains(QStringLiteral("RealName"))) {
        m_myRealName = settings.value(QStringLiteral("RealName"))
                           .toString().left(MAX_REALNAMEINPUT);
    }
    if (settings.contains(QStringLiteral("Email"))) {
        m_strEmail = settings.value(QStringLiteral("Email"))
                         .toString().left(MAX_EMAILINPUT);
    }
    if (settings.contains(QStringLiteral("ToolBarState")))
        m_pbCoolBarState = settings.value(
            QStringLiteral("ToolBarState")).toByteArray();
    if (settings.contains(QStringLiteral("HomePage"))) {
        m_strHomePage = settings.value(QStringLiteral("HomePage"))
                            .toString().left(MAX_HOMEPAGEINPUT);
    }
    readString(szProfileValName, m_myProfile);
    readString("AwayMsg", m_strAwayMessage);
    readString("Character", m_myCharacterName);
    readString("Backdrop", m_lastBackDrop);

    readBool("ShowComicView", m_bComicView);
    if (settings.contains(QStringLiteral("ComicsData")))
        SetSendComicsData(settings.value(
            QStringLiteral("ComicsData")).toBool());
    readBool("MemberListStyle", m_bIconMembers);
    readBool("PromptForSave", m_bPrompt);
    readBool("AcceptWhispers", m_bAcceptWhispers);
    readBool("AutoDownloadChars", m_bAutoDownloadAvatars);
    readBool("AutoDownloadBackdrops", m_bAutoDownloadBackdrops);

    if (settings.contains(QStringLiteral("FloodControl"))) {
        value = settings.value(QStringLiteral("FloodControl")).toInt();
        const UCHAR interval = static_cast<UCHAR>(value & 0xff);
        const UCHAR count = static_cast<UCHAR>((value >> 8) & 0xff);
        m_uFloodFlags = static_cast<UCHAR>((value >> 16) & 0xff);
        if (interval && count) {
            m_uFloodInterval = interval;
            m_uFloodCount = count;
        }
    }
    if (settings.contains(QStringLiteral("RulesControl"))) {
        value = settings.value(QStringLiteral("RulesControl")).toInt();
        const UCHAR interval = static_cast<UCHAR>(value & 0xff);
        const UCHAR occurrences = static_cast<UCHAR>((value >> 8) & 0xff);
        if (interval && occurrences)
            m_dynaRules.SetFloodParams(interval, occurrences);
    }

    if (settings.contains(QStringLiteral("ComicsFont"))) {
        const QVariant stored = settings.value(QStringLiteral("ComicsFont"));
        if (stored.canConvert<QFont>()) m_comicsFont = stored.value<QFont>();
    }
    if (settings.contains(QStringLiteral("ComicsColor"))) {
        m_comicsColor = static_cast<COLORREF>(
            settings.value(QStringLiteral("ComicsColor")).toUInt());
    }
    readString("SoundPath", m_soundPath);
    if (settings.contains(QStringLiteral("AutoGreeting"))) {
        m_strGreetingMesg = settings.value(
            QStringLiteral("AutoGreeting")).toString();
        bReplaceMacroTokens(m_strGreetingMesg, TRUE);
    }
    readInt("AutoGreetType", m_iGreetingType);

    const QByteArray regularFonts = settings.value(
        QStringLiteral("TextFonts")).toByteArray();
    if (settings.contains(QStringLiteral("TextFonts"))
        && regularFonts.size()
            == static_cast<qsizetype>(sizeof(CHARFORMAT)
                                      * NREGULARFONTS)) {
        std::memcpy(m_cfArray, regularFonts.constData(),
                    sizeof(CHARFORMAT) * NREGULARFONTS);
        m_bCfInitialized = true;
        if (m_cfArray[2].dwMask & CFM_COLOR)
            m_textColor = m_cfArray[2].crTextColor;
    }

    const QByteArray highlightedFonts = settings.value(
        QStringLiteral("HighlightedTextFonts")).toByteArray();
    if (settings.contains(QStringLiteral("HighlightedTextFonts"))
        && highlightedFonts.size()
            == static_cast<qsizetype>(sizeof(CHARFORMAT)
                                      * NHIGHLIGHTEDFONTS)) {
        std::memcpy(&m_cfArray[NREGULARFONTS],
                    highlightedFonts.constData(),
                    sizeof(CHARFORMAT) * NHIGHLIGHTEDFONTS);
        m_bCfHLInitialized = true;
    }

    readInt("HostHighlight", m_iHostHighlight);
    readInt("TextSpacing", m_textSpacing);
    readBool("ShowArrivals", m_bShowArrivals);
    readBool("AllowInvites", m_bAllowInvites);
    readBool("AllowFileTXs", m_bAllowFileTX);
    readInt("ShowBars", m_iShowBars);
    if ((m_iShowBars & (SB_TOOLBAR | SB_TOOLBAR_OLDREAD)) == SB_TOOLBAR)
        m_iShowBars |= SB_TOOLBAR_ANY | SB_TOOLBAR_OLDREAD;
    readBool("PlaySounds", m_bPlaySounds);
    readBool("NoMIDI", m_bNoMIDI);
    readBool("AcceptNMCalls", m_bAcceptNMCalls);
    readBool("ShowIdentity", m_bShowIdentity);
    readBool("ListRegistered", m_bListRegistered);

    if (settings.contains(QStringLiteral("Flags1")))
        m_flags1 = settings.value(QStringLiteral("Flags1")).toUInt();
    if (settings.contains(QStringLiteral("Flags0")))
        m_flags0 = settings.value(QStringLiteral("Flags0")).toUInt();
    if (settings.contains(QStringLiteral("WhisperDims")))
        m_rectWhisper = settings.value(
            QStringLiteral("WhisperDims")).toRect();
    if (settings.contains(QStringLiteral("NotifDims")))
        m_rectNotifs = settings.value(QStringLiteral("NotifDims")).toRect();
    readInt("OnConnect", m_iOnConnectAction);
    settings.endGroup();

    m_listChatServices.ReadFromRegistry();
    LoadMacros(settings);
    m_dynaRules.bLoadRulesFromReg();
    m_dynaNotifs.bLoadNotifsFromReg();
    return TRUE;
}

BOOL CChatApp::SaveToReg(BOOL shortSave)
{
    QSettings settings = originalUserSettings();
    settings.beginGroup(originalSettingsRoot());

    if (shortSave) {
        CMainFrame* frame = m_pMainWnd.data();
        if (frame) {
            QRect normal = frame->normalGeometry();
            if (!normal.isValid()) normal = frame->geometry();
            m_xFrame = normal.x();
            m_yFrame = normal.y();
            m_cxFrame = normal.width();
            m_cyFrame = normal.height();
            m_maxedFrame = frame->isMaximized() ? 1 : 0;

            settings.setValue(QStringLiteral("XFrame"), m_xFrame);
            settings.setValue(QStringLiteral("YFrame"), m_yFrame);
            settings.setValue(QStringLiteral("CXFrame"), m_cxFrame);
            settings.setValue(QStringLiteral("CYFrame"), m_cyFrame);
            settings.setValue(QStringLiteral("Maximized"), m_maxedFrame);

            m_iShowBars &= SB_TOOLBAR_ANY | SB_TOOLBAR | SB_TOOLBAR_OLDREAD;
            if (frame->statusBar()->isVisible()) m_iShowBars |= SB_STATUSBAR;
            settings.setValue(QStringLiteral("ShowBars"), m_iShowBars);

            if (CChatToolBar* toolbar = frame->GetToolBar()) {
                toolbar->SaveStateToBuffer(&m_pbCoolBarState);
                settings.setValue(QStringLiteral("ToolBarState"),
                                  m_pbCoolBarState);
            }
        }
        settings.endGroup();
        settings.sync();
        return TRUE;
    }

    settings.setValue(QStringLiteral("UPNLWidth"),
                      CUnitPanelPage::GetUnitPanelWidth());
    settings.setValue(QStringLiteral("UPNLHeight"),
                      CUnitPanelPage::GetUnitPanelHeight());
    settings.setValue(QStringLiteral("UnitsWide"),
                      CUnitPanelPage::GetUnitPanelsPerRow());
    settings.setValue(QStringLiteral("FavoritesDir"), m_strFavoritesDir);
    settings.setValue(QStringLiteral("LastFavorite"), m_strChatRooms);
    settings.setValue(QStringLiteral("FileTXDir"), m_strFileTXDir);
    settings.setValue(QStringLiteral("IRCServer"), m_strConnectedService);
    settings.setValue(QStringLiteral("IRCChannel"), m_myChannel);
    settings.setValue(QStringLiteral("Name"), m_myName);
    settings.setValue(QStringLiteral("RealName"), m_myRealName);
    settings.setValue(QStringLiteral("Email"), m_strEmail);

    QString storedGreeting = m_strGreetingMesg;
    bReplaceMacroTokens(storedGreeting, FALSE);
    settings.setValue(QStringLiteral("AutoGreeting"), storedGreeting);
    settings.setValue(QStringLiteral("AutoGreetType"), m_iGreetingType);
    settings.setValue(QStringLiteral("OnConnect"), m_iOnConnectAction);

    settings.setValue(QStringLiteral("HomePage"), m_strHomePage);
    if (!m_myProfile.isEmpty())
        settings.setValue(QString::fromLatin1(szProfileValName), m_myProfile);
    settings.setValue(QStringLiteral("AwayMsg"), m_strAwayMessage);
    if (m_bComicView) {
        settings.setValue(QStringLiteral("Character"), m_myCharacterName);
        settings.setValue(QStringLiteral("Backdrop"), m_lastBackDrop);
    }
    settings.setValue(QStringLiteral("ServersMigrated"), true);
    if (m_bSaveViewMode)
        settings.setValue(QStringLiteral("ShowComicView"), m_bComicView);
    settings.setValue(QStringLiteral("ComicsData"), GetSendComicsData());
    settings.setValue(QStringLiteral("MemberListStyle"), m_bIconMembers);
    settings.setValue(QStringLiteral("PromptForSave"), m_bPrompt);
    settings.setValue(QStringLiteral("AcceptWhispers"), m_bAcceptWhispers);
    settings.setValue(QStringLiteral("AutoDownloadChars"),
                      m_bAutoDownloadAvatars);
    settings.setValue(QStringLiteral("AutoDownloadBackdrops"),
                      m_bAutoDownloadBackdrops);

    const int floodControl = m_uFloodInterval
        + (static_cast<int>(m_uFloodCount) << 8)
        + (static_cast<int>(m_uFloodFlags) << 16);
    settings.setValue(QStringLiteral("FloodControl"), floodControl);
    const int rulesControl = m_dynaRules.GetFloodingInterval()
        + (static_cast<int>(m_dynaRules.GetFloodingOccurrences()) << 8);
    settings.setValue(QStringLiteral("RulesControl"), rulesControl);
    settings.setValue(QStringLiteral("ComicsFont"), m_comicsFont);
    settings.setValue(QStringLiteral("ComicsColor"),
                      static_cast<quint32>(m_comicsColor));
    settings.setValue(QStringLiteral("SoundPath"), m_soundPath);
    settings.setValue(QStringLiteral("TextFonts"),
        m_bCfInitialized
            ? QByteArray(reinterpret_cast<const char*>(m_cfArray),
                         sizeof(CHARFORMAT) * NREGULARFONTS)
            : QByteArray());
    settings.setValue(QStringLiteral("HighlightedTextFonts"),
        m_bCfHLInitialized
            ? QByteArray(reinterpret_cast<const char*>(
                             &m_cfArray[NREGULARFONTS]),
                         sizeof(CHARFORMAT) * NHIGHLIGHTEDFONTS)
            : QByteArray());
    settings.setValue(QStringLiteral("HostHighlight"), m_iHostHighlight);
    settings.setValue(QStringLiteral("TextSpacing"), m_textSpacing);
    settings.setValue(QStringLiteral("ShowArrivals"), m_bShowArrivals);
    settings.setValue(QStringLiteral("AllowInvites"), m_bAllowInvites);
    settings.setValue(QStringLiteral("AllowFileTXs"), m_bAllowFileTX);
    settings.setValue(QStringLiteral("PlaySounds"), m_bPlaySounds);
    settings.setValue(QStringLiteral("AcceptNMCalls"), m_bAcceptNMCalls);
    settings.setValue(QStringLiteral("ShowIdentity"), m_bShowIdentity);
    settings.setValue(QStringLiteral("ListRegistered"), m_bListRegistered);
    settings.setValue(QStringLiteral("Flags1"), static_cast<quint32>(m_flags1));
    m_flags0 |= F0_ALREADYRUN;
    settings.setValue(QStringLiteral("Flags0"), static_cast<quint32>(m_flags0));
    settings.setValue(QStringLiteral("WhisperDims"), m_rectWhisper);
    settings.setValue(QStringLiteral("NotifDims"), m_rectNotifs);
    settings.endGroup();

    SaveMacros(settings);
    settings.sync();
    m_listChatServices.WriteToRegistry();
    m_listChatServices.MarkServersMigrated();
    m_dynaRules.bSaveRulesToReg();
    m_dynaNotifs.bSaveNotifsToReg();
    return TRUE;
}

CSetupDlg::CSetupDlg(QWidget* parent)
    : QDialog(parent)
    , m_tabs(new QTabWidget(this))
    , m_setupPage(new CSetupPage(m_tabs))
    , m_personalPage(new CPersonalPage(m_tabs))
    , m_characterPage(new CCharacterPage(m_tabs))
    , m_backgroundPage(new CBackgroundPage(m_tabs))
{
    setWindowTitle(originalResourceString(QStringLiteral("IDS_CONNECTION")));
    m_tabs->addTab(m_setupPage, originalDialogCaption(
        QStringLiteral("IDD_SETUPDIALOG")));
    m_tabs->addTab(m_personalPage, originalDialogCaption(
        QStringLiteral("IDD_PERSONALPAGE_IRC")));
    if (theApp.m_bComicView) {
        m_tabs->addTab(m_characterPage, originalDialogCaption(
            QStringLiteral("IDD_CHARACTERPAGE")));
        m_tabs->addTab(m_backgroundPage, originalDialogCaption(
            QStringLiteral("IDD_BACKGROUNDPAGE")));
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &CSetupDlg::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);
    layout->addWidget(buttons);
}

void CSetupDlg::accept()
{
    if (!m_setupPage->validate()) {
        m_tabs->setCurrentWidget(m_setupPage);
        return;
    }
    if (!m_personalPage->validate()) {
        m_tabs->setCurrentWidget(m_personalPage);
        return;
    }

    m_setupPage->apply();
    m_personalPage->apply();
    if (theApp.m_bComicView) {
        m_characterPage->apply();
        m_backgroundPage->apply();
    }
    QDialog::accept();
}

QString CSetupDlg::server() const
{
    return QString::fromUtf8(GetMyServer());
}
QString CSetupDlg::nickname() const { return m_personalPage->nickname(); }
QString CSetupDlg::realName() const { return m_personalPage->realName(); }
QString CSetupDlg::channel() const { return m_setupPage->channel(); }
int CSetupDlg::onConnectAction() const { return m_setupPage->onConnectAction(); }

CChannelDlg::CChannelDlg(QWidget* parent)
    : QDialog(parent)
    , m_channel(new QLineEdit(this))
    , m_password(new QLineEdit(this))
{
    const QString resourceIdentifier = QStringLiteral("IDD_CHANNEL");
    const OriginalDialogResource dialog = originalDialogResource(
        resourceIdentifier);
    QFont dialogFont(dialog.fontFamily);
    if (dialog.fontPointSize > 0) dialogFont.setPointSize(dialog.fontPointSize);
    setFont(dialogFont);
    const DialogUnitMapper mapper(dialogFont);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    auto* channelLabel = new QLabel(originalDialogControlText(
        resourceIdentifier, QStringLiteral("IDC_STATIC"), 0), this);
    channelLabel->setWordWrap(true);
    channelLabel->setBuddy(m_channel);
    placeDialogControl(channelLabel, dialog, mapper,
                       QStringLiteral("IDC_STATIC"), 0);
    placeDialogControl(m_channel, dialog, mapper,
                       QStringLiteral("IDC_NEWCHANNEL"));
    auto* passwordLabel = new QLabel(originalDialogControlText(
        resourceIdentifier, QStringLiteral("IDC_STATIC"), 1), this);
    passwordLabel->setBuddy(m_password);
    placeDialogControl(passwordLabel, dialog, mapper,
                       QStringLiteral("IDC_STATIC"), 1);
    placeDialogControl(m_password, dialog, mapper,
                       QStringLiteral("IDC_PASSWORD"));

    m_password->setMaxLength(MAX_CHANNELPWD);
    m_channel->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[^,\\s\\r\\n\\a]*")), m_channel));
    m_password->setEchoMode(QLineEdit::Password);
    m_ok = new QPushButton(originalDialogControlText(
        resourceIdentifier, QStringLiteral("IDOK")), this);
    m_ok->setDefault(true);
    placeDialogControl(m_ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(originalDialogControlText(
        resourceIdentifier, QStringLiteral("IDCANCEL")), this);
    placeDialogControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    m_ok->setEnabled(false);
    connect(m_channel, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_ok->setEnabled(!text.isEmpty()); });
    connect(m_ok, &QPushButton::clicked, this, &CChannelDlg::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void CChannelDlg::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) {
        m_bInitialized = TRUE;
        m_channel->setMaxLength(m_bIsIRCX
            ? MAX_IRCXCHANNAME : MAX_IRCCHANNAME);
        m_channel->setText(m_strChannel);
        m_password->setText(m_strPassword);
        m_ok->setEnabled(!m_channel->text().isEmpty());
    }
    QDialog::showEvent(event);
}

void CChannelDlg::accept()
{
    if (m_channel->text().isEmpty()) {
        m_channel->setFocus();
        return;
    }
    m_strChannel = m_channel->text();
    m_strPassword = m_password->text();
    QDialog::accept();
}

QString CChannelDlg::channel() const { return m_channel->text(); }
QString CChannelDlg::password() const { return m_password->text(); }

const char* GetMyName()
{
    static QByteArray value;
    return utf8Pointer(theApp.m_myName, value);
}

const char* GetMyScreenName()
{
    static QByteArray value;
    return utf8Pointer(DecodeNickForScreen(theApp.m_myName), value);
}

const char* GetMyNickName()
{
    static QByteArray value;
    return utf8Pointer(theApp.m_myNick, value);
}

const char* GetMyIdent()
{
    static QByteArray value;
    return utf8Pointer(theApp.m_myIdent, value);
}

const char* GetMyServer()
{
    static QByteArray value;
    return utf8Pointer(theApp.m_strConnectedService, value);
}

const char* GetMyPhysicalServer()
{
    static QByteArray value;
    return utf8Pointer(theApp.m_strConnectedServer, value);
}

const char* GetMyRealName()
{
    if (theApp.m_myRealName.isEmpty()) {
        theApp.m_myRealName = originalResourceString(QStringLiteral("IDS_DEFAULT_REALNAME"));
    }
    static QByteArray value;
    return utf8Pointer(theApp.m_myRealName, value);
}

const char* GetMyCharacter()
{
    static QByteArray value;
    return utf8Pointer(theApp.m_myCharacterName, value);
}

const char* GetMyChannel()
{
    static QByteArray value;
    return utf8Pointer(theApp.m_myChannel, value);
}

const char* GetMyHomePage()
{
    static QByteArray value;
    return utf8Pointer(theApp.m_strHomePage, value);
}

const char* GetMyEmail()
{
    static QByteArray value;
    return utf8Pointer(theApp.m_strEmail, value);
}

const char* GetMyUserName()
{
    const qsizetype at = theApp.m_strEmail.indexOf(QLatin1Char('@'));
    if (at > 0) {
        theApp.m_strUserName = theApp.m_strEmail.left(at);
    } else if (!theApp.m_strEmail.isEmpty() && at != 0) {
        theApp.m_strUserName = theApp.m_strEmail;
    } else {
        theApp.m_strUserName = theApp.m_myName;
    }
    static QByteArray value;
    return utf8Pointer(theApp.m_strUserName, value);
}

void SetMyIdent(const QString& ident) { theApp.m_myIdent = ident; }
void SetMyName(const QString& name)
{
    theApp.m_myName = name;
    theApp.m_myNick = name;
}
void SetMyNameNick(const QString& nickname)
{
    theApp.m_myNick = nickname;
    // IRCX quoted-nickname decoding belongs to intl.cpp. Until that source
    // module is ported, preserve the wire name instead of guessing a decode.
    theApp.m_myName = nickname;
}
void SetMyRealName(const QString& realName) { theApp.m_myRealName = realName; }
void SetMyCharacter(const QString& characterName) { theApp.m_myCharacterName = characterName; }
void SetMyHomePage(const QString& homePage) { theApp.m_strHomePage = homePage; }
void SetMyEmail(const QString& email) { theApp.m_strEmail = email; }
void ChatSetChannel(const QString& channelName) { theApp.m_myChannel = channelName; }
void ChatSetServer(const QString& serverName)
{
    QString service;
    theApp.m_listChatServices.GetServiceNameFromDisplayName(serverName,
                                                            service);
    theApp.m_strConnectedService = service;
}

CChatService* AddToServerList(const QString& serviceName)
{
    CChatServiceList& services = theApp.m_listChatServices;
    CChatService parsed(serviceName);
    CChatService* service = services.FindService(parsed.GetGroup(),
                                                 parsed.GetServer());
    if (service) {
        services.MoveServiceToTop(service);
        services.WriteIfChanged();
        return service;
    }
    if (!parsed.GetServer().isNull()) {
        service = services.FindServiceForServer(parsed.GetServer());
        if (service) services.DestroyService(service);
    }
    service = services.CreateService(parsed.GetGroup(), parsed.GetServer());
    services.WriteIfChanged();
    return service;
}
