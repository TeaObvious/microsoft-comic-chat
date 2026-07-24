// Ported from v2.5-beta-1-modern/status.h.

#pragma once

#include "textview.h"

class CIrcPrint;

class CStatusView : public CTextView {
public:
    explicit CStatusView(CChatDoc* document, QWidget* parent = nullptr);
    BOOL OnUpdateViewComics(BOOL* checked = nullptr) const;
    BOOL OnUpdateViewText(BOOL* checked = nullptr) const;

protected:
    int LoadContextMenu(QMenu& menu) override;
};

CStatusView* GetStatusView();
void AddToStatus(CIrcPrint& ircPrint, const QString& line,
                 CDWordArray* formatting = nullptr);
