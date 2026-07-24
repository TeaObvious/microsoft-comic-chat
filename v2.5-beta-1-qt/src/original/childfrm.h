// Ported from v2.5-beta-1-modern/childfrm.h.

#pragma once

#include <QMdiSubWindow>

class CChatDoc;
class CChatView;
class QCloseEvent;
class QEvent;
class QHideEvent;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;

class CChildFrame : public QMdiSubWindow {
public:
    explicit CChildFrame(CChatDoc* document, bool ownsDocument,
                         QWidget* parent = nullptr);
    ~CChildFrame() override;

    CChatDoc* GetDocument() const { return m_document; }
    CChatView* GetChatView() const { return m_view; }
    void ActivateFrame(bool activate = true);
    void SetDocumentTitle(const QString& title);
    void setVisible(bool visible) override;

    bool m_bPositioned = false;

protected:
    bool event(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void UpdateMaximizedFlag();

    CChatDoc* m_document = nullptr;
    CChatView* m_view = nullptr;
    bool m_ownsDocument = false;
};
