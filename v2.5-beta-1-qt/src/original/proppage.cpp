// Ported from v2.5-beta-1-modern/proppage.cpp.

#include "proppage.h"

#include "avatar.h"
#include "backdrop.h"
#include "bodycam.h"
#include "chat.h"
#include "chatdoc.h"
#include "defines.h"
#include "ircproto.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "saywnd.h"
#include "setupdlg.h"
#include "textview.h"
#include "txtfntdg.h"
#include "whisprbx.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontDialog>
#include <QFontInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {
CPersonalPage* g_personalPage = nullptr;

QString decodedCopyright(const char* copyrightText, const QString& defaultResource)
{
    if (!copyrightText || !*copyrightText) {
        return originalResourceString(defaultResource);
    }

    const QString source = QString::fromLocal8Bit(copyrightText);
    const qsizetype separator = source.indexOf(QStringLiteral("\\n"));
    if (separator < 0) {
        return source;
    }

    const QString copyright = source.left(separator);
    const QString author = source.mid(separator + 2);
    if (author.isEmpty()) {
        return copyright;
    }

    QString format = originalResourceString(QStringLiteral("IDS_COPYRIGHT_PLUS_AUTHOR"));
    format.replace(format.indexOf(QStringLiteral("%s")), 2, copyright);
    format.replace(format.indexOf(QStringLiteral("%s")), 2, author);
    return format;
}

QString displayArtName(QString name)
{
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    return name;
}

void configureOriginalEdit(QLineEdit* edit, int maximumBytes)
{
    // The original is an ANSI/DBCS build and DDV_MaxChars counts bytes. The
    // final byte check is performed by CPersonalPage::validate; maxLength keeps
    // the identical ASCII limit while still allowing Qt input methods.
    edit->setMaxLength(maximumBytes);
}
}

CPersonalPage::CPersonalPage(QWidget* parent)
    : QWidget(parent)
    , m_realName(new QLineEdit(this))
    , m_nickname(new QLineEdit(this))
    , m_email(new QLineEdit(this))
    , m_homePage(new QLineEdit(this))
    , m_profile(new QTextEdit(this))
{
    g_personalPage = this;
    m_realName->setText(QString::fromUtf8(GetMyRealName()));
    m_nickname->setText(QString::fromUtf8(GetMyName()));
    m_email->setText(QString::fromUtf8(GetMyEmail()));
    m_homePage->setText(QString::fromUtf8(GetMyHomePage()));
    m_profile->setPlainText(theApp.m_myProfile);

    configureOriginalEdit(m_nickname, MAX_NICKINPUT);
    configureOriginalEdit(m_realName, MAX_REALNAMEINPUT);
    configureOriginalEdit(m_email, MAX_EMAILINPUT);
    configureOriginalEdit(m_homePage, MAX_HOMEPAGEINPUT);
    m_profile->document()->setMaximumBlockCount(0);

    m_nickname->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[^,]*")), m_nickname));
    m_email->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[^ ]*")), m_email));
    m_homePage->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[^ ]*")), m_homePage));

    auto* layout = new QFormLayout(this);
    layout->addRow(originalDialogControlText(
                       QStringLiteral("IDD_PERSONALPAGE_IRC"),
                       QStringLiteral("IDC_STATIC"), 0), m_realName);
    layout->addRow(originalDialogControlText(
                       QStringLiteral("IDD_PERSONALPAGE_IRC"),
                       QStringLiteral("IDC_STATIC"), 1), m_nickname);
    layout->addRow(originalDialogControlText(
                       QStringLiteral("IDD_PERSONALPAGE_IRC"),
                       QStringLiteral("IDC_STATIC"), 2), m_email);
    layout->addRow(originalDialogControlText(
                       QStringLiteral("IDD_PERSONALPAGE_IRC"),
                       QStringLiteral("IDC_STATIC"), 3), m_homePage);
    layout->addRow(originalDialogControlText(
                       QStringLiteral("IDD_PERSONALPAGE_IRC"),
                       QStringLiteral("IDC_STATIC"), 4), m_profile);
}

CPersonalPage::~CPersonalPage()
{
    if (g_personalPage == this) g_personalPage = nullptr;
}

CPersonalPage* GetPersonalPage()
{
    return g_personalPage;
}

QString CPersonalPage::nickname() const
{
    QString value = m_nickname->text();
    return value.trimmed();
}

QString CPersonalPage::realName() const { return m_realName->text(); }

void CPersonalPage::SetNickname(const QString& nickname)
{
    m_nickname->setText(nickname);
}

bool CPersonalPage::validate()
{
    const QString nick = nickname();
    if (nick.isEmpty()) {
        QMessageBox::warning(this,
                             originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
                             originalResourceString(QStringLiteral("IDS_BLANKNICK")));
        m_nickname->setFocus();
        return false;
    }
    if (nick.toLocal8Bit().size() > MAX_NICKINPUT
        || m_realName->text().toLocal8Bit().size() > MAX_REALNAMEINPUT
        || m_email->text().toLocal8Bit().size() > MAX_EMAILINPUT
        || m_homePage->text().toLocal8Bit().size() > MAX_HOMEPAGEINPUT) {
        return false;
    }
    return true;
}

void CPersonalPage::apply()
{
    const QString newNickname = nickname();
    CChatDoc* document = GetChatDoc();
    if (newNickname.compare(QString::fromUtf8(GetMyName()), Qt::CaseSensitive) != 0) {
        if (document && document->m_proto
            && document->GetConnectionStatus() != CX_DISCONNECTED) {
            document->m_proto->ChatChangeNick(newNickname);
        } else {
            SetMyName(newNickname);
        }
    }
    SetMyRealName(m_realName->text());
    SetMyEmail(m_email->text());
    SetMyHomePage(m_homePage->text());
    theApp.m_myProfile = m_profile->toPlainText().left(MAX_INPUTLEN);
}

CCharacterPage::CCharacterPage(QWidget* parent)
    : QWidget(parent)
    , m_avatarList(new QListWidget(this))
    , m_bodyCam(new CBodyCam(this))
    , m_copyright(new QTextEdit(this))
{
    m_avatarList->setSortingEnabled(true);
    for (const QString& name : GetAllAvatarNames()) {
        auto* item = new QListWidgetItem(displayArtName(name), m_avatarList);
        item->setData(Qt::UserRole, name);
    }

    m_bodyCam->EnableDoubleClick(FALSE);
    m_bodyCam->setMinimumSize(141, 270);
    m_copyright->setReadOnly(true);
    m_copyright->setMaximumHeight(48);

    QString selected = QString::fromUtf8(GetMyCharacter());
    if (GetChatDoc() && GetChatDoc()->m_bComicView && MyAvatar()) {
        selected = QString::fromUtf8(MyAvatar()->OriginalName());
    }
    for (int index = 0; index < m_avatarList->count(); ++index) {
        QListWidgetItem* item = m_avatarList->item(index);
        if (item->data(Qt::UserRole).toString().compare(selected, Qt::CaseInsensitive) == 0) {
            m_avatarList->setCurrentItem(item);
            break;
        }
    }
    if (!m_avatarList->currentItem() && m_avatarList->count() > 0) {
        m_avatarList->setCurrentRow(0);
    }

    auto* grid = new QGridLayout(this);
    grid->addWidget(new QLabel(originalDialogControlText(
        QStringLiteral("IDD_CHARACTERPAGE"), QStringLiteral("IDC_STATIC"), 0),
        this), 0, 0);
    grid->addWidget(new QLabel(originalDialogControlText(
        QStringLiteral("IDD_CHARACTERPAGE"), QStringLiteral("IDC_STATIC"), 1),
        this), 0, 1);
    grid->addWidget(m_avatarList, 1, 0);
    grid->addWidget(m_bodyCam, 1, 1);
    grid->addWidget(m_copyright, 2, 0, 1, 2);

    connect(m_avatarList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { selectAvatar(); });
    selectAvatar();
}

void CCharacterPage::selectAvatar()
{
    QListWidgetItem* item = m_avatarList->currentItem();
    if (!item) {
        m_bodyCam->m_avatar = nullptr;
        m_bodyCam->RefreshBody();
        updateCopyright(nullptr);
        return;
    }

    const QString name = item->data(Qt::UserRole).toString();
    CAvatarX* avatar = GetAvatar2(name);
    if (!avatar) {
        return;
    }
    m_selectedName = QString::fromUtf8(avatar->OriginalName());
    m_bodyCam->m_avatar = avatar;
    m_bodyCam->RefreshBody();
    updateCopyright(avatar);
}

void CCharacterPage::updateCopyright(CAvatarX* avatar)
{
    m_copyright->setPlainText(decodedCopyright(
        avatar ? avatar->Copyright() : nullptr, QStringLiteral("IDS_AUTHOR_NONE")));
}

void CCharacterPage::apply()
{
    if (m_selectedName.isEmpty()) {
        return;
    }
    CChatDoc* document = GetChatDoc();
    if (!document || !document->m_bComicView) {
        SetMyCharacter(m_selectedName);
        return;
    }
    SetMyAvatar(m_selectedName, document->GetConnectionStatus() == CX_INCHANNEL);
}

CBackgroundPage::CBackgroundPage(QWidget* parent)
    : QWidget(parent)
    , m_backgroundList(new QListWidget(this))
    , m_preview(new QLabel(this))
    , m_copyright(new QTextEdit(this))
{
    m_backgroundList->setSortingEnabled(true);
    for (const QString& fileName : OriginalBackdropNames()) {
        auto* item = new QListWidgetItem(displayArtName(QFileInfo(fileName).completeBaseName()),
                                         m_backgroundList);
        item->setData(Qt::UserRole, fileName);
    }
    m_preview->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(220, 255);
    m_copyright->setReadOnly(true);
    m_copyright->setMaximumHeight(48);

    const QString current = QString::fromLocal8Bit(GetCurrentBackDropName());
    for (int index = 0; index < m_backgroundList->count(); ++index) {
        QListWidgetItem* item = m_backgroundList->item(index);
        const QString fileName = item->data(Qt::UserRole).toString();
        if (fileName.compare(current, Qt::CaseInsensitive) == 0
            || QFileInfo(fileName).completeBaseName().compare(current, Qt::CaseInsensitive) == 0) {
            m_backgroundList->setCurrentItem(item);
            break;
        }
    }
    if (!m_backgroundList->currentItem() && m_backgroundList->count() > 0) {
        m_backgroundList->setCurrentRow(0);
    }

    auto* grid = new QGridLayout(this);
    grid->addWidget(new QLabel(originalDialogControlText(
        QStringLiteral("IDD_BACKGROUNDPAGE"), QStringLiteral("IDC_STATIC"), 0),
        this), 0, 0);
    grid->addWidget(new QLabel(originalDialogControlText(
        QStringLiteral("IDD_BACKGROUNDPAGE"), QStringLiteral("IDC_STATIC"), 1),
        this), 0, 1);
    grid->addWidget(m_backgroundList, 1, 0);
    grid->addWidget(m_preview, 1, 1);
    grid->addWidget(m_copyright, 2, 0, 1, 2);

    connect(m_backgroundList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { previewSelection(); });
    previewSelection();
}

void CBackgroundPage::previewSelection()
{
    QListWidgetItem* item = m_backgroundList->currentItem();
    if (!item) {
        m_selectedFile.clear();
        m_preview->clear();
        m_copyright->setPlainText(originalResourceString(
            QStringLiteral("IDS_AUTHOR_NONE_BK")));
        return;
    }

    m_selectedFile = item->data(Qt::UserRole).toString();
    std::unique_ptr<CChatBackdrop> backdrop(LoadBackdropInfo(m_selectedFile));
    CAvatarDIB* drawing = backdrop ? backdrop->GetDrawing() : nullptr;
    if (drawing && !drawing->Image().isNull()) {
        m_preview->setPixmap(QPixmap::fromImage(drawing->Image()).scaled(
            m_preview->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    } else {
        m_preview->clear();
    }
    m_copyright->setPlainText(decodedCopyright(
        backdrop ? backdrop->Copyright() : nullptr, QStringLiteral("IDS_AUTHOR_NONE_BK")));
}

void CBackgroundPage::apply()
{
    if (m_selectedFile.isEmpty()) {
        return;
    }
    const QByteArray encoded = QFile::encodeName(m_selectedFile);
    SetBackDrop(encoded.constData(), nullptr);
}

// -----------------------------------------------------------------------------
// CServersPage

namespace {

class ServerPageDluMapper {
public:
    explicit ServerPageDluMapper(const QFont& font)
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

QFont serverPageFont(const OriginalDialogResource& dialog)
{
    QFont font(dialog.fontFamily);
    if (dialog.fontPointSize > 0) font.setPointSize(dialog.fontPointSize);
    return font;
}

const OriginalDialogControl* serverPageControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

void placeServerPageControl(QWidget* widget,
                            const OriginalDialogResource& dialog,
                            const ServerPageDluMapper& mapper,
                            const QString& identifier, int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = serverPageControl(
            dialog, identifier, occurrence)) {
        widget->setGeometry(mapper.rect(*control));
        widget->setVisible(control->visible);
    }
}

QVariant handleData(void* handle)
{
    return QVariant::fromValue<qulonglong>(
        static_cast<qulonglong>(reinterpret_cast<quintptr>(handle)));
}

void* handleFromData(const QVariant& data)
{
    return reinterpret_cast<void*>(
        static_cast<quintptr>(data.toULongLong()));
}

int findComboText(const QComboBox* combo, const QString& text)
{
    if (!combo) return -1;
    for (int index = 0; index < combo->count(); ++index) {
        if (combo->itemText(index).compare(text, Qt::CaseInsensitive) == 0)
            return index;
    }
    return -1;
}

int insertSortedGroup(CServersOnlyComboBox* combo, const QString& text,
                      void* handle, bool unassociated)
{
    int index = 0;
    while (index < combo->count()
           && QString::localeAwareCompare(combo->itemText(index), text) < 0) {
        ++index;
    }
    const QIcon icon = unassociated
        ? QIcon()
        : QIcon(originalFileResourcePath(
              QStringLiteral("IDI_CONNECT_NET"), QStringLiteral("ICON")));
    combo->insertItem(index, icon, text, handleData(handle));
    return index;
}

QString substitutedDeleteGroupWarning(const QString& group)
{
    QString warning = originalResourceString(
        QStringLiteral("IDS_DELETEGROUP_WARNING"));
    warning.replace(QStringLiteral("%1"), group);
    return warning;
}

} // namespace

// -----------------------------------------------------------------------------
// CTextFontPage

CTextFontPage::CTextFontPage(UINT resourceId, QWidget* parent)
    : QWidget(parent)
{
    // The canonical non-CB32 build contains only IDD_TEXTFONTPAGE_IRC.
    Q_UNUSED(resourceId);
    const QString resourceName = QStringLiteral("IDD_TEXTFONTPAGE_IRC");
    const OriginalDialogResource dialog = originalDialogResource(resourceName);
    const QFont dialogFont = serverPageFont(dialog);
    const ServerPageDluMapper mapper(dialogFont);
    setFont(dialogFont);
    setObjectName(resourceName);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    m_bHeaderSeparate = (theApp.m_flags1 & F1_HEADERSEPARATE) != 0;
    m_spacing = static_cast<BYTE>(theApp.m_textSpacing);
    m_hostHdrsBold = (theApp.m_iHostHighlight & HH_BOLD_HEADERS) != 0;
    m_hostMsgsBold = (theApp.m_iHostHighlight & HH_BOLD_MESSAGES) != 0;
    m_bCfInitialized = theApp.m_bCfInitialized;
    m_bCfHLInitialized = theApp.m_bCfHLInitialized;
    if (m_bCfInitialized) {
        std::memcpy(m_cfArray, theApp.m_cfArray,
                    sizeof(CHARFORMAT) * NREGULARFONTS);
    }
    if (m_bCfHLInitialized) {
        std::memcpy(&m_cfArray[NREGULARFONTS],
                    &theApp.m_cfArray[NREGULARFONTS],
                    sizeof(CHARFORMAT) * NHIGHLIGHTEDFONTS);
    }

    int staticOccurrence = 0;
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != QLatin1String("IDC_STATIC")) continue;
        auto* label = new QLabel(control.text, this);
        label->setWordWrap(control.height > 10);
        placeServerPageControl(label, dialog, mapper,
                               QStringLiteral("IDC_STATIC"),
                               staticOccurrence++);
    }

    for (const QString& identifier : {
             QStringLiteral("IDC_SPACE_GROUP"),
             QStringLiteral("IDC_GROUP1"),
             QStringLiteral("IDC_GROUP2")}) {
        auto* group = new QGroupBox(originalDialogControlText(
            resourceName, identifier), this);
        placeServerPageControl(group, dialog, mapper, identifier);
    }

    m_linespaceAll = new QRadioButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_LINESPACE_ALL")), this);
    placeServerPageControl(m_linespaceAll, dialog, mapper,
                           QStringLiteral("IDC_LINESPACE_ALL"));
    m_linespaceDifferent = new QRadioButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_LINESPACE_DIFFERENT")), this);
    placeServerPageControl(m_linespaceDifferent, dialog, mapper,
                           QStringLiteral("IDC_LINESPACE_DIFFERENT"));
    m_linespaceNone = new QRadioButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_LINESPACE_NONE")), this);
    placeServerPageControl(m_linespaceNone, dialog, mapper,
                           QStringLiteral("IDC_LINESPACE_NONE"));
    if (m_spacing == TEXT_VIEW_BLANK_ALWAYS) m_linespaceAll->setChecked(true);
    else if (m_spacing == TEXT_VIEW_BLANK_DIFFTYPES)
        m_linespaceDifferent->setChecked(true);
    else m_linespaceNone->setChecked(true);

    m_headerSeparate = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_CHKHEADERSEPARATE")), this);
    m_headerSeparate->setChecked(m_bHeaderSeparate);
    placeServerPageControl(m_headerSeparate, dialog, mapper,
                           QStringLiteral("IDC_CHKHEADERSEPARATE"));

    auto* setFont = new QPushButton(originalDialogControlText(
        resourceName, QStringLiteral("ID_SETFONT")), this);
    placeServerPageControl(setFont, dialog, mapper,
                           QStringLiteral("ID_SETFONT"));
    auto* reset = new QPushButton(originalDialogControlText(
        resourceName, QStringLiteral("ID_RESET_TEXTFONTS")), this);
    placeServerPageControl(reset, dialog, mapper,
                           QStringLiteral("ID_RESET_TEXTFONTS"));

    m_hostHeadersBold = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_HOST_HDRS_BOLD")), this);
    m_hostHeadersBold->setChecked(m_hostHdrsBold);
    placeServerPageControl(m_hostHeadersBold, dialog, mapper,
                           QStringLiteral("IDC_HOST_HDRS_BOLD"));
    m_hostMessagesBold = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_HOST_MSGS_BOLD")), this);
    m_hostMessagesBold->setChecked(m_hostMsgsBold);
    placeServerPageControl(m_hostMessagesBold, dialog, mapper,
                           QStringLiteral("IDC_HOST_MSGS_BOLD"));

    connect(setFont, &QPushButton::clicked,
            this, &CTextFontPage::OnSetfont);
    connect(reset, &QPushButton::clicked,
            this, &CTextFontPage::OnResetTextfonts);
    connect(m_linespaceAll, &QRadioButton::clicked,
            this, &CTextFontPage::OnLinespaceAll);
    connect(m_linespaceDifferent, &QRadioButton::clicked,
            this, &CTextFontPage::OnLinespaceDifferent);
    connect(m_linespaceNone, &QRadioButton::clicked,
            this, &CTextFontPage::OnLinespaceNone);
    connect(m_headerSeparate, &QCheckBox::clicked,
            this, &CTextFontPage::OnHeaderSeparate);
    connect(m_hostHeadersBold, &QCheckBox::clicked,
            this, &CTextFontPage::OnHostHdrsBold);
    connect(m_hostMessagesBold, &QCheckBox::clicked,
            this, &CTextFontPage::OnHostMsgsBold);
}

void CTextFontPage::OnSetfont()
{
    CMyFontDialog dialog(this);
    dialog.m_bSetFromCf = m_bCfChangesMade;
    std::memcpy(dialog.m_cfArray, m_cfArray, sizeof(m_cfArray));
    if (dialog.exec() != QDialog::Accepted) return;

    m_bCfInitialized = TRUE;
    m_bCfHLInitialized = TRUE;
    std::memcpy(m_cfArray, dialog.m_cfArray, sizeof(m_cfArray));
    m_bCfChangesMade = TRUE;
}

void CTextFontPage::OnLinespaceAll()
{
    m_spacing = TEXT_VIEW_BLANK_ALWAYS;
}

void CTextFontPage::OnLinespaceDifferent()
{
    m_spacing = TEXT_VIEW_BLANK_DIFFTYPES;
}

void CTextFontPage::OnLinespaceNone()
{
    m_spacing = TEXT_VIEW_BLANK_NEVER;
}

void CTextFontPage::OnResetTextfonts()
{
    m_bCfInitialized = FALSE;
    m_bCfHLInitialized = FALSE;
}

void CTextFontPage::OnHostHdrsBold()
{
    m_hostHdrsBold = m_hostHeadersBold->isChecked();
}

void CTextFontPage::OnHostMsgsBold()
{
    m_hostMsgsBold = m_hostMessagesBold->isChecked();
}

void CTextFontPage::OnHeaderSeparate()
{
    m_bHeaderSeparate = m_headerSeparate->isChecked();
}

void CTextFontPage::apply()
{
    const BOOL restoreOld = !m_bCfInitialized;
    theApp.m_textSpacing = m_spacing;
    theApp.m_bCfInitialized = m_bCfInitialized;
    theApp.m_bCfHLInitialized = m_bCfHLInitialized;
    if (m_bCfInitialized) {
        std::memcpy(theApp.m_cfArray, m_cfArray,
                    sizeof(CHARFORMAT) * NREGULARFONTS);
        theApp.m_textColor = (theApp.m_cfArray[2].dwMask & CFM_COLOR)
            ? theApp.m_cfArray[2].crTextColor : RGB(0, 0, 0);
    }
    if (m_bCfHLInitialized) {
        std::memcpy(&theApp.m_cfArray[NREGULARFONTS],
                    &m_cfArray[NREGULARFONTS],
                    sizeof(CHARFORMAT) * NHIGHLIGHTEDFONTS);
    }

    theApp.m_iHostHighlight = 0;
    if (m_hostHdrsBold) theApp.m_iHostHighlight |= HH_BOLD_HEADERS;
    if (m_hostMsgsBold) theApp.m_iHostHighlight |= HH_BOLD_MESSAGES;
    if (m_bHeaderSeparate) theApp.m_flags1 |= F1_HEADERSEPARATE;
    else theApp.m_flags1 &= ~DWORD(F1_HEADERSEPARATE);

    InitializeTextCores(restoreOld, TRUE);
    InitializeWhisperCores(restoreOld);
}

void SetTextFont()
{
    CMyFontDialog dialog(QApplication::activeWindow());
    if (dialog.exec() == QDialog::Accepted) {
        theApp.m_bCfInitialized = TRUE;
        theApp.m_bCfHLInitialized = TRUE;
        std::memcpy(theApp.m_cfArray, dialog.m_cfArray,
                    sizeof(theApp.m_cfArray));
        theApp.m_textColor = (theApp.m_cfArray[2].dwMask & CFM_COLOR)
            ? theApp.m_cfArray[2].crTextColor : RGB(0, 0, 0);
    }

    if (CChatDoc* document = GetChatDoc()) document->SetFocusToSayWnd();
    InitializeTextCores(FALSE, TRUE);
    InitializeWhisperCores(FALSE);
}

// -----------------------------------------------------------------------------
// CComicsPropPage and the common Comic Font command

namespace {

int comicPointSize(const QFont& logicalFont)
{
    if (logicalFont.pixelSize() > 0)
        return qBound(8, qRound(logicalFont.pixelSize() / 20.0), 18);
    if (logicalFont.pointSizeF() > 0.0)
        return qBound(8, qRound(logicalFont.pointSizeF()), 18);
    return qBound(8, originalResourceString(
        QStringLiteral("IDS_DFLT_COMICSPNTSIZE")).toInt(), 18);
}

QFont comicDialogFont(const QFont& logicalFont)
{
    QFont dialogFont(logicalFont);
    const QString physicalFaceName = QFontInfo(logicalFont).family();
    if (!physicalFaceName.isEmpty()) dialogFont.setFamily(physicalFaceName);
    dialogFont.setPointSize(comicPointSize(logicalFont));
    return dialogFont;
}

QFont comicLogicalFont(const QFont& dialogFont)
{
    QFont logicalFont(dialogFont);
    qreal pointSize = dialogFont.pointSizeF();
    if (pointSize <= 0.0 && dialogFont.pixelSize() > 0)
        pointSize = dialogFont.pixelSize() / 20.0;
    if (pointSize <= 0.0)
        pointSize = originalResourceString(
            QStringLiteral("IDS_DFLT_COMICSPNTSIZE")).toInt();
    const int constrainedPointSize = qBound(8, qRound(pointSize), 18);
    // Comic drawing uses the original MM_TWIPS coordinate domain. QFont's
    // logical pixel size therefore carries the source LOGFONT twips height.
    logicalFont.setPixelSize(constrainedPointSize * 20);
    return logicalFont;
}

} // namespace

void SetComicsFont()
{
    QFontDialog dialog(comicDialogFont(theApp.m_comicsFont),
                       QApplication::activeWindow());
    // The Qt dialog keeps the application's fixed Windows 98 palette. The
    // host-native Linux dialog would leave that replacement boundary.
    dialog.setOption(QFontDialog::DontUseNativeDialog, true);

    if (dialog.exec() == QDialog::Accepted) {
        QFont selected = comicLogicalFont(dialog.selectedFont());
        CUnitPanelPage::SetFonts(selected, theApp.m_comicsColor);
        if (CSayWnd* say = GetSay()) say->SetFont(selected, TRUE);
    }

    if (CChatDoc* document = GetChatDoc()) document->SetFocusToSayWnd();
}

CComicsPropPage::CComicsPropPage(QWidget* parent)
    : QWidget(parent)
{
    const QString resourceName = QStringLiteral("IDD_COMICS_VIEW");
    const OriginalDialogResource dialog = originalDialogResource(resourceName);
    const QFont dialogFont = serverPageFont(dialog);
    const ServerPageDluMapper mapper(dialogFont);
    setFont(dialogFont);
    setObjectName(resourceName);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    m_bShowComicRTF = (theApp.m_flags1 & F1_RTFCOMIC) != 0;
    m_bAutoDownloadChars = theApp.m_bAutoDownloadAvatars;
    m_bAutoDownloadBackdrops = theApp.m_bAutoDownloadBackdrops;
    m_nPanelsSel = qBound(0,
        CUnitPanelPage::GetUnitPanelsPerRow() - 1, 3);

    int staticOccurrence = 0;
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != QLatin1String("IDC_STATIC")) continue;
        auto* label = new QLabel(control.text, this);
        label->setWordWrap(control.height > 10);
        placeServerPageControl(label, dialog, mapper,
                               QStringLiteral("IDC_STATIC"),
                               staticOccurrence++);
    }

    for (const QString& identifier : {
             QStringLiteral("IDC_GROUP0"),
             QStringLiteral("IDC_GROUP1"),
             QStringLiteral("IDC_GROUP2")}) {
        auto* group = new QGroupBox(originalDialogControlText(
            resourceName, identifier), this);
        placeServerPageControl(group, dialog, mapper, identifier);
    }

    auto* setFont = new QPushButton(originalDialogControlText(
        resourceName, QStringLiteral("ID_SETFONT")), this);
    placeServerPageControl(setFont, dialog, mapper,
                           QStringLiteral("ID_SETFONT"));
    auto* resetFont = new QPushButton(originalDialogControlText(
        resourceName, QStringLiteral("ID_RESET_TEXTFONTS")), this);
    placeServerPageControl(resetFont, dialog, mapper,
                           QStringLiteral("ID_RESET_TEXTFONTS"));

    m_showComicRtf = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SHOWCOMICRTF")), this);
    m_showComicRtf->setChecked(m_bShowComicRTF);
    placeServerPageControl(m_showComicRtf, dialog, mapper,
                           QStringLiteral("IDC_SHOWCOMICRTF"));

    m_comboPanels = new QComboBox(this);
    for (const QString& identifier : {
             QStringLiteral("IDS_1_WIDE"),
             QStringLiteral("IDS_2_WIDE"),
             QStringLiteral("IDS_3_WIDE"),
             QStringLiteral("IDS_4_WIDE")}) {
        m_comboPanels->addItem(originalResourceString(identifier));
    }
    m_comboPanels->setCurrentIndex(m_nPanelsSel);
    placeServerPageControl(m_comboPanels, dialog, mapper,
                           QStringLiteral("IDC_PANELS"));

    m_autoDownloadChars = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_AUTODOWNLOAD_CHARS")), this);
    m_autoDownloadChars->setChecked(m_bAutoDownloadChars);
    placeServerPageControl(m_autoDownloadChars, dialog, mapper,
                           QStringLiteral("IDC_AUTODOWNLOAD_CHARS"));
    m_autoDownloadBackdrops = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_AUTODOWNLOAD_BACKDROPS")), this);
    m_autoDownloadBackdrops->setChecked(m_bAutoDownloadBackdrops);
    placeServerPageControl(m_autoDownloadBackdrops, dialog, mapper,
                           QStringLiteral("IDC_AUTODOWNLOAD_BACKDROPS"));

    connect(setFont, &QPushButton::clicked,
            this, &CComicsPropPage::OnSetfont);
    connect(resetFont, &QPushButton::clicked,
            this, &CComicsPropPage::OnResetfont);
    connect(m_comboPanels, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { OnSelchangePanels(); });
    connect(m_showComicRtf, &QCheckBox::clicked,
            this, &CComicsPropPage::OnShowComicRTF);
    connect(m_autoDownloadChars, &QCheckBox::clicked,
            this, &CComicsPropPage::OnAutoDownloadChars);
    connect(m_autoDownloadBackdrops, &QCheckBox::clicked,
            this, &CComicsPropPage::OnAutoDownloadBackdrops);
}

void CComicsPropPage::OnSetfont()
{
    SetComicsFont();
}

void CComicsPropPage::OnResetfont()
{
    theApp.InitializeComicsFonts();
    CUnitPanelPage::SetFonts(theApp.m_comicsFont, theApp.m_comicsColor);
    if (CSayWnd* say = GetSay()) say->SetFont(theApp.m_comicsFont);
}

void CComicsPropPage::OnSelchangePanels()
{
    m_nPanelsSel = m_comboPanels->currentIndex();
    m_bPanelClicked = TRUE;
}

void CComicsPropPage::OnShowComicRTF()
{
    m_bShowComicRTF = m_showComicRtf->isChecked();
}

void CComicsPropPage::OnAutoDownloadChars()
{
    m_bAutoDownloadChars = m_autoDownloadChars->isChecked();
}

void CComicsPropPage::OnAutoDownloadBackdrops()
{
    m_bAutoDownloadBackdrops = m_autoDownloadBackdrops->isChecked();
}

void CComicsPropPage::apply()
{
    if (m_bPanelClicked) {
        const int panelsWide = m_nPanelsSel + 1;
        int smallestPanelWidth = 9999;
        for (CChatDoc* document : g_docs) {
            if (document && document->m_bComicView
                && !document->m_bObscured && document->m_view) {
                smallestPanelWidth = std::min(smallestPanelWidth,
                    document->m_view->GetProspectivePanelWidth(panelsWide));
            }
        }
        for (CChatDoc* document : g_docs) {
            if (document && document->m_bComicView && document->m_view)
                document->m_view->SetPanelsWide(
                    panelsWide, smallestPanelWidth);
        }
        m_bPanelClicked = FALSE;
    }

    if (m_bShowComicRTF) theApp.m_flags1 |= F1_RTFCOMIC;
    else theApp.m_flags1 &= ~DWORD(F1_RTFCOMIC);
    theApp.m_bAutoDownloadAvatars = m_bAutoDownloadChars;
    theApp.m_bAutoDownloadBackdrops = m_bAutoDownloadBackdrops;
}

QString CServersPage::sm_strUnassociatedGroup;

CServersOnlyComboBox::CServersOnlyComboBox(QWidget* parent)
    : QComboBox(parent)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
}

CServersPage::CServersPage(QWidget* parent, CChatServiceList* serviceList)
    : QWidget(parent)
    , m_pSvcList(serviceList ? serviceList : &theApp.m_listChatServices)
{
    const QString resourceName = QStringLiteral("IDD_SERVERSPAGE");
    const OriginalDialogResource dialog = originalDialogResource(resourceName);
    const QFont dialogFont = serverPageFont(dialog);
    const ServerPageDluMapper mapper(dialogFont);
    setFont(dialogFont);
    setObjectName(resourceName);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    sm_strUnassociatedGroup = originalResourceString(
        QStringLiteral("IDS_UNASSOCIATED_GROUP"));
    m_ui.SetServiceList(m_pSvcList);

    int staticOccurrence = 0;
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != QLatin1String("IDC_STATIC")) continue;
        QWidget* widget = nullptr;
        if (control.type == QLatin1String("CONTROL")
            && control.style.contains(QStringLiteral("SS_ETCHEDHORZ"))) {
            auto* separator = new QFrame(this);
            separator->setFrameShape(QFrame::HLine);
            separator->setFrameShadow(QFrame::Sunken);
            widget = separator;
        } else {
            auto* label = new QLabel(control.text, this);
            label->setWordWrap(control.height > 12);
            widget = label;
            if (staticOccurrence == 2) m_connectUsingLabel = label;
        }
        widget->setObjectName(QStringLiteral("IDC_STATIC"));
        widget->setGeometry(mapper.rect(control));
        ++staticOccurrence;
    }

    m_comboGroups = new CServersOnlyComboBox(this);
    m_comboGroups->lineEdit()->setMaxLength(40);
    placeServerPageControl(m_comboGroups, dialog, mapper,
                           QStringLiteral("IDC_SRVGROUP_COMBO"));
    m_addGroup = new QPushButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SRVGROUP_ADD")), this);
    placeServerPageControl(m_addGroup, dialog, mapper,
                           QStringLiteral("IDC_SRVGROUP_ADD"));
    m_removeGroup = new QPushButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SRVGROUP_REMOVE")), this);
    placeServerPageControl(m_removeGroup, dialog, mapper,
                           QStringLiteral("IDC_SRVGROUP_REMOVE"));

    m_serversLabel = new QLabel(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SERVERS_LABEL")), this);
    placeServerPageControl(m_serversLabel, dialog, mapper,
                           QStringLiteral("IDC_SERVERS_LABEL"));
    m_comboServers = new CSimpleComboBox(this);
    m_comboServers->setMaxLength(64);
    m_comboServers->lineEdit()->setObjectName(
        QStringLiteral("IDC_SERVERLIST_EDIT"));
    m_comboServers->listWidget()->setObjectName(
        QStringLiteral("IDC_SERVERLIST_LIST"));
    placeServerPageControl(m_comboServers, dialog, mapper,
                           QStringLiteral("IDC_SERVERLIST"));
    m_addServer = new QPushButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SERVER_ADD")), this);
    placeServerPageControl(m_addServer, dialog, mapper,
                           QStringLiteral("IDC_SERVER_ADD"));
    m_removeServer = new QPushButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SERVER_REMOVE")), this);
    placeServerPageControl(m_removeServer, dialog, mapper,
                           QStringLiteral("IDC_SERVER_REMOVE"));

    m_serverSecurity = new QGroupBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SERVER_SECURITY")), this);
    placeServerPageControl(m_serverSecurity, dialog, mapper,
                           QStringLiteral("IDC_SERVER_SECURITY"));
    m_serverSecurity->lower();
    m_portLabel = new QLabel(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SERVER_PORT_LABEL")), this);
    placeServerPageControl(m_portLabel, dialog, mapper,
                           QStringLiteral("IDC_SERVER_PORT_LABEL"));
    m_port = new QSpinBox(this);
    m_port->setRange(6000, 7000);
    m_port->setGroupSeparatorShown(false);
    m_port->setObjectName(QStringLiteral("IDC_SERVER_PORT_NW"));
    if (QLineEdit* portEdit = m_port->findChild<QLineEdit*>())
        portEdit->setObjectName(QStringLiteral("IDC_SERVER_PORT"));
    const OriginalDialogControl* portControl = serverPageControl(
        dialog, QStringLiteral("IDC_SERVER_PORT"));
    const OriginalDialogControl* spinControl = serverPageControl(
        dialog, QStringLiteral("IDC_SERVER_PORT_NW"));
    if (portControl && spinControl) {
        m_port->setGeometry(mapper.rect(*portControl).united(
            mapper.rect(*spinControl)));
    }
    m_noPassword = new QRadioButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SRVCONNECT_NOPWD")), this);
    placeServerPageControl(m_noPassword, dialog, mapper,
                           QStringLiteral("IDC_SRVCONNECT_NOPWD"));
    m_userPassword = new QRadioButton(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SRVCONNECT_PWD")), this);
    placeServerPageControl(m_userPassword, dialog, mapper,
                           QStringLiteral("IDC_SRVCONNECT_PWD"));
    m_noPasswordMessage = new QLabel(originalDialogControlText(
        resourceName, QStringLiteral("IDC_NOPWD_MESSAGE")), this);
    m_noPasswordMessage->setWordWrap(true);
    placeServerPageControl(m_noPasswordMessage, dialog, mapper,
                           QStringLiteral("IDC_NOPWD_MESSAGE"));
    m_userNameLabel = new QLabel(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SRV_USERNAME_LABEL")), this);
    placeServerPageControl(m_userNameLabel, dialog, mapper,
                           QStringLiteral("IDC_SRV_USERNAME_LABEL"));
    m_userName = new QLineEdit(this);
    m_userName->setMaxLength(30);
    placeServerPageControl(m_userName, dialog, mapper,
                           QStringLiteral("IDC_SRV_USERNAME"));
    m_passwordLabel = new QLabel(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SRV_PASSWORD_LABEL")), this);
    placeServerPageControl(m_passwordLabel, dialog, mapper,
                           QStringLiteral("IDC_SRV_PASSWORD_LABEL"));
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setMaxLength(30);
    placeServerPageControl(m_password, dialog, mapper,
                           QStringLiteral("IDC_SRV_PASSWORD"));
    m_rememberPassword = new QCheckBox(originalDialogControlText(
        resourceName, QStringLiteral("IDC_SRV_REMEMBER_PWD")), this);
    placeServerPageControl(m_rememberPassword, dialog, mapper,
                           QStringLiteral("IDC_SRV_REMEMBER_PWD"));

    HCHATSRVGROUP position = nullptr;
    BOOL unassociated = FALSE;
    if (HCHATSRVGROUP first = m_ui.EnumGroups(position, unassociated)) {
        m_strCurrentGroup = unassociated
            ? sm_strUnassociatedGroup : m_ui.GetGroupName(first);
    }
    ReloadSettings();

    connect(m_comboGroups,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
        if (m_bInDDX) return;
        AcceptServerSettings();
        ChangeServerGroup(false);
    });
    connect(m_comboGroups->lineEdit(), &QLineEdit::textEdited, this,
            [this](const QString&) {
        if (m_bInDDX) return;
        AcceptServerSettings();
        ChangeServerGroup(true);
    });
    connect(m_comboServers->listWidget(),
            &QListWidget::currentRowChanged, this, [this](int) {
        if (m_bInDDX) return;
        AcceptServerSettings();
        ChangeServer(false);
    });
    connect(m_comboServers->lineEdit(), &QLineEdit::textEdited, this,
            [this](const QString&) {
        if (m_bInDDX) return;
        AcceptServerSettings();
        ChangeServer(true);
    });
    connect(m_addGroup, &QPushButton::clicked, this,
            &CServersPage::OnAddServerGroup);
    connect(m_removeGroup, &QPushButton::clicked, this,
            &CServersPage::OnRemoveServerGroup);
    connect(m_addServer, &QPushButton::clicked, this,
            &CServersPage::OnAddServer);
    connect(m_removeServer, &QPushButton::clicked, this,
            &CServersPage::OnRemoveServer);
    connect(m_noPassword, &QRadioButton::clicked, this,
            &CServersPage::OnChangeAuthenticationType);
    connect(m_userPassword, &QRadioButton::clicked, this,
            &CServersPage::OnChangeAuthenticationType);
    connect(m_port, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { OnChangeServerProp(); });
    connect(m_userName, &QLineEdit::textEdited, this,
            [this](const QString&) { OnChangeServerProp(); });
    connect(m_password, &QLineEdit::textEdited, this,
            [this](const QString&) { OnChangeServerProp(); });
    connect(m_rememberPassword, &QCheckBox::clicked, this,
            [this](bool) { OnChangeServerProp(); });
}

CServersPage::~CServersPage()
{
    if (!m_applied) m_ui.Revert();
}

void CServersPage::ReloadSettings()
{
    m_bInDDX = true;
    m_comboGroups->clear();
    m_comboServers->clear();

    bool unassociatedCreated = false;
    HCHATSRVGROUP position = nullptr;
    BOOL unassociated = FALSE;
    HCHATSRVGROUP group = nullptr;
    while ((group = m_ui.EnumGroups(position, unassociated)) != nullptr) {
        insertSortedGroup(m_comboGroups,
                          unassociated ? sm_strUnassociatedGroup
                                       : m_ui.GetGroupName(group),
                          group, unassociated);
        if (unassociated) unassociatedCreated = true;
    }
    if (!unassociatedCreated) {
        group = m_ui.AddGroup(g_szGroupUnassociated);
        if (group) {
            insertSortedGroup(m_comboGroups, sm_strUnassociatedGroup,
                              group, true);
        }
    }

    m_nCurrSelGroup = findComboText(m_comboGroups, m_strCurrentGroup);
    m_comboGroups->setCurrentIndex(m_nCurrSelGroup);
    if (m_nCurrSelGroup < 0) m_comboGroups->setEditText(m_strCurrentGroup);
    SwitchGroup();
    m_nCurrSelServer = m_comboServers->findText(m_strCurrentServer);
    m_comboServers->setCurrentIndex(m_nCurrSelServer);
    if (m_nCurrSelServer < 0)
        m_comboServers->setEditText(m_strCurrentServer);
    SwitchServer();
    UpdateUIState();
    m_bInDDX = false;
}

HCHATSRVGROUP CServersPage::GetCurrGroup() const
{
    if (m_nCurrSelGroup < 0 || m_nCurrSelGroup >= m_comboGroups->count())
        return nullptr;
    return handleFromData(m_comboGroups->itemData(m_nCurrSelGroup));
}

HCHATSERVER CServersPage::GetCurrServer() const
{
    if (m_nCurrSelServer < 0 || m_nCurrSelServer >= m_comboServers->count())
        return nullptr;
    return handleFromData(m_comboServers->itemData(m_nCurrSelServer));
}

void CServersPage::SwitchGroup()
{
    m_comboServers->clear();
    if (HCHATSRVGROUP group = GetCurrGroup()) {
        HCHATSERVER position = nullptr;
        HCHATSERVER server = nullptr;
        while ((server = m_ui.EnumServersInGroup(group, position)) != nullptr)
            m_comboServers->addItem(m_ui.GetServerName(server),
                                    handleData(server));
    }
    m_nCurrSelServer = -1;
}

void CServersPage::SwitchServer()
{
    if (HCHATSERVER server = GetCurrServer()) {
        CChatServiceUI::ServerProps data;
        m_ui.GetServerProps(server, data);
        m_nPort = static_cast<int>(data.m_nPort);
        m_nSecurity = static_cast<int>(data.m_nAuthenticationType);
        m_strUserName = data.m_strUserName;
        m_strPassword = data.m_strPassword;
        m_bRememberPassword = data.m_bRememberPassword;
        m_strSecurityPackages = data.m_strSecurityPackages;
        if (m_nSecurity > 1) m_nSecurity = 0;
    } else {
        m_nPort = 6667;
        m_nSecurity = 0;
        m_strUserName.clear();
        m_strPassword.clear();
        m_bRememberPassword = false;
        m_strSecurityPackages.clear();
    }
    SetServerPropsToPage();
    m_bServerPropChange = false;
}

void CServersPage::SetServerPropsToPage()
{
    const bool previous = m_bInDDX;
    m_bInDDX = true;
    m_noPassword->setChecked(m_nSecurity == 0);
    m_userPassword->setChecked(m_nSecurity == 1);
    m_port->setValue(m_nPort);
    if (m_nSecurity == 1) {
        m_userName->setText(m_strUserName);
        m_password->setText(m_strPassword);
        m_rememberPassword->setChecked(m_bRememberPassword);
    } else {
        m_userName->clear();
        m_password->clear();
        m_rememberPassword->setChecked(false);
    }
    m_bInDDX = previous;
}

void CServersPage::GetServerPropsFromPage()
{
    m_nSecurity = m_userPassword->isChecked() ? 1 : 0;
    if (m_nSecurity == 1) {
        m_strUserName = m_userName->text();
        m_strPassword = m_password->text();
        m_bRememberPassword = m_rememberPassword->isChecked();
    } else {
        m_strUserName.clear();
        m_strPassword.clear();
        m_bRememberPassword = false;
    }
    if (m_nSecurity != 3) m_strSecurityPackages.clear();
    m_nPort = m_port->value();
}

bool CServersPage::ValidatePortNumber()
{
    if (m_port->hasAcceptableInput()
        && m_port->value() >= 6000 && m_port->value() <= 7000) {
        return true;
    }
    QMessageBox::warning(
        this, originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
        originalResourceString(QStringLiteral("IDS_PORTNUM_ERROR")));
    const bool previous = m_bInDDX;
    m_bInDDX = true;
    m_port->setValue(m_nPort);
    m_bInDDX = previous;
    m_port->setFocus();
    return false;
}

bool CServersPage::AcceptServerSettings()
{
    if (!m_bServerPropChange) return true;
    bool valid = true;
    HCHATSERVER server = GetCurrServer();
    HCHATSRVGROUP group = GetCurrGroup();
    if (server && group) {
        if (!ValidatePortNumber()) valid = false;
        GetServerPropsFromPage();
        CChatServiceUI::ServerProps data;
        data.m_nPort = static_cast<UINT>(m_nPort);
        data.m_nAuthenticationType = static_cast<UINT>(m_nSecurity);
        data.m_strUserName = m_strUserName;
        data.m_strPassword = m_strPassword;
        data.m_bRememberPassword = m_bRememberPassword;
        data.m_strSecurityPackages = m_strSecurityPackages;
        m_ui.SetServerProps(group, server, data);
        m_bServerPropChange = false;
    }
    return valid;
}

void CServersPage::ChangeServerGroup(bool keystroke, bool force)
{
    int selection = keystroke ? -1 : m_comboGroups->currentIndex();
    QString text;
    if (selection < 0) {
        text = m_comboGroups->currentText().trimmed();
        const int exact = findComboText(m_comboGroups, text);
        const QSignalBlocker blocker(m_comboGroups);
        if (exact >= 0) {
            selection = exact;
            m_comboGroups->setCurrentIndex(exact);
        } else {
            m_comboGroups->setCurrentIndex(-1);
            m_comboGroups->setEditText(text);
        }
    } else {
        text = m_comboGroups->itemText(selection);
    }
    m_strCurrentGroup = text;
    if (force || selection != m_nCurrSelGroup) {
        m_nCurrSelGroup = selection;
        m_strCurrentServer.clear();
        SwitchGroup();
        SwitchServer();
        UpdateUIState(updateChangeCurrSrvGroup);
    } else {
        UpdateUIState(updateNewServerGroupText);
    }
}

void CServersPage::ChangeServer(bool keystroke, bool force)
{
    int selection = keystroke ? -1 : m_comboServers->currentIndex();
    QString text;
    if (selection < 0) {
        text = m_comboServers->currentText().trimmed();
        const int exact = m_comboServers->findText(text);
        const QSignalBlocker blocker(m_comboServers->listWidget());
        if (exact >= 0) {
            selection = exact;
            m_comboServers->setCurrentIndex(exact);
        } else {
            m_comboServers->setCurrentIndex(-1);
            m_comboServers->setEditText(text);
        }
    } else {
        text = m_comboServers->itemText(selection);
    }
    m_strCurrentServer = text;
    if (force || selection != m_nCurrSelServer) {
        m_nCurrSelServer = selection;
        SwitchServer();
        UpdateUIState(updateChangeCurrServer);
    } else {
        UpdateUIState(updateNewServerText);
    }
}

void CServersPage::UpdateUIState(unsigned int updateHint)
{
    if (updateHint == 0) return;
    const bool groupSelected = m_nCurrSelGroup >= 0;
    const bool serverSelected = m_nCurrSelServer >= 0;

    if (updateHint & updateelemEnableServerUI) {
        m_serversLabel->setEnabled(groupSelected);
        m_comboServers->setEnabled(groupSelected);
    }

    if (updateHint & updateelemAddRemoveGroup) {
        const bool existingGroup = findComboText(
            m_comboGroups, m_strCurrentGroup) >= 0;
        const bool canAdd = !groupSelected
            && !m_strCurrentGroup.isEmpty()
            && !m_strCurrentGroup.startsWith(QLatin1Char('.'))
            && !m_strCurrentGroup.startsWith(QLatin1Char('{'))
            && !m_strCurrentGroup.contains(QLatin1Char('/'))
            && !m_strCurrentGroup.contains(QLatin1Char('\\'))
            && m_strCurrentGroup.compare(sm_strUnassociatedGroup,
                                         Qt::CaseInsensitive) != 0
            && !existingGroup;
        const bool canRemove = groupSelected
            && m_strCurrentGroup.compare(sm_strUnassociatedGroup,
                                         Qt::CaseInsensitive) != 0;
        m_addGroup->setEnabled(canAdd);
        m_removeGroup->setEnabled(canRemove);
    }

    if (updateHint & updateelemAddRemoveServer) {
        const bool existingServer = m_comboServers->findText(
            m_strCurrentServer) >= 0;
        const bool canAdd = groupSelected && !serverSelected
            && !m_strCurrentServer.isEmpty()
            && !m_strCurrentServer.startsWith(QLatin1Char('.'))
            && !m_strCurrentServer.contains(QLatin1Char('/'))
            && !m_strCurrentServer.contains(QLatin1Char('\\'))
            && !existingServer;
        m_addServer->setEnabled(canAdd);
        m_removeServer->setEnabled(groupSelected && serverSelected);
    }

    if (updateHint & updateelemServerProps) {
        const QList<QWidget*> baseControls = {
            m_serverSecurity, m_portLabel, m_port, m_connectUsingLabel,
            m_noPassword, m_userPassword, m_noPasswordMessage,
            m_userNameLabel, m_userName, m_passwordLabel, m_password,
            m_rememberPassword};
        for (QWidget* control : baseControls)
            if (control) control->setEnabled(serverSelected);
        const bool passwordAuth = serverSelected && m_nSecurity == 1;
        m_userNameLabel->setEnabled(passwordAuth);
        m_userName->setEnabled(passwordAuth);
        m_passwordLabel->setEnabled(passwordAuth);
        m_password->setEnabled(passwordAuth);
        m_rememberPassword->setEnabled(passwordAuth);
    }
}

void CServersPage::OnAddServer()
{
    HCHATSRVGROUP group = GetCurrGroup();
    if (!group) return;
    QString translated;
    int port = 6667;
    TranslateServerNameToServerAndPort(m_strCurrentServer,
                                       &translated, &port);
    Q_UNUSED(translated);
    HCHATSERVER server = m_ui.AddServer(group, m_strCurrentServer, port);
    if (!server) return;
    const int selection = m_comboServers->addItem(
        m_strCurrentServer, handleData(server));
    {
        const QSignalBlocker blocker(m_comboServers->listWidget());
        m_comboServers->setCurrentIndex(selection);
    }
    ChangeServer(false, true);
    UpdateUIState(updateAddServer);
    m_comboServers->setFocus();
}

void CServersPage::OnRemoveServer()
{
    HCHATSRVGROUP group = GetCurrGroup();
    HCHATSERVER server = GetCurrServer();
    if (!group || !server || !m_ui.RemoveServer(group, server)) return;
    const int oldSelection = m_nCurrSelServer;
    m_comboServers->removeItem(oldSelection);
    const int nextSelection = oldSelection < m_comboServers->count()
        ? oldSelection : oldSelection - 1;
    {
        const QSignalBlocker blocker(m_comboServers->listWidget());
        m_comboServers->setCurrentIndex(nextSelection);
    }
    ChangeServer(false, true);
    UpdateUIState(updateRemoveServer);
    m_comboServers->setFocus();
}

void CServersPage::OnAddServerGroup()
{
    if (m_nCurrSelGroup >= 0) return;
    HCHATSRVGROUP group = m_ui.AddGroup(m_strCurrentGroup);
    if (!group) return;
    const int selection = insertSortedGroup(
        m_comboGroups, m_strCurrentGroup, group, false);
    {
        const QSignalBlocker blocker(m_comboGroups);
        m_comboGroups->setCurrentIndex(selection);
    }
    ChangeServerGroup(false, true);
    UpdateUIState(updateAddSrvGroup);
    m_comboServers->setFocus();
}

void CServersPage::OnRemoveServerGroup()
{
    HCHATSRVGROUP group = GetCurrGroup();
    if (!group) return;
    if (!m_ui.IsGroupEmpty(group)) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            this,
            originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
            substitutedDeleteGroupWarning(m_strCurrentGroup),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }
    if (!m_ui.RemoveGroup(group)) return;
    const int oldSelection = m_nCurrSelGroup;
    m_comboGroups->removeItem(oldSelection);
    const int nextSelection = oldSelection < m_comboGroups->count()
        ? oldSelection : oldSelection - 1;
    {
        const QSignalBlocker blocker(m_comboGroups);
        m_comboGroups->setCurrentIndex(nextSelection);
    }
    ChangeServerGroup(false, true);
    UpdateUIState(updateRemoveSrvGroup);
    m_comboGroups->setFocus();
}

void CServersPage::OnChangeAuthenticationType()
{
    m_nSecurity = m_userPassword->isChecked() ? 1 : 0;
    UpdateUIState(updateChangeAuthenticationType);
    m_bServerPropChange = true;
}

void CServersPage::OnChangeServerProp()
{
    if (!m_bInDDX) m_bServerPropChange = true;
}

bool CServersPage::validate()
{
    return AcceptServerSettings();
}

bool CServersPage::apply()
{
    if (!AcceptServerSettings() || !m_ui.Apply()) return false;
    m_applied = true;
    return true;
}

void CServersPage::revert()
{
    if (m_applied) return;
    m_ui.Revert();
}

COptionsDialog::COptionsDialog(QWidget* parent)
    : QDialog(parent)
{
    build(theApp.m_bComicView, 0);
}

COptionsDialog::COptionsDialog(BOOL comicsMode, UINT initialPageId,
                               QWidget* parent)
    : QDialog(parent)
{
    build(comicsMode, initialPageId);
}

void COptionsDialog::build(BOOL comicsMode, UINT initialPageId)
{
    setWindowTitle(originalResourceString(QStringLiteral("IDS_OPTIONS")));
    auto* tabs = new QTabWidget(this);
    auto* personal = new CPersonalPage(tabs);
    CComicsPropPage* comics = comicsMode
        ? new CComicsPropPage(tabs) : nullptr;
    auto* character = new CCharacterPage(tabs);
    auto* background = new CBackgroundPage(tabs);
    CTextFontPage* textFont = comicsMode
        ? nullptr : new CTextFontPage(IDD_TEXTFONTPAGE_IRC, tabs);
    auto* servers = new CServersPage(tabs);
    tabs->addTab(personal, originalDialogCaption(
        QStringLiteral("IDD_PERSONALPAGE_IRC")));
    if (comicsMode) {
        tabs->addTab(comics, originalDialogCaption(
            QStringLiteral("IDD_COMICS_VIEW")));
        tabs->addTab(character, originalDialogCaption(
            QStringLiteral("IDD_CHARACTERPAGE")));
        tabs->addTab(background, originalDialogCaption(
            QStringLiteral("IDD_BACKGROUNDPAGE")));
    } else {
        tabs->addTab(textFont, originalDialogCaption(
            QStringLiteral("IDD_TEXTFONTPAGE_IRC")));
    }
    tabs->addTab(servers, originalDialogCaption(
        QStringLiteral("IDD_SERVERSPAGE")));

    if (initialPageId == IDD_PERSONALPAGE_IRC)
        tabs->setCurrentWidget(personal);
    else if (initialPageId == IDD_COMICS_VIEW && comicsMode)
        tabs->setCurrentWidget(comics);
    else if (initialPageId == IDD_CHARACTERPAGE && comicsMode)
        tabs->setCurrentWidget(character);
    else if (initialPageId == IDD_BACKGROUNDPAGE && comicsMode)
        tabs->setCurrentWidget(background);
    else if (initialPageId == IDD_TEXTFONTPAGE_IRC && textFont)
        tabs->setCurrentWidget(textFont);
    else if (initialPageId == IDD_SERVERSPAGE)
        tabs->setCurrentWidget(servers);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            [this, tabs, personal, comics, character, background,
             textFont, servers, comicsMode] {
        if (!personal->validate()) return;
        if (!servers->validate()) {
            tabs->setCurrentWidget(servers);
            return;
        }
        personal->apply();
        if (comicsMode) {
            comics->apply();
            character->apply();
            background->apply();
        } else if (textFont) {
            textFont->apply();
        }
        if (!servers->apply()) {
            tabs->setCurrentWidget(servers);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this,
            [this, servers] {
        servers->revert();
        reject();
    });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}
