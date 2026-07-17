// Ported from v2.5-beta-1-modern/spltchat.h.

#pragma once

#include <QSplitter>

class QKeyEvent;
class QResizeEvent;

class CSplitChat : public QSplitter {
public:
    explicit CSplitChat(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyOriginalSizes();

    int m_nPctBottom = 70;
    bool m_bApplyingSizes = false;
};

class CSplitChatV : public QSplitter {
public:
    explicit CSplitChatV(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyOriginalSizes();

    int m_nPctLeft = 80;
    bool m_bApplyingSizes = false;
};

class CSplitSay : public QSplitter {
public:
    explicit CSplitSay(QWidget* parent = nullptr);
    bool ReadyToSize() const { return count() == 2; }
    int SayMinimumPixels() const;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyOriginalSizes(bool initialSizing);

    int m_nPixelsClient = 0;
    int m_nPixelsSayMin = 0;
    int m_nPixelsSay = 0;
    bool m_bInitialSizingDone = false;
    bool m_bApplyingSizes = false;
};
