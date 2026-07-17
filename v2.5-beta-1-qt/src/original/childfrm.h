// Ported from v2.5-beta-1-modern/childfrm.h.

#pragma once

#include <QMdiSubWindow>

class CChatDoc;
class CChatView;
class QCloseEvent;

class CChildFrame : public QMdiSubWindow {
public:
    explicit CChildFrame(CChatDoc* document, bool ownsDocument,
                         QWidget* parent = nullptr);
    ~CChildFrame() override;

    CChatDoc* GetDocument() const { return m_document; }
    CChatView* GetChatView() const { return m_view; }
    void ActivateFrame();
    void SetDocumentTitle(const QString& title);

    bool m_bPositioned = false;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    CChatDoc* m_document = nullptr;
    CChatView* m_view = nullptr;
    bool m_ownsDocument = false;
};
