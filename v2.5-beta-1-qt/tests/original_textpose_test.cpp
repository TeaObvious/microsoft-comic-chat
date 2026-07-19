#include "avatar.h"
#include "chat.h"
#include "chatdoc.h"
#include "originalassets.h"
#include "panel.h"

#include <QApplication>
#include <QStringList>

#include <cstdlib>

BOOL CheckForUppers(const char* buff);
int CheckWord(const char* buff, const char* substr);
void GetEmotionsFromString(QString& str, CEmotionOpts& emOpts);
void InitializeEmotionRules();
void DestroyEmotionRules();

namespace {

[[noreturn]] void fail()
{
    std::abort();
}

void require(bool condition)
{
    if (!condition) fail();
}

QString ruleArgument(const QString& identifier, int lineIndex)
{
    const QStringList lines = originalResourceString(identifier).split(QLatin1Char('\n'));
    require(lineIndex >= 0 && lineIndex < lines.size());
    const QString& line = lines[lineIndex];
    const qsizetype firstQuote = line.indexOf(QLatin1Char('"'));
    const qsizetype secondQuote = line.indexOf(QLatin1Char('"'), firstQuote + 1);
    require(firstQuote >= 0 && secondQuote > firstQuote);
    return line.mid(firstQuote + 1, secondQuote - firstQuote - 1);
}

int priorityFor(const CEmotionOpts& options, float emotion)
{
    for (int index = 0; index < options.m_nOpts; ++index) {
        if (options.m_emotions[index].m_emotion == emotion) {
            return options.m_priorities[index];
        }
    }
    return -1;
}

void requireRule(const QString& text, float emotion, int priority)
{
    QString mutableText = text;
    CEmotionOpts options;
    GetEmotionsFromString(mutableText, options);
    require(priorityFor(options, emotion) == priority);
}

CBody* lastPanelBody(CChatDoc& document)
{
    require(!document.m_pages.isEmpty());
    CPage* page = document.m_pages.last();
    require(page && !page->m_panels.isEmpty());
    CPanel* panel = page->m_panels.last();
    require(panel && !panel->m_bodies.isEmpty());
    return panel->m_bodies.first();
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    theApp.InitVals();

    const QString shout = ruleArgument(QStringLiteral("ID_RULE_SHOUT"), 1);
    const QString laugh = ruleArgument(QStringLiteral("ID_RULE_LAUGH"), 0);
    const QString happy = ruleArgument(QStringLiteral("ID_RULE_HAPPY"), 0);
    const QString sad = ruleArgument(QStringLiteral("ID_RULE_SAD"), 0);
    const QString pointOtherStart = ruleArgument(
        QStringLiteral("ID_RULE_POINTOTHER"), 0);
    const QString pointOtherWord = ruleArgument(
        QStringLiteral("ID_RULE_POINTOTHER"), 1);
    const QString pointSelfWord = ruleArgument(
        QStringLiteral("ID_RULE_POINTSELF"), 1);
    const QString wave = ruleArgument(QStringLiteral("ID_RULE_WAVE"), 0);
    const QString coy = ruleArgument(QStringLiteral("ID_RULE_COY"), 0);

    const QByteArray laughBytes = laugh.toLatin1();
    require(CheckForUppers(laughBytes.constData()));
    require(CheckWord(laughBytes.constData(), laughBytes.constData()));
    require(!CheckForUppers(wave.toLatin1().constData()));

    InitializeEmotionRules();
    requireRule(shout, EM_SHOUT, 9);
    requireRule(laugh, EM_LAUGH, 11);
    requireRule(laugh, EM_SHOUT, 9);
    requireRule(happy, EM_HAPPY, 10);
    requireRule(sad, EM_SAD, 10);
    requireRule(pointOtherStart, EM_POINTOTHER, 4);
    requireRule(pointOtherWord, EM_POINTOTHER, 8);
    requireRule(pointSelfWord, EM_POINTSELF, 7);
    requireRule(wave, EM_WAVE, 2);
    requireRule(coy, EM_COY, 10);

    QString emptyRule = originalResourceString(QStringLiteral("ID_RULE_ANGRY"));
    CEmotionOpts emptyOptions;
    GetEmotionsFromString(emptyRule, emptyOptions);
    require(emptyRule == QStringLiteral("\"\"") && emptyOptions.m_nOpts == 0);

    theApp.InitializeComicsFonts();
    CUnitPanelPage::SetUnitPanelWidth(3000);
    CUnitPanelPage::SetUnitPanelHeight(3000);
    CUnitPanelPage::SetUnitPanelsPerRow(2);
    require(CUnitPanelPage::SetFonts(theApp.m_comicsFont, theApp.m_comicsColor));
    InitializeAvatars();

    QString avatarName;
    GetNextAvatarName(avatarName);
    CAvatarX* avatar = GetAvatar3(avatarName);
    require(avatar && avatar->m_body);

    CChatDoc document;
    SetChatDoc(&document);
    SetMyAvatar(avatar->m_avatarID, FALSE);
    require(MyAvatar() == avatar);

    avatar->SetNeutral();
    CBody* neutral = avatar->m_body->Clone();
    const QByteArray waveBytes = wave.toUtf8();
    document.ProcessLine(avatar->m_avatarID, waveBytes.constData(), BM_SAY,
                         TRUE, nullptr);
    require(neutral->IsSame(lastPanelBody(document)));
    delete neutral;

    document.DestroyPages();
    QString expectedText = wave;
    CEmotionOpts expectedOptions;
    GetEmotionsFromString(expectedText, expectedOptions);
    CBody* expected = avatar->GetBodyFromEmotion(expectedOptions);
    avatar->SetNeutral();
    document.ProcessLine(avatar->m_avatarID, waveBytes.constData(), BM_SAY,
                         FALSE, nullptr);
    require(expected->IsSame(lastPanelBody(document)));
    delete expected;

    SetChatDoc(nullptr);
    DestroyAvatars();
    CUnitPanelPage::DestroyFonts();
    DestroyEmotionRules();
    return EXIT_SUCCESS;
}
