//=--------------------------------------------------------------------------=
// NotiPage.cpp -- Qt port of v2.5-beta-1-modern/notipage.cpp
//=--------------------------------------------------------------------------=

#include "notipage.h"

#include "actions.h"
#include "chat.h"
#include "chatsrv.h"
#include "chatdoc.h"
#include "defines.h"
#include "originalassets.h"
#include "protsupp.h"
#include "resource.h"
#include "setupdlg.h"
#include "userinfo.h"
#include "userlist.h"
#include "whisprbx.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBitmap>
#include <QCloseEvent>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTime>
#include <QVariant>

#include <algorithm>

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

QFont resourceFont(const OriginalDialogResource& dialog)
{
    QFont font(dialog.fontFamily);
    if (dialog.fontPointSize > 0) font.setPointSize(dialog.fontPointSize);
    return font;
}

const OriginalDialogControl* findControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

void placeControl(QWidget* widget, const OriginalDialogResource& dialog,
                  const DialogUnitMapper& mapper, const QString& identifier,
                  int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = findControl(
            dialog, identifier, occurrence)) {
        widget->setGeometry(mapper.rect(*control));
        widget->setVisible(control->visible);
    }
}

QString controlText(const OriginalDialogResource& dialog,
                    const QString& identifier, int occurrence = 0)
{
    const OriginalDialogControl* control = findControl(
        dialog, identifier, occurrence);
    return control ? control->text : QString();
}

QIcon bitmapIcon(const QString& resourceIdentifier, int imageIndex = 0,
                 int imageWidth = 16, int imageHeight = 16)
{
    const QString path = originalFileResourcePath(
        resourceIdentifier, QStringLiteral("BITMAP"));
    QPixmap strip(path);
    if (strip.isNull()) return {};
    const int height = qMin(imageHeight, strip.height());
    QPixmap image = strip.copy(imageIndex * imageWidth, 0,
                               qMin(imageWidth, strip.width()
                                                - imageIndex * imageWidth),
                               height);
    if (image.isNull()) return {};
    image.setMask(image.createMaskFromColor(QColor(0, 0, 255),
                                             Qt::MaskInColor));
    return QIcon(image);
}

CCNotif* notifFromItem(const QTreeWidgetItem* item)
{
    return item ? reinterpret_cast<CCNotif*>(
                      item->data(0, Qt::UserRole).value<quintptr>())
                : nullptr;
}

CUser* userFromItem(const QTreeWidgetItem* item)
{
    return item ? reinterpret_cast<CUser*>(
                      item->data(0, Qt::UserRole).value<quintptr>())
                : nullptr;
}

QString trimQuotes(QString value)
{
    if (value.size() >= 2 && value.front() == QLatin1Char('"')
        && value.back() == QLatin1Char('"')) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

int compareNoCase(const QString& left, const QString& right)
{
    const int result = QString::compare(left, right, Qt::CaseInsensitive);
    return result < 0 ? -1 : result > 0 ? 1 : 0;
}

QString formatCount(int count)
{
    QString result = originalResourceString(QStringLiteral("IDS_NUM_USERS"));
    result.replace(QStringLiteral("%d"), QString::number(count));
    return result;
}

QString formatUpdateTime()
{
    QString result = originalResourceString(
        QStringLiteral("IDS_NOTIF_TIMELABEL"));
    const QString time = QLocale().toString(QTime::currentTime(),
                                             QLocale::ShortFormat);
    result.replace(QStringLiteral("%s"), time);
    return result;
}

QIcon notificationUserIcon(const QIcon& stateIcon, const QIcon& statusIcon,
                           bool isNew)
{
    QPixmap rendered(32, 16);
    rendered.fill(Qt::transparent);
    QPainter painter(&rendered);
    if (isNew) painter.drawPixmap(0, 0, stateIcon.pixmap(16, 16));
    painter.drawPixmap(16, 0, statusIcon.pixmap(16, 16));
    return QIcon(rendered);
}
}

/////////////////////////////////////////////////////////////////////////////
// CNotifsListCtrl

CNotifsListCtrl::CNotifsListCtrl(CNotificationsPage* parent)
    : QTreeWidget(parent)
    , m_notificationsPage(parent)
{
    setColumnCount(g_uNotifParamNum);
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSortingEnabled(false);
    setIconSize(QSize(16, 16));
    header()->setSectionsClickable(true);

    m_ilActiveStatus.append(bitmapIcon(QStringLiteral("IDB_INACTIVE"), 0,
                                       16, 15));
    m_ilActiveStatus.append(bitmapIcon(QStringLiteral("IDB_ACTIVE"), 0,
                                       16, 15));

    QObject::connect(header(), &QHeaderView::sectionClicked,
                     this, [this](int column) { OnColumnClick(column); });
}

CCNotif* CNotifsListCtrl::NotifAt(INT index) const
{
    return index >= 0 && index < topLevelItemCount()
        ? notifFromItem(topLevelItem(index)) : nullptr;
}

INT CNotifsListCtrl::iGetSelectedNotif(CCNotif** notif) const
{
    if (notif) *notif = nullptr;
    const QList<QTreeWidgetItem*> selected = selectedItems();
    if (selected.isEmpty()) return -1;
    QTreeWidgetItem* item = selected.first();
    if (notif) *notif = notifFromItem(item);
    return indexOfTopLevelItem(item);
}

INT CNotifsListCtrl::iGetSortPosition(CCNotif* notif) const
{
    if (!notif || m_uSortColumn >= g_uNotifParamNum)
        return topLevelItemCount();
    INT itemIndex = 0;
    for (; itemIndex < topLevelItemCount(); ++itemIndex) {
        CCNotif* other = NotifAt(itemIndex);
        if (!other) continue;
        const int order = notif->GetParam(m_uSortColumn).compare(
            other->GetParam(m_uSortColumn), Qt::CaseInsensitive);
        if ((order <= 0 && m_bSortAscending)
            || (order >= 0 && !m_bSortAscending)) {
            break;
        }
    }
    return itemIndex;
}

void CNotifsListCtrl::UpdateItem(QTreeWidgetItem* item, CCNotif* notif)
{
    if (!item || !notif) return;
    for (UCHAR parameter = g_uNickname; parameter <= g_uNetName;
         ++parameter) {
        const bool any = parameter < g_uNetName
            && notif->GetOperator(parameter) == g_uAny;
        item->setText(parameter,
                      any ? QString::fromLatin1(g_szAnyReplacement)
                          : notif->GetParam(parameter));
    }
    const int icon = notif->bActive() ? 1 : 0;
    if (icon < m_ilActiveStatus.size()) item->setIcon(0, m_ilActiveStatus.at(icon));
}

BOOL CNotifsListCtrl::bAddNotif(CCNotif* notif, INT index)
{
    if (!notif) return FALSE;
    auto* item = new QTreeWidgetItem;
    item->setData(0, Qt::UserRole,
                  QVariant::fromValue(reinterpret_cast<quintptr>(notif)));
    UpdateItem(item, notif);
    if (index < 0 || index > topLevelItemCount())
        index = topLevelItemCount();
    insertTopLevelItem(index, item);
    return TRUE;
}

BOOL CNotifsListCtrl::bFill(CCDynaNotifs* dynaNotifs)
{
    if (!dynaNotifs) return FALSE;
    CCNotif* selected = nullptr;
    iGetSelectedNotif(&selected);
    clear();
    const QVector<CCNotif*>& notifs = dynaNotifs->GetNotifsArray();
    for (INT index = 0; index < notifs.size(); ++index) {
        if (!bAddNotif(notifs.at(index), index)) return FALSE;
    }
    if (selected) {
        for (INT index = 0; index < topLevelItemCount(); ++index) {
            if (NotifAt(index) == selected) {
                setCurrentItem(topLevelItem(index));
                topLevelItem(index)->setSelected(true);
                break;
            }
        }
    }
    return TRUE;
}

void CNotifsListCtrl::SwitchActivation(INT index)
{
    CCNotif* notif = NotifAt(index);
    if (!notif) return;
    if (notif->bActive()) notif->Desactivate();
    else notif->Activate();
    UpdateItem(topLevelItem(index), notif);
    if (m_notificationsPage) m_notificationsPage->SetModified(TRUE);
}

void CNotifsListCtrl::OnColumnClick(INT column)
{
    if (column < g_uNickname || column > g_uNetName) return;
    if (m_uSortColumn == column) m_bSortAscending = !m_bSortAscending;
    else {
        m_uSortColumn = static_cast<UCHAR>(column);
        m_bSortAscending = TRUE;
    }
    if (m_notificationsPage)
        m_notificationsPage->SortNotifs(m_uSortColumn, m_bSortAscending);
}

void CNotifsListCtrl::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space) {
        CCNotif* notif = nullptr;
        const INT index = iGetSelectedNotif(&notif);
        if (index >= 0) SwitchActivation(index);
    }
    QTreeWidget::keyPressEvent(event);
}

void CNotifsListCtrl::mousePressEvent(QMouseEvent* event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    if (item && event->button() == Qt::LeftButton) {
        const QRect itemRect = visualItemRect(item);
        if (event->position().x() >= itemRect.left()
            && event->position().x() <= itemRect.left() + iconSize().width() + 4) {
            SwitchActivation(indexOfTopLevelItem(item));
        }
    }
    QTreeWidget::mousePressEvent(event);
}

/////////////////////////////////////////////////////////////////////////////
// CNotificationsPage

CNotificationsPage::CNotificationsPage(QWidget* parent)
    : QWidget(parent)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_NOTIFICATIONS"));
    setObjectName(QStringLiteral("IDD_NOTIFICATIONS"));
    setFont(resourceFont(dialog));
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != QLatin1String("IDC_STATIC")) continue;
        QWidget* widget = nullptr;
        if (control.type == QLatin1String("GROUPBOX")) {
            widget = new QGroupBox(control.text, this);
        } else if (control.type == QLatin1String("LTEXT")) {
            auto* label = new QLabel(control.text, this);
            if (control.style.contains(QStringLiteral("SS_CENTERIMAGE")))
                label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            widget = label;
        }
        if (widget) widget->setGeometry(mapper.rect(control));
    }

    m_lstNotifs = new CNotifsListCtrl(this);
    placeControl(m_lstNotifs, dialog, mapper, QStringLiteral("IDC_LSTNOTIFS"));
    m_lstNotifs->setHeaderLabels({
        originalResourceString(QStringLiteral("IDS_NICKARG_LABEL")),
        originalResourceString(QStringLiteral("IDS_USERARG_LABEL")),
        originalResourceString(QStringLiteral("IDS_HOSTARG_LABEL")),
        originalResourceString(QStringLiteral("IDS_NETARG_LABEL"))});
    m_lstNotifs->setColumnWidth(0, originalResourceString(
        QStringLiteral("IDS_NICKARG_WIDTH")).toInt());
    m_lstNotifs->setColumnWidth(1, originalResourceString(
        QStringLiteral("IDS_USERARG_WIDTH")).toInt());
    m_lstNotifs->setColumnWidth(2, originalResourceString(
        QStringLiteral("IDS_HOSTARG_WIDTH")).toInt());
    m_lstNotifs->setColumnWidth(3, originalResourceString(
        QStringLiteral("IDS_NETARG_WIDTH")).toInt());

    const QString comboIds[] = {QStringLiteral("IDC_CMBNICKOP"),
                                QStringLiteral("IDC_CMBUSEROP"),
                                QStringLiteral("IDC_CMBHOSTOP")};
    const QString editIds[] = {QStringLiteral("IDC_NICKARG"),
                               QStringLiteral("IDC_USERARG"),
                               QStringLiteral("IDC_HOSTARG")};
    for (INT parameter = g_uNickname; parameter <= g_uHostName;
         ++parameter) {
        m_cmbOperators[parameter] = new QComboBox(this);
        placeControl(m_cmbOperators[parameter], dialog, mapper,
                     comboIds[parameter]);
        m_editArgs[parameter] = new QLineEdit(this);
        placeControl(m_editArgs[parameter], dialog, mapper,
                     editIds[parameter]);
        m_editArgs[parameter]->setMaxLength(g_uMaxNotifParamLength);
        m_editArgs[parameter]->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[^!@]{0,32}")),
            m_editArgs[parameter]));
    }

    m_cmbNetArg = new CChatServiceComboBox(this);
    m_cmbNetArg->setEditable(true);
    placeControl(m_cmbNetArg, dialog, mapper, QStringLiteral("IDC_CMBNETARG"));
    if (m_cmbNetArg->lineEdit())
        m_cmbNetArg->lineEdit()->setMaxLength(g_uMaxNetArgLength);

    m_addNotif = new QPushButton(controlText(
        dialog, QStringLiteral("IDC_ADDNOTIF")), this);
    placeControl(m_addNotif, dialog, mapper, QStringLiteral("IDC_ADDNOTIF"));
    m_modifyNotif = new QPushButton(controlText(
        dialog, QStringLiteral("IDC_MODIFYNOTIF")), this);
    placeControl(m_modifyNotif, dialog, mapper,
                 QStringLiteral("IDC_MODIFYNOTIF"));
    m_deleteNotif = new QPushButton(controlText(
        dialog, QStringLiteral("IDC_DELETENOTIF")), this);
    placeControl(m_deleteNotif, dialog, mapper,
                 QStringLiteral("IDC_DELETENOTIF"));

    FillUpCombos();
    m_bNotifsColumnSet = TRUE;

    for (INT parameter = g_uNickname; parameter <= g_uHostName;
         ++parameter) {
        QObject::connect(m_cmbOperators[parameter],
                         &QComboBox::currentIndexChanged,
                         this, [this, parameter](int) {
                             OnComboChange(parameter);
                         });
        QObject::connect(m_editArgs[parameter], &QLineEdit::textChanged,
                         this, [this] { UpdateButtonsStatus(); });
    }
    QObject::connect(m_cmbNetArg, &QComboBox::currentTextChanged,
                     this, [this] { UpdateButtonsStatus(); });
    QObject::connect(m_lstNotifs, &QTreeWidget::itemSelectionChanged,
                     this, [this] { OnNotifItemChanged(); });
    QObject::connect(m_addNotif, &QPushButton::clicked,
                     this, [this] { OnAddNotifClick(); });
    QObject::connect(m_modifyNotif, &QPushButton::clicked,
                     this, [this] { OnModifyNotifClick(); });
    QObject::connect(m_deleteNotif, &QPushButton::clicked,
                     this, [this] { OnDeleteNotifClick(); });

    UpdateButtonsStatus();
}

void CNotificationsPage::SetDynaNotifs(CCDynaNotifs* dynaCopy)
{
    m_pDynaCopy = dynaCopy;
    if (m_pDynaCopy) OnSetActive();
}

void CNotificationsPage::FillUpCombos()
{
    for (INT parameter = g_uNickname; parameter <= g_uHostName;
         ++parameter) {
        for (UCHAR op = g_uAny; op <= g_uEndsWith; ++op) {
            m_cmbOperators[parameter]->addItem(
                originalResourceString(IDS_OP_ANY + op));
        }
        m_cmbOperators[parameter]->setCurrentIndex(g_uAny);
        m_editArgs[parameter]->setEnabled(false);
    }
    const QString any = originalResourceString(
        IDS_KEY_EVENT_PARAM0 + static_cast<UINT>(kepAny));
    m_cmbNetArg->addItem(any);
    m_cmbNetArg->SetServiceList(&theApp.m_listChatServices);
    m_cmbNetArg->Fill();
    m_cmbNetArg->setCurrentIndex(0);
}

void CNotificationsPage::FillUpParamsFromIdent(QString& ident)
{
    const qsizetype separator = ident.indexOf(QLatin1Char('@'));
    QString userName;
    QString hostName;
    if (separator >= 0) {
        userName = ident.left(separator);
        hostName = ident.mid(separator + 1);
    }
    m_cmbOperators[g_uUserName]->setCurrentIndex(g_uEquals);
    m_cmbOperators[g_uHostName]->setCurrentIndex(g_uEquals);
    m_editArgs[g_uUserName]->setEnabled(true);
    m_editArgs[g_uHostName]->setEnabled(true);
    m_editArgs[g_uUserName]->setText(userName);
    m_editArgs[g_uHostName]->setText(hostName);
}

BOOL CNotificationsPage::OnSetActive()
{
    if (!m_pDynaCopy) return FALSE;
    if (!m_pDynaCopy->GetStartUpIdent().isEmpty()) {
        QString ident = m_pDynaCopy->GetStartUpIdent();
        FillUpParamsFromIdent(ident);
        m_pDynaCopy->SetStartUpIdent(QString());
    }
    const UCHAR sortColumn = static_cast<UCHAR>(m_pDynaCopy->GetFlags() >> 12);
    m_lstNotifs->SetSortSettings(
        sortColumn < g_uNotifParamNum ? sortColumn : g_uNickname,
        !(m_pDynaCopy->GetFlags() & g_wSortDescending));
    m_lstNotifs->bFill(m_pDynaCopy);
    UpdateButtonsStatus();
    m_bModified = FALSE;
    return TRUE;
}

void CNotificationsPage::OnOK()
{
    if (!m_pDynaCopy) return;
    theApp.m_dynaNotifs = *m_pDynaCopy;
    if (theApp.m_dynaNotifs.bDaemonNeeded()) {
        theApp.m_dynaNotifs.bStartNotifsDaemon(
            g_uNotifsDaemonShortElapse, TRUE);
    } else {
        theApp.m_dynaNotifs.bStopNotifsDaemon();
    }
    theApp.m_dynaNotifs.bUpdateNotifsDaemonExt(FALSE);
    m_bModified = FALSE;
}

void CNotificationsPage::UpdateButtonsStatus()
{
    if (m_bFreezeButtons) return;
    BOOL invalidParam = FALSE;
    BOOL canAdd = FALSE;
    const BOOL notifSelected = !m_lstNotifs->selectedItems().isEmpty();
    for (INT parameter = g_uNickname; parameter <= g_uHostName;
         ++parameter) {
        const INT selected = m_cmbOperators[parameter]->currentIndex();
        const INT length = m_editArgs[parameter]->text().size();
        if (selected != g_uAny && selected >= 0 && length > 0)
            canAdd = TRUE;
        if (selected != g_uAny && selected >= 0 && length == 0)
            invalidParam = TRUE;
        if (selected < 0 && length > 0) invalidParam = TRUE;
    }
    const QString netArg = m_cmbNetArg->currentText();
    canAdd &= !invalidParam
        && (!netArg.isEmpty() || m_cmbNetArg->currentIndex() >= 0);
    m_addNotif->setEnabled(canAdd);
    m_modifyNotif->setEnabled(notifSelected && canAdd);
    m_deleteNotif->setEnabled(notifSelected);
}

void CNotificationsPage::OnComboChange(INT parameter)
{
    if (parameter < g_uNickname || parameter > g_uHostName) return;
    const INT selected = m_cmbOperators[parameter]->currentIndex();
    if (selected < 0) return;
    if (selected == g_uAny) m_editArgs[parameter]->clear();
    m_editArgs[parameter]->setEnabled(selected != g_uAny);
    UpdateButtonsStatus();
}

void CNotificationsPage::OnNotifItemChanged()
{
    CCNotif* notif = nullptr;
    if (m_lstNotifs->iGetSelectedNotif(&notif) >= 0 && notif) {
        for (INT parameter = g_uNickname; parameter <= g_uHostName;
             ++parameter) {
            const UCHAR op = notif->GetOperator(parameter);
            m_cmbOperators[parameter]->setCurrentIndex(op);
            m_editArgs[parameter]->setEnabled(op != g_uAny);
            m_editArgs[parameter]->setText(notif->GetParam(parameter));
        }
        const QString network = notif->GetParam(g_uNetName);
        const INT existing = m_cmbNetArg->findText(
            network, Qt::MatchExactly | Qt::MatchCaseSensitive);
        if (existing >= 0) m_cmbNetArg->setCurrentIndex(existing);
        else {
            m_cmbNetArg->setCurrentIndex(-1);
            m_cmbNetArg->setEditText(network);
        }
    }
    UpdateButtonsStatus();
}

void CNotificationsPage::SortNotifs(UCHAR sortColumn, BOOL sortAscending)
{
    if (!m_pDynaCopy || sortColumn >= g_uNotifParamNum) return;
    WORD flags = m_pDynaCopy->GetFlags();
    flags &= 0x0fff;
    flags |= static_cast<WORD>(sortColumn << 12);
    if (sortAscending) flags &= ~g_wSortDescending;
    else flags |= g_wSortDescending;
    m_pDynaCopy->SetFlags(flags);
    if (m_pDynaCopy->bSortNotifs()) m_lstNotifs->bFill(m_pDynaCopy);
}

void CNotificationsPage::OnAddNotifClick()
{
    if (!m_pDynaCopy || !m_addNotif->isEnabled()) return;
    auto* notif = new CCNotif;
    for (INT parameter = g_uNickname; parameter <= g_uHostName;
         ++parameter) {
        INT op = m_cmbOperators[parameter]->currentIndex();
        if (op < 0) op = g_uAny;
        notif->SetOperator(parameter, static_cast<UCHAR>(op));
        notif->SetParam(parameter, m_editArgs[parameter]->text());
    }
    notif->SetParam(g_uNetName, m_cmbNetArg->currentText());

    if (m_pDynaCopy->bNotifExists(notif)) {
        QMessageBox::information(
            this,
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
            originalResourceString(
                QStringLiteral("IDS_ERR_NOTIFALREADYEXISTS")));
        notif->Release();
        return;
    }

    const INT index = m_lstNotifs->iGetSortPosition(notif);
    if (!m_lstNotifs->bAddNotif(notif, index)
        || !m_pDynaCopy->bAddNotif(notif, index)) {
        if (index >= 0 && index < m_lstNotifs->topLevelItemCount())
            delete m_lstNotifs->takeTopLevelItem(index);
        notif->Release();
        return;
    }
    if (m_bActivateNew) notif->Activate();
    m_lstNotifs->UpdateItem(m_lstNotifs->topLevelItem(index), notif);
    QTreeWidgetItem* item = m_lstNotifs->topLevelItem(index);
    m_lstNotifs->setCurrentItem(item);
    item->setSelected(true);
    m_lstNotifs->scrollToItem(item);
    SetModified(TRUE);
}

void CNotificationsPage::OnModifyNotifClick()
{
    if (!m_pDynaCopy || !m_modifyNotif->isEnabled()) return;
    CCNotif* notif = nullptr;
    const INT index = m_lstNotifs->iGetSelectedNotif(&notif);
    if (index < 0 || !notif) return;
    m_bActivateNew = notif->bActive();
    m_bFreezeButtons = TRUE;
    delete m_lstNotifs->takeTopLevelItem(index);
    m_pDynaCopy->bRemoveNotif(nullptr, index);
    m_bFreezeButtons = FALSE;
    OnAddNotifClick();
    m_bActivateNew = TRUE;
}

void CNotificationsPage::OnDeleteNotifClick()
{
    if (!m_pDynaCopy || !m_deleteNotif->isEnabled()) return;
    CCNotif* notif = nullptr;
    const INT index = m_lstNotifs->iGetSelectedNotif(&notif);
    if (index < 0) return;
    delete m_lstNotifs->takeTopLevelItem(index);
    m_pDynaCopy->bRemoveNotif(nullptr, index);
    if (m_lstNotifs->topLevelItemCount() > 0
        && index < m_lstNotifs->topLevelItemCount()) {
        m_lstNotifs->setCurrentItem(m_lstNotifs->topLevelItem(index));
        m_lstNotifs->topLevelItem(index)->setSelected(true);
    } else if (m_lstNotifs->topLevelItemCount() == 0) {
        m_addNotif->setFocus();
    }
    SetModified(TRUE);
    UpdateButtonsStatus();
}

/////////////////////////////////////////////////////////////////////////////
// CNotificationUsers

CNotificationUsers::CNotificationUsers(QWidget* parent)
    : QDialog(parent)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_NOTIFICATIONUSERS"));
    setObjectName(QStringLiteral("IDD_NOTIFICATIONUSERS"));
    setWindowTitle(dialog.caption);
    setFont(resourceFont(dialog));
    setWindowFlags(Qt::Window | Qt::WindowTitleHint
                   | Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint
                   | Qt::WindowCloseButtonHint);
    const QString iconPath = originalFileResourcePath(
        QStringLiteral("IDI_NOTIF"), QStringLiteral("ICON"));
    if (!iconPath.isEmpty()) setWindowIcon(QIcon(iconPath));

    const DialogUnitMapper mapper(font());
    m_sizeMinimal = QSize(mapper.x(dialog.width), mapper.y(dialog.height));
    resize(m_sizeMinimal);
    setMinimumSize(m_sizeMinimal);
    m_sizeDialog = m_sizeMinimal;

    auto* topLabel = new QLabel(controlText(
        dialog, QStringLiteral("IDC_NOTIFTOPLABEL")), this);
    placeControl(topLabel, dialog, mapper,
                 QStringLiteral("IDC_NOTIFTOPLABEL"));

    m_lstUsers = new QTreeWidget(this);
    placeControl(m_lstUsers, dialog, mapper,
                 QStringLiteral("IDC_LSTNOTIFICATIONUSERS"));
    m_lstUsers->setColumnCount(4);
    m_lstUsers->setRootIsDecorated(false);
    m_lstUsers->setItemsExpandable(false);
    m_lstUsers->setUniformRowHeights(true);
    m_lstUsers->setAllColumnsShowFocus(true);
    m_lstUsers->setSelectionMode(QAbstractItemView::SingleSelection);
    m_lstUsers->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_lstUsers->setSortingEnabled(false);
    // Win32 ListView renders a state image and a small image side by side.
    // The two unchanged source bitmaps are composed only for painting.
    m_lstUsers->setIconSize(QSize(32, 16));
    m_lstUsers->header()->setSectionsClickable(true);
    m_lstUsers->setHeaderLabels({
        originalResourceString(QStringLiteral("ID_UL_NICK_LABEL")),
        originalResourceString(QStringLiteral("ID_UL_IDENT_LABEL")),
        originalResourceString(QStringLiteral("ID_UL_REALNAME_LABEL")),
        originalResourceString(QStringLiteral("ID_UL_ROOM_LABEL"))});
    m_lstUsers->setColumnWidth(0, originalResourceString(
        QStringLiteral("IDS_NOTIF_NICKWIDTH")).toInt());
    m_lstUsers->setColumnWidth(1, originalResourceString(
        QStringLiteral("IDS_NOTIF_IDENTWIDTH")).toInt());
    m_lstUsers->setColumnWidth(2, originalResourceString(
        QStringLiteral("IDS_NOTIF_REALWIDTH")).toInt());
    m_lstUsers->setColumnWidth(3, originalResourceString(
        QStringLiteral("IDS_NOTIF_ROOMWIDTH")).toInt());

    auto createButton = [&](const QString& identifier) {
        auto* button = new QPushButton(controlText(dialog, identifier), this);
        placeControl(button, dialog, mapper, identifier);
        return button;
    };
    m_defineNotif = createButton(QStringLiteral("IDC_DEFINENOTIF"));
    m_notifWhisper = createButton(QStringLiteral("IDC_NOTIFWHISPER"));
    m_notifInvite = createButton(QStringLiteral("IDC_NOTIFINVITE"));
    m_notifJoin = createButton(QStringLiteral("IDC_NOTIFJOIN"));
    m_notifUpdate = createButton(QStringLiteral("IDC_NOTIFUPDATE"));
    m_notifUpdate->setDefault(true);
    m_notifClear = createButton(QStringLiteral("IDC_NOTIFCLEAR"));
    m_closeNotifUsers = createButton(
        QStringLiteral("IDC_CLOSE_NOTIFUSERS"));

    m_notifCount = new QLabel(this);
    placeControl(m_notifCount, dialog, mapper, QStringLiteral("IDC_NOTIFCOUNT"));
    m_notifTime = new QLabel(this);
    m_notifTime->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    placeControl(m_notifTime, dialog, mapper, QStringLiteral("IDC_NOTIFTIME"));

    m_ImageList.append(bitmapIcon(QStringLiteral("IDB_CONNECT"), 0));
    m_ImageList.append(bitmapIcon(QStringLiteral("IDB_CONNECT"), 1));
    m_StateIcon = bitmapIcon(QStringLiteral("IDB_OLDNEW"), 0);

    for (QWidget* child : findChildren<QWidget*>(
             QString(), Qt::FindDirectChildrenOnly)) {
        m_originalControlGeometry.insert(child, child->geometry());
    }

    QObject::connect(m_lstUsers, &QTreeWidget::itemSelectionChanged,
                     this, [this] { UpdateButtons(); });
    QObject::connect(m_lstUsers->header(), &QHeaderView::sectionClicked,
                     this, [this](int column) { OnColumnClick(column); });
    QObject::connect(m_defineNotif, &QPushButton::clicked,
                     this, [this] { OnDefineNotif(); });
    QObject::connect(m_notifClear, &QPushButton::clicked,
                     this, [this] { OnNotifClear(); });
    QObject::connect(m_notifInvite, &QPushButton::clicked,
                     this, [this] { OnNotifInvite(); });
    QObject::connect(m_notifWhisper, &QPushButton::clicked,
                     this, [this] { OnNotifWhisper(); });
    QObject::connect(m_notifJoin, &QPushButton::clicked,
                     this, [this] { OnNotifJoin(); });
    QObject::connect(m_notifUpdate, &QPushButton::clicked,
                     this, [this] { OnNotifUpdate(); });
    QObject::connect(m_closeNotifUsers, &QPushButton::clicked,
                     this, [this] { OnCloseDialog(); });

    m_notifWhisper->setEnabled(false);
    m_notifInvite->setEnabled(false);
    m_notifJoin->setEnabled(false);
    m_notifClear->setEnabled(false);
    // The source routes this button through the four-page Automation sheet.
    // Keep it unavailable until that original sheet exists; no substitute
    // one-page dialog is invented here.
    m_defineNotif->setEnabled(true);
    bUpdateCountLabel();
}

void CNotificationUsers::UpdateItem(QTreeWidgetItem* item, CUser* user)
{
    if (!item || !user) return;
    item->setText(0, user->GetPrettyNick());
    item->setText(1, user->m_strIdentity);
    item->setText(2, user->m_strFullName);
    item->setText(3, user->m_strPrettyRoom);
    const int image = (user->GetFlags() & g_wConnected) ? 0 : 1;
    const bool isNew = (user->GetFlags() & g_wNew) != 0;
    if (image < m_ImageList.size()) {
        item->setIcon(0, notificationUserIcon(
            m_StateIcon, m_ImageList.at(image), isNew));
    }
    item->setData(0, Qt::UserRole + 1, isNew);
}

INT CNotificationUsers::iFindUserIndex(CUser* user,
                                        INT lastItemsCount) const
{
    if (!user || lastItemsCount <= 0) return -1;
    const INT users = m_lstUsers->topLevelItemCount();
    const INT start = users - lastItemsCount;
    if (start < 0) return -1;
    for (INT index = start; index < users; ++index) {
        if (userFromItem(m_lstUsers->topLevelItem(index)) == user)
            return index;
    }
    return -1;
}

BOOL CNotificationUsers::bFillList(CCItemPtrArray* notifUsers,
                                    UINT modifiedUsersCount)
{
    if (!notifUsers) return FALSE;
    const INT users = notifUsers->GetSize();
    if (users < static_cast<INT>(modifiedUsersCount)) return FALSE;
    INT initialItems = m_lstUsers->topLevelItemCount();
    for (INT modified = users - static_cast<INT>(modifiedUsersCount);
         modified < users; ++modified) {
        auto* user = static_cast<CUser*>(notifUsers->GetAt(modified));
        if (!user || !(user->GetFlags() & g_wVisible)
            || !(user->GetFlags() & g_wNew)
            || !(user->GetFlags() & g_wAltered)) {
            return FALSE;
        }
        user->SetFlags(user->GetFlags() & ~g_wAltered);
        if (initialItems > 0) {
            const INT found = iFindUserIndex(user, initialItems);
            if (found >= 0) {
                delete m_lstUsers->takeTopLevelItem(found);
                --initialItems;
            }
        }
        auto* item = new QTreeWidgetItem;
        item->setData(0, Qt::UserRole,
                      QVariant::fromValue(reinterpret_cast<quintptr>(user)));
        UpdateItem(item, user);
        m_lstUsers->insertTopLevelItem(0, item);
    }
    m_notifClear->setEnabled(m_lstUsers->topLevelItemCount() > 0);
    bUpdateCountLabel();
    bUpdateLastUpdateLabel();
    return TRUE;
}

BOOL CNotificationUsers::bUpdateCountLabel()
{
    if (!m_notifCount) return FALSE;
    m_notifCount->setText(formatCount(m_lstUsers->topLevelItemCount()));
    return TRUE;
}

BOOL CNotificationUsers::bUpdateLastUpdateLabel()
{
    if (!m_notifTime) return FALSE;
    m_notifTime->setText(formatUpdateTime());
    return TRUE;
}

BOOL CNotificationUsers::bSignalNewEntries()
{
    if (!isVisible()) showNormal();
    if (isMinimized() || !isActiveWindow()) {
        if (!m_bInverted) {
            QApplication::alert(this);
            m_bInverted = TRUE;
        }
        // The source requests the Windows sound alias "Default sound".
        // No unrelated Qt sound or copied media asset substitutes for it.
    }
    return TRUE;
}

BOOL CNotificationUsers::bSignalNewUpdate()
{
    m_lstUsers->clear();
    return bUpdateLastUpdateLabel();
}

CUser* CNotificationUsers::GetSelectedUser() const
{
    const QList<QTreeWidgetItem*> selected = m_lstUsers->selectedItems();
    return selected.size() == 1 ? userFromItem(selected.first()) : nullptr;
}

QString CNotificationUsers::GetSelectedNickname() const
{
    CUser* user = GetSelectedUser();
    return user ? user->m_strNickname : QString();
}

void CNotificationUsers::UpdateButtons()
{
    CUser* user = GetSelectedUser();
    const QString selectedNick = user ? user->m_strNickname : QString();
    BOOL inCurrentRoom = FALSE;
    BOOL canJoin = FALSE;
    const BOOL isOtherConnected = !selectedNick.isEmpty()
        && selectedNick != QString::fromUtf8(GetMyNickName())
        && (user->GetFlags() & g_wConnected);

    CChatDoc* document = GetChatDoc();
    if (!selectedNick.isEmpty() && document
        && document->GetConnectionStatus() == CX_INCHANNEL) {
        CUserInfo* pui = LookupPui(selectedNick, document);
        inCurrentRoom = pui && !pui->IsDeparted();
    }
    if (isOtherConnected && !user->m_strRoom.isEmpty()
        && CHANNELPREFIX(user->m_strRoom.front().toLatin1())) {
        document = LookupDoc(user->m_strRoom);
        canJoin = !document
            || document->GetConnectionStatus() != CX_INCHANNEL
            || document != GetChatDoc();
    }
    m_notifInvite->setEnabled(
        isOtherConnected && bCanInvite() && !inCurrentRoom);
    m_notifWhisper->setEnabled(isOtherConnected);
    m_notifJoin->setEnabled(canJoin);
}

void CNotificationUsers::OnColumnClick(INT column)
{
    if (column < g_uNickname || column > g_uNetName) return;
    if (m_uSortColumn == column) m_bSortAscending = !m_bSortAscending;
    else {
        m_uSortColumn = static_cast<UCHAR>(column);
        m_bSortAscending = TRUE;
    }
    CUser* selected = GetSelectedUser();
    QList<QTreeWidgetItem*> items;
    while (m_lstUsers->topLevelItemCount() > 0)
        items.append(m_lstUsers->takeTopLevelItem(0));
    const UCHAR sortColumn = m_uSortColumn;
    const BOOL ascending = m_bSortAscending;
    std::stable_sort(items.begin(), items.end(),
                     [sortColumn, ascending](QTreeWidgetItem* leftItem,
                                             QTreeWidgetItem* rightItem) {
        CUser* left = userFromItem(leftItem);
        CUser* right = userFromItem(rightItem);
        if (!left || !right) return left != nullptr;
        int result = 0;
        if (sortColumn == g_uNickname) {
            result = compareNoCase(trimQuotes(left->GetPrettyNick()),
                                   trimQuotes(right->GetPrettyNick()));
        } else if (sortColumn == g_uUserName) {
            result = compareNoCase(left->m_strIdentity,
                                   right->m_strIdentity);
        } else if (sortColumn == g_uHostName) {
            result = compareNoCase(left->m_strFullName,
                                   right->m_strFullName);
        } else if (sortColumn == g_uNetName) {
            result = compareNoCase(left->m_strPrettyRoom,
                                   right->m_strPrettyRoom);
        }
        if (!result && sortColumn != g_uNickname) {
            result = compareNoCase(trimQuotes(left->GetPrettyNick()),
                                   trimQuotes(right->GetPrettyNick()));
        }
        if (!ascending) result = -result;
        return result < 0;
    });
    for (QTreeWidgetItem* item : items)
        m_lstUsers->addTopLevelItem(item);
    if (selected) {
        for (QTreeWidgetItem* item : items) {
            if (userFromItem(item) == selected) {
                m_lstUsers->setCurrentItem(item);
                item->setSelected(true);
                break;
            }
        }
    }
}

void CNotificationUsers::OnNotifInvite()
{
    if (!currentRoom || currentRoom->GetConnectionStatus() != CX_INCHANNEL) {
        QMessageBox::information(
            this,
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
            originalResourceString(QStringLiteral("IDS_OUTCHANNEL_INVITE")));
        return;
    }
    const QString nick = GetSelectedNickname();
    if (!nick.isEmpty()) currentRoom->ChatSendInvitation(nick);
}

void CNotificationUsers::OnNotifWhisper()
{
    CUser* user = GetSelectedUser();
    if (!user || !(user->GetFlags() & g_wConnected)) return;
    CUserInfo pui(user->m_strNickname, user->m_strIdentity);
    WhisperBox(&pui);
}

void CNotificationUsers::OnNotifJoin()
{
    CUser* user = GetSelectedUser();
    if (!user || !(user->GetFlags() & g_wConnected)) return;
    g_bEnterOnCreate = FALSE;
    bSwitchToRoom(user->m_strRoom);
    UpdateButtons();
}

void CNotificationUsers::OnNotifClear()
{
    theApp.m_dynaNotifs.bRemoveFlagsFromAllUsers(g_wVisible);
    theApp.m_dynaNotifs.bRemoveUsersWithoutFlag(g_wConnected);
    m_lstUsers->clear();
    m_notifClear->setEnabled(false);
    bUpdateCountLabel();
    UpdateButtons();
}

void CNotificationUsers::OnDefineNotif()
{
    showMinimized();
    if (theApp.m_iAutoPage == -1) {
        theApp.m_iAutoPage = 1;
        QMetaObject::invokeMethod(qApp, [] {
            theApp.OnViewAutomations();
        }, Qt::QueuedConnection);
    }
}

void CNotificationUsers::OnNotifUpdate()
{
    if (theApp.m_dynaNotifs.bUpdateNotifs()) {
        m_lstUsers->clear();
        UpdateButtons();
        bUpdateCountLabel();
        bUpdateLastUpdateLabel();
    }
}

void CNotificationUsers::OnCloseDialog()
{
    theApp.m_bLoginNotifsShown = FALSE;
    theApp.m_dynaNotifs.bRemoveFlagsFromAllUsers(g_wNew);
    hide();
    RedirectFocus();
}

void CNotificationUsers::RedirectFocus()
{
    if (CChatDoc* document = GetChatDoc()) document->SetFocusToSayWnd();
}

void CNotificationUsers::SaveNotifCoords()
{
    if (m_bPostCreate && !isMinimized())
        theApp.m_rectNotifs = geometry();
}

void CNotificationUsers::ApplyResize(INT widthDelta, INT heightDelta)
{
    for (auto it = m_originalControlGeometry.cbegin();
         it != m_originalControlGeometry.cend(); ++it) {
        QWidget* widget = it.key();
        QRect rect = it.value();
        if (widget == m_lstUsers) {
            rect.setWidth(qMax(1, rect.width() + widthDelta));
            rect.setHeight(qMax(1, rect.height() + heightDelta));
        } else if (widget == m_defineNotif || widget == m_notifCount) {
            rect.translate(0, heightDelta);
        } else if (widget == m_notifWhisper || widget == m_notifInvite
                   || widget == m_notifJoin || widget == m_notifUpdate
                   || widget == m_notifClear
                   || widget == m_closeNotifUsers || widget == m_notifTime) {
            rect.translate(widthDelta, heightDelta);
        }
        widget->setGeometry(rect);
    }
}

void CNotificationUsers::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F5) {
        if (!event->isAutoRepeat()) OnNotifUpdate();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void CNotificationUsers::showEvent(QShowEvent* event)
{
    theApp.m_bLoginNotifsShown = TRUE;
    QDialog::showEvent(event);
    m_lstUsers->setFocus();
    if (!m_lstUsers->currentItem()
        && m_lstUsers->topLevelItemCount() > 0) {
        m_lstUsers->setCurrentItem(m_lstUsers->topLevelItem(0));
    }
}

void CNotificationUsers::hideEvent(QHideEvent* event)
{
    theApp.m_bLoginNotifsShown = FALSE;
    QDialog::hideEvent(event);
}

void CNotificationUsers::closeEvent(QCloseEvent* event)
{
    OnCloseDialog();
    event->ignore();
}

void CNotificationUsers::reject()
{
    OnCloseDialog();
}

void CNotificationUsers::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (!isMinimized()) {
        ApplyResize(event->size().width() - m_sizeMinimal.width(),
                    event->size().height() - m_sizeMinimal.height());
        m_sizeDialog = event->size();
        SaveNotifCoords();
    }
}

void CNotificationUsers::moveEvent(QMoveEvent* event)
{
    QDialog::moveEvent(event);
    SaveNotifCoords();
}

void CNotificationUsers::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && isMinimized()) {
        theApp.m_dynaNotifs.bRemoveFlagsFromAllUsers(g_wNew);
    } else if (event->type() == QEvent::ActivationChange
               && isActiveWindow()) {
        UpdateButtons();
        if (m_bInverted) {
            QApplication::alert(this, 0);
            m_bInverted = FALSE;
        }
    }
}
