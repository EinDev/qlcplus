/*
  Q Light Controller Plus - Control API
  apirgbmatrixdomain.cpp

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

#include <QJsonArray>
#include <QScopedPointer>
#include <QMutexLocker>
#include <QColor>
#include <QFont>
#include <QUrl>

#include "apirgbmatrixdomain.h"
#include "apifunctionsdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "rgbscriptscache.h"
#include "rgbalgorithm.h"
#include "fixturegroup.h"
#include "rgbmatrix.h"
#include "rgbimage.h"
#include "rgbplain.h"
#include "rgbaudio.h"
#include "rgbtext.h"
#include "universe.h"
#include "doc.h"

/*****************************************************************************
 * JSON helpers (file-local)
 *****************************************************************************/

namespace
{

QString algorithmTypeToString(RGBAlgorithm::Type type)
{
    switch (type)
    {
        case RGBAlgorithm::Text:   return QStringLiteral("text");
        case RGBAlgorithm::Script: return QStringLiteral("script");
        case RGBAlgorithm::Image:  return QStringLiteral("image");
        case RGBAlgorithm::Audio:  return QStringLiteral("audio");
        case RGBAlgorithm::Plain:
        default:                   return QStringLiteral("plain");
    }
}

/** The built-in algorithm's catalog name for a FunctionsRgbMatrixAlgorithmType
 *  other than 'script' (RGBAlgorithm::algorithm() resolves by that name),
 *  or an empty string for 'script'/unknown. */
QString builtinNameForType(Doc *doc, const QString &type)
{
    if (type == QStringLiteral("plain"))
        return RGBPlain(doc).name();
    if (type == QStringLiteral("text"))
        return RGBText(doc).name();
    if (type == QStringLiteral("image"))
        return RGBImage(doc).name();
    if (type == QStringLiteral("audio"))
        return RGBAudio(doc).name();
    return QString();
}

QString controlModeToString(RGBMatrix::ControlMode mode)
{
    switch (mode)
    {
        case RGBMatrix::ControlModeWhite:   return QStringLiteral("white");
        case RGBMatrix::ControlModeAmber:   return QStringLiteral("amber");
        case RGBMatrix::ControlModeUV:      return QStringLiteral("uv");
        case RGBMatrix::ControlModeDimmer:  return QStringLiteral("dimmer");
        case RGBMatrix::ControlModeShutter: return QStringLiteral("shutter");
        case RGBMatrix::ControlModeRgb:
        default:                            return QStringLiteral("rgb");
    }
}

bool controlModeFromString(const QString &str, RGBMatrix::ControlMode &mode)
{
    if (str == QStringLiteral("rgb"))          mode = RGBMatrix::ControlModeRgb;
    else if (str == QStringLiteral("white"))   mode = RGBMatrix::ControlModeWhite;
    else if (str == QStringLiteral("amber"))   mode = RGBMatrix::ControlModeAmber;
    else if (str == QStringLiteral("uv"))      mode = RGBMatrix::ControlModeUV;
    else if (str == QStringLiteral("dimmer"))  mode = RGBMatrix::ControlModeDimmer;
    else if (str == QStringLiteral("shutter")) mode = RGBMatrix::ControlModeShutter;
    else return false;
    return true;
}

QString textAnimationToString(RGBText::AnimationStyle style)
{
    switch (style)
    {
        case RGBText::Horizontal:    return QStringLiteral("horizontal");
        case RGBText::Vertical:      return QStringLiteral("vertical");
        case RGBText::StaticLetters:
        default:                     return QStringLiteral("staticLetters");
    }
}

bool textAnimationFromString(const QString &str, RGBText::AnimationStyle &style)
{
    if (str == QStringLiteral("staticLetters"))   style = RGBText::StaticLetters;
    else if (str == QStringLiteral("horizontal")) style = RGBText::Horizontal;
    else if (str == QStringLiteral("vertical"))   style = RGBText::Vertical;
    else return false;
    return true;
}

QString imageAnimationToString(RGBImage::AnimationStyle style)
{
    switch (style)
    {
        case RGBImage::Horizontal: return QStringLiteral("horizontal");
        case RGBImage::Vertical:   return QStringLiteral("vertical");
        case RGBImage::Animation:  return QStringLiteral("animation");
        case RGBImage::Static:
        default:                   return QStringLiteral("static");
    }
}

bool imageAnimationFromString(const QString &str, RGBImage::AnimationStyle &style)
{
    if (str == QStringLiteral("static"))          style = RGBImage::Static;
    else if (str == QStringLiteral("horizontal")) style = RGBImage::Horizontal;
    else if (str == QStringLiteral("vertical"))   style = RGBImage::Vertical;
    else if (str == QStringLiteral("animation"))  style = RGBImage::Animation;
    else return false;
    return true;
}

QString propertyTypeToString(RGBScriptProperty::ValueType type)
{
    switch (type)
    {
        case RGBScriptProperty::List:   return QStringLiteral("list");
        case RGBScriptProperty::Range:  return QStringLiteral("range");
        case RGBScriptProperty::Float:  return QStringLiteral("float");
        case RGBScriptProperty::String: return QStringLiteral("string");
        case RGBScriptProperty::None:
        default:                        return QStringLiteral("none");
    }
}

QJsonValue colorToJson(const QColor &color)
{
    if (color.isValid() == false)
        return QJsonValue();
    return color.name(); // #rrggbb
}

QJsonObject fontToJson(const QFont &font)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("family"), font.family());
    obj.insert(QStringLiteral("pointSize"), font.pointSize());
    obj.insert(QStringLiteral("bold"), font.bold());
    obj.insert(QStringLiteral("italic"), font.italic());
    return obj;
}

QJsonArray scriptPropertyDefsToJson(RGBScript *script)
{
    QJsonArray arr;
    for (const RGBScriptProperty &prop : script->properties())
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), prop.m_name);
        obj.insert(QStringLiteral("displayName"), prop.m_displayName);
        obj.insert(QStringLiteral("type"), propertyTypeToString(prop.m_type));
        if (prop.m_type == RGBScriptProperty::List)
        {
            QJsonArray values;
            for (const QString &v : prop.m_listValues)
                values.append(v);
            obj.insert(QStringLiteral("listValues"), values);
        }
        else if (prop.m_type == RGBScriptProperty::Range)
        {
            obj.insert(QStringLiteral("rangeMin"), prop.m_rangeMinValue);
            obj.insert(QStringLiteral("rangeMax"), prop.m_rangeMaxValue);
        }
        arr.append(obj);
    }
    return arr;
}

QJsonObject conflictDetails(Doc *doc)
{
    QJsonObject details;
    details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return details;
}

QJsonObject docRevisionResult(Doc *doc)
{
    QJsonObject result;
    result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return result;
}

/** Resolve params.functionId to an RGBMatrix, or answer NOT_FOUND/
 *  INVALID_PARAMS and return null. */
RGBMatrix *findMatrixOrRespond(Doc *doc, const QJsonObject &params, ApiSession *session, const QString &id)
{
    QString fidStr = params.value(QStringLiteral("functionId")).toString();
    bool ok = false;
    quint32 fid = fidStr.toUInt(&ok);
    Function *function = ok ? doc->function(fid) : nullptr;
    if (function == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No function with id %1").arg(fidStr)));
        return nullptr;
    }
    if (function->type() != Function::RGBMatrixType)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("Function %1 is not an RGB Matrix").arg(fidStr)));
        return nullptr;
    }
    return qobject_cast<RGBMatrix *>(function);
}

bool checkBaseRevision(Doc *doc, const QJsonObject &params, ApiSession *session, const QString &id)
{
    quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
    if (baseRevision != doc->docRevision())
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                        QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
        return false;
    }
    return true;
}

} // namespace

/*****************************************************************************
 * ApiRgbMatrixDomain
 *****************************************************************************/

ApiRgbMatrixDomain::ApiRgbMatrixDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Doc *d = m_doc;
    ApiFunctionsDomain::setTypeDetailProvider(int(Function::RGBMatrixType), [d](Function *function)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("functionId"), QString::number(function->id()));
        obj.insert(QStringLiteral("config"), configToJson(d, qobject_cast<RGBMatrix *>(function)));
        obj.insert(QStringLiteral("docRevision"), int(d->docRevision()));
        return obj;
    });

    registerMethods();
}

QJsonObject ApiRgbMatrixDomain::configToJson(Doc *doc, RGBMatrix *matrix)
{
    Q_UNUSED(doc)
    QJsonObject config;
    if (matrix == nullptr)
        return config;

    // FunctionsRgbMatrixConfig.fixtureGroupId is a required string in the
    // spec; a matrix created through functions.create has no group yet
    // (FixtureGroup::invalidId()), reported as null rather than the sentinel
    // so a client can't mistake it for a real id.
    if (matrix->fixtureGroup() == FixtureGroup::invalidId())
        config.insert(QStringLiteral("fixtureGroupId"), QJsonValue());
    else
        config.insert(QStringLiteral("fixtureGroupId"), QString::number(matrix->fixtureGroup()));

    QJsonObject algorithm;
    {
        QMutexLocker locker(&matrix->algorithmMutex());
        RGBAlgorithm *algo = matrix->algorithm();
        if (algo == nullptr)
        {
            algorithm.insert(QStringLiteral("type"), QStringLiteral("plain"));
        }
        else
        {
            algorithm.insert(QStringLiteral("type"), algorithmTypeToString(algo->type()));
            algorithm.insert(QStringLiteral("name"), algo->name());
            algorithm.insert(QStringLiteral("acceptedColors"), algo->acceptColors());
            switch (algo->type())
            {
                case RGBAlgorithm::Script:
                {
                    RGBScript *script = static_cast<RGBScript *>(algo);
                    algorithm.insert(QStringLiteral("scriptName"), script->name());
                    QJsonArray props;
                    for (const RGBScriptProperty &prop : script->properties())
                    {
                        QJsonObject p;
                        p.insert(QStringLiteral("name"), prop.m_name);
                        p.insert(QStringLiteral("value"), matrix->property(prop.m_name));
                        props.append(p);
                    }
                    algorithm.insert(QStringLiteral("scriptProperties"), props);
                }
                break;
                case RGBAlgorithm::Text:
                {
                    RGBText *text = static_cast<RGBText *>(algo);
                    algorithm.insert(QStringLiteral("text"), text->text());
                    algorithm.insert(QStringLiteral("font"), fontToJson(text->font()));
                    algorithm.insert(QStringLiteral("animationStyle"), textAnimationToString(text->animationStyle()));
                    algorithm.insert(QStringLiteral("xOffset"), text->xOffset());
                    algorithm.insert(QStringLiteral("yOffset"), text->yOffset());
                }
                break;
                case RGBAlgorithm::Image:
                {
                    RGBImage *image = static_cast<RGBImage *>(algo);
                    algorithm.insert(QStringLiteral("imagePath"), image->filename());
                    algorithm.insert(QStringLiteral("animationStyle"), imageAnimationToString(image->animationStyle()));
                    algorithm.insert(QStringLiteral("xOffset"), image->xOffset());
                    algorithm.insert(QStringLiteral("yOffset"), image->yOffset());
                }
                break;
                default:
                break;
            }
        }
    }
    config.insert(QStringLiteral("algorithm"), algorithm);

    QJsonArray colors;
    for (int i = 0; i < RGBMatrix::ColorAttributeCount; i++)
        colors.append(colorToJson(matrix->getColor(i)));
    config.insert(QStringLiteral("colors"), colors);

    config.insert(QStringLiteral("controlMode"), controlModeToString(matrix->controlMode()));
    config.insert(QStringLiteral("blendMode"), Universe::blendModeToString(matrix->blendMode()));
    config.insert(QStringLiteral("dimmerControl"), matrix->dimmerControl());
    return config;
}

QString ApiRgbMatrixDomain::applyConfig(RGBMatrix *matrix, const QJsonObject &config, QString &code)
{
    Doc *doc = m_doc;
    code = ApiEnvelope::ErrInvalidParams;

    /***** validate everything first, apply nothing until it all checks out *****/

    // fixture group
    bool hasGroup = config.contains(QStringLiteral("fixtureGroupId"));
    quint32 groupId = FixtureGroup::invalidId();
    if (hasGroup)
    {
        QJsonValue v = config.value(QStringLiteral("fixtureGroupId"));
        if (v.isNull())
        {
            groupId = FixtureGroup::invalidId();
        }
        else
        {
            bool ok = false;
            groupId = v.toString().toUInt(&ok);
            if (ok == false || doc->fixtureGroup(groupId) == nullptr)
            {
                code = ApiEnvelope::ErrNotFound;
                return QStringLiteral("No fixture group with id %1").arg(v.toString());
            }
        }
    }

    // algorithm
    bool hasAlgorithm = config.contains(QStringLiteral("algorithm"));
    QJsonObject algorithm = config.value(QStringLiteral("algorithm")).toObject();
    QString newAlgoName;
    if (hasAlgorithm)
    {
        QString type = algorithm.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("script"))
        {
            newAlgoName = algorithm.value(QStringLiteral("scriptName")).toString();
            if (newAlgoName.isEmpty() || doc->rgbScriptsCache()->names().contains(newAlgoName) == false)
                return QStringLiteral("Unknown RGB script '%1'").arg(newAlgoName);
        }
        else
        {
            newAlgoName = builtinNameForType(doc, type);
            if (newAlgoName.isEmpty())
                return QStringLiteral("Unknown algorithm type '%1'").arg(type);
        }

        if (algorithm.contains(QStringLiteral("animationStyle")))
        {
            QString ani = algorithm.value(QStringLiteral("animationStyle")).toString();
            RGBText::AnimationStyle ts;
            RGBImage::AnimationStyle is;
            bool valid = (type == QStringLiteral("text")) ? textAnimationFromString(ani, ts)
                       : (type == QStringLiteral("image")) ? imageAnimationFromString(ani, is)
                       : true; // ignored for the other types
            if (valid == false)
                return QStringLiteral("Invalid animationStyle '%1' for a %2 algorithm").arg(ani, type);
        }
    }

    // colours
    bool hasColors = config.contains(QStringLiteral("colors"));
    QVector<QColor> colors;
    if (hasColors)
    {
        QJsonArray arr = config.value(QStringLiteral("colors")).toArray();
        if (arr.count() > RGBMatrix::ColorAttributeCount)
            return QStringLiteral("colors has %1 entries, at most %2 allowed").arg(arr.count()).arg(int(RGBMatrix::ColorAttributeCount));
        for (const QJsonValue &v : arr)
        {
            if (v.isNull() || v.isUndefined())
            {
                colors.append(QColor());
                continue;
            }
            QColor c(v.toString());
            if (c.isValid() == false)
                return QStringLiteral("Invalid color '%1'").arg(v.toString());
            colors.append(c);
        }
    }

    // control / blend mode
    bool hasControlMode = config.contains(QStringLiteral("controlMode"));
    RGBMatrix::ControlMode controlMode = RGBMatrix::ControlModeRgb;
    if (hasControlMode && controlModeFromString(config.value(QStringLiteral("controlMode")).toString(), controlMode) == false)
        return QStringLiteral("Invalid controlMode '%1'").arg(config.value(QStringLiteral("controlMode")).toString());

    bool hasBlendMode = config.contains(QStringLiteral("blendMode"));
    Universe::BlendMode blendMode = Universe::NormalBlend;
    if (hasBlendMode)
    {
        QString str = config.value(QStringLiteral("blendMode")).toString();
        blendMode = Universe::stringToBlendMode(str);
        if (Universe::blendModeToString(blendMode) != str)
            return QStringLiteral("Invalid blendMode '%1'").arg(str);
    }

    /***** apply, in dependency order: group -> algorithm -> parameters -> colours -> modes *****/

    if (hasGroup)
        matrix->setFixtureGroup(groupId);

    if (hasAlgorithm)
    {
        bool sameAlgorithm = false;
        {
            QMutexLocker locker(&matrix->algorithmMutex());
            sameAlgorithm = matrix->algorithm() != nullptr && matrix->algorithm()->name() == newAlgoName;
        }
        if (sameAlgorithm == false)
        {
            // Mirrors RGBMatrixEditor::setAlgorithmIndex(): hand the matrix's
            // current colours to the new algorithm before installing it, so a
            // script's rgbMapGetColors() read-back in setAlgorithm() starts from
            // them. RGBAlgorithm::algorithm() never returns null for a name
            // validated above.
            RGBAlgorithm *algo = RGBAlgorithm::algorithm(doc, newAlgoName);
            algo->setColors(matrix->getColors());
            matrix->setAlgorithm(algo);
        }

        QMutexLocker locker(&matrix->algorithmMutex());
        RGBAlgorithm *algo = matrix->algorithm();
        if (algo != nullptr && algo->type() == RGBAlgorithm::Script)
        {
            RGBScript *script = static_cast<RGBScript *>(algo);
            QStringList known;
            for (const RGBScriptProperty &prop : script->properties())
                known << prop.m_name;
            for (const QJsonValue &v : algorithm.value(QStringLiteral("scriptProperties")).toArray())
            {
                QJsonObject p = v.toObject();
                QString name = p.value(QStringLiteral("name")).toString();
                if (known.contains(name) == false)
                    continue; // unknown for this script: never cached into m_properties/XML
                QString value = p.value(QStringLiteral("value")).toVariant().toString();
                if (matrix->property(name) != value)
                    matrix->setProperty(name, value);
            }
        }
        else if (algo != nullptr && algo->type() == RGBAlgorithm::Text)
        {
            RGBText *text = static_cast<RGBText *>(algo);
            if (algorithm.contains(QStringLiteral("text")))
                text->setText(algorithm.value(QStringLiteral("text")).toString());
            if (algorithm.contains(QStringLiteral("font")))
            {
                // Only the four exposed attributes change; everything else
                // the file's QFont carried (weight, stretch, spacing, ...)
                // is preserved.
                QJsonObject f = algorithm.value(QStringLiteral("font")).toObject();
                QFont font = text->font();
                if (f.contains(QStringLiteral("family")))
                    font.setFamily(f.value(QStringLiteral("family")).toString());
                if (f.contains(QStringLiteral("pointSize")) && f.value(QStringLiteral("pointSize")).toInt() > 0)
                    font.setPointSize(f.value(QStringLiteral("pointSize")).toInt());
                if (f.contains(QStringLiteral("bold")))
                    font.setBold(f.value(QStringLiteral("bold")).toBool());
                if (f.contains(QStringLiteral("italic")))
                    font.setItalic(f.value(QStringLiteral("italic")).toBool());
                text->setFont(font);
            }
            if (algorithm.contains(QStringLiteral("animationStyle")))
            {
                RGBText::AnimationStyle style;
                if (textAnimationFromString(algorithm.value(QStringLiteral("animationStyle")).toString(), style))
                    text->setAnimationStyle(style);
            }
            if (algorithm.contains(QStringLiteral("xOffset")))
                text->setXOffset(algorithm.value(QStringLiteral("xOffset")).toInt());
            if (algorithm.contains(QStringLiteral("yOffset")))
                text->setYOffset(algorithm.value(QStringLiteral("yOffset")).toInt());
        }
        else if (algo != nullptr && algo->type() == RGBAlgorithm::Image)
        {
            RGBImage *image = static_cast<RGBImage *>(algo);
            if (algorithm.contains(QStringLiteral("imagePath")))
            {
                QString path = algorithm.value(QStringLiteral("imagePath")).toString();
                if (path.startsWith(QStringLiteral("file:")))
                    path = QUrl(path).toLocalFile();
                if (image->filename() != path)
                    image->setFilename(path);
            }
            if (algorithm.contains(QStringLiteral("animationStyle")))
            {
                RGBImage::AnimationStyle style;
                if (imageAnimationFromString(algorithm.value(QStringLiteral("animationStyle")).toString(), style))
                    image->setAnimationStyle(style);
            }
            if (algorithm.contains(QStringLiteral("xOffset")))
                image->setXOffset(algorithm.value(QStringLiteral("xOffset")).toInt());
            if (algorithm.contains(QStringLiteral("yOffset")))
                image->setYOffset(algorithm.value(QStringLiteral("yOffset")).toInt());
        }
    }

    // Colours last: RGBMatrix::setAlgorithm() and ::setProperty() both read
    // colours back from a script, so anything applied earlier would be lost.
    if (hasColors)
    {
        for (int i = 0; i < colors.count(); i++)
        {
            if (matrix->getColor(i) != colors.at(i))
                matrix->setColor(i, colors.at(i));
        }
    }

    if (hasControlMode)
        matrix->setControlMode(controlMode);
    if (hasBlendMode)
        matrix->setBlendMode(blendMode);
    if (config.contains(QStringLiteral("dimmerControl")))
        matrix->setDimmerControl(config.value(QStringLiteral("dimmerControl")).toBool());

    code.clear();
    return QString();
}

void ApiRgbMatrixDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("functions.rgbmatrix.listAlgorithms"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonArray algorithms;
        for (const QString &name : RGBAlgorithm::algorithms(doc))
        {
            QScopedPointer<RGBAlgorithm> algo(RGBAlgorithm::algorithm(doc, name));
            if (algo.isNull())
                continue;
            QJsonObject obj;
            obj.insert(QStringLiteral("name"), name);
            obj.insert(QStringLiteral("type"), algorithmTypeToString(algo->type()));
            obj.insert(QStringLiteral("acceptedColors"), algo->acceptColors());
            if (algo->type() == RGBAlgorithm::Script)
            {
                obj.insert(QStringLiteral("apiVersion"), algo->apiVersion());
                obj.insert(QStringLiteral("author"), algo->author());
            }
            algorithms.append(obj);
        }
        QJsonObject result;
        result.insert(QStringLiteral("algorithms"), algorithms);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("functions.rgbmatrix.getScriptProperties"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QString scriptName = params.value(QStringLiteral("scriptName")).toString();
        if (doc->rgbScriptsCache()->names().contains(scriptName) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("Unknown RGB script '%1'").arg(scriptName)));
            return;
        }
        QScopedPointer<RGBScript> script(doc->rgbScriptsCache()->script(scriptName));
        QJsonObject result;
        result.insert(QStringLiteral("properties"), scriptPropertyDefsToJson(script.data()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("functions.rgbmatrix.getPreview"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        RGBMatrix *matrix = findMatrixOrRespond(doc, params, session, id);
        if (matrix == nullptr)
            return;

        int step = params.value(QStringLiteral("step")).toInt();
        QColor startColor = matrix->getColor(0);
        QColor endColor = matrix->getColor(1);
        QSize size(0, 0);
        int stepsCount = 0;
        RGBAlgorithm *algo = nullptr;
        {
            QMutexLocker locker(&matrix->algorithmMutex());
            algo = matrix->algorithm();
            FixtureGroup *grp = doc->fixtureGroup(matrix->fixtureGroup());
            if (grp != nullptr)
                size = grp->size();
            // Fresh, not RGBMatrix::stepsCount(): that cache only refreshes on
            // algorithm/group/property changes, not when the group grows.
            if (algo != nullptr && grp != nullptr)
                stepsCount = algo->rgbMapStepCount(size);
        }

        QJsonArray pixels;
        if (stepsCount <= 0)
            step = 0; // nothing to render: result.step is always a normalised index
        else
        {
            step = ((step % stepsCount) + stepsCount) % stepsCount;

            // A private step handler: the function's own one belongs to
            // MasterTimer while it runs. Same colour maths as
            // RGBMatrixEditor::initPreviewData()/slotPreviewTimeout().
            RGBMatrixStep handler;
            handler.setStepColor(startColor);
            handler.calculateColorDelta(startColor, endColor, algo);
            handler.updateStepColor(step, startColor, stepsCount);
            matrix->previewMap(step, &handler);

            for (int y = 0; y < handler.m_map.size(); y++)
            {
                QJsonArray row;
                for (int x = 0; x < handler.m_map[y].size(); x++)
                    row.append(int(handler.m_map[y][x] & 0x00FFFFFF));
                pixels.append(row);
            }
        }

        QJsonObject result;
        result.insert(QStringLiteral("stepsCount"), stepsCount);
        result.insert(QStringLiteral("step"), step);
        result.insert(QStringLiteral("width"), size.width());
        result.insert(QStringLiteral("height"), size.height());
        result.insert(QStringLiteral("pixels"), pixels);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("functions.rgbmatrix.setConfig"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        RGBMatrix *matrix = findMatrixOrRespond(doc, params, session, id);
        if (matrix == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;
        if (params.value(QStringLiteral("config")).isObject() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("config must be an object")));
            return;
        }

        QString code;
        QString error = applyConfig(matrix, params.value(QStringLiteral("config")).toObject(), code);
        if (error.isEmpty() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, code, error));
            return;
        }

        // Most RGBMatrix setters emit changed(), which Doc turns into
        // setModified() (one revision bump each), but the Text/Image
        // parameter setters are plain C++ objects with no signal at all -
        // the explicit bump guarantees every successful call advances the
        // revision. Like functions.scene.setValues, one call may therefore
        // advance it by more than one; response and event carry the final value.
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(matrix->id()));
        data.insert(QStringLiteral("config"), configToJson(doc, matrix));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.rgbmatrix.configChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.rgbmatrix.setScriptProperty"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        RGBMatrix *matrix = findMatrixOrRespond(doc, params, session, id);
        if (matrix == nullptr)
            return;
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        QString name = params.value(QStringLiteral("propertyName")).toString();
        QString value = params.value(QStringLiteral("value")).toVariant().toString();
        {
            QMutexLocker locker(&matrix->algorithmMutex());
            RGBAlgorithm *algo = matrix->algorithm();
            if (algo == nullptr || algo->type() != RGBAlgorithm::Script)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("The current algorithm is not an RGB script")));
                return;
            }
            bool known = false;
            for (const RGBScriptProperty &prop : static_cast<RGBScript *>(algo)->properties())
                known = known || prop.m_name == name;
            if (known == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("Script '%1' has no property '%2'").arg(algo->name(), name)));
                return;
            }
        }

        matrix->setProperty(name, value);
        doc->setModified();
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(matrix->id()));
        data.insert(QStringLiteral("propertyName"), name);
        data.insert(QStringLiteral("value"), matrix->property(name));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.rgbmatrix.scriptPropertyChanged"), data, session->clientId(), false);
    });
}
