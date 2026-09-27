/*
  Q Light Controller Plus - Control API
  apifixturechannelsdomain.cpp

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

#include <QXmlStreamReader>
#include <QJsonArray>
#include <QJsonValue>
#include <QColor>
#include <QFile>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

#include "apifixturechannelsdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "qlcmodifierscache.h"
#include "channelmodifier.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "qlcchannel.h"
#include "qlcconfig.h"
#include "qlcfile.h"
#include "fixture.h"
#include "doc.h"

namespace {

QString modifierFileName(const QString &name)
{
    return QStringLiteral("%1/%2%3").arg(QLCModifiersCache::userTemplateDirectory().absolutePath(),
                                         name, KExtModifierTemplate);
}

/** Path of the user template file holding $name: "<name>.qxmt" (how both
 *  the Qt UI and this domain save them), else any user file whose <Name>
 *  is $name (a template copied in by hand under another file name). */
QString findUserModifierFile(const QString &name)
{
    QString direct = modifierFileName(name);
    if (QFile::exists(direct))
        return direct;

    QDir dir = QLCModifiersCache::userTemplateDirectory();
    for (const QString &entry : dir.entryList())
    {
        QString path = dir.absoluteFilePath(entry);
        ChannelModifier probe;
        if (probe.loadXML(path, ChannelModifier::UserTemplate) == QFile::NoError && probe.name() == name)
            return path;
    }
    return QString();
}

/** A template name becomes a file name: refuse anything that is not one. */
QString validateTemplateName(const QString &name)
{
    if (name.isEmpty())
        return QStringLiteral("The modifier name cannot be empty");
    if (name.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("\"None\" is reserved (it means no modifier)");
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (const QChar &c : name)
        if (forbidden.contains(c) || c.unicode() < 32)
            return QStringLiteral("The modifier name cannot contain / \\ : * ? \" < > |");
    return QString();
}

QJsonObject colorToJson(const QColor &rgb, const QColor &wauv, const QString &name)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), name);
    if (rgb.isValid())
        obj.insert(QStringLiteral("rgb"), rgb.name());
    if (wauv.isValid())
    {
        // ColorFilters stores white/amber/UV as the red/green/blue of a QColor.
        obj.insert(QStringLiteral("white"), wauv.red());
        obj.insert(QStringLiteral("amber"), wauv.green());
        obj.insert(QStringLiteral("uv"), wauv.blue());
    }
    return obj;
}

/** qmlui/colorfilters.cpp's ColorFilters::loadXML(), read-only. */
bool loadColorFilterFile(const QString &path, QJsonObject &out)
{
    QXmlStreamReader *doc = QLCFile::getXMLReader(path);
    if (doc == nullptr || doc->device() == nullptr || doc->hasError())
        return false;

    while (!doc->atEnd())
    {
        if (doc->readNext() == QXmlStreamReader::DTD)
            break;
    }
    bool ok = false;
    QString title;
    QJsonArray filters;
    if (doc->hasError() == false && doc->dtdName() == QStringLiteral("ColorFilters") &&
        doc->readNextStartElement() && doc->name() == QStringLiteral("ColorFilters"))
    {
        ok = true;
        while (doc->readNextStartElement())
        {
            if (doc->name() == QStringLiteral("Color"))
            {
                QXmlStreamAttributes attrs = doc->attributes();
                QString name = attrs.value(QStringLiteral("Name")).toString();
                QColor rgb, wauv;
                if (attrs.hasAttribute(QStringLiteral("RGB")))
                    rgb = QColor(attrs.value(QStringLiteral("RGB")).toString());
                if (attrs.hasAttribute(QStringLiteral("WAUV")))
                    wauv = QColor(attrs.value(QStringLiteral("WAUV")).toString());
                if (name.isEmpty() == false && (rgb.isValid() || wauv.isValid()))
                    filters.append(colorToJson(rgb, wauv, name));
                doc->skipCurrentElement();
            }
            else if (doc->name() == QStringLiteral("Name"))
            {
                title = doc->readElementText();
            }
            else
            {
                doc->skipCurrentElement();
            }
        }
    }
    QLCFile::releaseXMLReader(doc);
    if (ok == false)
        return false;

    out.insert(QStringLiteral("name"), title.isEmpty() ? QFileInfo(path).completeBaseName() : title);
    out.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
    out.insert(QStringLiteral("filters"), filters);
    return true;
}

bool conflict(Doc *doc, ApiSession *session, const QString &id, const QJsonObject &params)
{
    quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
    if (baseRevision == doc->docRevision())
        return false;
    QJsonObject details;
    details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                    QStringLiteral("baseRevision is stale"), details));
    return true;
}

} // namespace

ApiFixtureChannelsDomain::ApiFixtureChannelsDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
    , m_modifiersRevision(0)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods();
}

ApiFixtureChannelsDomain::~ApiFixtureChannelsDomain()
{
    qDeleteAll(m_retiredModifiers);
}

void ApiFixtureChannelsDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    /*********************************************************************
     * fixtures.channel.setBehaviour
     *********************************************************************/

    // {fixtureId, channels: [int] | channel: int, canFade?, precedence?
    //  ("auto"|"htp"|"ltp"), modifier? (name, or null/"" for none),
    //  applyToSameType?, baseRevision} -> {docRevision, fixtureIds}
    dispatcher->registerMethod(QStringLiteral("fixtures.channel.setBehaviour"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (conflict(doc, session, id, params))
            return;

        bool ok = false;
        quint32 fixtureId = params.value(QStringLiteral("fixtureId")).toString().toUInt(&ok);
        Fixture *fixture = ok ? doc->fixture(fixtureId) : nullptr;
        if (fixture == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such fixture")));
            return;
        }

        QList<int> channels;
        if (params.contains(QStringLiteral("channels")))
        {
            for (const QJsonValue &v : params.value(QStringLiteral("channels")).toArray())
                channels.append(v.toInt(-1));
        }
        else if (params.contains(QStringLiteral("channel")))
        {
            channels.append(params.value(QStringLiteral("channel")).toInt(-1));
        }
        if (channels.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("channel or channels must be given")));
            return;
        }

        bool hasCanFade = params.contains(QStringLiteral("canFade"));
        bool hasPrecedence = params.contains(QStringLiteral("precedence"));
        bool hasModifier = params.contains(QStringLiteral("modifier"));
        if (hasCanFade == false && hasPrecedence == false && hasModifier == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("At least one of canFade/precedence/modifier must be given")));
            return;
        }
        QString precedence = params.value(QStringLiteral("precedence")).toString();
        if (hasPrecedence && precedence != QStringLiteral("auto") && precedence != QStringLiteral("htp") &&
            precedence != QStringLiteral("ltp"))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("precedence must be auto, htp or ltp")));
            return;
        }

        for (int idx : std::as_const(channels))
        {
            const QLCChannel *ch = idx >= 0 ? fixture->channel(quint32(idx)) : nullptr;
            if (ch == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("No channel %1 in this fixture").arg(idx)));
                return;
            }
            // FixtureManager::setItemRoleData("precedence"): intensity
            // channels are HTP by nature and can only be forced LTP, every
            // other channel is LTP by nature and can only be forced HTP.
            bool intensity = ch->group() == QLCChannel::Intensity;
            if (hasPrecedence && ((precedence == QStringLiteral("htp") && intensity) ||
                                  (precedence == QStringLiteral("ltp") && intensity == false)))
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("Channel %1 (%2) is already %3 by nature; only %4 can be forced")
                        .arg(idx + 1).arg(ch->name())
                        .arg(intensity ? QStringLiteral("HTP") : QStringLiteral("LTP"))
                        .arg(intensity ? QStringLiteral("LTP") : QStringLiteral("HTP"))));
                return;
            }
        }

        ChannelModifier *modifier = nullptr;
        QString modifierName;
        if (hasModifier)
        {
            modifierName = params.value(QStringLiteral("modifier")).toString();
            if (modifierName.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0)
                modifierName.clear();
            if (modifierName.isEmpty() == false)
            {
                modifier = doc->modifiersCache()->modifier(modifierName);
                if (modifier == nullptr)
                {
                    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                        QStringLiteral("No channel modifier template \"%1\"").arg(modifierName)));
                    return;
                }
            }
        }
        bool canFade = params.value(QStringLiteral("canFade")).toBool(true);

        // "Apply changes to fixtures of the same type": same definition AND
        // same mode instance, exactly FixtureManager's own test.
        QList<Fixture *> targets;
        if (params.value(QStringLiteral("applyToSameType")).toBool(false) &&
            fixture->fixtureDef() != nullptr && fixture->fixtureMode() != nullptr)
        {
            for (Fixture *other : doc->fixtures())
            {
                if (other->fixtureDef() == fixture->fixtureDef() && other->fixtureMode() == fixture->fixtureMode())
                    targets.append(other);
            }
        }
        else
        {
            targets.append(fixture);
        }

        QJsonArray fixtureIds;
        for (Fixture *target : std::as_const(targets))
        {
            QList<int> forcedHTP = target->forcedHTPChannels();
            QList<int> forcedLTP = target->forcedLTPChannels();
            for (int idx : std::as_const(channels))
            {
                if (hasCanFade)
                    target->setChannelCanFade(idx, canFade);
                if (hasPrecedence)
                {
                    forcedHTP.removeAll(idx);
                    forcedLTP.removeAll(idx);
                    if (precedence == QStringLiteral("htp"))
                        forcedHTP.append(idx);
                    else if (precedence == QStringLiteral("ltp"))
                        forcedLTP.append(idx);
                }
                if (hasModifier)
                    target->setChannelModifier(quint32(idx), modifier);
            }
            std::sort(forcedHTP.begin(), forcedHTP.end());
            std::sort(forcedLTP.begin(), forcedLTP.end());
            // Sets the forced lists and pushes HTP/LTP, default value and
            // modifier of every channel into the fixture's universe, so the
            // change is audible immediately (as FixtureManager does by hand).
            doc->updateFixtureChannelCapabilities(target->id(), forcedHTP, forcedLTP);
            fixtureIds.append(QString::number(target->id()));
        }
        doc->setModified();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("fixtureIds"), fixtureIds);
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        data.insert(QStringLiteral("fixtureIds"), fixtureIds);
        QJsonArray channelsJson;
        for (int idx : std::as_const(channels))
            channelsJson.append(idx);
        data.insert(QStringLiteral("channels"), channelsJson);
        if (hasCanFade)
            data.insert(QStringLiteral("canFade"), canFade);
        if (hasPrecedence)
            data.insert(QStringLiteral("precedence"), precedence);
        if (hasModifier)
            data.insert(QStringLiteral("modifier"), modifierName.isEmpty() ? QJsonValue() : QJsonValue(modifierName));
        m_server->broadcast(QStringLiteral("fixtures.channel.behaviourChanged"), data, session->clientId(), false);
    });

    /*********************************************************************
     * fixtures.modifiers.* (channel modifier template library, §4c)
     *********************************************************************/

    auto modifiersRevisionAccepted = [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (params.contains(QStringLiteral("baseRevision")) == false)
            return true;
        if (quint32(params.value(QStringLiteral("baseRevision")).toInt()) == m_modifiersRevision)
            return true;
        QJsonObject details;
        details.insert(QStringLiteral("modifiersRevision"), int(m_modifiersRevision));
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                        QStringLiteral("baseRevision is stale (modifiersRevision)"), details));
        return false;
    };

    auto broadcastChanged = [this](ApiSession *session, const QString &action, const QString &name, const QString &newName)
    {
        QJsonObject data;
        data.insert(QStringLiteral("modifiersRevision"), int(m_modifiersRevision));
        data.insert(QStringLiteral("action"), action);
        data.insert(QStringLiteral("name"), name);
        if (newName.isEmpty() == false)
            data.insert(QStringLiteral("newName"), newName);
        data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.modifiers.changed"), data, session->clientId(), false);
    };

    // {} -> {templates: [{name, isUser}], modifiersRevision}
    dispatcher->registerMethod(QStringLiteral("fixtures.modifiers.list"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QStringList names = doc->modifiersCache()->templateNames();
        std::sort(names.begin(), names.end(), [](const QString &a, const QString &b)
                  { return a.compare(b, Qt::CaseInsensitive) < 0; });
        QJsonArray templates;
        for (const QString &name : std::as_const(names))
        {
            ChannelModifier *mod = doc->modifiersCache()->modifier(name);
            QJsonObject t;
            t.insert(QStringLiteral("name"), name);
            t.insert(QStringLiteral("isUser"), mod != nullptr && mod->type() == ChannelModifier::UserTemplate);
            templates.append(t);
        }
        QJsonObject result;
        result.insert(QStringLiteral("templates"), templates);
        result.insert(QStringLiteral("modifiersRevision"), int(m_modifiersRevision));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // {name} -> {name, isUser, points: [{original, modified}], modifiersRevision}
    dispatcher->registerMethod(QStringLiteral("fixtures.modifiers.get"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QString name = params.value(QStringLiteral("name")).toString();
        ChannelModifier *mod = doc->modifiersCache()->modifier(name);
        if (mod == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No channel modifier template \"%1\"").arg(name)));
            return;
        }
        QJsonArray points;
        for (const QPair<uchar, uchar> &p : mod->modifierMap())
        {
            QJsonObject pt;
            pt.insert(QStringLiteral("original"), int(p.first));
            pt.insert(QStringLiteral("modified"), int(p.second));
            points.append(pt);
        }
        QJsonObject result;
        result.insert(QStringLiteral("name"), mod->name());
        result.insert(QStringLiteral("isUser"), mod->type() == ChannelModifier::UserTemplate);
        result.insert(QStringLiteral("points"), points);
        result.insert(QStringLiteral("modifiersRevision"), int(m_modifiersRevision));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // {name, points: [{original, modified}], baseRevision?}
    //   -> {modifiersRevision, name, created}
    // Upsert of a USER template (FixtureManager::saveChannelModifier()):
    // written to the user modifiers folder, the cached instance updated in
    // place so every channel already using it follows at once.
    dispatcher->registerMethod(QStringLiteral("fixtures.modifiers.save"), [doc, this, modifiersRevisionAccepted, broadcastChanged](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (modifiersRevisionAccepted(session, id, params) == false)
            return;
        QString name = params.value(QStringLiteral("name")).toString().simplified();
        QString nameError = validateTemplateName(name);
        if (nameError.isEmpty() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, nameError));
            return;
        }
        ChannelModifier *existing = doc->modifiersCache()->modifier(name);
        if (existing != nullptr && existing->type() == ChannelModifier::SystemTemplate)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("\"%1\" is a system template and cannot be overwritten; choose another name").arg(name)));
            return;
        }

        QJsonArray pointsJson = params.value(QStringLiteral("points")).toArray();
        QList<QPair<uchar, uchar>> map;
        int prevOriginal = -1;
        for (const QJsonValue &v : pointsJson)
        {
            QJsonObject pt = v.toObject();
            int orig = pt.value(QStringLiteral("original")).toInt(-1);
            int mod = pt.value(QStringLiteral("modified")).toInt(-1);
            if (orig < 0 || orig > 255 || mod < 0 || mod > 255 || orig < prevOriginal)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("points must be {original, modified} pairs of 0-255 with non-decreasing originals")));
                return;
            }
            prevOriginal = orig;
            map.append(qMakePair(uchar(orig), uchar(mod)));
        }
        if (map.count() < 2 || map.first().first != 0 || map.last().first != 255)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("points must start at original 0 and end at original 255")));
            return;
        }

        ChannelModifier *fresh = new ChannelModifier();
        fresh->setName(name);
        fresh->setType(ChannelModifier::UserTemplate);
        fresh->setModifierMap(map);
        QString path = existing != nullptr ? findUserModifierFile(name) : QString();
        if (path.isEmpty())
            path = modifierFileName(name);
        QFile::FileError err = fresh->saveXML(path);
        if (err != QFile::NoError)
        {
            delete fresh;
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                QStringLiteral("Could not write %1 (%2)").arg(path, QLCFile::errorString(err))));
            return;
        }

        bool created = existing == nullptr;
        if (created)
            doc->modifiersCache()->addModifier(fresh);
        else
        {
            existing->setModifierMap(map);
            delete fresh;
        }
        m_modifiersRevision++;

        QJsonObject result;
        result.insert(QStringLiteral("modifiersRevision"), int(m_modifiersRevision));
        result.insert(QStringLiteral("name"), name);
        result.insert(QStringLiteral("created"), created);
        session->send(ApiEnvelope::buildOkResponse(id, result));
        broadcastChanged(session, QStringLiteral("saved"), name, QString());
    });

    // {name, newName, baseRevision?} -> {modifiersRevision, docRevision}
    // User templates only. The .qxw stores a channel's modifier BY NAME, so
    // a rename that touches patched fixtures is also a document change.
    dispatcher->registerMethod(QStringLiteral("fixtures.modifiers.rename"), [doc, this, modifiersRevisionAccepted, broadcastChanged](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (modifiersRevisionAccepted(session, id, params) == false)
            return;
        QString name = params.value(QStringLiteral("name")).toString();
        QString newName = params.value(QStringLiteral("newName")).toString().simplified();
        ChannelModifier *mod = doc->modifiersCache()->modifier(name);
        if (mod == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No channel modifier template \"%1\"").arg(name)));
            return;
        }
        if (mod->type() != ChannelModifier::UserTemplate)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("System templates cannot be renamed")));
            return;
        }
        QString nameError = validateTemplateName(newName);
        if (nameError.isEmpty() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, nameError));
            return;
        }
        if (newName == name)
        {
            QJsonObject result;
            result.insert(QStringLiteral("modifiersRevision"), int(m_modifiersRevision));
            result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildOkResponse(id, result));
            return;
        }
        if (doc->modifiersCache()->modifier(newName) != nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("A template named \"%1\" already exists").arg(newName)));
            return;
        }

        // New file first, old file removed only once that worked.
        QString oldPath = findUserModifierFile(name);
        ChannelModifier renamed;
        renamed.setName(newName);
        renamed.setModifierMap(mod->modifierMap());
        QString newPath = modifierFileName(newName);
        QFile::FileError err = renamed.saveXML(newPath);
        if (err != QFile::NoError)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                QStringLiteral("Could not write %1 (%2)").arg(newPath, QLCFile::errorString(err))));
            return;
        }
        if (oldPath.isEmpty() == false && QFileInfo(oldPath) != QFileInfo(newPath))
            QFile::remove(oldPath);
        doc->modifiersCache()->renameModifier(name, newName);

        bool referenced = false;
        for (Fixture *fixture : doc->fixtures())
            for (quint32 i = 0; i < fixture->channels() && referenced == false; i++)
                if (fixture->channelModifier(i) == mod)
                    referenced = true;
        if (referenced)
            doc->setModified();
        m_modifiersRevision++;

        QJsonObject result;
        result.insert(QStringLiteral("modifiersRevision"), int(m_modifiersRevision));
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
        broadcastChanged(session, QStringLiteral("renamed"), name, newName);
    });

    // {name, baseRevision?} -> {modifiersRevision, docRevision, detachedFixtureIds}
    // User templates only: the file is deleted and every channel using the
    // template loses its modifier (a document change when there were any).
    dispatcher->registerMethod(QStringLiteral("fixtures.modifiers.delete"), [doc, this, modifiersRevisionAccepted, broadcastChanged](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (modifiersRevisionAccepted(session, id, params) == false)
            return;
        QString name = params.value(QStringLiteral("name")).toString();
        ChannelModifier *mod = doc->modifiersCache()->modifier(name);
        if (mod == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No channel modifier template \"%1\"").arg(name)));
            return;
        }
        if (mod->type() != ChannelModifier::UserTemplate)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("System templates cannot be deleted")));
            return;
        }
        QString path = findUserModifierFile(name);
        if (path.isEmpty() == false && QFile::remove(path) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                            QStringLiteral("Could not delete %1").arg(path)));
            return;
        }

        // Detach it from every channel (fixture AND universe) before it
        // leaves the cache; the instance itself is retired, not freed.
        QJsonArray detached;
        for (Fixture *fixture : doc->fixtures())
        {
            bool touched = false;
            for (quint32 i = 0; i < fixture->channels(); i++)
            {
                if (fixture->channelModifier(i) == mod)
                {
                    fixture->setChannelModifier(i, nullptr);
                    touched = true;
                }
            }
            if (touched)
            {
                doc->updateFixtureChannelCapabilities(fixture->id(), fixture->forcedHTPChannels(),
                                                      fixture->forcedLTPChannels());
                detached.append(QString::number(fixture->id()));
            }
        }
        m_retiredModifiers.append(doc->modifiersCache()->takeModifier(name));
        if (detached.isEmpty() == false)
            doc->setModified();
        m_modifiersRevision++;

        QJsonObject result;
        result.insert(QStringLiteral("modifiersRevision"), int(m_modifiersRevision));
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("detachedFixtureIds"), detached);
        session->send(ApiEnvelope::buildOkResponse(id, result));
        broadcastChanged(session, QStringLiteral("deleted"), name, QString());
    });

    /*********************************************************************
     * fixtures.colorFilters.list
     *********************************************************************/

    // {} -> {files: [{name, fileName, isUser, filters: [{name, rgb?, white?, amber?, uv?}]}]}
    dispatcher->registerMethod(QStringLiteral("fixtures.colorFilters.list"), [](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QStringList filters;
        filters << QStringLiteral("*%1").arg(KExtColorFilters);
        QList<QPair<QDir, bool>> dirs;
        dirs.append(qMakePair(QLCFile::systemDirectory(QString(COLORFILTERSDIR), QString(KExtColorFilters)), false));
        dirs.append(qMakePair(QLCFile::userDirectory(QString(USERCOLORFILTERSDIR), QString(COLORFILTERSDIR), filters), true));

        QJsonArray files;
        for (const QPair<QDir, bool> &entry : std::as_const(dirs))
        {
            QDir dir = entry.first;
            if (dir.exists() == false)
                continue;
            dir.setFilter(QDir::Files);
            dir.setNameFilters(filters);
            for (const QString &fileName : dir.entryList())
            {
                QJsonObject file;
                if (loadColorFilterFile(dir.absoluteFilePath(fileName), file) == false)
                    continue;
                file.insert(QStringLiteral("isUser"), entry.second);
                files.append(file);
            }
        }
        QJsonObject result;
        result.insert(QStringLiteral("files"), files);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}
