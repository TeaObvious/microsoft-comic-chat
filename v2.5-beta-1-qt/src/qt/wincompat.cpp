// Qt replacement for the Win32 process ANSI-code-page query used by the
// original IRC and international-text paths.

#include "wincompat.h"

#include <QLocale>

UINT GetACP()
{
    const QLocale locale;
    switch (locale.language()) {
    case QLocale::Japanese: return 932;
    case QLocale::Korean: return 949;
    case QLocale::Chinese:
        return locale.territory() == QLocale::Taiwan
                || locale.territory() == QLocale::HongKong
                || locale.territory() == QLocale::Macau
            ? 950 : 936;

    case QLocale::Czech:
    case QLocale::Hungarian:
    case QLocale::Polish:
    case QLocale::Slovenian:
    case QLocale::Slovak:
    case QLocale::Croatian:
    case QLocale::Romanian:
        return 1250;

    case QLocale::Belarusian:
    case QLocale::Bulgarian:
    case QLocale::Macedonian:
    case QLocale::Russian:
    case QLocale::Ukrainian:
        return 1251;

    case QLocale::Greek: return 1253;
    case QLocale::Turkish: return 1254;
    case QLocale::Hebrew: return 1255;
    case QLocale::Arabic: return 1256;
    case QLocale::Estonian:
    case QLocale::Latvian:
    case QLocale::Lithuanian:
        return 1257;
    case QLocale::Vietnamese: return 1258;
    case QLocale::Thai: return 874;
    default: return 1252;
    }
}
