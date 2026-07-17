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
#include <QFontComboBox>
#include <QFontMetrics>
#include <QPushButton>
#include <QRadioButton>
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
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.InitializeFonts();

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
        auto* face = dialog.findChild<QFontComboBox*>(QStringLiteral("1136"));
        auto* style = dialog.findChild<QComboBox*>(QStringLiteral("1137"));
        auto* pointSize = dialog.findChild<QComboBox*>(QStringLiteral("1138"));
        auto* color = dialog.findChild<QComboBox*>(QStringLiteral("1139"));
        auto* script = dialog.findChild<QComboBox*>(QStringLiteral("1140"));
        auto* strikeout = dialog.findChild<QCheckBox*>(QStringLiteral("1040"));
        auto* underline = dialog.findChild<QCheckBox*>(QStringLiteral("1041"));
        auto* ok = dialog.findChild<QPushButton*>(QStringLiteral("IDOK"));
        auto* cancel = dialog.findChild<QPushButton*>(QStringLiteral("IDCANCEL"));
        REQUIRE(messageTypes && preview && face && style && pointSize
                && color && script && strikeout && underline && ok && cancel);
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
        REQUIRE(strikeout->isTristate() && underline->isTristate());
        REQUIRE(color->count() == 16);

        const QStringList expectedTypes = expectedMessageTypes();
        REQUIRE(expectedTypes.size() == NFONTS + 1);
        REQUIRE(messageTypes->count() == expectedTypes.size());
        for (int index = 0; index < expectedTypes.size(); ++index)
            REQUIRE(messageTypes->itemText(index) == expectedTypes[index]);

        dialog.show();
        application.processEvents();
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

        COptionsDialog comicOptions(TRUE, IDD_CHARACTERPAGE);
        tabs = comicOptions.findChild<QTabWidget*>();
        REQUIRE(tabs != nullptr);
        for (int index = 0; index < tabs->count(); ++index)
            REQUIRE(tabs->tabText(index) != textCaption);
    }

    return 0;
}
