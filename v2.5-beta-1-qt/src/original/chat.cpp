// Ported from v2.5-beta-1-modern/chat.cpp.
// MFC CWinApp startup is replaced by QApplication startup.

#include "chat.h"

#include "chatdoc.h"
#include "chatver.h"
#include "actions.h"
#include "autopage.h"
#include "avatar.h"
#include "bodycam.h"
#include "ircproto.h"
#include "intl.h"
#include "mainfrm.h"
#include "motd.h"
#include "notipage.h"
#include "panel.h"
#include "originalassets.h"
#include "proppage.h"
#include "protsupp.h"
#include "roomlist.h"
#include "userinfo.h"
#include "userlist.h"
#include "whisprbx.h"
#include "win98palette.h"

#include <QApplication>
#include <QColor>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPixmap>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtPrintSupport/QPageSetupDialog>
#include <QtPrintSupport/QPrinter>

#ifndef COMIC_CHAT_ENTRY_ONLY
CChatApp theApp;

void GetVersionString(QString& version)
{
    version = originalResourceString(QStringLiteral("ID_SIMPLE_VERSION"));
    version.replace(QStringLiteral("%1"),
                    QString::fromLatin1(VER_PRODUCTVERSION_STR));
}

void CChatApp::InitializeComicsFonts()
{
    const int pointSize = originalResourceString(
        QStringLiteral("IDS_DFLT_COMICSPNTSIZE")).toInt();
    m_iFontHeightBalloon = -pointSize * 20;
    m_comicsFont = QFont(originalResourceString(
        QStringLiteral("ID_COMIC_FONT_NAME")));
    m_comicsFont.setPixelSize(qAbs(m_iFontHeightBalloon));
    const int weight = originalResourceString(
        QStringLiteral("IDS_COMICS_BOLD_DFLT")).toInt();
    m_comicsFont.setWeight(static_cast<QFont::Weight>(weight));
    m_comicsFont.setItalic(false);
    m_comicsFont.setUnderline(false);
    m_comicsFont.setStrikeOut(false);
    m_comicsColor = RGB(0, 0, 0);
}

void CChatApp::InitializeFonts()
{
    InitializeComicsFonts();
    // Original InitializeFonts leaves lfFaceName empty so Windows chooses the
    // default GUI face, and uses nFontHeight (-14) in MM_TEXT coordinates.
    m_textFont = QApplication::font();
    m_textFont.setPixelSize(qAbs(nFontHeight));
    m_textFont.setWeight(QFont::Normal);
    m_textFont.setItalic(false);
    m_textFont.setUnderline(false);
    m_textFont.setStrikeOut(false);
    m_textColor = RGB(0, 0, 0);
    SetMime(GetCorrectCharSet());
}

void CChatApp::InitStatusIcons()
{
    m_StatusIcons.clear();
    const QPixmap strip(originalFileResourcePath(
        QStringLiteral("IDB_MEMBER"), QStringLiteral("BITMAP")));
    for (int index = 0; index < 5; ++index) {
        QPixmap icon = strip.copy(index * 16, 0, 16, 16);
        if (!icon.isNull()) {
            icon.setMask(icon.createMaskFromColor(
                QColor(0, 0, 255), Qt::MaskInColor));
        }
        m_StatusIcons.append(QIcon(icon));
    }
}

void CChatApp::DoOptionsDialog(BOOL comicsView, UINT initialPageId)
{
    COptionsDialog options(comicsView, initialPageId, m_pMainWnd.data());
    options.exec();
    if (CChatDoc* document = GetChatDoc()) document->SetFocusToSayWnd();
}

void CChatApp::OnViewOptions()
{
    const BOOL comicsView = GetChatDoc()
        ? GetChatDoc()->m_bComicView : m_bComicView;
    DoOptionsDialog(comicsView);
}

int CChatApp::run(QApplication& app)
{
    void InitializeEmotionRules();
    void DestroyEmotionRules();

    win98palette::apply(app);

    InitVals();
    InitializeFonts();
    m_lastBackDrop = originalResourceString(
        QStringLiteral("IDS_DEFAULT_BACKDROP"));
    LoadFromReg();
    InitStatusIcons();
    m_SrvConnector.SetServiceList(&m_listChatServices);
    m_connectTimer = new QTimer;
    m_connectTimer->setInterval(50);
    QObject::connect(m_connectTimer, &QTimer::timeout, m_connectTimer,
                     [this] { ContinueConnection(); });
    if (!(m_flags0 & F0_ALREADYRUN)) {
        m_dynaRules.bLoadRulesFromResource();
    }
    CleanRoomInfos();
    CUnitPanelPage::SetFonts(m_comicsFont, m_comicsColor);
    LoadEmotionStrings();
    InitializeEmotionRules();
    InitializeBackDrops();
    InitializeAvatars();
    if (!CommunicationInits()) return 1;
    if (m_myCharacterName.isEmpty()) {
        GetNextAvatarName(m_myCharacterName);
    }

    auto* frame = new CMainFrame;
    m_pMainWnd = frame;
    frame->CreateStatusWindow();
    CChatDoc* doc = frame->CreateNewDocument();
    m_pDoc = doc;
    if (m_maxedFrame) frame->showMaximized();
    else frame->show();
    doc->ResetStatus(true, true);

    const int result = app.exec();
    SaveToReg(TRUE);
    SaveToReg(FALSE);
    if (m_connectTimer) m_connectTimer->stop();
    delete m_connectTimer;
    m_connectTimer = nullptr;
    m_SrvConnector.Cleanup();
    m_listChatServices.WriteIfChanged();
    m_listChatServices.MarkServersMigrated();
    delete frame;
    m_pMainWnd = nullptr;
    m_pDoc = nullptr;
    SetChatDoc(nullptr);
    DestroyEmotionRules();
    DestroyWhisperBox();
    DestroyNotificationBox();
    DestroyExternalUserInfos();
    CleanRoomInfos();
    CommunicationCleanup();
    return result;
}

void CChatApp::SetStatusPaneString(int pane, const QString& text)
{
    if (m_pMainWnd) {
        m_pMainWnd->SetStatusPaneString(pane, text);
    }
}

void CChatApp::OnFilePrintSetup(QPrinter* printer, QWidget* parent)
{
    if (!printer) return;
    QPageSetupDialog dialog(printer, parent);
    dialog.exec();
}

void CChatApp::CompleteConnection()
{
    m_SrvConnector.Cleanup();
    m_strConnectedService = m_SrvConnector.m_strSvc;
    if (CChatServer* server = m_SrvConnector.GetConnectingServer()) {
        m_strConnectedServer = server->m_pszName;
        if (CChatServerGroup* group =
                m_SrvConnector.GetConnectingServerGroup()) {
            group->SetLastAccessedServer(server);
        }
    }
    if (m_pDoc) {
        m_pDoc->ResetStatus();
    }
}

void CChatApp::OnConnectError()
{
    if (m_connectTimer) m_connectTimer->stop();
    if (CRoomInfo* protocol = GetDefaultProto())
        protocol->SetConnectionStatus(CX_DISCONNECTED);
    CChatService service(m_strConnectedService);
    QString message = originalResourceString(
        QStringLiteral("ID_ERR_CONNECT"));
    message.replace(QStringLiteral("%1"), service.GetDisplayName());
    QMessageBox::warning(m_pMainWnd.data(),
                         originalResourceString(
                             QStringLiteral("AFX_IDS_APP_TITLE")),
                         message);
}

void CChatApp::OnConnectConnected()
{
    if (m_connectTimer) m_connectTimer->stop();
}

void CChatApp::ContinueConnection()
{
    const int socketCount = m_SrvConnector.GetNumSockets();
    int result = 1;
    for (int socket = 0; result == 1 && socket < socketCount; ++socket)
        result = m_SrvConnector.AssignSocket(socket);
    if (result == 0) OnConnectError();
}

void CChatApp::StartConnectionTimer()
{
    if (m_connectTimer) {
        m_connectTimer->start();
    } else {
        ContinueConnection();
    }
}

void CChatApp::ResumeConnection()
{
    if (m_SrvConnector.BeginConnectToService(QString())) {
        if (m_connectTimer) m_connectTimer->start();
    } else {
        OnConnectError();
    }
}

void CChatApp::OnAwayToggle()
{
    BOOL toggle = FALSE;
    if (!m_bAway) {
        if (m_strAwayMessage.isEmpty()) {
            m_strAwayMessage = originalResourceString(IDS_DFLTAWAYMSG);
        }

        CAwayDlg away(m_pMainWnd.data());
        away.m_rtfAwayMsg.m_prgdwFormatting = new CDWordArray;
        QByteArray controlFull = m_strAwayMessage.toUtf8();
        char* controlLess = SzControlLess(
            controlFull.data(), away.m_rtfAwayMsg.m_prgdwFormatting);
        away.m_rtfAwayMsg.m_strText = controlLess
            ? QString::fromUtf8(controlLess) : QString();
        away.m_rtfAwayMsg.m_crTextColor = RGB(0, 0, 0);
        away.m_rtfAwayMsg.DefineDefaultCharFormat();

        if (away.exec() == QDialog::Accepted) {
            toggle = TRUE;
            QString rebuilt = away.m_rtfAwayMsg.m_strText;
            if (away.m_rtfAwayMsg.m_prgdwFormatting) {
                const QByteArray plain = away.m_rtfAwayMsg.m_strText.toUtf8();
                if (char* encoded = SzControlFull(
                        plain.constData(),
                        away.m_rtfAwayMsg.m_prgdwFormatting)) {
                    rebuilt = QString::fromUtf8(encoded);
                    delete[] encoded;
                }
            }
            m_strAwayMessage = rebuilt;
        }
    } else {
        toggle = TRUE;
    }

    if (toggle) {
        m_bAway = !m_bAway;
        m_bAwayPrompt = m_bAway;
        if (CRoomInfo* protocol = GetDefaultProto()) {
            protocol->ChatSetAway(m_bAway, m_strAwayMessage);
        }
    }
}

void CChatApp::OnMotd()
{
    if (CIrcProto* protocol = GetIrcProto()) {
        m_bDisableMOTD = protocol->bChatShowMOTD();
    }
}

void CChatApp::OnViewAutomations()
{
    QDialog sheet(m_pMainWnd.data());
    sheet.setObjectName(QStringLiteral("IDS_AUTOMATIONS"));
    sheet.setWindowTitle(originalResourceString(
        QStringLiteral("IDS_AUTOMATIONS")));

    auto* tabs = new QTabWidget(&sheet);
    CAutomationPage automation(tabs);
    CNotificationsPage notifications(tabs);
    CRuleSetsPage ruleSets(tabs);
    CRulesPage rules(tabs);
    CCDynaRules dynaRulesCopy;
    CCDynaNotifs dynaNotifsCopy;

    tabs->addTab(&automation, originalDialogCaption(
        QStringLiteral("IDD_AUTOMATION_PAGE")));
    if (m_rulesData.bInitAlloc() && m_rulesData.bLoadStrings()) {
        dynaRulesCopy = m_dynaRules;
        dynaNotifsCopy = m_dynaNotifs;
        notifications.SetDynaNotifs(&dynaNotifsCopy);
        ruleSets.SetDynaRules(&dynaRulesCopy);
        rules.SetDynaRules(&dynaRulesCopy);
        tabs->addTab(&notifications, originalDialogCaption(
            QStringLiteral("IDD_NOTIFICATIONS")));
        tabs->addTab(&ruleSets, originalDialogCaption(
            QStringLiteral("IDD_RULESETSPAGE")));
        tabs->addTab(&rules, originalDialogCaption(
            QStringLiteral("IDD_RULESPAGE")));
    }

    if (m_iAutoPage >= 0 && m_iAutoPage < tabs->count())
        tabs->setCurrentIndex(m_iAutoPage);
    m_iAutoPage = -2;

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &sheet);
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &sheet, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &sheet, &QDialog::reject);
    auto* layout = new QVBoxLayout(&sheet);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    if (sheet.exec() == QDialog::Accepted) {
        automation.OnOK();
        if (tabs->count() == 4) {
            notifications.OnOK();
            ruleSets.OnOK();
            rules.OnOK();
        }
    }
    m_iAutoPage = -1;
    m_dynaNotifs.SetStartUpIdent(QString());
    if (CChatDoc* document = GetChatDoc()) document->SetFocusToSayWnd();
}

void CChatApp::OnDefineMacro()
{
    OnViewAutomations();
}

namespace {
CRoomListPersist roomPersist;
CUserListPersist userPersist;
}

void OnChatroomListAux(const QString& query)
{
    if (!serverConn.m_bIrcXServer && !bCanViewUnrated(TRUE)) return;

    if (!query.isNull() || !roomPersist.m_strQuery.isEmpty())
        roomPersist.m_cachedServer.clear();
    roomPersist.m_strQuery = query.isNull() ? QString() : query;
    CRoomList roomDialog(&roomPersist, theApp.m_pMainWnd.data());
    theApp.m_pRoomList = &roomDialog;
    roomDialog.DoModal();
    theApp.m_pRoomList = nullptr;
    if (CChatDoc* doc = GetChatDoc()) doc->SetFocusToSayWnd();
}

void CChatApp::OnChatroomList()
{
    OnChatroomListAux();
}

void OnUserListAux(const QString& query, const QString& encodedRoom,
                   const QString& prettyRoom)
{
    if (!bCanViewUnrated(TRUE)) return;

    // Preserve the original chat.cpp assignment: this invalidates the room
    // list cache, not userPersist, when a WHO query is supplied.
    if (!query.isNull() || !userPersist.m_strQuery.isEmpty())
        roomPersist.m_cachedServer.clear();
    userPersist.m_strQuery = query.isNull() ? QString() : query;

    if (!encodedRoom.isNull()) {
        userPersist.m_strEncRoom = encodedRoom;
        userPersist.m_searchType = USERSEARCH_ROOM;
        userPersist.m_strRoomFilter = prettyRoom.isNull()
            ? QString() : prettyRoom;
    } else {
        userPersist.m_strEncRoom.clear();
    }

    CUserList userDialog(&userPersist, theApp.m_pMainWnd.data());
    theApp.m_pUserList = &userDialog;
    const int result = userDialog.DoModal();
    theApp.m_pUserList = nullptr;

    if (result == LAUNCH_WHISPERBOX) {
        if (userDialog.m_selUser) WhisperBox(userDialog.m_selUser);
    } else {
        if (CChatDoc* doc = GetChatDoc()) doc->SetFocusToSayWnd();
    }
    if (theApp.m_pRoomList) theApp.m_pRoomList->ReenableListMembers();
}

void CChatApp::OnUserList()
{
    OnUserListAux();
}

void CChatApp::OnViewLoginNotifs()
{
    m_bLoginNotifsShown = !m_bLoginNotifsShown;
    CNotificationUsers* notificationBox = GetNotifBox();
    if (m_bLoginNotifsShown) {
        if (notificationBox) notificationBox->showNormal();
        else CreateNotificationBox();
    } else if (notificationBox) {
        notificationBox->hide();
        m_dynaNotifs.bRemoveFlagsFromAllUsers(g_wNew);
    }
}

namespace {
void NoteArtServersGoneOnce()
{
    static BOOL shown = FALSE;
    if (shown) return;
    shown = TRUE;
    QMessageBox::information(
        theApp.m_pMainWnd, QString(),
        QStringLiteral(
            "The Comic Chat art servers are long gone, so custom characters and "
            "backdrops can no longer be downloaded.\n\n"
            "The characters bundled with this build will be used instead."));
}
}

BOOL CChatApp::StartDownloadingAvatar(CUserInfo* user, CChatDoc*, BOOL)
{
    if (!user) return FALSE;
    NoteArtServersGoneOnce();
    user->SetFlag(UF_AUTODOWNLOAD | UF_INTERACTIVEDOWNLOAD, false);
    return FALSE;
}

BOOL CChatApp::StartDownloadingBackdrop(const char*, const char*)
{
    NoteArtServersGoneOnce();
    return FALSE;
}

CRoomInfo* CChatApp::GetRoomInfoFromName(const QString& channelName,
                                         int* index, bool cloneOK,
                                         bool nullOK)
{
    if (m_enterInfos.isEmpty()) m_enterInfos.append(&g_enterInfo);

    CRoomInfo* enterInfo = nullptr;
    int roomIndex = m_enterInfos.size() - 1;
    for (; roomIndex >= 0; --roomIndex) {
        enterInfo = m_enterInfos.at(roomIndex);
        if (!enterInfo) continue;
        if (enterInfo->m_strChannel.compare(channelName,
                                            Qt::CaseInsensitive) == 0) {
            break;
        }
        if (cloneOK) {
            const QString upperChannel = channelName.toUpper();
            const QString upperEnter = enterInfo->m_strChannel.toUpper();
            if (upperChannel.startsWith(upperEnter)) {
                qsizetype suffix = upperEnter.size();
                while (suffix < upperChannel.size()
                       && upperChannel.at(suffix).isDigit()) {
                    ++suffix;
                }
                if (suffix == upperChannel.size()) break;
            }
        }
        enterInfo = nullptr;
    }

    if (!enterInfo) {
        if (nullOK) {
            roomIndex = -1;
        } else {
            roomIndex = 0;
            enterInfo = m_enterInfos.at(0);
            enterInfo->m_strChannel = channelName;
        }
    }
    if (index) *index = roomIndex;
    return enterInfo;
}

int CChatApp::AddRoomInfo(CRoomInfo* enterInfo)
{
    if (!enterInfo) return -1;
    if (m_enterInfos.isEmpty()) m_enterInfos.append(&g_enterInfo);
    m_enterInfos.append(enterInfo);
    return m_enterInfos.size() - 1;
}

void CChatApp::RemoveRoomInfo(int index)
{
    if (index <= 0 || index >= m_enterInfos.size()) return;
    delete m_enterInfos.takeAt(index);
}

void CChatApp::CleanRoomInfos()
{
    if (m_enterInfos.isEmpty()) {
        m_enterInfos.append(&g_enterInfo);
        return;
    }
    while (m_enterInfos.size() > 1) delete m_enterInfos.takeLast();
    m_enterInfos[0] = &g_enterInfo;
}
#endif

#ifndef COMIC_CHAT_NO_MAIN
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    return theApp.run(app);
}
#endif
