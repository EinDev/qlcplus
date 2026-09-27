/*
  Q Light Controller Plus - Control API
  apifixturedefsdomain.cpp

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>
#include <QXmlStreamReader>
#include <algorithm>

#include "apifixturedefsdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcfixturehead.h"
#include "qlcchannel.h"
#include "qlccapability.h"
#include "qlcphysical.h"
#include "qlcconfig.h"
#include "qlcfile.h"
#include "fixture.h"
#include "doc.h"

namespace {

// Domain-specific error codes (docs/api-spec/fragments/fixturedefs-notes.md)
const QString ErrSystemReadOnly = QStringLiteral("FIXTUREDEFS_SYSTEM_READONLY");
const QString ErrActsOnSelf = QStringLiteral("FIXTUREDEFS_ACTS_ON_SELF");
const QString ErrInUse = QStringLiteral("FIXTUREDEFS_IN_USE");
const QString ErrRangeOverlap = QStringLiteral("FIXTUREDEFS_RANGE_OVERLAP");

void sendError(ApiSession *client, const QString &id, const QString &code, const QString &message,
               const QJsonObject &details = QJsonObject())
{
    client->send(ApiEnvelope::buildErrorResponse(id, code, message, details));
}

void sendInvalid(ApiSession *client, const QString &id, const QString &message)
{
    sendError(client, id, ApiEnvelope::ErrInvalidParams, message);
}

/** Strip anything Windows/Unix reject in a file name component. */
QString sanitizeFileComponent(QString s)
{
    s.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return s.trimmed();
}

/** Same naming rule as EditorView::setFilenameFromModel(), but always
 *  anchored in the user fixture directory instead of the process cwd. */
QString defaultUserFileName(const QLCFixtureDef *def)
{
    QString man = sanitizeFileComponent(def->manufacturer()).replace(QLatin1Char(' '), QLatin1Char('-'));
    QString mod = sanitizeFileComponent(def->model()).replace(QLatin1Char(' '), QLatin1Char('-'));
    QString base = QStringLiteral("%1-%2%3").arg(man, mod, KExtFixture);
    return QLCFixtureDefCache::userDefinitionDirectory().absoluteFilePath(base);
}

bool isInsideUserDirectory(const QString &absPath)
{
    if (absPath.isEmpty())
        return false;
    QString dir = QDir::cleanPath(QLCFixtureDefCache::userDefinitionDirectory().absolutePath());
    QString file = QDir::cleanPath(QFileInfo(absPath).absolutePath());
#if defined(Q_OS_WIN)
    return file.compare(dir, Qt::CaseInsensitive) == 0;
#else
    return file == dir;
#endif
}

/** Absolute path of the bundled .qxf for manufacturer/model according to the
 *  system FixturesMap.xml (the same map QLCFixtureDefCache::loadMap() reads:
 *  <M n="Manu_Name"><F n="file-base" m="Model"/></M>), or an empty string when
 *  there is no bundled definition of that name. */
QString bundledDefinitionPath(const QString &manufacturer, const QString &model)
{
    QDir dir = QLCFixtureDefCache::systemDefinitionDirectory();
    QFile map(dir.absoluteFilePath(QStringLiteral("FixturesMap.xml")));
    if (map.open(QIODevice::ReadOnly) == false)
        return QString();
    QXmlStreamReader xml(&map);
    QString mapManufacturer, spaced;
    while (xml.atEnd() == false)
    {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        if (xml.name() == QLatin1String("M"))
        {
            mapManufacturer = xml.attributes().value(QStringLiteral("n")).toString();
            spaced = mapManufacturer;
            spaced.replace(QLatin1Char('_'), QLatin1Char(' '));
        }
        else if (xml.name() == QLatin1String("F") && spaced == manufacturer &&
                 xml.attributes().value(QStringLiteral("m")).toString() == model)
        {
            QString path = dir.absoluteFilePath(mapManufacturer + QLatin1Char('/') +
                                                xml.attributes().value(QStringLiteral("n")).toString() + KExtFixture);
            return QFile::exists(path) ? path : QString();
        }
    }
    return QString();
}

// ---------------------------------------------------------------------------
// Physical
// ---------------------------------------------------------------------------

QJsonObject physicalToJson(const QLCPhysical &phy)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("bulbType"), phy.bulbType());
    obj.insert(QStringLiteral("bulbLumens"), phy.bulbLumens());
    obj.insert(QStringLiteral("bulbColourTemperature"), phy.bulbColourTemperature());
    obj.insert(QStringLiteral("weight"), phy.weight());
    obj.insert(QStringLiteral("width"), phy.width());
    obj.insert(QStringLiteral("height"), phy.height());
    obj.insert(QStringLiteral("depth"), phy.depth());
    obj.insert(QStringLiteral("lensName"), phy.lensName());
    obj.insert(QStringLiteral("lensDegreesMin"), phy.lensDegreesMin());
    obj.insert(QStringLiteral("lensDegreesMax"), phy.lensDegreesMax());
    obj.insert(QStringLiteral("focusType"), phy.focusType());
    obj.insert(QStringLiteral("focusPanMax"), phy.focusPanMax());
    obj.insert(QStringLiteral("focusTiltMax"), phy.focusTiltMax());
    obj.insert(QStringLiteral("layoutWidth"), phy.layoutSize().width());
    obj.insert(QStringLiteral("layoutHeight"), phy.layoutSize().height());
    obj.insert(QStringLiteral("powerConsumption"), phy.powerConsumption());
    obj.insert(QStringLiteral("dmxConnector"), phy.dmxConnector());
    return obj;
}

/** Partial update: only keys present in obj are applied. */
void applyPhysicalFromJson(QLCPhysical &phy, const QJsonObject &obj)
{
    if (obj.contains(QStringLiteral("bulbType")))
        phy.setBulbType(obj.value(QStringLiteral("bulbType")).toString());
    if (obj.contains(QStringLiteral("bulbLumens")))
        phy.setBulbLumens(obj.value(QStringLiteral("bulbLumens")).toInt());
    if (obj.contains(QStringLiteral("bulbColourTemperature")))
        phy.setBulbColourTemperature(obj.value(QStringLiteral("bulbColourTemperature")).toInt());
    if (obj.contains(QStringLiteral("weight")))
        phy.setWeight(obj.value(QStringLiteral("weight")).toDouble());
    if (obj.contains(QStringLiteral("width")))
        phy.setWidth(obj.value(QStringLiteral("width")).toInt());
    if (obj.contains(QStringLiteral("height")))
        phy.setHeight(obj.value(QStringLiteral("height")).toInt());
    if (obj.contains(QStringLiteral("depth")))
        phy.setDepth(obj.value(QStringLiteral("depth")).toInt());
    if (obj.contains(QStringLiteral("lensName")))
        phy.setLensName(obj.value(QStringLiteral("lensName")).toString());
    if (obj.contains(QStringLiteral("lensDegreesMin")))
        phy.setLensDegreesMin(obj.value(QStringLiteral("lensDegreesMin")).toDouble());
    if (obj.contains(QStringLiteral("lensDegreesMax")))
        phy.setLensDegreesMax(obj.value(QStringLiteral("lensDegreesMax")).toDouble());
    if (obj.contains(QStringLiteral("focusType")))
        phy.setFocusType(obj.value(QStringLiteral("focusType")).toString());
    if (obj.contains(QStringLiteral("focusPanMax")))
        phy.setFocusPanMax(obj.value(QStringLiteral("focusPanMax")).toInt());
    if (obj.contains(QStringLiteral("focusTiltMax")))
        phy.setFocusTiltMax(obj.value(QStringLiteral("focusTiltMax")).toInt());
    if (obj.contains(QStringLiteral("layoutWidth")) || obj.contains(QStringLiteral("layoutHeight")))
    {
        QSize size = phy.layoutSize();
        if (obj.contains(QStringLiteral("layoutWidth")))
            size.setWidth(obj.value(QStringLiteral("layoutWidth")).toInt());
        if (obj.contains(QStringLiteral("layoutHeight")))
            size.setHeight(obj.value(QStringLiteral("layoutHeight")).toInt());
        phy.setLayoutSize(size);
    }
    if (obj.contains(QStringLiteral("powerConsumption")))
        phy.setPowerConsumption(obj.value(QStringLiteral("powerConsumption")).toInt());
    if (obj.contains(QStringLiteral("dmxConnector")))
        phy.setDmxConnector(obj.value(QStringLiteral("dmxConnector")).toString());
}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

QString warningToString(QLCCapability::WarningType w)
{
    switch (w)
    {
        case QLCCapability::EmptyName: return QStringLiteral("EmptyName");
        case QLCCapability::Overlapping: return QStringLiteral("Overlapping");
        default: return QStringLiteral("NoWarning");
    }
}

/** Port of ChannelEdit::checkCapabilities(): recompute every capability's
 *  warning flag from its name and its range against the others. */
void refreshWarnings(QLCChannel *channel)
{
    QVector<bool> allocation(256, false);
    for (QLCCapability *cap : channel->capabilities())
    {
        cap->setWarning(QLCCapability::NoWarning);
        if (cap->name().isEmpty())
            cap->setWarning(QLCCapability::EmptyName);
        for (int i = cap->min(); i <= cap->max(); i++)
        {
            if (allocation[i])
                cap->setWarning(QLCCapability::Overlapping);
            else
                allocation[i] = true;
        }
    }
}

QJsonValue resourceToJson(const QVariant &v)
{
    if (v.typeId() == QMetaType::QColor)
        return v.value<QColor>().name();
    switch (v.typeId())
    {
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Float:
        case QMetaType::Double:
            return v.toDouble();
        default:
            return v.toString();
    }
}

QJsonObject capabilityToJson(const QLCCapability *cap)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("min"), int(cap->min()));
    obj.insert(QStringLiteral("max"), int(cap->max()));
    obj.insert(QStringLiteral("name"), cap->name());
    obj.insert(QStringLiteral("preset"), QLCCapability::presetToString(cap->preset()));

    QJsonArray resources;
    for (const QVariant &v : cap->resources())
    {
        if (v.isValid() == false)
            continue;
        resources.append(resourceToJson(v));
    }
    obj.insert(QStringLiteral("resources"), resources);

    QJsonArray aliases;
    for (const AliasInfo &alias : cap->aliasList())
    {
        QJsonObject a;
        a.insert(QStringLiteral("targetMode"), alias.targetMode);
        a.insert(QStringLiteral("targetChannel"), alias.targetChannel);
        aliases.append(a);
    }
    obj.insert(QStringLiteral("aliases"), aliases);
    obj.insert(QStringLiteral("warning"), warningToString(cap->warning()));
    return obj;
}

/** Convert a JSON resources array according to the preset's type. Returns
 *  false (with an error message) when a value doesn't fit the type. */
bool resourcesFromJson(QLCCapability::PresetType type, const QJsonArray &arr, QVariantList &out, QString &error)
{
    out.clear();
    int arity = 0;
    switch (type)
    {
        case QLCCapability::SingleColor:
        case QLCCapability::SingleValue:
        case QLCCapability::Picture:
            arity = 1;
            break;
        case QLCCapability::DoubleColor:
        case QLCCapability::DoubleValue:
            arity = 2;
            break;
        default:
            return true; // ignored for presets without resources
    }

    for (int i = 0; i < arr.count() && i < arity; i++)
    {
        const QJsonValue v = arr.at(i);
        switch (type)
        {
            case QLCCapability::SingleColor:
            case QLCCapability::DoubleColor:
            {
                QColor col(v.toString());
                if (v.isString() == false || col.isValid() == false)
                {
                    error = QStringLiteral("resources[%1] must be a \"#RRGGBB\" colour string").arg(i);
                    return false;
                }
                out.append(col);
                break;
            }
            case QLCCapability::SingleValue:
            case QLCCapability::DoubleValue:
                if (v.isDouble() == false)
                {
                    error = QStringLiteral("resources[%1] must be a number").arg(i);
                    return false;
                }
                out.append(float(v.toDouble()));
                break;
            default:
                if (v.isString() == false)
                {
                    error = QStringLiteral("resources[%1] must be a resource name string").arg(i);
                    return false;
                }
                out.append(v.toString());
                break;
        }
    }
    return true;
}

/** QLCChannel only appends capabilities and has no "replace at index", so
 *  rebuilding the list in place is the way to swap one capability while
 *  keeping every other index stable. The entries never overlapped before,
 *  so re-adding them cannot fail. */
void replaceCapabilityAt(QLCChannel *channel, int index, QLCCapability *fresh)
{
    QList<QLCCapability *> copies;
    QList<QLCCapability *> current = channel->capabilities();
    for (int i = 0; i < current.count(); i++)
        copies.append(i == index ? fresh : current.at(i)->createCopy());
    for (QLCCapability *cap : current)
        channel->removeCapability(cap);
    for (QLCCapability *cap : copies)
    {
        if (channel->addCapability(cap) == false)
            delete cap; // unreachable in practice, see above
    }
}

// ---------------------------------------------------------------------------
// Channels
// ---------------------------------------------------------------------------

bool parseChannelPreset(const QString &str, QLCChannel::Preset &out)
{
    int value = int(QLCChannel::stringToPreset(str));
    if (value < 0 || value >= int(QLCChannel::LastPreset))
        return false;
    out = QLCChannel::Preset(value);
    return true;
}

bool parseCapabilityPreset(const QString &str, QLCCapability::Preset &out)
{
    int value = int(QLCCapability::stringToPreset(str));
    if (value < 0 || value >= int(QLCCapability::LastPreset))
        return false;
    out = QLCCapability::Preset(value);
    return true;
}

bool parseColour(const QString &str, QLCChannel::PrimaryColour &out)
{
    if (str == QStringLiteral("Generic") || str.isEmpty())
    {
        out = QLCChannel::NoColour;
        return true;
    }
    if (QLCChannel::colourList().contains(str) == false)
        return false;
    out = QLCChannel::stringToColour(str);
    return true;
}

/** Replace the capability list with the single auto-generated one for the
 *  channel's current preset (ChannelEdit::setupPreset). */
void applyPresetCapability(QLCChannel *channel)
{
    for (QLCCapability *cap : channel->capabilities())
        channel->removeCapability(cap);
    channel->addPresetCapability();
}

/** Port of EditorView::addPresetChannel(): a channel from either a primary
 *  colour (Intensity group + colour + matching Intensity* preset) or a plain
 *  group (with the group's default preset). Returns an unparented channel
 *  the caller must add to the definition. */
QLCChannel *makePresetChannel(const QString &name, bool isColour, QLCChannel::PrimaryColour colour, QLCChannel::Group group)
{
    QLCChannel *channel = new QLCChannel();
    channel->setName(name);
    if (isColour)
    {
        channel->setGroup(QLCChannel::Intensity);
        channel->setColour(colour);
        switch (colour)
        {
            case QLCChannel::Red: channel->setPreset(QLCChannel::IntensityRed); break;
            case QLCChannel::Green: channel->setPreset(QLCChannel::IntensityGreen); break;
            case QLCChannel::Blue: channel->setPreset(QLCChannel::IntensityBlue); break;
            case QLCChannel::Cyan: channel->setPreset(QLCChannel::IntensityCyan); break;
            case QLCChannel::Magenta: channel->setPreset(QLCChannel::IntensityMagenta); break;
            case QLCChannel::Yellow: channel->setPreset(QLCChannel::IntensityYellow); break;
            case QLCChannel::White: channel->setPreset(QLCChannel::IntensityWhite); break;
            case QLCChannel::Amber: channel->setPreset(QLCChannel::IntensityAmber); break;
            case QLCChannel::UV: channel->setPreset(QLCChannel::IntensityUV); break;
            case QLCChannel::Lime: channel->setPreset(QLCChannel::IntensityLime); break;
            case QLCChannel::Indigo: channel->setPreset(QLCChannel::IntensityIndigo); break;
            default: break;
        }
    }
    else
    {
        channel->setGroup(group);
        switch (group)
        {
            case QLCChannel::Intensity: channel->setPreset(QLCChannel::IntensityDimmer); break;
            case QLCChannel::Pan: channel->setPreset(QLCChannel::PositionPan); break;
            case QLCChannel::Tilt: channel->setPreset(QLCChannel::PositionTilt); break;
            case QLCChannel::Colour: channel->setPreset(QLCChannel::ColorMacro); break;
            case QLCChannel::Shutter: channel->setPreset(QLCChannel::ShutterStrobeSlowFast); break;
            case QLCChannel::Beam:
            case QLCChannel::Effect: channel->setPreset(QLCChannel::NoFunction); break;
            default: break;
        }
    }
    // setPreset() rewrote the name from the preset; the wizard's label wins.
    channel->setName(name);
    channel->addPresetCapability();
    return channel;
}

// ---------------------------------------------------------------------------
// Colour-name detection (self-contained copy of ChannelEdit's helpers)
// ---------------------------------------------------------------------------

struct NamedColor
{
    QString name;
    QString normalized;
    QString compact;
    QColor rgb;
};

QString normalizeWords(const QString &input)
{
    QString normalized = input.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    return normalized.simplified();
}

bool isEntirelyLowercase(const QString &text)
{
    bool hasLetters = false;
    for (const QChar &ch : text)
    {
        if (!ch.isLetter())
            continue;
        hasLetters = true;
        if (ch != ch.toLower())
            return false;
    }
    return hasLetters;
}

QString titleCaseWords(const QString &text)
{
    QString title = text;
    bool wordStart = true;
    for (int i = 0; i < title.length(); i++)
    {
        QChar ch = title.at(i);
        if (ch.isLetterOrNumber())
        {
            if (wordStart && ch.isLetter())
                title[i] = ch.toUpper();
            wordStart = false;
        }
        else
        {
            wordStart = true;
        }
    }
    return title;
}

int fuzzyScore(const QString &a, const QString &b)
{
    if (a == b)
        return 1000;
    const int maxLen = qMax(a.length(), b.length());
    if (maxLen == 0)
        return 1000;

    QVector<int> prev(b.length() + 1, 0);
    QVector<int> curr(b.length() + 1, 0);
    for (int j = 0; j <= b.length(); j++)
        prev[j] = j;
    for (int i = 1; i <= a.length(); i++)
    {
        curr[0] = i;
        for (int j = 1; j <= b.length(); j++)
        {
            const int cost = (a.at(i - 1) == b.at(j - 1)) ? 0 : 1;
            curr[j] = qMin(qMin(curr[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
        }
        prev = curr;
    }
    const int dist = prev[b.length()];
    return ((maxLen - dist) * 1000) / maxLen;
}

/** The bundled "Named RGB" colour filter file, read with a bare
 *  QXmlStreamReader (the ColorFilters class lives in qmlui). Empty when the
 *  file isn't installed - autoPatchColors then only title-cases names,
 *  exactly like the desktop editor in the same situation. */
QVector<NamedColor> loadNamedColors()
{
    QVector<NamedColor> colors;
    QDir filtersDir = QLCFile::systemDirectory(QString(COLORFILTERSDIR), QString(KExtColorFilters));
    QString path = filtersDir.filePath(QStringLiteral("namedrgb.qxcf"));
    if (QFileInfo::exists(path) == false)
    {
        QStringList matches = filtersDir.entryList(QStringList() << QStringLiteral("*namedrgb*.qxcf"),
                                                   QDir::Files | QDir::Readable);
        if (matches.isEmpty() == false)
            path = filtersDir.filePath(matches.first());
    }
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text) == false)
        return colors;

    QXmlStreamReader xml(&file);
    while (xml.atEnd() == false)
    {
        xml.readNext();
        if (xml.isStartElement() == false || xml.name() != QStringLiteral("Color"))
            continue;
        QXmlStreamAttributes attrs = xml.attributes();
        QString name = attrs.value(QStringLiteral("Name")).toString().trimmed();
        QColor rgb(attrs.value(QStringLiteral("RGB")).toString());
        if (name.isEmpty() || rgb.isValid() == false)
            continue;
        QString normalized = normalizeWords(name);
        if (normalized.isEmpty())
            continue;
        QString compact = normalized;
        compact.remove(QLatin1Char(' '));
        colors.append({ name, normalized, compact, rgb });
    }
    return colors;
}

QList<QColor> detectNamedColors(const QString &description, const QVector<NamedColor> &colors)
{
    QList<QColor> detected;
    const QString normalizedDesc = normalizeWords(description);
    if (normalizedDesc.isEmpty() || colors.isEmpty())
        return detected;

    QSet<QString> selectedNames;
    QString remainingDesc = normalizedDesc;

    struct ExactMatch { int startPos; int endPos; int len; int colorIndex; };
    QList<ExactMatch> exactMatches;
    for (int i = 0; i < colors.count(); i++)
    {
        QString pattern = QRegularExpression::escape(colors.at(i).normalized);
        pattern.replace(QStringLiteral("\\ "), QStringLiteral("\\s+"));
        QRegularExpression re(QStringLiteral("\\b%1\\b").arg(pattern));
        auto matches = re.globalMatch(remainingDesc);
        while (matches.hasNext())
        {
            const auto match = matches.next();
            exactMatches.append({ int(match.capturedStart()), int(match.capturedEnd()),
                                  int(colors.at(i).normalized.length()), i });
        }
    }
    std::sort(exactMatches.begin(), exactMatches.end(), [](const ExactMatch &a, const ExactMatch &b) {
        if (a.startPos != b.startPos)
            return a.startPos < b.startPos;
        return a.len > b.len;
    });

    QList<QPair<int, int> > occupiedRanges;
    for (const ExactMatch &match : exactMatches)
    {
        bool overlaps = false;
        for (const auto &range : occupiedRanges)
        {
            if (match.startPos < range.second && match.endPos > range.first)
            {
                overlaps = true;
                break;
            }
        }
        if (overlaps)
            continue;
        const NamedColor &color = colors.at(match.colorIndex);
        if (selectedNames.contains(color.name))
            continue;
        selectedNames.insert(color.name);
        detected.append(color.rgb);
        occupiedRanges.append(qMakePair(match.startPos, match.endPos));
        remainingDesc.replace(match.startPos, match.endPos - match.startPos,
                              QString(match.endPos - match.startPos, QLatin1Char(' ')));
        if (detected.count() >= 2)
            return detected;
    }

    const QString unmatchedDesc = remainingDesc.simplified();
    if (unmatchedDesc.isEmpty())
        return detected;

    QStringList chunks;
    const QStringList tokens = unmatchedDesc.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < tokens.count(); i++)
    {
        chunks.append(tokens.at(i));
        if (i + 1 < tokens.count())
            chunks.append(tokens.at(i) + QLatin1Char(' ') + tokens.at(i + 1));
    }
    for (const QString &chunk : chunks)
    {
        QString compactChunk = chunk;
        compactChunk.remove(QLatin1Char(' '));
        if (compactChunk.length() < 3)
            continue;
        const bool chunkHasDigits = chunk.contains(QRegularExpression(QStringLiteral("\\d")));
        int bestIndex = -1;
        int bestScore = 0;
        for (int i = 0; i < colors.count(); i++)
        {
            if (selectedNames.contains(colors.at(i).name))
                continue;
            if (!chunkHasDigits && colors.at(i).normalized.contains(QRegularExpression(QStringLiteral("\\s\\d+$"))))
                continue;
            const int score = fuzzyScore(compactChunk, colors.at(i).compact);
            if (score > bestScore)
            {
                bestScore = score;
                bestIndex = i;
            }
        }
        if (bestIndex < 0 || bestScore < 850)
            continue;
        const NamedColor &color = colors.at(bestIndex);
        selectedNames.insert(color.name);
        detected.append(color.rgb);
        if (detected.count() >= 2)
            break;
    }
    return detected;
}

QString expandLabel(const QString &pattern, int index)
{
    QString label = pattern;
    label.replace(QLatin1Char('#'), QString::number(index));
    return label;
}

} // namespace

// ===========================================================================
// Session
// ===========================================================================

QString ApiFixtureDefsDomain::Session::channelId(const QLCChannel *channel)
{
    if (channelIds.contains(channel) == false)
        channelIds.insert(channel, QStringLiteral("ch-%1").arg(nextChannelSeq++));
    return channelIds.value(channel);
}

QString ApiFixtureDefsDomain::Session::modeId(const QLCFixtureMode *mode)
{
    if (modeIds.contains(mode) == false)
        modeIds.insert(mode, QStringLiteral("mode-%1").arg(nextModeSeq++));
    return modeIds.value(mode);
}

QLCChannel *ApiFixtureDefsDomain::Session::channelById(const QString &id) const
{
    for (QLCChannel *channel : def->channels())
    {
        if (channelIds.value(channel) == id)
            return channel;
    }
    return nullptr;
}

QLCFixtureMode *ApiFixtureDefsDomain::Session::modeById(const QString &id) const
{
    for (QLCFixtureMode *mode : def->modes())
    {
        if (modeIds.value(mode) == id)
            return mode;
    }
    return nullptr;
}

void ApiFixtureDefsDomain::Session::assignIds()
{
    for (QLCChannel *channel : def->channels())
        channelId(channel);
    for (QLCFixtureMode *mode : def->modes())
        modeId(mode);
}

// ===========================================================================
// Domain
// ===========================================================================

ApiFixtureDefsDomain::ApiFixtureDefsDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
    , m_nextSessionSeq(1)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);
    registerMethods();
}

ApiFixtureDefsDomain::~ApiFixtureDefsDomain()
{
    for (Session *s : m_sessions)
    {
        delete s->def;
        delete s;
    }
    m_sessions.clear();
}

QString ApiFixtureDefsDomain::defKey(const QString &manufacturer, const QString &model) const
{
    return manufacturer + QLatin1Char('\x1f') + model;
}

int ApiFixtureDefsDomain::defRevision(const QString &manufacturer, const QString &model) const
{
    return m_defRevisions.value(defKey(manufacturer, model), 0);
}

int ApiFixtureDefsDomain::bumpDefRevision(const QString &manufacturer, const QString &model)
{
    // Counters are never erased (not even on delete), so a re-created
    // definition can't collide with a stale client's remembered revision.
    int next = defRevision(manufacturer, model) + 1;
    m_defRevisions.insert(defKey(manufacturer, model), next);
    return next;
}

ApiFixtureDefsDomain::Session *ApiFixtureDefsDomain::session(const QString &id) const
{
    return m_sessions.value(id, nullptr);
}

ApiFixtureDefsDomain::Session *ApiFixtureDefsDomain::createSession(QLCFixtureDef *def)
{
    Session *s = new Session();
    s->id = QStringLiteral("fxs-%1").arg(m_nextSessionSeq++);
    s->def = def;
    s->fileName = def->definitionSourceFile();
    s->assignIds();
    m_sessions.insert(s->id, s);
    return s;
}

void ApiFixtureDefsDomain::destroySession(Session *s)
{
    m_sessions.remove(s->id);
    delete s->def;
    delete s;
}

ApiFixtureDefsDomain::Session *ApiFixtureDefsDomain::resolveSession(ApiSession *client, const QString &id,
                                                                     const QJsonObject &params, bool checkRevision)
{
    Session *s = session(params.value(QStringLiteral("sessionId")).toString());
    if (s == nullptr)
    {
        sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such editing session"));
        return nullptr;
    }
    if (checkRevision)
    {
        const QJsonValue base = params.value(QStringLiteral("baseRevision"));
        if (base.isDouble() == false || base.toInt() != s->revision)
        {
            QJsonObject details;
            details.insert(QStringLiteral("sessionRevision"), s->revision);
            details.insert(QStringLiteral("definition"), definitionToJson(s));
            sendError(client, id, ApiEnvelope::ErrConflict, QStringLiteral("baseRevision is stale"), details);
            return nullptr;
        }
    }
    return s;
}

void ApiFixtureDefsDomain::commitSession(Session *s, const QString &changeKind, const QString &originClientId)
{
    s->revision++;
    s->modified = true;

    QJsonObject data;
    data.insert(QStringLiteral("sessionId"), s->id);
    data.insert(QStringLiteral("sessionRevision"), s->revision);
    data.insert(QStringLiteral("changeKind"), changeKind);
    data.insert(QStringLiteral("definition"), definitionToJson(s));
    m_server->broadcast(QStringLiteral("fixturedefs.session.updated"), data, originClientId, false);
}

void ApiFixtureDefsDomain::sendSessionAck(ApiSession *client, const QString &id, Session *s, const QJsonObject &extra)
{
    QJsonObject result = extra;
    result.insert(QStringLiteral("sessionId"), s->id);
    result.insert(QStringLiteral("sessionRevision"), s->revision);
    client->send(ApiEnvelope::buildOkResponse(id, result));
}

// ---------------------------------------------------------------------------
// JSON snapshots
// ---------------------------------------------------------------------------

QJsonObject ApiFixtureDefsDomain::definitionToJson(Session *s) const
{
    QLCFixtureDef *def = s->def;
    QJsonObject obj;
    obj.insert(QStringLiteral("manufacturer"), def->manufacturer());
    obj.insert(QStringLiteral("model"), def->model());
    obj.insert(QStringLiteral("type"), QLCFixtureDef::typeToString(def->type()));
    obj.insert(QStringLiteral("author"), def->author());
    obj.insert(QStringLiteral("isUser"), def->isUser());
    obj.insert(QStringLiteral("sourceFile"), s->fileName.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(s->fileName));
    obj.insert(QStringLiteral("physical"), physicalToJson(def->physical()));

    QJsonArray channels;
    for (QLCChannel *channel : def->channels())
    {
        refreshWarnings(channel);
        QJsonObject c;
        c.insert(QStringLiteral("channelId"), s->channelId(channel));
        c.insert(QStringLiteral("name"), channel->name());
        c.insert(QStringLiteral("group"), QLCChannel::groupToString(channel->group()));
        c.insert(QStringLiteral("colour"), QLCChannel::colourToString(channel->colour()));
        c.insert(QStringLiteral("preset"), QLCChannel::presetToString(channel->preset()));
        c.insert(QStringLiteral("defaultValue"), int(channel->defaultValue()));
        c.insert(QStringLiteral("controlByte"), channel->controlByte() == QLCChannel::LSB ? QStringLiteral("LSB") : QStringLiteral("MSB"));
        QJsonArray caps;
        for (QLCCapability *cap : channel->capabilities())
            caps.append(capabilityToJson(cap));
        c.insert(QStringLiteral("capabilities"), caps);
        channels.append(c);
    }
    obj.insert(QStringLiteral("channels"), channels);

    QJsonArray modes;
    for (QLCFixtureMode *mode : def->modes())
    {
        QJsonObject m;
        m.insert(QStringLiteral("modeId"), s->modeId(mode));
        m.insert(QStringLiteral("name"), mode->name());

        const QVector<QLCChannel *> modeChannels = mode->channels();
        QJsonArray slotArray;
        for (int i = 0; i < modeChannels.count(); i++)
        {
            QJsonObject slot;
            slot.insert(QStringLiteral("channelId"), s->channelId(modeChannels.at(i)));
            quint32 actsOn = mode->channelActsOn(quint32(i));
            if (actsOn != QLCChannel::invalid() && int(actsOn) < modeChannels.count())
                slot.insert(QStringLiteral("actsOnChannelId"), s->channelId(modeChannels.at(int(actsOn))));
            else
                slot.insert(QStringLiteral("actsOnChannelId"), QJsonValue::Null);
            slotArray.append(slot);
        }
        m.insert(QStringLiteral("channels"), slotArray);

        QJsonArray heads;
        const QVector<QLCFixtureHead> modeHeads = mode->heads();
        for (int h = 0; h < modeHeads.count(); h++)
        {
            QJsonObject head;
            head.insert(QStringLiteral("headIndex"), h);
            QJsonArray ids;
            for (quint32 idx : modeHeads.at(h).channels())
            {
                if (int(idx) < modeChannels.count())
                    ids.append(s->channelId(modeChannels.at(int(idx))));
            }
            head.insert(QStringLiteral("channelIds"), ids);
            heads.append(head);
        }
        m.insert(QStringLiteral("heads"), heads);
        m.insert(QStringLiteral("useGlobalPhysical"), mode->useGlobalPhysical());
        m.insert(QStringLiteral("physical"), mode->useGlobalPhysical() ? QJsonValue(QJsonValue::Null)
                                                                        : QJsonValue(physicalToJson(mode->physical())));
        modes.append(m);
    }
    obj.insert(QStringLiteral("modes"), modes);
    return obj;
}

QJsonObject ApiFixtureDefsDomain::sessionInfoToJson(Session *s) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("sessionId"), s->id);
    obj.insert(QStringLiteral("manufacturer"), s->def->manufacturer());
    obj.insert(QStringLiteral("model"), s->def->model());
    obj.insert(QStringLiteral("isUser"), s->def->isUser());
    obj.insert(QStringLiteral("isModified"), s->modified);
    obj.insert(QStringLiteral("sessionRevision"), s->revision);
    obj.insert(QStringLiteral("baseRevision"), s->hasBaseRevision ? QJsonValue(s->baseRevision) : QJsonValue(QJsonValue::Null));
    return obj;
}

QJsonObject ApiFixtureDefsDomain::sessionOpenedResult(Session *s) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("sessionId"), s->id);
    obj.insert(QStringLiteral("definition"), definitionToJson(s));
    obj.insert(QStringLiteral("sessionRevision"), s->revision);
    obj.insert(QStringLiteral("isUser"), s->def->isUser());
    obj.insert(QStringLiteral("baseRevision"), s->hasBaseRevision ? QJsonValue(s->baseRevision) : QJsonValue(QJsonValue::Null));
    return obj;
}

/** Port of EditorView::checkFixture(), minus the HTML list markup. */
QStringList ApiFixtureDefsDomain::validate(Session *s) const
{
    QStringList warnings;
    QLCFixtureDef *def = s->def;

    if (def->channels().isEmpty())
    {
        warnings << QStringLiteral("No channels provided");
    }
    else
    {
        for (QLCChannel *channel : def->channels())
        {
            if (channel->capabilities().isEmpty())
                warnings << QStringLiteral("No capability provided in channel '%1'").arg(channel->name());
            for (QLCCapability *cap : channel->capabilities())
            {
                if (cap->name().isEmpty())
                    warnings << QStringLiteral("Empty capability description provided in channel '%1'").arg(channel->name());
            }
        }
        for (QLCFixtureMode *mode : def->modes())
        {
            if (mode->name().isEmpty())
                warnings << QStringLiteral("Empty mode name provided");
            if (mode->channels().isEmpty())
                warnings << QStringLiteral("Mode '%1' has no channels defined").arg(mode->name());
            quint32 chIndex = 0;
            for (QLCChannel *channel : mode->channels())
            {
                if (mode->channelActsOn(chIndex) == chIndex)
                    warnings << QStringLiteral("In mode '%1', channel '%2' cannot act on itself").arg(mode->name(), channel->name());
                chIndex++;
            }
        }
    }
    if (def->modes().isEmpty())
        warnings << QStringLiteral("No modes provided. Without modes, this fixture will not appear in the list!");
    return warnings;
}

void ApiFixtureDefsDomain::rebuildModeChannels(QLCFixtureMode *mode, const QList<QLCChannel *> &channels,
                                               const QList<QLCChannel *> &actsOn)
{
    // Heads reference slot indices, so remember them by channel identity
    // before the list changes.
    const QVector<QLCChannel *> oldChannels = mode->channels();
    QList<QList<QLCChannel *> > headChannels;
    for (const QLCFixtureHead &head : mode->heads())
    {
        QList<QLCChannel *> members;
        for (quint32 idx : head.channels())
        {
            if (int(idx) < oldChannels.count())
                members.append(oldChannels.at(int(idx)));
        }
        headChannels.append(members);
    }

    // QLCFixtureMode::removeAllChannels() leaves m_actsOnMap alone.
    for (int i = 0; i < oldChannels.count(); i++)
        mode->setChannelActsOn(quint32(i), QLCChannel::invalid());
    mode->removeAllChannels();

    for (QLCChannel *channel : channels)
        mode->insertChannel(channel, QLCChannel::invalid());
    for (int i = 0; i < channels.count() && i < actsOn.count(); i++)
    {
        if (actsOn.at(i) == nullptr)
            continue;
        int target = channels.indexOf(actsOn.at(i));
        if (target >= 0 && target != i)
            mode->setChannelActsOn(quint32(i), quint32(target));
    }

    while (mode->heads().isEmpty() == false)
        mode->removeHead(mode->heads().count() - 1);
    for (const QList<QLCChannel *> &members : headChannels)
    {
        QLCFixtureHead head;
        for (QLCChannel *channel : members)
        {
            int idx = channels.indexOf(channel);
            if (idx >= 0)
                head.addChannel(quint32(idx));
        }
        if (head.channels().isEmpty() == false)
            mode->insertHead(-1, head);
    }
}

// ===========================================================================
// Methods
// ===========================================================================

void ApiFixtureDefsDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    QLCFixtureDefCache *cache = m_doc->fixtureDefCache();

    // ---------------------------------------------------------------- library

    dispatcher->registerMethod(QStringLiteral("fixturedefs.list"), [this, cache](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        const QString filter = params.value(QStringLiteral("manufacturer")).toString();
        QJsonArray entries;
        const QMap<QString, QMap<QString, bool> > map = cache->fixtureCache();
        for (auto mit = map.constBegin(); mit != map.constEnd(); ++mit)
        {
            if (filter.isEmpty() == false && mit.key() != filter)
                continue;
            for (auto it = mit.value().constBegin(); it != mit.value().constEnd(); ++it)
            {
                // fixtureDef() force-loads the definition (see the spec's
                // cost warning on FixtureDefsLibraryEntry).
                QLCFixtureDef *def = cache->fixtureDef(mit.key(), it.key());
                if (def == nullptr)
                    continue;
                QJsonObject entry;
                entry.insert(QStringLiteral("manufacturer"), def->manufacturer());
                entry.insert(QStringLiteral("model"), def->model());
                entry.insert(QStringLiteral("type"), QLCFixtureDef::typeToString(def->type()));
                entry.insert(QStringLiteral("author"), def->author());
                entry.insert(QStringLiteral("isUser"), def->isUser());
                entry.insert(QStringLiteral("channelCount"), def->channels().count());
                entry.insert(QStringLiteral("modeCount"), def->modes().count());
                entry.insert(QStringLiteral("defRevision"), defRevision(def->manufacturer(), def->model()));
                entries.append(entry);
            }
        }
        QJsonObject result;
        result.insert(QStringLiteral("entries"), entries);
        client->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.get"), [this, cache](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        const QString manufacturer = params.value(QStringLiteral("manufacturer")).toString();
        const QString model = params.value(QStringLiteral("model")).toString();
        QLCFixtureDef *def = cache->fixtureDef(manufacturer, model);
        if (def == nullptr)
        {
            sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such fixture definition"));
            return;
        }
        // Not an editing session: ids here are index-based ("ch-1"... in
        // channel-pool order) and only meaningful within this response.
        Session tmp;
        tmp.def = def;
        tmp.fileName = def->definitionSourceFile();
        tmp.assignIds();
        QJsonObject result;
        result.insert(QStringLiteral("definition"), definitionToJson(&tmp));
        result.insert(QStringLiteral("defRevision"), defRevision(manufacturer, model));
        client->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.delete"), [this, cache](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        const QString manufacturer = params.value(QStringLiteral("manufacturer")).toString();
        const QString model = params.value(QStringLiteral("model")).toString();
        QLCFixtureDef *def = cache->fixtureDef(manufacturer, model);
        if (def == nullptr)
        {
            sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such fixture definition"));
            return;
        }
        const int current = defRevision(manufacturer, model);
        if (params.value(QStringLiteral("baseRevision")).toInt(-1) != current)
        {
            QJsonObject details;
            details.insert(QStringLiteral("defRevision"), current);
            sendError(client, id, ApiEnvelope::ErrConflict, QStringLiteral("baseRevision is stale"), details);
            return;
        }
        const QString path = def->definitionSourceFile();
        if (def->isUser() == false || isInsideUserDirectory(path) == false)
        {
            sendError(client, id, ErrSystemReadOnly,
                      QStringLiteral("Bundled/system fixture definitions cannot be deleted"));
            return;
        }
        QJsonArray usedBy;
        for (Fixture *fixture : m_doc->fixtures())
        {
            if (fixture != nullptr && fixture->fixtureDef() == def)
                usedBy.append(int(fixture->id()));
        }
        if (usedBy.isEmpty() == false)
        {
            QJsonObject details;
            details.insert(QStringLiteral("fixtureIds"), usedBy);
            sendError(client, id, ErrInUse,
                      QStringLiteral("Definition is used by patched fixtures in the current project"), details);
            return;
        }
        if (QFile::exists(path) && QFile::remove(path) == false)
        {
            sendError(client, id, ApiEnvelope::ErrInternal, QStringLiteral("Could not delete %1").arg(path));
            return;
        }
        cache->removeFixtureDef(def);
        // A user definition can shadow a bundled one of the same name (that is
        // what session.forkToUser + save produce). Removing the cache entry
        // took the bundled definition out of the library with it until the
        // next restart; put it back, as a restart would.
        const QString bundled = bundledDefinitionPath(manufacturer, model);
        if (bundled.isEmpty() == false && cache->loadQXF(bundled, false) == false)
            qWarning() << "fixturedefs.delete: could not reload the bundled definition" << bundled;
        bumpDefRevision(manufacturer, model);

        QJsonObject result;
        result.insert(QStringLiteral("manufacturer"), manufacturer);
        result.insert(QStringLiteral("model"), model);
        client->send(ApiEnvelope::buildOkResponse(id, result));
        m_server->broadcast(QStringLiteral("fixturedefs.deleted"), result, client->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.export"), [this, cache](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        QLCFixtureDef *def = nullptr;
        QString sourceName;
        if (params.contains(QStringLiteral("sessionId")))
        {
            Session *s = session(params.value(QStringLiteral("sessionId")).toString());
            if (s != nullptr)
            {
                def = s->def;
                sourceName = s->fileName;
            }
        }
        else if (params.contains(QStringLiteral("manufacturer")) && params.contains(QStringLiteral("model")))
        {
            def = cache->fixtureDef(params.value(QStringLiteral("manufacturer")).toString(),
                                    params.value(QStringLiteral("model")).toString());
            if (def != nullptr)
                sourceName = def->definitionSourceFile();
        }
        if (def == nullptr)
        {
            sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such session or fixture definition"));
            return;
        }

        QString fileName = QFileInfo(sourceName).fileName();
        if (fileName.isEmpty())
            fileName = QFileInfo(defaultUserFileName(def)).fileName();

        // saveXML writes "<path>.temp" then renames, so it needs a real
        // directory rather than an open QTemporaryFile handle.
        QTemporaryDir tmpDir;
        const QString tmpPath = tmpDir.filePath(QStringLiteral("export.qxf"));
        if (tmpDir.isValid() == false || def->saveXML(tmpPath) != QFile::NoError)
        {
            sendError(client, id, ApiEnvelope::ErrInternal, QStringLiteral("Could not serialize the definition"));
            return;
        }
        QFile file(tmpPath);
        if (file.open(QIODevice::ReadOnly) == false)
        {
            sendError(client, id, ApiEnvelope::ErrInternal, QStringLiteral("Could not read back the exported definition"));
            return;
        }
        QJsonObject result;
        result.insert(QStringLiteral("fileName"), fileName);
        result.insert(QStringLiteral("qxfBase64"), QString::fromLatin1(file.readAll().toBase64()));
        client->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // ------------------------------------------------------- session lifecycle

    auto announceSession = [this](ApiSession *client, const QString &id, Session *s, const QString &source)
    {
        client->send(ApiEnvelope::buildOkResponse(id, sessionOpenedResult(s)));
        QJsonObject data = sessionOpenedResult(s);
        data.insert(QStringLiteral("source"), source);
        m_server->broadcast(QStringLiteral("fixturedefs.session.opened"), data, client->clientId(), false);
    };

    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.create"), [this, announceSession](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QLCFixtureDef *def = new QLCFixtureDef();
        def->setIsUser(true);
        Session *s = createSession(def);
        announceSession(client, id, s, QStringLiteral("created"));
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.open"), [this, cache, announceSession](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        const QString manufacturer = params.value(QStringLiteral("manufacturer")).toString();
        const QString model = params.value(QStringLiteral("model")).toString();
        QLCFixtureDef *libraryDef = cache->fixtureDef(manufacturer, model);
        if (libraryDef == nullptr)
        {
            sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such fixture definition"));
            return;
        }
        // Same clone EditorView's constructor makes: the session never
        // shares an instance with the cache.
        QLCFixtureDef *def = new QLCFixtureDef(libraryDef);
        Session *s = createSession(def);
        s->hasBaseRevision = true;
        s->baseRevision = defRevision(manufacturer, model);
        announceSession(client, id, s, QStringLiteral("opened"));
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.import"), [this, cache, announceSession](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        const QString requestedName = QFileInfo(params.value(QStringLiteral("fileName")).toString()).fileName();
        const QByteArray bytes = QByteArray::fromBase64(params.value(QStringLiteral("qxfBase64")).toString().toLatin1());
        if (bytes.isEmpty())
        {
            sendInvalid(client, id, QStringLiteral("qxfBase64 is missing or empty"));
            return;
        }
        QTemporaryDir tmpDir;
        const QString tmpPath = tmpDir.filePath(QStringLiteral("import.qxf"));
        QFile tmpFile(tmpPath);
        if (tmpDir.isValid() == false || tmpFile.open(QIODevice::WriteOnly) == false)
        {
            sendError(client, id, ApiEnvelope::ErrInternal, QStringLiteral("Could not stage the imported file"));
            return;
        }
        tmpFile.write(bytes);
        tmpFile.close();

        QLCFixtureDef *def = new QLCFixtureDef();
        if (def->loadXML(tmpPath) != QFile::NoError)
        {
            delete def;
            sendInvalid(client, id, QStringLiteral("Not a valid QLC+ fixture definition (.qxf)"));
            return;
        }
        def->setIsUser(true);
        QString targetName = sanitizeFileComponent(requestedName);
        if (targetName.isEmpty())
            targetName = QFileInfo(defaultUserFileName(def)).fileName();
        if (targetName.endsWith(KExtFixture, Qt::CaseInsensitive) == false)
            targetName += KExtFixture;
        def->setDefinitionSourceFile(QLCFixtureDefCache::userDefinitionDirectory().absoluteFilePath(targetName));

        Session *s = createSession(def);
        s->modified = true;
        if (cache->fixtureDef(def->manufacturer(), def->model()) != nullptr)
        {
            // The library already has this manufacturer/model: a save will
            // overwrite it, so the session must carry its revision.
            s->hasBaseRevision = true;
            s->baseRevision = defRevision(def->manufacturer(), def->model());
        }
        announceSession(client, id, s, QStringLiteral("imported"));
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.close"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, false);
        if (s == nullptr)
            return;
        QJsonObject result;
        result.insert(QStringLiteral("sessionId"), s->id);
        destroySession(s);
        client->send(ApiEnvelope::buildOkResponse(id, result));
        m_server->broadcast(QStringLiteral("fixturedefs.session.closed"), result, client->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.list"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonArray sessions;
        QStringList ids = m_sessions.keys();
        std::sort(ids.begin(), ids.end());
        for (const QString &sid : ids)
            sessions.append(sessionInfoToJson(m_sessions.value(sid)));
        QJsonObject result;
        result.insert(QStringLiteral("sessions"), sessions);
        client->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // Read-only snapshot of one session: the same shape session.open answers
    // plus isModified. This is how a client that only knows a sessionId (from
    // session.list after a page reload) gets the definition to edit - there is
    // no other JSON read path (export is QXF only) and a fake mutation would
    // bump the revision and mark the session modified.
    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.get"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, false);
        if (s == nullptr)
            return;
        QJsonObject result = sessionOpenedResult(s);
        result.insert(QStringLiteral("isModified"), s->modified);
        client->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.forkToUser"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return;
        if (s->def->isUser())
        {
            sendSessionAck(client, id, s); // already user-owned: nothing to do
            return;
        }
        // EditorView::remapFilename(): same file name, user directory.
        QString base = QFileInfo(s->fileName).fileName();
        if (base.isEmpty())
            base = QFileInfo(defaultUserFileName(s->def)).fileName();
        s->fileName = QLCFixtureDefCache::userDefinitionDirectory().absoluteFilePath(base);
        s->def->setIsUser(true);
        s->def->setDefinitionSourceFile(s->fileName);
        commitSession(s, QStringLiteral("forkToUser"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.update"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return;
        if (params.contains(QStringLiteral("manufacturer")))
            s->def->setManufacturer(params.value(QStringLiteral("manufacturer")).toString());
        if (params.contains(QStringLiteral("model")))
            s->def->setModel(params.value(QStringLiteral("model")).toString());
        if (params.contains(QStringLiteral("type")))
            s->def->setType(QLCFixtureDef::stringToType(params.value(QStringLiteral("type")).toString()));
        if (params.contains(QStringLiteral("author")))
            s->def->setAuthor(params.value(QStringLiteral("author")).toString());
        commitSession(s, QStringLiteral("metadata"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.setPhysical"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return;
        if (params.value(QStringLiteral("physical")).isObject() == false)
        {
            sendInvalid(client, id, QStringLiteral("physical object is required"));
            return;
        }
        QLCPhysical phy = s->def->physical();
        applyPhysicalFromJson(phy, params.value(QStringLiteral("physical")).toObject());
        s->def->setPhysical(phy);
        commitSession(s, QStringLiteral("physical"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.session.validate"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, false);
        if (s == nullptr)
            return;
        QJsonObject result;
        result.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(validate(s)));
        client->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.save"), [this, cache](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, false);
        if (s == nullptr)
            return;
        QLCFixtureDef *def = s->def;
        if (def->isUser() == false)
        {
            sendError(client, id, ErrSystemReadOnly,
                      QStringLiteral("Session was opened from a bundled definition; call fixturedefs.session.forkToUser first"));
            return;
        }
        if (def->manufacturer().trimmed().isEmpty() || def->model().trimmed().isEmpty())
        {
            sendInvalid(client, id, QStringLiteral("manufacturer and model must be set before saving"));
            return;
        }

        // The library's view of this manufacturer/model right now: an
        // integer revision when an entry exists, "null" when it doesn't.
        const bool libraryHas = cache->fixtureDef(def->manufacturer(), def->model()) != nullptr;
        const int libraryRevision = defRevision(def->manufacturer(), def->model());
        const QJsonValue base = params.value(QStringLiteral("baseRevision"));
        const bool baseIsNull = base.isNull() || base.isUndefined();
        if (libraryHas != !baseIsNull || (libraryHas && base.toInt() != libraryRevision))
        {
            QJsonObject details;
            details.insert(QStringLiteral("defRevision"), libraryHas ? QJsonValue(libraryRevision) : QJsonValue(QJsonValue::Null));
            sendError(client, id, ApiEnvelope::ErrConflict, QStringLiteral("baseRevision is stale"), details);
            return;
        }

        // Patched fixtures using the library entry get re-pointed to the
        // saved definition below (same as FixtureEditor::slotReloadFixture).
        // Check first that every one of them still fits: a mode that grew
        // into the next fixture's channels made Doc::slotFixtureChanged()
        // hit Q_ASSERT(!m_addresses.contains(i)), and a definition left
        // without modes kept those fixtures pointing at the modes that
        // reloadOrAddFixtureDef()'s deep copy frees. Only fixtures holding
        // the library instance itself count - per-fixture generic
        // definitions (Generic/Generic dimmers) merely share its name.
        QLCFixtureDef *libraryDef = cache->fixtureDef(def->manufacturer(), def->model());
        QMap<quint32, QString> fixturesToModes;
        QJsonArray blocked;
        for (Fixture *fixture : m_doc->fixtures())
        {
            if (libraryDef == nullptr || fixture == nullptr ||
                fixture->fixtureDef() != libraryDef || fixture->fixtureMode() == nullptr)
                continue;
            fixturesToModes.insert(fixture->id(), fixture->fixtureMode()->name());

            QLCFixtureMode *newMode = def->mode(fixture->fixtureMode()->name());
            if (newMode == nullptr && def->modes().isEmpty() == false)
                newMode = def->modes().first();
            QString reason;
            if (newMode == nullptr)
            {
                reason = QStringLiteral("definition has no modes");
            }
            else
            {
                const quint32 newCount = quint32(newMode->channels().size());
                if (fixture->address() + newCount > 512)
                    reason = QStringLiteral("mode no longer fits in the universe");
                for (quint32 i = fixture->channels(); reason.isEmpty() && i < newCount; i++)
                {
                    const quint32 owner = m_doc->fixtureForAddress(fixture->universeAddress() + i);
                    if (owner != Fixture::invalidId() && owner != fixture->id())
                        reason = QStringLiteral("mode would overlap fixture %1").arg(owner);
                }
            }
            if (reason.isEmpty() == false)
            {
                QJsonObject entry;
                entry.insert(QStringLiteral("fixtureId"), QString::number(fixture->id()));
                entry.insert(QStringLiteral("reason"), reason);
                blocked.append(entry);
            }
        }
        if (blocked.isEmpty() == false)
        {
            QJsonObject details;
            details.insert(QStringLiteral("fixtures"), blocked);
            sendError(client, id, ApiEnvelope::ErrConflict,
                      QStringLiteral("Patched fixtures using this definition would not fit the saved version; re-patch them first"),
                      details);
            return;
        }

        if (s->fileName.isEmpty())
            s->fileName = defaultUserFileName(def);
        QDir().mkpath(QFileInfo(s->fileName).absolutePath());
        def->setDefinitionSourceFile(s->fileName);

        const QStringList warnings = validate(s);
        QFile::FileError error = def->saveXML(s->fileName);
        if (error != QFile::NoError)
        {
            sendError(client, id, ApiEnvelope::ErrInternal,
                      QStringLiteral("Could not save file (%1)").arg(QLCFile::errorString(error)));
            return;
        }

        // FixtureEditor::slotReloadFixture(): fixturesToModes (collected
        // above, BEFORE the cache copy is overwritten - their QLCFixtureMode
        // pointers die with it) get re-pointed to the new modes.
        cache->reloadOrAddFixtureDef(def);
        QLCFixtureDef *cacheDef = cache->fixtureDef(def->manufacturer(), def->model());
        if (cacheDef != nullptr)
        {
            for (auto it = fixturesToModes.constBegin(); it != fixturesToModes.constEnd(); ++it)
            {
                Fixture *fixture = m_doc->fixture(it.key());
                if (fixture == nullptr)
                    continue;
                QLCFixtureMode *mode = cacheDef->mode(it.value());
                if (mode == nullptr && cacheDef->modes().isEmpty() == false)
                    mode = cacheDef->modes().first();
                if (mode != nullptr)
                    fixture->setFixtureDefinition(cacheDef, mode);
            }
        }

        const int newRevision = bumpDefRevision(def->manufacturer(), def->model());
        s->hasBaseRevision = true;
        s->baseRevision = newRevision;
        s->modified = false;

        QJsonObject result;
        result.insert(QStringLiteral("sessionId"), s->id);
        result.insert(QStringLiteral("manufacturer"), def->manufacturer());
        result.insert(QStringLiteral("model"), def->model());
        result.insert(QStringLiteral("defRevision"), newRevision);
        result.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
        client->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data = result;
        data.insert(QStringLiteral("definition"), definitionToJson(s));
        m_server->broadcast(QStringLiteral("fixturedefs.saved"), data, client->clientId(), false);
    });

    // ---------------------------------------------------------------- channels

    // EditorView::requestChannelEditor()'s "New channel N" default, made
    // unique since this API rejects duplicate channel names.
    auto uniqueChannelName = [](QLCFixtureDef *def)
    {
        int n = def->channels().count() + 1;
        QString name = QStringLiteral("New channel %1").arg(n);
        while (def->channel(name) != nullptr)
            name = QStringLiteral("New channel %1").arg(++n);
        return name;
    };

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.add"), [this, uniqueChannelName](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return;
        QLCFixtureDef *def = s->def;

        QString name = params.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = uniqueChannelName(def);
        if (def->channel(name) != nullptr)
        {
            sendInvalid(client, id, QStringLiteral("A channel named '%1' already exists").arg(name));
            return;
        }
        QLCChannel::Group group = QLCChannel::NoGroup;
        if (params.contains(QStringLiteral("group")))
        {
            group = QLCChannel::stringToGroup(params.value(QStringLiteral("group")).toString());
            if (group == QLCChannel::NoGroup)
            {
                sendInvalid(client, id, QStringLiteral("Unknown channel group"));
                return;
            }
        }
        QLCChannel::PrimaryColour colour = QLCChannel::NoColour;
        bool hasColour = params.contains(QStringLiteral("colour"));
        if (hasColour && parseColour(params.value(QStringLiteral("colour")).toString(), colour) == false)
        {
            sendInvalid(client, id, QStringLiteral("Unknown channel colour"));
            return;
        }
        QLCChannel::Preset preset = QLCChannel::Custom;
        bool hasPreset = params.contains(QStringLiteral("preset"));
        if (hasPreset && parseChannelPreset(params.value(QStringLiteral("preset")).toString(), preset) == false)
        {
            sendInvalid(client, id, QStringLiteral("Unknown channel preset"));
            return;
        }

        QLCChannel *channel = new QLCChannel();
        if (hasPreset && preset != QLCChannel::Custom)
        {
            // setPreset() derives group/colour/controlByte/name; explicit
            // params override, then the preset's capability is generated.
            channel->setPreset(preset);
            channel->setName(name);
            if (group != QLCChannel::NoGroup)
                channel->setGroup(group);
            if (hasColour)
                channel->setColour(colour);
            channel->addPresetCapability();
        }
        else
        {
            channel->setName(name);
            if (group != QLCChannel::NoGroup)
                channel->setGroup(group);
            if (hasColour)
                channel->setColour(colour);
            QLCCapability *cap = new QLCCapability(0, UCHAR_MAX);
            cap->setWarning(QLCCapability::EmptyName);
            channel->addCapability(cap);
        }
        def->addChannel(channel);
        const QString channelId = s->channelId(channel);
        commitSession(s, QStringLiteral("channel.add"), client->clientId());
        QJsonObject extra;
        extra.insert(QStringLiteral("channelId"), channelId);
        sendSessionAck(client, id, s, extra);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.update"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return;
        QLCChannel *channel = s->channelById(params.value(QStringLiteral("channelId")).toString());
        if (channel == nullptr)
        {
            sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such channel"));
            return;
        }

        // Validate everything before touching the channel.
        QString newName = channel->name();
        if (params.contains(QStringLiteral("name")))
        {
            newName = params.value(QStringLiteral("name")).toString();
            QLCChannel *clash = s->def->channel(newName);
            if (clash != nullptr && clash != channel)
            {
                sendInvalid(client, id, QStringLiteral("A channel named '%1' already exists").arg(newName));
                return;
            }
        }
        QLCChannel::Group group = QLCChannel::NoGroup;
        if (params.contains(QStringLiteral("group")))
        {
            group = QLCChannel::stringToGroup(params.value(QStringLiteral("group")).toString());
            if (group == QLCChannel::NoGroup)
            {
                sendInvalid(client, id, QStringLiteral("Unknown channel group"));
                return;
            }
        }
        QLCChannel::PrimaryColour colour = QLCChannel::NoColour;
        const bool hasColour = params.contains(QStringLiteral("colour"));
        if (hasColour && parseColour(params.value(QStringLiteral("colour")).toString(), colour) == false)
        {
            sendInvalid(client, id, QStringLiteral("Unknown channel colour"));
            return;
        }
        QLCChannel::Preset preset = QLCChannel::Custom;
        const bool hasPreset = params.contains(QStringLiteral("preset"));
        if (hasPreset && parseChannelPreset(params.value(QStringLiteral("preset")).toString(), preset) == false)
        {
            sendInvalid(client, id, QStringLiteral("Unknown channel preset"));
            return;
        }
        if (params.contains(QStringLiteral("defaultValue")))
        {
            int v = params.value(QStringLiteral("defaultValue")).toInt(-1);
            if (v < 0 || v > 255)
            {
                sendInvalid(client, id, QStringLiteral("defaultValue must be 0..255"));
                return;
            }
        }
        QString controlByte = params.value(QStringLiteral("controlByte")).toString();
        if (params.contains(QStringLiteral("controlByte")) && controlByte != QStringLiteral("MSB") && controlByte != QStringLiteral("LSB"))
        {
            sendInvalid(client, id, QStringLiteral("controlByte must be MSB or LSB"));
            return;
        }

        const QString oldName = channel->name();
        if (hasPreset)
        {
            channel->setPreset(preset);
            if (preset != QLCChannel::Custom)
                applyPresetCapability(channel); // ChannelEdit::setupPreset
        }
        channel->setName(newName);
        if (group != QLCChannel::NoGroup)
            channel->setGroup(group);
        if (hasColour)
            channel->setColour(colour);
        if (params.contains(QStringLiteral("defaultValue")))
            channel->setDefaultValue(uchar(params.value(QStringLiteral("defaultValue")).toInt()));
        if (params.contains(QStringLiteral("controlByte")))
            channel->setControlByte(controlByte == QStringLiteral("LSB") ? QLCChannel::LSB : QLCChannel::MSB);

        // AliasInfo stores the owning channel's name as sourceChannel and
        // that is what gets saved to the QXF - keep it in step with a rename
        // (aliases *targeting* the old name are deliberately left alone, see
        // the spec).
        if (newName != oldName)
        {
            for (QLCCapability *cap : channel->capabilities())
            {
                QList<AliasInfo> list = cap->aliasList();
                bool touched = false;
                for (AliasInfo &info : list)
                {
                    if (info.sourceChannel == oldName)
                    {
                        info.sourceChannel = newName;
                        touched = true;
                    }
                }
                if (touched)
                    cap->replaceAliases(list);
            }
        }

        commitSession(s, QStringLiteral("channel.update"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.remove"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return;
        const QJsonArray ids = params.value(QStringLiteral("channelIds")).toArray();
        if (ids.isEmpty())
        {
            sendInvalid(client, id, QStringLiteral("channelIds must be a non-empty array"));
            return;
        }
        QList<QLCChannel *> victims;
        for (const QJsonValue &v : ids)
        {
            QLCChannel *channel = s->channelById(v.toString());
            if (channel == nullptr)
            {
                sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such channel: %1").arg(v.toString()));
                return;
            }
            if (victims.contains(channel) == false)
                victims.append(channel);
        }

        QStringList victimNames;
        for (QLCChannel *channel : victims)
            victimNames << channel->name();

        // Modes first, keeping heads/acts-on consistent (the engine's own
        // removeChannel() leaves both index-based structures stale).
        for (QLCFixtureMode *mode : s->def->modes())
        {
            const QVector<QLCChannel *> current = mode->channels();
            QList<QLCChannel *> kept, actsOn;
            for (int i = 0; i < current.count(); i++)
            {
                if (victims.contains(current.at(i)))
                    continue;
                kept.append(current.at(i));
                quint32 target = mode->channelActsOn(quint32(i));
                QLCChannel *actsOnChannel = (target != QLCChannel::invalid() && int(target) < current.count())
                                            ? current.at(int(target)) : nullptr;
                actsOn.append(victims.contains(actsOnChannel) ? nullptr : actsOnChannel);
            }
            rebuildModeChannels(mode, kept, actsOn);
        }
        // Alias entries pointing at a removed channel are dropped.
        for (QLCChannel *channel : s->def->channels())
        {
            if (victims.contains(channel))
                continue;
            for (QLCCapability *cap : channel->capabilities())
            {
                QList<AliasInfo> list = cap->aliasList();
                QList<AliasInfo> kept;
                for (const AliasInfo &info : list)
                {
                    if (victimNames.contains(info.targetChannel) == false)
                        kept.append(info);
                }
                if (kept.count() != list.count())
                    cap->replaceAliases(kept);
            }
        }
        for (QLCChannel *channel : victims)
        {
            s->channelIds.remove(channel);
            s->def->removeChannel(channel);
        }
        commitSession(s, QStringLiteral("channel.remove"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.wizard"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return;
        const int amount = params.value(QStringLiteral("amount")).toInt(1);
        if (amount < 1 || amount > 1000)
        {
            sendInvalid(client, id, QStringLiteral("amount must be 1..1000"));
            return;
        }
        QString label = params.value(QStringLiteral("label")).toString();
        if (label.isEmpty())
            label = QStringLiteral("Channel #");
        const QString type = params.value(QStringLiteral("type")).toString();

        // PopupChannelWizard's type list: a primary colour, a compound
        // colour set, or a channel group.
        struct Component { QString name; bool isColour; QLCChannel::PrimaryColour colour; QLCChannel::Group group; };
        QList<Component> components;
        static const QHash<QString, QStringList> compound = {
            { QStringLiteral("RGB"),   { QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue") } },
            { QStringLiteral("RGBW"),  { QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue"), QStringLiteral("White") } },
            { QStringLiteral("RGBA"),  { QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue"), QStringLiteral("Amber") } },
            { QStringLiteral("RGBL"),  { QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue"), QStringLiteral("Lime") } },
            { QStringLiteral("RGBAW"), { QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue"), QStringLiteral("Amber"), QStringLiteral("White") } },
        };
        QLCChannel::PrimaryColour colour = QLCChannel::NoColour;
        if (compound.contains(type))
        {
            for (const QString &c : compound.value(type))
                components.append({ c, true, QLCChannel::stringToColour(c), QLCChannel::Intensity });
        }
        else if (QLCChannel::colourList().contains(type))
        {
            colour = QLCChannel::stringToColour(type);
            components.append({ QString(), true, colour, QLCChannel::Intensity });
        }
        else
        {
            QLCChannel::Group group = QLCChannel::stringToGroup(type);
            if (type == QStringLiteral("Dimmer"))
                group = QLCChannel::Intensity;
            else if (type == QStringLiteral("Color Macro"))
                group = QLCChannel::Colour;
            if (group == QLCChannel::NoGroup)
            {
                sendInvalid(client, id, QStringLiteral("Unknown wizard channel type"));
                return;
            }
            components.append({ QString(), false, QLCChannel::NoColour, group });
        }

        // Every name must be free before anything is created.
        QStringList names;
        for (int i = 0; i < amount; i++)
        {
            for (const Component &comp : components)
            {
                QString name = components.count() == 1 ? expandLabel(label, i + 1)
                                                       : QStringLiteral("%1 %2").arg(comp.name).arg(i + 1);
                if (names.contains(name) || s->def->channel(name) != nullptr)
                {
                    sendInvalid(client, id, QStringLiteral("A channel named '%1' already exists").arg(name));
                    return;
                }
                names << name;
            }
        }

        QJsonArray channelIds;
        int n = 0;
        for (int i = 0; i < amount; i++)
        {
            for (const Component &comp : components)
            {
                QLCChannel *channel = makePresetChannel(names.at(n++), comp.isColour, comp.colour, comp.group);
                s->def->addChannel(channel);
                channelIds.append(s->channelId(channel));
            }
        }
        commitSession(s, QStringLiteral("channel.wizard"), client->clientId());
        QJsonObject extra;
        extra.insert(QStringLiteral("channelIds"), channelIds);
        sendSessionAck(client, id, s, extra);
    });

    // ------------------------------------------------------------ capabilities

    // Shared lookup for every capability-scoped method: session (revision
    // checked), channel by id, capability by index. Sends its own errors.
    auto resolveCapability = [this](ApiSession *client, const QString &id, const QJsonObject &params,
                                    Session *&s, QLCChannel *&channel, QLCCapability *&cap, bool needIndex) -> bool
    {
        s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return false;
        channel = s->channelById(params.value(QStringLiteral("channelId")).toString());
        if (channel == nullptr)
        {
            sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such channel"));
            return false;
        }
        cap = nullptr;
        if (needIndex)
        {
            int index = params.value(QStringLiteral("capabilityIndex")).toInt(-1);
            if (index < 0 || index >= channel->capabilities().count())
            {
                sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such capability index"));
                return false;
            }
            cap = channel->capabilities().at(index);
        }
        return true;
    };

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.capability.add"), [this, resolveCapability](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCChannel *channel; QLCCapability *unused;
        if (resolveCapability(client, id, params, s, channel, unused, false) == false)
            return;
        int min = 0;
        if (channel->capabilities().isEmpty() == false)
            min = channel->capabilities().last()->max() + 1; // ChannelEdit::addNewCapability
        if (params.contains(QStringLiteral("min")))
            min = params.value(QStringLiteral("min")).toInt(-1);
        int max = params.value(QStringLiteral("max")).toInt(255);
        if (min < 0 || min > 255 || max < 0 || max > 255 || min > max)
        {
            sendInvalid(client, id, min > 255 && params.contains(QStringLiteral("min")) == false
                        ? QStringLiteral("Channel has no free DMX range left")
                        : QStringLiteral("min/max must be 0..255 with min <= max"));
            return;
        }
        QLCCapability *cap = new QLCCapability(uchar(min), uchar(max), params.value(QStringLiteral("name")).toString());
        if (channel->addCapability(cap) == false)
        {
            delete cap;
            sendError(client, id, ErrRangeOverlap, QStringLiteral("Range %1-%2 overlaps an existing capability").arg(min).arg(max));
            return;
        }
        const int index = channel->capabilities().count() - 1;
        commitSession(s, QStringLiteral("capability.add"), client->clientId());
        QJsonObject extra;
        extra.insert(QStringLiteral("capabilityIndex"), index);
        sendSessionAck(client, id, s, extra);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.capability.update"), [this, resolveCapability](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCChannel *channel; QLCCapability *cap;
        if (resolveCapability(client, id, params, s, channel, cap, true) == false)
            return;
        const int index = params.value(QStringLiteral("capabilityIndex")).toInt();

        int min = params.value(QStringLiteral("min")).toInt(cap->min());
        int max = params.value(QStringLiteral("max")).toInt(cap->max());
        if (min < 0 || min > 255 || max < 0 || max > 255 || min > max)
        {
            sendInvalid(client, id, QStringLiteral("min/max must be 0..255 with min <= max"));
            return;
        }
        QLCCapability::Preset preset = cap->preset();
        if (params.contains(QStringLiteral("preset")) &&
            parseCapabilityPreset(params.value(QStringLiteral("preset")).toString(), preset) == false)
        {
            sendInvalid(client, id, QStringLiteral("Unknown capability preset"));
            return;
        }
        // Resources are re-derived when the preset or the resources change:
        // a fresh capability carries exactly the values that fit the
        // resulting preset type (QLCCapability can't shrink its list).
        const bool rebuild = params.contains(QStringLiteral("preset")) || params.contains(QStringLiteral("resources"));
        QVariantList resources;
        if (rebuild)
        {
            QLCCapability probe;
            probe.setPreset(preset);
            QJsonArray arr = params.contains(QStringLiteral("resources")) ? params.value(QStringLiteral("resources")).toArray()
                                                                          : QJsonArray();
            if (params.contains(QStringLiteral("resources")) == false && probe.presetType() == cap->presetType())
            {
                // preset changed within the same type: keep the values
                for (const QVariant &v : cap->resources())
                    arr.append(resourceToJson(v));
            }
            QString error;
            if (resourcesFromJson(probe.presetType(), arr, resources, error) == false)
            {
                sendInvalid(client, id, error);
                return;
            }
        }

        if (params.contains(QStringLiteral("min")) || params.contains(QStringLiteral("max")))
        {
            if (channel->setCapabilityRange(cap, uchar(min), uchar(max)) == false)
            {
                sendError(client, id, ErrRangeOverlap, QStringLiteral("Range %1-%2 overlaps another capability").arg(min).arg(max));
                return;
            }
        }
        if (params.contains(QStringLiteral("name")))
            cap->setName(params.value(QStringLiteral("name")).toString());

        if (rebuild)
        {
            QLCCapability *fresh = new QLCCapability(cap->min(), cap->max(), cap->name());
            fresh->setPreset(preset);
            for (int i = 0; i < resources.count(); i++)
                fresh->setResource(i, resources.at(i));
            if (preset == QLCCapability::Alias)
            {
                for (const AliasInfo &alias : cap->aliasList())
                    fresh->addAlias(alias);
            }
            replaceCapabilityAt(channel, index, fresh);
        }
        commitSession(s, QStringLiteral("capability.update"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.capability.remove"), [this, resolveCapability](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCChannel *channel; QLCCapability *cap;
        if (resolveCapability(client, id, params, s, channel, cap, true) == false)
            return;
        channel->removeCapability(cap);
        commitSession(s, QStringLiteral("capability.remove"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.capability.wizard"), [this, resolveCapability](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCChannel *channel; QLCCapability *unused;
        if (resolveCapability(client, id, params, s, channel, unused, false) == false)
            return;
        const int start = params.value(QStringLiteral("start")).toInt(0);
        const int width = params.value(QStringLiteral("width")).toInt(1);
        const int amount = params.value(QStringLiteral("amount")).toInt(1);
        QString label = params.value(QStringLiteral("label")).toString();
        if (label.isEmpty())
            label = QStringLiteral("Capability #");
        if (start < 0 || start > 254 || width < 1 || amount < 1)
        {
            sendInvalid(client, id, QStringLiteral("start must be 0..254, width and amount >= 1"));
            return;
        }
        // 64-bit: width * amount in int wraps (65536 * 65536 == 0), which
        // slipped past this check into a loop creating `amount` capabilities
        const qint64 end = qint64(start) + qint64(width) * amount - 1;
        if (end > 255)
        {
            sendInvalid(client, id, QStringLiteral("start + width * amount exceeds 255"));
            return;
        }
        // ChannelEdit::checkAvailability()
        for (QLCCapability *cap : channel->capabilities())
        {
            if (cap->max() >= start && cap->min() <= end)
            {
                sendError(client, id, ErrRangeOverlap,
                          QStringLiteral("Range %1-%2 overlaps existing capability '%3' (%4-%5)")
                              .arg(start).arg(end).arg(cap->name()).arg(cap->min()).arg(cap->max()));
                return;
            }
        }
        QJsonArray indexes;
        for (int i = 0; i < amount; i++)
        {
            const int min = start + width * i;
            QLCCapability *cap = new QLCCapability(uchar(min), uchar(min + width - 1), expandLabel(label, i + 1));
            channel->addCapability(cap); // cannot overlap: checked above
            indexes.append(channel->capabilities().count() - 1);
        }
        commitSession(s, QStringLiteral("capability.wizard"), client->clientId());
        QJsonObject extra;
        extra.insert(QStringLiteral("capabilityIndexes"), indexes);
        sendSessionAck(client, id, s, extra);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.capability.autoPatchColors"), [this, resolveCapability](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCChannel *channel; QLCCapability *unused;
        if (resolveCapability(client, id, params, s, channel, unused, false) == false)
            return;
        bool changed = false;
        if (channel->group() == QLCChannel::Colour)
        {
            const QVector<NamedColor> named = loadNamedColors();
            // Re-read the list every round: replaceCapabilityAt() rebuilds
            // it, so pointers taken before are dead afterwards.
            for (int i = 0; i < channel->capabilities().count(); i++)
            {
                QLCCapability *cap = channel->capabilities().at(i);
                if (isEntirelyLowercase(cap->name()))
                {
                    cap->setName(titleCaseWords(cap->name()));
                    changed = true;
                }
                const QList<QColor> colors = detectNamedColors(cap->name(), named);
                if (colors.isEmpty())
                    continue;
                QLCCapability *fresh = new QLCCapability(cap->min(), cap->max(), cap->name());
                fresh->setPreset(colors.count() >= 2 ? QLCCapability::ColorDoubleMacro : QLCCapability::ColorMacro);
                fresh->setResource(0, colors.at(0));
                if (colors.count() >= 2)
                    fresh->setResource(1, colors.at(1));
                replaceCapabilityAt(channel, i, fresh);
                changed = true;
            }
        }
        if (changed)
            commitSession(s, QStringLiteral("capability.autoPatchColors"), client->clientId());
        sendSessionAck(client, id, s);
    });

    // ----------------------------------------------------------------- aliases

    auto resolveAliasCapability = [this, resolveCapability](ApiSession *client, const QString &id, const QJsonObject &params,
                                                            Session *&s, QLCChannel *&channel, QLCCapability *&cap) -> bool
    {
        if (resolveCapability(client, id, params, s, channel, cap, true) == false)
            return false;
        if (cap->preset() != QLCCapability::Alias)
        {
            sendInvalid(client, id, QStringLiteral("Capability preset must be Alias"));
            return false;
        }
        return true;
    };

    auto validAliasTarget = [](ApiSession *client, const QString &id, Session *s, QLCChannel *channel,
                               const QString &targetMode, const QString &targetChannel, bool checkMode, bool checkChannel) -> bool
    {
        if (checkMode)
        {
            QLCFixtureMode *mode = s->def->mode(targetMode);
            if (targetMode.isEmpty() || mode == nullptr || mode->channel(channel->name()) == nullptr)
            {
                sendInvalid(client, id, QStringLiteral("targetMode must name a mode that contains this channel"));
                return false;
            }
        }
        if (checkChannel && (targetChannel.isEmpty() || s->def->channel(targetChannel) == nullptr))
        {
            sendInvalid(client, id, QStringLiteral("targetChannel must name an existing channel"));
            return false;
        }
        return true;
    };

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.capability.alias.add"), [this, resolveAliasCapability, validAliasTarget](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCChannel *channel; QLCCapability *cap;
        if (resolveAliasCapability(client, id, params, s, channel, cap) == false)
            return;
        const QString targetMode = params.value(QStringLiteral("targetMode")).toString();
        const QString targetChannel = params.value(QStringLiteral("targetChannel")).toString();
        if (validAliasTarget(client, id, s, channel, targetMode, targetChannel, true, true) == false)
            return;
        QList<AliasInfo> list = cap->aliasList();
        for (int i = 0; i < list.count(); i++)
        {
            const AliasInfo &info = list.at(i);
            if (info.targetMode == targetMode && info.sourceChannel == channel->name() && info.targetChannel == targetChannel)
            {
                QJsonObject extra;
                extra.insert(QStringLiteral("aliasIndex"), i); // identical alias exists: no-op
                sendSessionAck(client, id, s, extra);
                return;
            }
        }
        AliasInfo alias;
        alias.targetMode = targetMode;
        alias.sourceChannel = channel->name();
        alias.targetChannel = targetChannel;
        cap->addAlias(alias);
        commitSession(s, QStringLiteral("alias.add"), client->clientId());
        QJsonObject extra;
        extra.insert(QStringLiteral("aliasIndex"), cap->aliasList().count() - 1);
        sendSessionAck(client, id, s, extra);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.capability.alias.update"), [this, resolveAliasCapability, validAliasTarget](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCChannel *channel; QLCCapability *cap;
        if (resolveAliasCapability(client, id, params, s, channel, cap) == false)
            return;
        QList<AliasInfo> list = cap->aliasList();
        const int index = params.value(QStringLiteral("aliasIndex")).toInt(-1);
        if (index < 0 || index >= list.count())
        {
            sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such alias index"));
            return;
        }
        const bool hasMode = params.contains(QStringLiteral("targetMode"));
        const bool hasChannel = params.contains(QStringLiteral("targetChannel"));
        const QString targetMode = params.value(QStringLiteral("targetMode")).toString();
        const QString targetChannel = params.value(QStringLiteral("targetChannel")).toString();
        if (validAliasTarget(client, id, s, channel, targetMode, targetChannel, hasMode, hasChannel) == false)
            return;
        AliasInfo info = list.at(index);
        if (hasMode)
            info.targetMode = targetMode;
        if (hasChannel)
            info.targetChannel = targetChannel;
        list.replace(index, info);
        cap->replaceAliases(list);
        commitSession(s, QStringLiteral("alias.update"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.capability.alias.remove"), [this, resolveAliasCapability](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCChannel *channel; QLCCapability *cap;
        if (resolveAliasCapability(client, id, params, s, channel, cap) == false)
            return;
        QList<AliasInfo> list = cap->aliasList();
        const int index = params.value(QStringLiteral("aliasIndex")).toInt(-1);
        if (index < 0 || index >= list.count())
        {
            sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such alias index"));
            return;
        }
        list.removeAt(index);
        cap->replaceAliases(list);
        commitSession(s, QStringLiteral("alias.remove"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.channel.capability.alias.applyToAllModes"), [this, resolveAliasCapability](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCChannel *channel; QLCCapability *cap;
        if (resolveAliasCapability(client, id, params, s, channel, cap) == false)
            return;
        // AliasEdit::applyToAllModes()
        QString target = params.value(QStringLiteral("targetChannel")).toString();
        if (target.isEmpty())
        {
            if (s->def->channels().isEmpty() == false)
                target = s->def->channels().first()->name();
        }
        else if (s->def->channel(target) == nullptr)
        {
            sendInvalid(client, id, QStringLiteral("targetChannel must name an existing channel"));
            return;
        }
        QList<AliasInfo> list = cap->aliasList();
        QStringList existing;
        for (const AliasInfo &info : list)
            existing << info.targetMode;
        int added = 0;
        if (target.isEmpty() == false)
        {
            for (QLCFixtureMode *mode : s->def->modes())
            {
                if (mode->channel(channel->name()) == nullptr || existing.contains(mode->name()))
                    continue;
                AliasInfo alias;
                alias.targetMode = mode->name();
                alias.sourceChannel = channel->name();
                alias.targetChannel = target;
                list.append(alias);
                added++;
            }
        }
        if (added > 0)
        {
            cap->replaceAliases(list);
            commitSession(s, QStringLiteral("alias.applyToAllModes"), client->clientId());
        }
        QJsonObject extra;
        extra.insert(QStringLiteral("addedCount"), added);
        sendSessionAck(client, id, s, extra);
    });

    // ------------------------------------------------------------------- modes

    auto resolveMode = [this](ApiSession *client, const QString &id, const QJsonObject &params,
                              Session *&s, QLCFixtureMode *&mode) -> bool
    {
        s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return false;
        mode = s->modeById(params.value(QStringLiteral("modeId")).toString());
        if (mode == nullptr)
        {
            sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such mode"));
            return false;
        }
        return true;
    };

    dispatcher->registerMethod(QStringLiteral("fixturedefs.mode.add"), [this](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s = resolveSession(client, id, params, true);
        if (s == nullptr)
            return;
        QString name = params.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
        {
            name = QStringLiteral("New mode");
            int n = 1;
            while (s->def->mode(name) != nullptr)
                name = QStringLiteral("New mode %1").arg(++n);
        }
        if (s->def->mode(name) != nullptr)
        {
            sendInvalid(client, id, QStringLiteral("A mode named '%1' already exists").arg(name));
            return;
        }
        QLCFixtureMode *mode = new QLCFixtureMode(s->def);
        mode->setName(name);
        s->def->addMode(mode);
        const QString modeId = s->modeId(mode);
        commitSession(s, QStringLiteral("mode.add"), client->clientId());
        QJsonObject extra;
        extra.insert(QStringLiteral("modeId"), modeId);
        sendSessionAck(client, id, s, extra);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.mode.rename"), [this, resolveMode](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCFixtureMode *mode;
        if (resolveMode(client, id, params, s, mode) == false)
            return;
        const QString name = params.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
        {
            sendInvalid(client, id, QStringLiteral("name is required"));
            return;
        }
        QLCFixtureMode *clash = s->def->mode(name);
        if (clash != nullptr && clash != mode)
        {
            sendInvalid(client, id, QStringLiteral("A mode named '%1' already exists").arg(name));
            return;
        }
        mode->setName(name);
        commitSession(s, QStringLiteral("mode.rename"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.mode.remove"), [this, resolveMode](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCFixtureMode *mode;
        if (resolveMode(client, id, params, s, mode) == false)
            return;
        s->modeIds.remove(mode);
        s->def->removeMode(mode);
        commitSession(s, QStringLiteral("mode.remove"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.mode.setChannels"), [this, resolveMode](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCFixtureMode *mode;
        if (resolveMode(client, id, params, s, mode) == false)
            return;
        if (params.value(QStringLiteral("channels")).isArray() == false)
        {
            sendInvalid(client, id, QStringLiteral("channels must be an array"));
            return;
        }
        const QJsonArray slotArray = params.value(QStringLiteral("channels")).toArray();
        QList<QLCChannel *> channels;
        QList<QString> actsOnIds;
        for (const QJsonValue &v : slotArray)
        {
            const QJsonObject slot = v.toObject();
            QLCChannel *channel = s->channelById(slot.value(QStringLiteral("channelId")).toString());
            if (channel == nullptr)
            {
                sendError(client, id, ApiEnvelope::ErrNotFound,
                          QStringLiteral("No such channel: %1").arg(slot.value(QStringLiteral("channelId")).toString()));
                return;
            }
            if (channels.contains(channel))
            {
                sendInvalid(client, id, QStringLiteral("Channel %1 appears twice in the mode").arg(s->channelId(channel)));
                return;
            }
            channels.append(channel);
            actsOnIds.append(slot.value(QStringLiteral("actsOnChannelId")).toString());
        }
        QList<QLCChannel *> actsOn;
        for (int i = 0; i < channels.count(); i++)
        {
            const QString actsOnId = actsOnIds.at(i);
            if (actsOnId.isEmpty())
            {
                actsOn.append(nullptr);
                continue;
            }
            if (actsOnId == s->channelId(channels.at(i)))
            {
                sendError(client, id, ErrActsOnSelf, QStringLiteral("Slot %1 cannot act on itself").arg(i));
                return;
            }
            QLCChannel *target = s->channelById(actsOnId);
            if (target == nullptr || channels.contains(target) == false)
            {
                sendInvalid(client, id, QStringLiteral("actsOnChannelId %1 is not part of the mode").arg(actsOnId));
                return;
            }
            actsOn.append(target);
        }
        rebuildModeChannels(mode, channels, actsOn);
        commitSession(s, QStringLiteral("mode.setChannels"), client->clientId());
        sendSessionAck(client, id, s);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.mode.setPhysical"), [this, resolveMode](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCFixtureMode *mode;
        if (resolveMode(client, id, params, s, mode) == false)
            return;
        if (params.value(QStringLiteral("useGlobalPhysical")).isBool() == false)
        {
            sendInvalid(client, id, QStringLiteral("useGlobalPhysical (boolean) is required"));
            return;
        }
        if (params.value(QStringLiteral("useGlobalPhysical")).toBool())
        {
            mode->resetPhysical();
        }
        else
        {
            QLCPhysical phy = mode->useGlobalPhysical() ? s->def->physical() : mode->physical();
            if (params.value(QStringLiteral("physical")).isObject())
                applyPhysicalFromJson(phy, params.value(QStringLiteral("physical")).toObject());
            mode->setPhysical(phy);
        }
        commitSession(s, QStringLiteral("mode.setPhysical"), client->clientId());
        sendSessionAck(client, id, s);
    });

    // ------------------------------------------------------------------- heads

    dispatcher->registerMethod(QStringLiteral("fixturedefs.mode.head.add"), [this, resolveMode](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCFixtureMode *mode;
        if (resolveMode(client, id, params, s, mode) == false)
            return;
        const QJsonArray ids = params.value(QStringLiteral("channelIds")).toArray();
        if (ids.isEmpty())
        {
            sendInvalid(client, id, QStringLiteral("channelIds must be a non-empty array"));
            return;
        }
        QLCFixtureHead head;
        for (const QJsonValue &v : ids)
        {
            QLCChannel *channel = s->channelById(v.toString());
            quint32 index = channel != nullptr ? mode->channelNumber(channel) : QLCChannel::invalid();
            if (index == QLCChannel::invalid())
            {
                sendInvalid(client, id, QStringLiteral("Channel %1 is not part of this mode").arg(v.toString()));
                return;
            }
            head.addChannel(index);
        }
        mode->insertHead(-1, head);
        commitSession(s, QStringLiteral("head.add"), client->clientId());
        QJsonObject extra;
        extra.insert(QStringLiteral("headIndex"), mode->heads().count() - 1);
        sendSessionAck(client, id, s, extra);
    });

    dispatcher->registerMethod(QStringLiteral("fixturedefs.mode.head.remove"), [this, resolveMode](ApiSession *client, const QString &id, const QJsonObject &params)
    {
        Session *s; QLCFixtureMode *mode;
        if (resolveMode(client, id, params, s, mode) == false)
            return;
        const QJsonArray arr = params.value(QStringLiteral("headIndexes")).toArray();
        if (arr.isEmpty())
        {
            sendInvalid(client, id, QStringLiteral("headIndexes must be a non-empty array"));
            return;
        }
        QVector<int> indexes;
        for (const QJsonValue &v : arr)
        {
            int index = v.toInt(-1);
            if (index < 0 || index >= mode->heads().count())
            {
                sendError(client, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such head index: %1").arg(index));
                return;
            }
            if (indexes.contains(index) == false)
                indexes.append(index);
        }
        std::sort(indexes.begin(), indexes.end(), std::greater<int>()); // ModeEdit::deleteHeads
        for (int index : indexes)
            mode->removeHead(index);
        commitSession(s, QStringLiteral("head.remove"), client->clientId());
        sendSessionAck(client, id, s);
    });
}
