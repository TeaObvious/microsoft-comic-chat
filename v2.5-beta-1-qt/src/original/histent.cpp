// Ported from v2.5-beta-1-modern/histent.cpp.

#include "histent.h"

#include "avatar.h"
#include "avatario.h"
#include "backdrop.h"
#include "chat.h"
#include "chatdoc.h"
#include "memblst.h"
#include "ircproto.h"
#include "originalassets.h"
#include "pageview.h"
#include "panel.h"
#include "protsupp.h"
#include "setupdlg.h"
#include "textview.h"

#include <QRegularExpression>
#include <QTextStream>

#include <cstdint>

namespace {

QStringList fields(const QString& record)
{
    return record.split(QLatin1Char('\t'), Qt::KeepEmptyParts);
}

int integerAfter(const QString& arguments, const QString& prefix, int fallback)
{
    const QRegularExpression expression(
        QStringLiteral("(?:^|\\s)%1(-?\\d+)")
            .arg(QRegularExpression::escape(prefix)));
    const QRegularExpressionMatch match = expression.match(arguments);
    return match.hasMatch() ? match.captured(1).toInt() : fallback;
}

void writeRecord(QTextStream& stream, const QStringList& recordFields)
{
    stream << recordFields.join(QLatin1Char('\t')) << QStringLiteral("\r\n");
}

QByteArray bytes(const QString& value)
{
    return value.toUtf8();
}

} // namespace

CDWordArray* NoFormattingSentinel()
{
    return reinterpret_cast<CDWordArray*>(~std::uintptr_t{0});
}

QString QuoteReturns(const QString& value)
{
    QString quoted;
    quoted.reserve(value.size() * 2);
    for (const QChar character : value) {
        switch (character.unicode()) {
        case '\n': quoted += QStringLiteral("\\n"); break;
        case '\r': quoted += QStringLiteral("\\r"); break;
        case '\\': quoted += QStringLiteral("\\\\"); break;
        case '\t': quoted += QStringLiteral("\\t"); break;
        default: quoted += character; break;
        }
    }
    return quoted;
}

QString UnQuoteReturns(const QString& value)
{
    QString unquoted;
    unquoted.reserve(value.size());
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar character = value.at(index);
        if (character != QLatin1Char('\\') || index + 1 >= value.size()) {
            unquoted += character;
            continue;
        }
        const QChar escaped = value.at(++index);
        if (escaped == QLatin1Char('n')) unquoted += QLatin1Char('\n');
        else if (escaped == QLatin1Char('r')) unquoted += QLatin1Char('\r');
        else if (escaped == QLatin1Char('t')) unquoted += QLatin1Char('\t');
        else if (escaped == QLatin1Char('\\')) unquoted += QLatin1Char('\\');
        else {
            // The original keeps the backslash but discards the unknown
            // escaped byte.
            unquoted += QLatin1Char('\\');
        }
    }
    return unquoted;
}

SayEntry::SayEntry(CUserInfo* pui, const QString& message,
                   CDWordArray* formatting, char highlightType)
    : m_pui(pui)
    , m_name(pui ? pui->GetName() : QString())
    , m_cHighlightType(highlightType)
{
    if (pui) {
        m_udi = pui->m_udi;
        m_udi.m_bbReq = 1;
    }

    if (!formatting) {
        QByteArray mutableMessage = bytes(message);
        m_prgdwFormatting = new CDWordArray;
        char* controlLess = SzControlLess(mutableMessage.data(), m_prgdwFormatting);
        m_mesg = QString::fromUtf8(controlLess);
    } else if (formatting == NoFormattingSentinel()) {
        m_mesg = message;
    } else {
        m_mesg = message;
        m_prgdwFormatting = CopyFormatting(formatting);
    }
    const QByteArray urlText = bytes(m_mesg);
    m_prgdwFormatting = IdentifyURLs(m_prgdwFormatting,
                                     urlText.constData());
}

SayEntry::SayEntry(const QString& record, CChatDoc* document,
                   char highlightType)
    : m_cHighlightType(highlightType)
{
    const QStringList recordFields = fields(record);
    m_name = recordFields.value(1);
    m_pui = LookupPui(m_name, document);
    if (!m_pui) {
        // This is the original bad/old conversation repair path. It is only
        // reachable while loading a conversation, never during IRC JOIN.
        m_pui = CIUserJoin(new CUserInfo(m_name));
    }
    ReadOtherArgs(recordFields.value(2), document);
    QByteArray mutableMessage = bytes(UnQuoteReturns(recordFields.value(3)));
    m_prgdwFormatting = new CDWordArray;
    char* controlLess = SzControlLess(mutableMessage.data(), m_prgdwFormatting);
    m_mesg = QString::fromUtf8(controlLess);
    const QByteArray urlText = bytes(m_mesg);
    m_prgdwFormatting = IdentifyURLs(m_prgdwFormatting,
                                     urlText.constData());
}

SayEntry::~SayEntry()
{
    FreeAndNullFormatting(&m_prgdwFormatting);
}

void SayEntry::Execute(int, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target || !m_pui) return;
    m_pui->m_udi.m_talkTos = m_udi.m_talkTos;

    const QByteArray message = bytes(m_mesg);
    if (!target->m_bComicView) {
        if (target->m_textView) {
            target->m_textView->TextLine(
                m_pui, nullptr, nullptr, message.constData(), m_udi.m_uModes,
                m_udi.m_bbCooked, m_prgdwFormatting, m_cHighlightType);
        }
        return;
    }

    CAvatarX* avatar = GetAvatar(m_pui->GetAvatarID());
    if (!avatar) return;
    if (m_udi.m_bbCooked) {
        if (!(avatar->m_flags & OTHERMAPPED)) {
            avatar->SetIndices(m_udi.m_chExpr, m_udi.m_chGest, m_udi.m_bbReq);
        } else {
            CEmotion expression;
            CEmotion gesture;
            BytesToEmotion(expression, m_udi.m_chExprE, m_udi.m_chExprI);
            BytesToEmotion(gesture, m_udi.m_chGestE, m_udi.m_chGestI);
            avatar->SetEmotions(expression, gesture);
        }
    }
    target->ProcessLine(m_pui->GetAvatarID(), message.constData(),
                        m_udi.m_uModes, m_udi.m_bbCooked,
                        m_prgdwFormatting);
}

QString SayEntry::FormatOtherArgs() const
{
    QString result = QStringLiteral("(G:%1 %2 %3 E:%4 %5 %6 R:%7 M:%8")
        .arg(static_cast<int>(m_udi.m_chGest))
        .arg(static_cast<int>(m_udi.m_chGestE))
        .arg(static_cast<int>(m_udi.m_chGestI))
        .arg(static_cast<int>(m_udi.m_chExpr))
        .arg(static_cast<int>(m_udi.m_chExprE))
        .arg(static_cast<int>(m_udi.m_chExprI))
        .arg(static_cast<int>(m_udi.m_bbReq))
        .arg(static_cast<int>(BM2SM(m_udi.m_uModes)));
    if (!m_udi.m_talkTos.isEmpty()) {
        QStringList names;
        for (CUserInfo* pui : m_udi.m_talkTos) {
            if (pui) names.append(pui->GetName());
        }
        if (!names.isEmpty()) result += QStringLiteral(" T:") + names.join(QLatin1Char(','));
    }
    result += QLatin1Char(')');
    return result;
}

void SayEntry::ReadOtherArgs(const QString& arguments, CChatDoc* document)
{
    if (!m_pui || !arguments.startsWith(QLatin1Char('('))) return;
    m_udi.Reset();
    const QRegularExpression sixValues(
        QStringLiteral("G:(-?\\d+)\\s+(-?\\d+)\\s+(-?\\d+)\\s+"
                       "E:(-?\\d+)\\s+(-?\\d+)\\s+(-?\\d+)"));
    const QRegularExpressionMatch values = sixValues.match(arguments);
    if (values.hasMatch()) {
        m_udi.m_chGest = static_cast<signed char>(values.captured(1).toInt());
        m_udi.m_chGestE = static_cast<signed char>(values.captured(2).toInt());
        m_udi.m_chGestI = static_cast<signed char>(values.captured(3).toInt());
        m_udi.m_chExpr = static_cast<signed char>(values.captured(4).toInt());
        m_udi.m_chExprE = static_cast<signed char>(values.captured(5).toInt());
        m_udi.m_chExprI = static_cast<signed char>(values.captured(6).toInt());
    }
    m_udi.m_bbReq = static_cast<unsigned char>(integerAfter(arguments,
                                                             QStringLiteral("R:"), 0));
    m_udi.m_uModes = SM2BM(static_cast<unsigned char>(integerAfter(
        arguments, QStringLiteral("M:"), SM_SAY)));
    m_udi.m_bbCooked = static_cast<unsigned char>(integerAfter(
        arguments, QStringLiteral("C:"), 0));

    const qsizetype talkTo = arguments.indexOf(QStringLiteral(" T:"));
    if (talkTo >= 0) {
        QString names = arguments.mid(talkTo + 3);
        const qsizetype close = names.indexOf(QLatin1Char(')'));
        if (close >= 0) names.truncate(close);
        for (const QString& name : names.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            if (CUserInfo* pui = LookupPui(name, document))
                m_udi.m_talkTos.append(pui);
        }
    }
}

void SayEntry::WriteSelf(QTextStream& stream) const
{
    QString message = m_mesg;
    if (m_prgdwFormatting) {
        const QByteArray plain = bytes(m_mesg);
        if (char* controlFull = SzControlFull(plain.constData(), m_prgdwFormatting)) {
            message = QString::fromUtf8(controlFull);
            delete[] controlFull;
        }
    }
    writeRecord(stream, {QStringLiteral("say"), m_name,
                         FormatOtherArgs(), QuoteReturns(message)});
}

JoinEntry::JoinEntry(CUserInfo* pui, BOOL therePrior, char highlightType)
    : m_pui(pui)
    , m_bTherePrior(therePrior)
    , m_cHighlightType(highlightType)
{
    if (!pui) return;
    pui->GetAttedNick(m_name, false);
    m_fullName = pui->GetFullName();
}

JoinEntry::JoinEntry(const QString& record, char highlightType)
    : m_cHighlightType(highlightType)
{
    const QStringList recordFields = fields(record);
    m_bTherePrior = recordFields.value(0) == QLatin1String("ejoin");
    m_name = recordFields.value(1);
    m_fullName = recordFields.value(2);
    m_pui = new CUserInfo(m_name, m_fullName);
}

void JoinEntry::Execute(int mode, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target || !m_pui) return;
    if (mode == HM_LIVE) CIUserJoin(m_pui);
    else ReinstallPui(m_pui, m_name);

    if (!m_bTherePrior && theApp.m_bShowArrivals
        && !target->m_bComicView && target->m_textView) {
        const QByteArray nickname = bytes(m_name);
        target->m_textView->DisplayJoin(nickname.constData(), m_cHighlightType);
    }

    if (mode == HM_LIVE && !theApp.m_bNoRefresh && target->m_proto) {
        if (m_pui->IsSelf()) {
            target->m_proto->ChatAnnounceNewAvatar(
                QString::fromUtf8(GetMyCharacter()),
                QString::fromUtf8(MyAvatarURL() ? MyAvatarURL() : ""));
            if (theApp.m_bAway) {
                target->m_proto->ChatSetAway(true, theApp.m_strAwayMessage,
                                             nullptr, false);
            }
        } else if (!m_bTherePrior) {
            AutoGreet(m_name);
            if (theApp.m_bAway) {
                target->m_proto->ChatSetAway(true, theApp.m_strAwayMessage,
                                             m_pui, false);
            }
        }
    }
}

void JoinEntry::WriteSelf(QTextStream& stream) const
{
    QStringList record{m_bTherePrior ? QStringLiteral("ejoin")
                                     : QStringLiteral("join"),
                       m_name};
    if (!m_fullName.isEmpty()) record.append(m_fullName);
    writeRecord(stream, record);
}

PartEntry::PartEntry(const QString& nickname, char highlightType,
                     bool serializedRecord)
    : m_cHighlightType(highlightType)
{
    m_name = serializedRecord ? fields(nickname).value(1) : nickname;
}

void PartEntry::Execute(int mode, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target) return;
    if (mode == HM_LIVE) CIUserPart(m_name, target);
    if (theApp.m_bShowArrivals && !target->m_bComicView && target->m_textView) {
        const QByteArray nickname = bytes(m_name);
        target->m_textView->DisplayPart(nickname.constData(), m_cHighlightType);
    }
}

void PartEntry::WriteSelf(QTextStream& stream) const
{
    writeRecord(stream, {QStringLiteral("part"), m_name});
}

ChangeAvatarEntry::ChangeAvatarEntry(CUserInfo* pui, const QString& avatarName,
                                     const QString& avatarUrl)
    : m_pui(pui)
    , m_name(pui ? pui->GetName() : QString())
    , m_avName(avatarName)
    , m_avURL(avatarUrl)
{
}

ChangeAvatarEntry::ChangeAvatarEntry(const QString& record)
{
    const QStringList recordFields = fields(record);
    m_name = recordFields.value(1);
    m_avName = recordFields.value(2);
    if (!recordFields.value(3).isEmpty()) m_avURL = recordFields.value(3);
    if (CAvatarX* avatar = GetAvatar3(m_avName, nullptr))
        m_avID = avatar->m_avatarID;
}

void UpdateMemberListIcon(CUserInfo* pui, CChatDoc* document)
{
    AddToImageList(pui);
    if (document && document->m_memberList)
        document->m_memberList->AddUser(pui);
}

void ChangeAvatarEntry::Execute(int mode, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target || !target->m_bComicView) return;
    if (!m_pui) m_pui = LookupPui(m_name, target);
    if (!m_pui) return;

    if (m_avID == 0) {
        m_avID = m_pui->GetAvatarID();
        const BOOL selectRandom = m_avURL.isNull() || m_avID == 0;
        if (CAvatarX* avatar = GetAvatar3(m_avName, m_pui, selectRandom))
            m_avID = avatar->m_avatarID;
    }
    if (!m_avID) return;

    if (m_pui == g_puiSelf) {
        SetMyAvatar(m_avID);
    } else {
        SetUserAvatarID(m_pui, m_avID);
        SetUserAvatarRealInfo(m_pui, m_avName, m_avURL, target,
                              mode == HM_LIVE);
    }

    if (mode == HM_LIVE) {
        m_avID = m_pui->GetAvatarID();
        UpdateMemberListIcon(m_pui, target);
        UpdateTitle(target);
    }
}

void ChangeAvatarEntry::WriteSelf(QTextStream& stream) const
{
    QString avatarName = m_avName;
    QString avatarUrl = m_avURL;
    if (m_avID) {
        if (CAvatarX* avatar = GetAvatar(m_avID)) {
            if (avatar->OriginalName()) avatarName = QString::fromUtf8(avatar->OriginalName());
            if (avatar->Url()) avatarUrl = QString::fromUtf8(avatar->Url());
        }
    }
    writeRecord(stream, {QStringLiteral("changeavatar"), m_name,
                         avatarName, avatarUrl});
}

GetInfoEntry::GetInfoEntry(CUserInfo* pui, const QString& info)
    : m_pui(pui)
    , m_info(info)
    , m_name(pui ? pui->GetName() : QString())
{
}

GetInfoEntry::GetInfoEntry(const QString& record)
{
    const QStringList recordFields = fields(record);
    m_name = recordFields.value(1);
    m_info = recordFields.value(2);
}

void GetInfoEntry::Execute(int, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target) return;
    if (!m_pui) m_pui = LookupPui(m_name, target);
    if (m_pui) target->ShowInfo(m_pui, m_info, false, 0);
}

void GetInfoEntry::WriteSelf(QTextStream& stream) const
{
    writeRecord(stream, {QStringLiteral("getinfo"), m_name, m_info});
}

ComicCharacterEntry::ComicCharacterEntry(CUserInfo* pui)
    : GetInfoEntry(pui, QString())
{
}

ComicCharacterEntry::ComicCharacterEntry(const QString& record)
    : GetInfoEntry(record)
{
}

void ComicCharacterEntry::Execute(int, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target) return;
    if (!m_pui) m_pui = LookupPui(m_name, target);
    if (!m_pui || m_pui->GetAvatarRealName().isEmpty()) return;

    QString message = originalResourceString(QStringLiteral("IDS_NO_CHAR_INFO"));
    QString link = originalResourceString(QStringLiteral("IDS_NO_CHAR_HOTLINK"));
    link.prepend(QChar(0x18));
    link.append(QChar(0x18));
    message.replace(QStringLiteral("%1"), m_pui->GetScreenName());
    message.replace(QStringLiteral("%2"), link);
    target->ShowInfo(m_pui, message, true, 0x18);
}

void ComicCharacterEntry::WriteSelf(QTextStream& stream) const
{
    writeRecord(stream, {QStringLiteral("comicchar"), m_name, m_info});
}

StartHistoryEntry::StartHistoryEntry(const QString& title,
                                     const QString& avatarName,
                                     int randomStart)
    : m_randStart(randomStart)
    , m_title(title.isEmpty() ? GetRandomTitle() : title)
    , m_avName(avatarName)
    , m_name(QString::fromUtf8(GetMyNickName()))
{
}

StartHistoryEntry::StartHistoryEntry(const QString& record)
{
    const QStringList recordFields = fields(record);
    m_name = recordFields.value(1);
    if (m_name.isEmpty())
        m_name = originalResourceString(QStringLiteral("IDS_DEFAULT_NICK"));
    m_avName = recordFields.value(2);
    m_title = recordFields.value(3);
}

void StartHistoryEntry::Execute(int, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target) return;
    if (!target->m_bArchived) SetMyNameNick(m_name);
    if (target->m_bComicView) {
        target->SetComicsTitle(m_title);
        SetMyAvatar(m_avName, FALSE);
    }
}

void StartHistoryEntry::WriteSelf(QTextStream& stream) const
{
    writeRecord(stream, {QStringLiteral("starthistory"), m_name,
                         m_avName, m_title});
}

ChangeBackDropEntry::ChangeBackDropEntry(const QString& backdropName,
                                         const QString& backdropUrl)
    : m_backName(backdropName)
    , m_backURL(backdropUrl)
{
}

ChangeBackDropEntry::ChangeBackDropEntry(const QString& record,
                                         bool serializedRecord)
{
    if (serializedRecord) {
        const QStringList recordFields = fields(record);
        m_backName = recordFields.value(1);
        if (!recordFields.value(2).isEmpty()) m_backURL = recordFields.value(2);
    } else {
        m_backName = record;
    }
}

void ChangeBackDropEntry::Execute(int, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target || !target->m_bComicView) return;
    const QByteArray name = bytes(m_backName);
    const QByteArray url = bytes(m_backURL);
    SetBackDrop(name.constData(), m_backURL.isNull() ? nullptr : url.constData());
}

void ChangeBackDropEntry::WriteSelf(QTextStream& stream) const
{
    writeRecord(stream, {QStringLiteral("backdrop"), m_backName, m_backURL});
}

NickEntry::NickEntry(const QString& oldNickname, const QString& newNickname)
    : m_oldNick(oldNickname)
    , m_newNick(newNickname)
{
}

NickEntry::NickEntry(const QString& record)
{
    const QStringList recordFields = fields(record);
    m_oldNick = recordFields.value(1);
    m_newNick = recordFields.value(2);
}

void NickEntry::Execute(int mode, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!target) return;
    if (mode == HM_LIVE) ProcessNick(target, m_oldNick, m_newNick, true);
    else ReinstallPui(LookupPui(m_oldNick, target), m_newNick);

    if (!target->m_bComicView && target->m_textView) {
        const QByteArray oldNickname = bytes(m_oldNick);
        target->m_textView->DisplayNickChange(
            LookupPui(m_newNick, target), oldNickname.constData());
    }
}

void NickEntry::WriteSelf(QTextStream& stream) const
{
    writeRecord(stream, {QStringLiteral("nick"), m_oldNick, m_newNick});
}

void AddAndExecute(HistoryEntry* entry, CChatDoc* document)
{
    CChatDoc* target = document ? document : GetChatDoc();
    if (!entry) return;
    if (!target) {
        delete entry;
        return;
    }
    if (theApp.m_bPrompt && !theApp.m_bEmbedded)
        target->SetModifiedFlag(true);
    target->RegisterNewContent();
    target->m_history.append(entry);

    CChatDoc* oldDocument = GetChatDoc();
    if (oldDocument != target) SetChatDoc(target);
    entry->Execute(HM_LIVE, target);
    if (oldDocument != target) SetChatDoc(oldDocument);
}

void AddEntry(HistoryEntry* entry, CChatDoc* document)
{
    if (!entry) return;
    if (!document) {
        delete entry;
        return;
    }
    document->m_history.append(entry);
    document->RegisterNewContent();
}
