/*
  Q Light Controller Plus - Control API
  apiimportdomain.cpp

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
#include <QFileInfo>
#include <QSet>
#include <algorithm>

#include "apiimportdomain.h"
#include "apidocchanges.h"
#include "apienvelope.h"
#include "apisession.h"
#include "apiserver.h"

#include "monitorproperties.h"
#include "projectimporter.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "fixturegroup.h"
#include "qlcpalette.h"
#include "function.h"
#include "fixture.h"
#include "doc.h"

namespace {

QJsonArray idList(QList<quint32> ids)
{
    std::sort(ids.begin(), ids.end());
    QJsonArray out;
    for (quint32 id : ids)
        out.append(QString::number(id));
    return out;
}

QJsonObject remapToJson(const QMap<quint32, quint32> &map)
{
    QJsonObject out;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        out.insert(QString::number(it.key()), QString::number(it.value()));
    return out;
}

/** Parse an array of string (or numeric) ids. Returns false on a malformed entry. */
bool parseIds(const QJsonValue &value, QList<quint32> &out, QString *bad)
{
    for (const QJsonValue &v : value.toArray())
    {
        bool ok = false;
        quint32 id = 0;
        if (v.isString())
            id = v.toString().toUInt(&ok);
        else if (v.isDouble() && v.toDouble() >= 0)
        {
            id = quint32(v.toDouble());
            ok = true;
        }
        if (ok == false)
        {
            *bad = v.toVariant().toString();
            return false;
        }
        out.append(id);
    }
    return true;
}

} // namespace

ApiImportDomain::ApiImportDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    registerMethods();
}

bool ApiImportDomain::loadSource(ProjectImporter &importer, const QJsonObject &params,
                                 QString *errorCode, QString *error) const
{
    const QString source = params.value(QStringLiteral("source")).toString();
    if (source == QLatin1String("path"))
    {
        const QString path = params.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
        {
            *errorCode = ApiEnvelope::ErrInvalidParams;
            *error = QStringLiteral("path is required when source=path");
            return false;
        }
        if (QFileInfo(path).isFile() == false)
        {
            *errorCode = ApiEnvelope::ErrNotFound;
            *error = QStringLiteral("Project file not found: ") + path;
            return false;
        }
        if (importer.loadFile(path) == false)
        {
            *errorCode = ApiEnvelope::ErrInvalidParams;
            *error = QStringLiteral("Not a QLC+ workspace: ") + path;
            return false;
        }
        return true;
    }

    if (source == QLatin1String("upload"))
    {
        const QByteArray xml = QByteArray::fromBase64(params.value(QStringLiteral("contentBase64")).toString().toLatin1());
        if (xml.isEmpty())
        {
            *errorCode = ApiEnvelope::ErrInvalidParams;
            *error = QStringLiteral("contentBase64 is required when source=upload");
            return false;
        }
        // relative media paths of an uploaded project can only resolve against this one's folder
        if (importer.loadData(xml, m_doc->workspacePath()) == false)
        {
            *errorCode = ApiEnvelope::ErrInvalidParams;
            *error = QStringLiteral("The uploaded file is not a QLC+ workspace");
            return false;
        }
        return true;
    }

    *errorCode = ApiEnvelope::ErrInvalidParams;
    *error = QStringLiteral("source must be \"path\" or \"upload\"");
    return false;
}

QJsonObject ApiImportDomain::contentsToJson(ProjectImporter &importer) const
{
    Doc *src = importer.sourceDoc();
    MonitorProperties *monProps = src->monitorProperties();

    QSet<QString> existingNames;
    for (Fixture *fxi : m_doc->fixtures())
        existingNames.insert(fxi->name());

    QList<quint32> universes;
    QJsonArray fixtures;
    QList<Fixture *> sorted = src->fixtures();
    std::sort(sorted.begin(), sorted.end(), [](Fixture *a, Fixture *b) { return a->id() < b->id(); });
    for (Fixture *fxi : sorted)
    {
        if (universes.contains(fxi->universe()) == false)
            universes.append(fxi->universe());

        QJsonObject f;
        f.insert(QStringLiteral("id"), QString::number(fxi->id()));
        f.insert(QStringLiteral("name"), fxi->name());
        f.insert(QStringLiteral("universe"), int(fxi->universe()));
        f.insert(QStringLiteral("address"), int(fxi->address()));
        f.insert(QStringLiteral("channels"), int(fxi->channels()));
        f.insert(QStringLiteral("manufacturer"), fxi->fixtureDef() ? fxi->fixtureDef()->manufacturer() : QString());
        f.insert(QStringLiteral("model"), fxi->fixtureDef() ? fxi->fixtureDef()->model() : QString());
        f.insert(QStringLiteral("mode"), fxi->fixtureMode() ? fxi->fixtureMode()->name() : QString());
        f.insert(QStringLiteral("existsInProject"), existingNames.contains(fxi->name()));

        QJsonArray linked;
        for (quint32 subID : monProps->fixtureIDList(fxi->id()))
        {
            quint16 linkedIndex = monProps->fixtureLinkedIndex(subID);
            if (subID == 0 || linkedIndex == 0)
                continue;
            quint16 headIndex = monProps->fixtureHeadIndex(subID);
            QJsonObject l;
            l.insert(QStringLiteral("headIndex"), headIndex);
            l.insert(QStringLiteral("linkedIndex"), linkedIndex);
            l.insert(QStringLiteral("name"), monProps->fixtureName(fxi->id(), headIndex, linkedIndex));
            linked.append(l);
        }
        f.insert(QStringLiteral("linked"), linked);
        fixtures.append(f);
    }

    std::sort(universes.begin(), universes.end());
    QJsonArray universesJson;
    for (quint32 u : universes)
    {
        QJsonObject o;
        o.insert(QStringLiteral("universe"), int(u));
        o.insert(QStringLiteral("name"), QStringLiteral("Universe %1").arg(u + 1));
        universesJson.append(o);
    }

    QJsonArray groups;
    for (FixtureGroup *grp : src->fixtureGroups())
    {
        QJsonObject g;
        g.insert(QStringLiteral("id"), QString::number(grp->id()));
        g.insert(QStringLiteral("name"), grp->name());
        g.insert(QStringLiteral("fixtureIds"), idList(grp->fixtureList()));
        groups.append(g);
    }

    QJsonArray palettes;
    for (QLCPalette *p : src->palettes())
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QString::number(p->id()));
        o.insert(QStringLiteral("name"), p->name());
        o.insert(QStringLiteral("type"), QLCPalette::typeToString(p->type()));
        palettes.append(o);
    }

    QJsonArray functions;
    QList<Function *> funcs = src->functions();
    std::sort(funcs.begin(), funcs.end(), [](Function *a, Function *b) { return a->id() < b->id(); });
    for (Function *func : funcs)
    {
        if (func->isVisible() == false)
            continue;

        QList<quint32> fx, grp, pal, fn;
        importer.functionDependencies(func->id(), fx, grp, pal, fn);
        QJsonObject deps;
        deps.insert(QStringLiteral("fixtureIds"), idList(fx));
        deps.insert(QStringLiteral("fixtureGroupIds"), idList(grp));
        deps.insert(QStringLiteral("paletteIds"), idList(pal));
        deps.insert(QStringLiteral("functionIds"), idList(fn));

        QJsonObject o;
        o.insert(QStringLiteral("id"), QString::number(func->id()));
        o.insert(QStringLiteral("name"), func->name());
        o.insert(QStringLiteral("type"), Function::typeToString(func->type()));
        o.insert(QStringLiteral("path"), func->path(true));
        o.insert(QStringLiteral("dependencies"), deps);
        functions.append(o);
    }

    QJsonObject result;
    result.insert(QStringLiteral("universes"), universesJson);
    result.insert(QStringLiteral("fixtures"), fixtures);
    result.insert(QStringLiteral("fixtureGroups"), groups);
    result.insert(QStringLiteral("palettes"), palettes);
    result.insert(QStringLiteral("functions"), functions);
    return result;
}

void ApiImportDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("core.project.importList"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ProjectImporter importer(m_doc);
        QString code, error;
        if (loadSource(importer, params, &code, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, code, error));
            return;
        }

        QJsonObject result = contentsToJson(importer);
        if (params.contains(QStringLiteral("fileName")))
            result.insert(QStringLiteral("fileName"), params.value(QStringLiteral("fileName")).toString());
        else if (params.contains(QStringLiteral("path")))
            result.insert(QStringLiteral("fileName"), QFileInfo(params.value(QStringLiteral("path")).toString()).fileName());
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("core.project.import"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        ProjectImporter importer(doc);
        QString code, error;
        if (loadSource(importer, params, &code, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, code, error));
            return;
        }

        QList<quint32> fixtureIds, groupIds, functionIds;
        QString bad;
        if (parseIds(params.value(QStringLiteral("fixtureIds")), fixtureIds, &bad) == false ||
            parseIds(params.value(QStringLiteral("fixtureGroupIds")), groupIds, &bad) == false ||
            parseIds(params.value(QStringLiteral("functionIds")), functionIds, &bad) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Malformed id: ") + bad));
            return;
        }

        Doc *src = importer.sourceDoc();
        for (quint32 fid : fixtureIds)
            if (src->fixture(fid) == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No fixture %1 in the source project").arg(fid)));
                return;
            }
        for (quint32 gid : groupIds)
            if (src->fixtureGroup(gid) == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No fixture group %1 in the source project").arg(gid)));
                return;
            }
        for (quint32 fid : functionIds)
            if (src->function(fid) == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No function %1 in the source project").arg(fid)));
                return;
            }

        if (fixtureIds.isEmpty() && groupIds.isEmpty() && functionIds.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Nothing selected to import")));
            return;
        }

        for (quint32 fid : fixtureIds)
            importer.selectFixture(fid, true);
        for (quint32 gid : groupIds)
            importer.selectFixtureGroup(gid);
        for (quint32 fid : functionIds)
            importer.selectFunction(fid);

        ApiDocChanges changes(doc, m_server);
        importer.apply();
        doc->setModified();
        changes.broadcastCreated(session->clientId());

        QJsonObject data;
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        data.insert(QStringLiteral("fixtureIdMap"), remapToJson(importer.fixtureRemap()));
        data.insert(QStringLiteral("fixtureGroupIdMap"), remapToJson(importer.fixtureGroupRemap()));
        data.insert(QStringLiteral("paletteIdMap"), remapToJson(importer.paletteRemap()));
        data.insert(QStringLiteral("functionIdMap"), remapToJson(importer.functionRemap()));
        data.insert(QStringLiteral("createdFixtureIds"), idList(importer.createdFixtures()));
        data.insert(QStringLiteral("skippedFixtureIds"), idList(importer.skippedFixtures()));
        session->send(ApiEnvelope::buildOkResponse(id, data));
        m_server->broadcast(QStringLiteral("core.project.imported"), data, session->clientId(), false);
    });
}
