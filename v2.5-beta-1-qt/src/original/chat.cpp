// Ported from v2.5-beta-1-modern/chat.cpp.
// MFC CWinApp startup is replaced by QApplication startup.

#include "chat.h"

#include "chatdoc.h"
#include "chatver.h"
#include "actions.h"
#include "autopage.h"
#include "avatar.h"
#include "bodycam.h"
#include "chatbars.h"
#include "format.h"
#include "filesend.h"
#include "ircproto.h"
#include "intl.h"
#include "mainfrm.h"
#include "motd.h"
#include "notipage.h"
#include "panel.h"
#include "originalassets.h"
#include "proppage.h"
#include "protsupp.h"
#include "resource.h"
#include "roomlist.h"
#include "setupdlg.h"
#include "tabbar.h"
#include "userinfo.h"
#include "userlist.h"
#include "whisprbx.h"
#include "win98palette.h"

#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtPrintSupport/QAbstractPrintDialog>
#include <QtPrintSupport/QPrintDialog>
#include <QtPrintSupport/QPrinter>

#ifndef COMIC_CHAT_ENTRY_ONLY
CChatApp theApp;

namespace {
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

const OriginalDialogControl* findControl(
    const OriginalDialogResource& dialog, const QString& identifier)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier == identifier) return &control;
    }
    return nullptr;
}

void placeControl(QWidget* widget, const OriginalDialogResource& dialog,
                  const DialogUnitMapper& mapper,
                  const QString& identifier)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control =
            findControl(dialog, identifier)) {
        widget->setGeometry(mapper.rect(*control));
        widget->setVisible(control->visible);
    }
}

QFont resourceFont(const OriginalDialogResource& dialog)
{
    QFont font(dialog.fontFamily);
    if (dialog.fontPointSize > 0) font.setPointSize(dialog.fontPointSize);
    return font;
}

class CAboutDlg final : public QDialog {
public:
    explicit CAboutDlg(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        const QString resource = QStringLiteral("IDD_ABOUTBOX");
        const OriginalDialogResource dialog =
            originalDialogResource(resource);
        const QFont font = resourceFont(dialog);
        const DialogUnitMapper mapper(font);

        setObjectName(resource);
        setFont(font);
        setWindowTitle(dialog.caption);
        setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        QPalette whitePalette = palette();
        whitePalette.setColor(QPalette::Window, Qt::white);
        setPalette(whitePalette);
        setAutoFillBackground(true);

        auto* tiki = new QLabel(this);
        tiki->setAlignment(Qt::AlignCenter);
        tiki->setPixmap(QPixmap(originalFileResourcePath(
            QStringLiteral("IDB_TIKI"), QStringLiteral("BITMAP"))));
        placeControl(tiki, dialog, mapper, QStringLiteral("IDC_TIKI"));

        auto makeLabel = [&](const QString& identifier) {
            auto* label = new QLabel(
                originalDialogControlText(resource, identifier), this);
            placeControl(label, dialog, mapper, identifier);
            return label;
        };

        QLabel* version = makeLabel(QStringLiteral("IDC_VERSION"));
        QString versionText;
        GetVersionString(versionText);
        version->setText(versionText);

        QLabel* warning = makeLabel(QStringLiteral("IDC_WARNING"));
        warning->setText(originalResourceString(
            QStringLiteral("IDS_WARNING_TEXT")));
        warning->setWordWrap(true);

        QLabel* corporation = makeLabel(QStringLiteral("IDC_CORP"));
        QLabel* user = makeLabel(QStringLiteral("IDC_USER"));
        makeLabel(QStringLiteral("IDC_COPY"));

#ifdef Q_OS_WIN
        struct RegistrationLocation {
            const char* path;
            const char* userValue;
            const char* corporationValue;
        };
        static const RegistrationLocation locations[] = {
            {"HKEY_CURRENT_USER\\Software\\Microsoft\\MS Setup (ACME)\\User Info",
             "DefName", "DefCompany"},
            {"HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion",
             "RegisteredOwner", "RegisteredOrganization"},
            {"HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion",
             "RegisteredOwner", "RegisteredOrganization"},
        };
        for (const RegistrationLocation& location : locations) {
            QSettings settings(QString::fromLatin1(location.path),
                               QSettings::NativeFormat);
            const QString registeredUser = settings.value(
                QString::fromLatin1(location.userValue)).toString();
            const QString registeredCorporation = settings.value(
                QString::fromLatin1(location.corporationValue)).toString();
            if (!registeredUser.isEmpty()
                || !registeredCorporation.isEmpty()) {
                user->setText(registeredUser);
                corporation->setText(registeredCorporation);
                break;
            }
        }
#else
        Q_UNUSED(corporation);
        Q_UNUSED(user);
#endif

        auto* license = new QGroupBox(originalDialogControlText(
            resource, QStringLiteral("IDC_LICENSE")), this);
        placeControl(license, dialog, mapper, QStringLiteral("IDC_LICENSE"));

        auto* ok = new QPushButton(originalDialogControlText(
            resource, QStringLiteral("IDOK")), this);
        ok->setDefault(true);
        placeControl(ok, dialog, mapper, QStringLiteral("IDOK"));
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);

        tiki->lower();
    }
};

void LaunchMicrosoftURL(const QString& resourceIdentifier)
{
    const QString url =
        originalResourceString(QStringLiteral("IDS_URL_MSPREFIX"))
        + originalResourceString(resourceIdentifier);
    const QByteArray encoded = url.toUtf8();
    FLaunchBrowser(encoded.constData());
}
}

void GetVersionString(QString& version)
{
    version = originalResourceString(QStringLiteral("ID_SIMPLE_VERSION"));
    version.replace(QStringLiteral("%1"),
                    QString::fromLatin1(VER_PRODUCTVERSION_STR));
}

void CChatApp::InitializeComicsFonts()
{
    registerOriginalComicFont();
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

void CChatApp::OnAppAbout()
{
    CAboutDlg about(m_pMainWnd.data());
    about.exec();
}

void CChatApp::OnHelpFreestuff()
{
    LaunchMicrosoftURL(QStringLiteral("IDS_URL_FREESTUFF"));
}

void CChatApp::OnHelpProductnews()
{
    LaunchMicrosoftURL(QStringLiteral("IDS_URL_PRODUCTNEWS"));
}

void CChatApp::OnHelpFaq()
{
    LaunchMicrosoftURL(QStringLiteral("IDS_URL_FAQ"));
}

void CChatApp::OnHelpOnlineSupport()
{
    LaunchMicrosoftURL(QStringLiteral("IDS_URL_ONLINESUPPORT"));
}

void CChatApp::OnHelpBestofWeb()
{
    LaunchMicrosoftURL(QStringLiteral("IDS_URL_BESTOFWEB"));
}

void CChatApp::OnHelpSearchtheWeb()
{
    LaunchMicrosoftURL(QStringLiteral("IDS_URL_SEARCHTHEWEB"));
}

void CChatApp::OnHelpMsHomepage()
{
    LaunchMicrosoftURL(QStringLiteral("IDS_URL_MSHOMEPAGE"));
}

bool CChatApp::ProcessShellCommand(const QString& argument,
                                   QString* fileName, BOOL* fileNew)
{
    if (!fileName || !fileNew) return false;
    QString value = argument;
    if (value.size() >= 2 && value.front() == QLatin1Char('"')
        && value.back() == QLatin1Char('"')) {
        value = value.mid(1, value.size() - 2);
    }

    *fileNew = FALSE;
    *fileName = value;
    if (!value.startsWith(QStringLiteral("mic://"), Qt::CaseSensitive)
        && !value.startsWith(QStringLiteral("irc://"),
                             Qt::CaseSensitive)) {
        return true;
    }

    const qsizetype slash = value.indexOf(QLatin1Char('/'), 6);
    if (slash < 0) return true;

    ChatSetServer(value.mid(6, slash - 6));
    const QString roomComponent = value.mid(slash + 1);
    QString room;
    QString password;
    const qsizetype passwordAt =
        roomComponent.indexOf(QStringLiteral("___"));
    if (passwordAt >= 0) {
        room = roomComponent.left(passwordAt);
        password = roomComponent.mid(passwordAt + 3);
    }

    const qsizetype textAt =
        roomComponent.indexOf(QStringLiteral("__text"));
    const qsizetype comicsAt =
        roomComponent.indexOf(QStringLiteral("__comics"));
    if (textAt >= 0) {
        room = roomComponent.left(textAt);
        g_iViewMode = VM_TEXT;
        m_bSaveViewMode = false;
    } else if (comicsAt >= 0) {
        room = roomComponent.left(comicsAt);
        g_iViewMode = VM_COMICS;
        m_bSaveViewMode = false;
    }
    if (room.isEmpty()) room = roomComponent;

    bInitEnterInfo(g_enterInfo, room, password, QString(), 0L, TRUE);
    if (CChatDoc* document = GetChatDoc())
        document->m_fileType = FT_CCR;
    ChatSetCXPrompt(FALSE);
    m_bLoadURL = true;
    fileName->clear();
    *fileNew = TRUE;
    return true;
}

void CChatApp::ScheduleDocumentInitialize(CChatDoc* document)
{
    if (!m_bMainLoopReady || m_bDocumentInitializeActive
        || !document || document->m_bStatusView
        || document->m_fileType != FT_CCR
        || document->m_bChatInitializeScheduled
        || document->m_bChatInitializeComplete) {
        return;
    }

    document->m_bChatInitializeScheduled = true;
    const quint64 generation = ++document->m_chatInitializeGeneration;
    const QPointer<CMainFrame> frame = m_pMainWnd;
    QTimer::singleShot(0, frame, [frame, document, generation] {
        if (!frame || !g_docs.contains(document)
            || document->IsCloseStarted()
            || document->m_chatInitializeGeneration != generation) {
            return;
        }
        if (document->m_fileType != FT_CCR
            || frame->GetActiveDocument() != document) {
            document->m_bChatInitializeScheduled = false;
            return;
        }
        document->m_bChatInitializeComplete = true;
        theApp.m_bDocumentInitializeActive = true;
        ChatInitialize(&g_nCXKeepServer, &g_bCXPrompt);
        theApp.m_bDocumentInitializeActive = false;
        if (g_docs.contains(document) && !document->IsCloseStarted()) {
            document->m_bChatInitializeScheduled = false;
            document->m_bChatInitializeComplete = true;
        }
    });
}

void CChatApp::OnFileOpen()
{
    QString initial;
    if (CChatDoc* document = GetChatDoc())
        initial = document->GetPathname();

    const QString sourceFilter =
        originalResourceString(QStringLiteral("IDS_CCC_FILTER"));
    const QString label = sourceFilter.section(QLatin1Char('|'), 0, 0);
    QFileDialog dialog(m_pMainWnd.data());
    dialog.setObjectName(QStringLiteral("CFileDialog"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setDefaultSuffix(QString::fromLatin1(g_szCCCExt));
    dialog.setNameFilter(label);
    if (!initial.isEmpty()) {
        const QFileInfo info(initial);
        dialog.setDirectory(info.absolutePath());
        dialog.selectFile(info.fileName());
    }
    if (dialog.exec() == QDialog::Accepted)
        OpenDocumentFile(dialog.selectedFiles().value(0));
}

CChatDoc* CChatApp::OpenDocumentFile(const QString& fileName)
{
    if (!m_pMainWnd || fileName.isEmpty()) return nullptr;
    const QFileInfo requestedInfo(fileName);
    const QString absolute = requestedInfo.canonicalFilePath().isEmpty()
        ? requestedInfo.absoluteFilePath()
        : requestedInfo.canonicalFilePath();
    for (CChatDoc* document : g_docs) {
        if (!document || document->IsCloseStarted()
            || document->GetPathname().isEmpty()) {
            continue;
        }
        const QFileInfo openInfo(document->GetPathname());
        const QString openPath = openInfo.canonicalFilePath().isEmpty()
            ? openInfo.absoluteFilePath() : openInfo.canonicalFilePath();
        if (openPath.compare(absolute, Qt::CaseInsensitive) == 0) {
            m_pMainWnd->ActivateDocument(document);
            return document;
        }
    }

    CChatDoc* previous = m_pMainWnd->GetActiveDocument();
    const QPointer<QWidget> previousFocus = QApplication::focusWidget();
    CChatDoc* document = m_pMainWnd->CreateNewDocument();
    if (!document) return nullptr;
    CChatDoc* oldContext = GetChatDoc();
    SetChatDoc(document);
    const bool opened = document->OnOpenDocument(absolute);
    if (oldContext && oldContext != document
        && GetChatDoc() == document) {
        SetChatDoc(oldContext);
    }
    if (!opened) {
        const bool anotherDocumentActivated =
            m_pMainWnd->GetActiveDocument()
            && m_pMainWnd->GetActiveDocument() != document;
        document->SetModifiedFlag(false);
        document->OnCloseDocument();
        m_pMainWnd->CloseDocument(document);
        if (m_pExitingDoc == document) m_pExitingDoc = nullptr;
        if (!anotherDocumentActivated && previous) {
            m_pMainWnd->ActivateDocument(previous);
            if (previousFocus && previousFocus->isVisible()
                && previousFocus->isEnabled()) {
                previousFocus->setFocus(Qt::OtherFocusReason);
            }
        }
        return nullptr;
    }
    m_pMainWnd->ActivateDocument(document);
    if (document->m_fileType == FT_CCR)
        ScheduleDocumentInitialize(document);
    return document;
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
    m_bMainLoopReady = true;
    frame->CreateStatusWindow();
    CChatDoc* doc = nullptr;
    const QStringList arguments = app.arguments();
    bool startupCommandFailed = false;
    if (arguments.size() > 1) {
        QString fileName;
        BOOL fileNew = FALSE;
        if (!ProcessShellCommand(arguments.at(1), &fileName, &fileNew)) {
            startupCommandFailed = true;
        } else if (fileNew) {
            doc = frame->CreateNewDocument();
        } else {
            doc = OpenDocumentFile(fileName);
            startupCommandFailed = !doc;
        }
    }
    if (!doc && !startupCommandFailed) {
        CChatDoc* activated = frame->GetActiveDocument();
        if (activated && !activated->m_bStatusView
            && !activated->IsCloseStarted()) {
            doc = activated;
        }
    }
    if (!doc && !startupCommandFailed) doc = frame->CreateNewDocument();
    m_pDoc = doc;
    SetPrinterResolution(frame->GetPrinter());
    int result = 1;
    if (!startupCommandFailed && doc) {
        if (m_maxedFrame) frame->showMaximized();
        else frame->show();
        doc->ResetStatus(true, true);
        ScheduleDocumentInitialize(doc);
        result = app.exec();
        SaveToReg(TRUE);
    }
    // Modern performs the full persistence phase from ExitInstance before
    // shutdown cleanup even when ProcessShellCommand failed during startup.
    SaveToReg(FALSE);
    m_bMainLoopReady = false;
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
    CleanupFileProgressStore(TRUE);
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

void CChatApp::SetPrinterResolution(QPrinter* printer)
{
    if (!printer) return;

    // Modern asks the Windows driver for symbolic DMRES_MEDIUM quality. Qt has
    // no equivalent symbolic setting, and the source's numeric 150-DPI
    // alternative is commented out. The shared printer therefore keeps Qt's
    // defined HighResolution backend/native DPI. This deliberate no-op does not
    // claim that HighResolution is equivalent to DMRES_MEDIUM.
}

void CChatApp::OnFilePrintSetup(QPrinter* printer, QWidget* parent)
{
    if (!printer) return;
    // CWinApp::OnFilePrintSetup is a printer-selection/setup route, not the
    // separate ID_FILE_PAGE_SETUP resource. QPrintDialog is the closest Qt
    // adapter; accepting it only updates the shared printer and does not print.
    QPrintDialog dialog(printer, parent);
    dialog.setObjectName(QStringLiteral("CPrintDialog"));
    dialog.setWindowTitle(originalResourceString(
        QStringLiteral("ID_FILE_PRINT_SETUP")).section(QLatin1Char('\n'), 1, 1));
    dialog.setOption(QAbstractPrintDialog::PrintPageRange, false);
    dialog.setOption(QAbstractPrintDialog::PrintSelection, false);
    dialog.setOption(QAbstractPrintDialog::PrintCurrentPage, false);
    dialog.exec();
}

void CChatApp::OnSessionConnect()
{
    if (g_docs.size() < 2) {
        const QPointer<CMainFrame> frame = m_pMainWnd;
        if (frame) {
            QTimer::singleShot(0, frame, [frame] {
                if (frame) frame->CreateNewDocument();
            });
        }
        return;
    }

    InitializeServerConnection(&g_enterInfo, &g_bCXPrompt);
}

void CChatApp::OnNewroom()
{
    ChatSwitchChannel();
}

void CChatApp::OnCreateroom()
{
    ChatCreateRoom(g_enterInfo);
}

void CChatApp::OnDisconnect()
{
    ChatServerDisconnect(TRUE, FALSE);
}

BOOL CChatApp::OnUpdateSessionConnect() const
{
    CIrcProto* protocol = GetIrcProto();
    return !protocol
        || protocol->GetConnectionStatus() == CX_DISCONNECTED;
}

BOOL CChatApp::OnUpdateNewroom() const
{
    CIrcProto* protocol = GetIrcProto();
    if (!protocol) return FALSE;
    const ConnectionStatus status = protocol->GetConnectionStatus();
    return status == CX_INCHANNEL || status == CX_NOCHANNEL;
}

BOOL CChatApp::OnUpdateDisconnect() const
{
    CIrcProto* protocol = GetIrcProto();
    return protocol
        && protocol->GetConnectionStatus() != CX_DISCONNECTED;
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
    InitializeServerConnection(&g_enterInfo, &g_bCXPrompt);
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

BOOL CChatApp::OnUpdateAwayToggle(BOOL* checked) const
{
    if (checked) *checked = m_bAway;
    CRoomInfo* protocol = GetDefaultProto();
    if (!protocol) return FALSE;
    const ConnectionStatus status = protocol->GetConnectionStatus();
    return status == CX_NOCHANNEL || status == CX_INCHANNEL;
}

void CChatApp::OnMotd()
{
    if (CIrcProto* protocol = GetIrcProto()) {
        m_bDisableMOTD = protocol->bChatShowMOTD();
    }
}

BOOL CChatApp::OnUpdateMotd() const
{
    CIrcProto* protocol = GetIrcProto();
    if (!protocol) return FALSE;
    const ConnectionStatus status = protocol->GetConnectionStatus();
    return (status == CX_INCHANNEL || status == CX_NOCHANNEL)
        && !m_bDisableMOTD;
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

BOOL CChatApp::OnUpdateViewAutomations() const
{
    return !currentRoom
        || currentRoom->GetConnectionStatus() != CX_CONNECTING;
}

BOOL CChatApp::OnUpdateViewOptions() const
{
    return !currentRoom
        || currentRoom->GetConnectionStatus() != CX_CONNECTING;
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

BOOL CChatApp::OnUpdateCanSearch() const
{
    CIrcProto* protocol = GetIrcProto();
    if (!protocol) return FALSE;
    const ConnectionStatus status = protocol->GetConnectionStatus();
    return !m_bInSearch
        && (status == CX_INCHANNEL || status == CX_NOCHANNEL);
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

void CChatApp::OnViewTabbar()
{
    CTabBar* tabBar = m_pMainWnd ? m_pMainWnd->GetTabBar() : nullptr;
    if (!tabBar) return;
    const bool show = !tabBar->isVisible();
    tabBar->setVisible(show);
    if (show) m_flags1 |= F1_SHOWTABBAR;
    else m_flags1 &= ~DWORD(F1_SHOWTABBAR);
}

BOOL CChatApp::OnViewToolBar(UINT commandID)
{
    UINT which;
    switch (commandID) {
    case ID_VIEW_TOOLBAR_MAIN:
        which = CHAT_TOOLBAR_MAIN;
        break;
    case ID_VIEW_TOOLBAR_MEMBER:
        which = CHAT_TOOLBAR_MEMBER;
        break;
    case ID_VIEW_TOOLBAR_TEXT:
        which = CHAT_TOOLBAR_TEXT;
        break;
    default:
        return FALSE;
    }
    CChatToolBar* toolBar = m_pMainWnd
        ? m_pMainWnd->GetToolBar() : nullptr;
    if (!toolBar) return FALSE;
    toolBar->ToggleBar(which);
    return TRUE;
}

void CChatApp::OnViewStatuswindow()
{
    if (m_pMainWnd)
        m_pMainWnd->ShowStatusWindow(
            (m_flags0 & F0_SHOWSTATUSWINDOW) == 0);
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

BOOL CChatApp::OnUpdateViewTabbar(BOOL* checked) const
{
    const CTabBar* tabBar = m_pMainWnd
        ? m_pMainWnd->GetTabBar() : nullptr;
    if (checked) *checked = tabBar && tabBar->isVisible();
    return TRUE;
}

BOOL CChatApp::OnUpdateViewToolBar(UINT commandID,
                                   BOOL* checked) const
{
    UINT flag;
    switch (commandID) {
    case ID_VIEW_TOOLBAR_MAIN:
        flag = SB_TOOLBAR_MAIN;
        break;
    case ID_VIEW_TOOLBAR_MEMBER:
        flag = SB_TOOLBAR_MEMBER;
        break;
    case ID_VIEW_TOOLBAR_TEXT:
        flag = SB_TOOLBAR_TEXT;
        break;
    default:
        if (checked) *checked = FALSE;
        return FALSE;
    }
    if (checked) *checked = (m_iShowBars & flag) != 0;
    return TRUE;
}

BOOL CChatApp::OnUpdateViewStatuswindow(BOOL* checked) const
{
    if (checked) *checked = (m_flags0 & F0_SHOWSTATUSWINDOW) != 0;
    return TRUE;
}

BOOL CChatApp::OnUpdateViewLoginNotifs(BOOL* checked) const
{
    if (checked) *checked = m_bLoginNotifsShown;
    return TRUE;
}

BOOL CChatApp::StartDownloadingAvatar(CUserInfo* user, CChatDoc*, BOOL)
{
    if (!user) return FALSE;
    user->SetFlag(UF_AUTODOWNLOAD | UF_INTERACTIVEDOWNLOAD, false);
    return FALSE;
}

BOOL CChatApp::StartDownloadingBackdrop(const char*, const char*)
{
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
