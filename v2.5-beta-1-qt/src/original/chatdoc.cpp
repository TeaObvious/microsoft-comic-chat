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

#include <QMessageBox>
#include <QAction>
#include <QMenu>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

#include <cstring>

namespace {
CChatDoc* g_doc = nullptr;
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
    g_docs.removeOne(this);
    if (g_doc == this) {
        g_doc = g_docs.isEmpty() ? nullptr : g_docs.first();
        if (g_doc) g_doc->LoadDocData();
        else {
            currentRoom = nullptr;
            g_puiSelf = nullptr;
            g_mapNickToPtr = nullptr;
        }
    }
    delete m_proto;
    m_proto = nullptr;
    DestroyPages();
    DestroyHistory();
    DestroyUserState();
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

void CChatDoc::OnAdministratorKick()
{
    CUserInfo* pui = GetSingleSelectedMember();
    if (pui && m_proto) m_proto->ChatKickUser(pui);
}

void CChatDoc::OnAdminBan()
{
    if (m_proto) m_proto->ChatBanUser(GetSingleSelectedMember());
}

void CChatDoc::OnInvite()
{
    if (m_proto) m_proto->ChatInvite();
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

void CChatDoc::OnChannelprops()
{
    if (m_proto) m_proto->DoChannelDialog();
}

void CChatDoc::OnLeave()
{
    if (theApp.m_pMainWnd) {
        theApp.m_pMainWnd->CloseDocument(this);
    } else if (m_proto) {
        m_proto->ChatPartChannel(this, false);
    }
}

ConnectionStatus CChatDoc::GetConnectionStatus() const
{
    return m_proto ? m_proto->GetConnectionStatus() : CX_DISCONNECTED;
}

void CChatDoc::SetComicsTitle(const QString& title)
{
    m_comicsTitle = title;
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
    m_bDocumentInitialized = true;
    if (!m_bComicView) return;
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
        return false;
    }

    const bool oldRefresh = theApp.m_bNoRefresh;
    theApp.m_bNoRefresh = true;
    while (!stream.atEnd()) {
        const QString record = stream.readLine();
        if (record.isEmpty()) continue;
        const QString keyword = record.section(QLatin1Char('\t'), 0, 0);
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
        // The original reports ID_ERR_BAD_CONV_FIELD and continues. A Qt
        // dialog is deliberately not invented at this non-UI parser boundary.
    }

    if (!m_puiSelf) {
        // Exact old/bad conversation repair from the source; this is never an
        // IRC JOIN/NAMES path and never seeds a live room.
        AddAndExecute(new JoinEntry(new CUserInfo(
                          QString::fromUtf8(GetMyNickName()))), this);
        SetModifiedFlag(false);
    }
    theApp.m_bNoRefresh = oldRefresh;
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
    } while (!(applicableTypes[index] & currentType));

    QWidget* next = GetComponentWindow(tabOrder[index]);
    if (!next) return;
    if (tabOrder[index] == CHATFOCUS_MEMBERLIST && m_memberList)
        m_memberList->EnsureFocusItem();
    next->setFocus();
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
    theApp.m_bIconMembers = m_bIconMembers = false;
    if (m_memberList) {
        m_memberList->SetIconMode(false);
    }
}

void CChatDoc::OnViewText()
{
    if (!m_bComicView) return;
    theApp.m_bComicView = false;
    m_bComicView = false;
    if (m_client) m_client->CreateTextView(true);
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
    if (!menu) return;

    QAction* characterAction = nullptr;
    for (QAction* action : menu->actions()) {
        if (action->data().toString()
            == QLatin1String("ID_MEMBER_GETCHAR")) {
            characterAction = action;
            break;
        }
    }

    const BOOL needsCharacterItem = bCanViewUnrated() && m_bComicView;
    if (!needsCharacterItem) {
        if (characterAction) {
            menu->removeAction(characterAction);
            delete characterAction;
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
        characterAction = new QAction(originalResourceString(
            QStringLiteral("IDS_GET_CHARACTER")), menu);
        characterAction->setData(QStringLiteral("ID_MEMBER_GETCHAR"));
        characterAction->setStatusTip(originalResourceString(
            QStringLiteral("ID_MEMBER_GETCHAR")).section(
                QLatin1Char('\n'), 0, 0));
        menu->insertAction(insertBefore, characterAction);
        QObject::connect(characterAction, &QAction::triggered, menu,
                         [] {
                             if (CChatDoc* document = GetChatDoc())
                                 document->OnGetComicCharacter();
                         });
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
        doc->InitMyDocument();
        doc->LoadDocData();
    } else {
        currentRoom = nullptr;
        g_puiSelf = nullptr;
        g_mapNickToPtr = nullptr;
    }
}

CChatDoc* LookupDoc(const QString& channel)
{
    for (CChatDoc* document : g_docs) {
        if (document && document->m_proto
            && document->m_proto->m_strChannel.compare(
                   channel, Qt::CaseInsensitive) == 0) {
            return document;
        }
    }
    return nullptr;
}
