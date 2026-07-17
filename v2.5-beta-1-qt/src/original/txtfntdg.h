// Port boundary for v2.5-beta-1-modern/txtfntdg.h.
// LOGFONT/CHARFORMAT conversion retains the original public boundary. The Qt
// dialog below replaces only CWin4FontDialog/RichEdit controls.

#pragma once

#include "defines.h"
#include "wincompat.h"

#include <QDialog>

#include <array>
#include <memory>

class CTextCore;
class QCheckBox;
class QComboBox;
class QFontComboBox;
class QTextEdit;
class QShowEvent;

BOOL bCHARFORMATToLOGFONT(CHARFORMAT* charFormat, DWORD mask, LOGFONT* font);

class CMyFontDialog : public QDialog
{
public:
    explicit CMyFontDialog(QWidget* parent = nullptr);
    ~CMyFontDialog() override;

    int GetMessageType() const;
    BOOL SetupRichPreview();
    void OnChangeMessageType();
    void HandleSelection();
    void SetMessageType(int messageType);
    DWORD GetCharFormatMask() const;
    void OnFontChange();
    void OnStrikeoutChange();
    void OnUnderlineChange();
    void UpdateFromCf();

    BOOL m_bSetFromCf = FALSE;
    CHARFORMAT m_cfArray[NFONTS]{};

public slots:
    void accept() override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildResourceDialog();
    void capturePreviewFormats();
    void syncFontControls();
    void syncMessageType();
    void populateStyles(const QString& family);
    void populateSizes(const QString& family, const QString& style,
                       int selectedPointSize = -1);
    void populateScripts(const QString& family, int selectedCharSet = -1);
    std::array<bool, NFONTS> selectedTypes() const;
    void applyCharFormat(const CHARFORMAT& format);

    BOOL m_bClosing = FALSE;
    BOOL m_bFontCtlEvents = TRUE;
    BOOL m_bSettingUI = FALSE;
    BOOL m_bInitialized = FALSE;
    int m_nLines = 0;
    std::array<int, NFONTS> m_mesgStarts{};
    std::array<int, NFONTS> m_mesgEnds{};
    std::array<BYTE, NFONTS> m_mesgTypes{};
    std::unique_ptr<CTextCore> m_richCore;
    QTextEdit* m_richPreview = nullptr;
    QComboBox* m_messageType = nullptr;
    QFontComboBox* m_face = nullptr;
    QComboBox* m_style = nullptr;
    QComboBox* m_pointSize = nullptr;
    QComboBox* m_color = nullptr;
    QComboBox* m_script = nullptr;
    QCheckBox* m_strikeout = nullptr;
    QCheckBox* m_underline = nullptr;
};
