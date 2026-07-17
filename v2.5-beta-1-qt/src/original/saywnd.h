// Ported from v2.5-beta-1-modern/saywnd.h.

#pragma once

#include "rtfctrl.h"

#include <QFont>
#include <QToolBar>
#include <QWidget>

#define ID_SAYCTRL 1
#define SAYNBUTTONS 7
#define SB_SAY 0x01
#define SB_THINK 0x02
#define SB_WHISPER 0x04
#define SB_ACTION 0x08
#define SB_WACTION 0x10
#define SB_SOUND 0x20
#define SB_WSOUND 0x40

class QKeyEvent;
class QMimeData;
class QPaintEvent;
class QResizeEvent;

class CSayWnd;

class CSayCtrl : public CRtfCtrl {
public:
    explicit CSayCtrl(CSayWnd* parent);

    void ValidateIMEEntry();
    bool IsEmpty() const;
    bool bEmptyAndIndent();
    bool m_bWhisperSay = false;
    bool m_bEnChangeFreeze = false;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;

private:
    void enforceMaximumLength();

    CSayWnd* m_sayWnd = nullptr;
    bool m_bTruncating = false;
};

class CSayToolBar : public QToolBar {
public:
    explicit CSayToolBar(QWidget* parent = nullptr);
};

class CSayWnd : public QWidget {
public:
    explicit CSayWnd(QWidget* parent = nullptr);
    CSayWnd(bool whisperSay, unsigned long buttons, QWidget* parent = nullptr);

    static void SetDefaultButtons(unsigned long buttons) { m_dwDefaultButtons = buttons; }
    void SetToolBarInfo(bool whisperSay, unsigned long buttons);
    void SetFormattingToolBarInfo(void*, int, int, int, int, int) {}
    void SwitchSelectionFormat(unsigned short format);
    unsigned short wGetConsistentFormats() const;
    bool IsEmpty() const;
    bool TextEntered() const;
    void SendReturn();
    void SendSayFromReturn();
    void SetFocusToSayWnd();
    BOOL SetFont(QFont& font, BOOL matchButtonsToSelection = FALSE);
    CSayCtrl* GetSayEdit() const { return m_wndSayCtrl; }
    CSayToolBar* GetSayBar() const { return m_wndSayBar; }

    void OnActionsSay();
    void OnActionsThink();
    void OnActionsWhisper();
    void OnSendAction();
    void OnPlaySound();

    bool m_bWhisperSay = false;
    unsigned int m_cntBalloons = 0;
    unsigned int m_cxSayBar = 0;
    unsigned long m_dwButtons = 0;
    QFont m_fontText;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void initialize();
    void createSayBar();
    bool bLegalToSend(bool privateMessage = false);
    void showOriginalMessage(const QString& identifier);

    CSayCtrl* m_wndSayCtrl = nullptr;
    CSayToolBar* m_wndSayBar = nullptr;
    static unsigned long m_dwDefaultButtons;
};

CSayWnd* GetSay();
