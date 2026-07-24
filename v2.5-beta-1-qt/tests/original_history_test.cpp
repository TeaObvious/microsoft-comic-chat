#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "histent.h"
#include "intl.h"
#include "ircproto.h"
#include "ircsock.h"
#include "memblst.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "userinfo.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail() { std::abort(); }
void requireAt(bool condition, int line)
{
    if (!condition) {
        std::fprintf(stderr, "require failed at line %d\n", line);
        fail();
    }
}
#define REQUIRE(condition) requireAt((condition), __LINE__)

QString saveConversation(const CChatDoc& document)
{
    QString conversation;
    QTextStream stream(&conversation, QIODevice::WriteOnly);
    document.ChatSaveConversation(stream);
    stream.flush();
    return conversation;
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(contents) == contents.size());
}

bool saveModifiedWithAnswer(CChatDoc& document,
                            QMessageBox::StandardButton answer)
{
    bool answered = false;
    QTimer::singleShot(0, [&answered, answer] {
        auto* prompt = qobject_cast<QMessageBox*>(
            QApplication::activeModalWidget());
        REQUIRE(prompt != nullptr);
        REQUIRE(prompt->objectName()
                == QLatin1String("CChatDocSaveModified"));
        answered = true;
        prompt->done(static_cast<int>(answer));
    });
    const bool result = document.SaveModified();
    REQUIRE(answered);
    return result;
}

class EnterInfoState {
public:
    EnterInfoState()
        : m_channel(g_enterInfo.m_strChannel)
        , m_prettyChannel(g_enterInfo.m_strPrettyChannel)
        , m_password(g_enterInfo.m_strPassword)
        , m_creationModes(g_enterInfo.m_strCreationModes)
        , m_topic(g_enterInfo.m_strTopic)
        , m_formatting(CopyFormatting(
              g_enterInfo.m_prgdwTopicFormatting))
        , m_modes(g_enterInfo.m_dwModes)
        , m_maximumUsers(g_enterInfo.m_dwMaxUsers)
        , m_setMode(g_enterInfo.m_bSetMode)
        , m_document(g_enterInfo.m_doc)
    {
    }

    ~EnterInfoState()
    {
        g_enterInfo.m_strChannel = m_channel;
        g_enterInfo.m_strPrettyChannel = m_prettyChannel;
        g_enterInfo.m_strPassword = m_password;
        g_enterInfo.m_strCreationModes = m_creationModes;
        g_enterInfo.m_strTopic = m_topic;
        FreeAndNullFormatting(&g_enterInfo.m_prgdwTopicFormatting);
        g_enterInfo.m_prgdwTopicFormatting = m_formatting;
        m_formatting = nullptr;
        g_enterInfo.m_dwModes = m_modes;
        g_enterInfo.m_dwMaxUsers = m_maximumUsers;
        g_enterInfo.m_bSetMode = m_setMode;
        g_enterInfo.m_doc = m_document;
    }

    EnterInfoState(const EnterInfoState&) = delete;
    EnterInfoState& operator=(const EnterInfoState&) = delete;

private:
    QString m_channel;
    QString m_prettyChannel;
    QString m_password;
    QString m_creationModes;
    QString m_topic;
    CDWordArray* m_formatting = nullptr;
    unsigned long m_modes = 0;
    unsigned long m_maximumUsers = 0;
    bool m_setMode = false;
    CChatDoc* m_document = nullptr;
};
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    theApp.InitVals();
    theApp.m_bComicView = true;
    theApp.InitializeComicsFonts();
    REQUIRE(CUnitPanelPage::SetFonts(theApp.m_comicsFont,
                                     theApp.m_comicsColor));
    InitializeBackDrops();
    InitializeAvatars();

    QString selfAvatarName;
    QString otherSourceName;
    GetNextAvatarName(selfAvatarName);
    GetNextAvatarName(otherSourceName);
    REQUIRE(!selfAvatarName.isEmpty());
    REQUIRE(!otherSourceName.isEmpty());
    REQUIRE(selfAvatarName.compare(otherSourceName, Qt::CaseInsensitive) != 0);
    theApp.m_myCharacterName = selfAvatarName;

    REQUIRE(CommunicationInits());
    QTemporaryDir temporaryDirectory;
    REQUIRE(temporaryDirectory.isValid());
    {
        CChatDoc document;
        SetChatDoc(&document);
        CMemberList members;
        document.m_memberList = &members;
        CPageView view;
        document.m_view = &view;

        const QString nick = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
        const QString channel = originalResourceString(QStringLiteral("IDS_DEFAULT_CHANNEL"));
        const QString user = QString::fromUtf8(GetMyUserName());
        serverConn.ProcessMessage(
            QStringLiteral(":%1!%2@127.0.0.1 JOIN :%3").arg(nick, user, channel));
        REQUIRE(document.m_history.size() == 2);
        REQUIRE(dynamic_cast<StartHistoryEntry*>(document.m_history.at(0)) != nullptr);
        REQUIRE(dynamic_cast<ChangeBackDropEntry*>(document.m_history.at(1)) != nullptr);
        REQUIRE(g_puiSelf == nullptr);

        serverConn.ProcessMessage(
            QStringLiteral("353 %1 = %2 :%1 %3").arg(nick, channel,
                                                       otherSourceName));
        REQUIRE(document.m_history.size() == 4);
        REQUIRE(dynamic_cast<JoinEntry*>(document.m_history.at(2)) != nullptr);
        REQUIRE(dynamic_cast<JoinEntry*>(document.m_history.at(3)) != nullptr);
        REQUIRE(g_puiSelf != nullptr);
        REQUIRE(document.m_allChannelPuis.size() == 2);

        serverConn.ProcessMessage(
            QStringLiteral("366 %1 %2").arg(nick, channel));
        REQUIRE(document.m_pages.first()->m_panels.first()->m_elements.size() == 6);

        AddAndExecute(new SayEntry(g_puiSelf, QStringLiteral("<Chr>"),
                                   NoFormattingSentinel()), &document);
        REQUIRE(document.m_history.size() == 5);
        REQUIRE(dynamic_cast<SayEntry*>(document.m_history.last()) != nullptr);

        REQUIRE(document.FindFileType(QStringLiteral("history.ccr"))
                == FT_CCR);
        REQUIRE(document.FindFileType(QStringLiteral("HISTORY.CCR"))
                == FT_CCR);
        REQUIRE(document.FindFileType(QStringLiteral("history.rtf"))
                == FT_RTF);
        REQUIRE(document.FindFileType(QStringLiteral("HISTORY.RTF"))
                == FT_RTF);
        REQUIRE(document.FindFileType(QStringLiteral("history.ccc"))
                == FT_CCC);
        REQUIRE(document.FindFileType(QStringLiteral("history.txt"))
                == FT_CCC);
        REQUIRE(document.FindFileType(QStringLiteral("history.ccr.bak"))
                == FT_CCC);

        const QString escapedSource =
            QStringLiteral("first\r\nsecond\t\\");
        REQUIRE(QuoteReturns(escapedSource)
                == QLatin1String("first\\r\\nsecond\\t\\\\"));
        REQUIRE(UnQuoteReturns(QuoteReturns(escapedSource))
                == escapedSource);
        REQUIRE(UnQuoteReturns(QStringLiteral("left\\xright\\"))
                == QStringLiteral("left\\right\\"));

        SayEntry boundedSay(
            QStringLiteral("say\t%1\t"
                           "(G:-1 0 0 E:-1 0 0 R:0 M:1)\t"
                           "first\\nline\tdiscarded")
                .arg(nick),
            &document);
        REQUIRE(boundedSay.m_mesg == QStringLiteral("first\nline"));
        GetInfoEntry boundedInfo(
            QStringLiteral("getinfo\tFieldNick\tfirst\tdiscarded"));
        REQUIRE(boundedInfo.m_name == QStringLiteral("FieldNick"));
        REQUIRE(boundedInfo.m_info == QStringLiteral("first"));
        StartHistoryEntry boundedStart(
            QStringLiteral("starthistory\tFieldNick\tAvatar\t"
                           "Title\tdiscarded"));
        REQUIRE(boundedStart.m_name == QStringLiteral("FieldNick"));
        REQUIRE(boundedStart.m_avName == QStringLiteral("Avatar"));
        REQUIRE(boundedStart.m_title == QStringLiteral("Title"));

        const qsizetype usersBeforeReplay = document.m_allChannelPuis.size();
        view.SetPanelsWide(1);
        REQUIRE(document.m_allChannelPuis.size() == usersBeforeReplay);
        REQUIRE(!document.m_pages.isEmpty());
        REQUIRE(document.m_pages.first()->m_panels.first()->m_elements.size() == 6);

        const QString conversation = saveConversation(document);
        REQUIRE(conversation.startsWith(QStringLiteral("#CHATCONVERSATION\r\n")));
        REQUIRE(conversation.contains(QStringLiteral("starthistory\t")));
        REQUIRE(conversation.contains(QStringLiteral("backdrop\t")));
        REQUIRE(conversation.count(QStringLiteral("ejoin\t")) == 2);
        REQUIRE(conversation.contains(QStringLiteral("say\t")));
        REQUIRE(conversation.endsWith(QStringLiteral("\r\n")));
        QString recordsWithoutCrLf = conversation;
        recordsWithoutCrLf.remove(QStringLiteral("\r\n"));
        REQUIRE(!recordsWithoutCrLf.contains(QLatin1Char('\r')));
        REQUIRE(!recordsWithoutCrLf.contains(QLatin1Char('\n')));

        const QString roundTripPath =
            temporaryDirectory.filePath(QStringLiteral("round-trip.ccc"));
        REQUIRE(document.OnSaveDocument(roundTripPath));
        REQUIRE(document.m_fileType == FT_CCC);
        REQUIRE(readFile(roundTripPath)
                == IntlTextFromQString(QStringView(conversation)));
        {
            CChatDoc loadedDocument;
            loadedDocument.m_bComicView = false;
            SetChatDoc(&loadedDocument);
            REQUIRE(loadedDocument.OnOpenDocument(roundTripPath));
            REQUIRE(loadedDocument.m_fileType == FT_CCC);
            REQUIRE(loadedDocument.m_bArchived);
            REQUIRE(!loadedDocument.IsModified());
            REQUIRE(loadedDocument.m_proto != nullptr);
            REQUIRE(loadedDocument.m_proto->m_strChannel
                    == QStringLiteral(": :"));
            REQUIRE(saveConversation(loadedDocument) == conversation);
        }
        SetChatDoc(&document);

        {
            const QString cfbPath =
                temporaryDirectory.filePath(
                    QStringLiteral("compound-locator.ccr"));
            QByteArray compoundLocator =
                QByteArray::fromHex("d0cf11e0a1b11ae1");
            compoundLocator.append(64, '\0');
            compoundLocator +=
                "#CHATLOCATOR\r\n"
                "CXPROMPT:\t0\r\n";
            writeFile(cfbPath, compoundLocator);

            CChatDoc compoundDocument;
            compoundDocument.m_bComicView = false;
            SetChatDoc(&compoundDocument);
            g_nCXKeepServer = 0;
            ChatSetCXPrompt(TRUE);
            REQUIRE(!compoundDocument.OnOpenDocument(cfbPath));
            REQUIRE(GetCXPrompt());

            ChatSetCXPrompt(TRUE);
            REQUIRE(!compoundDocument.ParseLocatorFile(cfbPath));
            REQUIRE(GetCXPrompt());
        }
        SetChatDoc(&document);

        const QString savedService =
            theApp.m_strConnectedService;
        {
            EnterInfoState enterInfoState;
            {
                const QString flatLocatorPath =
                    temporaryDirectory.filePath(
                        QStringLiteral("flat-locator.txt"));
                writeFile(
                    flatLocatorPath,
                    IntlTextFromQString(QStringView(
                        QStringLiteral("#CHATLOCATOR\r\n"
                                       "CXPROMPT:\t1\r\n"))));

                CChatDoc flatLocatorDocument;
                flatLocatorDocument.m_bComicView = false;
                SetChatDoc(&flatLocatorDocument);
                g_nCXKeepServer = 0;
                ChatSetCXPrompt(FALSE);
                QString unexpectedDiagnostic;
                QTimer::singleShot(0, [&unexpectedDiagnostic] {
                    auto* prompt = qobject_cast<QMessageBox*>(
                        QApplication::activeModalWidget());
                    if (!prompt) return;
                    unexpectedDiagnostic = prompt->objectName();
                    prompt->done(QMessageBox::Ok);
                });
                REQUIRE(flatLocatorDocument.OnOpenDocument(
                    flatLocatorPath));
                application.processEvents();
                REQUIRE(unexpectedDiagnostic.isEmpty());
                REQUIRE(GetCXPrompt());
            }
            SetChatDoc(&document);

            const QString savedChannel = theApp.m_myChannel;
            const QString savedCharacter =
                theApp.m_myCharacterName;
            const QString savedBackdrop = theApp.m_lastBackDrop;
            const bool savedComicsData = GetSendComicsData();
            const BOOL savedPrompt = GetCXPrompt();
            const int savedViewMode = g_iViewMode;

            {
                CChatDoc locatorDocument;
                locatorDocument.m_bComicView = false;
                SetChatDoc(&locatorDocument);
                locatorDocument.SetComicsTitle(
                    QStringLiteral("Before Locator"));

                bool locatorErrorDismissed = false;
                QTimer::singleShot(0, [&locatorErrorDismissed] {
                    auto* prompt = qobject_cast<QMessageBox*>(
                        QApplication::activeModalWidget());
                    REQUIRE(prompt != nullptr);
                    REQUIRE(prompt->objectName()
                            == QLatin1String("ID_ERR_NOT_CHATLOC"));
                    locatorErrorDismissed = true;
                    prompt->done(QMessageBox::Ok);
                });
                QString wrongCaseLocator =
                    QStringLiteral("#chatlocator\r\n"
                                   "VIEW:\tText\r\n");
                QTextStream wrongCaseStream(
                    &wrongCaseLocator, QIODevice::ReadOnly);
                SHORT wrongCaseKeepServer = 0;
                REQUIRE(!CChatDoc::ChatLoadLocator(
                    wrongCaseStream, FALSE, FALSE,
                    &wrongCaseKeepServer));
                REQUIRE(locatorErrorDismissed);

                ChatSetCXPrompt(TRUE);
                QString emptyPromptLocator =
                    QStringLiteral("#CHATLOCATOR\r\n"
                                   "CXPROMPT:\t   \r\n");
                QTextStream emptyPromptStream(
                    &emptyPromptLocator, QIODevice::ReadOnly);
                SHORT emptyPromptKeepServer = 0;
                REQUIRE(CChatDoc::ChatLoadLocator(
                    emptyPromptStream, FALSE, FALSE,
                    &emptyPromptKeepServer));
                REQUIRE(GetCXPrompt());

                QString locator =
                    QStringLiteral(
                        "unstructured preface\r\n"
                        "#CHATLOCATOR\r\n"
                        "iRcSeRvEr:\tlocalhost\r\n"
                        "IrCcHaNnEl:\t#LocatorRoom\r\n"
                        "CxPrOmPt:\t1\r\n"
                        "cHaRaCtEr:\tLocator Character\r\n"
                        "BaCkDrOp:\tLocator Backdrop\r\n"
                        "CoMiCsDaTa:\t1\r\n"
                        "TiTlE:\tLocator Title\r\n"
                        "ViEw:\tTeXt\r\n"
                        "\r\n"
                        "CHARACTER:\tIgnored After Blank\r\n");
                QTextStream locatorStream(
                    &locator, QIODevice::ReadOnly);
                SHORT keepServer = 0;
                REQUIRE(CChatDoc::ChatLoadLocator(
                    locatorStream, FALSE, FALSE, &keepServer));
                REQUIRE(keepServer == 1);
                REQUIRE(theApp.m_myChannel
                        == QStringLiteral("#LocatorRoom"));
                REQUIRE(g_enterInfo.m_strChannel
                        == QStringLiteral("#LocatorRoom"));
                REQUIRE(GetCXPrompt());
                REQUIRE(theApp.m_myCharacterName
                        == QStringLiteral("Locator Character"));
                REQUIRE(theApp.m_lastBackDrop
                        == QStringLiteral(
                            "BaCkDrOp:\tLocator Backdrop"));
                REQUIRE(GetSendComicsData());
                REQUIRE(locatorDocument.GetComicsTitle()
                        == QStringLiteral("Locator Title"));
                REQUIRE(g_iViewMode == VM_TEXT);
            }

            SetChatDoc(&document);
            theApp.m_strConnectedService = savedService;
            theApp.m_myChannel = savedChannel;
            theApp.m_myCharacterName = savedCharacter;
            theApp.m_lastBackDrop = savedBackdrop;
            SetSendComicsData(savedComicsData);
            ChatSetCXPrompt(savedPrompt);
            g_iViewMode = savedViewMode;
        }

        const QLocale savedLocale;
        QLocale::setDefault(QLocale(QLocale::English,
                                    QLocale::UnitedStates));
        SetMime(ANSI_CHARSET);
        {
            CChatDoc exactDocument;
            auto* exactSay = new SayEntry(
                nullptr,
                QStringLiteral("ACP \u20ac\r\nTab\tSlash\\"),
                NoFormattingSentinel());
            exactSay->m_name = QStringLiteral("FieldNick");
            exactDocument.m_history.append(exactSay);

            const QString exactConversation =
                QStringLiteral(
                    "#CHATCONVERSATION\r\n"
                    "say\tFieldNick\t"
                    "(G:-1 0 0 E:-1 0 0 R:0 M:1)\t"
                    "ACP \u20ac\\r\\nTab\\tSlash\\\\\r\n");
            REQUIRE(saveConversation(exactDocument)
                    == exactConversation);

            const QString exactConversationPath =
                temporaryDirectory.filePath(
                    QStringLiteral("exact-payload.ccc"));
            REQUIRE(exactDocument.OnSaveDocument(
                exactConversationPath));
            const QByteArray exactConversationBytes =
                readFile(exactConversationPath);
            REQUIRE(exactConversationBytes
                    == IntlTextFromQString(
                        QStringView(exactConversation)));
            REQUIRE(exactConversationBytes.contains(char(0x80)));
            REQUIRE(!exactConversationBytes.startsWith(
                QByteArray::fromHex("efbbbf")));

            theApp.m_strConnectedService =
                QStringLiteral("irc.example.test");
            exactDocument.m_proto->m_strPrettyChannel =
                QStringLiteral("#LocatorRoom");
            const QString exactLocator =
                QStringLiteral("#CHATLOCATOR\r\n"
                               "IRCSERVER:\tirc.example.test\r\n"
                               "IRCCHANNEL:\t#LocatorRoom\r\n"
                               "CXPROMPT:\t0\r\n");
            QString savedLocator;
            QTextStream savedLocatorStream(
                &savedLocator, QIODevice::WriteOnly);
            REQUIRE(exactDocument.ChatSaveLocator(
                savedLocatorStream));
            savedLocatorStream.flush();
            REQUIRE(savedLocator == exactLocator);

            const QString exactLocatorPath =
                temporaryDirectory.filePath(
                    QStringLiteral("exact-payload.CCR"));
            REQUIRE(exactDocument.OnSaveDocument(exactLocatorPath));
            REQUIRE(exactDocument.m_fileType == FT_CCR);
            REQUIRE(readFile(exactLocatorPath)
                    == IntlTextFromQString(
                        QStringView(exactLocator)));

            const QString lastDialogDirectory =
                temporaryDirectory.filePath(
                    QStringLiteral("last-dialog-directory"));
            REQUIRE(QDir().mkpath(lastDialogDirectory));
            {
                QFileDialog seedDialog;
                seedDialog.setDirectory(lastDialogDirectory);
                seedDialog.selectFile(QStringLiteral("seed.ccc"));
                static_cast<QDialog&>(seedDialog).accept();
            }
            const QString saveFilter =
                originalResourceString(
                    QStringLiteral("IDS_RTF_FILTER"))
                + originalResourceString(
                    QStringLiteral("IDS_CCC_FILTER"));
            CChatFileDialog pathlessDialog(
                FALSE, QString::fromLatin1(g_szCCCExt),
                QStringLiteral("Untitled.rtf"), saveFilter);
            REQUIRE(pathlessDialog.directory().absolutePath()
                    == QFileInfo(lastDialogDirectory)
                           .absoluteFilePath());
            REQUIRE(QFileInfo(
                        pathlessDialog.selectedFiles().value(0))
                        .fileName()
                    == QStringLiteral("Untitled.rtf"));

            const QString closePath =
                temporaryDirectory.filePath(
                    QStringLiteral("close-save.ccc"));
            REQUIRE(exactDocument.DoSave(closePath, true));
            REQUIRE(exactDocument.GetPathname()
                    == closePath);
            exactSay->m_mesg =
                QStringLiteral("Changed \u20ac");
            exactDocument.SetModifiedFlag(true);
            const QByteArray unchangedBytes = readFile(closePath);
            REQUIRE(!saveModifiedWithAnswer(
                exactDocument, QMessageBox::Cancel));
            REQUIRE(exactDocument.IsModified());
            REQUIRE(readFile(closePath) == unchangedBytes);
            REQUIRE(saveModifiedWithAnswer(
                exactDocument, QMessageBox::Discard));
            REQUIRE(exactDocument.IsModified());
            REQUIRE(readFile(closePath) == unchangedBytes);
            REQUIRE(saveModifiedWithAnswer(
                exactDocument, QMessageBox::Save));
            REQUIRE(!exactDocument.IsModified());
            REQUIRE(readFile(closePath)
                    == IntlTextFromQString(QStringView(
                        saveConversation(exactDocument))));
        }
        theApp.m_strConnectedService = savedService;
        QLocale::setDefault(savedLocale);
        SetMime(GetCorrectCharSet());

        // The document does not own these stack-backed widgets. Detach them
        // before their reverse-order destruction at the end of this scope.
        document.m_memberList = nullptr;
        document.m_view = nullptr;
    }

    SetChatDoc(nullptr);
    CommunicationCleanup();
    CUnitPanelPage::DestroyFonts();
    DestroyBackDropArt();
    DestroyAvatars();
    return 0;
}
