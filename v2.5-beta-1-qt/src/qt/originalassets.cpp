#include "originalassets.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#ifndef COMIC_CHAT_ORIGINAL_ROOT
#error COMIC_CHAT_ORIGINAL_ROOT must be supplied by CMake
#endif
#ifndef COMIC_CHAT_ARTIFACTS_ROOT
#error COMIC_CHAT_ARTIFACTS_ROOT must be supplied by CMake
#endif
#ifndef COMIC_CHAT_V1_SHARED_ROOT
#error COMIC_CHAT_V1_SHARED_ROOT must be supplied by CMake
#endif

QString originalAssetRoot()
{
    static const QString root = QFileInfo(QString::fromUtf8(COMIC_CHAT_ORIGINAL_ROOT)).canonicalFilePath();
    return root;
}

QString originalArtifactsRoot()
{
    static const QString root = QFileInfo(
        QString::fromUtf8(COMIC_CHAT_ARTIFACTS_ROOT)).canonicalFilePath();
    return root;
}

QString originalV1SharedRoot()
{
    static const QString root = QFileInfo(
        QString::fromUtf8(COMIC_CHAT_V1_SHARED_ROOT)).canonicalFilePath();
    return root;
}

QString originalAssetDirectoryPath(const QString& relativePath)
{
    const QString root = originalAssetRoot();
    if (root.isEmpty() || relativePath.isEmpty()
        || QDir::isAbsolutePath(relativePath)) {
        return {};
    }

    const QString clean = QDir::cleanPath(relativePath);
    if (clean == QLatin1String("..")
        || clean.startsWith(QLatin1String("../"))
        || clean == QLatin1String(".")) {
        return {};
    }

    QString resolved = root;
    const QStringList components = clean.split(QLatin1Char('/'),
                                                Qt::SkipEmptyParts);
    for (const QString& component : components) {
        QDir directory(resolved);
        QString matched = component;
        const QFileInfo exact(directory.filePath(component));
        if (!exact.exists() || !exact.isDir()) {
            matched.clear();
            const QFileInfoList entries = directory.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
            for (const QFileInfo& entry : entries) {
                if (entry.fileName().compare(component,
                                             Qt::CaseInsensitive) == 0) {
                    matched = entry.fileName();
                    break;
                }
            }
        }
        resolved = directory.filePath(matched.isEmpty() ? component : matched);
    }

    const QString normalized = QDir::cleanPath(resolved);
    const QString rootPrefix = root + QDir::separator();
    if (!normalized.startsWith(rootPrefix)) return {};

    const QFileInfo info(normalized);
    if (!info.exists() || !info.isDir()) return normalized;
    const QString canonical = info.canonicalFilePath();
    return canonical.startsWith(rootPrefix) ? canonical : QString();
}

QString originalFileInDirectoryPath(const QString& directoryPath,
                                    const QString& fileName)
{
    if (directoryPath.isEmpty() || fileName.isEmpty()
        || QDir::isAbsolutePath(fileName)
        || QFileInfo(fileName).fileName() != fileName) {
        return {};
    }

    const QFileInfo directoryInfo(directoryPath);
    if (!directoryInfo.exists() || !directoryInfo.isDir()) return {};
    const QString directory = directoryInfo.canonicalFilePath();
    const QString root = originalAssetRoot();
    if (root.isEmpty()
        || (directory != root
            && !directory.startsWith(root + QDir::separator()))) {
        return {};
    }

    QFileInfo match(QDir(directory).filePath(fileName));
    if (!match.exists() || !match.isFile()) {
        const QFileInfoList files = QDir(directory).entryInfoList(
            QDir::Files, QDir::NoSort);
        for (const QFileInfo& candidate : files) {
            if (candidate.fileName().compare(fileName,
                                             Qt::CaseInsensitive) == 0) {
                match = candidate;
                break;
            }
        }
    }
    if (!match.exists() || !match.isFile()) return {};

    const QString canonical = match.canonicalFilePath();
    return canonical.startsWith(directory + QDir::separator())
        ? canonical : QString();
}

QString originalAssetPath(const QString& relativePath)
{
    const QString root = originalAssetRoot();
    if (root.isEmpty() || relativePath.isEmpty() || QDir::isAbsolutePath(relativePath)) {
        return QString();
    }

    const QString clean = QDir::cleanPath(relativePath);
    if (clean == QLatin1String("..") || clean.startsWith(QLatin1String("../")) || clean == QLatin1String(".")) {
        return QString();
    }

    const QFileInfo info(QDir(root).filePath(clean));
    if (!info.exists() || !info.isFile()) {
        return QString();
    }

    const QString canonical = info.canonicalFilePath();
    const QString rootPrefix = root + QDir::separator();
    return canonical.startsWith(rootPrefix) ? canonical : QString();
}

QString originalArtifactPath(const QString& relativePath)
{
    const QString root = originalArtifactsRoot();
    if (root.isEmpty() || relativePath.isEmpty() || QDir::isAbsolutePath(relativePath))
        return {};
    const QString clean = QDir::cleanPath(relativePath);
    if (clean == QLatin1String("..") || clean.startsWith(QLatin1String("../"))
        || clean == QLatin1String(".")) return {};
    const QFileInfo info(QDir(root).filePath(clean));
    if (!info.exists() || !info.isFile()) return {};
    const QString canonical = info.canonicalFilePath();
    return canonical.startsWith(root + QDir::separator()) ? canonical : QString();
}

QString originalV1SharedPath(const QString& fileName)
{
    const QString root = originalV1SharedRoot();
    if (root.isEmpty() || fileName.isEmpty() || QDir::isAbsolutePath(fileName))
        return {};
    const QString clean = QDir::cleanPath(fileName);
    if (clean == QLatin1String("..") || clean.startsWith(QLatin1String("../"))
        || clean == QLatin1String(".")) return {};
    const QFileInfo info(QDir(root).filePath(clean));
    if (!info.exists() || !info.isFile()) return {};
    const QString canonical = info.canonicalFilePath();
    return canonical.startsWith(root + QDir::separator()) ? canonical : QString();
}

QString originalResourcePath(const QString& fileName)
{
    return originalAssetPath(QStringLiteral("res/%1").arg(fileName));
}

QString originalComicArtPath(const QString& fileName)
{
    return originalAssetPath(QStringLiteral("comicart/%1").arg(fileName));
}

QString originalArtPackPath(const QString& fileName)
{
    return originalAssetPath(QStringLiteral("artpack1/%1").arg(fileName));
}

QString originalArtPackArchivePath(const QString& fileName)
{
    return originalAssetPath(QStringLiteral("artpack1/archive/%1").arg(fileName));
}

int registerOriginalComicFont()
{
    static const int fontId = QFontDatabase::addApplicationFont(
        originalV1SharedPath(QStringLiteral("comic.ttf")));
    return fontId;
}

namespace {
QString decodeRcString(QString value)
{
    QString decoded;
    decoded.reserve(value.size());
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar character = value[index];
        if (character != QLatin1Char('\\') || index + 1 >= value.size()) {
            decoded.append(character);
            continue;
        }
        const QChar escaped = value[++index];
        if (escaped == QLatin1Char('n')) {
            decoded.append(QLatin1Char('\n'));
        } else if (escaped == QLatin1Char('r')) {
            decoded.append(QLatin1Char('\r'));
        } else if (escaped == QLatin1Char('t')) {
            decoded.append(QLatin1Char('\t'));
        } else {
            decoded.append(escaped);
        }
    }
    return decoded;
}

bool parseRcQuotedLiteral(const QString& source, qsizetype firstQuote,
                          QString* value, qsizetype* end)
{
    if (firstQuote < 0 || firstQuote >= source.size()
        || source[firstQuote] != QLatin1Char('"')) {
        return false;
    }

    QString encoded;
    for (qsizetype index = firstQuote + 1; index < source.size(); ++index) {
        if (source[index] == QLatin1Char('\\') && index + 1 < source.size()) {
            encoded.append(source[index]);
            encoded.append(source[++index]);
            continue;
        }
        if (source[index] == QLatin1Char('"')) {
            // Microsoft RC represents a quote inside a string by doubling it.
            if (index + 1 < source.size()
                && source[index + 1] == QLatin1Char('"')) {
                encoded.append(QLatin1Char('"'));
                ++index;
                continue;
            }
            if (value) *value = decodeRcString(encoded);
            if (end) *end = index + 1;
            return true;
        }
        encoded.append(source[index]);
    }
    return false;
}

QHash<QString, QString> parseStringTables(const QString& source)
{
    QHash<QString, QString> result;
    const QStringList lines = source.split(QLatin1Char('\n'));
    const QRegularExpression identifierExpression(
        QStringLiteral(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*,?\s*)"));
    bool awaitingBegin = false;
    bool inStringTable = false;

    for (const QString& sourceLine : lines) {
        const QString trimmed = sourceLine.trimmed();
        if (!inStringTable && trimmed.startsWith(QLatin1String("STRINGTABLE"))) {
            awaitingBegin = true;
            continue;
        }
        if (awaitingBegin) {
            if (trimmed == QLatin1String("BEGIN")) {
                awaitingBegin = false;
                inStringTable = true;
            }
            continue;
        }
        if (!inStringTable) continue;
        if (trimmed == QLatin1String("END")) {
            inStringTable = false;
            continue;
        }

        const QRegularExpressionMatch identifierMatch =
            identifierExpression.match(sourceLine);
        if (!identifierMatch.hasMatch()) continue;
        const qsizetype firstQuote = sourceLine.indexOf(
            QLatin1Char('"'), identifierMatch.capturedEnd());
        QString value;
        qsizetype end = 0;
        if (parseRcQuotedLiteral(sourceLine, firstQuote, &value, &end)
            && sourceLine.mid(end).trimmed().isEmpty()) {
            result.insert(identifierMatch.captured(1), value);
        }
    }
    return result;
}

QString originalRcText()
{
    static const QString text = [] {
        QFile file(originalAssetPath(QStringLiteral("chat.rc")));
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    }();
    return text;
}

const QHash<QString, QString>& resourceStrings()
{
    static const QHash<QString, QString> strings = [] {
        return parseStringTables(originalRcText());
    }();
    return strings;
}

const QHash<int, QString>& numericResourceNames()
{
    static const QHash<int, QString> names = [] {
        QHash<int, QString> result;
        QFile file(originalAssetPath(QStringLiteral("resource.h")));
        if (!file.open(QIODevice::ReadOnly)) return result;
        const QString source = QString::fromUtf8(file.readAll());
        const QRegularExpression definition(
            QStringLiteral(R"((?m)^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+([0-9]+)\s*$)"));
        QRegularExpressionMatchIterator matches = definition.globalMatch(source);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const QString name = match.captured(1);
            if (resourceStrings().contains(name)) {
                result.insert(match.captured(2).toInt(), name);
            }
        }
        return result;
    }();
    return names;
}

const QHash<QString, QString>& textViewResourceStrings()
{
    static const QHash<QString, QString> strings = [] {
        QFile file(originalArtifactPath(QStringLiteral("inc/textview.rc")));
        if (!file.open(QIODevice::ReadOnly)) return QHash<QString, QString>{};
        return parseStringTables(QString::fromUtf8(file.readAll()));
    }();
    return strings;
}

QStringList resourceBlock(const QString& resourceIdentifier,
                          const QString& resourceType,
                          QString* declaration = nullptr)
{
    const QString source = originalRcText();
    if (source.isEmpty() || resourceIdentifier.isEmpty() || resourceType.isEmpty()) {
        return {};
    }

    const QStringList lines = source.split(QLatin1Char('\n'));
    const QRegularExpression header(
        QStringLiteral(R"(^\s*)")
        + QRegularExpression::escape(resourceIdentifier)
        + QStringLiteral(R"(\s+)")
        + QRegularExpression::escape(resourceType)
        + QStringLiteral(R"((?:\s+.*)?$)"));

    int index = 0;
    for (; index < lines.size(); ++index) {
        if (header.match(lines[index]).hasMatch()) {
            if (declaration) {
                *declaration = lines[index].trimmed();
            }
            break;
        }
    }
    if (index == lines.size()) {
        return {};
    }

    while (++index < lines.size() && lines[index].trimmed() != QLatin1String("BEGIN")) {
    }
    if (index == lines.size()) {
        return {};
    }

    QStringList result;
    int depth = 1;
    for (++index; index < lines.size() && depth > 0; ++index) {
        const QString trimmed = lines[index].trimmed();
        if (trimmed == QLatin1String("BEGIN")) {
            ++depth;
        } else if (trimmed == QLatin1String("END")) {
            --depth;
        }
        if (depth > 0) {
            result.append(lines[index]);
        }
    }
    return depth == 0 ? result : QStringList{};
}

bool parseQuotedPrefix(const QString& source, QString* value, qsizetype* end)
{
    const qsizetype firstQuote = source.indexOf(QLatin1Char('"'));
    return parseRcQuotedLiteral(source, firstQuote, value, end);
}

QStringList commaFields(const QString& source)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype index = 0; index < source.size(); ++index) {
        const QChar character = source[index];
        if (character == QLatin1Char('\\') && quoted
            && index + 1 < source.size()) {
            field.append(character);
            field.append(source[++index]);
            continue;
        }
        if (character == QLatin1Char('"')) {
            if (quoted && index + 1 < source.size()
                && source[index + 1] == QLatin1Char('"')) {
                field.append(character);
                field.append(source[++index]);
                continue;
            }
            quoted = !quoted;
            field.append(character);
            continue;
        }
        if (character == QLatin1Char(',') && !quoted) {
            if (!field.trimmed().isEmpty()) fields.append(field.trimmed());
            field.clear();
            continue;
        }
        field.append(character);
    }
    if (!field.trimmed().isEmpty()) fields.append(field.trimmed());
    return fields;
}

bool isDialogControlStart(const QString& line)
{
    static const QRegularExpression expression(QStringLiteral(
        R"(^(AUTO3STATE|AUTOCHECKBOX|AUTORADIOBUTTON|CHECKBOX|COMBOBOX|CONTROL|CTEXT|DEFPUSHBUTTON|EDITTEXT|GROUPBOX|ICON|LISTBOX|LTEXT|PUSHBOX|PUSHBUTTON|RADIOBUTTON|RTEXT|SCROLLBAR|STATE3)\b)"));
    return expression.match(line).hasMatch();
}

OriginalDialogControl parseDialogControl(const QString& statement)
{
    OriginalDialogControl control;
    const qsizetype typeEnd = statement.indexOf(
        QRegularExpression(QStringLiteral("\\s")));
    if (typeEnd < 0) return control;
    control.type = statement.left(typeEnd);
    QString remainder = statement.mid(typeEnd).trimmed();

    static const QStringList textTypes = {
        QStringLiteral("AUTO3STATE"),
        QStringLiteral("AUTOCHECKBOX"),
        QStringLiteral("AUTORADIOBUTTON"),
        QStringLiteral("CHECKBOX"),
        QStringLiteral("CONTROL"),
        QStringLiteral("CTEXT"),
        QStringLiteral("DEFPUSHBUTTON"),
        QStringLiteral("GROUPBOX"),
        QStringLiteral("LTEXT"),
        QStringLiteral("PUSHBOX"),
        QStringLiteral("PUSHBUTTON"),
        QStringLiteral("RADIOBUTTON"),
        QStringLiteral("RTEXT"),
        QStringLiteral("STATE3")
    };

    const bool hasQuotedText =
        remainder.startsWith(QLatin1Char('"'));
    if (textTypes.contains(control.type) && hasQuotedText) {
        qsizetype quoteEnd = 0;
        if (!parseRcQuotedLiteral(remainder, 0, &control.text, &quoteEnd))
            return {};
        remainder = remainder.mid(quoteEnd).trimmed();
        if (remainder.startsWith(QLatin1Char(','))) remainder.remove(0, 1);
    }

    control.fields = commaFields(remainder);
    // CONTROL accepts either a quoted string or an ordinal/resource token as
    // its first field.  parseRcQuotedLiteral handled the former above; retain
    // the latter as the source text and normalize the remaining fields to the
    // same identifier,class,style,x,y,w,h shape.
    if (control.type == QLatin1String("CONTROL")
        && !hasQuotedText
        && !control.fields.isEmpty()) {
        control.text = control.fields.takeFirst();
    }
    int coordinateStart = 1;
    if (control.type == QLatin1String("ICON")) {
        if (control.fields.size() >= 2)
            control.identifier = control.fields[1];
        coordinateStart = 2;
    } else if (!control.fields.isEmpty()) {
        control.identifier = control.fields[0];
        if (control.type == QLatin1String("CONTROL")) coordinateStart = 3;
    }

    if (control.fields.size() >= coordinateStart + 4) {
        bool xOk = false;
        bool yOk = false;
        bool widthOk = false;
        bool heightOk = false;
        const int x = control.fields[coordinateStart].toInt(&xOk);
        const int y = control.fields[coordinateStart + 1].toInt(&yOk);
        const int width = control.fields[coordinateStart + 2].toInt(&widthOk);
        const int height = control.fields[coordinateStart + 3].toInt(&heightOk);
        if (xOk && yOk && widthOk && heightOk) {
            control.x = x;
            control.y = y;
            control.width = width;
            control.height = height;
        }
    }
    if (control.type == QLatin1String("CONTROL")
        && control.fields.size() > 2) {
        control.style = control.fields[2];
    } else if (control.fields.size() > coordinateStart + 4) {
        control.style = control.fields[coordinateStart + 4];
    }
    control.visible = !control.style.contains(
        QRegularExpression(QStringLiteral(R"(\bNOT\s+WS_VISIBLE\b)")));
    return control;
}

OriginalDialogResource parseDialogResource(const QString& resourceIdentifier)
{
    OriginalDialogResource result;
    if (resourceIdentifier.isEmpty()) return result;
    const QStringList lines = originalRcText().split(QLatin1Char('\n'));
    const QRegularExpression header(
        QStringLiteral(R"(^\s*)")
        + QRegularExpression::escape(resourceIdentifier)
        + QStringLiteral(R"(\s+DIALOG(?:EX)?\s+(.+)$)"));
    const QRegularExpression dimensions(QStringLiteral(
        R"((-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*$)"));

    int index = 0;
    for (; index < lines.size(); ++index) {
        const QRegularExpressionMatch match = header.match(lines[index]);
        if (!match.hasMatch()) continue;
        const QRegularExpressionMatch size = dimensions.match(match.captured(1));
        if (size.hasMatch()) {
            result.x = size.captured(1).toInt();
            result.y = size.captured(2).toInt();
            result.width = size.captured(3).toInt();
            result.height = size.captured(4).toInt();
        }
        break;
    }
    if (index == lines.size()) return result;

    while (++index < lines.size()) {
        const QString line = lines[index].trimmed();
        if (line.startsWith(QLatin1String("CAPTION"))) {
            parseQuotedPrefix(line, &result.caption, nullptr);
        } else if (line.startsWith(QLatin1String("FONT"))) {
            const QStringList fields = commaFields(line.mid(4).trimmed());
            if (!fields.isEmpty()) {
                bool sizeOk = false;
                const int pointSize = fields.first().toInt(&sizeOk);
                if (sizeOk) result.fontPointSize = pointSize;
            }
            const qsizetype firstQuote = line.indexOf(QLatin1Char('"'));
            if (firstQuote >= 0)
                parseRcQuotedLiteral(line, firstQuote, &result.fontFamily,
                                     nullptr);
        }
        if (line == QLatin1String("BEGIN")) break;
    }
    if (index == lines.size()) return result;

    QString statement;
    auto finishStatement = [&] {
        if (statement.isEmpty()) return;
        OriginalDialogControl control = parseDialogControl(statement);
        if (!control.type.isEmpty() && !control.identifier.isEmpty())
            result.controls.append(control);
        statement.clear();
    };
    while (++index < lines.size()) {
        const QString line = lines[index].trimmed();
        if (line == QLatin1String("END")) {
            finishStatement();
            break;
        }
        if (line.isEmpty()) continue;
        if (isDialogControlStart(line)) finishStatement();
        if (!statement.isEmpty()) statement += QLatin1Char(' ');
        statement += line;
    }
    return result;
}

QList<OriginalMenuItem> parseMenuItems(const QStringList& lines, int* lineIndex)
{
    QList<OriginalMenuItem> result;
    while (*lineIndex < lines.size()) {
        const QString line = lines[*lineIndex].trimmed();
        if (line == QLatin1String("END")) {
            ++*lineIndex;
            break;
        }
        if (line == QLatin1String("BEGIN") || line.isEmpty()) {
            ++*lineIndex;
            continue;
        }
        if (line.startsWith(QLatin1String("POPUP"))) {
            OriginalMenuItem item;
            item.type = OriginalMenuItemType::Popup;
            qsizetype quoteEnd = 0;
            if (!parseQuotedPrefix(line, &item.text, &quoteEnd)) {
                ++*lineIndex;
                continue;
            }
            item.flags = commaFields(line.mid(quoteEnd));
            ++*lineIndex;
            if (*lineIndex < lines.size()
                && lines[*lineIndex].trimmed() == QLatin1String("BEGIN")) {
                ++*lineIndex;
                item.children = parseMenuItems(lines, lineIndex);
            }
            result.append(item);
            continue;
        }
        if (line.startsWith(QLatin1String("MENUITEM"))) {
            OriginalMenuItem item;
            if (line.mid(8).trimmed().startsWith(QLatin1String("SEPARATOR"))) {
                item.type = OriginalMenuItemType::Separator;
                result.append(item);
                ++*lineIndex;
                continue;
            }
            item.type = OriginalMenuItemType::Command;
            qsizetype quoteEnd = 0;
            if (parseQuotedPrefix(line, &item.text, &quoteEnd)) {
                const QStringList fields = commaFields(line.mid(quoteEnd));
                if (!fields.isEmpty()) {
                    item.commandIdentifier = fields.first();
                    item.flags = fields.mid(1);
                    result.append(item);
                }
            }
            ++*lineIndex;
            continue;
        }
        ++*lineIndex;
    }
    return result;
}
}

QString originalFileResourcePath(const QString& resourceIdentifier,
                                 const QString& resourceType)
{
    const QString source = originalRcText();
    if (source.isEmpty() || resourceIdentifier.isEmpty() || resourceType.isEmpty()) {
        return QString();
    }
    const QRegularExpression expression(
        QStringLiteral(R"((?m)^\s*)")
        + QRegularExpression::escape(resourceIdentifier)
        + QStringLiteral(R"(\s+)")
        + QRegularExpression::escape(resourceType)
        + QStringLiteral(R"rc([^\r\n]*"((?:\\.|[^"\\])*)"\s*$)rc"));
    const QRegularExpressionMatch match = expression.match(source);
    if (!match.hasMatch()) {
        return QString();
    }
    QString relativePath = decodeRcString(match.captured(1));
    relativePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return originalAssetPath(relativePath);
}

QString originalResourceString(const QString& identifier)
{
    return resourceStrings().value(identifier);
}

QString originalResourceString(int identifier)
{
    return originalResourceString(numericResourceNames().value(identifier));
}

QString originalTextViewResourceString(const QString& identifier)
{
    return textViewResourceStrings().value(identifier);
}

OriginalDialogResource originalDialogResource(
    const QString& resourceIdentifier)
{
    return parseDialogResource(resourceIdentifier);
}

QString originalDialogCaption(const QString& resourceIdentifier)
{
    return originalDialogResource(resourceIdentifier).caption;
}

QString originalDialogControlText(const QString& resourceIdentifier,
                                  const QString& controlIdentifier,
                                  int occurrence)
{
    if (occurrence < 0) return {};
    for (const OriginalDialogControl& control
         : originalDialogResource(resourceIdentifier).controls) {
        if (control.identifier != controlIdentifier) continue;
        if (occurrence-- == 0) return control.text;
    }
    return {};
}

QStringList originalDialogInitStrings(const QString& resourceIdentifier,
                                      const QString& controlIdentifier)
{
    QStringList result;
    const QStringList block = resourceBlock(resourceIdentifier,
                                             QStringLiteral("DLGINIT"));
    for (int line = 0; line < block.size(); ++line) {
        const QStringList header = commaFields(block[line].trimmed());
        if (header.size() < 3 || header[0] != controlIdentifier) continue;
        bool lengthOK = false;
        int bytesLeft = header[2].toInt(&lengthOK, 0);
        if (!lengthOK || bytesLeft <= 0) continue;

        QByteArray encoded;
        while (bytesLeft > 0 && ++line < block.size()) {
            const QStringList words = commaFields(block[line].trimmed());
            for (const QString& wordText : words) {
                bool wordOK = false;
                const ushort word = wordText.toUShort(&wordOK, 0);
                if (!wordOK) continue;
                encoded.append(static_cast<char>(word & 0xff));
                --bytesLeft;
                if (bytesLeft > 0) {
                    encoded.append(static_cast<char>((word >> 8) & 0xff));
                    --bytesLeft;
                }
                if (bytesLeft == 0) break;
            }
        }
        const int terminator = encoded.indexOf('\0');
        if (terminator >= 0) encoded.truncate(terminator);
        result.append(QString::fromLatin1(encoded));
    }
    return result;
}

QString originalMenuItemText(const QString& commandIdentifier)
{
    const QString source = originalRcText();
    if (source.isEmpty() || commandIdentifier.isEmpty()) {
        return QString();
    }
    const QRegularExpression expression(
        QStringLiteral("MENUITEM\\s+\"((?:\\\\.|[^\"\\\\])*)\"\\s*,\\s*")
        + QRegularExpression::escape(commandIdentifier)
        + QStringLiteral(R"((?:\s|$))"));
    const QRegularExpressionMatch match = expression.match(source);
    return match.hasMatch() ? decodeRcString(match.captured(1)) : QString();
}

QList<OriginalMenuItem> originalMenuResource(const QString& resourceIdentifier)
{
    const QStringList block = resourceBlock(resourceIdentifier, QStringLiteral("MENU"));
    int lineIndex = 0;
    return parseMenuItems(block, &lineIndex);
}

OriginalToolbarResource originalToolbarResource(const QString& resourceIdentifier)
{
    OriginalToolbarResource result;
    QString declaration;
    const QStringList block = resourceBlock(resourceIdentifier, QStringLiteral("TOOLBAR"),
                                             &declaration);
    const QRegularExpression sizeExpression(QStringLiteral(R"((\d+)\s*,\s*(\d+)\s*$)"));
    const QRegularExpressionMatch sizeMatch = sizeExpression.match(declaration);
    if (sizeMatch.hasMatch()) {
        result.buttonWidth = sizeMatch.captured(1).toInt();
        result.buttonHeight = sizeMatch.captured(2).toInt();
    }

    for (const QString& sourceLine : block) {
        const QString line = sourceLine.trimmed();
        if (line == QLatin1String("SEPARATOR")) {
            result.items.append({true, QString()});
        } else if (line.startsWith(QLatin1String("BUTTON"))) {
            const QString identifier = line.mid(6).trimmed().section(
                QRegularExpression(QStringLiteral("\\s+")), 0, 0);
            if (!identifier.isEmpty()) {
                result.items.append({false, identifier});
            }
        }
    }
    return result;
}

QList<OriginalAccelerator> originalAcceleratorResource(const QString& resourceIdentifier)
{
    QList<OriginalAccelerator> result;
    const QStringList block = resourceBlock(resourceIdentifier, QStringLiteral("ACCELERATORS"));
    for (const QString& sourceLine : block) {
        const QString line = sourceLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList fields = commaFields(line);
        if (fields.size() < 2) {
            continue;
        }
        OriginalAccelerator accelerator;
        accelerator.key = fields[0];
        if (accelerator.key.size() >= 2
            && accelerator.key.front() == QLatin1Char('"')
            && accelerator.key.back() == QLatin1Char('"')) {
            accelerator.key = decodeRcString(accelerator.key.mid(1, accelerator.key.size() - 2));
        }
        accelerator.commandIdentifier = fields[1];
        for (int field = 2; field < fields.size(); ++field) {
            const QString flag = fields[field];
            accelerator.virtualKey = accelerator.virtualKey || flag == QLatin1String("VIRTKEY");
            accelerator.alt = accelerator.alt || flag == QLatin1String("ALT");
            accelerator.control = accelerator.control || flag == QLatin1String("CONTROL");
            accelerator.shift = accelerator.shift || flag == QLatin1String("SHIFT");
            accelerator.noInvert = accelerator.noInvert || flag == QLatin1String("NOINVERT");
        }
        result.append(accelerator);
    }
    return result;
}
