// Ported from v2.5-beta-1-modern/chatdoc.cpp.
// History and OLE are not ported here; this file owns current room/UI state.

#include "chatdoc.h"

#include "bodycam.h"
#include "backdrop.h"
#include "avatar.h"
#include "chat.h"
#include "chatview.h"
#include "ircproto.h"
#include "histent.h"
#include "intl.h"
#include "memblst.h"
#include "mainfrm.h"
#include "pageview.h"
#include "panel.h"
#include "proppage.h"
#include "protsupp.h"
#include "saywnd.h"
#include "setupdlg.h"
#include "tabbar.h"
#include "textview.h"
#include "whisprbx.h"
#include "originalassets.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <QtGlobal>

#include <cstring>
#include <utility>

namespace {
CChatDoc* g_doc = nullptr;

void showDocumentMessage(const QString& resourceIdentifier,
                         const QString& replacement = QString())
{
    QString message = originalResourceString(resourceIdentifier);
    if (!replacement.isNull())
        message.replace(QStringLiteral("%1"), replacement);
    QMessageBox box(
        QMessageBox::Information,
        originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
        message, QMessageBox::Ok, theApp.m_pMainWnd.data());
    box.setObjectName(resourceIdentifier);
    box.exec();
}

bool hasConversationExtension(const QString& path, const char* extension)
{
    return path.right(3).compare(QString::fromLatin1(extension),
                                 Qt::CaseInsensitive) == 0;
}

bool hasSelectedFileExtension(const QString& path, const char* extension)
{
    return QFileInfo(path).suffix().compare(
               QString::fromLatin1(extension),
               Qt::CaseInsensitive) == 0;
}

bool hasCompoundFileMagic(const QByteArray& bytes)
{
    static constexpr unsigned char magic[] = {
        0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1
    };
    return bytes.size() >= static_cast<qsizetype>(sizeof(magic))
        && std::memcmp(bytes.constData(), magic, sizeof(magic)) == 0;
}
}

QList<CChatDoc*> g_docs;

CChatDoc::CChatDoc()
{
    m_bComicView = theApp.m_bComicView;
    m_bIconMembers = m_bLastMemberView = theApp.m_bIconMembers;
    m_myBackDropID = static_cast<unsigned int>(GetCurrentBackDropID());
    m_strStatus = originalResourceString(QStringLiteral("ID_DISCONNECTED"));
    m_proto = static_cast<CIrcProto*>(NewDefaultProto(this));
    g_docs.prepend(this);
}

CChatDoc::~CChatDoc()
{
    DeleteContents();
    const bool ownedCurrentRoom = currentRoom == m_proto;
    g_docs.removeOne(this);
    CChatDoc* nextDocument = nullptr;
    for (CChatDoc* document : std::as_const(g_docs)) {
        if (document && !document->IsCloseStarted()) {
            nextDocument = document;
            break;
        }
    }
    if (theApp.m_pDoc == this) theApp.m_pDoc = nextDocument;
    if (g_doc == this || (g_doc && g_doc->IsCloseStarted())) {
        g_doc = nextDocument;
        if (g_doc) g_doc->LoadDocData();
        else {
            currentRoom = nullptr;
            g_puiSelf = nullptr;
            g_mapNickToPtr = nullptr;
        }
    } else if (ownedCurrentRoom) {
        if (g_doc) g_doc->LoadDocData();
        else {
            currentRoom = nullptr;
            g_puiSelf = nullptr;
            g_mapNickToPtr = nullptr;
        }
    }
    delete m_proto;
    m_proto = nullptr;
}

int CChatDoc::FindFileType(const QString& pathName) const
{
    const QString extension = pathName.right(4);
    if (extension.compare(QStringLiteral(".ccr"),
                          Qt::CaseInsensitive) == 0) {
        return FT_CCR;
    }
    if (extension.compare(QStringLiteral(".rtf"),
                          Qt::CaseInsensitive) == 0) {
        return FT_RTF;
    }
    return FT_CCC;
}

bool CChatDoc::OnNewDocument()
{
    if (m_bDocumentInitialized || !m_pages.isEmpty()
        || !m_history.isEmpty() || !m_allChannelPuis.isEmpty()) {
        DeleteContents();
    }
    m_bContentsDeleted = false;
    m_bCloseStarted = false;
    m_bDocumentInitialized = false;
    m_bArchived = false;
    m_bChatInitializeScheduled = false;
    m_bChatInitializeComplete = false;
    ++m_chatInitializeGeneration;
    m_fileType = FT_CCR;
    m_strPathName.clear();
    InitMyDocument();
    SetModifiedFlag(false);
    return true;
}

bool CChatDoc::OnOpenDocument(const QString& pathName)
{
    m_fileType = static_cast<char>(FindFileType(pathName));
    m_bChatInitializeScheduled = false;
    m_bChatInitializeComplete = false;
    ++m_chatInitializeGeneration;
    DeleteContents();
    m_bContentsDeleted = false;
    m_bCloseStarted = false;
    m_bDocumentInitialized = false;
    m_bArchived = false;
    InitMyDocument();

    QFile file(pathName);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.readAll();
    // Modern's normal CCC/CCR files are CFB because
    // CDocObjectServerDoc enables compound storage. That COM/OLE container is
    // deliberately deferred; never feed its raw sectors to the flat adapter.
    if (hasCompoundFileMagic(bytes)) return false;

    QString payload = IntlTextToQString(bytes.constData(), bytes.size());
    QTextStream stream(&payload, QIODevice::ReadOnly);
    bool loaded = false;
    if (m_fileType == FT_CCR) {
        loaded = ChatLoadLocator(stream, TRUE, TRUE, &g_nCXKeepServer);
    } else if (payload.startsWith(
                   QStringLiteral("#CHATCONVERSATION"),
                   Qt::CaseInsensitive)) {
        loaded = ChatLoadConversation(stream);
    } else {
        // Modern reaches this locator path only after its compound open has
        // failed. Dispatching by the flat header avoids first reporting the
        // same payload as a bad conversation.
        loaded = ChatLoadLocator(stream, TRUE, FALSE, &g_nCXKeepServer);
    }
    if (!loaded) return false;

    m_strPathName = QFileInfo(pathName).absoluteFilePath();
    SetTitle(QFileInfo(pathName).fileName());
    SetModifiedFlag(false);
    return true;
}

bool CChatDoc::ParseLocatorFile(const QString& pathName)
{
    QFile file(pathName);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.readAll();
    if (hasCompoundFileMagic(bytes)) return false;
    QString payload = IntlTextToQString(bytes.constData(), bytes.size());
    QTextStream stream(&payload, QIODevice::ReadOnly);
    return ChatLoadLocator(stream, TRUE, FALSE, &g_nCXKeepServer);
}

bool CChatDoc::OnSaveDocument(const QString& pathName)
{
    m_fileType = static_cast<char>(FindFileType(pathName));
    if (m_fileType == FT_RTF) {
        if (!m_bComicView) {
            if (m_textView && WriteRTF(m_textView->m_pRichEdit, pathName))
                return true;
            showDocumentMessage(QStringLiteral("ID_ERR_SAVE"), pathName);
            return false;
        }
        showDocumentMessage(QStringLiteral("ID_RTF_NO_COMICS"));
        return true;
    }

    QString payload;
    QTextStream stream(&payload, QIODevice::WriteOnly);
    if (m_fileType == FT_CCR) {
        if (!ChatSaveLocator(stream)) return false;
    } else {
        ChatSaveConversation(stream);
    }
    stream.flush();
    const QByteArray encoded = IntlTextFromQString(QStringView(payload));
    QFile file(pathName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(encoded) == encoded.size();
}

bool CChatDoc::DoSave(const QString& pathName, bool replace)
{
    QString newName = pathName;
    const bool prompted = newName.isEmpty();
    if (prompted) {
        QString filter =
            originalResourceString(QStringLiteral("IDS_CCC_FILTER"));
        if (!m_bComicView) {
            filter.prepend(
                originalResourceString(QStringLiteral("IDS_RTF_FILTER")));
        }

        QString initial = m_strPathName;
        if (initial.isEmpty()) initial = GetTitle();
        if (!hasConversationExtension(initial, g_szCCCExt)
            && !hasConversationExtension(initial, g_szRTFExt)) {
            initial += QLatin1Char('.')
                + QString::fromLatin1(m_bComicView
                                         ? g_szCCCExt : g_szRTFExt);
        }

        CChatFileDialog dialog(
            FALSE, QString::fromLatin1(g_szCCCExt), initial,
            filter, theApp.m_pMainWnd.data());
        dialog.SetFilterIndex(
            !m_bComicView
                && hasConversationExtension(initial, g_szCCCExt)
            ? 2 : 1);
        if (dialog.exec() != QDialog::Accepted) return false;
        newName = dialog.selectedFiles().value(0);
        if (!hasSelectedFileExtension(newName, g_szCCCExt)
            && !hasSelectedFileExtension(newName, g_szRTFExt)) {
            newName += QLatin1Char('.')
                + QString::fromLatin1(g_szCCCExt);
        }
    }

    if (!OnSaveDocument(newName)) {
        if (prompted) QFile::remove(newName);
        return false;
    }

    if (replace) {
        const bool keepTitle = m_proto
            && m_proto->m_strChannel != QStringLiteral(": :");
        const QString oldTitle = GetTitle();
        m_strPathName = QFileInfo(newName).absoluteFilePath();
        SetTitle(QFileInfo(newName).fileName());
        if (keepTitle) SetTitle(oldTitle);
        if (!theApp.m_bEmbedded) SetModifiedFlag(false);
    }
    return true;
}

bool CChatDoc::SaveModified(QWidget* parent)
{
    if (!IsModified()) return true;

    QMessageBox prompt(
        QMessageBox::Warning,
        originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
        GetTitle(),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        parent ? parent : theApp.m_pMainWnd.data());
    prompt.setObjectName(QStringLiteral("CChatDocSaveModified"));
    prompt.setDefaultButton(QMessageBox::Save);
    prompt.setEscapeButton(QMessageBox::Cancel);
    const int answer = prompt.exec();
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Discard) return true;
    if (answer != QMessageBox::Save) return false;
    return DoSave(m_strPathName, true);
}

void CChatDoc::DeleteContents()
{
    if (m_bContentsDeleted) return;
    m_bContentsDeleted = true;

    DestroyPages();
    if (!m_bComicView && m_textView) m_textView->ClearTextView();
    if (m_bComicView && m_view) m_view->ResetExistingPanels(FALSE);
    DestroyHistory();
    if (m_proto) m_proto->ChatPartChannel(this, FALSE);
    ChatEmptyMemberList(this);
    DestroyUserState();
    m_comicsTitle.clear();
    m_bDocumentInitialized = false;
}

void CChatDoc::OnCloseDocument()
{
    if (m_bCloseStarted) return;
    m_bCloseStarted = true;
    theApp.m_pExitingDoc = this;
    if (theApp.m_bEmbedded) {
        if (m_proto) m_proto->ChatPartChannel(this, FALSE);
        return;
    }
    DeleteContents();
}

void CChatDoc::SaveShortcut(const QString& pathName)
{
    DoSave(pathName, false);
}

void CChatDoc::SetLegalPath(const QString& roomName, BOOL addToMRU)
{
    Q_UNUSED(addToMRU);
    QString legal = roomName;
    static const QString invalid = QStringLiteral("\\/:*?\"<>|.");
    for (const QChar character : invalid)
        legal.replace(character, QLatin1Char('%'));
    const bool replaced = legal != roomName;
    m_strPathName = legal;
    SetTitle(legal);
    if (replaced) SetTitle(roomName);
}

bool CChatDoc::CleanupExistingWindows()
{
    CChatDoc* reuse = nullptr;
    const QList<CChatDoc*> documents = g_docs;
    for (CChatDoc* document : documents) {
        if (!document || document->IsCloseStarted()
            || !document->m_proto
            || document->m_proto->GetType() != PC_IRC
            || document->m_bStatusView) {
            continue;
        }
        if (!reuse) {
            reuse = document;
            continue;
        }
        if (!document->SaveModified(theApp.m_pMainWnd.data()))
            return false;
        document->OnCloseDocument();
        if (theApp.m_pMainWnd)
            theApp.m_pMainWnd->CloseDocument(document);
    }

    if (reuse) {
        if (!reuse->SaveModified(theApp.m_pMainWnd.data()))
            return false;
        reuse->OnNewDocument();
        if (reuse->m_proto) reuse->m_proto->m_strChannel.clear();
    }
    return true;
}

CUserInfo* CChatDoc::GetNextSelectedMember(int& index) const
{
    return m_memberList ? m_memberList->GetNextSelectedMember(index) : nullptr;
}

int CChatDoc::SelectedMemberCount() const
{
    return m_memberList ? m_memberList->SelectedMemberCount() : 0;
}

CUserInfo* CChatDoc::GetSingleSelectedMember() const
{
    if (SelectedMemberCount() != 1) return nullptr;
    int index = -1;
    return GetNextSelectedMember(index);
}

void CChatDoc::OnSetfont()
{
    if (m_bComicView) SetComicsFont();
    else SetTextFont();
}

void CChatDoc::OnSetColor()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->SwitchSelectionFormat(wForeground);
}

void CChatDoc::OnSwitchBold()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->SwitchSelectionFormat(wBold);
}

void CChatDoc::OnSwitchItalic()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->SwitchSelectionFormat(wItalic);
}

void CChatDoc::OnSwitchUnderlined()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->SwitchSelectionFormat(wUnderline);
}

void CChatDoc::OnSwitchFixedPitch()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->SwitchSelectionFormat(wFixedPitch);
}

void CChatDoc::OnSwitchSymbol()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->SwitchSelectionFormat(wSymbol);
}

BOOL CChatDoc::OnUpdateFormat(UINT commandID, BOOL* checked) const
{
    auto* say = dynamic_cast<CSayWnd*>(m_sayWnd);
    if (checked) *checked = FALSE;
    if (!say) return FALSE;

    const WORD formats = say->wGetConsistentFormats();
    WORD mask = 0;
    switch (commandID) {
    case ID_SWITCHBOLD:
        mask = wBold;
        break;
    case ID_SWITCHITALIC:
        mask = wItalic;
        break;
    case ID_SWITCHUNDERLINED:
        mask = wUnderline;
        break;
    case ID_SWITCHFIXEDPITCH:
        mask = wFixedPitch;
        break;
    case ID_SWITCHSYMBOL:
        mask = wSymbol;
        break;
    case ID_SETCOLOR:
        break;
    default:
        return FALSE;
    }
    if (checked && mask) *checked = (formats & mask) != 0;
    return TRUE;
}

void CChatDoc::OnFileClose()
{
    if (!theApp.m_pMainWnd) return;
    if (m_bStatusView) {
        theApp.OnViewStatuswindow();
        return;
    }
    theApp.m_pMainWnd->CloseDocument(this);
}

void CChatDoc::OnMemberGetinfo()
{
    if (!m_proto) return;
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index))
        m_proto->ChatGetInfo(pui);
}

void CChatDoc::OnMemberIgnore()
{
    bool enable = false;
    bool allIgnored = true;
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index)) {
        if (!pui->IsSelf()) {
            enable = true;
            if (!pui->Ignored()) {
                allIgnored = false;
                break;
            }
        }
    }
    CRoomInfo* protocol = GetDefaultProto();
    if (!protocol) return;
    index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index))
        protocol->DoIgnoreUser(pui, !allIgnored, false);
}

void CChatDoc::OnAddToNotifs()
{
    if (SelectedMemberCount() != 1 || theApp.m_iAutoPage != -1) return;
    int index = -1;
    CUserInfo* pui = GetNextSelectedMember(index);
    if (!pui || pui->GetFullName().isEmpty()) return;
    theApp.m_dynaNotifs.SetStartUpIdent(pui->GetFullName());
    theApp.m_iAutoPage = 1;
    QTimer::singleShot(0, [] { theApp.OnViewAutomations(); });
}

BOOL CChatDoc::OnUpdateMemberGetinfo() const
{
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index)) {
        if (pui->IsComicUser()) return TRUE;
    }
    return FALSE;
}

BOOL CChatDoc::OnUpdateMemberIgnore(BOOL* checked) const
{
    BOOL enabled = FALSE;
    BOOL allIgnored = TRUE;
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index)) {
        if (pui->IsSelf()) continue;
        enabled = TRUE;
        if (!pui->Ignored()) {
            allIgnored = FALSE;
            break;
        }
    }
    if (checked) *checked = enabled && allIgnored;
    return enabled;
}

BOOL CChatDoc::OnUpdateAddToNotifs() const
{
    if (SelectedMemberCount() != 1 || theApp.m_iAutoPage != -1)
        return FALSE;
    int index = -1;
    CUserInfo* pui = GetNextSelectedMember(index);
    return pui && !pui->GetFullName().isEmpty();
}

void CChatDoc::OnGetidentity()
{
    if (!m_proto) return;
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index))
        m_proto->ChatGetIdentity(pui);
}

void CChatDoc::OnGetComicCharacter()
{
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index)) {
        if (!pui->IsAvatarReal() && g_bCanViewUnrated)
            theApp.StartDownloadingAvatar(pui, this, TRUE);
    }
}

void CChatDoc::OnGetVersion()
{
    if (!m_proto) return;
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index))
        m_proto->ChatGetVersion(pui);
}

void CChatDoc::OnPingUser()
{
    if (!m_proto) return;
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index))
        m_proto->ChatPingUser(pui);
}

void CChatDoc::OnGetLocaltime()
{
    if (!m_proto) return;
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index))
        m_proto->ChatGetLocalTime(pui);
}

void CChatDoc::OnWhisperboxMlist()
{
    if (CUserInfo* pui = GetSingleSelectedMember()) WhisperBox(pui);
}

void CChatDoc::OnSendEmail()
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (pui && m_proto) m_proto->ChatGetEmail(pui);
}

void CChatDoc::OnVisitHomepage()
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (pui && m_proto) m_proto->ChatGetHomePage(pui);
}

BOOL CChatDoc::OnUpdate1SelectionNotSelf() const
{
    CUserInfo* pui = GetSingleSelectedMember();
    return pui && !pui->IsSelf();
}

BOOL CChatDoc::OnUpdateComicUserNotSelf() const
{
    CUserInfo* pui = GetSingleSelectedMember();
    return pui && !pui->IsSelf() && pui->IsComicUser();
}

BOOL CChatDoc::OnUpdateVisitHomepage() const
{
    CUserInfo* pui = GetSingleSelectedMember();
    return pui && pui->IsComicUser();
}

void CChatDoc::OnAdministratorKick()
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (pui && m_proto) m_proto->ChatKickUser(pui);
}

void CChatDoc::OnAdminBan()
{
    if (m_proto) m_proto->ChatBanUser(GetSingleSelectedMember());
}

BOOL CChatDoc::OnUpdateAdminBan() const
{
    CUserInfo* pui = nullptr;
    int selected = 2;
    if (m_memberList) {
        selected = SelectedMemberCount();
        if (selected == 1) pui = GetSingleSelectedMember();
    }
    return selected < 2 && (!pui || !pui->IsSelf());
}

void CChatDoc::OnAdminBgrndsync()
{
    if (!m_bComicView || !m_proto) return;
    const char* backdropName =
        GetBackDropNameFromID(m_myBackDropID);
    const char* backdropUrl =
        GetBackDropURLFromID(m_myBackDropID);
    if (backdropName) {
        m_proto->ChatSyncBackDrop(
            this, QString::fromLocal8Bit(backdropName),
            backdropUrl ? QString::fromLocal8Bit(backdropUrl)
                        : QString());
    }
}

void CChatDoc::OnInvite()
{
    if (m_proto) m_proto->ChatInvite();
}

BOOL CChatDoc::OnUpdateInvite() const
{
    return bCanInvite();
}

void CChatDoc::OnMakeadmin()
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (pui && m_proto && GetConnectionStatus() == CX_INCHANNEL)
        m_proto->ChatSetOperator(pui, UM_HOST);
}

void CChatDoc::OnMakespeaker()
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (pui && m_proto && GetConnectionStatus() == CX_INCHANNEL)
        m_proto->ChatSetOperator(pui, UM_SPEAKER);
}

void CChatDoc::OnMakespectator()
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (pui && m_proto && GetConnectionStatus() == CX_INCHANNEL)
        m_proto->ChatSetOperator(pui, UM_SPECTATOR);
}

BOOL CChatDoc::OnUpdateMakeadmin(BOOL* checked) const
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (checked) *checked = pui && pui->IsOperator();
    return pui && g_puiSelf && g_puiSelf->IsOperator()
        && GetConnectionStatus() == CX_INCHANNEL;
}

BOOL CChatDoc::OnUpdateMakespeaker(BOOL* checked) const
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (checked)
        *checked = pui && pui->IsSpeaker() && !pui->IsOperator();
    return pui && g_puiSelf && g_puiSelf->IsOperator()
        && GetConnectionStatus() == CX_INCHANNEL;
}

BOOL CChatDoc::OnUpdateMakespectator(BOOL* checked) const
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (checked) *checked = pui && pui->IsSpectator();
    return pui && g_puiSelf && g_puiSelf->IsOperator()
        && GetConnectionStatus() == CX_INCHANNEL && m_proto
        && (m_proto->m_dwModes & CM_MODERATED);
}

void CChatDoc::OnChannelprops()
{
    if (m_proto) m_proto->DoChannelDialog();
}

BOOL CChatDoc::OnUpdateChannelprops() const
{
    return GetConnectionStatus() == CX_INCHANNEL && g_puiSelf
        && m_puiSelf && !m_allChannelPuis.isEmpty();
}

BOOL CChatDoc::OnUpdateAdminBgrndsync() const
{
    return GetConnectionStatus() == CX_INCHANNEL && m_bComicView;
}

BOOL CChatDoc::OnUpdateGetidentity() const
{
    return GetConnectionStatus() == CX_INCHANNEL
        && SelectedMemberCount() > 0;
}

BOOL CChatDoc::OnUpdateGetComicCharacter() const
{
    if (!g_bCanViewUnrated
        || GetConnectionStatus() != CX_INCHANNEL
        || SelectedMemberCount() == 0) {
        return FALSE;
    }
    int index = -1;
    while (CUserInfo* pui = GetNextSelectedMember(index)) {
        if (!pui->IsAvatarReal()) return TRUE;
    }
    return FALSE;
}

void CChatDoc::OnLeave()
{
    OnFileClose();
}

BOOL CChatDoc::OnUpdateFilePrint() const
{
    return TRUE;
}

BOOL CChatDoc::OnUpdateFileSave() const
{
    return TRUE;
}

BOOL CChatDoc::OnUpdateFileSaveAs() const
{
    return TRUE;
}

BOOL CChatDoc::OnUpdateLeave() const
{
    return GetConnectionStatus() == CX_INCHANNEL;
}

ConnectionStatus CChatDoc::GetConnectionStatus() const
{
    return m_proto ? m_proto->GetConnectionStatus() : CX_DISCONNECTED;
}

void CChatDoc::SetComicsTitle(const QString& title)
{
    m_comicsTitle = title;
}

void CChatDoc::SetComicsTitle2(const QString& title)
{
    m_comicsTitle = title;
    if (m_bComicView && m_view) m_view->ResetExistingPanels(TRUE);
}

void CChatDoc::SetTitle(const QString& title)
{
    m_title = title;
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->UpdateDocumentTitle(this, title);
}

QString CChatDoc::GetComicsTitle()
{
    if (m_bComicView && m_comicsTitle.isEmpty()) m_comicsTitle = GetRandomTitle();
    return m_comicsTitle;
}

void CChatDoc::InitMyDocument()
{
    if (m_bDocumentInitialized) return;
    m_bContentsDeleted = false;
    m_bDocumentInitialized = true;
    const QByteArray defaultArtDirectory = theApp.m_strDefaultArtDir.toLocal8Bit();
    SetArtDir(defaultArtDirectory.constData());
    if (!m_bComicView) return;
    Q_ASSERT(m_pages.isEmpty());
    AddNewPage();
    if (!m_pages.isEmpty()) {
        const QByteArray title = IntlTextFromQString(
            QStringView(GetComicsTitle()));
        m_pages.first()->AddTitle(title.constData());
    }
}

void CChatDoc::LoadDocData()
{
    currentRoom = m_proto;
    g_puiSelf = m_puiSelf;
    g_mapNickToPtr = &m_mapNickToPtr;
}

void CChatDoc::SaveConnectStatus(const QString& status)
{
    m_strStatus = status;
}

void CChatDoc::ResetStatus(bool left, bool right)
{
    if (GetChatDoc() != this) return;
    if (left) {
        theApp.SetStatusPaneString(0, m_strStatus);
    }
    if (right && !m_bStatusView) {
        const int members = m_memberList ? m_memberList->count() : 0;
        QString memberText = originalResourceString(
            members == 1 ? QStringLiteral("ID_USER_SINGULAR")
                         : QStringLiteral("ID_USER_PLURAL"));
        memberText.replace(QStringLiteral("%1"), QString::number(members));
        theApp.SetStatusPaneString(1, memberText);
    }
}

void CChatDoc::RegisterNewContent()
{
    if (m_bNewContent || !theApp.m_pMainWnd
        || !theApp.m_pMainWnd->GetTabBar()) {
        return;
    }
    if (!m_bObscured) return;
    CTabBar* tabBar = theApp.m_pMainWnd->GetTabBar();
    tabBar->SetTabIcon(tabBar->FindTabNum(this),
                       m_bStatusView ? 3 : 1);
    m_bNewContent = true;
}

void CChatDoc::SetObscured(BOOL obscured)
{
    if (obscured == m_bObscured) return;
    if (m_bNewContent && !obscured) {
        if (theApp.m_pMainWnd && theApp.m_pMainWnd->GetTabBar()) {
            CTabBar* tabBar = theApp.m_pMainWnd->GetTabBar();
            tabBar->SetTabIcon(tabBar->FindTabNum(this),
                               m_bStatusView ? 2 : 0);
        }
        m_bNewContent = false;
    }
    m_bObscured = obscured;
}

QTextEdit* CChatDoc::GetFocusSayOrEdit(BOOL sayOnly) const
{
    QWidget* focused = theApp.m_pMainWnd
        ? theApp.m_pMainWnd->GetCommandFocusWidget()
        : QApplication::focusWidget();
    QTextEdit* candidates[4] = {nullptr, nullptr, nullptr, nullptr};

    if (auto* sayWindow = dynamic_cast<CSayWnd*>(m_sayWnd))
        candidates[0] = sayWindow->GetSayEdit();
    if (!sayOnly && !m_bComicView && m_textView)
        candidates[1] = m_textView->m_pRichEdit;

    if (CWhisperBox* whisper = GetWhisperBox()) {
        if (whisper->m_sayWnd)
            candidates[2] = whisper->m_sayWnd->GetSayEdit();
        if (!sayOnly) candidates[3] = whisper->GetCurrentEdit();
    }

    for (QTextEdit* candidate : candidates) {
        if (candidate && candidate == focused) return candidate;
    }
    return nullptr;
}

void CChatDoc::OnEditUndo()
{
    if (QTextEdit* edit = GetFocusSayOrEdit(TRUE)) edit->undo();
}

void CChatDoc::OnEditCut()
{
    if (QTextEdit* edit = GetFocusSayOrEdit(TRUE)) edit->cut();
}

void CChatDoc::OnEditCopy()
{
    if (QTextEdit* edit = GetFocusSayOrEdit(FALSE)) edit->copy();
}

void CChatDoc::OnEditPaste()
{
    if (QTextEdit* edit = GetFocusSayOrEdit(TRUE)) edit->paste();
}

void CChatDoc::OnEditDelete()
{
    if (QTextEdit* edit = GetFocusSayOrEdit(TRUE)) {
        QTextCursor cursor = edit->textCursor();
        cursor.removeSelectedText();
    }
}

void CChatDoc::OnEditSelectAll()
{
    if (QTextEdit* edit = GetFocusSayOrEdit(FALSE)) edit->selectAll();
}

BOOL CChatDoc::OnUpdateEditUndo() const
{
    QTextEdit* edit = GetFocusSayOrEdit(TRUE);
    return edit && edit->document()->isUndoAvailable();
}

BOOL CChatDoc::OnUpdateEditCut() const
{
    QTextEdit* edit = GetFocusSayOrEdit(TRUE);
    return edit && edit->textCursor().hasSelection();
}

BOOL CChatDoc::OnUpdateEditCopy() const
{
    QTextEdit* edit = GetFocusSayOrEdit(FALSE);
    return edit && edit->textCursor().hasSelection();
}

BOOL CChatDoc::OnUpdateEditPaste() const
{
    QTextEdit* edit = GetFocusSayOrEdit(TRUE);
    const QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* data = clipboard ? clipboard->mimeData() : nullptr;
    return edit && data && data->hasText();
}

BOOL CChatDoc::OnUpdateEditDelete() const
{
    return OnUpdateEditCut();
}

BOOL CChatDoc::OnUpdateEditSelectAll() const
{
    return GetFocusSayOrEdit(FALSE) != nullptr;
}

void CChatDoc::OnActionsSay()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->OnActionsSay();
}

void CChatDoc::OnActionsThink()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->OnActionsThink();
}

void CChatDoc::OnActionsWhisper()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->OnActionsWhisper();
}

void CChatDoc::OnSendAction()
{
    if (auto* say = dynamic_cast<CSayWnd*>(m_sayWnd))
        say->OnSendAction();
}

void CChatDoc::UpdateAdminMenu()
{
    if (theApp.m_pMainWnd)
        theApp.m_pMainWnd->UpdateAdminMenu(this);
}

void CChatDoc::InitHistory()
{
    DestroyHistory();
    AddAndExecute(new StartHistoryEntry(GetComicsTitle(),
                                        QString::fromUtf8(GetMyCharacter()), 0),
                  this);
    AddAndExecute(new ChangeBackDropEntry(theApp.m_lastBackDrop), this);
    SetModifiedFlag(false);
}

void CChatDoc::ExecuteHistory(int mode)
{
    const bool oldRefresh = theApp.m_bNoRefresh;
    if (mode != HM_LIVE) theApp.m_bNoRefresh = true;

    CChatDoc* oldDocument = GetChatDoc();
    if (oldDocument != this) SetChatDoc(this);
    for (HistoryEntry* entry : m_history) {
        if (entry) entry->Execute(mode, this);
    }
    if (oldDocument != this) SetChatDoc(oldDocument);
    theApp.m_bNoRefresh = oldRefresh;
}

void CChatDoc::DestroyHistory()
{
    qDeleteAll(m_history);
    m_history.clear();
}

void CChatDoc::ChatSaveConversation(QTextStream& stream) const
{
    stream << QStringLiteral("#CHATCONVERSATION\r\n");
    for (const HistoryEntry* entry : m_history) {
        if (entry) entry->WriteSelf(stream);
    }
}

bool CChatDoc::ChatLoadConversation(QTextStream& stream)
{
    m_bArchived = true;
    const QString firstLine = stream.readLine();
    if (!firstLine.startsWith(QStringLiteral("#CHATCONVERSATION"),
                              Qt::CaseInsensitive)) {
        showDocumentMessage(QStringLiteral("ID_ERR_NOT_CHATCONV"));
        return false;
    }

    const bool oldRefresh = theApp.m_bNoRefresh;
    theApp.m_bNoRefresh = true;
    while (!stream.atEnd()) {
        const QString record = stream.readLine();
        const QRegularExpressionMatch keywordMatch =
            QRegularExpression(QStringLiteral("^\\s*(\\S+)"))
                .match(record);
        if (!keywordMatch.hasMatch()) continue;
        const QString keyword = keywordMatch.captured(1);
        if (keyword.compare(QStringLiteral("say"), Qt::CaseInsensitive) == 0)
            AddAndExecute(new SayEntry(record, this), this);
        else if (keyword.compare(QStringLiteral("join"), Qt::CaseInsensitive) == 0
                 || keyword.compare(QStringLiteral("ejoin"), Qt::CaseInsensitive) == 0)
            AddAndExecute(new JoinEntry(record), this);
        else if (keyword.compare(QStringLiteral("part"), Qt::CaseInsensitive) == 0)
            AddAndExecute(new PartEntry(record, -1, true), this);
        else if (keyword.compare(QStringLiteral("changeavatar"), Qt::CaseInsensitive) == 0)
            AddAndExecute(new ChangeAvatarEntry(record), this);
        else if (keyword.compare(QStringLiteral("getinfo"), Qt::CaseInsensitive) == 0)
            AddAndExecute(new GetInfoEntry(record), this);
        else if (keyword.compare(QStringLiteral("comicchar"), Qt::CaseInsensitive) == 0)
            AddAndExecute(new ComicCharacterEntry(record), this);
        else if (keyword.compare(QStringLiteral("nick"), Qt::CaseInsensitive) == 0)
            AddAndExecute(new NickEntry(record), this);
        else if (keyword.compare(QStringLiteral("backdrop"), Qt::CaseInsensitive) == 0)
            AddAndExecute(new ChangeBackDropEntry(record, true), this);
        else if (keyword.compare(QStringLiteral("starthistory"), Qt::CaseInsensitive) == 0)
            AddAndExecute(new StartHistoryEntry(record), this);
        else
            showDocumentMessage(
                QStringLiteral("ID_ERR_BAD_CONV_FIELD"), keyword);
    }

    if (!m_puiSelf) {
        // Exact old/bad conversation repair from the source; this is never an
        // IRC JOIN/NAMES path and never seeds a live room.
        AddAndExecute(new JoinEntry(new CUserInfo(
                          QString::fromUtf8(GetMyNickName()))), this);
        SetModifiedFlag(false);
    }
    theApp.m_bNoRefresh = oldRefresh;
    if (m_bComicView && m_view) {
        m_view->UpdateScroll();
        m_view->viewport()->update();
    } else if (m_textView) {
        m_textView->show();
    }
    SetModifiedFlag(false);
    if (m_proto) m_proto->m_strChannel = QStringLiteral(": :");
    return true;
}

void CChatDoc::AddLine(UINT avatarId, const char* text, USHORT modes,
                       CDWordArray* formatting)
{
    if (m_pages.isEmpty()) AddNewPage();
    CPage* page = m_pages.isEmpty() ? nullptr : m_pages.last();
    if (page && !page->AddLine(avatarId, text, modes, formatting)) {
        AddNewPage();
        page = m_pages.isEmpty() ? nullptr : m_pages.first();
        if (page) page->AddLine(avatarId, text, modes, formatting);
    }
}

void CChatDoc::ProcessLine(UINT avatarId, const char* text, USHORT modes,
                           BYTE cooked, CDWordArray* formatting)
{
    if (!text) return;
    QString message = QString::fromUtf8(text);
    if (!cooked) ChatPreSendText(message, static_cast<int>(avatarId));
    CDWordArray* nativeFormatting = IntlFormattingFromUtf8(text, formatting);
    const QByteArray encoded = IntlTextFromQString(QStringView(message));
    if (std::strcmp(encoded.constData(), "<Brk>") != 0) TallySpeech(avatarId);
    AddLine(avatarId, encoded.constData(), modes, nativeFormatting);
    FreeAndNullFormatting(&nativeFormatting);
}

void CChatDoc::TallySpeech(UINT avatarId)
{
    if (CAvatarX* avatar = GetAvatar(static_cast<USHORT>(avatarId)))
        ++avatar->m_nSends;
}

void CChatDoc::ShowInfo(CUserInfo* pui, const QString& info,
                        bool onlyInComics, char hotLinkChar)
{
    if (!pui) return;
    if (m_bComicView) {
        const QByteArray message = IntlTextFromQString(QStringView(info));
        if (m_pages.isEmpty()) AddNewPage();
        CPage* page = m_pages.isEmpty() ? nullptr : m_pages.last();
        if (!pui->GetAvatarID()) AssignArbitraryAvatar(pui);
        if (page) page->ShowInfo(pui->GetAvatarID(), message.constData(),
                                 hotLinkChar);
    } else if (!onlyInComics && m_textView) {
        const QByteArray message = info.toUtf8();
        m_textView->ShowInfo(pui, message.constData());
    }
}

void CChatDoc::AddNewPage()
{
    auto* page = new CUnitPanelPage(this);
    page->m_topY = page->m_leftX = 0;
    m_pages.prepend(page);
}

void CChatDoc::DestroyPages()
{
    qDeleteAll(m_pages);
    m_pages.clear();
}

void CChatDoc::DestroyUserState()
{
    for (CUserInfo* pui : m_allChannelPuis) {
        if (!pui) continue;
        if (CAvatarX* avatar = GetAvatar(pui->GetAvatarID())) {
            if (avatar->m_userInfo == pui) avatar->m_userInfo = nullptr;
            avatar->m_nSends = 0;
        }
    }
    qDeleteAll(m_allChannelPuis);
    m_allChannelPuis.clear();
    m_mapNickToPtr.clear();
    if (m_puiSelf == g_puiSelf) g_puiSelf = nullptr;
    m_puiSelf = nullptr;
}

void CChatDoc::SetFocusToSayWnd()
{
    if (auto* sayWindow = dynamic_cast<CSayWnd*>(m_sayWnd))
        sayWindow->SetFocusToSayWnd();
    else if (m_sayWnd)
        m_sayWnd->setFocus();
}

QWidget* CChatDoc::GetComponentWindow(UINT component) const
{
    if (component == CHATFOCUS_OUTPUTWND)
        component = m_bComicView ? CHATFOCUS_COMICVIEW : CHATFOCUS_TEXTVIEW;
    switch (component) {
    case CHATFOCUS_INPUTWND:
        return m_sayWnd;
    case CHATFOCUS_EMOTIONWND:
        return m_bodyCam;
    case CHATFOCUS_COMICVIEW:
        return m_bComicView ? m_view : nullptr;
    case CHATFOCUS_TEXTVIEW:
        return !m_bComicView && m_textView ? m_textView->m_pRichEdit : nullptr;
    case CHATFOCUS_MEMBERLIST:
        return m_memberList ? m_memberList->FocusWidget() : nullptr;
    case CHATFOCUS_TABBAR:
        return theApp.m_pMainWnd && theApp.m_pMainWnd->GetTabBar()
            ? theApp.m_pMainWnd->GetTabBar()->TabControl() : nullptr;
    default:
        return nullptr;
    }
}

void CChatDoc::CycleFocus(UINT currentFocus, bool backward)
{
    constexpr BYTE statusView = 1;
    constexpr BYTE comicView = 2;
    constexpr BYTE textView = 4;
    constexpr BYTE allViews = 7;
    constexpr BYTE roomViews = 6;
    constexpr BYTE textViews = 5;
    static const BYTE tabOrder[] = {
        CHATFOCUS_TABBAR,
        CHATFOCUS_COMICVIEW,
        CHATFOCUS_TEXTVIEW,
        CHATFOCUS_INPUTWND,
        CHATFOCUS_MEMBERLIST,
        CHATFOCUS_EMOTIONWND
    };
    static const BYTE applicableTypes[] = {
        allViews, comicView, textViews, allViews, roomViews, comicView
    };
    const BYTE currentType = m_bStatusView ? statusView
        : (m_bComicView ? comicView : textView);

    int index = 0;
    while (index < 6 && tabOrder[index] != currentFocus) ++index;
    if (index == 6) return;
    do {
        index = (index + (backward ? 5 : 1)) % 6;
    } while (!(applicableTypes[index] & currentType)
             || (tabOrder[index] == CHATFOCUS_TABBAR
                 && theApp.m_bEmbedded));

    QWidget* next = GetComponentWindow(tabOrder[index]);
    if (!next) return;
    if (tabOrder[index] == CHATFOCUS_MEMBERLIST && m_memberList)
        m_memberList->EnsureFocusItem();
    next->setFocus();
}

void CChatDoc::OnViewComics()
{
    if (m_bComicView) return;

    theApp.m_bFoundArt = ArtDirsOK();
    if (!theApp.m_bFoundArt) {
        if (QMessageBox::question(
                m_client,
                originalResourceString(
                    QStringLiteral("ID_MESSAGE_BOX_TITLE")),
                originalResourceString(QStringLiteral("IDS_NEEDART")),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) == QMessageBox::Yes) {
            theApp.OnHelpFreestuff();
        }
        return;
    }

    m_fileType = FT_CCC;
    MapNullAvatars(this);
    if (m_client) m_client->CreateComicView(true);
    UpdateComicCharacterMenu();
}

void CChatDoc::OnViewIcon()
{
    theApp.m_bIconMembers = m_bIconMembers = true;
    if (m_memberList) {
        m_memberList->SetIconMode(true);
    }
}

void CChatDoc::OnViewListAux()
{
    m_bIconMembers = false;
    if (m_memberList) {
        m_memberList->SetIconMode(false);
    }
}

void CChatDoc::OnViewList()
{
    OnViewListAux();
    theApp.m_bIconMembers = false;
}

void CChatDoc::OnViewText()
{
    if (!m_bComicView) return;
    theApp.m_bComicView = false;
    m_bComicView = false;
    m_fileType = FT_CCC;
    if (m_client) m_client->CreateTextView(true);
    UpdateComicCharacterMenu();
}

BOOL CChatDoc::OnUpdateViewComics(BOOL* checked) const
{
    if (checked) *checked = m_bComicView;
    return !m_proto || !(m_proto->m_dwModes & CM_NOFORMAT);
}

BOOL CChatDoc::OnUpdateViewText(BOOL* checked) const
{
    if (checked) *checked = !m_bComicView;
    return TRUE;
}

BOOL CChatDoc::OnUpdateViewIcon(BOOL* checked) const
{
    if (checked) *checked = m_bComicView && m_bIconMembers;
    return m_bComicView;
}

BOOL CChatDoc::OnUpdateViewList(BOOL submenu, BOOL* checked) const
{
    if (checked) *checked = !m_bIconMembers;
    if (!submenu) return TRUE;
    return GetConnectionStatus() == CX_INCHANNEL && m_bComicView;
}

void CChatDoc::OnMacro(UINT commandID)
{
    const INT macro = static_cast<INT>(commandID) - ID_MACRO_A0;
    if (macro < 0 || macro >= NMACROS) return;
    theApp.m_macros[macro].Invoke();
}

BOOL CChatDoc::OnUpdateMacro(UINT commandID) const
{
    const INT macro = static_cast<INT>(commandID) - ID_MACRO_A0;
    if (macro < 0 || macro >= NMACROS
        || !theApp.m_macros[macro].m_bDefined) return FALSE;
    QString contents = theApp.m_macros[macro].m_strValue;
    BOOL enabled = ExpandVariables(contents, const_cast<CChatDoc*>(this));
    const ConnectionStatus status = GetConnectionStatus();
    const BOOL disconnected = status == CX_DISCONNECTED
        || status == CX_CONNECTING;
    if (m_bStatusView) {
        if (disconnected) return FALSE;
        enabled = enabled && m_client && m_client->isVisible()
            && contents.startsWith(QLatin1Char('/'));
    } else if (disconnected && contents.startsWith(QLatin1Char('/'))) {
        enabled = FALSE;
    }
    return enabled;
}

void CChatDoc::UpdateMacroMenu()
{
    if (theApp.m_pMainWnd) theApp.m_pMainWnd->UpdateMacroMenu();
}

void CChatDoc::UpdateComicCharacterMenu(QMenu* menu)
{
    if (!menu) {
        if (theApp.m_pMainWnd
            && theApp.m_pMainWnd->GetActiveDocument() == this) {
            theApp.m_pMainWnd->RefreshCommandUi();
        }
        return;
    }

    QAction* characterAction = nullptr;
    for (QAction* action : menu->actions()) {
        if (action->data().toString()
            == QLatin1String("ID_MEMBER_GETCHAR")) {
            characterAction = action;
            break;
        }
    }

    const BOOL needsCharacterItem = bCanViewUnrated() && m_bComicView;
    const bool registeredMainMenu = theApp.m_pMainWnd
        && theApp.m_pMainWnd->IsRegisteredMemberMenu(menu);
    if (!needsCharacterItem) {
        if (characterAction) {
            if (registeredMainMenu)
                theApp.m_pMainWnd->RemoveDynamicCommand(
                    menu, characterAction);
            else {
                menu->removeAction(characterAction);
                delete characterAction;
            }
        }
        return;
    }

    if (!characterAction) {
        QAction* insertBefore = nullptr;
        const QList<QAction*> actions = menu->actions();
        for (INT position = actions.size() - 1; position >= 0; --position) {
            const QString command = actions.at(position)->data().toString();
            if (command == QLatin1String("ID_GETIDENTITY")
                || command == QLatin1String("ID_MEMBER_GETINFO")) {
                if (position + 1 < actions.size())
                    insertBefore = actions.at(position + 1);
                break;
            }
        }
        const QString text = originalResourceString(
            QStringLiteral("IDS_GET_CHARACTER"));
        if (registeredMainMenu) {
            characterAction =
                theApp.m_pMainWnd->InsertDynamicCommand(
                    menu, insertBefore, text,
                    QStringLiteral("ID_MEMBER_GETCHAR"));
        } else {
            characterAction = new QAction(text, menu);
            characterAction->setData(
                QStringLiteral("ID_MEMBER_GETCHAR"));
            characterAction->setStatusTip(originalResourceString(
                QStringLiteral("ID_MEMBER_GETCHAR")).section(
                    QLatin1Char('\n'), 0, 0));
            menu->insertAction(insertBefore, characterAction);
            QObject::connect(characterAction, &QAction::triggered,
                             menu, [] {
                if (CChatDoc* document = GetChatDoc())
                    document->OnGetComicCharacter();
            });
        }
    }

    BOOL enabled = FALSE;
    if (g_bCanViewUnrated && GetConnectionStatus() == CX_INCHANNEL
        && SelectedMemberCount() > 0) {
        int index = -1;
        while (CUserInfo* pui = GetNextSelectedMember(index)) {
            if (!pui->IsAvatarReal()) {
                enabled = TRUE;
                break;
            }
        }
    }
    characterAction->setEnabled(enabled);
}

void CChatDoc::OnClearHistory()
{
    const QMessageBox::StandardButton answer = QMessageBox::question(
        m_client,
        originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
        originalResourceString(QStringLiteral("IDS_CLEARHISTORY_PROMPT")),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    DestroyPages();
    if (m_bComicView) {
        if (m_view) m_view->ResetExistingPanels(TRUE);
    } else if (m_textView) {
        m_textView->ClearTextView();
    }
    QList<HistoryEntry*> avatarEntries;
    for (qsizetype index = m_history.size() - 1; index >= 0; --index) {
        HistoryEntry* entry = m_history.at(index);
        if (!entry || entry->GetType() != HT_AVATARENTRY) continue;
        auto* avatarEntry = static_cast<ChangeAvatarEntry*>(entry);
        if (!avatarEntry->m_pui || avatarEntry->m_pui->IsDeparted()) continue;
        bool alreadySaved = false;
        for (HistoryEntry* saved : avatarEntries) {
            if (static_cast<ChangeAvatarEntry*>(saved)->m_pui
                == avatarEntry->m_pui) {
                alreadySaved = true;
                break;
            }
        }
        if (!alreadySaved) {
            m_history.removeAt(index);
            avatarEntries.append(entry);
        }
    }

    DestroyHistory();
    InitHistory();
    for (CUserInfo* pui : m_allChannelPuis) {
        if (pui && !pui->IsDeparted()) AddEntry(new JoinEntry(pui), this);
    }
    for (HistoryEntry* entry : avatarEntries) AddEntry(entry, this);
}

CChatDoc* GetChatDoc()
{
    return g_doc;
}

void SetChatDoc(CChatDoc* doc)
{
    g_doc = doc;
    if (doc) {
        doc->LoadDocData();
        doc->InitMyDocument();
    } else {
        currentRoom = nullptr;
        g_puiSelf = nullptr;
        g_mapNickToPtr = nullptr;
    }
}

CChatDoc* LookupDoc(const QString& channel)
{
    for (CChatDoc* document : g_docs) {
        if (document && !document->IsCloseStarted() && document->m_proto
            && document->m_proto->m_strChannel.compare(
                   channel, Qt::CaseInsensitive) == 0) {
            return document;
        }
    }
    return nullptr;
}
