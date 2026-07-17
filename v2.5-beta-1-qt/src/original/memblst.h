// Ported from v2.5-beta-1-modern/memblst.h.

#pragma once

#include "wincompat.h"

#include <QList>
#include <QWidget>

class CChatDoc;
class QListWidget;
class CUserInfo;
class QEvent;
class QMenu;

class CMemberList : public QWidget {
public:
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

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void OnContextMenu(const QPoint& listPoint, const QPoint& globalPoint,
                       BOOL keyboard);

private:
    friend class CUserInfo;
    friend void UpdateSpectators(CChatDoc* doc, BOOL moderated);
    QListWidget* m_list = nullptr;
    bool m_iconMode = false;
};

void ForwardToSayWnd(unsigned int nChar);
void AddMacroMenu(QMenu& contextMenu);
void ShowMemberContext(int x, int y);
void GetSelectedPuis(QList<CUserInfo*>& selections);
void UpdateSpectators(CChatDoc* doc, BOOL moderated);
