// Ported from v2.5-beta-1-modern/txtfntdg.cpp.
// Qt replaces only the HDC used for the original twips-to-pixel conversion.

#include "txtfntdg.h"

#include "chat.h"
#include "ccommon.h"
#include "colordlg.h"
#include "format.h"
#include "originalassets.h"
#include "setupdlg.h"
#include "textcore.h"
#include "textview.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QGroupBox>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QShowEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>

class CSourceSimpleCombo final : public QWidget
{
public:
    explicit CSourceSimpleCombo(const QString& identifier,
                                QWidget* parent = nullptr)
        : QWidget(parent)
        , m_editor(new QLineEdit(this))
        , m_list(new QListWidget(this))
    {
        setObjectName(identifier);
        m_editor->setObjectName(identifier + QStringLiteral(".Edit"));
        m_list->setObjectName(identifier + QStringLiteral(".List"));
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
        m_list->setFocusPolicy(Qt::NoFocus);
        m_list->setFrameShape(QFrame::StyledPanel);
        setFocusPolicy(Qt::StrongFocus);
        setFocusProxy(m_editor);
        m_editor->installEventFilter(this);

        connect(m_list, &QListWidget::currentRowChanged, this,
                [this](int row) {
                    if (m_setting) return;
                    m_setting = true;
                    m_editor->setText(row >= 0 && row < count()
                        ? itemText(row) : QString());
                    m_setting = false;
                    if (m_selectionChanged) m_selectionChanged(row);
                });
        connect(m_editor, &QLineEdit::textEdited, this,
                [this](const QString& text) {
                    if (m_setting) return;
                    m_setting = true;
                    m_list->setCurrentRow(findText(text));
                    m_setting = false;
                });
        connect(m_editor, &QLineEdit::editingFinished, this,
                [this] {
                    if (!m_setting && m_editingFinished)
                        m_editingFinished();
                });
    }

    void addItem(const QString& text, const QVariant& data = {})
    {
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, data);
        if (count() == 1) setCurrentIndex(0);
    }

    void addItems(const QStringList& items)
    {
        for (const QString& item : items) addItem(item);
    }

    void clear()
    {
        m_setting = true;
        m_list->clear();
        m_editor->clear();
        m_setting = false;
    }

    int count() const { return m_list->count(); }

    QString itemText(int index) const
    {
        QListWidgetItem* item = index >= 0 && index < count()
            ? m_list->item(index) : nullptr;
        return item ? item->text() : QString();
    }

    QVariant itemData(int index) const
    {
        QListWidgetItem* item = index >= 0 && index < count()
            ? m_list->item(index) : nullptr;
        return item ? item->data(Qt::UserRole) : QVariant();
    }

    int findText(const QString& text) const
    {
        for (int index = 0; index < count(); ++index) {
            if (itemText(index).compare(text, Qt::CaseInsensitive) == 0)
                return index;
        }
        return -1;
    }

    int findData(const QVariant& data) const
    {
        for (int index = 0; index < count(); ++index) {
            if (itemData(index) == data) return index;
        }
        return -1;
    }

    int currentIndex() const
    {
        const int row = m_list->currentRow();
        return row >= 0
            && itemText(row).compare(m_editor->text(),
                                    Qt::CaseInsensitive) == 0
            ? row : -1;
    }

    QString currentText() const { return m_editor->text(); }
    QVariant currentData() const { return itemData(currentIndex()); }

    void setCurrentIndex(int index)
    {
        m_setting = true;
        if (index >= 0 && index < count()) {
            m_list->setCurrentRow(index);
            m_editor->setText(itemText(index));
        } else {
            m_list->setCurrentRow(-1);
            m_editor->clear();
        }
        m_setting = false;
    }

    void setSelectionChangedHandler(std::function<void(int)> handler)
    {
        m_selectionChanged = std::move(handler);
    }

    void setEditingFinishedHandler(std::function<void()> handler)
    {
        m_editingFinished = std::move(handler);
    }

    QLineEdit* editor() const { return m_editor; }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        const int editorHeight = qMin(
            height(), m_editor->fontMetrics().height() + 6);
        m_editor->setGeometry(0, 0, width(), editorHeight);
        m_list->setGeometry(0, editorHeight, width(),
                            qMax(0, height() - editorHeight));
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_editor && event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            int row = currentIndex();
            if (key->key() == Qt::Key_Down && row + 1 < count()) {
                setCurrentIndex(row + 1);
                if (m_selectionChanged) m_selectionChanged(row + 1);
                return true;
            }
            if (key->key() == Qt::Key_Up && row > 0) {
                setCurrentIndex(row - 1);
                if (m_selectionChanged) m_selectionChanged(row - 1);
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    QLineEdit* m_editor = nullptr;
    QListWidget* m_list = nullptr;
    bool m_setting = false;
    std::function<void(int)> m_selectionChanged;
    std::function<void()> m_editingFinished;
};

namespace {
class CSourceThreeStateCheckBox final : public QCheckBox
{
public:
    using QCheckBox::QCheckBox;

protected:
    void nextCheckState() override
    {
        setCheckState(checkState() == Qt::Unchecked
            ? Qt::Checked : Qt::Unchecked);
    }
};
}

BOOL bCHARFORMATToLOGFONT(CHARFORMAT* charFormat, DWORD mask, LOGFONT* font)
{
    if (!font || !charFormat) return FALSE;

    std::memset(font, 0, sizeof(LOGFONT));
    if ((charFormat->dwMask & CFM_BOLD)
        && (charFormat->dwEffects & CFE_BOLD))
        font->lfWeight = 700;
    if ((charFormat->dwMask & CFM_ITALIC)
        && (charFormat->dwEffects & CFE_ITALIC))
        font->lfItalic = TRUE;
    if ((charFormat->dwMask & CFM_UNDERLINE)
        && (charFormat->dwEffects & CFE_UNDERLINE))
        font->lfUnderline = TRUE;
    if ((charFormat->dwMask & CFM_STRIKEOUT)
        && (charFormat->dwEffects & CFE_STRIKEOUT))
        font->lfStrikeOut = TRUE;

    if (charFormat->dwMask & CFM_SIZE) {
        QScreen* screen = QGuiApplication::primaryScreen();
        const int dpi = screen
            ? qMax(1, qRound(screen->logicalDotsPerInchY())) : 96;
        font->lfHeight = charFormat->yHeight * dpi / 1440;
    }
    font->lfOutPrecision = 1;
    font->lfClipPrecision = 0x40;
    font->lfQuality = 1;
    font->lfCharSet = charFormat->bCharSet;
    font->lfPitchAndFamily = 34;
    if (charFormat->dwMask & CFM_FACE) {
        std::strncpy(font->lfFaceName, charFormat->szFaceName,
                     LF_FACESIZE - 1);
        font->lfFaceName[LF_FACESIZE - 1] = '\0';
    }
    Q_UNUSED(mask);
    return TRUE;
}

namespace {

constexpr int ALL_MESSAGETYPE = 255;
constexpr std::array<BYTE, NFONTS + 1> mesgDropdown = {
    ALL_MESSAGETYPE, 0, 1, 2, 3, 4, 5, 6, 7, 8,
    9, 10, 11, 12, 13, 14, 15, 16, 17
};

class DialogUnitMapper
{
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

QString controlText(const OriginalDialogResource& dialog,
                    const QString& identifier, int occurrence = 0)
{
    const OriginalDialogControl* control = findControl(
        dialog, identifier, occurrence);
    return control ? control->text : QString();
}

void placeControl(QWidget* widget, const OriginalDialogResource& dialog,
                  const DialogUnitMapper& mapper,
                  const QString& identifier, int occurrence = 0)
{
    if (!widget) return;
    widget->setObjectName(identifier);
    if (const OriginalDialogControl* control = findControl(
            dialog, identifier, occurrence)) {
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

QColor qColor(COLORREF color)
{
    return QColor(GetRValue(color), GetGValue(color), GetBValue(color));
}

QTextCharFormat textFormat(const CHARFORMAT& source)
{
    QTextCharFormat target;
    if (source.dwMask & CFM_FACE) {
        const char* end = std::find(source.szFaceName,
                                    source.szFaceName + LF_FACESIZE, '\0');
        if (end != source.szFaceName) {
            target.setFontFamilies({QString::fromLatin1(
                source.szFaceName,
                static_cast<qsizetype>(end - source.szFaceName))});
        }
    }
    if ((source.dwMask & CFM_SIZE) && source.yHeight > 0)
        target.setFontPointSize(source.yHeight / 20.0);
    if ((source.dwMask & CFM_COLOR)
        && !(source.dwEffects & CFE_AUTOCOLOR))
        target.setForeground(qColor(source.crTextColor));
    if (source.dwMask & CFM_BOLD) {
        target.setFontWeight((source.dwEffects & CFE_BOLD)
            ? QFont::Bold : QFont::Normal);
    }
    if (source.dwMask & CFM_ITALIC)
        target.setFontItalic(source.dwEffects & CFE_ITALIC);
    if (source.dwMask & CFM_UNDERLINE)
        target.setFontUnderline(source.dwEffects & CFE_UNDERLINE);
    if (source.dwMask & CFM_STRIKEOUT)
        target.setFontStrikeOut(source.dwEffects & CFE_STRIKEOUT);
    return target;
}

LOGFONT logFontFromQFont(const QFont& font, BYTE charSet)
{
    LOGFONT result{};
    QScreen* screen = QGuiApplication::primaryScreen();
    const int dpi = screen
        ? qMax(1, qRound(screen->logicalDotsPerInchY())) : 96;
    if (font.pointSizeF() > 0)
        result.lfHeight = qRound(font.pointSizeF() * dpi / 72.0);
    else if (font.pixelSize() > 0)
        result.lfHeight = font.pixelSize();
    result.lfWeight = font.weight();
    result.lfItalic = font.italic();
    result.lfUnderline = font.underline();
    result.lfStrikeOut = font.strikeOut();
    result.lfCharSet = charSet;
    const QByteArray family = font.family().toLatin1().left(LF_FACESIZE - 1);
    std::memcpy(result.lfFaceName, family.constData(),
                static_cast<size_t>(family.size()));
    return result;
}

int previewTextLength(const QTextEdit* preview)
{
    return preview && preview->document()
        ? qMax(0, preview->document()->characterCount() - 1) : 0;
}

int charSetForWritingSystem(QFontDatabase::WritingSystem writingSystem)
{
    switch (writingSystem) {
    case QFontDatabase::Latin: return ANSI_CHARSET;
    case QFontDatabase::Greek: return GREEK_CHARSET;
    case QFontDatabase::Cyrillic: return RUSSIAN_CHARSET;
    case QFontDatabase::Hebrew: return HEBREW_CHARSET;
    case QFontDatabase::Arabic: return ARABIC_CHARSET;
    case QFontDatabase::Thai: return THAI_CHARSET;
    case QFontDatabase::SimplifiedChinese: return GB2312_CHARSET;
    case QFontDatabase::TraditionalChinese: return CHINESEBIG5_CHARSET;
    case QFontDatabase::Japanese: return SHIFTJIS_CHARSET;
    case QFontDatabase::Korean: return HANGEUL_CHARSET;
    case QFontDatabase::Symbol: return SYMBOL_CHARSET;
    default: return -1;
    }
}

void mergeCharFormat(CHARFORMAT& target, const CHARFORMAT& source)
{
    target.cbSize = sizeof(CHARFORMAT);
    const auto mergeEffect = [&](DWORD mask, DWORD effect) {
        if (!(source.dwMask & mask)) return;
        target.dwMask |= mask;
        target.dwEffects &= ~effect;
        target.dwEffects |= source.dwEffects & effect;
    };
    mergeEffect(CFM_BOLD, CFE_BOLD);
    mergeEffect(CFM_ITALIC, CFE_ITALIC);
    mergeEffect(CFM_UNDERLINE, CFE_UNDERLINE);
    mergeEffect(CFM_STRIKEOUT, CFE_STRIKEOUT);
    if (source.dwMask & CFM_FACE) {
        target.dwMask |= CFM_FACE;
        std::memcpy(target.szFaceName, source.szFaceName, LF_FACESIZE);
    }
    if (source.dwMask & CFM_SIZE) {
        target.dwMask |= CFM_SIZE;
        target.yHeight = source.yHeight;
    }
    if (source.dwMask & CFM_OFFSET) {
        target.dwMask |= CFM_OFFSET;
        target.yOffset = source.yOffset;
    }
    if (source.dwMask & CFM_COLOR) {
        target.dwMask |= CFM_COLOR;
        target.dwEffects &= ~CFE_AUTOCOLOR;
        target.dwEffects |= source.dwEffects & CFE_AUTOCOLOR;
        target.crTextColor = source.crTextColor;
    }
    if (source.dwMask & CFM_CHARSET) {
        target.dwMask |= CFM_CHARSET;
        target.bCharSet = source.bCharSet;
        target.bPitchAndFamily = source.bPitchAndFamily;
    }
}

} // namespace

CMyFontDialog::CMyFontDialog(QWidget* parent)
    : QDialog(parent)
{
    buildResourceDialog();
}

CMyFontDialog::~CMyFontDialog()
{
    if (m_richCore) m_richCore->DetachTextViewHWnd();
}

void CMyFontDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (m_bInitialized) return;
    m_bInitialized = SetupRichPreview();
}

void CMyFontDialog::buildResourceDialog()
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_SETTEXTFONT"));
    setObjectName(QStringLiteral("IDD_SETTEXTFONT"));
    setWindowTitle(dialog.caption);
    setFont(resourceFont(dialog));
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));

    auto* messageLabel = new QLabel(controlText(
        dialog, QStringLiteral("IDC_STATIC"), 0), this);
    placeControl(messageLabel, dialog, mapper,
                 QStringLiteral("IDC_STATIC"), 0);
    m_messageType = new QComboBox(this);
    placeControl(m_messageType, dialog, mapper,
                 QStringLiteral("IDC_MESSAGETYPE"));
    messageLabel->setBuddy(m_messageType);

    const QString messageTypes = originalResourceString(
        QStringLiteral("IDS_MESSAGETYPES"))
        + originalResourceString(QStringLiteral("IDS_MESSAGETYPES2"));
    for (const QString& entry : messageTypes.split(
             QLatin1Char(';'), Qt::SkipEmptyParts))
        m_messageType->addItem(entry.trimmed());

    m_richPreview = new QTextEdit(this);
    m_richPreview->setObjectName(QStringLiteral("5"));
    m_richPreview->setReadOnly(true);
    m_richPreview->setAcceptRichText(true);
    m_richPreview->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_richPreview->setFocusPolicy(Qt::ClickFocus);
    m_richPreview->setGeometry(mapper.x(5), mapper.y(20),
                               mapper.x(251), mapper.y(120));
    m_richPreview->viewport()->setCursor(Qt::PointingHandCursor);

    auto* faceLabel = new QLabel(controlText(dialog, QStringLiteral("1088")), this);
    placeControl(faceLabel, dialog, mapper, QStringLiteral("1088"));
    m_face = new CSourceSimpleCombo(QStringLiteral("1136"), this);
    QStringList families = QFontDatabase::families();
    families.erase(std::remove_if(families.begin(), families.end(),
        [](const QString& family) {
            return family.startsWith(QLatin1Char('@'));
        }), families.end());
    m_face->addItems(families);
    placeControl(m_face, dialog, mapper, QStringLiteral("1136"));
    faceLabel->setBuddy(m_face);

    auto* styleLabel = new QLabel(controlText(dialog, QStringLiteral("1089")), this);
    placeControl(styleLabel, dialog, mapper, QStringLiteral("1089"));
    m_style = new CSourceSimpleCombo(QStringLiteral("1137"), this);
    placeControl(m_style, dialog, mapper, QStringLiteral("1137"));
    styleLabel->setBuddy(m_style);

    auto* sizeLabel = new QLabel(controlText(dialog, QStringLiteral("1090")), this);
    placeControl(sizeLabel, dialog, mapper, QStringLiteral("1090"));
    m_pointSize = new CSourceSimpleCombo(QStringLiteral("1138"), this);
    placeControl(m_pointSize, dialog, mapper, QStringLiteral("1138"));
    sizeLabel->setBuddy(m_pointSize);

    auto* effects = new QGroupBox(controlText(dialog, QStringLiteral("1072")), this);
    placeControl(effects, dialog, mapper, QStringLiteral("1072"));
    m_strikeout = new CSourceThreeStateCheckBox(controlText(
        dialog, QStringLiteral("1040")), this);
    m_strikeout->setTristate(true);
    placeControl(m_strikeout, dialog, mapper, QStringLiteral("1040"));
    m_underline = new CSourceThreeStateCheckBox(controlText(
        dialog, QStringLiteral("1041")), this);
    m_underline->setTristate(true);
    placeControl(m_underline, dialog, mapper, QStringLiteral("1041"));

    auto* colorLabel = new QLabel(controlText(dialog, QStringLiteral("1091")), this);
    placeControl(colorLabel, dialog, mapper, QStringLiteral("1091"));
    m_color = new QComboBox(this);
    for (COLORREF color : clrTable) {
        QPixmap swatch(18, 12);
        swatch.fill(qColor(color));
        m_color->addItem(QIcon(swatch), QString(),
                         QVariant::fromValue<quint32>(color));
    }
    placeControl(m_color, dialog, mapper, QStringLiteral("1139"));
    colorLabel->setBuddy(m_color);

    auto* scriptLabel = new QLabel(controlText(dialog, QStringLiteral("1094")), this);
    placeControl(scriptLabel, dialog, mapper, QStringLiteral("1094"));
    m_script = new QComboBox(this);
    placeControl(m_script, dialog, mapper, QStringLiteral("1140"));
    scriptLabel->setBuddy(m_script);

    auto* hiddenSample = new QGroupBox(controlText(
        dialog, QStringLiteral("1073")), this);
    placeControl(hiddenSample, dialog, mapper, QStringLiteral("1073"));
    auto* hiddenText = new QLabel(controlText(dialog, QStringLiteral("1093")), this);
    placeControl(hiddenText, dialog, mapper, QStringLiteral("1093"));
    auto* hiddenAngry = new QLabel(controlText(
        dialog, QStringLiteral("IDR_ANGRY")), this);
    placeControl(hiddenAngry, dialog, mapper, QStringLiteral("IDR_ANGRY"));

    auto* ok = new QPushButton(controlText(dialog, QStringLiteral("IDOK")), this);
    placeControl(ok, dialog, mapper, QStringLiteral("IDOK"));
    ok->setDefault(true);
    auto* cancel = new QPushButton(controlText(
        dialog, QStringLiteral("IDCANCEL")), this);
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    auto* apply = new QPushButton(controlText(dialog, QStringLiteral("1026")), this);
    placeControl(apply, dialog, mapper, QStringLiteral("1026"));
    apply->hide();
    auto* help = new QPushButton(controlText(dialog, QStringLiteral("1038")), this);
    placeControl(help, dialog, mapper, QStringLiteral("1038"));
    help->hide();

    connect(ok, &QPushButton::clicked, this, &CMyFontDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &CMyFontDialog::reject);
    connect(m_messageType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { OnChangeMessageType(); });
    connect(m_richPreview, &QTextEdit::selectionChanged,
            this, &CMyFontDialog::HandleSelection);
    const auto faceChanged = [this] {
        if (m_bSettingUI) return;
        populateStyles(m_face->currentText());
        populateSizes(m_face->currentText(), m_style->currentText());
        populateScripts(m_face->currentText());
        OnFontChange();
    };
    m_face->setSelectionChangedHandler(
        [faceChanged](int) { faceChanged(); });
    m_face->setEditingFinishedHandler(faceChanged);
    const auto styleChanged = [this] {
        if (m_bSettingUI) return;
        populateSizes(m_face->currentText(),
                      m_style->currentText());
        OnFontChange();
    };
    m_style->setSelectionChangedHandler(
        [styleChanged](int) { styleChanged(); });
    m_style->setEditingFinishedHandler(styleChanged);
    m_pointSize->setSelectionChangedHandler(
        [this](int) { OnFontChange(); });
    m_pointSize->setEditingFinishedHandler(
        [this] { OnFontChange(); });
    connect(m_color, qOverload<int>(&QComboBox::activated),
            this, [this](int) { OnFontChange(); });
    connect(m_script, qOverload<int>(&QComboBox::activated),
            this, [this](int) { OnFontChange(); });
    connect(m_strikeout, &QCheckBox::checkStateChanged,
            this, [this](Qt::CheckState) { OnStrikeoutChange(); });
    connect(m_underline, &QCheckBox::checkStateChanged,
            this, [this](Qt::CheckState) { OnUnderlineChange(); });

    QWidget::setTabOrder(m_messageType, m_face->editor());
    QWidget::setTabOrder(m_face->editor(), m_style->editor());
    QWidget::setTabOrder(m_style->editor(), m_pointSize->editor());
    QWidget::setTabOrder(m_pointSize->editor(), m_strikeout);
    QWidget::setTabOrder(m_strikeout, m_underline);
    QWidget::setTabOrder(m_underline, m_color);
    QWidget::setTabOrder(m_color, m_script);
    QWidget::setTabOrder(m_script, ok);
    QWidget::setTabOrder(ok, cancel);
}

BOOL CMyFontDialog::SetupRichPreview()
{
    if (!m_richPreview) return FALSE;
    m_richCore = std::make_unique<CTextCore>();
    if (!m_richCore->AttachTextViewHWnd(m_richPreview)) return FALSE;
    m_richCore->bSetTextViewBufferMaxSize(10000);
    m_richCore->dwClearTextViewBuffer(0);
    InitializeTextCore(m_richCore.get());

    int line = 0;
    const auto endLine = [&] {
        m_mesgEnds[line] = previewTextLength(m_richPreview);
        ++line;
    };
    const auto bytes = [](const QString& value) { return value.toUtf8(); };

    QString nick1 = QString::fromUtf8(GetMyName());
    if (nick1.isEmpty())
        nick1 = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
    const QString nick2 = originalResourceString(
        QStringLiteral("IDS_SAMPLE_NICK"));
    const QByteArray nick1Bytes = bytes(nick1);
    const QByteArray nick2Bytes = bytes(nick2);

    m_mesgTypes[line] = 0;
    m_richCore->iDisplayMemberStatus(nick1Bytes.constData(), 0,
                                     mtJoin, msParticipant);
    endLine();

    QString entry = originalResourceString(QStringLiteral("IDS_SAMPLE_SEND"));
    QByteArray entryBytes = bytes(entry);
    int cbLen = entryBytes.size();
    m_mesgTypes[line] = 1;
    m_richCore->iDisplayMsgHeader(cbLen, nick1Bytes.constData(), 0,
                                  "", 0, mtNormal, msParticipant);
    endLine();
    m_mesgTypes[line] = 2;
    m_richCore->iDisplayMsgText(entryBytes.constData(), 0,
                                mtNormal, msParticipant);
    endLine();

    entry = originalResourceString(QStringLiteral("IDS_SAMPLE_WHISPER"));
    entryBytes = bytes(entry);
    cbLen = entryBytes.size();
    m_mesgTypes[line] = 3;
    m_richCore->iDisplayMsgHeader(cbLen, nick1Bytes.constData(), 0,
                                  nick2Bytes.constData(), 0,
                                  mtWhisper, msParticipant);
    endLine();
    m_mesgTypes[line] = 4;
    m_richCore->iDisplayMsgText(entryBytes.constData(), 0,
                                mtWhisper, msParticipant);
    endLine();

    entry = originalResourceString(QStringLiteral("IDS_SAMPLE_THOUGHT"));
    entryBytes = bytes(entry);
    m_mesgTypes[line] = 5;
    m_richCore->iDisplayMsgHeader(cbLen, nick1Bytes.constData(), 0,
                                  "", 0, mtThought, msParticipant);
    endLine();
    m_mesgTypes[line] = 6;
    m_richCore->iDisplayMsgText(entryBytes.constData(), 0,
                                mtThought, msParticipant);
    endLine();

    entry = originalResourceString(QStringLiteral("IDS_SAMPLE_ACTION"));
    entryBytes = bytes(entry);
    m_mesgTypes[line] = 7;
    m_richCore->iDisplayAction(nick1Bytes.constData(), 0,
                               entryBytes.constData(), 0, msParticipant);
    endLine();
    m_mesgTypes[line] = 8;
    m_richCore->iDisplayInfo(nick1Bytes.constData(), 0,
                             nick2Bytes.constData(), 0,
                             nullptr, 0, mtAliasChange, msParticipant);
    endLine();
    m_mesgTypes[line] = 9;
    m_richCore->iDisplayMemberStatus(nick2Bytes.constData(), 0,
                                     mtLeave, msParticipant);
    endLine();

    for (UINT highlight = 0; highlight < NHIGHLIGHTEDFONTS / 2;
         ++highlight) {
        entry = originalResourceString(QStringLiteral("IDS_SAMPLE_HIGHLIGHT"));
        entry.replace(QStringLiteral("%d"), QString::number(highlight + 1));
        entryBytes = bytes(entry);
        cbLen = entryBytes.size();
        m_mesgTypes[line] = static_cast<BYTE>(10 + highlight * 2);
        m_richCore->iDisplayMsgHeader(cbLen, nick2Bytes.constData(), 0,
                                      "", 0, mtNormal, msParticipant,
                                      nullptr, static_cast<INT>(2 * highlight));
        endLine();
        m_mesgTypes[line] = static_cast<BYTE>(11 + highlight * 2);
        m_richCore->iDisplayMsgText(entryBytes.constData(), 0,
                                    mtNormal, msParticipant,
                                    FALSE, FALSE, FALSE, DEFAULT_INDENT,
                                    nullptr,
                                    static_cast<INT>(2 * highlight + 1));
        endLine();
    }

    m_nLines = line;
    m_mesgStarts[0] = 0;
    const int newMessageDelta =
        theApp.m_textSpacing == TEXT_VIEW_BLANK_NEVER ? 1 : 2;
    const int headerBodyDelta = (theApp.m_flags1 & F1_HEADERSEPARATE)
        ? 1 : g_nHeaderTabLen;
    for (int index = 1; index < m_nLines; ++index) {
        switch (index) {
        case 2: case 4: case 6: case 11:
        case 13: case 15: case 17:
            m_mesgStarts[index] = m_mesgEnds[index - 1] + headerBodyDelta;
            break;
        default:
            m_mesgStarts[index] = m_mesgEnds[index - 1] + newMessageDelta;
            break;
        }
        m_mesgStarts[index] = qMin(m_mesgStarts[index], m_mesgEnds[index]);
    }

    if (m_bSetFromCf) UpdateFromCf();
    else capturePreviewFormats();

    QTextCursor cursor(m_richPreview->document());
    cursor.setPosition(0);
    m_richPreview->setTextCursor(cursor);
    HandleSelection();
    return TRUE;
}

void CMyFontDialog::capturePreviewFormats()
{
    for (int line = 0; line < m_nLines; ++line) {
        const int type = m_mesgTypes[line];
        const BOOL initialized = type < NREGULARFONTS
            ? theApp.m_bCfInitialized : theApp.m_bCfHLInitialized;
        if (initialized) {
            m_cfArray[type] = theApp.m_cfArray[type];
            continue;
        }
        const int start = qBound(0, m_mesgStarts[line],
                                 previewTextLength(m_richPreview));
        QTextCursor cursor(m_richPreview->document());
        cursor.setPosition(start);
        if (start < previewTextLength(m_richPreview))
            cursor.movePosition(QTextCursor::NextCharacter,
                                QTextCursor::KeepAnchor);
        const QTextCharFormat format = cursor.charFormat();
        const QFont font = format.font();
        LOGFONT logFont = logFontFromQFont(font, theApp.m_charSet);
        QColor color = format.foreground().color();
        if (!color.isValid()) color = Qt::black;
        CHARFORMAT captured{};
        bLOGFONTToCHARFORMAT(&logFont,
            RGB(static_cast<BYTE>(color.red()),
                static_cast<BYTE>(color.green()),
                static_cast<BYTE>(color.blue())), 0, &captured);
        if (font.pointSizeF() > 0) captured.yHeight = qRound(
            font.pointSizeF() * 20.0);
        m_cfArray[type] = captured;
    }
}

void CMyFontDialog::UpdateFromCf()
{
    if (!m_richPreview) return;
    m_bClosing = TRUE;
    const QTextCursor old = m_richPreview->textCursor();
    for (int line = 0; line < m_nLines; ++line) {
        QTextCursor cursor(m_richPreview->document());
        cursor.setPosition(qBound(0, m_mesgStarts[line],
                                  previewTextLength(m_richPreview)));
        cursor.setPosition(qBound(0, m_mesgEnds[line],
                                  previewTextLength(m_richPreview)),
                           QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(textFormat(m_cfArray[m_mesgTypes[line]]));
    }
    m_richPreview->setTextCursor(old);
    m_bClosing = FALSE;
}

int CMyFontDialog::GetMessageType() const
{
    return m_messageType ? m_messageType->currentIndex() : -1;
}

void CMyFontDialog::SetMessageType(int messageType)
{
    if (!m_messageType) return;
    int entry = -1;
    for (int index = 0; index < static_cast<int>(mesgDropdown.size()); ++index) {
        if (messageType == mesgDropdown[index]) {
            entry = index;
            break;
        }
    }
    m_bSettingUI = TRUE;
    m_messageType->setCurrentIndex(entry);
    m_bSettingUI = FALSE;
}

void CMyFontDialog::OnChangeMessageType()
{
    if (m_bSettingUI || !m_messageType || !m_richPreview) return;
    const int position = m_messageType->currentIndex();
    if (position < 0) return;
    int first = 0;
    int last = m_nLines - 1;
    if (position > 0) {
        const int type = mesgDropdown[position];
        first = -1;
        for (int line = 0; line < m_nLines; ++line) {
            if (m_mesgTypes[line] == type) {
                first = last = line;
                break;
            }
        }
        if (first < 0) return;
    }
    m_bClosing = TRUE;
    QTextCursor cursor(m_richPreview->document());
    cursor.setPosition(m_mesgStarts[first]);
    cursor.setPosition(m_mesgEnds[last], QTextCursor::KeepAnchor);
    m_richPreview->setTextCursor(cursor);
    m_bClosing = FALSE;
    syncFontControls();
}

void CMyFontDialog::HandleSelection()
{
    if (m_bClosing || !m_richPreview) return;
    const QTextCursor cursor = m_richPreview->textCursor();
    const int minimum = cursor.selectionStart();
    const int maximum = cursor.selectionEnd();
    int lower = -1;
    int upper = -1;
    for (int line = 0; line < m_nLines; ++line) {
        if (maximum >= m_mesgStarts[line] && maximum <= m_mesgEnds[line])
            upper = line;
        if (minimum >= m_mesgStarts[line] && minimum <= m_mesgEnds[line])
            lower = line;
    }
    if (lower >= 0 && upper >= 0) {
        m_bClosing = TRUE;
        QTextCursor atomized(m_richPreview->document());
        atomized.setPosition(m_mesgStarts[lower]);
        atomized.setPosition(m_mesgEnds[upper], QTextCursor::KeepAnchor);
        m_richPreview->setTextCursor(atomized);
        m_bClosing = FALSE;
    }
    syncFontControls();
    syncMessageType();
}

std::array<bool, NFONTS> CMyFontDialog::selectedTypes() const
{
    std::array<bool, NFONTS> selected{};
    if (!m_richPreview) return selected;
    const QTextCursor cursor = m_richPreview->textCursor();
    const int minimum = cursor.selectionStart();
    const int maximum = cursor.selectionEnd();
    int lower = -1;
    int upper = -1;
    for (int line = 0; line < m_nLines; ++line) {
        if (maximum >= m_mesgStarts[line] && maximum <= m_mesgEnds[line])
            upper = line;
        if (minimum >= m_mesgStarts[line] && minimum <= m_mesgEnds[line])
            lower = line;
    }
    if (lower >= 0 && upper >= lower) {
        for (int line = lower; line <= upper; ++line)
            selected[m_mesgTypes[line]] = true;
        return selected;
    }
    const int position = m_messageType ? m_messageType->currentIndex() : -1;
    if (position == 0) selected.fill(true);
    else if (position > 0) selected[mesgDropdown[position]] = true;
    return selected;
}

void CMyFontDialog::syncMessageType()
{
    if (!m_richPreview) return;
    const std::array<bool, NFONTS> selected = selectedTypes();
    int count = 0;
    int type = -1;
    for (int index = 0; index < NFONTS; ++index) {
        if (!selected[index]) continue;
        ++count;
        type = index;
    }
    if (count == NFONTS) SetMessageType(ALL_MESSAGETYPE);
    else if (count == 1) SetMessageType(type);
    else SetMessageType(-1);
}

void CMyFontDialog::populateStyles(const QString& family)
{
    const bool old = m_bSettingUI;
    m_bSettingUI = TRUE;
    m_style->clear();
    if (!family.isEmpty()) m_style->addItems(QFontDatabase::styles(family));
    m_bSettingUI = old;
}

void CMyFontDialog::populateSizes(const QString& family, const QString& style,
                                  int selectedPointSize)
{
    const bool old = m_bSettingUI;
    m_bSettingUI = TRUE;
    m_pointSize->clear();
    QList<int> sizes = QFontDatabase::pointSizes(family, style);
    if (sizes.isEmpty()) sizes = QFontDatabase::standardSizes();
    if (selectedPointSize > 0 && !sizes.contains(selectedPointSize))
        sizes.append(selectedPointSize);
    std::sort(sizes.begin(), sizes.end());
    for (int size : std::as_const(sizes))
        m_pointSize->addItem(QString::number(size), size);
    if (selectedPointSize > 0) {
        const int index = m_pointSize->findData(selectedPointSize);
        m_pointSize->setCurrentIndex(index);
    }
    m_bSettingUI = old;
}

void CMyFontDialog::populateScripts(const QString& family, int selectedCharSet)
{
    const bool old = m_bSettingUI;
    m_bSettingUI = TRUE;
    m_script->clear();
    QSet<int> added;
    for (QFontDatabase::WritingSystem writingSystem
         : QFontDatabase::writingSystems(family)) {
        const int charSet = charSetForWritingSystem(writingSystem);
        if (charSet < 0 || added.contains(charSet)) continue;
        m_script->addItem(QFontDatabase::writingSystemName(writingSystem),
                          charSet);
        added.insert(charSet);
    }
    m_script->setCurrentIndex(selectedCharSet >= 0
        ? m_script->findData(selectedCharSet) : -1);
    m_bSettingUI = old;
}

void CMyFontDialog::syncFontControls()
{
    const std::array<bool, NFONTS> selected = selectedTypes();
    int first = -1;
    for (int index = 0; index < NFONTS; ++index) {
        if (selected[index]) { first = index; break; }
    }
    if (first < 0) return;

    const CHARFORMAT& base = m_cfArray[first];
    const auto consistent = [&](DWORD mask, const auto& equal) {
        if (!(base.dwMask & mask)) return false;
        for (int index = first + 1; index < NFONTS; ++index) {
            if (!selected[index]) continue;
            if (!(m_cfArray[index].dwMask & mask)
                || !equal(base, m_cfArray[index])) return false;
        }
        return true;
    };
    const bool faceConsistent = consistent(CFM_FACE,
        [](const CHARFORMAT& a, const CHARFORMAT& b) {
            return std::strncmp(a.szFaceName, b.szFaceName, LF_FACESIZE) == 0;
        });
    const bool sizeConsistent = consistent(CFM_SIZE,
        [](const CHARFORMAT& a, const CHARFORMAT& b) {
            return a.yHeight == b.yHeight;
        });
    const bool colorConsistent = consistent(CFM_COLOR,
        [](const CHARFORMAT& a, const CHARFORMAT& b) {
            return a.crTextColor == b.crTextColor
                && (a.dwEffects & CFE_AUTOCOLOR)
                    == (b.dwEffects & CFE_AUTOCOLOR);
        });
    const bool boldConsistent = consistent(CFM_BOLD,
        [](const CHARFORMAT& a, const CHARFORMAT& b) {
            return (a.dwEffects & CFE_BOLD) == (b.dwEffects & CFE_BOLD);
        });
    const bool italicConsistent = consistent(CFM_ITALIC,
        [](const CHARFORMAT& a, const CHARFORMAT& b) {
            return (a.dwEffects & CFE_ITALIC) == (b.dwEffects & CFE_ITALIC);
        });
    const bool strikeConsistent = consistent(CFM_STRIKEOUT,
        [](const CHARFORMAT& a, const CHARFORMAT& b) {
            return (a.dwEffects & CFE_STRIKEOUT)
                == (b.dwEffects & CFE_STRIKEOUT);
        });
    const bool underlineConsistent = consistent(CFM_UNDERLINE,
        [](const CHARFORMAT& a, const CHARFORMAT& b) {
            return (a.dwEffects & CFE_UNDERLINE)
                == (b.dwEffects & CFE_UNDERLINE);
        });
    const bool scriptConsistent = consistent(CFM_CHARSET,
        [](const CHARFORMAT& a, const CHARFORMAT& b) {
            return a.bCharSet == b.bCharSet;
        });

    m_bSettingUI = TRUE;
    if (faceConsistent) {
        const QString family = QString::fromLatin1(base.szFaceName);
        int familyIndex = -1;
        for (int index = 0; index < m_face->count(); ++index) {
            if (m_face->itemText(index).compare(
                    family, Qt::CaseInsensitive) == 0) {
                familyIndex = index;
                break;
            }
        }
        m_face->setCurrentIndex(familyIndex);
    } else {
        m_face->setCurrentIndex(-1);
    }
    const QString family = faceConsistent
        ? QString::fromLatin1(base.szFaceName) : QString();
    populateStyles(family);
    if (faceConsistent && boldConsistent && italicConsistent) {
        const bool bold = base.dwEffects & CFE_BOLD;
        const bool italic = base.dwEffects & CFE_ITALIC;
        int styleIndex = -1;
        for (int index = 0; index < m_style->count(); ++index) {
            const QFont candidate = QFontDatabase::font(
                family, m_style->itemText(index), 10);
            if ((candidate.weight() >= QFont::Bold) == bold
                && candidate.italic() == italic) {
                styleIndex = index;
                break;
            }
        }
        m_style->setCurrentIndex(styleIndex);
    } else {
        m_style->setCurrentIndex(-1);
    }
    const int pointSize = sizeConsistent
        ? qRound(base.yHeight / 20.0) : -1;
    populateSizes(family, m_style->currentText(), pointSize);
    if (!sizeConsistent) m_pointSize->setCurrentIndex(-1);

    if (colorConsistent && !(base.dwEffects & CFE_AUTOCOLOR)) {
        m_color->setCurrentIndex(m_color->findData(
            QVariant::fromValue<quint32>(base.crTextColor)));
    } else {
        m_color->setCurrentIndex(-1);
    }
    populateScripts(family, scriptConsistent ? base.bCharSet : -1);
    m_strikeout->setCheckState(!strikeConsistent
        ? Qt::PartiallyChecked
        : ((base.dwEffects & CFE_STRIKEOUT)
            ? Qt::Checked : Qt::Unchecked));
    m_underline->setCheckState(!underlineConsistent
        ? Qt::PartiallyChecked
        : ((base.dwEffects & CFE_UNDERLINE)
            ? Qt::Checked : Qt::Unchecked));
    m_bSettingUI = FALSE;
}

DWORD CMyFontDialog::GetCharFormatMask() const
{
    DWORD mask = 0;
    if (m_face && m_face->currentIndex() >= 0) mask |= CFM_FACE;
    if (m_style && m_style->currentIndex() >= 0)
        mask |= CFM_ITALIC | CFM_BOLD;
    if (m_pointSize && m_pointSize->currentIndex() >= 0) mask |= CFM_SIZE;
    if (m_color && m_color->currentIndex() >= 0) mask |= CFM_COLOR;
    if (m_script && m_script->currentIndex() >= 0) mask |= CFM_CHARSET;
    if (m_strikeout
        && m_strikeout->checkState() != Qt::PartiallyChecked)
        mask |= CFM_STRIKEOUT;
    if (m_underline
        && m_underline->checkState() != Qt::PartiallyChecked)
        mask |= CFM_UNDERLINE;
    return mask;
}

void CMyFontDialog::applyCharFormat(const CHARFORMAT& format)
{
    const std::array<bool, NFONTS> selected = selectedTypes();
    for (int index = 0; index < NFONTS; ++index) {
        if (selected[index]) mergeCharFormat(m_cfArray[index], format);
    }
}

void CMyFontDialog::OnFontChange()
{
    if (!m_bFontCtlEvents || m_bSettingUI) return;
    const DWORD mask = GetCharFormatMask();
    if (!mask) return;

    QString family;
    if (m_face->currentIndex() >= 0)
        family = m_face->currentText();
    QFont font = family.isEmpty() ? QFont() : QFont(family);
    int pointSize = 0;
    if (m_pointSize->currentIndex() >= 0)
        pointSize = m_pointSize->currentData().toInt();
    if (pointSize > 0) font.setPointSize(pointSize);
    if (m_style->currentIndex() >= 0) {
        const QFont styled = QFontDatabase::font(
            family, m_style->currentText(), pointSize > 0 ? pointSize : 10);
        font.setWeight(styled.weight());
        font.setItalic(styled.italic());
    }
    font.setStrikeOut(m_strikeout->checkState() == Qt::Checked);
    font.setUnderline(m_underline->checkState() == Qt::Checked);
    const BYTE charSet = m_script->currentIndex() >= 0
        ? static_cast<BYTE>(m_script->currentData().toInt())
        : theApp.m_charSet;
    LOGFONT logFont = logFontFromQFont(font, charSet);
    const COLORREF color = m_color->currentIndex() >= 0
        ? static_cast<COLORREF>(m_color->currentData().toUInt())
        : RGB(0, 0, 0);
    CHARFORMAT charFormat{};
    if (!bLOGFONTToCHARFORMAT(&logFont, color, mask, &charFormat)) return;
    if ((mask & CFM_SIZE) && pointSize > 0)
        charFormat.yHeight = pointSize * 20;
    applyCharFormat(charFormat);
    UpdateFromCf();
    syncFontControls();
}

void CMyFontDialog::OnStrikeoutChange()
{
    OnFontChange();
}

void CMyFontDialog::OnUnderlineChange()
{
    OnFontChange();
}

void CMyFontDialog::accept()
{
    m_bClosing = TRUE;
    QDialog::accept();
}
