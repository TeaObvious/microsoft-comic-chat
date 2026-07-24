#include "chat.h"
#include "chatdoc.h"
#include "chatview.h"
#include "mainfrm.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "textview.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QtPrintSupport/QPrintDialog>
#include <QtPrintSupport/QPrinter>

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(int line)
{
    std::fprintf(stderr, "require failed at line %d\n", line);
    std::abort();
}

#define REQUIRE(condition) do { if (!(condition)) fail(__LINE__); } while (false)

void configurePdfPrinter(QPrinter& printer, const QString& outputFile,
                         int resolution = 96)
{
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(outputFile);
    printer.setResolution(resolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setFullPage(false);
    printer.setPrintRange(QPrinter::AllPages);
}

SIZE comicsPageSize(QPrinter& printer)
{
    const QRectF page = printer.pageRect(QPrinter::DevicePixel);
    return {
        static_cast<LONG>(page.width() * 1440.0 / printer.logicalDpiX()),
        static_cast<LONG>(page.height() * 1440.0 / printer.logicalDpiY())
            - 250 * 3 / 2
    };
}

void appendEmptyPanels(CUnitPanelPage* page, int count)
{
    for (int index = 0; index < count; ++index)
        page->m_panels.append(new CUnitPanel);
}

bool hasInk(const QImage& image, int left, int right)
{
    left = qBound(0, left, image.width());
    right = qBound(left, right, image.width());
    for (int y = 0; y < image.height(); ++y) {
        for (int x = left; x < right; ++x) {
            if (image.pixelColor(x, y) != QColor(Qt::white)) return true;
        }
    }
    return false;
}

int pdfPageCount(const QString& fileName)
{
    QFile file(fileName);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    REQUIRE(bytes.startsWith("%PDF-"));
    const QString contents = QString::fromLatin1(bytes);
    const QRegularExpression pageObject(
        QStringLiteral("/Type\\s*/Page(?!s)"));
    int pages = 0;
    auto matches = pageObject.globalMatch(contents);
    while (matches.hasNext()) {
        matches.next();
        ++pages;
    }
    return pages;
}

void requireFooterRegions(const QImage& image)
{
    REQUIRE(hasInk(image, 0, 600));
    REQUIRE(hasInk(image, 850, 1550));
    REQUIRE(hasInk(image, 1650, image.width()));
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();
    theApp.InitializeFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont,
                                     theApp.m_comicsColor));
    CUnitPanelPage::SetUnitPanelWidth(MINUNITPANELWIDTH);
    CUnitPanelPage::SetUnitPanelHeight(MINUNITPANELHEIGHT);
    CUnitPanelPage::SetUnitPanelsPerRow(2);

    // Directly exercise the equations and page index established by the
    // original CUnitPanelPage::PreparePrintDC.
    CUnitPanelPage layoutPage;
    const SIZE twoByTwo{
        2 * CUnitPanelPage::m_unitWidth
            + 2 * CUnitPanelPage::m_hInterstice
            - CUnitPanelPage::m_vInterstice,
        2 * CUnitPanelPage::m_unitHeight
            + CUnitPanelPage::m_hInterstice
    };
    int panelsWide = 0;
    int panelsHigh = 0;
    layoutPage.PageSizeInPanels(twoByTwo, panelsWide, panelsHigh);
    REQUIRE(panelsWide == 2);
    REQUIRE(panelsHigh == 2);
    appendEmptyPanels(&layoutPage, 9);
    REQUIRE(layoutPage.GetPhysicalPageCount(twoByTwo) == 3);
    const CUnitPanelPrintInfo second = layoutPage.PreparePrintDC(twoByTwo, 2);
    REQUIRE(second.m_panelsWide == 2);
    REQUIRE(second.m_panelsHigh == 2);
    REQUIRE(second.m_startPanelRow == 2);
    REQUIRE(second.m_firstPanel == 4);
    REQUIRE(CUnitPanelPage::m_printPanelsPerRow == 2);
    REQUIRE(second.m_clipRect.right - second.m_clipRect.left
        == 2 * CUnitPanelPage::m_unitWidth
            + CUnitPanelPage::m_vInterstice);
    REQUIRE(second.m_clipRect.top - second.m_clipRect.bottom
        == 2 * CUnitPanelPage::m_unitHeight
            + CUnitPanelPage::m_hInterstice);

    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());

    {
        QPrinter highResolutionReference(QPrinter::HighResolution);
        CMainFrame frame;
        REQUIRE(frame.GetPrinter() != nullptr);
        REQUIRE(frame.GetPrinter()->resolution()
                == highResolutionReference.resolution());
        const int resolution = frame.GetPrinter()->resolution();
        theApp.SetPrinterResolution(frame.GetPrinter());
        REQUIRE(frame.GetPrinter()->resolution() == resolution);
    }

    CChatDoc document;
    document.m_bComicView = true;
    document.SetComicsTitle(originalResourceString(
        QStringLiteral("IDS_NOTITLE")));
    SetChatDoc(&document);
    CChatView chatView(&document);
    REQUIRE(document.m_view != nullptr);

    const QString comicPdf = temporary.filePath(QStringLiteral("comic.pdf"));
    QPrinter comicPrinter(QPrinter::HighResolution);
    configurePdfPrinter(comicPrinter, comicPdf);

    const QString setupPrinterName = comicPrinter.printerName();
    const QPrinter::OutputFormat setupOutputFormat =
        comicPrinter.outputFormat();
    const QString setupOutputFileName = comicPrinter.outputFileName();
    const int setupResolution = comicPrinter.resolution();
    const QPageLayout setupPageLayout = comicPrinter.pageLayout();
    const bool setupFullPage = comicPrinter.fullPage();
    const QPrinter::ColorMode setupColorMode = comicPrinter.colorMode();
    const QPrinter::DuplexMode setupDuplex = comicPrinter.duplex();
    const int setupCopyCount = comicPrinter.copyCount();
    const bool setupCollateCopies = comicPrinter.collateCopies();
    const QPrinter::PageOrder setupPageOrder = comicPrinter.pageOrder();
    const QPrinter::PrintRange setupPrintRange = comicPrinter.printRange();
    const int setupFromPage = comicPrinter.fromPage();
    const int setupToPage = comicPrinter.toPage();

    bool sawPrintSetup = false;
    QTimer::singleShot(0, &application, [&sawPrintSetup] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QPrintDialog*>(widget);
            if (!dialog
                || dialog->objectName() != QLatin1String("CPrintDialog")) {
                continue;
            }
            sawPrintSetup = true;
            REQUIRE(!dialog->testOption(
                QAbstractPrintDialog::PrintPageRange));
            REQUIRE(!dialog->testOption(
                QAbstractPrintDialog::PrintSelection));
            REQUIRE(!dialog->testOption(
                QAbstractPrintDialog::PrintCurrentPage));
            dialog->reject();
        }
    });
    theApp.OnFilePrintSetup(&comicPrinter, nullptr);
    REQUIRE(sawPrintSetup);
    REQUIRE(comicPrinter.printerName() == setupPrinterName);
    REQUIRE(comicPrinter.outputFormat() == setupOutputFormat);
    REQUIRE(comicPrinter.outputFileName() == setupOutputFileName);
    REQUIRE(comicPrinter.resolution() == setupResolution);
    REQUIRE(comicPrinter.pageLayout() == setupPageLayout);
    REQUIRE(comicPrinter.fullPage() == setupFullPage);
    REQUIRE(comicPrinter.colorMode() == setupColorMode);
    REQUIRE(comicPrinter.duplex() == setupDuplex);
    REQUIRE(comicPrinter.copyCount() == setupCopyCount);
    REQUIRE(comicPrinter.collateCopies() == setupCollateCopies);
    REQUIRE(comicPrinter.pageOrder() == setupPageOrder);
    REQUIRE(comicPrinter.printRange() == setupPrintRange);
    REQUIRE(comicPrinter.fromPage() == setupFromPage);
    REQUIRE(comicPrinter.toPage() == setupToPage);

    document.DestroyPages();
    REQUIRE(document.m_view->OnPreparePrinting(&comicPrinter));
    const SIZE printSize = comicsPageSize(comicPrinter);
    CUnitPanelPage capacityProbe;
    capacityProbe.PageSizeInPanels(printSize, panelsWide, panelsHigh);
    REQUIRE(panelsWide > 0 && panelsHigh > 0);
    const int capacity = panelsWide * panelsHigh;

    document.DestroyPages();
    auto* firstLogicalPage = new CUnitPanelPage(&document);
    auto* secondLogicalPage = new CUnitPanelPage(&document);
    appendEmptyPanels(firstLogicalPage, capacity + 1);
    appendEmptyPanels(secondLogicalPage, 1);
    document.m_pages.append(firstLogicalPage);
    document.m_pages.append(secondLogicalPage);
    REQUIRE(document.m_view->GetPhysicalPageCount(&comicPrinter) == 3);
    REQUIRE(chatView.GetPhysicalPageCount(&comicPrinter) == 3);
    REQUIRE(chatView.Print(&comicPrinter));
    REQUIRE(QFile::exists(comicPdf));
    REQUIRE(pdfPageCount(comicPdf) == 3);

    QImage comicFooter(2400, 400, QImage::Format_RGB32);
    comicFooter.fill(Qt::white);
    document.m_view->OnBeginPrinting(&comicPrinter);
    REQUIRE(document.m_view->GetPrintRetainedPanel() != nullptr);
    REQUIRE(document.m_view->GetPrintRetainedPanel()->format()
        == QImage::Format_RGB888);
    {
        QPainter painter(&comicFooter);
        document.m_view->PrintFooter(&painter, comicFooter.rect(), 2,
                                     96.0, 96.0);
    }
    document.m_view->OnEndPrinting(&comicPrinter);
    REQUIRE(document.m_view->GetPrintRetainedPanel() == nullptr);
    requireFooterRegions(comicFooter);

    chatView.CreateTextView(false);
    CTextView* textView = document.m_textView;
    REQUIRE(textView != nullptr);
    const QString appTitle = originalResourceString(
        QStringLiteral("AFX_IDS_APP_TITLE"));
    const QString pageFooter = originalResourceString(
        QStringLiteral("IDS_PAGEFOOTER"));
    REQUIRE(!appTitle.isEmpty() && !pageFooter.isEmpty());

    textView->m_pRichEdit->clear();
    QTextCursor rtfCursor = textView->m_pRichEdit->textCursor();
    QTextBlockFormat visibleBlockFormat = rtfCursor.blockFormat();
    visibleBlockFormat.setLeftMargin(
        (DEFAULT_INDENT + 72)
        * qMax(1, textView->m_pRichEdit->logicalDpiX())
        / 1440.0);
    rtfCursor.setBlockFormat(visibleBlockFormat);
    rtfCursor.insertText(appTitle);
    textView->m_pRichEdit->setTextCursor(rtfCursor);
    const qreal visibleLeftMargin =
        textView->m_pRichEdit->document()->begin()
            .blockFormat().leftMargin();
    REQUIRE(visibleLeftMargin > 0.0);
    const QString rtfPath =
        temporary.filePath(QStringLiteral("visible-transcript.rtf"));
    REQUIRE(WriteRTF(textView->m_pRichEdit, rtfPath));
    QFile rtfFile(rtfPath);
    REQUIRE(rtfFile.open(QIODevice::ReadOnly));
    const QByteArray rtfBytes = rtfFile.readAll();
    const int expectedLeftIndent = qRound(
        visibleLeftMargin * 1440.0
        / qMax(1, textView->m_pRichEdit->logicalDpiX()));
    REQUIRE(rtfBytes.contains(
        QByteArray("\\li") + QByteArray::number(expectedLeftIndent)));

    textView->m_pRichEdit->clear();
    QTextCursor cursor(textView->m_pRichEdit->document());
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    for (int line = 0; line < 600; ++line) {
        cursor.insertText((line & 1) ? appTitle : pageFooter, bold);
        cursor.insertBlock();
    }
    const QString originalText = textView->m_pRichEdit->toPlainText();

    const QString textPdf = temporary.filePath(QStringLiteral("text.pdf"));
    QPrinter textPrinter(QPrinter::HighResolution);
    configurePdfPrinter(textPrinter, textPdf);

    bool sawCancelledPrintDialog = false;
    QTimer::singleShot(0, &application, [&sawCancelledPrintDialog] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* dialog = qobject_cast<QPrintDialog*>(widget)) {
                sawCancelledPrintDialog = true;
                dialog->reject();
            }
        }
    });
    REQUIRE(!chatView.OnFilePrint(&textPrinter, FALSE));
    REQUIRE(sawCancelledPrintDialog);
    REQUIRE(textView->m_printDocument == nullptr);

    const int textPages = static_cast<int>(textView->lPrintPage(
        &textPrinter, nullptr, 0, FALSE));
    REQUIRE(textPages > 1);
    REQUIRE(textView->m_printDocument != nullptr);
    REQUIRE(textView->m_printDocument->documentLayout()->paintDevice()
        == &textPrinter);
    QTextCursor printFirst(textView->m_printDocument);
    printFirst.movePosition(QTextCursor::NextCharacter,
                            QTextCursor::KeepAnchor);
    REQUIRE(printFirst.charFormat().fontWeight() == QFont::Bold);

    QPrinter highDpiTextPrinter(QPrinter::HighResolution);
    configurePdfPrinter(highDpiTextPrinter,
                        temporary.filePath(QStringLiteral("text-high-dpi.pdf")),
                        300);
    const int highDpiTextPages = static_cast<int>(textView->lPrintPage(
        &highDpiTextPrinter, nullptr, 0, FALSE));
    REQUIRE(textView->m_printDocument->documentLayout()->paintDevice()
        == &highDpiTextPrinter);
    REQUIRE(qAbs(highDpiTextPages - textPages) <= 1);

    REQUIRE(chatView.GetPhysicalPageCount(&textPrinter) == textPages);

    QImage textFooter(2400, 400, QImage::Format_RGB32);
    textFooter.fill(Qt::white);
    textView->OnBeginPrinting(&textPrinter);
    {
        QPainter painter(&textFooter);
        textView->PrintFooter(&painter, textFooter.rect(), 2);
    }
    textView->OnEndPrinting(&textPrinter);
    requireFooterRegions(textFooter);

    REQUIRE(chatView.Print(&textPrinter));
    REQUIRE(textView->m_pRichEdit->toPlainText() == originalText);
    REQUIRE(QFile::exists(textPdf));
    REQUIRE(pdfPageCount(textPdf) == textPages);

    SetChatDoc(nullptr);
    CUnitPanelPage::DestroyFonts();
    return 0;
}
