// Qt-only infrastructure. Applies fixed Windows 98 colors instead of host system colors.

#include "win98palette.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace {
constexpr int kFace = 192;
constexpr int kShadow = 128;
constexpr int kDkShadow = 64;
constexpr int kLight = 223;
constexpr int kHighlightR = 0;
constexpr int kHighlightG = 0;
constexpr int kHighlightB = 128;
}

void win98palette::apply(QApplication& app)
{
    if (QStyle* windows = QStyleFactory::create(QStringLiteral("Windows"))) {
        app.setStyle(windows);
    }

    QPalette pal;
    pal.setColor(QPalette::Window, QColor(kFace, kFace, kFace));
    pal.setColor(QPalette::WindowText, Qt::black);
    pal.setColor(QPalette::Base, Qt::white);
    pal.setColor(QPalette::AlternateBase, QColor(kFace, kFace, kFace));
    pal.setColor(QPalette::Text, Qt::black);
    pal.setColor(QPalette::Button, QColor(kFace, kFace, kFace));
    pal.setColor(QPalette::ButtonText, Qt::black);
    pal.setColor(QPalette::Light, QColor(kLight, kLight, kLight));
    pal.setColor(QPalette::Midlight, QColor(210, 210, 210));
    pal.setColor(QPalette::Mid, QColor(kShadow, kShadow, kShadow));
    pal.setColor(QPalette::Dark, QColor(kDkShadow, kDkShadow, kDkShadow));
    pal.setColor(QPalette::Shadow, Qt::black);
    pal.setColor(QPalette::Highlight, QColor(kHighlightR, kHighlightG, kHighlightB));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(255, 255, 225));
    pal.setColor(QPalette::ToolTipText, Qt::black);
    app.setPalette(pal);

    app.setStyleSheet(QStringLiteral(
        "QMainWindow, QDialog, QWidget { background: rgb(192,192,192); color: black; }"
        "QTextEdit, QLineEdit, QListWidget, QComboBox { background: white; color: black; }"
        "QMenuBar, QMenu, QToolBar, QStatusBar, QTabBar::tab { background: rgb(192,192,192); color: black; }"
        "QMenu::item:selected { background: rgb(0,0,128); color: white; }"
        "QSplitter::handle { background: rgb(192,192,192); }"
    ));
}
