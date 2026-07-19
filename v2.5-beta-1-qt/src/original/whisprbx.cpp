// Ported from v2.5-beta-1-modern/whisprbx.cpp.

#include "whisprbx.h"

#include "chat.h"
#include "chatdoc.h"
#include "ircproto.h"
#include "originalassets.h"
#include "protsupp.h"
#include "resource.h"
#include "setupdlg.h"
#include "textview.h"
#include "userinfo.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QTextEdit>
#include <QToolButton>

#include <algorithm>
#include <cstring>
#include <functional>

namespace {
constexpr int ID_RICHVIEW = 5;
constexpr int HISTMINHEIGHT = 44;
constexpr int INTERBUTTON = 7; // Source uses this value without MapDialogRect.

CWhisperBox* g_whisperBox = nullptr;

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

QFont resourceFont(const OriginalDialogResource& dialog)
{
    QFont font(dialog.fontFamily);
    if (dialog.fontPointSize > 0) font.setPointSize(dialog.fontPointSize);
    return font;
}

void appendResourceMenu(QMenu& menu, const QList<OriginalMenuItem>& items)
{
    for (const OriginalMenuItem& item : items) {
        if (item.type == OriginalMenuItemType::Separator) {
            menu.addSeparator();
        } else if (item.type == OriginalMenuItemType::Popup) {
            appendResourceMenu(menu, item.children);
        } else if (item.commandIdentifier != QLatin1String("ID_CLEAR_HISTORY")) {
            QAction* action = menu.addAction(item.text);
            action->setData(item.commandIdentifier);
        }
    }
}

QRect visibleRect(QRect saved)
{
    if (!saved.isValid() || saved.isEmpty()) return saved;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen && screen->availableGeometry().intersects(saved)) return saved;
    }
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return saved;
    const QRect available = screen->availableGeometry();
    saved.setSize(saved.size().boundedTo(available.size()));
    saved.moveTopLeft(available.topLeft());
    return saved;
}

QByteArray utf8(const QString& value)
{
    return value.toUtf8();
}
}

// Qt replacement for the resource's SysTabControl32 with TCS_BUTTONS and
// TCS_MULTILINE. It retains insertion order and wraps real tab buttons instead
// of introducing a separate conversation model.
class CWhisperTabCtrl : public QWidget {
public:
    explicit CWhisperTabCtrl(QWidget* parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("IDC_TAB1"));
        setFocusPolicy(Qt::StrongFocus);
        setAutoFillBackground(true);
    }

    int InsertItem(int index, const QString& text)
    {
        if (index < 0 || index > m_buttons.size()) return -1;
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFocusPolicy(Qt::StrongFocus);
        connect(button, &QToolButton::clicked, this, [this, button] {
            const int selected = m_buttons.indexOf(button);
            if (selected >= 0 && m_onSelection) m_onSelection(selected);
        });
        m_buttons.insert(index, button);
        layoutButtons();
        return index;
    }

    void DeleteItem(int index)
    {
        if (index < 0 || index >= m_buttons.size()) return;
        delete m_buttons.takeAt(index);
        if (m_current == index) m_current = -1;
        else if (m_current > index) --m_current;
        layoutButtons();
    }

    void SetItemText(int index, const QString& text)
    {
        if (index < 0 || index >= m_buttons.size()) return;
        m_buttons[index]->setText(text);
        layoutButtons();
    }

    void SetCurSel(int index)
    {
        if (index < 0 || index >= m_buttons.size()) return;
        m_current = index;
        m_buttons[index]->setChecked(true);
    }

    int GetCurSel() const { return m_current; }
    int GetItemCount() const { return m_buttons.size(); }

    QRect contentRectInParent() const
    {
        const int frame = 2;
        const int top = y() + frame + m_rows * m_buttonHeight;
        return {x() + frame, top,
                qMax(0, width() - 2 * frame),
                qMax(0, height() - (top - y()) - frame)};
    }

    void SetSelectionHandler(std::function<void(int)> handler)
    {
        m_onSelection = std::move(handler);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QWidget::paintEvent(event);
        QPainter painter(this);
        painter.setPen(QColor(128, 128, 128));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        painter.setPen(Qt::white);
        painter.drawLine(1, 1, width() - 2, 1);
        painter.drawLine(1, 1, 1, height() - 2);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        layoutButtons();
    }

private:
    void layoutButtons()
    {
        const int oldRows = m_rows;
        m_buttonHeight = qMax(1, fontMetrics().height() + 8);
        int x = 2;
        int y = 2;
        m_rows = m_buttons.isEmpty() ? 0 : 1;
        for (QToolButton* button : m_buttons) {
            const int buttonWidth = qMax(1,
                fontMetrics().horizontalAdvance(button->text()) + 16);
            if (x > 2 && x + buttonWidth > qMax(2, width() - 2)) {
                x = 2;
                y += m_buttonHeight;
                ++m_rows;
            }
            button->setGeometry(x, y, buttonWidth, m_buttonHeight);
            button->show();
            x += buttonWidth;
        }
        update();
        if (oldRows != m_rows && m_onGeometryChanged)
            m_onGeometryChanged();
    }

public:
    void SetGeometryChangedHandler(std::function<void()> handler)
    {
        m_onGeometryChanged = std::move(handler);
    }

private:
    QList<QToolButton*> m_buttons;
    int m_current = -1;
    int m_rows = 0;
    int m_buttonHeight = 0;
    std::function<void(int)> m_onSelection;
    std::function<void()> m_onGeometryChanged;
};

namespace {
class CWhisperEdit : public QTextEdit {
public:
    explicit CWhisperEdit(CWhisperBox* owner)
        : QTextEdit(owner), m_owner(owner)
    {
        setObjectName(QString::number(ID_RICHVIEW));
        setReadOnly(true);
        setAcceptRichText(true);
        setUndoRedoEnabled(false);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        viewport()->setMouseTracking(true);
    }

protected:
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (m_owner) m_owner->OnContextMenu(event->globalPos());
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        viewport()->setCursor(anchorAt(event->position().toPoint()).isEmpty()
                                  ? Qt::IBeamCursor
                                  : Qt::PointingHandCursor);
        QTextEdit::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton
            && !(event->modifiers() & Qt::ControlModifier)) {
            const QString link = anchorAt(event->position().toPoint());
            if (!link.isEmpty() && m_owner && m_owner->HandleLink(link)) {
                event->accept();
                return;
            }
        }
        QTextEdit::mousePressEvent(event);
    }

private:
    CWhisperBox* m_owner = nullptr;
};
}

CWhisperLeaf::CWhisperLeaf(CUserInfo* pui)
{
    if (!pui) return;
    m_label = pui->GetScreenName();
    m_nick = pui->GetName();
    m_bModified = FALSE;
    m_bIgnore = pui->Ignored();
    m_id = pui->GetFullName();
}

CWhisperLeaf::~CWhisperLeaf()
{
    if (m_richCore) {
        m_richCore->DetachTextViewHWnd();
        delete m_richCore;
    }
    delete m_richView;
}

CWhisperBox::CWhisperBox(QWidget* parent)
    : QDialog(parent,
              Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                  | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint)
{
    const QString resourceName = QStringLiteral("IDD_WHISPERBOX");
    const OriginalDialogResource dialog = originalDialogResource(resourceName);
    const QFont font = resourceFont(dialog);
    const DialogUnitMapper mapper(font);
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFont(font);
    setWindowTitle(dialog.caption);
    setMinimumSize(267, 175);
    resize(mapper.x(dialog.width), mapper.y(dialog.height));

    const OriginalDialogControl* tab = findControl(
        dialog, QStringLiteral("IDC_TAB1"));
    m_tabCtrl = new CWhisperTabCtrl(this);
    if (tab) m_tabCtrl->setGeometry(mapper.rect(*tab));
    m_tabCtrl->SetSelectionHandler([this](int index) { SwitchToTab(index); });
    m_tabCtrl->SetGeometryChangedHandler([this] { updateRichGeometry(); });

    QWidget* sayPosition = new QWidget(this);
    sayPosition->setObjectName(QStringLiteral("IDC_SAYPOSITION"));
    if (const OriginalDialogControl* control = findControl(
            dialog, QStringLiteral("IDC_SAYPOSITION"))) {
        sayPosition->setGeometry(mapper.rect(*control));
    }
    sayPosition->hide();

    m_deleteButton = new QPushButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_DELETE_TAB")), this);
    m_deleteButton->setObjectName(QStringLiteral("IDC_DELETE_TAB"));
    if (const OriginalDialogControl* control = findControl(
            dialog, QStringLiteral("IDC_DELETE_TAB"))) {
        m_deleteButton->setGeometry(mapper.rect(*control));
    }

    m_ignoreButton = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_IGNORE_WBOX")), this);
    m_ignoreButton->setObjectName(QStringLiteral("IDC_IGNORE_WBOX"));
    if (const OriginalDialogControl* control = findControl(
            dialog, QStringLiteral("IDC_IGNORE_WBOX"))) {
        m_ignoreButton->setGeometry(mapper.rect(*control));
    }

    QRect sayRect = sayPosition->geometry();
    sayRect.adjust(4, 0, -5, 0);
    m_sayWnd = new CSayWnd(TRUE, SB_WHISPER | SB_WACTION | SB_WSOUND, this);
    m_sayWnd->setGeometry(sayRect);
    m_sayHeight = sayRect.height();

    m_marginLeft = mapper.x(7);
    m_marginTop = mapper.y(7);
    m_marginRight = mapper.x(7);
    m_marginBottom = mapper.y(3);
    m_sayTopFromBottom = mapper.y(40);
    m_tabBottomFromBottom = mapper.y(46);

    connect(m_deleteButton, &QPushButton::clicked,
            this, &CWhisperBox::OnDeleteTab);
    connect(m_ignoreButton, &QCheckBox::clicked,
            this, &CWhisperBox::OnIgnoreWbox);

    installEventFilter(this);
    m_tabCtrl->installEventFilter(this);
    m_sayWnd->installEventFilter(this);
    m_sayWnd->GetSayEdit()->installEventFilter(this);
    setTabOrder(m_sayWnd->GetSayEdit(), m_deleteButton);
    setTabOrder(m_deleteButton, m_ignoreButton);

    updateChildGeometry();
}

CWhisperBox::~CWhisperBox()
{
    FreeLeaves();
}

void CWhisperBox::FreeLeaves()
{
    qDeleteAll(m_leaves);
    m_leaves.clear();
    m_currentIndex = -1;
}

int CWhisperBox::AddTab(CUserInfo* pui)
{
    if (!pui) return -1;
    const QString tabName = pui->GetScreenName();
    int place = 0;
    while (place < m_leaves.size()
           && m_leaves[place]->m_label.compare(
                  tabName, Qt::CaseInsensitive) <= 0) {
        ++place;
    }
    if (m_tabCtrl->InsertItem(place, tabName) < 0) return -1;

    auto* leaf = new CWhisperLeaf(pui);
    m_leaves.insert(place, leaf);
    if (place <= m_currentIndex) ++m_currentIndex;

    updateRichGeometry();
    leaf->m_richView = new CWhisperEdit(this);
    leaf->m_richView->setGeometry(m_richRect);
    leaf->m_richView->hide();
    leaf->m_richView->installEventFilter(this);
    leaf->m_richCore = new CTextCore;
    leaf->m_richCore->AttachTextViewHWnd(leaf->m_richView);
    leaf->m_richCore->bSetTextViewBufferMaxSize(65536);
    InitializeTextCore(leaf->m_richCore);

    if (m_currentIndex >= 0 && m_currentIndex < m_leaves.size()) {
        CWhisperLeaf* current = m_leaves[m_currentIndex];
        current->m_richView->setGeometry(m_richRect);
        current->m_richView->show();
        current->m_richView->raise();
    }

    if (m_richRect.height() < HISTMINHEIGHT) {
        const int increase = HISTMINHEIGHT - m_richRect.height();
        QRect windowRect = geometry();
        windowRect.setTop(windowRect.top() - increase / 2);
        windowRect.setBottom(windowRect.bottom() + increase - increase / 2);
        setGeometry(windowRect);
    }
    return place;
}

int CWhisperBox::GetTab(const QString& nick) const
{
    for (int index = 0; index < m_leaves.size(); ++index) {
        if (m_leaves[index]->m_nick == nick) return index;
    }
    return -1;
}

void CWhisperBox::SwitchToTab(int tabNumber)
{
    if (tabNumber < 0 || tabNumber >= m_leaves.size()
        || tabNumber == m_currentIndex) {
        return;
    }
    CWhisperLeaf* next = m_leaves[tabNumber];
    next->m_richView->setGeometry(m_richRect);
    next->m_richView->show();
    next->m_richView->raise();
    if (m_currentIndex >= 0 && m_currentIndex < m_leaves.size())
        m_leaves[m_currentIndex]->m_richView->hide();
    m_tabCtrl->SetCurSel(tabNumber);
    m_currentIndex = tabNumber;
    m_ignoreButton->setChecked(next->m_bIgnore != 0);
    SetModified(tabNumber, FALSE);
}

void CWhisperBox::SetModified(int tabNumber, BOOL value)
{
    if (tabNumber < 0 || tabNumber >= m_leaves.size()) return;
    if (value) {
        if (!isVisible()) {
            show();
            SwitchToTab(tabNumber);
        }
        if (isMinimized() || !isActiveWindow()) {
            if (!m_bInverted) {
                QApplication::alert(this, 0);
                m_bInverted = TRUE;
                // The source also plays the named system sound "Default sound".
                // Sound lookup/playback remains at mcithrd.*; no substitute is used.
            }
        }
    }
    if (tabNumber == m_currentIndex && value) return;
    CWhisperLeaf* leaf = m_leaves[tabNumber];
    if (leaf->m_bModified == value) return;
    leaf->m_bModified = value;
    m_tabCtrl->SetItemText(tabNumber,
        value ? leaf->m_label + QStringLiteral(" *") : leaf->m_label);
    updateRichGeometry();
}

void CWhisperBox::OnDeleteTab()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_leaves.size()) return;
    delete m_leaves.takeAt(m_currentIndex);
    const int nextTab = m_currentIndex > 0 ? m_currentIndex - 1 : 0;
    m_tabCtrl->DeleteItem(m_currentIndex);
    m_currentIndex = -1;
    if (m_tabCtrl->GetItemCount() == 0) hide();
    else SwitchToTab(nextTab);
    updateRichGeometry();
}

void CWhisperBox::OnIgnoreWbox()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_leaves.size()) return;
    CWhisperLeaf* leaf = m_leaves[m_currentIndex];
    CUserInfo* pui = LookupPui(leaf->m_nick);
    if (!pui || pui->IsDeparted())
        pui = ExternalPui(leaf->m_nick, leaf->m_id, true);
    if (pui) {
        if (CRoomInfo* protocol = GetDefaultProto())
            protocol->DoIgnoreUser(pui, m_ignoreButton->isChecked(), false);
    }
}

void CWhisperBox::OnOK()
{
    if (m_sayWnd) m_sayWnd->SendReturn();
}

void CWhisperBox::SaveWhisperCoords()
{
    if (m_bPostCreate && !isMinimized()) theApp.m_rectWhisper = geometry();
}

QTextEdit* CWhisperBox::GetCurrentEdit() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_leaves.size()) return nullptr;
    return m_leaves[m_currentIndex]->m_richView;
}

void CWhisperBox::OnContextMenu(const QPoint& screenPoint)
{
    QMenu menu(this);
    appendResourceMenu(menu,
        originalMenuResource(QStringLiteral("IDR_STATUSVIEW")));
    for (QAction* action : menu.actions()) {
        if (action->data().toString() == QLatin1String("ID_EDIT_COPY")) {
            QTextEdit* edit = GetCurrentEdit();
            action->setEnabled(edit && edit->textCursor().hasSelection());
        }
    }
    const QPoint point = screenPoint.x() == -1 && screenPoint.y() == -1
        ? mapToGlobal(rect().center()) : screenPoint;
    if (QAction* selected = menu.exec(point)) {
        if (selected->data().toString() == QLatin1String("ID_EDIT_COPY")) {
            if (QTextEdit* edit = GetCurrentEdit()) edit->copy();
        }
    }
}

BOOL CWhisperBox::HandleLink(const QString& link)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_leaves.size()) return FALSE;
    CWhisperLeaf* leaf = m_leaves[m_currentIndex];
    return leaf && leaf->m_richCore
        ? leaf->m_richCore->bHandleLink(link) : FALSE;
}

void CWhisperBox::CycleFocus(BOOL backward)
{
    QWidget* current = QApplication::focusWidget();
    if (!current || current->window() != this) current = m_sayWnd->GetSayEdit();
    QWidget* candidate = current;
    do {
        candidate = backward ? candidate->previousInFocusChain()
                             : candidate->nextInFocusChain();
        if (!candidate || candidate == current) return;
    } while (candidate->window() != this || !candidate->isVisibleTo(this)
             || !candidate->isEnabled()
             || candidate->focusPolicy() == Qt::NoFocus);
    candidate->setFocus(backward ? Qt::BacktabFocusReason : Qt::TabFocusReason);
}

void CWhisperBox::SendScrollKey(QKeyEvent* event)
{
    if (QTextEdit* edit = GetCurrentEdit())
        QApplication::sendEvent(edit, event);
}

bool CWhisperBox::event(QEvent* event)
{
    if (event->type() == QEvent::WindowActivate && m_bInverted) {
        QApplication::alert(this, 0);
        m_bInverted = FALSE;
    }
    return QDialog::event(event);
}

bool CWhisperBox::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) return true;
        if ((key->modifiers() & Qt::AltModifier)
            && key->key() >= Qt::Key_0 && key->key() <= Qt::Key_9) {
            theApp.m_macros[key->key() - Qt::Key_0].Invoke(
                nullptr, nullptr, FALSE, TRUE);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CWhisperBox::closeEvent(QCloseEvent* event)
{
    QDialog::closeEvent(event);
    if (CChatDoc* document = GetChatDoc()) document->SetFocusToSayWnd();
}

void CWhisperBox::contextMenuEvent(QContextMenuEvent* event)
{
    OnContextMenu(event->globalPos());
}

void CWhisperBox::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        OnOK();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void CWhisperBox::moveEvent(QMoveEvent* event)
{
    QDialog::moveEvent(event);
    SaveWhisperCoords();
}

void CWhisperBox::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updateChildGeometry();
    SaveWhisperCoords();
}

void CWhisperBox::reject()
{
    // Escape is explicitly filtered by the original dialog.
}

void CWhisperBox::updateChildGeometry()
{
    if (!m_deleteButton || !m_ignoreButton || !m_tabCtrl || !m_sayWnd) return;
    const int deleteWidth = m_deleteButton->width();
    const int deleteHeight = m_deleteButton->height();
    const int deleteX = width() - deleteWidth - m_marginRight;
    const int deleteY = height() - deleteHeight - m_marginBottom;
    m_deleteButton->move(deleteX, deleteY);

    const int ignoreX = deleteX - INTERBUTTON - m_ignoreButton->width();
    const int ignoreY = deleteY
        + (deleteHeight - m_ignoreButton->height()) / 2;
    m_ignoreButton->move(ignoreX, ignoreY);

    m_tabCtrl->setGeometry(m_marginLeft, m_marginTop,
                           qMax(0, width() - m_marginLeft - m_marginRight),
                           qMax(0, height() - m_tabBottomFromBottom));
    updateRichGeometry();
    m_sayWnd->setGeometry(m_richRect.left(),
                          height() - m_sayTopFromBottom,
                          m_richRect.width(), m_sayHeight);
}

void CWhisperBox::updateRichGeometry()
{
    if (!m_tabCtrl) return;
    m_richRect = m_tabCtrl->contentRectInParent();
    if (m_currentIndex >= 0 && m_currentIndex < m_leaves.size()
        && m_leaves[m_currentIndex]->m_richView) {
        m_leaves[m_currentIndex]->m_richView->setGeometry(m_richRect);
    }
}

CWhisperBox* GetWhisperBox()
{
    return g_whisperBox;
}

CWhisperBox* CreateWhisperBox()
{
    if (g_whisperBox) return g_whisperBox;
    auto* box = new CWhisperBox(nullptr);
    g_whisperBox = box;
    const QString iconPath = originalFileResourcePath(
        QStringLiteral("IDI_WHISPER"), QStringLiteral("ICON"));
    if (!iconPath.isEmpty()) box->setWindowIcon(QIcon(iconPath));
    box->m_bPostCreate = TRUE;
    if (theApp.m_rectWhisper.isValid() && !theApp.m_rectWhisper.isEmpty())
        box->setGeometry(visibleRect(theApp.m_rectWhisper));
    box->showNormal();
    return box;
}

void WhisperBox(CUserInfo* pui, BOOL giveFocus, BOOL restore)
{
    if (!pui) return;
    CWhisperBox* box = GetWhisperBox();
    if (!box) box = CreateWhisperBox();
    else if (restore) box->showNormal();
    int tab = box->GetTab(pui->GetName());
    if (tab >= 0) box->SwitchToTab(tab);
    else {
        tab = box->AddTab(pui);
        if (tab < 0) return;
        box->SwitchToTab(tab);
    }
    if (giveFocus) box->m_sayWnd->GetSayEdit()->setFocus();
}

void DestroyWhisperBox()
{
    delete g_whisperBox;
    g_whisperBox = nullptr;
}

void InitializeWhisperCores(BOOL restoreOld)
{
    CWhisperBox* box = GetWhisperBox();
    if (!box) return;
    for (CWhisperLeaf* leaf : box->m_leaves)
        InitializeTextCore(leaf->m_richCore, restoreOld);
}

BOOL bAddToWhisperBox(CUserInfo* pui, USHORT modes,
                      const QString& message)
{
    if (!pui || !(modes & BM_WHISPER)) return FALSE;
    int tab = -1;
    CWhisperBox* box = GetWhisperBox();
    if (box) tab = box->GetTab(pui->GetName());
    if (!pui->IsExternal() && tab < 0) return FALSE;

    enumActions actionIDs[4] = {
        static_cast<enumActions>(3),
        aDoNotDisplay,
        aHighlightMessage,
        aReplaceMessage
    };
    CChatDoc* sourceDocument = nullptr;
    CUserInfo* roomUser = PuiFromDocNickIdent(
        &sourceDocument, pui->GetName(), pui->GetFullName(), FALSE, FALSE);
    QString channel;
    if (sourceDocument && sourceDocument->m_proto) {
        channel = sourceDocument->m_proto->m_strChannel;
    }
    QString senderIdentity = pui->GetName();
    if (!pui->GetFullName().isEmpty()) {
        senderIdentity += QLatin1Char('!') + pui->GetFullName();
    }
    const QString ruleRecipients = GetAddressees(
        pui, QStringLiteral("; "), false);
    QString server = QString::fromUtf8(GetMyServer());
    QString originalMessage = message;
    const enumEvents event = roomUser ? eOnWhisperInRoom : eOnWhisper;
    theApp.m_dynaRules.SetCachRecipients(ruleRecipients);
    theApp.m_dynaRules.bMatchAndApplyRules(
        event, actionIDs, nullptr, server, senderIdentity,
        channel, originalMessage);

    if (!(theApp.m_dynaRules.GetFlags() & g_wDoNotDisplay)) {
        if (tab < 0) {
            if (!box) box = CreateWhisperBox();
            if (!box) return TRUE;
            tab = box->AddTab(pui);
            if (tab < 0) return TRUE;
            if (box->m_currentIndex == -1) box->SwitchToTab(tab);
        }

        CWhisperLeaf* leaf = box->m_leaves[tab];
        QByteArray controlFull = message.toUtf8();
        controlFull.append('\0');
        auto* formatting = new CDWordArray;
        char* controlLess = SzControlLess(controlFull.data(), formatting);
        if (!controlLess) {
            FreeAndNullFormatting(&formatting);
            return TRUE;
        }

        box->SetModified(tab, TRUE);
        const QString recipients = GetAddressees(
            pui, QStringLiteral(", "), false);
        const QByteArray sender = utf8(pui->GetScreenName());
        const QByteArray receiver = utf8(recipients);
        const int textLength = static_cast<int>(std::strlen(controlLess));
        char highlightType = -1;
        if (theApp.m_dynaRules.GetFlags() & g_wHighlight) {
            highlightType = static_cast<char>(
                theApp.m_dynaRules.GetFlags() >> 8);
        }
        leaf->m_id = pui->GetFullName();
        leaf->m_richCore->iDisplayMsgHeader(
            textLength, sender.constData(), 0, receiver.constData(), 0,
            mtWhisper, msParticipant, nullptr, 2 * highlightType);
        if (modes & BM_ACTION) {
            leaf->m_richCore->iDisplayAction(
                nullptr, 0, controlLess, 0, msParticipant, TRUE,
                DEFAULT_INDENT, nullptr, 2 * highlightType + 1,
                formatting->GetData(), formatting->GetSize());
        } else {
            leaf->m_richCore->iDisplayMsgText(
                controlLess, 0, mtWhisper, msParticipant, TRUE, FALSE, FALSE,
                DEFAULT_INDENT, nullptr, 2 * highlightType + 1,
                formatting->GetData(), formatting->GetSize());
        }
        FreeAndNullFormatting(&formatting);
    }

    theApp.m_dynaRules.bMatchAndApplyRules(
        event, nullptr, actionIDs, server, senderIdentity,
        channel, originalMessage);
    theApp.m_dynaRules.SetCachRecipients(QString());
    return TRUE;
}

BOOL bWhisperInBox(const QString& filename, const QString& message,
                   CDWordArray* formatting, USHORT modes)
{
    CWhisperBox* box = GetWhisperBox();
    if (!box || box->m_currentIndex < 0
        || box->m_currentIndex >= box->m_leaves.size()) {
        return FALSE;
    }
    if (modes & BM_SOUND) {
        Q_UNUSED(filename);
        // bChatSendSound/StrGetSoundAction remain in the unported sound modules.
        return FALSE;
    }

    CWhisperLeaf* leaf = box->m_leaves[box->m_currentIndex];
    CChatDoc* document = nullptr;
    CUserInfo* pui = PuiFromDocNickIdent(
        &document, leaf->m_nick, leaf->m_id, false, false);
    if (!pui || pui->IsDeparted())
        pui = ExternalPui(leaf->m_nick, leaf->m_id, true);
    if (!pui) return FALSE;

    g_rgpuiWhisperees.clear();
    g_rgpuiWhisperees.append(pui);
    const BOOL result = bChatSendText(
        message, modes, false, formatting, nullptr, true, true);
    if (!message.isEmpty() && message.front() == QLatin1Char('/'))
        return result;

    if (box->isMinimized() || !box->isActiveWindow())
        box->SetModified(box->m_currentIndex, TRUE);
    if (leaf->m_nick.compare(QString::fromUtf8(GetMyNickName()),
                             Qt::CaseInsensitive) == 0) {
        return result;
    }

    const QString senderName = g_puiSelf
        ? g_puiSelf->GetScreenName() : QString::fromUtf8(GetMyName());
    const QByteArray sender = utf8(senderName);
    const QByteArray receiver = utf8(leaf->m_label);
    const QByteArray text = utf8(message);
    leaf->m_richCore->iDisplayMsgHeader(
        text.size(), sender.constData(), 0, receiver.constData(), 0,
        mtWhisper, msParticipant, nullptr, -1);
    if (modes & BM_ACTION) {
        if (formatting) {
            PushFormattingOffsets(formatting,
                static_cast<SHORT>(std::strlen(GetMyName()) + 1));
        }
        const QString action = QString::fromUtf8(GetMyName())
            + QLatin1Char(' ') + message;
        const QByteArray actionBytes = utf8(action);
        leaf->m_richCore->iDisplayAction(
            nullptr, 0, actionBytes.constData(), 0, msParticipant, TRUE,
            DEFAULT_INDENT, nullptr, -1,
            formatting ? formatting->GetData() : nullptr,
            formatting ? formatting->GetSize() : 0);
    } else {
        leaf->m_richCore->iDisplayMsgText(
            text.constData(), 0, mtWhisper, msParticipant, TRUE, FALSE,
            FALSE, DEFAULT_INDENT, nullptr, -1,
            formatting ? formatting->GetData() : nullptr,
            formatting ? formatting->GetSize() : 0);
    }
    return result;
}
