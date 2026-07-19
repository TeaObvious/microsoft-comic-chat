// Ported from v2.5-beta-1-modern/memblst.h.

#pragma once

#include "wincompat.h"

#include <QList>
#include <QListWidget>
#include <QWidget>

class CChatDoc;
class CMemberList;
class CUserInfo;
class QEvent;
class QKeyEvent;
class QMenu;

class CMemberListCtrl : public QListWidget {
public:
    explicit CMemberListCtrl(CMemberList* owner);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    CMemberList* m_owner = nullptr;
};

class CMemberList : public QWidget {
public:
    enum ItemDataRole {
        UserPointerRole = Qt::UserRole,
        StatusImageRole = Qt::UserRole + 1,
        StateImageRole = Qt::UserRole + 2,
        AvatarImageRole = Qt::UserRole + 3,
        IconModeRole = Qt::UserRole + 4
    };

    explicit CMemberList(QWidget* parent = nullptr);

    void AddUser(CUserInfo* pui);
    void RemoveUser(CUserInfo* pui);
    void Clear();
    void Sort();
    void SetIconMode(bool iconMode);
    int count() const;
    CUserInfo* currentUser() const;
    QList<CUserInfo*> selectedUsers() const;
    CUserInfo* GetNextSelectedMember(int& index) const;
    int SelectedMemberCount() const;
    QWidget* FocusWidget() const;
    void EnsureFocusItem();
    void MakeVisible(CUserInfo* pui);

    // CListCtrl replacement kept under the original member name.
    CMemberListCtrl* m_MemberListBox = nullptr;

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void OnContextMenu(const QPoint& listPoint, const QPoint& globalPoint,
                       BOOL keyboard);

private:
    friend class CUserInfo;
    friend void UpdateSpectators(CChatDoc* doc, BOOL moderated);
    CMemberListCtrl* m_list = nullptr;
    bool m_iconMode = false;
};

void ForwardToSayWnd(unsigned int nChar);
void AddMacroMenu(QMenu& contextMenu);
void ShowMemberContext(int x, int y);
void GetSelectedPuis(QList<CUserInfo*>& selections);
void UpdateSpectators(CChatDoc* doc, BOOL moderated);
