// Ported from v2.5-beta-1-modern/histent.h.
// Qt containers replace MFC ownership only. History remains the single
// execution/replay path for visible conversation state.

#pragma once

#include "format.h"
#include "userinfo.h"

#include <QString>

class CChatDoc;
class QTextStream;

constexpr UINT HT_UNSPECED = 0;
constexpr UINT HT_AVATARENTRY = 1;

constexpr int HM_LIVE = 1;
constexpr int HM_RELOAD = 2;
constexpr int HM_LOAD = 4;

class HistoryEntry {
public:
    virtual ~HistoryEntry() = default;
    virtual void Execute(int mode, CChatDoc* document = nullptr) = 0;
    virtual void WriteSelf(QTextStream& stream) const = 0;
    virtual UINT GetType() const { return HT_UNSPECED; }
};

class SayEntry final : public HistoryEntry {
public:
    SayEntry(CUserInfo* pui, const QString& message,
             CDWordArray* formatting, char highlightType = -1);
    SayEntry(const QString& record, CChatDoc* document,
             char highlightType = -1);
    ~SayEntry() override;

    void Execute(int mode, CChatDoc* document = nullptr) override;
    void WriteSelf(QTextStream& stream) const override;
    QString FormatOtherArgs() const;
    void ReadOtherArgs(const QString& arguments, CChatDoc* document);

    CUserDisplayInfo m_udi;
    CDWordArray* m_prgdwFormatting = nullptr;
    CUserInfo* m_pui = nullptr;
    QString m_mesg;
    QString m_name;
    char m_cHighlightType = -1;
};

class JoinEntry final : public HistoryEntry {
public:
    explicit JoinEntry(CUserInfo* pui, BOOL therePrior = TRUE,
                       char highlightType = -1);
    explicit JoinEntry(const QString& record, char highlightType = -1);

    void Execute(int mode, CChatDoc* document = nullptr) override;
    void WriteSelf(QTextStream& stream) const override;

    CUserInfo* m_pui = nullptr;
    QString m_name;
    QString m_fullName;
    BOOL m_bTherePrior = TRUE;
    char m_cHighlightType = -1;
};

class PartEntry final : public HistoryEntry {
public:
    explicit PartEntry(const QString& nickname, char highlightType = -1,
                       bool serializedRecord = false);

    void Execute(int mode, CChatDoc* document = nullptr) override;
    void WriteSelf(QTextStream& stream) const override;

    QString m_name;
    char m_cHighlightType = -1;
};

class ChangeAvatarEntry final : public HistoryEntry {
public:
    ChangeAvatarEntry(CUserInfo* pui, const QString& avatarName,
                      const QString& avatarUrl = QString());
    explicit ChangeAvatarEntry(const QString& record);

    void Execute(int mode, CChatDoc* document = nullptr) override;
    void WriteSelf(QTextStream& stream) const override;
    UINT GetType() const override { return HT_AVATARENTRY; }

    CUserInfo* m_pui = nullptr;
    QString m_name;
    unsigned short m_avID = 0;
    QString m_avName;
    QString m_avURL;
};

class GetInfoEntry : public HistoryEntry {
public:
    GetInfoEntry(CUserInfo* pui, const QString& info);
    explicit GetInfoEntry(const QString& record);

    void Execute(int mode, CChatDoc* document = nullptr) override;
    void WriteSelf(QTextStream& stream) const override;

    CUserInfo* m_pui = nullptr;
    QString m_info;
    QString m_name;
};

class ComicCharacterEntry final : public GetInfoEntry {
public:
    explicit ComicCharacterEntry(CUserInfo* pui);
    explicit ComicCharacterEntry(const QString& record);

    void Execute(int mode, CChatDoc* document = nullptr) override;
    void WriteSelf(QTextStream& stream) const override;
};

class StartHistoryEntry final : public HistoryEntry {
public:
    StartHistoryEntry(const QString& title, const QString& avatarName,
                      int randomStart);
    explicit StartHistoryEntry(const QString& record);

    void Execute(int mode, CChatDoc* document = nullptr) override;
    void WriteSelf(QTextStream& stream) const override;

    int m_randStart = 0;
    QString m_title;
    QString m_avName;
    QString m_name;
};

class ChangeBackDropEntry final : public HistoryEntry {
public:
    ChangeBackDropEntry(const QString& backdropName,
                        const QString& backdropUrl = QString());
    explicit ChangeBackDropEntry(const QString& record, bool serializedRecord);

    void Execute(int mode, CChatDoc* document = nullptr) override;
    void WriteSelf(QTextStream& stream) const override;

    QString m_backName;
    QString m_backURL;
};

class NickEntry final : public HistoryEntry {
public:
    NickEntry(const QString& oldNickname, const QString& newNickname);
    explicit NickEntry(const QString& record);

    void Execute(int mode, CChatDoc* document = nullptr) override;
    void WriteSelf(QTextStream& stream) const override;

    QString m_oldNick;
    QString m_newNick;
};

// The original uses (CDWordArray*)-1 to distinguish an outgoing unformatted
// message from an incoming control-full message. Keep that distinction at the
// original boundary without dereferencing the sentinel.
CDWordArray* NoFormattingSentinel();

void AddAndExecute(HistoryEntry* entry, CChatDoc* document = nullptr);
void AddEntry(HistoryEntry* entry, CChatDoc* document);

QString QuoteReturns(const QString& value);
QString UnQuoteReturns(const QString& value);
