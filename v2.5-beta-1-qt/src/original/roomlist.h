// Ported from v2.5-beta-1-modern/roomlist.h.
// QDialog/QTreeWidget replace only the MFC dialog and list-view boundary.

#pragma once

#include "wincompat.h"

#include <QDialog>
#include <QList>
#include <QString>
#include <QTreeWidget>

class QCheckBox;
class QFocusEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QWidget;

class CRoom {
public:
    void CalculateSortByte();

    QString m_name;
    QString m_prettyName;
    UINT m_nUsers = 0;
    QString m_descr;
    BYTE m_byteRegistered = FALSE;
    BYTE m_byteSort = 0;
};

class CRoomListPersist {
public:
    CRoomListPersist();
    ~CRoomListPersist();

    void MakeEmpty();
    void Reset();
    void SortRooms();
    int AddRoom(CRoom* room);

    QString m_cachedServer;
    QList<CRoom*> m_rooms;
    int m_nRooms = 0;
    int m_roomsSize = 0;
    BOOL m_sortAscending = TRUE;
    int m_sortColumn = 0;
    QString m_strTopicFilter;
    BOOL m_bSearchDescrs = FALSE;
    BOOL m_bRegisteredOnly = FALSE;
    UINT m_maxMembers = 9999;
    UINT m_minMembers = 0;
    QString m_searchTime;
    QString m_strQuery;
};

class CRoomList;

class CRoomListCtrl : public QTreeWidget {
public:
    explicit CRoomListCtrl(CRoomList* parent);

    CRoom* GetSelectedRoom() const;

protected:
    void focusInEvent(QFocusEvent* event) override;

private:
    CRoomList* m_roomList = nullptr;
};

class CRoomList : public QDialog {
public:
    explicit CRoomList(CRoomListPersist* persist, QWidget* parent = nullptr);

    int DoModal();
    void ClearRoomList();
    void AddToRoomList(int roomIndex);
    void SortRooms(BOOL resetList = TRUE);
    void LoadRooms(BOOL resetList = TRUE);
    void FilterByTopic(const QString& topic);
    BOOL MatchesTopicFilter(CRoom* room, const QString& searchTopic,
                            BOOL searchDescrs) const;
    void AnnounceCount();
    void AnnounceTime();
    CRoom* GetSelectedRoom() const;
    void ReenableListMembers();

    void OnResetList();
    void OnGoto();
    void OnSearchDescrs();
    void OnChangeTopicEdit();
    void OnCreateRoom();
    void OnChangeMinMembers();
    void OnChangeMaxMembers();
    void OnRegisteredOnly();
    void OnListmembers();
    void OnCloseDialog();

    CRoomListPersist* m_persist = nullptr;
    CRoomListCtrl* m_roomList = nullptr;
    QLineEdit* m_topicEdit = nullptr;
    QCheckBox* m_ctrlSearchDescrs = nullptr;
    QCheckBox* m_registeredOnly = nullptr;
    QLineEdit* m_minMembersEdit = nullptr;
    QLineEdit* m_maxMembersEdit = nullptr;
    QPushButton* m_reset = nullptr;
    QPushButton* m_goto = nullptr;
    QPushButton* m_listMembers = nullptr;
    QLabel* m_ctlCaption = nullptr;
    QLabel* m_searchTime = nullptr;
    QString m_strTopic;
    BOOL m_bSearchDescrs = FALSE;
    UINT m_minMembers = 0;
    UINT m_maxMembers = 9999;
    BOOL m_bRegisteredOnly = FALSE;
    BOOL m_bResetHadFocus = FALSE;

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initializeDialog();
    void storeFilterState();

    bool m_bInitialized = false;
    bool m_loadingControls = false;
};
