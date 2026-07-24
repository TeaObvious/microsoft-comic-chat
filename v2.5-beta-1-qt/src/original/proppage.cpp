// Ported from v2.5-beta-1-modern/proppage.cpp.

#include "proppage.h"

#include "avatar.h"
#include "backdrop.h"
#include "bodycam.h"
#include "chat.h"
#include "chatdoc.h"
#include "colordlg.h"
#include "defines.h"
#include "histent.h"
#include "ircproto.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "protsupp.h"
#include "saywnd.h"
#include "setupdlg.h"
#include "textview.h"
#include "txtfntdg.h"
#include "whisprbx.h"

#include <QApplication>
#include <QBoxLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDialog>
#include <QFontInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {
CPersonalPage* g_personalPage = nullptr;
constexpr int kSourceMaxPath = 260;

class CBackgroundListWidget final : public QListWidget {
public:
    explicit CBackgroundListWidget(QWidget* parent)
        : QListWidget(parent)
    {
    }

    void preserveNoCurrentItem(bool preserve)
    {
        m_preserveNoCurrentItem = preserve;
    }

protected:
    void focusInEvent(QFocusEvent* event) override
    {
        if (!m_preserveNoCurrentItem) {
            QListWidget::focusInEvent(event);
            return;
        }
        const QSignalBlocker blocker(this);
        QListWidget::focusInEvent(event);
        clearSelection();
        selectionModel()->clearCurrentIndex();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        m_preserveNoCurrentItem = false;
        QListWidget::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        m_preserveNoCurrentItem = false;
        QListWidget::mousePressEvent(event);
    }

private:
    bool m_preserveNoCurrentItem = false;
};

class DialogUnitMapper {
public:
    explicit DialogUnitMapper(const QFont& font)
    {
        const QFontMetrics metrics(font);
        const QString alphabet = QStringLiteral(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        m_baseX = qMax(1,
            (metrics.horizontalAdvance(alphabet) / 26 + 1) / 2);
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

QFont propertyPageFont(const OriginalDialogResource& dialog)
{
    QFont font(dialog.fontFamily);
    if (dialog.fontPointSize > 0) font.setPointSize(dialog.fontPointSize);
    return font;
}

const OriginalDialogControl* propertyPageControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

void placePropertyPageControl(
    QWidget* widget, const OriginalDialogResource& dialog,
    const DialogUnitMapper& mapper, const QString& identifier,
    int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = propertyPageControl(
            dialog, identifier, occurrence)) {
        widget->setGeometry(mapper.rect(*control));
        widget->setVisible(control->visible);
    }
}

QLabel* createPropertyPageLabel(
    QWidget* parent, const QString& resource,
    const OriginalDialogResource& dialog, const DialogUnitMapper& mapper,
    int occurrence, QWidget* buddy = nullptr)
{
    auto* label = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_STATIC"), occurrence), parent);
    if (const OriginalDialogControl* control = propertyPageControl(
            dialog, QStringLiteral("IDC_STATIC"), occurrence)) {
        label->setWordWrap(control->height > 10);
    }
    if (buddy) label->setBuddy(buddy);
    placePropertyPageControl(label, dialog, mapper,
                             QStringLiteral("IDC_STATIC"), occurrence);
    return label;
}

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

int sourceByteLength(const QString& value)
{
    return value.toLocal8Bit().size();
}

int sourceCharacterLimit(const QString& value, int maximumBytes)
{
    int accepted = 0;
    while (accepted < value.size()
           && sourceByteLength(value.left(accepted + 1)) <= maximumBytes) {
        ++accepted;
    }
    return accepted;
}

QString removeDuplicatePathEntries(const QString& path)
{
    QStringList entries;
    bool duplicate = false;
    qsizetype position = 0;
    while (position < path.size()) {
        while (position < path.size() && path.at(position).isSpace())
            ++position;
        if (position >= path.size()) break;

        QString entry;
        if (path.at(position) == QLatin1Char('"')) {
            // The original quoted branch searches from the opening quote
            // itself, so it does not establish a usable quoted-path
            // de-duplication rule. Preserve the complete value instead of
            // inventing repaired quote semantics.
            return path;
        } else {
            const qsizetype separator = path.indexOf(
                QLatin1Char(';'), position);
            const qsizetype end = separator < 0 ? path.size() : separator;
            entry = path.mid(position, end - position).trimmed();
            position = separator < 0 ? path.size() : separator + 1;
        }

        bool alreadyPresent = false;
        for (const QString& existing : entries) {
            if (existing.compare(entry, Qt::CaseInsensitive) == 0) {
                alreadyPresent = true;
                duplicate = true;
                break;
            }
        }
        if (!alreadyPresent) entries.append(entry);
    }
    return duplicate ? entries.join(QLatin1Char(';')) : path;
}
}

// -----------------------------------------------------------------------------
// CSettingsPage

CSettingsPage::CSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    const QString resource = QStringLiteral("IDD_SETTINGSPAGE");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = propertyPageFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setObjectName(resource);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    auto* connection = new QGroupBox(originalDialogControlText(
        resource, QStringLiteral("IDC_GROUP0")), this);
    placePropertyPageControl(connection, dialog, mapper,
                             QStringLiteral("IDC_GROUP0"));
    createPropertyPageLabel(this, resource, dialog, mapper, 0);

    const auto checkBox = [&](const QString& identifier) {
        auto* box = new QCheckBox(originalDialogControlText(
            resource, identifier), this);
        placePropertyPageControl(box, dialog, mapper, identifier);
        return box;
    };

    m_comicsData = checkBox(QStringLiteral("IDC_COMICSDATA"));

    auto* ratingsGroup = new QGroupBox(originalDialogControlText(
        resource, QStringLiteral("IDC_ADVANCED_RATINGS_GROUPBOX")), this);
    placePropertyPageControl(ratingsGroup, dialog, mapper,
                             QStringLiteral("IDC_ADVANCED_RATINGS_GROUPBOX"));
    auto* ratingsIcon = new QLabel(this);
    placePropertyPageControl(ratingsIcon, dialog, mapper,
                             QStringLiteral("IDC_RATINGS_ICON"));
    const QString ratingsIconPath = originalFileResourcePath(
        QStringLiteral("IDI_RATINGS"), QStringLiteral("ICON"));
    if (!ratingsIconPath.isEmpty()) {
        ratingsIcon->setPixmap(QIcon(ratingsIconPath).pixmap(
            ratingsIcon->size()));
    }
    auto* ratingsText = new QLabel(originalDialogControlText(
        resource, QStringLiteral("IDC_RATINGS_TEXT")), this);
    ratingsText->setWordWrap(true);
    placePropertyPageControl(ratingsText, dialog, mapper,
                             QStringLiteral("IDC_RATINGS_TEXT"));
    auto* ratingsOn = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDC_RATINGS_TURN_ON")), this);
    placePropertyPageControl(ratingsOn, dialog, mapper,
                             QStringLiteral("IDC_RATINGS_TURN_ON"));
    auto* ratingsAdvanced = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDC_ADVANCED_RATINGS_BUTTON")), this);
    placePropertyPageControl(ratingsAdvanced, dialog, mapper,
                             QStringLiteral("IDC_ADVANCED_RATINGS_BUTTON"));

    // This is the source branch taken when MSRATING.DLL cannot be loaded.
    ratingsGroup->setEnabled(false);
    ratingsIcon->setEnabled(false);
    ratingsText->setEnabled(false);
    ratingsOn->setEnabled(false);
    ratingsAdvanced->setEnabled(false);

    m_acceptWhispers = checkBox(QStringLiteral("IDC_ACCEPTWHISPERS"));
    m_playSounds = checkBox(QStringLiteral("IDC_PLAYSOUNDS"));
    m_showArrivals = checkBox(QStringLiteral("IDC_SHOWARRIVALS"));
    m_showIdentity = checkBox(QStringLiteral("IDC_SHOWIDENTITY"));
    m_visible = checkBox(QStringLiteral("IDC_INVISIBLE"));
    m_allowInvites = checkBox(QStringLiteral("IDC_ALLOWINVITES"));
    m_allowFileTx = checkBox(QStringLiteral("IDC_ALLOW_FILETX"));
    m_acceptNmCalls = checkBox(QStringLiteral("IDC_NETMEETING_AUTOSTART"));
    m_save = checkBox(QStringLiteral("IDC_SAVE"));

    createPropertyPageLabel(this, resource, dialog, mapper, 1);
    m_soundPath = new QLineEdit(this);
    placePropertyPageControl(m_soundPath, dialog, mapper,
                             QStringLiteral("IDC_SOUNDPATH"));
    m_soundPath->setMaxLength(kSourceMaxPath);
    auto* browseSoundPath = new QPushButton(originalDialogControlText(
        resource, QStringLiteral("IDC_SOUNDPATH_BROWSE")), this);
    placePropertyPageControl(browseSoundPath, dialog, mapper,
                             QStringLiteral("IDC_SOUNDPATH_BROWSE"));
    // CBrowseFolderDialogEx and the MCI sound package are deferred. Do not
    // replace their source-defined workflow with a native Qt picker.
    browseSoundPath->setEnabled(false);

    m_comicsData->setChecked(!GetSendComicsData());
    m_acceptWhispers->setChecked(theApp.m_bAcceptWhispers);
    m_playSounds->setChecked(theApp.m_bPlaySounds);
    m_showArrivals->setChecked(theApp.m_bShowArrivals);
    m_showIdentity->setChecked(theApp.m_bShowIdentity);
    m_visible->setChecked((theApp.m_flags1 & F1_USERVISIBLE) != 0);
    m_allowInvites->setChecked(theApp.m_bAllowInvites);
    m_allowFileTx->setChecked(theApp.m_bAllowFileTX);
    m_acceptNmCalls->setChecked(theApp.m_bAcceptNMCalls);
    m_save->setChecked(theApp.m_bPrompt);
    m_soundPath->setText(theApp.m_soundPath);
    if (GetChatDoc() && theApp.m_bEmbedded) m_save->setEnabled(false);
}

bool CSettingsPage::validate()
{
    if (sourceByteLength(m_soundPath->text()) <= kSourceMaxPath) return true;
    m_soundPath->setFocus();
    return false;
}

void CSettingsPage::apply()
{
    const bool sendComicsData = !m_comicsData->isChecked();
    if (sendComicsData != GetSendComicsData()) ToggleSendComicsData();

    const bool save = m_save->isChecked();
    theApp.m_bPrompt = save;
    for (CChatDoc* document : g_docs) {
        if (document) document->SetModifiedFlag(save);
    }

    theApp.m_bAcceptWhispers = m_acceptWhispers->isChecked();
    theApp.m_bAllowInvites = m_allowInvites->isChecked();
    theApp.m_bAllowFileTX = m_allowFileTx->isChecked();
    theApp.m_bShowArrivals = m_showArrivals->isChecked();
    theApp.m_bPlaySounds = m_playSounds->isChecked();
    theApp.m_bAcceptNMCalls = m_acceptNmCalls->isChecked();
    theApp.m_bShowIdentity = m_showIdentity->isChecked();

    const bool visible = m_visible->isChecked();
    if (((theApp.m_flags1 & F1_USERVISIBLE) != 0) != visible) {
        if (auto* protocol = dynamic_cast<CIrcProto*>(GetDefaultProto()))
            protocol->SetVisibility(visible);
    }

    theApp.m_soundPath = removeDuplicatePathEntries(m_soundPath->text());
}

// -----------------------------------------------------------------------------
// CPersonalPage

CPersonalPage::CPersonalPage(QWidget* parent)
    : QWidget(parent)
    , m_rtfProfile(this)
    , m_realName(new QLineEdit(this))
    , m_nickname(new QLineEdit(this))
    , m_email(new QLineEdit(this))
    , m_homePage(new QLineEdit(this))
{
    const QString resource = QStringLiteral("IDD_PERSONALPAGE_IRC");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = propertyPageFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setObjectName(resource);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    m_realName->setText(QString::fromUtf8(GetMyRealName()));
    m_nickname->setText(QString::fromUtf8(GetMyName()));
    m_email->setText(QString::fromUtf8(GetMyEmail()));
    m_homePage->setText(QString::fromUtf8(GetMyHomePage()));

    configureOriginalEdit(m_nickname, MAX_NICKINPUT);
    configureOriginalEdit(m_realName, MAX_REALNAMEINPUT);
    configureOriginalEdit(m_email, MAX_EMAILINPUT);
    configureOriginalEdit(m_homePage, MAX_HOMEPAGEINPUT);

    m_nickname->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[^,]*")), m_nickname));
    m_email->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\S*")), m_email));
    m_homePage->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("\\S*")), m_homePage));

    placePropertyPageControl(m_realName, dialog, mapper,
                             QStringLiteral("IDC_REALNAME"));
    placePropertyPageControl(m_nickname, dialog, mapper,
                             QStringLiteral("IDC_NICKNAME"));
    placePropertyPageControl(m_email, dialog, mapper,
                             QStringLiteral("IDC_EMAIL"));
    placePropertyPageControl(m_homePage, dialog, mapper,
                             QStringLiteral("IDC_HOMEPAGE"));
    placePropertyPageControl(&m_rtfProfile, dialog, mapper,
                             QStringLiteral("IDC_PROFILE_RICHEDIT"));
    createPropertyPageLabel(this, resource, dialog, mapper, 0, m_realName);
    createPropertyPageLabel(this, resource, dialog, mapper, 1, m_nickname);
    createPropertyPageLabel(this, resource, dialog, mapper, 2, m_email);
    createPropertyPageLabel(this, resource, dialog, mapper, 3, m_homePage);
    createPropertyPageLabel(this, resource, dialog, mapper, 4, &m_rtfProfile);

    for (const QString& identifier : {
             QStringLiteral("IDC_ACCEPTWHISPERS"),
             QStringLiteral("IDC_SHOWARRIVALS"),
             QStringLiteral("IDC_SAVE")}) {
        auto* unused = new QCheckBox(originalDialogControlText(
            resource, identifier), this);
        placePropertyPageControl(unused, dialog, mapper, identifier);
    }

    const QColor profileColor = palette().color(QPalette::Text);
    m_rtfProfile.m_crTextColor = RGB(
        profileColor.red(), profileColor.green(), profileColor.blue());
    m_rtfProfile.DefineDefaultCharFormat();
    m_rtfProfile.UseDefaultCharFormat();
    if (theApp.m_myProfile.isEmpty()) {
        m_rtfProfile.m_strText = originalResourceString(
            QStringLiteral("ID_DEFAULT_PROFILE"));
    } else {
        QByteArray controlFull = theApp.m_myProfile.toUtf8();
        m_rtfProfile.m_prgdwFormatting = new CDWordArray;
        char* controlLess = SzControlLess(
            controlFull.data(), m_rtfProfile.m_prgdwFormatting);
        m_rtfProfile.m_strText = QString::fromUtf8(controlLess);
    }
    m_rtfProfile.bSetWindowFormattedText(
        m_rtfProfile.m_strText, m_rtfProfile.m_prgdwFormatting);

    connect(&m_rtfProfile, &QTextEdit::textChanged, this, [this] {
        const QString text = m_rtfProfile.toPlainText();
        if (sourceByteLength(text) <= MAX_INPUTLEN) return;
        const int keep = sourceCharacterLimit(text, MAX_INPUTLEN);
        const QSignalBlocker blocker(&m_rtfProfile);
        QTextCursor cursor(m_rtfProfile.document());
        cursor.setPosition(keep);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    });

    setTabOrder(m_realName, m_nickname);
    setTabOrder(m_nickname, m_email);
    setTabOrder(m_email, m_homePage);
    setTabOrder(m_homePage, &m_rtfProfile);
}

CPersonalPage::~CPersonalPage()
{
    if (g_personalPage == this) g_personalPage = nullptr;
}

CPersonalPage* GetPersonalPage()
{
    return g_personalPage;
}

void CPersonalPage::showEvent(QShowEvent* event)
{
    g_personalPage = this;
    const CRoomInfo* protocol = GetDefaultProto();
    const int status = protocol
        ? protocol->GetConnectionStatus() : CX_DISCONNECTED;
    m_realName->setEnabled(status == CX_DISCONNECTED);
    QWidget::showEvent(event);
}

void CPersonalPage::hideEvent(QHideEvent* event)
{
    if (g_personalPage == this) g_personalPage = nullptr;
    QWidget::hideEvent(event);
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
    const auto validateBytes = [](QLineEdit* edit, int maximum) {
        if (sourceByteLength(edit->text()) <= maximum) return true;
        edit->setFocus();
        edit->selectAll();
        return false;
    };
    if (sourceByteLength(nick) > MAX_NICKINPUT) {
        m_nickname->setFocus();
        m_nickname->selectAll();
        return false;
    }
    if (!validateBytes(m_realName, MAX_REALNAMEINPUT)
        || !validateBytes(m_email, MAX_EMAILINPUT)
        || !validateBytes(m_homePage, MAX_HOMEPAGEINPUT)) {
        return false;
    }
    return true;
}

void CPersonalPage::apply()
{
    m_rtfProfile.m_strText = m_rtfProfile.toPlainText();
    FreeAndNullFormatting(&m_rtfProfile.m_prgdwFormatting);
    m_rtfProfile.m_prgdwFormatting = PRGDWGetFormatting(
        &m_rtfProfile, m_rtfProfile.m_pFont,
        m_rtfProfile.m_crTextColor);
    const QByteArray plain = m_rtfProfile.m_strText.toUtf8();
    char* controlFull = m_rtfProfile.m_prgdwFormatting
        ? SzControlFull(plain.constData(),
                        m_rtfProfile.m_prgdwFormatting)
        : nullptr;
    theApp.m_myProfile = controlFull
        ? QString::fromUtf8(controlFull) : m_rtfProfile.m_strText;
    delete[] controlFull;

    const QString newNickname = nickname();
    if (newNickname.compare(
            QString::fromUtf8(GetMyName()), Qt::CaseInsensitive) != 0) {
        if (auto* protocol = dynamic_cast<CIrcProto*>(GetDefaultProto()))
            protocol->ChatSetNick(newNickname);
    }
    SetMyRealName(m_realName->text());
    SetMyEmail(m_email->text());
    SetMyHomePage(m_homePage->text());
}

CCharacterPage::CCharacterPage(QWidget* parent)
    : QWidget(parent)
    , m_avatarList(new QListWidget(this))
    , m_bodyCam(new CBodyCam(this))
    , m_copyright(new QTextEdit(this))
{
    const QString resource = QStringLiteral("IDD_CHARACTERPAGE");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = propertyPageFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setObjectName(resource);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    createPropertyPageLabel(this, resource, dialog, mapper, 0, m_avatarList);
    createPropertyPageLabel(this, resource, dialog, mapper, 1, m_bodyCam);
    placePropertyPageControl(m_avatarList, dialog, mapper,
                             QStringLiteral("IDC_AVLIST"));
    auto* previewPosition = new QWidget(this);
    placePropertyPageControl(previewPosition, dialog, mapper,
                             QStringLiteral("IDC_CHARACTER_PREVIEW"));
    m_bodyCam->setObjectName(QStringLiteral("5"));
    m_bodyCam->setGeometry(previewPosition->geometry());
    m_bodyCam->setVisible(true);
    placePropertyPageControl(m_copyright, dialog, mapper,
                             QStringLiteral("IDC_AVATAR_COPYRIGHT"));

    m_avatarList->setSortingEnabled(true);

    m_bodyCam->EnableDoubleClick(FALSE);
    m_bodyCam->m_forcedDelete = FALSE;
    m_copyright->setReadOnly(true);

    m_initialSelection = QString::fromUtf8(GetMyCharacter());
    if (GetChatDoc() && GetChatDoc()->m_bComicView && MyAvatar()) {
        m_initialSelection = QString::fromUtf8(MyAvatar()->OriginalName());
    }

    connect(m_avatarList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { selectAvatar(); });
}

CCharacterPage::~CCharacterPage()
{
    if (GetCharSelBodyCam() == m_bodyCam) SetCharSelBodyCam(nullptr);
}

void CCharacterPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    initializePage();
    SetCharSelBodyCam(m_bodyCam);
}

void CCharacterPage::hideEvent(QHideEvent* event)
{
    if (GetCharSelBodyCam() == m_bodyCam) SetCharSelBodyCam(nullptr);
    {
        const QSignalBlocker blocker(m_avatarList);
        m_avatarList->clear();
    }
    QWidget::hideEvent(event);
}

void CCharacterPage::initializePage()
{
    const QSignalBlocker blocker(m_avatarList);
    m_avatarList->clear();
    for (const QString& name : GetAllAvatarNames()) {
        auto* item = new QListWidgetItem(displayArtName(name), m_avatarList);
        item->setData(Qt::UserRole, name);
    }

    QString selected = m_selectedName.isEmpty()
        ? m_initialSelection : m_selectedName;
    CAvatarX* avatar = GetAvatar2(selected);
    if (!avatar) avatar = GetAvatar3(QStringLiteral("X"));
    if (avatar) {
        selected = QString::fromUtf8(avatar->OriginalName());
        m_selectedName = selected;
        m_bodyCam->m_avatar = avatar;
    }
    for (int index = 0; index < m_avatarList->count(); ++index) {
        QListWidgetItem* item = m_avatarList->item(index);
        if (item->data(Qt::UserRole).toString().compare(
                selected, Qt::CaseInsensitive) == 0) {
            m_avatarList->setCurrentItem(item);
            break;
        }
    }
    m_bodyCam->RefreshBody();
    updateCopyright(avatar);
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
        QString message = originalResourceString(QStringLiteral("IDS_INVALIDART"));
        message.replace(QStringLiteral("%1"), item->text());
        QMessageBox::warning(
            this,
            originalResourceString(QStringLiteral("ID_MESSAGE_BOX_TITLE")),
            message);
        if (m_bodyCam->m_avatar) {
            const QString previous = QString::fromUtf8(
                m_bodyCam->m_avatar->OriginalName());
            const QSignalBlocker blocker(m_avatarList);
            for (int index = 0; index < m_avatarList->count(); ++index) {
                QListWidgetItem* candidate = m_avatarList->item(index);
                if (candidate->data(Qt::UserRole).toString().compare(
                        previous, Qt::CaseInsensitive) == 0) {
                    m_avatarList->setCurrentItem(candidate);
                    break;
                }
            }
        }
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
    CAvatarX* modelAvatar = m_bodyCam->m_avatar;
    if (!modelAvatar || MyAvatarID() == modelAvatar->m_avatarID) return;
    if (g_puiSelf) {
        AddAndExecute(new ChangeAvatarEntry(
            g_puiSelf, modelAvatar->m_name,
            QString::fromUtf8(modelAvatar->Url() ? modelAvatar->Url() : "")),
            document);
    } else {
        SetMyAvatar(modelAvatar->m_name);
    }
}

CBackgroundPage::CBackgroundPage(QWidget* parent)
    : QWidget(parent)
    , m_backgroundList(new CBackgroundListWidget(this))
    , m_preview(new QLabel(this))
    , m_copyright(new QTextEdit(this))
{
    const QString resource = QStringLiteral("IDD_BACKGROUNDPAGE");
    const OriginalDialogResource dialog = originalDialogResource(resource);
    const QFont font = propertyPageFont(dialog);
    const DialogUnitMapper mapper(font);
    setFont(font);
    setObjectName(resource);
    setWindowTitle(dialog.caption);
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    createPropertyPageLabel(
        this, resource, dialog, mapper, 0, m_backgroundList);
    createPropertyPageLabel(this, resource, dialog, mapper, 1, m_preview);
    placePropertyPageControl(m_backgroundList, dialog, mapper,
                             QStringLiteral("IDC_BACKLIST"));

    for (const QString& identifier : {
             QStringLiteral("IDC_BACKPREV"),
             QStringLiteral("IDC_PREVX"),
             QStringLiteral("IDC_PREVY")}) {
        auto* marker = new QLabel(originalDialogControlText(
            resource, identifier), this);
        placePropertyPageControl(marker, dialog, mapper, identifier);
    }
    const OriginalDialogControl* previewStart = propertyPageControl(
        dialog, QStringLiteral("IDC_BACKPREV"));
    const OriginalDialogControl* previewRight = propertyPageControl(
        dialog, QStringLiteral("IDC_PREVX"));
    const OriginalDialogControl* previewBottom = propertyPageControl(
        dialog, QStringLiteral("IDC_PREVY"));
    if (previewStart && previewRight && previewBottom) {
        const int left = mapper.x(previewStart->x);
        const int top = mapper.y(previewStart->y);
        const int right = mapper.x(
            previewRight->x + previewRight->width);
        const int bottom = mapper.y(
            previewBottom->y + previewBottom->height);
        m_preview->setGeometry(left, top, right - left, bottom - top);
    }
    m_preview->setObjectName(QStringLiteral("BACKGROUND_PREVIEW"));
    placePropertyPageControl(m_copyright, dialog, mapper,
                             QStringLiteral("IDC_BACKGROUND_COPYRIGHT"));

    m_backgroundList->setSortingEnabled(true);
    m_preview->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_preview->setAlignment(Qt::AlignCenter);
    m_copyright->setReadOnly(true);

    m_selectedFile = QString::fromLocal8Bit(GetCurrentBackDropName());

    connect(m_backgroundList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { previewSelection(); });
}

void CBackgroundPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    initializePage();
}

void CBackgroundPage::hideEvent(QHideEvent* event)
{
    {
        const QSignalBlocker blocker(m_backgroundList);
        m_backgroundList->clear();
    }
    QWidget::hideEvent(event);
}

void CBackgroundPage::initializePage()
{
    const QSignalBlocker blocker(m_backgroundList);
    m_backgroundList->clear();
    for (const QString& fileName : OriginalBackdropNames()) {
        auto* item = new QListWidgetItem(
            displayArtName(QFileInfo(fileName).completeBaseName()),
            m_backgroundList);
        item->setData(Qt::UserRole, fileName);
    }

    QListWidgetItem* selectedItem = nullptr;
    for (int index = 0; index < m_backgroundList->count(); ++index) {
        QListWidgetItem* item = m_backgroundList->item(index);
        const QString fileName = item->data(Qt::UserRole).toString();
        if (fileName.compare(m_selectedFile, Qt::CaseInsensitive) == 0
            || QFileInfo(fileName).completeBaseName().compare(
                   m_selectedFile, Qt::CaseInsensitive) == 0) {
            selectedItem = item;
            break;
        }
    }
    m_backgroundList->setCurrentItem(selectedItem);
    if (!selectedItem) {
        m_backgroundList->clearSelection();
        m_backgroundList->selectionModel()->clearCurrentIndex();
    }
    static_cast<CBackgroundListWidget*>(m_backgroundList)
        ->preserveNoCurrentItem(!selectedItem);
    previewSelection();
}

void CBackgroundPage::previewSelection()
{
    QListWidgetItem* item = m_backgroundList->currentItem();
    if (!item) {
        m_preview->clear();
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
    const int status = currentRoom
        ? currentRoom->GetConnectionStatus() : CX_DISCONNECTED;
    if (status == CX_INCHANNEL || status == CX_DISCONNECTED) {
        AddAndExecute(new ChangeBackDropEntry(m_selectedFile));
    } else {
        const QByteArray encoded = QFile::encodeName(m_selectedFile);
        SetBackDrop(encoded.constData(), nullptr);
    }
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

class CComicFontDialog final : public QFontDialog {
public:
    CComicFontDialog(const QFont& initialFont, COLORREF initialColor,
                     QWidget* parent)
        : QFontDialog(initialFont, parent)
        , m_color(new QComboBox)
    {
        setObjectName(QStringLiteral("CComicFontDialog"));
        setOption(QFontDialog::DontUseNativeDialog, true);

        auto* colorRow = new QWidget(this);
        colorRow->setObjectName(QStringLiteral("comicFontColorRow"));
        auto* rowLayout = new QHBoxLayout(colorRow);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* label = new QLabel(originalDialogControlText(
            QStringLiteral("IDD_SETTEXTFONT"),
            QStringLiteral("1091")), colorRow);
        m_color->setParent(colorRow);
        m_color->setObjectName(QStringLiteral("1139"));
        label->setBuddy(m_color);
        rowLayout->addWidget(label);
        rowLayout->addWidget(m_color, 1);

        INT initialIndex = -1;
        for (COLORREF color : clrTable) {
            QPixmap swatch(18, 12);
            swatch.fill(QColor(GetRValue(color), GetGValue(color),
                               GetBValue(color)));
            m_color->addItem(QIcon(swatch), QString(),
                             QVariant::fromValue<quint32>(color));
            if (color == initialColor) initialIndex = m_color->count() - 1;
        }
        if (initialIndex < 0) {
            QPixmap swatch(18, 12);
            swatch.fill(QColor(GetRValue(initialColor),
                               GetGValue(initialColor),
                               GetBValue(initialColor)));
            m_color->insertItem(
                0, QIcon(swatch), QString(),
                QVariant::fromValue<quint32>(initialColor));
            initialIndex = 0;
        }
        m_color->setCurrentIndex(initialIndex);
        if (layout()) layout()->addWidget(colorRow);
    }

    COLORREF selectedColor() const
    {
        return m_color
            ? static_cast<COLORREF>(
                m_color->currentData().value<quint32>())
            : RGB(0, 0, 0);
    }

    void done(int result) override
    {
        if (result == QDialog::Accepted) {
            const QFont selected = currentFont();
            qreal pointSize = selected.pointSizeF();
            if (pointSize <= 0.0 && selected.pixelSize() > 0)
                pointSize = selected.pixelSize() / 20.0;
            if (pointSize < 8.0 || pointSize > 18.0) {
                QApplication::beep();
                return;
            }
        }
        QFontDialog::done(result);
    }

private:
    QComboBox* m_color = nullptr;
};

} // namespace

void SetComicsFont()
{
    CComicFontDialog dialog(comicDialogFont(theApp.m_comicsFont),
                            theApp.m_comicsColor,
                            QApplication::activeWindow());

    if (dialog.exec() == QDialog::Accepted) {
        QFont selected = comicLogicalFont(dialog.selectedFont());
        theApp.m_comicsColor = dialog.selectedColor();
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
    tabs->setObjectName(QStringLiteral("OptionsTabs"));
    auto* personal = new CPersonalPage(tabs);
    auto* settings = new CSettingsPage(tabs);
    CComicsPropPage* comics = comicsMode
        ? new CComicsPropPage(tabs) : nullptr;
    auto* character = new CCharacterPage(tabs);
    auto* background = new CBackgroundPage(tabs);
    CTextFontPage* textFont = comicsMode
        ? nullptr : new CTextFontPage(IDD_TEXTFONTPAGE_IRC, tabs);
    auto* servers = new CServersPage(tabs);
    tabs->addTab(personal, originalDialogCaption(
        QStringLiteral("IDD_PERSONALPAGE_IRC")));
    tabs->addTab(settings, originalDialogCaption(
        QStringLiteral("IDD_SETTINGSPAGE")));
    if (comicsMode) {
        tabs->addTab(comics, originalDialogCaption(
            QStringLiteral("IDD_COMICS_VIEW")));
        tabs->addTab(character, originalDialogCaption(
            QStringLiteral("IDD_CHARACTERPAGE")));
        tabs->addTab(background, originalDialogCaption(
            QStringLiteral("IDD_BACKGROUNDPAGE")));
    } else {
        // These lightweight page objects also exist in the original text-mode
        // stack, but without an active property-page window. Keep their Qt
        // widgets explicitly hidden so showing the parent tab control cannot
        // activate either art page.
        character->hide();
        background->hide();
        tabs->addTab(textFont, originalDialogCaption(
            QStringLiteral("IDD_TEXTFONTPAGE_IRC")));
    }
    tabs->addTab(servers, originalDialogCaption(
        QStringLiteral("IDD_SERVERSPAGE")));

    if (initialPageId == IDD_PERSONALPAGE_IRC)
        tabs->setCurrentWidget(personal);
    else if (initialPageId == IDD_SETTINGSPAGE)
        tabs->setCurrentWidget(settings);
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
            [this, tabs, personal, settings, comics, character, background,
             textFont, servers, comicsMode] {
        if (!personal->validate()) {
            tabs->setCurrentWidget(personal);
            return;
        }
        if (!settings->validate()) {
            tabs->setCurrentWidget(settings);
            return;
        }
        if (!servers->validate()) {
            tabs->setCurrentWidget(servers);
            return;
        }

        // All page validation is complete before the first externally
        // observable effect. Commit the only fallible staged model first.
        if (!servers->apply()) {
            tabs->setCurrentWidget(servers);
            return;
        }
        personal->apply();
        settings->apply();
        if (comicsMode) {
            comics->apply();
            character->apply();
            background->apply();
        } else if (textFont) {
            textFont->apply();
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
