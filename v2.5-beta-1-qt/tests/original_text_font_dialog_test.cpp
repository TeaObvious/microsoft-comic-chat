#include "chat.h"
#include "colordlg.h"
#include "format.h"
#include "originalassets.h"
#include "proppage.h"
#include "resource.h"
#include "textcore.h"
#include "txtfntdg.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDialog>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}

#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

class DluMapper
{
public:
    explicit DluMapper(const QFont& font)
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

private:
    int m_baseX = 1;
    int m_baseY = 1;
};

const OriginalDialogControl* resourceControl(
    const OriginalDialogResource& dialog, const QString& identifier,
    int occurrence = 0)
{
    for (const OriginalDialogControl& control : dialog.controls) {
        if (control.identifier != identifier) continue;
        if (occurrence-- == 0) return &control;
    }
    return nullptr;
}

QRect resourceRect(const OriginalDialogControl& control, const QFont& font)
{
    const DluMapper mapper(font);
    return {mapper.x(control.x), mapper.y(control.y),
            mapper.x(control.width), mapper.y(control.height)};
}

QStringList expectedMessageTypes()
{
    const QString source = originalResourceString(
        QStringLiteral("IDS_MESSAGETYPES"))
        + originalResourceString(QStringLiteral("IDS_MESSAGETYPES2"));
    QStringList result;
    for (const QString& part : source.split(
             QLatin1Char(';'), Qt::SkipEmptyParts))
        result.append(part.trimmed());
    return result;
}

COLORREF differentSourceColor(COLORREF current, int start = 0)
{
    for (int offset = 0; offset < 16; ++offset) {
        const COLORREF candidate = clrTable[(start + offset) % 16];
        if (candidate != current) return candidate;
    }
    return current;
}

int colorIndex(QComboBox* combo, COLORREF color)
{
    return combo->findData(QVariant::fromValue<quint32>(color));
}

void sendTab(QWidget* widget)
{
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(widget, &release);
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.InitializeFonts();

    {
        const QFont before = theApp.m_comicsFont;
        QTimer::singleShot(0, [] {
            auto* dialog = qobject_cast<QFontDialog*>(
                QApplication::activeModalWidget());
            REQUIRE(dialog != nullptr);
            dialog->reject();
        });
        SetComicsFont();
        REQUIRE(theApp.m_comicsFont == before);

        QFont selected(originalResourceString(
            QStringLiteral("ID_COMIC_FONT_NAME")));
        selected.setPointSize(18);
        QTimer::singleShot(0, [selected] {
            auto* dialog = qobject_cast<QFontDialog*>(
                QApplication::activeModalWidget());
            REQUIRE(dialog != nullptr);
            dialog->setCurrentFont(selected);
            dialog->accept();
        });
        SetComicsFont();
        REQUIRE(theApp.m_comicsFont.family()
                == originalResourceString(
                    QStringLiteral("ID_COMIC_FONT_NAME")));
        REQUIRE(theApp.m_comicsFont.pixelSize() == 18 * 20);
    }

    REQUIRE(sizeof(LOGFONT) == 60);
    REQUIRE(sizeof(CHARFORMAT) == 60);
    REQUIRE(IDD_TEXTFONTPAGE_IRC == 224);

    {
        LOGFONT source{};
        source.lfHeight = 16;
        source.lfWeight = 700;
        source.lfItalic = TRUE;
        source.lfUnderline = TRUE;
        source.lfStrikeOut = TRUE;
        source.lfCharSet = ANSI_CHARSET;
        source.lfPitchAndFamily = 34;
        const QByteArray face = QApplication::font().family().toLatin1();
        REQUIRE(!face.isEmpty() && face.size() < LF_FACESIZE);
        std::memcpy(source.lfFaceName, face.constData(),
                    static_cast<size_t>(face.size()));

        CHARFORMAT format{};
        REQUIRE(bLOGFONTToCHARFORMAT(&source, clrTable[6], 0, &format));
        REQUIRE(format.cbSize == sizeof(CHARFORMAT));
        REQUIRE((format.dwMask & (CFM_FACE | CFM_SIZE | CFM_COLOR
                                  | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE
                                  | CFM_STRIKEOUT | CFM_CHARSET))
                == (CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_BOLD
                    | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT
                    | CFM_CHARSET));
        REQUIRE((format.dwEffects & (CFE_BOLD | CFE_ITALIC
                                     | CFE_UNDERLINE | CFE_STRIKEOUT))
                == (CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE
                    | CFE_STRIKEOUT));
        REQUIRE(format.crTextColor == clrTable[6]);
        REQUIRE(format.bCharSet == ANSI_CHARSET);

        LOGFONT roundTrip{};
        REQUIRE(bCHARFORMATToLOGFONT(&format, format.dwMask, &roundTrip));
        REQUIRE(roundTrip.lfWeight == 700);
        REQUIRE(roundTrip.lfItalic && roundTrip.lfUnderline
                && roundTrip.lfStrikeOut);
        REQUIRE(roundTrip.lfCharSet == ANSI_CHARSET);
        REQUIRE(std::strncmp(roundTrip.lfFaceName, source.lfFaceName,
                             LF_FACESIZE) == 0);
    }

    {
        const BOOL regularBefore = theApp.m_bCfInitialized;
        const BOOL highlightBefore = theApp.m_bCfHLInitialized;
        std::array<CHARFORMAT, NFONTS> appBefore{};
        std::memcpy(appBefore.data(), theApp.m_cfArray,
                    sizeof(theApp.m_cfArray));

        std::array<CHARFORMAT, NFONTS> exact{};
        const QByteArray face = QApplication::font().family().toLatin1();
        REQUIRE(!face.isEmpty() && face.size() < LF_FACESIZE);
        for (int index = 0; index < NFONTS; ++index) {
            CHARFORMAT& format = exact[index];
            format.cbSize = sizeof(CHARFORMAT);
            format.dwMask = CFM_FACE | CFM_SIZE | CFM_OFFSET | CFM_COLOR
                | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE
                | CFM_STRIKEOUT | CFM_CHARSET;
            if (index & 1) format.dwEffects |= CFE_BOLD | CFE_AUTOCOLOR;
            if (index & 2) format.dwEffects |= CFE_ITALIC;
            if (index & 4) format.dwEffects |= CFE_UNDERLINE;
            if (index & 8) format.dwEffects |= CFE_STRIKEOUT;
            format.yHeight = (8 + index % 5) * 20;
            format.yOffset = index - NFONTS / 2;
            format.crTextColor = clrTable[index % 16];
            format.bCharSet = index & 1 ? ANSI_CHARSET : SYMBOL_CHARSET;
            format.bPitchAndFamily = static_cast<BYTE>(index);
            std::memcpy(format.szFaceName, face.constData(),
                        static_cast<size_t>(face.size()));
        }
        std::memcpy(theApp.m_cfArray, exact.data(), sizeof(exact));
        theApp.m_bCfInitialized = TRUE;
        theApp.m_bCfHLInitialized = TRUE;

        CMyFontDialog dialog;
        dialog.show();
        application.processEvents();
        dialog.accept();
        REQUIRE(std::memcmp(dialog.m_cfArray, exact.data(), sizeof(exact)) == 0);

        CMyFontDialog changedDialog;
        changedDialog.show();
        application.processEvents();
        auto* messageTypes = changedDialog.findChild<QComboBox*>(
            QStringLiteral("IDC_MESSAGETYPE"));
        auto* color = changedDialog.findChild<QComboBox*>(
            QStringLiteral("1139"));
        REQUIRE(messageTypes && color);
        messageTypes->setCurrentIndex(3);
        const COLORREF changedColor = differentSourceColor(
            exact[2].crTextColor, 7);
        color->setCurrentIndex(colorIndex(color, changedColor));
        REQUIRE(color->currentIndex() >= 0);
        changedDialog.OnFontChange();
        changedDialog.accept();
        REQUIRE(changedDialog.m_cfArray[2].crTextColor == changedColor);
        for (int index = 0; index < NFONTS; ++index) {
            if (index == 2) continue;
            REQUIRE(std::memcmp(&changedDialog.m_cfArray[index],
                                &exact[index], sizeof(CHARFORMAT)) == 0);
        }

        std::memcpy(theApp.m_cfArray, appBefore.data(),
                    sizeof(theApp.m_cfArray));
        theApp.m_bCfInitialized = regularBefore;
        theApp.m_bCfHLInitialized = highlightBefore;
    }

    {
        const std::array<CHARFORMAT, NFONTS> appFormatsBefore = [&] {
            std::array<CHARFORMAT, NFONTS> result{};
            std::memcpy(result.data(), theApp.m_cfArray,
                        sizeof(theApp.m_cfArray));
            return result;
        }();
        const BOOL regularFlagBefore = theApp.m_bCfInitialized;
        const BOOL highlightFlagBefore = theApp.m_bCfHLInitialized;

        CMyFontDialog dialog;
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_SETTEXTFONT"));
        const DluMapper mapper(dialog.font());
        REQUIRE(resource.width == 264 && resource.height == 261);
        REQUIRE(dialog.objectName() == QStringLiteral("IDD_SETTEXTFONT"));
        REQUIRE(dialog.windowTitle() == resource.caption);
        REQUIRE(dialog.layout() == nullptr);
        REQUIRE(dialog.size() == QSize(mapper.x(resource.width),
                                       mapper.y(resource.height)));

        auto* messageTypes = dialog.findChild<QComboBox*>(
            QStringLiteral("IDC_MESSAGETYPE"));
        auto* preview = dialog.findChild<QTextEdit*>(QStringLiteral("5"));
        auto* face = dialog.findChild<QWidget*>(QStringLiteral("1136"));
        auto* style = dialog.findChild<QWidget*>(QStringLiteral("1137"));
        auto* pointSize = dialog.findChild<QWidget*>(QStringLiteral("1138"));
        auto* color = dialog.findChild<QComboBox*>(QStringLiteral("1139"));
        auto* script = dialog.findChild<QComboBox*>(QStringLiteral("1140"));
        auto* strikeout = dialog.findChild<QCheckBox*>(QStringLiteral("1040"));
        auto* underline = dialog.findChild<QCheckBox*>(QStringLiteral("1041"));
        auto* ok = dialog.findChild<QPushButton*>(QStringLiteral("IDOK"));
        auto* cancel = dialog.findChild<QPushButton*>(QStringLiteral("IDCANCEL"));
        auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("1026"));
        auto* help = dialog.findChild<QPushButton*>(QStringLiteral("1038"));
        REQUIRE(messageTypes && preview && face && style && pointSize
                && color && script && strikeout && underline && ok && cancel
                && apply && help);
        REQUIRE(messageTypes->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("IDC_MESSAGETYPE")),
            dialog.font()));
        REQUIRE(face->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("1136")),
            dialog.font()));
        REQUIRE(color->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("1139")),
            dialog.font()));
        REQUIRE(preview->geometry()
                == QRect(mapper.x(5), mapper.y(20),
                         mapper.x(251), mapper.y(120)));
        REQUIRE(preview->isReadOnly());
        REQUIRE(preview->focusPolicy() == Qt::ClickFocus);
        REQUIRE(strikeout->isTristate() && underline->isTristate());
        REQUIRE(color->count() == 16);

        std::array<QListWidget*, 3> simpleLists{};
        int simpleIndex = 0;
        for (QWidget* simple : {face, style, pointSize}) {
            auto* editor = simple->findChild<QLineEdit*>(
                simple->objectName() + QStringLiteral(".Edit"));
            auto* list = simple->findChild<QListWidget*>(
                simple->objectName() + QStringLiteral(".List"));
            REQUIRE(editor != nullptr);
            REQUIRE(list != nullptr);
            REQUIRE(list->focusPolicy() == Qt::NoFocus);
            REQUIRE(resourceControl(resource, simple->objectName())
                        ->style.contains(QStringLiteral("CBS_SIMPLE")));
            simpleLists[static_cast<size_t>(simpleIndex++)] = list;
        }

        for (const QString& identifier : {
                 QStringLiteral("1088"), QStringLiteral("1089"),
                 QStringLiteral("1090"), QStringLiteral("1091"),
                 QStringLiteral("1094")}) {
            auto* label = dialog.findChild<QLabel*>(identifier);
            REQUIRE(label != nullptr);
            REQUIRE(label->buddy() != nullptr);
        }

        const QStringList expectedTypes = expectedMessageTypes();
        REQUIRE(expectedTypes.size() == NFONTS + 1);
        REQUIRE(messageTypes->count() == expectedTypes.size());
        for (int index = 0; index < expectedTypes.size(); ++index)
            REQUIRE(messageTypes->itemText(index) == expectedTypes[index]);

        dialog.show();
        application.processEvents();
        REQUIRE(!apply->isVisible());
        REQUIRE(!help->isVisible());
        for (QListWidget* list : simpleLists) {
            REQUIRE(list->isVisible());
            REQUIRE(list->geometry().top() > 0);
            REQUIRE(list->geometry().bottom()
                    == list->parentWidget()->rect().bottom());
        }
        auto* faceEditor = face->findChild<QLineEdit*>(
            QStringLiteral("1136.Edit"));
        auto* styleEditor = style->findChild<QLineEdit*>(
            QStringLiteral("1137.Edit"));
        REQUIRE(faceEditor != nullptr);
        REQUIRE(styleEditor != nullptr);
        messageTypes->setFocus();
        application.processEvents();
        REQUIRE(QApplication::focusWidget() == messageTypes);
        sendTab(messageTypes);
        REQUIRE(QApplication::focusWidget() == faceEditor);
        sendTab(faceEditor);
        REQUIRE(QApplication::focusWidget() == styleEditor);
        const QString previewText = preview->toPlainText();
        REQUIRE(previewText.contains(originalResourceString(
            QStringLiteral("IDS_SAMPLE_SEND"))));
        REQUIRE(previewText.contains(originalResourceString(
            QStringLiteral("IDS_SAMPLE_WHISPER"))));
        REQUIRE(previewText.contains(originalResourceString(
            QStringLiteral("IDS_SAMPLE_THOUGHT"))));
        REQUIRE(previewText.contains(originalResourceString(
            QStringLiteral("IDS_SAMPLE_ACTION"))));
        REQUIRE(previewText.contains(originalResourceString(
            QStringLiteral("IDS_SAMPLE_NICK"))));
        for (int index = 1; index <= NHIGHLIGHTEDFONTS / 2; ++index) {
            QString sample = originalResourceString(
                QStringLiteral("IDS_SAMPLE_HIGHLIGHT"));
            sample.replace(QStringLiteral("%d"), QString::number(index));
            REQUIRE(previewText.contains(sample));
        }
        for (const CHARFORMAT& format : dialog.m_cfArray)
            REQUIRE(format.cbSize == sizeof(CHARFORMAT));

        std::array<CHARFORMAT, NFONTS> before{};
        std::memcpy(before.data(), dialog.m_cfArray, sizeof(before));
        messageTypes->setCurrentIndex(3);
        application.processEvents();
        const COLORREF selectedColor = differentSourceColor(
            dialog.m_cfArray[2].crTextColor, 5);
        const int selectedColorIndex = colorIndex(color, selectedColor);
        REQUIRE(selectedColorIndex >= 0);
        color->setCurrentIndex(selectedColorIndex);
        dialog.OnFontChange();
        REQUIRE(dialog.m_cfArray[2].dwMask & CFM_COLOR);
        REQUIRE(dialog.m_cfArray[2].crTextColor == selectedColor);
        for (int index = 0; index < NFONTS; ++index) {
            if (index == 2) continue;
            REQUIRE(std::memcmp(&dialog.m_cfArray[index], &before[index],
                                sizeof(CHARFORMAT)) == 0);
        }

        messageTypes->setCurrentIndex(0);
        application.processEvents();
        const COLORREF allColor = differentSourceColor(selectedColor, 10);
        const int allColorIndex = colorIndex(color, allColor);
        REQUIRE(allColorIndex >= 0);
        color->setCurrentIndex(allColorIndex);
        dialog.OnFontChange();
        for (const CHARFORMAT& format : dialog.m_cfArray) {
            REQUIRE(format.dwMask & CFM_COLOR);
            REQUIRE(format.crTextColor == allColor);
        }

        {
            const QSignalBlocker blocked(strikeout);
            strikeout->setCheckState(Qt::PartiallyChecked);
        }
        strikeout->click();
        REQUIRE(strikeout->checkState() == Qt::Unchecked);
        for (const CHARFORMAT& format : dialog.m_cfArray) {
            REQUIRE(format.dwMask & CFM_STRIKEOUT);
            REQUIRE(!(format.dwEffects & CFE_STRIKEOUT));
        }
        strikeout->click();
        REQUIRE(strikeout->checkState() == Qt::Checked);
        for (const CHARFORMAT& format : dialog.m_cfArray)
            REQUIRE(format.dwEffects & CFE_STRIKEOUT);

        dialog.reject();
        REQUIRE(theApp.m_bCfInitialized == regularFlagBefore);
        REQUIRE(theApp.m_bCfHLInitialized == highlightFlagBefore);
        REQUIRE(std::memcmp(theApp.m_cfArray, appFormatsBefore.data(),
                            sizeof(theApp.m_cfArray)) == 0);
    }

    {
        theApp.m_bCfInitialized = FALSE;
        theApp.m_bCfHLInitialized = FALSE;
        const COLORREF acceptedColor = clrTable[3];
        QTimer::singleShot(0, [acceptedColor] {
            auto* dialog = dynamic_cast<CMyFontDialog*>(
                QApplication::activeModalWidget());
            REQUIRE(dialog != nullptr);
            auto* messageTypes = dialog->findChild<QComboBox*>(
                QStringLiteral("IDC_MESSAGETYPE"));
            auto* color = dialog->findChild<QComboBox*>(
                QStringLiteral("1139"));
            REQUIRE(messageTypes && color);
            messageTypes->setCurrentIndex(3);
            color->setCurrentIndex(colorIndex(color, acceptedColor));
            REQUIRE(color->currentIndex() >= 0);
            dialog->OnFontChange();
            dialog->accept();
        });
        SetTextFont();
        REQUIRE(theApp.m_bCfInitialized);
        REQUIRE(theApp.m_bCfHLInitialized);
        REQUIRE(theApp.m_cfArray[2].dwMask & CFM_COLOR);
        REQUIRE(theApp.m_cfArray[2].crTextColor == acceptedColor);
        REQUIRE(theApp.m_textColor == acceptedColor);
    }

    {
        theApp.m_flags1 &= ~DWORD(F1_RTFCOMIC);
        theApp.m_bAutoDownloadAvatars = false;
        theApp.m_bAutoDownloadBackdrops = true;
        CComicsPropPage page;
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_COMICS_VIEW"));
        const DluMapper mapper(page.font());
        REQUIRE(page.objectName() == QStringLiteral("IDD_COMICS_VIEW"));
        REQUIRE(page.layout() == nullptr);
        REQUIRE(page.size() == QSize(mapper.x(resource.width),
                                     mapper.y(resource.height)));

        auto* setFont = page.findChild<QPushButton*>(
            QStringLiteral("ID_SETFONT"));
        auto* reset = page.findChild<QPushButton*>(
            QStringLiteral("ID_RESET_TEXTFONTS"));
        auto* richText = page.findChild<QCheckBox*>(
            QStringLiteral("IDC_SHOWCOMICRTF"));
        auto* panels = page.findChild<QComboBox*>(
            QStringLiteral("IDC_PANELS"));
        auto* characters = page.findChild<QCheckBox*>(
            QStringLiteral("IDC_AUTODOWNLOAD_CHARS"));
        auto* backdrops = page.findChild<QCheckBox*>(
            QStringLiteral("IDC_AUTODOWNLOAD_BACKDROPS"));
        REQUIRE(setFont && reset && richText && panels
                && characters && backdrops);
        REQUIRE(setFont->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("ID_SETFONT")),
            page.font()));
        REQUIRE(panels->count() == 4);
        REQUIRE(panels->itemText(0) == originalResourceString(
            QStringLiteral("IDS_1_WIDE")));
        REQUIRE(!richText->isChecked());
        REQUIRE(!characters->isChecked());
        REQUIRE(backdrops->isChecked());

        richText->click();
        characters->click();
        backdrops->click();
        page.apply();
        REQUIRE(theApp.m_flags1 & F1_RTFCOMIC);
        REQUIRE(theApp.m_bAutoDownloadAvatars);
        REQUIRE(!theApp.m_bAutoDownloadBackdrops);

        reset->click();
        REQUIRE(theApp.m_comicsFont.family()
                == originalResourceString(
                    QStringLiteral("ID_COMIC_FONT_NAME")));
        REQUIRE(theApp.m_comicsFont.pixelSize()
                == originalResourceString(
                    QStringLiteral("IDS_DFLT_COMICSPNTSIZE")).toInt() * 20);
    }

    {
        theApp.m_textSpacing = TEXT_VIEW_BLANK_DIFFTYPES;
        theApp.m_iHostHighlight = HH_BOLD_HEADERS;
        theApp.m_flags1 &= ~DWORD(F1_HEADERSEPARATE);
        CTextFontPage page;
        const OriginalDialogResource resource = originalDialogResource(
            QStringLiteral("IDD_TEXTFONTPAGE_IRC"));
        const DluMapper mapper(page.font());
        REQUIRE(page.objectName() == QStringLiteral("IDD_TEXTFONTPAGE_IRC"));
        REQUIRE(page.layout() == nullptr);
        REQUIRE(page.size() == QSize(mapper.x(resource.width),
                                     mapper.y(resource.height)));

        auto* all = page.findChild<QRadioButton*>(
            QStringLiteral("IDC_LINESPACE_ALL"));
        auto* different = page.findChild<QRadioButton*>(
            QStringLiteral("IDC_LINESPACE_DIFFERENT"));
        auto* none = page.findChild<QRadioButton*>(
            QStringLiteral("IDC_LINESPACE_NONE"));
        auto* separate = page.findChild<QCheckBox*>(
            QStringLiteral("IDC_CHKHEADERSEPARATE"));
        auto* hostHeaders = page.findChild<QCheckBox*>(
            QStringLiteral("IDC_HOST_HDRS_BOLD"));
        auto* hostMessages = page.findChild<QCheckBox*>(
            QStringLiteral("IDC_HOST_MSGS_BOLD"));
        auto* setFont = page.findChild<QPushButton*>(
            QStringLiteral("ID_SETFONT"));
        auto* reset = page.findChild<QPushButton*>(
            QStringLiteral("ID_RESET_TEXTFONTS"));
        REQUIRE(all && different && none && separate && hostHeaders
                && hostMessages && setFont && reset);
        REQUIRE(different->isChecked());
        REQUIRE(!separate->isChecked());
        REQUIRE(hostHeaders->isChecked());
        REQUIRE(!hostMessages->isChecked());
        REQUIRE(setFont->geometry() == resourceRect(
            *resourceControl(resource, QStringLiteral("ID_SETFONT")),
            page.font()));

        all->click();
        separate->click();
        hostHeaders->click();
        hostMessages->click();
        page.apply();
        REQUIRE(theApp.m_textSpacing == TEXT_VIEW_BLANK_ALWAYS);
        REQUIRE(theApp.m_flags1 & F1_HEADERSEPARATE);
        REQUIRE((theApp.m_iHostHighlight
                 & (HH_BOLD_HEADERS | HH_BOLD_MESSAGES))
                == HH_BOLD_MESSAGES);
        REQUIRE(theApp.m_bCfInitialized && theApp.m_bCfHLInitialized);

        reset->click();
        page.apply();
        REQUIRE(!theApp.m_bCfInitialized);
        REQUIRE(!theApp.m_bCfHLInitialized);
    }

    {
        COptionsDialog textOptions(FALSE, IDD_TEXTFONTPAGE_IRC);
        QTabWidget* tabs = textOptions.findChild<QTabWidget*>();
        REQUIRE(tabs != nullptr);
        const QString textCaption = originalDialogCaption(
            QStringLiteral("IDD_TEXTFONTPAGE_IRC"));
        QWidget* textPage = tabs->findChild<QWidget*>(
            QStringLiteral("IDD_TEXTFONTPAGE_IRC"));
        REQUIRE(textPage != nullptr);
        REQUIRE(tabs->indexOf(textPage) >= 0);
        REQUIRE(tabs->tabText(tabs->currentIndex()) == textCaption);

        COptionsDialog comicOptions(TRUE, IDD_COMICS_VIEW);
        tabs = comicOptions.findChild<QTabWidget*>();
        REQUIRE(tabs != nullptr);
        const QString comicsCaption = originalDialogCaption(
            QStringLiteral("IDD_COMICS_VIEW"));
        QWidget* comicsPage = tabs->findChild<QWidget*>(
            QStringLiteral("IDD_COMICS_VIEW"));
        REQUIRE(comicsPage != nullptr);
        REQUIRE(tabs->indexOf(comicsPage) >= 0);
        REQUIRE(tabs->tabText(tabs->currentIndex()) == comicsCaption);
        for (int index = 0; index < tabs->count(); ++index)
            REQUIRE(tabs->tabText(index) != textCaption);
    }

    return 0;
}
