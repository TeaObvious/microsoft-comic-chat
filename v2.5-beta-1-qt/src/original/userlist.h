// Ported from v2.5-beta-1-modern/userlist.h.
// QDialog/QTreeWidget replace only the MFC dialog and list-view boundary.

#pragma once

#include "wincompat.h"

#include <QDialog>
#include <QList>
#include <QString>
#include <QTreeWidget>

class CUserInfo;
class QFocusEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QShowEvent;
class QWidget;

class CUser {
public:
    CUser() = default;
    ~CUser() = default;

    WORD GetFlags() const { return m_wFlags; }
    void SetFlags(WORD flags) { m_wFlags = flags; }
    const QString& GetPrettyNick() const
    {
        return m_hasPrettyNick ? m_strPrettyNick : m_strNickname;
    }
    void SetPrettyNick(const QString& nick)
    {
        m_strPrettyNick = nick;
        m_hasPrettyNick = true;
    }
    void AddRef() { ++m_nRefCount; }
    void Release();
    BOOL operator==(const CUser& user) const;

    QString m_strNickname;
    QString m_strIdentity;
    QString m_strFullName;
    QString m_strRoom;
    QString m_strPrettyRoom;
    short m_nRefCount = 1;
    WORD m_wFlags = 0;

private:
    QString m_strPrettyNick;
    bool m_hasPrettyNick = false;
};

constexpr int USERSEARCH_ALL = 0;
constexpr int USERSEARCH_NICK = 1;
constexpr int USERSEARCH_ID = 2;
constexpr int USERSEARCH_ROOM = 3;
constexpr int LAUNCH_WHISPERBOX = 8181;

class CUserListPersist {
public:
    CUserListPersist();
    ~CUserListPersist();

    void MakeEmpty();
    void Reset();
    void Sort();
    int AddUser(CUser* user);

    QString m_cachedServer;
    QList<CUser*> m_users;
    int m_nUsers = 0;
    int m_usersSize = 0;
    BOOL m_sortAscending = TRUE;
    int m_sortColumn = 0;
    QString m_strUserFilter;
    QString m_strRoomFilter;
    int m_searchType = USERSEARCH_NICK;
    QString m_searchTime;
    QString m_strQuery;
    QString m_strEncRoom;
};

class CUserList;

class CUserListCtrl : public QTreeWidget {
public:
    explicit CUserListCtrl(CUserList* parent);

    CUser* GetSelectedUser() const;
    QString GetSelectedNickname() const;

protected:
    void focusInEvent(QFocusEvent* event) override;

private:
    CUserList* m_userList = nullptr;
};

class CUserList : public QDialog {
public:
    explicit CUserList(CUserListPersist* persist, QWidget* parent = nullptr);
    ~CUserList() override;

    int DoModal();
    void AddToUserList(int index);
    void AnnounceCount();
    void AnnounceTime();
    void Sort(BOOL resetList = TRUE);
    void Load(BOOL resetList = TRUE);
    void ShowAndEnableControl(const QString& identifier, BOOL showAndEnable);

    void OnResetList();
    void OnInviteFromList();
    void OnItemchangedUserlist();
    void OnUsersearchAll();
    void OnUsersearchIdentity();
    void OnUsersearchNick();
    void OnMessageFromList();
    void OnJoinRoom();
    void OnUsersearchRoom();
    void OnChangeRoomEdit();
    void OnCloseDialog();

    CUserListPersist* m_persist = nullptr;
    CUserListCtrl* m_userListCtrl = nullptr;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_ctlRoom = nullptr;
    QPushButton* m_reset = nullptr;
    QPushButton* m_invite = nullptr;
    QPushButton* m_message = nullptr;
    QPushButton* m_join = nullptr;
    QLabel* m_ctlCaption = nullptr;
    QLabel* m_searchTime = nullptr;
    QString m_strUser;
    QString m_strRoom;
    CUserInfo* m_selUser = nullptr;
    BOOL m_bResetHadFocus = FALSE;

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initializeDialog();
    void setSearchType(int searchType);

    QRadioButton* m_searchAll = nullptr;
    QRadioButton* m_searchNick = nullptr;
    QRadioButton* m_searchIdentity = nullptr;
    QRadioButton* m_searchRoom = nullptr;
    QWidget* m_searchLabel = nullptr;
    QWidget* m_roomLabel = nullptr;
    QWidget* m_group = nullptr;
    bool m_bInitialized = false;
};
