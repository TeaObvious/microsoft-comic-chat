//=--------------------------------------------------------------------------=
// AutoPage.cpp -- Qt port of v2.5-beta-1-modern/autopage.cpp
//=--------------------------------------------------------------------------=

#include "autopage.h"

#include "actions.h"
#include "chatdoc.h"
#include "chatsrv.h"
#include "format.h"
#include "mainfrm.h"
#include "originalassets.h"
#include "protsupp.h"
#include "resource.h"
#include "userinfo.h"
#include "utils.h"
#include "whisprbx.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QPixmap>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShowEvent>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTextCursor>
#include <QVariant>

#include <algorithm>
#include <cstring>

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

void createStaticControls(QWidget* parent,
                          const OriginalDialogResource& dialog,
                          const DialogUnitMapper& mapper)
{
    int staticIndex = 0;
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != QLatin1String("IDC_STATIC")) continue;
        QWidget* widget = nullptr;
        if (control.type == QLatin1String("GROUPBOX")) {
            widget = new QGroupBox(control.text, parent);
        } else if (control.type == QLatin1String("LTEXT")
                   || control.type == QLatin1String("CTEXT")
                   || control.type == QLatin1String("RTEXT")) {
            auto* label = new QLabel(control.text, parent);
            if (control.type == QLatin1String("CTEXT"))
                label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
            else if (control.type == QLatin1String("RTEXT"))
                label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            else if (control.style.contains(QStringLiteral("SS_CENTERIMAGE")))
                label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            label->setWordWrap(control.height >= 16);
            widget = label;
        }
        if (!widget) continue;
        widget->setObjectName(QStringLiteral("IDC_STATIC_%1").arg(staticIndex++));
        widget->setGeometry(mapper.rect(control));
        widget->setVisible(control.visible);
    }
}

QIcon bitmapIcon(const QString& resourceIdentifier, const QColor& mask,
                 int imageIndex = 0, int imageWidth = 16,
                 int imageHeight = 16)
{
    QPixmap strip(originalFileResourcePath(resourceIdentifier,
                                            QStringLiteral("BITMAP")));
    if (strip.isNull()) return {};
    const int left = imageIndex * imageWidth;
    if (left >= strip.width()) return {};
    QPixmap image = strip.copy(left, 0,
                               qMin(imageWidth, strip.width() - left),
                               qMin(imageHeight, strip.height()));
    image.setMask(image.createMaskFromColor(mask, Qt::MaskInColor));
    return QIcon(image);
}

template<typename T>
T* pointerFromVariant(const QVariant& value)
{
    return reinterpret_cast<T*>(value.value<quintptr>());
}

QVariant pointerVariant(const void* pointer)
{
    return QVariant::fromValue(
        reinterpret_cast<quintptr>(const_cast<void*>(pointer)));
}

QString controlFullText(const CRtfCtrl* control)
{
    if (!control) return {};
    const QString plain = control->toPlainText();
    CDWordArray* formatting = PRGDWGetFormatting(
        control, control->m_pFont, control->m_crTextColor);
    QString result = plain;
    const QByteArray utf8 = plain.toUtf8();
    if (formatting) {
        if (char* encoded = SzControlFull(utf8.constData(), formatting)) {
            result = QString::fromUtf8(encoded);
            delete[] encoded;
        }
    }
    FreeAndNullFormatting(&formatting);
    return result;
}

void setControlFullText(CRtfCtrl* control, const QString& controlFull)
{
    if (!control) return;
    QByteArray bytes = controlFull.toUtf8();
    CDWordArray formatting;
    char* plain = SzControlLess(bytes.data(), &formatting);
    control->UseDefaultCharFormat();
    control->bSetTextColor(control->m_crTextColor);
    control->bSetWindowFormattedText(QString::fromUtf8(plain), &formatting);
    control->moveCursor(QTextCursor::Start);
}

QString trimmedRuleSetName(const QString& value)
{
    return value.trimmed();
}

bool invalidRuleSetName(const QString& value)
{
    static const QRegularExpression invalid(QStringLiteral("[/\\\\:*?\"<>|]"));
    return value.contains(invalid);
}

int findRuleSetByName(const CRuleSetsListBox* list, const QString& name)
{
    if (!list) return -1;
    for (int index = 0; index < list->count(); ++index) {
        if (list->item(index)->text() == name) return index;
    }
    return -1;
}

QString ruleSetFileFilter()
{
    QString filter = originalResourceString(QStringLiteral("IDS_CRS_FILTER"));
    QStringList fields = filter.split(QLatin1Char('|'));
    if (!fields.isEmpty() && fields.back().isEmpty()) fields.removeLast();
    if (!fields.isEmpty() && fields.back().isEmpty()) fields.removeLast();
    QStringList qtFilters;
    for (int index = 0; index + 1 < fields.size(); index += 2)
        qtFilters.append(fields[index] + QStringLiteral(" (")
                         + fields[index + 1] + QLatin1Char(')'));
    return qtFilters.join(QStringLiteral(";;"));
}

void showResourceMessage(QWidget* parent, const QString& resourceIdentifier)
{
    QMessageBox::information(parent,
        originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
        originalResourceString(resourceIdentifier));
}

void applyRulesCopy(CCDynaRules* source)
{
    if (!source) return;
    theApp.m_dynaRules = *source;
    if (theApp.m_dynaRules.bDaemonNeeded())
        theApp.m_dynaRules.bStartRulesDaemon(g_uRulesDaemonShortElapse, TRUE);
    else
        theApp.m_dynaRules.bStopRulesDaemon();
    theApp.m_dynaRules.bUpdateRuleSetsDaemonExt(FALSE);
}

void AddTextFileToComboBox(qintptr context, const QString& path,
                           const QString& fileName, int)
{
    auto* combo = reinterpret_cast<QComboBox*>(context);
    if (!combo) return;

    const QStringList reserved = originalResourceString(
        QStringLiteral("IDS_TEXTFILES")).split(
            QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString reservedName : reserved) {
        reservedName.remove(QLatin1Char('\r'));
        if (fileName.compare(reservedName, Qt::CaseInsensitive) == 0)
            return;
    }

    QString relativePath = QDir(theApp.m_strBaseDir).relativeFilePath(path);
    if (relativePath == QLatin1String(".")) relativePath.clear();
    relativePath.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (!relativePath.isEmpty()) relativePath += QLatin1Char('\\');
    combo->addItem(relativePath + fileName + QString::fromLatin1(szTxtExt));
}
}

/////////////////////////////////////////////////////////////////////////////
// CMacro

INT CMacro::Serialize(char* buffer, INT bufferLength) const
{
    if (!buffer || bufferLength <= 0) return 0;
    const QByteArray name = m_strName.toUtf8();
    const QByteArray value = m_strValue.toUtf8();
    // setupdlg.cpp stores macros as REG_MULTI_SZ: both strings are
    // NUL-terminated and the complete value has one additional terminal NUL.
    const INT total = name.size() + value.size() + 3;
    if (bufferLength < total) return 0;
    std::memcpy(buffer, name.constData(), name.size());
    buffer[name.size()] = '\0';
    std::memcpy(buffer + name.size() + 1, value.constData(), value.size());
    buffer[total - 2] = '\0';
    buffer[total - 1] = '\0';
    return total;
}

void CMacro::UnSerialize(const char* buffer)
{
    if (!buffer) return;
    m_bDefined = TRUE;
    m_strName = QString::fromUtf8(buffer);
    m_strValue = QString::fromUtf8(buffer + std::strlen(buffer) + 1);
}

void CMacro::Invoke(const QString* encodedChannelName, CUserInfo* pui,
                    BOOL invokedByRule, BOOL inWhisperBox)
{
    if (!m_bDefined) return;
    QString controlFull = m_strValue;
    CChatDoc* document = encodedChannelName
        ? LookupDoc(*encodedChannelName) : nullptr;
    if (!ExpandVariables(controlFull, document, pui, invokedByRule)) return;

    QByteArray bytes = controlFull.toUtf8();
    CDWordArray formatting;
    char* controlLess = SzControlLess(bytes.data(), &formatting);
    const QByteArray plain(controlLess);
    if (invokedByRule) g_rgpuiWhisperees.clear();

    int lineStart = 0;
    while (lineStart <= plain.size()) {
        int lineEnd = plain.indexOf('\n', lineStart);
        const bool finalLine = lineEnd < 0;
        if (finalLine) lineEnd = plain.size();
        int contentEnd = lineEnd;
        if (contentEnd > lineStart && plain[contentEnd - 1] == '\r')
            --contentEnd;
        if (contentEnd > lineStart) {
            const QByteArray lineBytes = plain.mid(lineStart,
                                                   contentEnd - lineStart);
            const QString line = QString::fromUtf8(lineBytes);
            QString prepared = line;
            ChatPreSendText(prepared);
            CDWordArray* lineFormatting = PullFormattingOffsets(
                &formatting, static_cast<SHORT>(lineStart));
            lineFormatting = CutFormattingArray(
                lineFormatting, static_cast<SHORT>(lineBytes.size()));
            if (inWhisperBox) {
                bWhisperInBox(QString(), line, lineFormatting, BM_WHISPER);
            } else {
                bChatSendText(line, BM_SAY, TRUE, lineFormatting,
                              encodedChannelName, invokedByRule);
            }
            FreeAndNullFormatting(&lineFormatting);
        }
        if (finalLine) break;
        lineStart = lineEnd + 1;
    }
}

static void CopyMacros(const CMacro source[], CMacro destination[])
{
    for (INT index = 0; index < NMACROS; ++index)
        destination[index] = source[index];
}

/////////////////////////////////////////////////////////////////////////////
// CAutomationPage

CAutomationPage::CAutomationPage(QWidget* parent)
    : QWidget(parent)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_AUTOMATION_PAGE"));
    setObjectName(QStringLiteral("IDD_AUTOMATION_PAGE"));
    setFont(resourceFont(dialog));
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);

    auto makeGroup = [this, &dialog, &mapper](const QString& id) {
        const OriginalDialogControl* source = findControl(dialog, id);
        auto* group = new QGroupBox(source ? source->text : QString(), this);
        placeControl(group, dialog, mapper, id);
        group->lower();
    };
    makeGroup(QStringLiteral("IDC_GROUP0"));
    makeGroup(QStringLiteral("IDC_GROUP1"));
    makeGroup(QStringLiteral("IDC_GROUP2"));

    m_noGreeting = new QRadioButton(controlText(
        dialog, QStringLiteral("IDC_NOGREETING")), this);
    placeControl(m_noGreeting, dialog, mapper, QStringLiteral("IDC_NOGREETING"));
    m_whisperGreeting = new QRadioButton(controlText(
        dialog, QStringLiteral("IDC_WHISPERGREETING")), this);
    placeControl(m_whisperGreeting, dialog, mapper,
                 QStringLiteral("IDC_WHISPERGREETING"));
    m_sayGreeting = new QRadioButton(controlText(
        dialog, QStringLiteral("IDC_SAYGREETING")), this);
    placeControl(m_sayGreeting, dialog, mapper,
                 QStringLiteral("IDC_SAYGREETING"));

    m_rtfGreetingMesg = new CRtfCtrl(this);
    placeControl(m_rtfGreetingMesg, dialog, mapper,
                 QStringLiteral("IDC_GREETINGMESG"));
    m_rtfGreetingMesg->m_crTextColor = RGB(0, 0, 0);
    m_rtfGreetingMesg->DefineDefaultCharFormat();

    m_autoIgnore = new QCheckBox(controlText(
        dialog, QStringLiteral("IDC_AUTOIGNORE")), this);
    placeControl(m_autoIgnore, dialog, mapper, QStringLiteral("IDC_AUTOIGNORE"));
    m_mesgCntCtl = new QLineEdit(this);
    placeControl(m_mesgCntCtl, dialog, mapper, QStringLiteral("IDC_MESGCOUNT"));
    m_mesgCntCtl->setMaxLength(3);
    m_mesgCntCtl->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,3}")), m_mesgCntCtl));
    m_spinMesgCnt = new QSpinBox(this);
    placeControl(m_spinMesgCnt, dialog, mapper,
                 QStringLiteral("IDC_MESGCOUNTSPIN"));
    m_spinMesgCnt->setRange(1, 255);
    m_spinMesgCnt->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_intervalCtl = new QLineEdit(this);
    placeControl(m_intervalCtl, dialog, mapper, QStringLiteral("IDC_INTERVAL"));
    m_intervalCtl->setMaxLength(3);
    m_intervalCtl->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,3}")), m_intervalCtl));
    m_spinInterval = new QSpinBox(this);
    placeControl(m_spinInterval, dialog, mapper,
                 QStringLiteral("IDC_INTERVALSPIN"));
    m_spinInterval->setRange(1, 255);
    m_spinInterval->setButtonSymbols(QAbstractSpinBox::UpDownArrows);

    m_keyCtl = new QComboBox(this);
    placeControl(m_keyCtl, dialog, mapper, QStringLiteral("IDC_KEY"));
    const QStringList keys = originalDialogInitStrings(
        QStringLiteral("IDD_AUTOMATION_PAGE"), QStringLiteral("IDC_KEY"));
    m_keyCtl->addItems(keys);
    m_macroNameCtl = new QLineEdit(this);
    placeControl(m_macroNameCtl, dialog, mapper,
                 QStringLiteral("IDC_MACRONAME"));
    m_macroNameCtl->setMaxLength(20);
    m_rtfMacro = new CRtfCtrl(this);
    placeControl(m_rtfMacro, dialog, mapper,
                 QStringLiteral("IDC_MACRORICHEDIT"));
    m_rtfMacro->m_crTextColor = RGB(0, 0, 0);
    m_rtfMacro->DefineDefaultCharFormat();
    m_addMacro = new QPushButton(controlText(
        dialog, QStringLiteral("IDC_ADD_MACRO")), this);
    placeControl(m_addMacro, dialog, mapper, QStringLiteral("IDC_ADD_MACRO"));
    m_deleteMacro = new QPushButton(controlText(
        dialog, QStringLiteral("IDC_DELETE_MACRO")), this);
    placeControl(m_deleteMacro, dialog, mapper,
                 QStringLiteral("IDC_DELETE_MACRO"));

    m_iGreetingType = theApp.m_iGreetingType;
    m_bAutoIgnore = (theApp.m_uFloodFlags & FLOOD_IGNORE) != 0;
    m_uMesgCnt = theApp.m_uFloodCount;
    m_uInterval = theApp.m_uFloodInterval;
    m_strMesgCnt = QString::number(m_uMesgCnt);
    m_strInterval = QString::number(m_uInterval);

    connect(m_noGreeting, &QRadioButton::clicked,
            this, [this] { OnNogreeting(); });
    connect(m_whisperGreeting, &QRadioButton::clicked,
            this, [this] { OnWhispergreeting(); });
    connect(m_sayGreeting, &QRadioButton::clicked,
            this, [this] { OnSaygreeting(); });
    connect(m_rtfGreetingMesg, &QTextEdit::textChanged,
            this, [this] { LimitRichText(m_rtfGreetingMesg); OnChangeGreetingMesg(); });
    connect(m_rtfMacro, &QTextEdit::textChanged,
            this, [this] { LimitRichText(m_rtfMacro); });
    connect(m_autoIgnore, &QCheckBox::clicked,
            this, [this] { OnAutoIgnore(); });
    connect(m_mesgCntCtl, &QLineEdit::textChanged,
            this, [this] { OnMesgCountChg(); });
    connect(m_intervalCtl, &QLineEdit::textChanged,
            this, [this] { OnIntervalChg(); });
    connect(m_spinMesgCnt, &QSpinBox::valueChanged,
            this, [this](int value) {
                if (!m_bUpdating) m_mesgCntCtl->setText(QString::number(value));
            });
    connect(m_spinInterval, &QSpinBox::valueChanged,
            this, [this](int value) {
                if (!m_bUpdating) m_intervalCtl->setText(QString::number(value));
            });
    connect(m_keyCtl, &QComboBox::currentIndexChanged,
            this, [this](int) { OnSelchangeKey(); });
    connect(m_addMacro, &QPushButton::clicked,
            this, [this] { OnAddMacro(); });
    connect(m_deleteMacro, &QPushButton::clicked,
            this, [this] { OnDeleteMacro(); });

    OnSetActive();
}

void CAutomationPage::LimitRichText(CRtfCtrl* control)
{
    if (m_bUpdating || !control || control->toPlainText().size() <= MAX_INPUTLEN)
        return;
    m_bUpdating = TRUE;
    QTextCursor cursor(control->document());
    cursor.setPosition(MAX_INPUTLEN);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    m_bUpdating = FALSE;
}

BOOL CAutomationPage::OnSetActive()
{
    if (!m_bSetActiveNeverCalled) return TRUE;
    m_bUpdating = TRUE;
    CopyMacros(theApp.m_macros, m_macros);
    setControlFullText(m_rtfGreetingMesg, theApp.m_strGreetingMesg);
    m_noGreeting->setChecked(m_iGreetingType == AGT_NONE);
    m_whisperGreeting->setChecked(m_iGreetingType == AGT_WHISPER);
    m_sayGreeting->setChecked(m_iGreetingType == AGT_SAY);
    m_rtfGreetingMesg->setEnabled(m_iGreetingType != AGT_NONE);
    m_autoIgnore->setChecked(m_bAutoIgnore);
    m_mesgCntCtl->setText(m_strMesgCnt);
    m_intervalCtl->setText(m_strInterval);
    m_spinMesgCnt->setValue(static_cast<int>(m_uMesgCnt));
    m_spinInterval->setValue(static_cast<int>(m_uInterval));
    m_mesgCntCtl->setEnabled(m_bAutoIgnore);
    m_intervalCtl->setEnabled(m_bAutoIgnore);
    m_spinMesgCnt->setEnabled(m_bAutoIgnore);
    m_spinInterval->setEnabled(m_bAutoIgnore);
    if (m_keyCtl->count() > 0) m_keyCtl->setCurrentIndex(0);
    m_bUpdating = FALSE;
    OnSelchangeKey();
    m_bSetActiveNeverCalled = FALSE;
    m_bModified = FALSE;
    return TRUE;
}

void CAutomationPage::OnOK()
{
    m_bOKing = TRUE;
    theApp.m_strGreetingMesg = controlFullText(m_rtfGreetingMesg);
    theApp.m_iGreetingType = m_iGreetingType;
    CopyMacros(m_macros, theApp.m_macros);
    if (CChatDoc* document = GetChatDoc()) document->UpdateMacroMenu();
    theApp.m_uFloodCount = static_cast<UCHAR>(m_uMesgCnt);
    theApp.m_uFloodInterval = static_cast<UCHAR>(m_uInterval);
    if (m_bAutoIgnore) theApp.m_uFloodFlags |= FLOOD_IGNORE;
    else theApp.m_uFloodFlags &= ~UCHAR(FLOOD_IGNORE);
    m_bModified = FALSE;
}

BOOL CAutomationPage::OnKillActive()
{
    m_bOKing = TRUE;
    return TRUE;
}

void CAutomationPage::OnChangeGreetingMesg()
{
    if (m_bUpdating) return;
    if (m_bOKing) m_bOKing = FALSE;
    else SetModified(TRUE);
}

void CAutomationPage::OnNogreeting()
{
    m_iGreetingType = AGT_NONE;
    m_rtfGreetingMesg->setEnabled(false);
    SetModified(TRUE);
}

void CAutomationPage::OnSaygreeting()
{
    m_iGreetingType = AGT_SAY;
    m_rtfGreetingMesg->setEnabled(true);
    SetModified(TRUE);
}

void CAutomationPage::OnWhispergreeting()
{
    m_iGreetingType = AGT_WHISPER;
    m_rtfGreetingMesg->setEnabled(true);
    SetModified(TRUE);
}

void CAutomationPage::OnAddMacro()
{
    const INT index = m_keyCtl->currentIndex();
    const QString name = m_macroNameCtl->text();
    const QString controlFull = controlFullText(m_rtfMacro);
    QString trimmedName = name;
    while (!trimmedName.isEmpty() && trimmedName.front().isSpace())
        trimmedName.remove(0, 1);
    QString trimmedValue = controlFull;
    while (!trimmedValue.isEmpty() && trimmedValue.front().isSpace())
        trimmedValue.remove(0, 1);
    if (index < 0 || trimmedName.isEmpty() || trimmedValue.isEmpty()) {
        showResourceMessage(this, QStringLiteral("IDS_EMPTYMACRO"));
        return;
    }
    m_macros[index].m_strName = name;
    m_macros[index].m_strValue = controlFull;
    m_macros[index].m_bDefined = TRUE;
    m_deleteMacro->setEnabled(true);
    SetModified(TRUE);
}

void CAutomationPage::OnDeleteMacro()
{
    const INT index = m_keyCtl->currentIndex();
    if (index < 0) return;
    m_macros[index] = CMacro{};
    m_bUpdating = TRUE;
    m_macroNameCtl->clear();
    m_rtfMacro->clear();
    m_bUpdating = FALSE;
    m_deleteMacro->setEnabled(false);
    SetModified(TRUE);
}

void CAutomationPage::OnSelchangeKey()
{
    const INT index = m_keyCtl->currentIndex();
    if (index < 0) return;
    m_bUpdating = TRUE;
    m_macroNameCtl->setText(m_macros[index].m_strName);
    setControlFullText(m_rtfMacro, m_macros[index].m_strValue);
    m_deleteMacro->setEnabled(m_macros[index].m_bDefined);
    m_bUpdating = FALSE;
}

void CAutomationPage::OnAutoIgnore()
{
    m_bAutoIgnore = m_autoIgnore->isChecked();
    m_mesgCntCtl->setEnabled(m_bAutoIgnore);
    m_intervalCtl->setEnabled(m_bAutoIgnore);
    m_spinMesgCnt->setEnabled(m_bAutoIgnore);
    m_spinInterval->setEnabled(m_bAutoIgnore);
    SetModified(TRUE);
}

void CAutomationPage::OnMesgCountChg()
{
    if (m_bUpdating) return;
    bool valid = false;
    const UINT value = m_mesgCntCtl->text().toUInt(&valid);
    if (!valid || value < 1 || value > 255) {
        m_bUpdating = TRUE;
        m_mesgCntCtl->setText(m_strMesgCnt);
        m_mesgCntCtl->selectAll();
        m_bUpdating = FALSE;
    } else {
        m_uMesgCnt = value;
        m_strMesgCnt = m_mesgCntCtl->text();
        m_bUpdating = TRUE;
        m_spinMesgCnt->setValue(static_cast<int>(value));
        m_bUpdating = FALSE;
    }
    SetModified(TRUE);
}

void CAutomationPage::OnIntervalChg()
{
    if (m_bUpdating) return;
    bool valid = false;
    const UINT value = m_intervalCtl->text().toUInt(&valid);
    if (!valid || value < 1 || value > 255) {
        m_bUpdating = TRUE;
        m_intervalCtl->setText(m_strInterval);
        m_intervalCtl->selectAll();
        m_bUpdating = FALSE;
    } else {
        m_uInterval = value;
        m_strInterval = m_intervalCtl->text();
        m_bUpdating = TRUE;
        m_spinInterval->setValue(static_cast<int>(value));
        m_bUpdating = FALSE;
    }
    SetModified(TRUE);
}

/////////////////////////////////////////////////////////////////////////////
// Rule activation icons and list controls

CRuleIcons::CRuleIcons()
{
    m_icons[g_nInactiveIndex] = bitmapIcon(
        QStringLiteral("IDB_INACTIVE"), QColor(0, 0, 255), 0, 16, 15);
    m_icons[g_nActiveIndex] = bitmapIcon(
        QStringLiteral("IDB_ACTIVE"), QColor(0, 0, 255), 0, 16, 15);
}

QIcon CRuleIcons::GetIcon(SHORT index) const
{
    return index >= 0 && index < g_nIconCount ? m_icons[index] : QIcon{};
}

CRuleSetsListBox::CRuleSetsListBox(QWidget* parent)
    : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::SingleSelection);
    setIconSize(QSize(g_nIconWidth, g_nIconHeight));
    setUniformItemSizes(true);
}

void CRuleSetsListBox::AddRuleSet(CCRuleSet* ruleSet, INT index)
{
    if (!ruleSet) return;
    auto* item = new QListWidgetItem(ruleSet->GetName());
    item->setData(Qt::UserRole, pointerVariant(ruleSet));
    item->setIcon(m_icons.GetIcon(ruleSet->bActive()
                                  ? g_nActiveIndex : g_nInactiveIndex));
    if (index < 0 || index >= count()) addItem(item);
    else insertItem(index, item);
}

CCRuleSet* CRuleSetsListBox::RuleSetAt(INT index) const
{
    return index >= 0 && index < count()
        ? pointerFromVariant<CCRuleSet>(item(index)->data(Qt::UserRole))
        : nullptr;
}

void CRuleSetsListBox::UpdateItem(INT index)
{
    if (CCRuleSet* ruleSet = RuleSetAt(index)) {
        item(index)->setText(ruleSet->GetName());
        item(index)->setIcon(m_icons.GetIcon(ruleSet->bActive()
                                      ? g_nActiveIndex : g_nInactiveIndex));
    }
}

void CRuleSetsListBox::SwitchActivation(INT index)
{
    CCRuleSet* ruleSet = RuleSetAt(index);
    if (!ruleSet) return;
    if (ruleSet->bActive()) ruleSet->Desactivate();
    else ruleSet->Activate();
    UpdateItem(index);
    if (auto* page = dynamic_cast<CRuleSetsPage*>(parentWidget()))
        page->SetModified(TRUE);
}

void CRuleSetsListBox::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Space && currentRow() >= 0) {
        SwitchActivation(currentRow());
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}

void CRuleSetsListBox::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (QListWidgetItem* clicked = itemAt(event->pos())) {
            const QRect rect = visualItemRect(clicked);
            if (event->position().x() >= rect.left() + g_nIconMargin
                && event->position().x()
                    < rect.left() + 2 * g_nIconMargin + g_nIconWidth - 1) {
                SwitchActivation(row(clicked));
            }
        }
    }
    QListWidget::mousePressEvent(event);
}

CRulesListCtrl::CRulesListCtrl(QWidget* parent)
    : QTreeWidget(parent)
{
    setColumnCount(2);
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setIconSize(QSize(16, 15));
    header()->setSectionsClickable(false);
    m_statusIcons[0] = bitmapIcon(QStringLiteral("IDB_INACTIVE"),
                                  QColor(0, 0, 255), 0, 16, 15);
    m_statusIcons[1] = bitmapIcon(QStringLiteral("IDB_ACTIVE"),
                                  QColor(0, 0, 255), 0, 16, 15);
    m_statusIcons[2] = bitmapIcon(QStringLiteral("IDB_STOPPED"),
                                  QColor(0, 0, 128), 0, 16, 15);
}

CCRule* CRulesListCtrl::RuleAt(INT index) const
{
    return index >= 0 && index < topLevelItemCount()
        ? pointerFromVariant<CCRule>(topLevelItem(index)->data(0, Qt::UserRole))
        : nullptr;
}

void CRulesListCtrl::UpdateItem(QTreeWidgetItem* item, CCRule* rule)
{
    if (!item || !rule) return;
    item->setData(0, Qt::UserRole, pointerVariant(rule));
    item->setText(0, rule->StrGetEventDisplay());
    item->setText(1, rule->StrGetActionDisplay());
    const int icon = rule->bActive() ? (rule->bStopped() ? 2 : 1) : 0;
    item->setIcon(0, m_statusIcons[icon]);
}

BOOL CRulesListCtrl::bAddRule(CCRule* rule, INT index)
{
    if (!rule) return FALSE;
    auto* item = new QTreeWidgetItem;
    UpdateItem(item, rule);
    if (index < 0 || index >= topLevelItemCount()) addTopLevelItem(item);
    else insertTopLevelItem(index, item);
    return TRUE;
}

BOOL CRulesListCtrl::bFill(CCDynaRules* dynaRules)
{
    clear();
    m_pRuleSet = dynaRules ? dynaRules->GetSelectedRuleSet() : nullptr;
    if (!m_pRuleSet) return TRUE;
    for (CCRule* rule : m_pRuleSet->GetRulesArray())
        if (!bAddRule(rule)) return FALSE;
    return TRUE;
}

INT CRulesListCtrl::iGetSelectedRule(CCRule** rule) const
{
    if (rule) *rule = nullptr;
    const int index = indexOfTopLevelItem(currentItem());
    if (index < 0 || !currentItem() || !currentItem()->isSelected()) return -1;
    if (rule) *rule = RuleAt(index);
    return index;
}

void CRulesListCtrl::SwitchActivation(INT index)
{
    CCRule* rule = RuleAt(index);
    if (!rule) return;
    if (rule->bStopped()) {
        rule->SetFlags(rule->wGetFlags() & ~g_wStopped);
    } else if (rule->bActive()) {
        rule->Desactivate();
    } else {
        rule->Activate();
    }
    UpdateItem(topLevelItem(index), rule);
    if (auto* page = dynamic_cast<CRulesPage*>(parentWidget()))
        page->SetModified(TRUE);
}

void CRulesListCtrl::keyPressEvent(QKeyEvent* event)
{
    const INT index = iGetSelectedRule();
    if (event->key() == Qt::Key_Space && index >= 0) {
        SwitchActivation(index);
        event->accept();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

void CRulesListCtrl::mousePressEvent(QMouseEvent* event)
{
    QTreeWidgetItem* clicked = itemAt(event->pos());
    if (clicked && event->button() == Qt::LeftButton) {
        const QRect rect = visualItemRect(clicked);
        if (event->position().x() >= rect.left()
            && event->position().x() <= rect.left() + iconSize().width() + 4) {
            SwitchActivation(indexOfTopLevelItem(clicked));
        }
    }
    QTreeWidget::mousePressEvent(event);
}

/////////////////////////////////////////////////////////////////////////////
// Advanced dialogs

CAdvancedEventParams::CAdvancedEventParams(WORD flags, QWidget* parent)
    : QDialog(parent)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_ADVANCEDEVENTPARAMS"));
    setObjectName(QStringLiteral("IDD_ADVANCEDEVENTPARAMS"));
    setFont(resourceFont(dialog));
    setWindowTitle(dialog.caption);
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);

    m_matchCase = new QCheckBox(controlText(
        dialog, QStringLiteral("IDC_CHKMATCHCASE")), this);
    placeControl(m_matchCase, dialog, mapper,
                 QStringLiteral("IDC_CHKMATCHCASE"));
    m_matchWord = new QCheckBox(controlText(
        dialog, QStringLiteral("IDC_CHKMATCHWORD")), this);
    placeControl(m_matchWord, dialog, mapper,
                 QStringLiteral("IDC_CHKMATCHWORD"));
    m_matchCase->setChecked(flags & g_wMatchCase);
    m_matchWord->setChecked(flags & g_wMatchWord);

    auto* ok = new QPushButton(controlText(dialog, QStringLiteral("IDOK")), this);
    ok->setDefault(true);
    placeControl(ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(controlText(
        dialog, QStringLiteral("IDCANCEL")), this);
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    connect(ok, &QPushButton::clicked, this, &CAdvancedEventParams::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void CAdvancedEventParams::accept()
{
    m_iMatchCase = m_matchCase->isChecked() ? 1 : 0;
    m_iMatchWord = m_matchWord->isChecked() ? 1 : 0;
    QDialog::accept();
}

CAdvancedRuleSettings::CAdvancedRuleSettings(
    UCHAR occurrences, UCHAR interval, QWidget* parent)
    : QDialog(parent)
    , m_uOcc(occurrences)
    , m_uInt(interval)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_ADVANCEDRULESETTINGS"));
    setObjectName(QStringLiteral("IDD_ADVANCEDRULESETTINGS"));
    setFont(resourceFont(dialog));
    setWindowTitle(dialog.caption);
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);

    // Qt's QSpinBox replaces each original EDITTEXT/up-down buddy pair while
    // retaining the edit's resource rectangle plus the adjacent arrow width.
    const OriginalDialogControl* occEdit = findControl(
        dialog, QStringLiteral("IDC_RULEOCC"));
    const OriginalDialogControl* occSpin = findControl(
        dialog, QStringLiteral("IDC_RULEOCCSPIN"));
    m_spinOcc = new QSpinBox(this);
    m_spinOcc->setObjectName(QStringLiteral("IDC_RULEOCC"));
    m_spinOcc->setRange(1, 255);
    m_spinOcc->setValue(m_uOcc);
    if (occEdit && occSpin) {
        QRect geometry = mapper.rect(*occEdit);
        geometry.setRight(mapper.rect(*occSpin).right());
        m_spinOcc->setGeometry(geometry);
    }
    const OriginalDialogControl* intEdit = findControl(
        dialog, QStringLiteral("IDC_RULEINT"));
    const OriginalDialogControl* intSpin = findControl(
        dialog, QStringLiteral("IDC_RULEINTSPIN"));
    m_spinInt = new QSpinBox(this);
    m_spinInt->setObjectName(QStringLiteral("IDC_RULEINT"));
    m_spinInt->setRange(1, 255);
    m_spinInt->setValue(m_uInt);
    if (intEdit && intSpin) {
        QRect geometry = mapper.rect(*intEdit);
        geometry.setRight(mapper.rect(*intSpin).right());
        m_spinInt->setGeometry(geometry);
    }
    auto* ok = new QPushButton(controlText(dialog, QStringLiteral("IDOK")), this);
    ok->setDefault(true);
    placeControl(ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(controlText(
        dialog, QStringLiteral("IDCANCEL")), this);
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    connect(ok, &QPushButton::clicked, this, &CAdvancedRuleSettings::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void CAdvancedRuleSettings::accept()
{
    m_uOcc = static_cast<UCHAR>(m_spinOcc->value());
    m_uInt = static_cast<UCHAR>(m_spinInt->value());
    QDialog::accept();
}

CSoundComboBox::CSoundComboBox(QWidget* parent)
    : QComboBox(parent)
{
    setEditable(true);
}

/////////////////////////////////////////////////////////////////////////////
// Rule-set helper dialogs

CAddToSets::CAddToSets(CCDynaRules* dynaCopy, CCRuleSet* ruleSet,
                       CCRule* rule, QWidget* parent)
    : QDialog(parent)
    , m_pDynaCopy(dynaCopy)
    , m_pRuleSet(ruleSet)
    , m_pRule(rule)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_ADDTOSETS"));
    setObjectName(QStringLiteral("IDD_ADDTOSETS"));
    setFont(resourceFont(dialog));
    setWindowTitle(dialog.caption);
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);

    m_lstRule = new QTreeWidget(this);
    placeControl(m_lstRule, dialog, mapper, QStringLiteral("IDC_ALSTRULES"));
    m_lstRule->setRootIsDecorated(false);
    m_lstRule->setItemsExpandable(false);
    m_lstRule->setColumnCount(2);
    m_lstRule->setHeaderLabels({
        originalResourceString(QStringLiteral("IDS_EVENTS_LABEL")),
        originalResourceString(QStringLiteral("IDS_ACTIONS_LABEL"))});
    m_lstRule->setColumnWidth(0, originalResourceString(
        QStringLiteral("IDS_EVENTS_WIDTH")).toInt());
    m_lstRule->setColumnWidth(1, originalResourceString(
        QStringLiteral("IDS_ACTIONS_WIDTH")).toInt());
    if (m_pRule) {
        auto* item = new QTreeWidgetItem;
        item->setText(0, m_pRule->StrGetEventDisplay());
        item->setText(1, m_pRule->StrGetActionDisplay());
        m_lstRule->addTopLevelItem(item);
    }

    m_lstSets = new QListWidget(this);
    placeControl(m_lstSets, dialog, mapper, QStringLiteral("IDC_ALSTSETS"));
    m_lstSets->setSelectionMode(QAbstractItemView::MultiSelection);
    bFillRuleSets();

    auto* ok = new QPushButton(controlText(dialog, QStringLiteral("IDOK")), this);
    ok->setDefault(true);
    placeControl(ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(controlText(
        dialog, QStringLiteral("IDCANCEL")), this);
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    connect(ok, &QPushButton::clicked, this, &CAddToSets::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_lstSets->setFocus();
}

BOOL CAddToSets::bFillRuleSets()
{
    if (!m_pDynaCopy) return FALSE;
    for (CCRuleSet* set : m_pDynaCopy->GetRuleSetsArray()) {
        if (!set || set == m_pRuleSet) continue;
        auto* item = new QListWidgetItem(set->GetName(), m_lstSets);
        item->setData(Qt::UserRole, pointerVariant(set));
    }
    return TRUE;
}

BOOL CAddToSets::bAddToSets()
{
    if (!m_pRule) return FALSE;
    BOOL added = FALSE;
    for (QListWidgetItem* item : m_lstSets->selectedItems()) {
        CCRuleSet* set = pointerFromVariant<CCRuleSet>(
            item->data(Qt::UserRole));
        if (!set) continue;
        auto* copy = new CCRule(m_pRule, set->GetDynaRules());
        if (set->bAddRule(copy)) added = TRUE;
        else copy->Release();
    }
    return added;
}

void CAddToSets::accept()
{
    m_bRuleAdded = bAddToSets();
    QDialog::accept();
}

CSetNameConflict::CSetNameConflict(const QString& setName,
                                   CRuleSetsListBox* sets,
                                   QWidget* parent)
    : QDialog(parent)
    , m_strSetName(setName)
    , m_plstSets(sets)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_RENAMELOADEDSET"));
    setObjectName(QStringLiteral("IDD_RENAMELOADEDSET"));
    setFont(resourceFont(dialog));
    setWindowTitle(dialog.caption);
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);

    QString conflict = originalResourceString(
        QStringLiteral("IDS_ERR_CRS_CONFLICT"));
    conflict.replace(QStringLiteral("%s"), m_strSetName);
    auto* conflictLabel = new QLabel(conflict, this);
    conflictLabel->setWordWrap(true);
    placeControl(conflictLabel, dialog, mapper,
                 QStringLiteral("IDC_RULESETCONFLICT"));
    m_editSetName = new QLineEdit(m_strSetName, this);
    m_editSetName->setMaxLength(g_uMaxSetNameLength);
    m_editSetName->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[^/\\\\:*?\"<>|]{0,19}")),
        m_editSetName));
    placeControl(m_editSetName, dialog, mapper,
                 QStringLiteral("IDC_RENAMEDRULESET"));

    auto* overwrite = new QPushButton(controlText(
        dialog, QStringLiteral("IDC_BTNOVERWRITERULESET")), this);
    placeControl(overwrite, dialog, mapper,
                 QStringLiteral("IDC_BTNOVERWRITERULESET"));
    m_rename = new QPushButton(controlText(
        dialog, QStringLiteral("IDC_BTNRENAMELOADEDRULESET")), this);
    m_rename->setDefault(true);
    placeControl(m_rename, dialog, mapper,
                 QStringLiteral("IDC_BTNRENAMELOADEDRULESET"));
    auto* cancel = new QPushButton(controlText(
        dialog, QStringLiteral("IDCANCEL")), this);
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    connect(overwrite, &QPushButton::clicked,
            this, [this] { OnOverwriteRuleSetClick(); });
    connect(m_rename, &QPushButton::clicked,
            this, [this] { OnRenameRuleSetClick(); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_editSetName, &QLineEdit::textChanged,
            this, [this] { OnRenamedRuleSetChanged(); });
    OnRenamedRuleSetChanged();
}

void CSetNameConflict::OnOverwriteRuleSetClick()
{
    done(IDOVERWRITE);
}

void CSetNameConflict::OnRenameRuleSetClick()
{
    const QString name = trimmedRuleSetName(m_editSetName->text());
    if (name.isEmpty() || invalidRuleSetName(name)) return;
    if (findRuleSetByName(m_plstSets, name) >= 0) {
        QString conflict = originalResourceString(
            QStringLiteral("IDS_ERR_CRS_CONFLICT2"));
        conflict.replace(QStringLiteral("%s"), name);
        QMessageBox::information(this,
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
            conflict);
        return;
    }
    m_strSetName = name;
    done(IDRENAME);
}

void CSetNameConflict::OnRenamedRuleSetChanged()
{
    const QString name = trimmedRuleSetName(m_editSetName->text());
    m_rename->setEnabled(!name.isEmpty() && !invalidRuleSetName(name));
}

CCreateSet::CCreateSet(CRuleSetsListBox* sets, QWidget* parent)
    : QDialog(parent)
    , m_plstSets(sets)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_CREATESET"));
    setObjectName(QStringLiteral("IDD_CREATESET"));
    setFont(resourceFont(dialog));
    setWindowTitle(dialog.caption);
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);
    m_editSetName = new QLineEdit(this);
    m_editSetName->setMaxLength(g_uMaxSetNameLength);
    m_editSetName->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[^/\\\\:*?\"<>|]{0,19}")),
        m_editSetName));
    placeControl(m_editSetName, dialog, mapper, QStringLiteral("IDC_CREATEDSET"));
    m_ok = new QPushButton(controlText(dialog, QStringLiteral("IDOK")), this);
    m_ok->setDefault(true);
    placeControl(m_ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(controlText(
        dialog, QStringLiteral("IDCANCEL")), this);
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    connect(m_ok, &QPushButton::clicked, this, &CCreateSet::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_editSetName, &QLineEdit::textChanged,
            this, [this] { OnCreatedRuleSetChanged(); });
    OnCreatedRuleSetChanged();
}

void CCreateSet::OnCreatedRuleSetChanged()
{
    const QString name = trimmedRuleSetName(m_editSetName->text());
    m_ok->setEnabled(!name.isEmpty() && !invalidRuleSetName(name));
}

void CCreateSet::accept()
{
    const QString name = trimmedRuleSetName(m_editSetName->text());
    if (name.isEmpty() || invalidRuleSetName(name)) return;
    if (findRuleSetByName(m_plstSets, name) >= 0) {
        QString conflict = originalResourceString(
            QStringLiteral("IDS_ERR_CRS_CONFLICT2"));
        conflict.replace(QStringLiteral("%s"), name);
        QMessageBox::information(this,
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
            conflict);
        m_editSetName->setFocus();
        return;
    }
    m_strSetName = name;
    m_editSetName->setText(name);
    QDialog::accept();
}

CRenameSet::CRenameSet(CCRuleSet* ruleSet, CRuleSetsListBox* sets,
                       QWidget* parent)
    : QDialog(parent)
    , m_plstSets(sets)
    , m_pRuleSet(ruleSet)
    , m_strSetName(ruleSet ? ruleSet->GetName() : QString())
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_RENAMESET"));
    setObjectName(QStringLiteral("IDD_RENAMESET"));
    setFont(resourceFont(dialog));
    setWindowTitle(dialog.caption);
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);
    m_editSetName = new QLineEdit(m_strSetName, this);
    m_editSetName->setMaxLength(g_uMaxSetNameLength);
    m_editSetName->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[^/\\\\:*?\"<>|]{0,19}")),
        m_editSetName));
    placeControl(m_editSetName, dialog, mapper, QStringLiteral("IDC_RENAMEDSET"));
    m_ok = new QPushButton(controlText(dialog, QStringLiteral("IDOK")), this);
    m_ok->setDefault(true);
    placeControl(m_ok, dialog, mapper, QStringLiteral("IDOK"));
    auto* cancel = new QPushButton(controlText(
        dialog, QStringLiteral("IDCANCEL")), this);
    placeControl(cancel, dialog, mapper, QStringLiteral("IDCANCEL"));
    connect(m_ok, &QPushButton::clicked, this, &CRenameSet::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_editSetName, &QLineEdit::textChanged,
            this, [this] { OnRenamedRuleSetChanged(); });
    OnRenamedRuleSetChanged();
}

void CRenameSet::OnRenamedRuleSetChanged()
{
    const QString name = trimmedRuleSetName(m_editSetName->text());
    m_ok->setEnabled(!name.isEmpty() && !invalidRuleSetName(name));
}

void CRenameSet::accept()
{
    const QString name = trimmedRuleSetName(m_editSetName->text());
    if (name.isEmpty() || invalidRuleSetName(name) || !m_pRuleSet) return;
    const int found = findRuleSetByName(m_plstSets, name);
    if (found >= 0
        && QString::compare(name, m_strSetName,
                            Qt::CaseInsensitive) != 0) {
        QString conflict = originalResourceString(
            QStringLiteral("IDS_ERR_CRS_CONFLICT2"));
        conflict.replace(QStringLiteral("%s"), name);
        QMessageBox::information(this,
            originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
            conflict);
        m_editSetName->setFocus();
        return;
    }
    if (name != m_strSetName) {
        m_pRuleSet->SetName(name);
        done(IDRENAME);
    } else {
        QDialog::accept();
    }
}

/////////////////////////////////////////////////////////////////////////////
// CRuleSetsPage

CRuleSetsPage::CRuleSetsPage(QWidget* parent)
    : QWidget(parent)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_RULESETSPAGE"));
    setObjectName(QStringLiteral("IDD_RULESETSPAGE"));
    setFont(resourceFont(dialog));
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);

    m_lstSets = new CRuleSetsListBox(this);
    placeControl(m_lstSets, dialog, mapper, QStringLiteral("IDC_LSTRULESETS"));
    auto makeButton = [this, &dialog, &mapper](const QString& id) {
        auto* button = new QPushButton(controlText(dialog, id), this);
        placeControl(button, dialog, mapper, id);
        return button;
    };
    m_create = makeButton(QStringLiteral("IDC_CREATERULESET"));
    m_rename = makeButton(QStringLiteral("IDC_RENAMERULESET"));
    m_delete = makeButton(QStringLiteral("IDC_DELETERULESET"));
    m_moveUp = makeButton(QStringLiteral("IDC_MOVERULESETUP"));
    m_moveDown = makeButton(QStringLiteral("IDC_MOVERULESETDOWN"));
    m_save = makeButton(QStringLiteral("IDC_BTNSAVESET"));
    m_load = makeButton(QStringLiteral("IDC_BTNLOADSET"));

    connect(m_create, &QPushButton::clicked,
            this, [this] { OnCreateRuleSet(); });
    connect(m_rename, &QPushButton::clicked,
            this, [this] { OnRenameRuleSet(); });
    connect(m_delete, &QPushButton::clicked,
            this, [this] { OnDeleteRuleSet(); });
    connect(m_moveUp, &QPushButton::clicked,
            this, [this] { OnMoveUpRuleSet(); });
    connect(m_moveDown, &QPushButton::clicked,
            this, [this] { OnMoveDownRuleSet(); });
    connect(m_load, &QPushButton::clicked,
            this, [this] { OnLoadRuleSet(); });
    connect(m_save, &QPushButton::clicked,
            this, [this] { OnSaveRuleSet(); });
    connect(m_lstSets, &QListWidget::currentRowChanged,
            this, [this](int) { OnRuleSetItemChanged(); });
    UpdateButtonsStatus();
}

void CRuleSetsPage::SetDynaRules(CCDynaRules* dynaCopy)
{
    m_pDynaCopy = dynaCopy;
    if (m_pDynaCopy) OnSetActive();
}

BOOL CRuleSetsPage::bFillRuleSets()
{
    if (!m_pDynaCopy) return FALSE;
    m_lstSets->clear();
    INT selected = -1;
    const QVector<CCRuleSet*>& sets = m_pDynaCopy->GetRuleSetsArray();
    for (INT index = 0; index < sets.size(); ++index) {
        CCRuleSet* set = sets[index];
        if (!set) return FALSE;
        m_lstSets->AddRuleSet(set);
        if ((index == 0 && !m_pDynaCopy->GetSelectedRuleSet())
            || set == m_pDynaCopy->GetSelectedRuleSet()) {
            selected = index;
        }
    }
    if (selected >= 0) SetCurRuleSet(sets[selected], selected);
    else m_pDynaCopy->SetSelectedRuleSet(nullptr);
    return TRUE;
}

void CRuleSetsPage::SetCurRuleSet(CCRuleSet* ruleSet, INT index)
{
    if (!m_pDynaCopy || index < 0 || index >= m_lstSets->count()) return;
    m_lstSets->setCurrentRow(index);
    m_pDynaCopy->SetSelectedRuleSet(
        ruleSet ? ruleSet : m_lstSets->RuleSetAt(index));
}

void CRuleSetsPage::UpdateButtonsStatus()
{
    const BOOL selected = m_pDynaCopy
        && m_pDynaCopy->GetSelectedRuleSet();
    const INT index = m_lstSets ? m_lstSets->currentRow() : -1;
    m_rename->setEnabled(selected);
    m_delete->setEnabled(selected);
    m_save->setEnabled(selected);
    m_moveUp->setEnabled(selected && index > 0);
    m_moveDown->setEnabled(selected && index >= 0
                           && index < m_lstSets->count() - 1);
}

BOOL CRuleSetsPage::OnSetActive()
{
    if (!m_pDynaCopy) return FALSE;
    const BOOL result = bFillRuleSets();
    UpdateButtonsStatus();
    m_bModified = FALSE;
    return result;
}

void CRuleSetsPage::OnOK()
{
    applyRulesCopy(m_pDynaCopy);
    m_bModified = FALSE;
}

void CRuleSetsPage::OnCreateRuleSet()
{
    if (!m_pDynaCopy) return;
    CCreateSet dialog(m_lstSets, this);
    if (dialog.exec() != QDialog::Accepted) {
        m_create->setFocus();
        return;
    }
    auto* set = new CCRuleSet(m_pDynaCopy);
    set->SetName(dialog.m_strSetName);
    set->Activate();
    m_pDynaCopy->bAddRuleSet(set);
    m_lstSets->AddRuleSet(set);
    SetCurRuleSet(set, m_lstSets->count() - 1);
    SetModified(TRUE);
    m_lstSets->setFocus();
    UpdateButtonsStatus();
}

void CRuleSetsPage::OnRenameRuleSet()
{
    if (!m_pDynaCopy) return;
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    const INT index = m_lstSets->currentRow();
    if (!set || index < 0 || m_lstSets->RuleSetAt(index) != set) return;
    CRenameSet dialog(set, m_lstSets, this);
    if (dialog.exec() == IDRENAME) {
        m_lstSets->item(index)->setText(set->GetName());
        SetCurRuleSet(set, index);
        SetModified(TRUE);
    }
}

void CRuleSetsPage::OnDeleteRuleSet()
{
    if (!m_pDynaCopy) return;
    const INT index = m_lstSets->currentRow();
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    if (!set || index < 0 || m_lstSets->RuleSetAt(index) != set) return;
    delete m_lstSets->takeItem(index);
    m_pDynaCopy->bRemoveRuleSet(nullptr, index);
    if (m_lstSets->count() == 0) {
        m_pDynaCopy->SetSelectedRuleSet(nullptr);
        m_create->setFocus();
    } else {
        const INT next = index < m_lstSets->count() ? index : index - 1;
        SetCurRuleSet(nullptr, next);
    }
    UpdateButtonsStatus();
    SetModified(TRUE);
}

void CRuleSetsPage::OnMoveUpRuleSet()
{
    if (!m_pDynaCopy) return;
    const INT index = m_lstSets->currentRow();
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    if (!set || index < 1 || m_lstSets->RuleSetAt(index) != set) return;
    m_pDynaCopy->bUpRuleSet(nullptr, index);
    QListWidgetItem* item = m_lstSets->takeItem(index);
    m_lstSets->insertItem(index - 1, item);
    SetCurRuleSet(set, index - 1);
    UpdateButtonsStatus();
    (index == 1 ? m_moveDown : m_moveUp)->setFocus();
    SetModified(TRUE);
}

void CRuleSetsPage::OnMoveDownRuleSet()
{
    if (!m_pDynaCopy) return;
    const INT index = m_lstSets->currentRow();
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    if (!set || index < 0 || index >= m_lstSets->count() - 1
        || m_lstSets->RuleSetAt(index) != set) return;
    m_pDynaCopy->bDownRuleSet(nullptr, index);
    QListWidgetItem* item = m_lstSets->takeItem(index);
    m_lstSets->insertItem(index + 1, item);
    SetCurRuleSet(set, index + 1);
    UpdateButtonsStatus();
    (index == m_lstSets->count() - 2 ? m_moveUp : m_moveDown)->setFocus();
    SetModified(TRUE);
}

void CRuleSetsPage::AddLoadedRuleSet(CCRuleSet* set)
{
    if (!set || !m_pDynaCopy) return;
    m_pDynaCopy->bAddRuleSet(set);
    m_lstSets->AddRuleSet(set);
    SetCurRuleSet(set, m_lstSets->count() - 1);
    SetModified(TRUE);
    UpdateButtonsStatus();
}

void CRuleSetsPage::OnLoadRuleSet()
{
    if (!m_pDynaCopy) return;
    const QString fileName = QFileDialog::getOpenFileName(
        this, QString(), QString(), ruleSetFileFilter());
    if (fileName.isEmpty()) return;
    auto* loaded = new CCRuleSet(m_pDynaCopy);
    UINT error = 0;
    if (!loaded->bLoadFromFile(fileName, &error)) {
        QString errorResource;
        if (error == g_uErrVersion) errorResource = QStringLiteral("IDS_ERR_CRS_VERSION");
        else if (error == g_uErrFormat) errorResource = QStringLiteral("IDS_ERR_CRS_FORMAT");
        else errorResource = QStringLiteral("IDS_ERR_CRS_LOADGENERIC");
        showResourceMessage(this, errorResource);
        delete loaded;
        return;
    }
    if (error == g_uErrRulesSkipped)
        showResourceMessage(this, QStringLiteral("IDS_ERR_CRS_SKIPPEDRULES"));

    const INT existing = findRuleSetByName(m_lstSets, loaded->GetName());
    if (existing < 0) {
        AddLoadedRuleSet(loaded);
        return;
    }
    CSetNameConflict conflict(loaded->GetName(), m_lstSets, this);
    const INT result = conflict.exec();
    if (result == IDOVERWRITE) {
        delete m_lstSets->takeItem(existing);
        m_pDynaCopy->bRemoveRuleSet(nullptr, existing);
        AddLoadedRuleSet(loaded);
    } else if (result == IDRENAME) {
        loaded->SetName(conflict.m_strSetName);
        AddLoadedRuleSet(loaded);
    } else {
        delete loaded;
    }
}

void CRuleSetsPage::OnSaveRuleSet()
{
    if (!m_pDynaCopy) return;
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    const INT index = m_lstSets->currentRow();
    if (!set || index < 0 || m_lstSets->RuleSetAt(index) != set) return;
    QString fileName = QFileDialog::getSaveFileName(
        this, QString(), set->GetName(), ruleSetFileFilter());
    if (fileName.isEmpty()) return;
    if (!QFileInfo(fileName).fileName().contains(QLatin1Char('.')))
        fileName += QStringLiteral(".crs");
    UINT error = 0;
    if (!set->bSaveToFile(fileName, &error))
        showResourceMessage(this, QStringLiteral("IDS_ERR_CRS_SAVEGENERIC"));
}

void CRuleSetsPage::OnRuleSetItemChanged()
{
    if (!m_pDynaCopy) return;
    const INT index = m_lstSets->currentRow();
    m_pDynaCopy->SetSelectedRuleSet(
        index >= 0 ? m_lstSets->RuleSetAt(index) : nullptr);
    UpdateButtonsStatus();
}

/////////////////////////////////////////////////////////////////////////////
// CRulesPage

CRulesPage::CRulesPage(QWidget* parent)
    : QWidget(parent)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_RULESPAGE"));
    setObjectName(QStringLiteral("IDD_RULESPAGE"));
    setFont(resourceFont(dialog));
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);

    m_lstSets = new QListWidget(this);
    m_lstSets->setSelectionMode(QAbstractItemView::SingleSelection);
    placeControl(m_lstSets, dialog, mapper, QStringLiteral("IDC_LSTSETS"));
    m_lstRules = new CRulesListCtrl(this);
    placeControl(m_lstRules, dialog, mapper, QStringLiteral("IDC_LSTRULES"));
    m_lstRules->setHeaderLabels({
        originalResourceString(QStringLiteral("IDS_EVENTS_LABEL")),
        originalResourceString(QStringLiteral("IDS_ACTIONS_LABEL"))});
    m_lstRules->setColumnWidth(0, originalResourceString(
        QStringLiteral("IDS_EVENTS_WIDTH")).toInt());
    m_lstRules->setColumnWidth(1, originalResourceString(
        QStringLiteral("IDS_ACTIONS_WIDTH")).toInt());
    m_bRulesColumnSet = TRUE;

    auto makeButton = [this, &dialog, &mapper](const QString& id) {
        auto* button = new QPushButton(controlText(dialog, id), this);
        placeControl(button, dialog, mapper, id);
        return button;
    };
    m_advanced = makeButton(QStringLiteral("IDC_ADVANCEDRULE"));
    m_add = makeButton(QStringLiteral("IDC_ADDRULE"));
    m_edit = makeButton(QStringLiteral("IDC_EDITRULE"));
    m_delete = makeButton(QStringLiteral("IDC_DELETERULE"));
    m_duplicate = makeButton(QStringLiteral("IDC_DUPLICATERULE"));
    m_addToSets = makeButton(QStringLiteral("IDC_ADDTORULESETS"));
    m_moveUp = makeButton(QStringLiteral("IDC_MOVEUP"));
    m_moveDown = makeButton(QStringLiteral("IDC_MOVEDOWN"));

    connect(m_lstSets, &QListWidget::currentRowChanged,
            this, [this](int) { OnRuleSetItemChanged(); });
    connect(m_lstRules, &QTreeWidget::itemSelectionChanged,
            this, [this] { UpdateButtonsStatus(); });
    connect(m_lstRules, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem*, int) {
                if (m_lstRules->iGetSelectedRule() >= 0) OnEditRule();
            });
    connect(m_add, &QPushButton::clicked, this, [this] { OnAddRule(); });
    connect(m_edit, &QPushButton::clicked, this, [this] { OnEditRule(); });
    connect(m_delete, &QPushButton::clicked, this, [this] { OnDeleteRule(); });
    connect(m_duplicate, &QPushButton::clicked,
            this, [this] { OnDuplicateRule(); });
    connect(m_moveUp, &QPushButton::clicked,
            this, [this] { OnMoveUpRule(); });
    connect(m_moveDown, &QPushButton::clicked,
            this, [this] { OnMoveDownRule(); });
    connect(m_addToSets, &QPushButton::clicked,
            this, [this] { OnAddToRuleSets(); });
    connect(m_advanced, &QPushButton::clicked,
            this, [this] { OnAdvancedRuleSettings(); });
    UpdateButtonsStatus();
}

void CRulesPage::SetDynaRules(CCDynaRules* dynaCopy)
{
    m_pDynaCopy = dynaCopy;
    if (m_pDynaCopy) OnSetActive();
}

BOOL CRulesPage::bFillRuleSets()
{
    if (!m_pDynaCopy) return FALSE;
    m_lstSets->clear();
    INT selected = -1;
    const QVector<CCRuleSet*>& sets = m_pDynaCopy->GetRuleSetsArray();
    for (INT index = 0; index < sets.size(); ++index) {
        CCRuleSet* set = sets[index];
        if (!set) return FALSE;
        auto* item = new QListWidgetItem(set->GetName(), m_lstSets);
        item->setData(Qt::UserRole, pointerVariant(set));
        if ((index == 0 && !m_pDynaCopy->GetSelectedRuleSet())
            || set == m_pDynaCopy->GetSelectedRuleSet()) selected = index;
    }
    if (selected >= 0) SetCurRuleSet(sets[selected], selected);
    else {
        m_pDynaCopy->SetSelectedRuleSet(nullptr);
        m_lstRules->bFill(m_pDynaCopy);
    }
    return TRUE;
}

void CRulesPage::SetCurRuleSet(CCRuleSet* ruleSet, INT index)
{
    if (!m_pDynaCopy || index < 0 || index >= m_lstSets->count()) return;
    m_lstSets->setCurrentRow(index);
    CCRuleSet* selected = ruleSet ? ruleSet
        : pointerFromVariant<CCRuleSet>(m_lstSets->item(index)->data(Qt::UserRole));
    m_pDynaCopy->SetSelectedRuleSet(selected);
    m_lstRules->bFill(m_pDynaCopy);
}

void CRulesPage::UpdateButtonsStatus()
{
    const BOOL setSelected = m_pDynaCopy
        && m_pDynaCopy->GetSelectedRuleSet();
    const INT index = m_lstRules ? m_lstRules->iGetSelectedRule() : -1;
    const BOOL ruleSelected = index >= 0;
    m_add->setEnabled(setSelected);
    m_edit->setEnabled(ruleSelected);
    m_delete->setEnabled(ruleSelected);
    m_duplicate->setEnabled(ruleSelected);
    m_addToSets->setEnabled(ruleSelected && m_pDynaCopy
                            && m_pDynaCopy->GetRuleSetsArray().size() > 1);
    m_moveUp->setEnabled(ruleSelected && index > 0);
    m_moveDown->setEnabled(ruleSelected
                           && index < m_lstRules->topLevelItemCount() - 1);
}

BOOL CRulesPage::OnSetActive()
{
    if (!m_pDynaCopy) return FALSE;
    const BOOL result = bFillRuleSets() && m_lstRules->bFill(m_pDynaCopy);
    UpdateButtonsStatus();
    m_bModified = FALSE;
    return result;
}

void CRulesPage::OnOK()
{
    applyRulesCopy(m_pDynaCopy);
    m_bModified = FALSE;
}

void CRulesPage::OnRuleSetItemChanged()
{
    if (!m_pDynaCopy) return;
    const INT index = m_lstSets->currentRow();
    CCRuleSet* selected = index >= 0
        ? pointerFromVariant<CCRuleSet>(
              m_lstSets->item(index)->data(Qt::UserRole))
        : nullptr;
    m_pDynaCopy->SetSelectedRuleSet(selected);
    m_lstRules->bFill(m_pDynaCopy);
    UpdateButtonsStatus();
}

void CRulesPage::OnAddRule()
{
    if (!m_pDynaCopy) return;
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    if (!set) return;
    auto* rule = new CCRule(m_pDynaCopy);
    CEditRule dialog(this);
    dialog.UseRule(rule, m_pDynaCopy);
    if (dialog.exec() == QDialog::Accepted) {
        set->bAddRule(rule);
        rule->Activate();
        rule->SetDelay(dialog.GetDelay());
        m_lstRules->bAddRule(rule);
        QTreeWidgetItem* item = m_lstRules->topLevelItem(
            m_lstRules->topLevelItemCount() - 1);
        m_lstRules->setCurrentItem(item);
        item->setSelected(true);
        m_lstRules->scrollToItem(item);
        SetModified(TRUE);
    } else {
        rule->Release();
    }
    UpdateButtonsStatus();
}

void CRulesPage::OnEditRule()
{
    if (!m_pDynaCopy) return;
    CCRule* rule = nullptr;
    const INT index = m_lstRules->iGetSelectedRule(&rule);
    if (index < 0 || !rule || !m_pDynaCopy->GetSelectedRuleSet()) return;
    auto* copy = new CCRule(rule, m_pDynaCopy);
    CEditRule dialog(this);
    dialog.UseRule(copy, m_pDynaCopy);
    if (dialog.exec() == QDialog::Accepted) {
        copy->SetDelay(dialog.GetDelay());
        rule->CopyRule(copy);
        QTreeWidgetItem* item = m_lstRules->topLevelItem(index);
        item->setText(0, rule->StrGetEventDisplay());
        item->setText(1, rule->StrGetActionDisplay());
        SetModified(TRUE);
    }
    copy->Release();
}

void CRulesPage::OnDeleteRule()
{
    if (!m_pDynaCopy) return;
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    const INT index = m_lstRules->iGetSelectedRule();
    if (!set || index < 0) return;
    delete m_lstRules->takeTopLevelItem(index);
    set->bRemoveRule(nullptr, index);
    SetModified(TRUE);
    if (m_lstRules->topLevelItemCount() > 0) {
        const INT next = qMin(index, m_lstRules->topLevelItemCount() - 1);
        QTreeWidgetItem* item = m_lstRules->topLevelItem(next);
        m_lstRules->setCurrentItem(item);
        item->setSelected(true);
    } else {
        m_add->setFocus();
    }
    UpdateButtonsStatus();
}

void CRulesPage::OnDuplicateRule()
{
    if (!m_pDynaCopy) return;
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    const INT index = m_lstRules->iGetSelectedRule();
    if (!set || index < 0) return;
    CCRule* copy = nullptr;
    if (set->bDuplicateRule(index, &copy) && copy) {
        m_lstRules->bAddRule(copy, index + 1);
        SetModified(TRUE);
    }
    UpdateButtonsStatus();
}

void CRulesPage::OnMoveUpRule()
{
    if (!m_pDynaCopy) return;
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    CCRule* rule = nullptr;
    const INT index = m_lstRules->iGetSelectedRule(&rule);
    if (!set || !rule || index < 1) return;
    QTreeWidgetItem* item = m_lstRules->takeTopLevelItem(index);
    m_lstRules->insertTopLevelItem(index - 1, item);
    m_lstRules->setCurrentItem(item);
    item->setSelected(true);
    set->bUpRule(nullptr, index);
    (index == 1 ? m_moveDown : m_moveUp)->setFocus();
    SetModified(TRUE);
    UpdateButtonsStatus();
}

void CRulesPage::OnMoveDownRule()
{
    if (!m_pDynaCopy) return;
    CCRuleSet* set = m_pDynaCopy->GetSelectedRuleSet();
    CCRule* rule = nullptr;
    const INT index = m_lstRules->iGetSelectedRule(&rule);
    if (!set || !rule || index < 0
        || index >= m_lstRules->topLevelItemCount() - 1) return;
    QTreeWidgetItem* item = m_lstRules->takeTopLevelItem(index);
    m_lstRules->insertTopLevelItem(index + 1, item);
    m_lstRules->setCurrentItem(item);
    item->setSelected(true);
    set->bDownRule(nullptr, index);
    (index == m_lstRules->topLevelItemCount() - 2
         ? m_moveUp : m_moveDown)->setFocus();
    SetModified(TRUE);
    UpdateButtonsStatus();
}

void CRulesPage::OnAddToRuleSets()
{
    if (!m_pDynaCopy) return;
    CCRule* rule = nullptr;
    if (m_lstRules->iGetSelectedRule(&rule) < 0 || !rule) return;
    CCDynaRules copy;
    copy = *m_pDynaCopy;
    if (!copy.GetSelectedRuleSet()) return;
    CAddToSets dialog(&copy, copy.GetSelectedRuleSet(), rule, this);
    if (dialog.exec() == QDialog::Accepted && dialog.bRuleAdded()) {
        *m_pDynaCopy = copy;
        bFillRuleSets();
        m_lstRules->bFill(m_pDynaCopy);
        UpdateButtonsStatus();
        SetModified(TRUE);
    }
}

void CRulesPage::OnAdvancedRuleSettings()
{
    if (!m_pDynaCopy) return;
    CAdvancedRuleSettings dialog(
        m_pDynaCopy->GetFloodingOccurrences(),
        m_pDynaCopy->GetFloodingInterval(), this);
    if (dialog.exec() == QDialog::Accepted) {
        m_pDynaCopy->SetFloodParams(dialog.m_uInt, dialog.m_uOcc);
        SetModified(TRUE);
    }
}

/////////////////////////////////////////////////////////////////////////////
// CEditRule

CEditRule::CEditRule(QWidget* parent)
    : QDialog(parent)
{
    const OriginalDialogResource dialog = originalDialogResource(
        QStringLiteral("IDD_EDITRULE"));
    setObjectName(QStringLiteral("IDD_EDITRULE"));
    setFont(resourceFont(dialog));
    setWindowTitle(dialog.caption);
    const DialogUnitMapper mapper(font());
    setFixedSize(mapper.x(dialog.width), mapper.y(dialog.height));
    createStaticControls(this, dialog, mapper);

    for (UINT type = 0; type < static_cast<UINT>(ptMax); ++type)
        m_rgstrParamLabels[type] = originalResourceString(
            IDS_LBL_ACTIVATE + static_cast<INT>(type));

    m_cmbEvents = new CRtfCmb(this);
    m_cmbEvents->setEditable(false);
    placeControl(m_cmbEvents, dialog, mapper, QStringLiteral("IDC_CMBEVENTS"));
    {
        QRect geometry = m_cmbEvents->geometry();
        geometry.setHeight(qMin(geometry.height(),
                                m_cmbEvents->sizeHint().height()));
        m_cmbEvents->setGeometry(geometry);
    }
    m_cmbEvents->setProperty("descriptionKind", QStringLiteral("events"));
    m_cmbEvents->installEventFilter(this);

    m_lblEventHeader = new QLabel(controlText(
        dialog, QStringLiteral("IDC_LBLEP")), this);
    placeControl(m_lblEventHeader, dialog, mapper, QStringLiteral("IDC_LBLEP"));
    const QString eventLabelIds[] = {QStringLiteral("IDC_LBLEP0"),
                                     QStringLiteral("IDC_LBLEP1"),
                                     QStringLiteral("IDC_LBLEP2")};
    const QString eventComboIds[] = {QStringLiteral("IDC_CMBEP0"),
                                     QStringLiteral("IDC_CMBEP1"),
                                     QStringLiteral("IDC_CMBEP2")};
    for (UINT index = 0; index < g_uMaxEventParams; ++index) {
        m_lblEventParams[index] = new QLabel(this);
        m_lblEventParams[index]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        placeControl(m_lblEventParams[index], dialog, mapper,
                     eventLabelIds[index]);
        auto* eventCombo = new CChatServiceComboBox(this);
        eventCombo->SetServiceList(&theApp.m_listChatServices);
        m_cmbEventParams[index] = eventCombo;
        m_cmbEventParams[index]->setEditable(true);
        m_cmbEventParams[index]->setInsertPolicy(QComboBox::NoInsert);
        placeControl(m_cmbEventParams[index], dialog, mapper,
                     eventComboIds[index]);
        if (m_cmbEventParams[index]->lineEdit())
            m_cmbEventParams[index]->lineEdit()->setMaxLength(
                g_uMaxParamLength);
        m_cmbEventParams[index]->setProperty("descriptionKind",
                                             QStringLiteral("eventParam"));
        m_cmbEventParams[index]->setProperty("descriptionIndex",
                                             static_cast<int>(index));
        m_cmbEventParams[index]->installEventFilter(this);
        if (m_cmbEventParams[index]->lineEdit())
            m_cmbEventParams[index]->lineEdit()->installEventFilter(this);
    }

    m_btnAdvanced = new QPushButton(controlText(
        dialog, QStringLiteral("IDC_BTNADVANCED")), this);
    placeControl(m_btnAdvanced, dialog, mapper,
                 QStringLiteral("IDC_BTNADVANCED"));

    m_cmbActions = new CRtfCmb(this);
    m_cmbActions->setEditable(false);
    placeControl(m_cmbActions, dialog, mapper,
                 QStringLiteral("IDC_CMBACTIONS"));
    {
        QRect geometry = m_cmbActions->geometry();
        geometry.setHeight(qMin(geometry.height(),
                                m_cmbActions->sizeHint().height()));
        m_cmbActions->setGeometry(geometry);
    }
    m_cmbActions->setProperty("descriptionKind", QStringLiteral("actions"));
    m_cmbActions->installEventFilter(this);
    m_lblActionHeader = new QLabel(controlText(
        dialog, QStringLiteral("IDC_LBLAP")), this);
    placeControl(m_lblActionHeader, dialog, mapper,
                 QStringLiteral("IDC_LBLAP"));
    const QString actionLabelIds[] = {QStringLiteral("IDC_LBLAP0"),
                                      QStringLiteral("IDC_LBLAP1"),
                                      QStringLiteral("IDC_LBLAP2")};
    const QString actionComboIds[] = {QStringLiteral("IDC_CMBAP0"),
                                      QStringLiteral("IDC_CMBAP1"),
                                      QStringLiteral("IDC_CMBAP2")};
    for (UINT index = 0; index < g_uMaxActionParams; ++index) {
        m_lblActionParams[index] = new QLabel(this);
        m_lblActionParams[index]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        placeControl(m_lblActionParams[index], dialog, mapper,
                     actionLabelIds[index]);
        auto* actionCombo = new CRtfCmb(this);
        m_cmbActionParams[index] = actionCombo;
        placeControl(m_cmbActionParams[index], dialog, mapper,
                     actionComboIds[index]);
        {
            QRect geometry = m_cmbActionParams[index]->geometry();
            geometry.setHeight(qMin(
                geometry.height(),
                m_cmbActionParams[index]->sizeHint().height()));
            m_cmbActionParams[index]->setGeometry(geometry);
        }
        if (m_cmbActionParams[index]->lineEdit())
            m_cmbActionParams[index]->lineEdit()->setMaxLength(
                g_uMaxParamLength);
        m_cmbActionParams[index]->setProperty("descriptionKind",
                                              QStringLiteral("actionParam"));
        m_cmbActionParams[index]->setProperty("descriptionIndex",
                                              static_cast<int>(index));
        m_cmbActionParams[index]->installEventFilter(this);
        if (m_cmbActionParams[index]->lineEdit())
            m_cmbActionParams[index]->lineEdit()->installEventFilter(this);
    }

    m_cmbActionNetParam = new CChatServiceComboBox(this);
    m_cmbActionNetParam->SetServiceList(&theApp.m_listChatServices);
    m_cmbActionNetParam->setObjectName(QStringLiteral("IDC_CMBAPNS"));
    if (m_cmbActionNetParam->lineEdit())
        m_cmbActionNetParam->lineEdit()->setMaxLength(g_uMaxParamLength);
    m_cmbActionNetParam->setProperty(
        "descriptionKind", QStringLiteral("actionParam"));
    m_cmbActionNetParam->installEventFilter(this);
    if (m_cmbActionNetParam->lineEdit())
        m_cmbActionNetParam->lineEdit()->installEventFilter(this);
    m_cmbActionNetParam->hide();

    m_lblParamDesc = new QLabel(this);
    m_lblParamDesc->setWordWrap(true);
    placeControl(m_lblParamDesc, dialog, mapper,
                 QStringLiteral("IDC_LBLPARAMDESC"));

    const OriginalDialogControl* delayEdit = findControl(
        dialog, QStringLiteral("IDC_RULEDELAY"));
    const OriginalDialogControl* delaySpin = findControl(
        dialog, QStringLiteral("IDC_DELAYSPIN"));
    m_spinDelay = new QSpinBox(this);
    m_spinDelay->setObjectName(QStringLiteral("IDC_RULEDELAY"));
    m_spinDelay->setRange(0, 255);
    if (delayEdit && delaySpin) {
        QRect geometry = mapper.rect(*delayEdit);
        geometry.setRight(mapper.rect(*delaySpin).right());
        m_spinDelay->setGeometry(geometry);
    }

    m_chkSubRules = new QCheckBox(controlText(
        dialog, QStringLiteral("IDC_CHKSUBRULES")), this);
    placeControl(m_chkSubRules, dialog, mapper,
                 QStringLiteral("IDC_CHKSUBRULES"));
    m_ok = new QPushButton(controlText(dialog, QStringLiteral("IDOK")), this);
    m_ok->setDefault(true);
    placeControl(m_ok, dialog, mapper, QStringLiteral("IDOK"));
    m_cancel = new QPushButton(controlText(
        dialog, QStringLiteral("IDCANCEL")), this);
    placeControl(m_cancel, dialog, mapper, QStringLiteral("IDCANCEL"));

    connect(m_cmbEvents, &QComboBox::currentIndexChanged,
            this, [this](int) { if (!m_bUpdating) OnEventChanged(); });
    connect(m_cmbActions, &QComboBox::currentIndexChanged,
            this, [this](int) { if (!m_bUpdating) OnActionChanged(); });
    connect(m_chkSubRules, &QCheckBox::clicked,
            this, [this] { OnFlagsCheckClick(); });
    connect(m_btnAdvanced, &QPushButton::clicked,
            this, [this] { OnAdvancedClick(); });
    connect(m_ok, &QPushButton::clicked, this, &CEditRule::accept);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
}

CEditRule::~CEditRule()
{
    for (UINT type = 0; type < static_cast<UINT>(ptMax); ++type)
        FreeAndNullFormatting(&m_prgdwActionParamFormatting[type]);
}

void CEditRule::UseRule(CCRule* rule, CCDynaRules* dynaRules)
{
    m_pRule = rule;
    m_pDynaRules = dynaRules;
}

void CEditRule::showEvent(QShowEvent* event)
{
    if (!m_bInitialized) InitializeRule();
    QDialog::showEvent(event);
}

BOOL CEditRule::IsUnsupportedAction(enumActions action) const
{
    switch (action) {
    case aPlaySound:
    case aSendSound:
        return TRUE;
    default:
        return FALSE;
    }
}

BOOL CEditRule::InitializeRule()
{
    if (m_bInitialized) return TRUE;
    if (!m_pRule || !m_pDynaRules) return FALSE;
    if (!theApp.m_rulesData.bInitAlloc()
        || !theApp.m_rulesData.bLoadStrings()) return FALSE;
    m_bUpdating = TRUE;
    m_uDelay = m_pRule->GetDelay();
    m_spinDelay->setValue(m_uDelay);

    struct EventItem { QString text; CCEvent* event = nullptr; };
    QList<EventItem> events;
    for (UINT index = 0; index < static_cast<UINT>(eMax); ++index) {
        if (CCEvent* event = theApp.m_rulesData.GetEvent(index))
            events.append({event->GetLongDesc(), event});
    }
    std::sort(events.begin(), events.end(), [](const EventItem& left,
                                                const EventItem& right) {
        return QString::localeAwareCompare(left.text, right.text) < 0;
    });
    INT selectedEvent = -1;
    for (const EventItem& entry : events) {
        m_cmbEvents->addItem(entry.text, pointerVariant(entry.event));
        if (entry.event == m_pRule->GetEvent())
            selectedEvent = m_cmbEvents->count() - 1;
    }
    if (selectedEvent < 0 && m_cmbEvents->count() > 0) {
        selectedEvent = 0;
        m_pRule->SetEvent(pointerFromVariant<CCEvent>(
            m_cmbEvents->itemData(0)));
    }
    m_cmbEvents->setCurrentIndex(selectedEvent);
    m_chkSubRules->setChecked(m_pRule->wGetFlags() & g_wNoSubsequent);

    bFillActionsFromEvent();
    SaveRuleParams();
    FillParamLabels(TRUE, TRUE);
    bFillParamsFromRule(TRUE, TRUE);
    UpdateAdvancedControls();
    m_bUpdating = FALSE;
    m_bInitialized = TRUE;
    m_cmbEvents->setFocus();
    return TRUE;
}

BOOL CEditRule::bFillActionsFromEvent(BOOL* actionChanged)
{
    if (!m_pRule || !m_pRule->GetEvent()) return FALSE;
    CCAction* oldAction = m_pRule->GetAction();
    struct ActionItem { QString text; CCAction* action = nullptr; };
    QList<ActionItem> actions;
    DWORD mask = 1;
    const DWORD enabled = m_pRule->GetEvent()->GetEnabledActions();
    for (UINT index = 0; index < static_cast<UINT>(aMax); ++index) {
        if (enabled & mask) {
            if (CCAction* action = theApp.m_rulesData.GetAction(index))
                actions.append({action->GetLongDesc(), action});
        }
        mask <<= 1;
    }
    std::sort(actions.begin(), actions.end(), [](const ActionItem& left,
                                                  const ActionItem& right) {
        return QString::localeAwareCompare(left.text, right.text) < 0;
    });
    m_cmbActions->clear();
    INT selected = -1;
    INT firstSupported = -1;
    for (const ActionItem& entry : actions) {
        m_cmbActions->addItem(entry.text, pointerVariant(entry.action));
        const INT itemIndex = m_cmbActions->count() - 1;
        const BOOL unsupported = IsUnsupportedAction(entry.action->GetID());
        if (auto* model = qobject_cast<QStandardItemModel*>(
                m_cmbActions->model())) {
            if (QStandardItem* item = model->item(itemIndex))
                item->setEnabled(!unsupported);
        }
        if (!unsupported && firstSupported < 0) firstSupported = itemIndex;
        if (entry.action == oldAction) selected = itemIndex;
    }
    const BOOL oldSelected = selected >= 0;
    if (!oldSelected) selected = firstSupported;
    if (selected < 0 && m_cmbActions->count() > 0) selected = 0;
    m_cmbActions->setCurrentIndex(selected);
    CCAction* selectedAction = selected >= 0
        ? pointerFromVariant<CCAction>(m_cmbActions->itemData(selected))
        : nullptr;
    m_pRule->SetAction(selectedAction);
    if (actionChanged) *actionChanged = !oldSelected;
    m_ok->setEnabled(selectedAction
                     && !IsUnsupportedAction(selectedAction->GetID()));
    return selectedAction != nullptr;
}

void CEditRule::FillParamLabels(BOOL events, BOOL actions)
{
    if (events && m_pRule && m_pRule->GetEvent()) {
        CCEvent* event = m_pRule->GetEvent();
        const UINT count = event->GetParamNum();
        for (UINT index = 0; index < g_uMaxEventParams; ++index) {
            const BOOL visible = index < count;
            m_lblEventParams[index]->setVisible(visible);
            m_cmbEventParams[index]->setVisible(visible);
            m_cmbEventParams[index]->clear();
            if (visible)
                m_lblEventParams[index]->setText(
                    m_rgstrParamLabels[event->GetParamType(index)]);
        }
        m_lblEventHeader->setVisible(count > 0);
    }
    if (actions && m_pRule && m_pRule->GetAction()) {
        CCAction* action = m_pRule->GetAction();
        const UINT count = action->GetParamNum();
        BOOL netParameterPresent = FALSE;
        m_cmbActionNetParam->hide();
        m_cmbActionNetParam->clear();
        for (UINT index = 0; index < g_uMaxActionParams; ++index) {
            const BOOL visible = index < count;
            m_lblActionParams[index]->setVisible(visible);
            m_cmbActionParams[index]->clear();
            if (!visible) {
                m_cmbActionParams[index]->hide();
                continue;
            }
            const enumParamType type = action->GetParamType(index);
            m_lblActionParams[index]->setText(m_rgstrParamLabels[type]);
            if (type == ptServerName) {
                m_cmbActionParams[index]->hide();
                m_cmbActionNetParam->setGeometry(
                    m_cmbActionParams[index]->geometry());
                m_cmbActionNetParam->setProperty(
                    "descriptionIndex", static_cast<int>(index));
                m_cmbActionNetParam->show();
                netParameterPresent = TRUE;
                continue;
            }

            const BOOL rich = RTFParam(type, action->GetID());
            m_cmbActionParams[index]->bSetRtfMode(rich);
            m_cmbActionParams[index]->LimitText(g_uMaxParamLength);
            if (rich) {
                m_cmbActionParams[index]->bAttachRtfCtrl(
                    QStringLiteral("IDC_RTFAP%1").arg(index));
                CRtfCmbEdit* editor =
                    m_cmbActionParams[index]->GetRtfCmbEdit();
                editor->m_crTextColor = RGB(0, 0, 0);
                editor->setProperty(
                    "descriptionKind", QStringLiteral("actionParam"));
                editor->setProperty(
                    "descriptionIndex", static_cast<int>(index));
                editor->installEventFilter(this);
            }
            m_cmbActionParams[index]->show();
        }
        if (!netParameterPresent) m_cmbActionNetParam->hide();
        m_lblActionHeader->setVisible(count > 0);
    }
}

void CEditRule::SaveRuleParams()
{
    if (!m_pRule || !m_pRule->GetEvent() || !m_pRule->GetAction()) return;
    CCEvent* event = m_pRule->GetEvent();
    for (UINT index = 0; index < event->GetParamNum(); ++index)
        m_rgstrEventParams[event->GetParamType(index)] =
            m_pRule->GetEventParam(index);
    CCAction* action = m_pRule->GetAction();
    for (UINT index = 0; index < action->GetParamNum(); ++index) {
        const enumParamType type = action->GetParamType(index);
        m_rgstrActionParams[type] = m_pRule->GetActionParam(index);
        if (RTFParam(type, action->GetID())) {
            FreeAndNullFormatting(&m_prgdwActionParamFormatting[type]);
            m_prgdwActionParamFormatting[type] =
                CopyFormatting(m_pRule->GetMsgFormatting());
        }
    }
}

QString CEditRule::EventParamText(UINT index) const
{
    return index < g_uMaxEventParams && m_cmbEventParams[index]
        ? m_cmbEventParams[index]->currentText() : QString();
}

QString CEditRule::ActionParamText(UINT index) const
{
    if (!m_pRule || !m_pRule->GetAction()
        || index >= m_pRule->GetAction()->GetParamNum()) return {};
    const enumParamType type = m_pRule->GetAction()->GetParamType(index);
    if (type == ptServerName) return m_cmbActionNetParam->currentText();
    return m_cmbActionParams[index]->GetWindowText();
}

void CEditRule::SetEventParamText(UINT index, const QString& text)
{
    if (index < g_uMaxEventParams) m_cmbEventParams[index]->setEditText(text);
}

void CEditRule::SetActionParamText(UINT index, const QString& text,
                                  CDWordArray* formatting)
{
    if (!m_pRule || !m_pRule->GetAction()
        || index >= m_pRule->GetAction()->GetParamNum()) return;
    const enumParamType type = m_pRule->GetAction()->GetParamType(index);
    if (type == ptServerName) {
        m_cmbActionNetParam->setEditText(text);
        return;
    }
    if (RTFParam(type, m_pRule->GetAction()->GetID())) {
        CRtfCmbEdit* editor =
            m_cmbActionParams[index]->GetRtfCmbEdit();
        if (!editor) return;
        editor->UseDefaultCharFormat();
        editor->bSetTextColor(editor->m_crTextColor);
        editor->bSetWindowFormattedText(text, formatting);
        editor->moveCursor(QTextCursor::Start);
    } else {
        m_cmbActionParams[index]->SetWindowText(text);
    }
}

BOOL CEditRule::bFillParamsFromRule(BOOL events, BOOL actions)
{
    if (!m_pRule || !m_pRule->GetEvent() || !m_pRule->GetAction())
        return FALSE;
    CCEvent* event = m_pRule->GetEvent();
    CCAction* action = m_pRule->GetAction();
    if (events) {
        for (UINT index = 0; index < event->GetParamNum(); ++index) {
            const enumParamType type = event->GetParamType(index);
            QComboBox* combo = m_cmbEventParams[index];
            combo->clear();
            UINT bit = 1;
            const UINT keys = event->GetKeyParam(index);
            for (UINT key = 0; key < static_cast<UINT>(kepMax); ++key) {
                if (keys & bit)
                    combo->addItem(theApp.m_rulesData.GetKeyEventParam(
                        static_cast<enumKeyEventParam>(key)));
                bit <<= 1;
            }
            if (type == ptServerName)
                static_cast<CChatServiceComboBox*>(combo)->Fill();
            SetEventParamText(index, m_rgstrEventParams[type]);
        }
    }
    if (actions) {
        const DWORD exposed = event->GetActionKeysExposed();
        for (UINT index = 0; index < action->GetParamNum(); ++index) {
            const enumParamType type = action->GetParamType(index);
            QComboBox* combo = type == ptServerName
                ? static_cast<QComboBox*>(m_cmbActionNetParam)
                : static_cast<QComboBox*>(m_cmbActionParams[index]);
            combo->clear();
            switch (type) {
            case ptHighlight:
                for (UINT highlight = 1;
                     highlight <= NHIGHLIGHTEDFONTS / 2; ++highlight) {
                    QString text = originalResourceString(
                        QStringLiteral("IDS_HIGHLIGHT_TYPE"));
                    text.replace(QStringLiteral("%d"),
                                 QString::number(highlight));
                    combo->addItem(text);
                }
                break;
            case ptMacroName:
                for (INT macro = 0; macro < NMACROS; ++macro)
                    if (theApp.m_macros[macro].m_bDefined)
                        combo->addItem(theApp.m_macros[macro].m_strName);
                break;
            case ptRuleSetName:
                if (m_pDynaRules) {
                    for (CCRuleSet* set : m_pDynaRules->GetRuleSetsArray())
                        if (set) combo->addItem(set->GetName());
                }
                break;
            case ptSoundFileName:
                // Original enumeration belongs to sounddlg.* / utils.*.
                // The combo remains empty/editable until Sound is ported.
                break;
            case ptTextFileName: {
                FILEENUMSTRUCT fileEnum;
                fileEnum.pszTypes = "txt\0";
                fileEnum.pfnAdd = AddTextFileToComboBox;
                fileEnum.lParam = reinterpret_cast<qintptr>(combo);
                fileEnum.bRecursive = TRUE;
                EnumFiles(theApp.m_strBaseDir, &fileEnum);
                break;
            }
            default: {
                UINT bit = 1;
                const UINT keys = action->GetKeyParam(index);
                for (UINT key = 0; key < static_cast<UINT>(kapMax); ++key) {
                    if ((keys & bit) && (exposed & bit))
                        combo->addItem(theApp.m_rulesData.GetKeyActionParam(
                            static_cast<enumKeyActionParam>(key)));
                    bit <<= 1;
                }
                break;
            }
            }
            if (type == ptServerName) m_cmbActionNetParam->Fill();
            SetActionParamText(index, m_rgstrActionParams[type],
                               m_prgdwActionParamFormatting[type]);
        }

        const BOOL delayOK = action->GetDelayOK();
        m_spinDelay->setEnabled(delayOK);
        if (!delayOK) {
            m_uMinDelay = 0;
            m_uDelay = 0;
            m_spinDelay->setRange(0, 255);
            m_spinDelay->setValue(0);
        } else {
            m_uMinDelay = 0;
            for (const RULEX& exception : g_rgex) {
                if (exception.ex == etMinDelay
                    && exception.eID == event->GetID()
                    && exception.aID == action->GetID()) {
                    m_uMinDelay = static_cast<UCHAR>(exception.dwValue);
                    break;
                }
            }
            m_spinDelay->setRange(m_uMinDelay, 255);
            if (m_spinDelay->value() < m_uMinDelay)
                m_spinDelay->setValue(m_uMinDelay);
            m_uDelay = static_cast<UCHAR>(m_spinDelay->value());
        }
        m_ok->setEnabled(!IsUnsupportedAction(action->GetID()));
    }
    return TRUE;
}

BOOL CEditRule::bCorrectActionKeys()
{
    if (!m_pRule || !m_pRule->GetEvent() || !m_pRule->GetAction())
        return FALSE;
    CCAction* action = m_pRule->GetAction();
    const DWORD exposed = m_pRule->GetEvent()->GetActionKeysExposed();
    for (UINT index = 0; index < action->GetParamNum(); ++index) {
        const enumParamType type = action->GetParamType(index);
        switch (type) {
        case ptHighlight:
        case ptMacroName:
        case ptRuleSetName:
        case ptSoundFileName:
        case ptServerName:
            continue;
        default:
            break;
        }
        const BOOL rich = RTFParam(type, action->GetID());
        const QString current = rich
            ? QString() : m_cmbActionParams[index]->GetWindowText();
        m_cmbActionParams[index]->clear();
        UINT bit = 1;
        const UINT keys = action->GetKeyParam(index);
        for (UINT key = 0; key < static_cast<UINT>(kapMax); ++key) {
            if ((keys & bit) && (exposed & bit))
                m_cmbActionParams[index]->addItem(
                    theApp.m_rulesData.GetKeyActionParam(
                        static_cast<enumKeyActionParam>(key)));
            bit <<= 1;
        }
        if (!rich) m_cmbActionParams[index]->SetWindowText(current);
    }
    return TRUE;
}

void CEditRule::SaveComboParams(BOOL events, BOOL actions)
{
    if (!m_pRule) return;
    if (events && m_pRule->GetEvent()) {
        CCEvent* event = m_pRule->GetEvent();
        for (UINT index = 0; index < event->GetParamNum(); ++index)
            m_rgstrEventParams[event->GetParamType(index)] =
                EventParamText(index);
    }
    if (actions && m_pRule->GetAction()) {
        CCAction* action = m_pRule->GetAction();
        for (UINT index = 0; index < action->GetParamNum(); ++index) {
            const enumParamType type = action->GetParamType(index);
            m_rgstrActionParams[type] = ActionParamText(index);
            if (RTFParam(type, action->GetID())) {
                FreeAndNullFormatting(&m_prgdwActionParamFormatting[type]);
                CRtfCmbEdit* editor =
                    m_cmbActionParams[index]->GetRtfCmbEdit();
                if (!editor) continue;
                m_prgdwActionParamFormatting[type] = PRGDWGetFormatting(
                    editor, editor->m_pFont, editor->m_crTextColor);
            }
        }
    }
}

void CEditRule::UpdateAdvancedControls()
{
    BOOL show = FALSE;
    if (m_pRule && m_pRule->GetEvent()) {
        CCEvent* event = m_pRule->GetEvent();
        for (UINT index = 0; index < event->GetParamNum(); ++index)
            if (event->GetParamType(index) == ptMessage) show = TRUE;
    }
    m_btnAdvanced->setVisible(show);
    m_btnAdvanced->setEnabled(show);
}

void CEditRule::OnEventChanged()
{
    if (!m_pRule || m_cmbEvents->currentIndex() < 0) return;
    SaveComboParams(TRUE, TRUE);
    m_bUpdating = TRUE;
    m_pRule->SetEvent(pointerFromVariant<CCEvent>(
        m_cmbEvents->currentData()));
    UpdateAdvancedControls();
    BOOL actionChanged = FALSE;
    bFillActionsFromEvent(&actionChanged);
    FillParamLabels(TRUE, actionChanged);
    bFillParamsFromRule(TRUE, actionChanged);
    if (!actionChanged) bCorrectActionKeys();
    m_bUpdating = FALSE;
}

void CEditRule::OnActionChanged()
{
    if (!m_pRule || m_cmbActions->currentIndex() < 0) return;
    SaveComboParams(FALSE, TRUE);
    m_bUpdating = TRUE;
    m_pRule->SetAction(pointerFromVariant<CCAction>(
        m_cmbActions->currentData()));
    FillParamLabels(FALSE, TRUE);
    bFillParamsFromRule(FALSE, TRUE);
    m_bUpdating = FALSE;
}

void CEditRule::OnFlagsCheckClick()
{
    if (!m_pRule) return;
    WORD flags = m_pRule->wGetFlags();
    if (m_chkSubRules->isChecked()) flags |= g_wNoSubsequent;
    else flags &= ~g_wNoSubsequent;
    m_pRule->SetFlags(flags);
}

void CEditRule::OnAdvancedClick()
{
    if (!m_pRule) return;
    WORD flags = m_pRule->wGetFlags();
    CAdvancedEventParams dialog(flags & (g_wMatchCase | g_wMatchWord), this);
    if (dialog.exec() != QDialog::Accepted) return;
    flags &= ~(g_wMatchCase | g_wMatchWord);
    if (dialog.m_iMatchCase) flags |= g_wMatchCase;
    if (dialog.m_iMatchWord) flags |= g_wMatchWord;
    m_pRule->SetFlags(flags);
}

void CEditRule::SetDescription(const QString& description)
{
    m_lblParamDesc->setText(description);
}

bool CEditRule::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn) {
        QObject* source = watched;
        if (source->property("descriptionKind").toString().isEmpty()
            && source->parent()) source = source->parent();
        const QString kind = source->property("descriptionKind").toString();
        const INT index = source->property("descriptionIndex").toInt();
        if (kind == QLatin1String("events")) {
            SetDescription(theApp.m_rulesData.GetEventsDesc());
        } else if (kind == QLatin1String("actions")) {
            SetDescription(theApp.m_rulesData.GetActionsDesc());
        } else if (kind == QLatin1String("eventParam")
                   && m_pRule && m_pRule->GetEvent()
                   && index >= 0
                   && index < static_cast<INT>(
                       m_pRule->GetEvent()->GetParamNum())) {
            SetDescription(m_pRule->GetEvent()->GetParamDesc(index));
        } else if (kind == QLatin1String("actionParam")
                   && m_pRule && m_pRule->GetAction()
                   && index >= 0
                   && index < static_cast<INT>(
                       m_pRule->GetAction()->GetParamNum())) {
            SetDescription(m_pRule->GetAction()->GetParamDesc(index));
        }
    } else if (event->type() == QEvent::FocusOut) {
        SetDescription(QString());
    }
    return QDialog::eventFilter(watched, event);
}

bool CEditRule::focusNextPrevChild(bool next)
{
    if (!m_pRule || !m_pRule->GetEvent() || !m_pRule->GetAction())
        return QDialog::focusNextPrevChild(next);

    QList<QWidget*> sourceOrder;
    auto appendFocusable = [this, &sourceOrder](QWidget* widget) {
        if (widget && widget->isVisibleTo(this) && widget->isEnabled()
            && widget->focusPolicy() != Qt::NoFocus) {
            sourceOrder.append(widget);
        }
    };

    appendFocusable(m_cmbEvents);
    for (UINT index = 0;
         index < m_pRule->GetEvent()->GetParamNum(); ++index) {
        appendFocusable(m_cmbEventParams[index]);
    }
    appendFocusable(m_btnAdvanced);
    appendFocusable(m_cmbActions);
    for (UINT index = 0;
         index < m_pRule->GetAction()->GetParamNum(); ++index) {
        const enumParamType type =
            m_pRule->GetAction()->GetParamType(index);
        if (type == ptServerName) {
            appendFocusable(m_cmbActionNetParam);
        } else if (m_cmbActionParams[index]->bGetRtfMode()) {
            appendFocusable(m_cmbActionParams[index]->GetRtfCmbEdit());
        } else {
            appendFocusable(m_cmbActionParams[index]);
        }
    }
    appendFocusable(m_spinDelay);
    appendFocusable(m_chkSubRules);
    appendFocusable(m_ok);
    appendFocusable(m_cancel);
    if (sourceOrder.isEmpty())
        return QDialog::focusNextPrevChild(next);

    QWidget* focused = QApplication::focusWidget();
    INT current = -1;
    for (INT index = 0; index < sourceOrder.size(); ++index) {
        QWidget* candidate = sourceOrder.at(index);
        if (focused == candidate
            || (focused && candidate->isAncestorOf(focused))) {
            current = index;
            break;
        }
    }
    if (current < 0) return QDialog::focusNextPrevChild(next);

    const INT count = sourceOrder.size();
    const INT targetIndex = next
        ? (current + 1) % count : (current + count - 1) % count;
    sourceOrder.at(targetIndex)->setFocus(
        next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    return true;
}

void CEditRule::accept()
{
    if (!m_pRule || !m_pRule->GetEvent() || !m_pRule->GetAction()) return;
    CCEvent* event = m_pRule->GetEvent();
    CCAction* action = m_pRule->GetAction();
    if (IsUnsupportedAction(action->GetID())) return;

    for (UINT index = 0; index < event->GetParamNum(); ++index) {
        QString parameter = EventParamText(index);
        if (parameter.isEmpty()) {
            QMessageBox::information(this,
                originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
                originalResourceString(static_cast<int>(
                    theApp.m_rulesData.GetMissingEventParamError(
                        event->GetParamType(index)))));
            return;
        }
        m_pRule->SetEventKeyParam(index, kepMax);
        UINT bit = 1;
        for (UINT key = static_cast<UINT>(kepAny);
             key < static_cast<UINT>(kepMax); ++key) {
            if ((event->GetKeyParam(index) & bit)
                && QString::compare(theApp.m_rulesData.GetKeyEventParam(
                       static_cast<enumKeyEventParam>(key)), parameter,
                       Qt::CaseInsensitive) == 0) {
                m_pRule->SetEventKeyParam(
                    index, static_cast<enumKeyEventParam>(key));
                m_pRule->SetEventParam(index,
                    theApp.m_rulesData.GetKeyEventParam(
                        static_cast<enumKeyEventParam>(key)));
                break;
            }
            bit <<= 1;
        }
        if (m_pRule->GetEventKeyParam(index) == kepMax) {
            m_pRule->SetEventParam(index, parameter);
            UINT error = 0;
            if (!m_pRule->bValidateRuleEvent(index, parameter, &error)) {
                QMessageBox::information(this,
                    originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
                    originalResourceString(static_cast<int>(error)));
                return;
            }
        }
    }

    for (UINT index = 0; index < action->GetParamNum(); ++index) {
        QString parameter = ActionParamText(index);
        if (parameter.isEmpty()
            && !(action->GetID() == aSendSound
                 && action->GetParamType(index) == ptMessage)) {
            QMessageBox::information(this,
                originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
                originalResourceString(static_cast<int>(
                    theApp.m_rulesData.GetMissingActionParamError(
                        action->GetParamType(index)))));
            return;
        }
        m_pRule->SetActionKeyParam(index, kapMax);
        for (UINT key = 0; key < static_cast<UINT>(kapMax); ++key) {
            if (QString::compare(theApp.m_rulesData.GetKeyActionParam(
                    static_cast<enumKeyActionParam>(key)), parameter,
                    Qt::CaseInsensitive) == 0) {
                m_pRule->SetActionKeyParam(
                    index, static_cast<enumKeyActionParam>(key));
                m_pRule->SetActionParam(index,
                    theApp.m_rulesData.GetKeyActionParam(
                        static_cast<enumKeyActionParam>(key)));
                break;
            }
        }
        if (m_pRule->GetActionKeyParam(index) == kapMax) {
            m_pRule->SetActionParam(index, parameter);
            UINT error = 0;
            if (!m_pRule->bValidateRuleAction(index, parameter, &error)) {
                QMessageBox::information(this,
                    originalResourceString(QStringLiteral("AFX_IDS_APP_TITLE")),
                    originalResourceString(static_cast<int>(error)));
                return;
            }
        }
        if (RTFParam(action->GetParamType(index), action->GetID())) {
            CRtfCmbEdit* editor =
                m_cmbActionParams[index]->GetRtfCmbEdit();
            if (!editor) return;
            CDWordArray* formatting = PRGDWGetFormatting(
                editor, editor->m_pFont, editor->m_crTextColor);
            m_pRule->SetMsgFormatting(formatting, FALSE);
        }
    }
    m_uDelay = static_cast<UCHAR>(m_spinDelay->value());
    if (m_pRule->GetDaemonExt())
        m_pRule->GetDaemonExt()->SetResetItemLists(TRUE);
    QDialog::accept();
}
