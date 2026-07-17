// Ported from v2.5-beta-1-modern/textpose.cpp.
// CString/CPtrList and LoadString are replaced mechanically by QString/QList
// and direct parsing of the authoritative chat.rc resource strings.

#include "chat.h"

#include "avatar.h"
#include "chatdoc.h"
#include "originalassets.h"

#include <QByteArray>
#include <QList>

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {

const char* const ruleIDs[] = {
    "ID_RULE_SHOUT", "ID_RULE_LAUGH", "ID_RULE_HAPPY", "ID_RULE_SAD",
    "ID_RULE_POINTOTHER", "ID_RULE_POINTSELF", "ID_RULE_WAVE",
    "ID_RULE_COY", "ID_RULE_ANGRY", "ID_RULE_SCARED", "ID_RULE_BORED"
};

const float ruleEMs[] = {
    EM_SHOUT, EM_LAUGH, EM_HAPPY, EM_SAD, EM_POINTOTHER, EM_POINTSELF,
    EM_WAVE, EM_COY, EM_ANGRY, EM_SCARED, EM_BORED
};

const char* sentenceTerminator = ".!?";

struct STRINGUNIT {
    char* arg = nullptr;
    int length = 0;
    int strength = 0;
    float emotion = 0.0f;
    BOOL caseSensitive = FALSE;
};

QList<STRINGUNIT*> generalRules;
QList<STRINGUNIT*> wordRules;
QList<STRINGUNIT*> sentenceRules;
int capsStrength = 0;
float capsEmotion = 0.0f;

bool IsLower(char value)
{
    return std::islower(static_cast<unsigned char>(value)) != 0;
}

bool IsUpper(char value)
{
    return std::isupper(static_cast<unsigned char>(value)) != 0;
}

bool IsSpace(char value)
{
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

bool IsPunct(char value)
{
    return std::ispunct(static_cast<unsigned char>(value)) != 0;
}

bool IsAlnum(char value)
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0;
}

bool IsPrint(char value)
{
    return std::isprint(static_cast<unsigned char>(value)) != 0;
}

bool IsDigit(char value)
{
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
}

char* DuplicateString(const char* value)
{
    const std::size_t length = std::strlen(value);
    auto* result = static_cast<char*>(std::malloc(length + 1));
    if (!result) return nullptr;
    std::memcpy(result, value, length + 1);
    return result;
}

bool SameNoCase(const char* left, const char* right)
{
    return QByteArray(left).compare(QByteArray(right), Qt::CaseInsensitive) == 0;
}

void AddToGeneral(STRINGUNIT* rule)
{
    generalRules.append(rule);
}

void AddToWord(STRINGUNIT* rule)
{
    wordRules.append(rule);
}

void AddToSentence(STRINGUNIT* rule)
{
    sentenceRules.append(rule);
}

} // namespace

BOOL CheckForUppers(const char* buff)
{
    int nUppers = 0;
    while (*buff != '\0') {
        if (IsLower(*buff)) return FALSE;
        if (IsUpper(*buff++)) ++nUppers;
    }
    return nUppers > 1 ? TRUE : FALSE;
}

int CheckWord(const char* buff, const char* substr)
{
    const char* loc = buff;
    while ((loc = std::strstr(loc, substr)) != nullptr) {
        if (loc == buff || IsSpace(*(loc - 1))) {
            const int len = static_cast<int>(std::strlen(substr));
            const char after = loc[len];
            if (!after || IsSpace(after) || IsPunct(after)) return TRUE;
        }
        ++loc;
    }
    return FALSE;
}

const char* GetNextSentenceStart(const char* buff)
{
    buff = std::strpbrk(buff, sentenceTerminator);
    if (!buff) return nullptr;
    while (IsPunct(*buff) || IsSpace(*buff)) ++buff;
    return buff;
}

char* ToLower(const char* buff)
{
    char* newStr = DuplicateString(buff);
    if (!newStr) return nullptr;
    char* sptr = newStr;
    while (*sptr) {
        if (IsUpper(*sptr)) {
            *sptr = static_cast<char>(
                std::tolower(static_cast<unsigned char>(*sptr)));
        }
        ++sptr;
    }
    return newStr;
}

CEmotionOpts emo;

void GetEmotionsFromString(QString& str, CEmotionOpts& emOpts);

void ChatPreSendText(QString& str, int avID)
{
    if (!GetChatDoc() || !GetChatDoc()->m_bComicView) return;

    CAvatarX* av = avID ? GetAvatar(static_cast<USHORT>(avID)) : MyAvatar();
    if (!av || av->m_freeze != AF_UNFROZEN) return;
    GetEmotionsFromString(str, emo);
    CBody* newBody = av->GetBodyFromEmotion(emo);
    av->UpdateBody(newBody);
}

const char* ReadString(const char* string, char* buff)
{
    const char* firstQuote = std::strchr(string, '"');
    if (!firstQuote) {
        *buff = '\0';
        return string;
    }
    const char* secondQuote = std::strchr(firstQuote + 1, '"');
    if (!secondQuote) {
        *buff = '\0';
        return string;
    }
    const int len = static_cast<int>(secondQuote - firstQuote - 1);
    std::strncpy(buff, firstQuote + 1, static_cast<std::size_t>(len));
    buff[len] = '\0';
    return secondQuote + 1;
}

STRINGUNIT* StringUnit(float emotion, const char* arg, int strength,
                       BOOL caseSensitive)
{
    auto* unit = static_cast<STRINGUNIT*>(std::malloc(sizeof(STRINGUNIT)));
    if (!unit) return nullptr;
    unit->emotion = emotion;
    unit->arg = caseSensitive ? DuplicateString(arg) : ToLower(arg);
    unit->length = static_cast<int>(std::strlen(arg));
    unit->strength = strength;
    unit->caseSensitive = caseSensitive;
    return unit;
}

void RegisterRule(float emotion, const char* function, const char* arg,
                  int strength)
{
    if (SameNoCase(function, "AllCaps")) {
        capsStrength = strength;
        capsEmotion = emotion;
    } else if (SameNoCase(function, "FindString")) {
        AddToGeneral(StringUnit(emotion, arg, strength, TRUE));
    } else if (SameNoCase(function, "FindString*")) {
        AddToGeneral(StringUnit(emotion, arg, strength, FALSE));
    } else if (SameNoCase(function, "CheckWord")) {
        AddToWord(StringUnit(emotion, arg, strength, TRUE));
    } else if (SameNoCase(function, "CheckWord*")) {
        AddToWord(StringUnit(emotion, arg, strength, FALSE));
    } else if (SameNoCase(function, "CheckStart")) {
        AddToSentence(StringUnit(emotion, arg, strength, TRUE));
    } else if (SameNoCase(function, "CheckStart*")) {
        AddToSentence(StringUnit(emotion, arg, strength, FALSE));
    }
}

BOOL LoadSingleRule(float emotion, const char* start, const char** end)
{
    char function[20];
    char arg[200];
    char strengthStr[100];
    char* fptr = function;
    const char* sptr = start;

    while (!IsPrint(*sptr) && *sptr) ++sptr;
    if (!*sptr) return FALSE;
    while (*sptr != '(' && *sptr) *fptr++ = *sptr++;
    *fptr = '\0';
    if (!*sptr) return FALSE;

    ++sptr;
    sptr = ReadString(sptr, arg);
    while (*sptr != ';' && *sptr) ++sptr;
    if (!*sptr) return FALSE;

    ++sptr;
    char* strPtr = strengthStr;
    while (*sptr != '\n' && *sptr) {
        if (IsDigit(*sptr)) *strPtr++ = *sptr;
        ++sptr;
    }
    *strPtr = '\0';
    const int strength = std::atoi(strengthStr);
    while (*sptr == '\n') ++sptr;
    *end = sptr;

    RegisterRule(emotion, function, arg, strength);
    return *sptr != '\0' ? TRUE : FALSE;
}

void LoadCompositeRule(float emotion, QString& rule)
{
    const QByteArray encodedRule = rule.toUtf8();
    const char* rptr = encodedRule.constData();
    while (TRUE) {
        if (!LoadSingleRule(emotion, rptr, &rptr)) break;
    }
}

void InitializeEmotionRules()
{
    const int nRules = static_cast<int>(sizeof(ruleIDs) / sizeof(ruleIDs[0]));
    for (int index = 0; index < nRules; ++index) {
        QString rule = originalResourceString(QString::fromLatin1(ruleIDs[index]));
        LoadCompositeRule(ruleEMs[index], rule);
    }
}

int StartCompare2(const char* sent, const char* substring, int len)
{
    return std::strncmp(sent, substring, static_cast<std::size_t>(len)) == 0
            && !IsAlnum(sent[len])
        ? TRUE
        : FALSE;
}

void GetEmotionsFromString(QString& str, CEmotionOpts& emOpts)
{
    const QByteArray encoded = str.toUtf8();
    const char* buff = encoded.constData();
    char* lower = ToLower(buff);
    if (!lower) return;
    emOpts.m_nOpts = 0;

    if (capsStrength && CheckForUppers(buff)) {
        emOpts.Add(capsEmotion, 1.0, capsStrength);
    }

    for (STRINGUNIT* unit : generalRules) {
        if (unit->caseSensitive) {
            if (std::strstr(buff, unit->arg)) {
                emOpts.Add(unit->emotion, 1.0, unit->strength);
            }
        } else if (std::strstr(lower, unit->arg)) {
            emOpts.Add(unit->emotion, 1.0, unit->strength);
        }
    }

    for (STRINGUNIT* unit : wordRules) {
        if (unit->caseSensitive) {
            if (CheckWord(buff, unit->arg)) {
                emOpts.Add(unit->emotion, 1.0, unit->strength);
            }
        } else if (CheckWord(lower, unit->arg)) {
            emOpts.Add(unit->emotion, 1.0, unit->strength);
        }
    }

    const char* bptr = buff;
    while (IsSpace(*bptr)) ++bptr;
    while (bptr && *bptr) {
        char* lptr = lower + (bptr - buff);
        Q_UNUSED(lptr);
        for (STRINGUNIT* unit : sentenceRules) {
            // Keep the original control flow: textpose.cpp computes bptr/lptr,
            // but StartCompare2 intentionally receives buff/lower here.
            if (unit->caseSensitive) {
                if (StartCompare2(buff, unit->arg, unit->length)) {
                    emOpts.Add(unit->emotion, 1.0, unit->strength);
                }
            } else if (StartCompare2(lower, unit->arg, unit->length)) {
                emOpts.Add(unit->emotion, 1.0, unit->strength);
            }
        }
        bptr = GetNextSentenceStart(bptr);
    }

    std::free(lower);
}

void DestroyEmotionList(QList<STRINGUNIT*>& list)
{
    for (STRINGUNIT* unit : list) {
        std::free(unit->arg);
        std::free(unit);
    }
}

void DestroyEmotionRules()
{
    DestroyEmotionList(generalRules);
    DestroyEmotionList(wordRules);
    DestroyEmotionList(sentenceRules);
}
