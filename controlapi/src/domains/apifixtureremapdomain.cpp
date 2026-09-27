/*
  Q Light Controller Plus - Control API
  apifixtureremapdomain.cpp

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
#include <QJsonObject>
#include <QMap>
#include <QSet>

#include "apifixtureremapdomain.h"
#include "apimonitordomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "apivchost.h"
#include "fixtureremapper.h"
#include "monitorproperties.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcchannel.h"
#include "channelmodifier.h"
#include "fixture.h"
#include "doc.h"

namespace {

bool isGenericFixture(const Fixture *fixture)
{
    const QLCFixtureDef *def = fixture->fixtureDef();
    return def == nullptr || (def->manufacturer() == KXMLFixtureGeneric && def->model() == KXMLFixtureGeneric);
}

QJsonObject fixtureSummaryToJson(Fixture *fixture)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(fixture->id()));
    obj.insert(QStringLiteral("name"), fixture->name());
    obj.insert(QStringLiteral("universe"), int(fixture->universe()));
    obj.insert(QStringLiteral("address"), int(fixture->address()));
    obj.insert(QStringLiteral("channels"), int(fixture->channels()));
    obj.insert(QStringLiteral("fixtureType"), fixture->typeString());
    obj.insert(QStringLiteral("isGeneric"), isGenericFixture(fixture));
    obj.insert(QStringLiteral("crossUniverse"), fixture->crossUniverse());
    QLCFixtureDef *def = fixture->fixtureDef();
    if (def != nullptr)
    {
        obj.insert(QStringLiteral("manufacturer"), def->manufacturer());
        obj.insert(QStringLiteral("model"), def->model());
    }
    if (fixture->fixtureMode() != nullptr)
        obj.insert(QStringLiteral("mode"), fixture->fixtureMode()->name());
    return obj;
}

/** A resolved FixturesDefinitionRef: either a library def+mode or a generic
 *  dimmer channel count. */
struct TargetDef
{
    QLCFixtureDef *def = nullptr;
    QLCFixtureMode *mode = nullptr;
    int genericChannels = 0;
    bool isGeneric() const { return def == nullptr; }
    int channelCount() const { return isGeneric() ? genericChannels : mode->channels().count(); }
    QString modelName() const { return isGeneric() ? QStringLiteral("Generic Dimmer") : def->model(); }
};

/** Resolves a FixturesDefinitionRef object; sends the error itself. */
bool resolveDefinition(Doc *doc, const QJsonObject &defObj, ApiSession *session, const QString &id, TargetDef &out)
{
    bool hasGeneric = defObj.contains(QStringLiteral("generic"));
    bool hasNamed = defObj.contains(QStringLiteral("manufacturer")) || defObj.contains(QStringLiteral("model")) ||
                    defObj.contains(QStringLiteral("mode"));
    if (hasGeneric == hasNamed)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
            QStringLiteral("definition must be exactly one of {manufacturer+model+mode, generic}")));
        return false;
    }
    if (hasGeneric)
    {
        out.genericChannels = defObj.value(QStringLiteral("generic")).toObject().value(QStringLiteral("channels")).toInt(0);
        if (out.genericChannels < 1)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("generic.channels must be >= 1")));
            return false;
        }
        return true;
    }
    QString manufacturer = defObj.value(QStringLiteral("manufacturer")).toString();
    QString model = defObj.value(QStringLiteral("model")).toString();
    QString modeName = defObj.value(QStringLiteral("mode")).toString();
    out.def = (manufacturer.isEmpty() || model.isEmpty()) ? nullptr : doc->fixtureDefCache()->fixtureDef(manufacturer, model);
    if (out.def == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No such fixture definition")));
        return false;
    }
    out.mode = out.def->mode(modeName);
    if (out.mode == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No such fixture mode")));
        return false;
    }
    return true;
}

/** FixtureRemapper::autoConnectFixtures()'s matching rule computed against a
 *  mode's channel layout (the target fixture does not exist yet). */
QList<QPair<quint32, quint32>> suggestChannelMap(Fixture *src, const TargetDef &target)
{
    QList<QPair<quint32, quint32>> pairs;
    bool srcGeneric = isGenericFixture(src);
    bool oneToOne = false;

    if (srcGeneric && target.isGeneric())
        oneToOne = true;
    else if (!srcGeneric && !target.isGeneric() && src->fixtureDef() != nullptr && src->fixtureMode() != nullptr &&
             src->fixtureDef()->name() == target.def->name() && src->fixtureMode()->name() == target.mode->name())
        oneToOne = true;

    int tgtCount = target.channelCount();
    for (quint32 s = 0; s < src->channels(); s++)
    {
        if (oneToOne)
        {
            if (int(s) < tgtCount)
                pairs.append(qMakePair(s, s));
            continue;
        }

        const QLCChannel *srcCh = src->channel(s);
        if (srcCh == nullptr)
            continue;

        for (int t = 0; t < tgtCount; t++)
        {
            const QLCChannel *tgtCh = target.isGeneric() ? nullptr : target.mode->channel(quint32(t));
            // A generic dimmer target only has Intensity channels
            QLCChannel::Group tgtGroup = tgtCh != nullptr ? tgtCh->group() : QLCChannel::Intensity;
            QLCChannel::ControlByte tgtByte = tgtCh != nullptr ? tgtCh->controlByte() : QLCChannel::MSB;
            QLCChannel::PrimaryColour tgtColour = tgtCh != nullptr ? tgtCh->colour() : QLCChannel::NoColour;

            if (tgtGroup != srcCh->group() || tgtByte != srcCh->controlByte())
                continue;
            if (tgtGroup == QLCChannel::Intensity && tgtColour != srcCh->colour())
                continue;

            pairs.append(qMakePair(s, quint32(t)));
            break;
        }
    }
    return pairs;
}

QJsonArray channelMapToJson(const QList<QPair<quint32, quint32>> &pairs)
{
    QJsonArray arr;
    for (const auto &p : pairs)
    {
        QJsonObject e;
        e.insert(QStringLiteral("sourceChannel"), int(p.first));
        e.insert(QStringLiteral("targetChannel"), int(p.second));
        arr.append(e);
    }
    return arr;
}

/** Copy of an existing fixture (same id / patch / definition / fade and
 *  modifier settings) parented to tmp - what Doc::replaceFixtures() copies
 *  from. Needed because replaceFixtures() deletes the originals before it
 *  reads the list it was handed. */
Fixture *cloneFixture(Doc *doc, Fixture *src, QObject *tmp)
{
    Fixture *copy = new Fixture(tmp);
    copy->setID(src->id());
    copy->setName(src->name());
    copy->setUniverse(src->universe());
    copy->setAddress(src->address());
    if (isGenericFixture(src))
        copy->setChannels(src->channels());
    else
        copy->setFixtureDefinition(src->fixtureDef(), src->fixtureMode());
    copy->setExcludeFadeChannels(src->excludeFadeChannels());
    copy->setForcedHTPChannels(src->forcedHTPChannels());
    copy->setForcedLTPChannels(src->forcedLTPChannels());
    for (quint32 s = 0; s < src->channels(); s++)
        if (src->channelModifier(s) != nullptr)
            copy->setChannelModifier(s, src->channelModifier(s));
    Q_UNUSED(doc)
    return copy;
}

} // namespace

ApiFixtureRemapDomain::ApiFixtureRemapDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);
    registerMethods();
}

void ApiFixtureRemapDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    // fixtures.remap.suggestChannelMap {sourceFixtureId, target} -> {channelMap}
    dispatcher->registerMethod(QStringLiteral("fixtures.remap.suggestChannelMap"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        bool ok = false;
        quint32 srcId = params.value(QStringLiteral("sourceFixtureId")).toString().toUInt(&ok);
        Fixture *src = ok ? doc->fixture(srcId) : nullptr;
        if (src == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such fixture")));
            return;
        }
        TargetDef target;
        if (resolveDefinition(doc, params.value(QStringLiteral("target")).toObject(), session, id, target) == false)
            return;

        QJsonObject result;
        result.insert(QStringLiteral("channelMap"), channelMapToJson(suggestChannelMap(src, target)));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // fixtures.remap.apply {mappings, unmappedSourceFixtureIds?, baseRevision} -> {docRevision}
    dispatcher->registerMethod(QStringLiteral("fixtures.remap.apply"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
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

        QJsonArray mappings = params.value(QStringLiteral("mappings")).toArray();
        if (mappings.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("mappings must not be empty")));
            return;
        }

        struct Mapping
        {
            Fixture *source = nullptr;
            quint32 universe = 0;
            int address = 0;
            TargetDef target;
            QString name;
            QList<QPair<quint32, quint32>> channelMap;
        };
        QList<Mapping> plan;
        QSet<quint32> consumed; // source ids replaced or deleted

        for (const QJsonValue &v : mappings)
        {
            QJsonObject m = v.toObject();
            Mapping mp;
            bool ok = false;
            quint32 srcId = m.value(QStringLiteral("sourceFixtureId")).toString().toUInt(&ok);
            mp.source = ok ? doc->fixture(srcId) : nullptr;
            if (mp.source == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                    QStringLiteral("No such fixture: %1").arg(m.value(QStringLiteral("sourceFixtureId")).toString())));
                return;
            }
            if (consumed.contains(srcId))
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("Fixture %1 is mapped twice").arg(srcId)));
                return;
            }
            consumed.insert(srcId);

            int universe = m.value(QStringLiteral("universe")).toInt(-1);
            if (universe < 0 || doc->inputOutputMap()->universe(quint32(universe)) == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such universe")));
                return;
            }
            mp.universe = quint32(universe);
            mp.address = m.value(QStringLiteral("address")).toInt(-1);
            if (resolveDefinition(doc, m.value(QStringLiteral("definition")).toObject(), session, id, mp.target) == false)
                return;
            int chCount = mp.target.channelCount();
            if (mp.address < 0 || mp.address + chCount > 512)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("Address %1 does not fit %2 channels in a 512-channel universe").arg(mp.address).arg(chCount)));
                return;
            }
            mp.name = m.value(QStringLiteral("name")).toString();
            if (mp.name.isEmpty())
                mp.name = mp.source->name();

            for (const QJsonValue &cv : m.value(QStringLiteral("channelMap")).toArray())
            {
                QJsonObject c = cv.toObject();
                int sc = c.value(QStringLiteral("sourceChannel")).toInt(-1);
                int tc = c.value(QStringLiteral("targetChannel")).toInt(-1);
                if (sc < 0 || sc >= int(mp.source->channels()) || tc < 0 || tc >= chCount)
                {
                    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                        QStringLiteral("channelMap entry %1 -> %2 is out of range for fixture %3").arg(sc).arg(tc).arg(srcId)));
                    return;
                }
                mp.channelMap.append(qMakePair(quint32(sc), quint32(tc)));
            }
            plan.append(mp);
        }

        QList<quint32> deleted;
        for (const QJsonValue &v : params.value(QStringLiteral("unmappedSourceFixtureIds")).toArray())
        {
            bool ok = false;
            quint32 fid = v.toString().toUInt(&ok);
            if (ok == false || doc->fixture(fid) == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                    QStringLiteral("No such fixture: %1").arg(v.toString())));
                return;
            }
            if (consumed.contains(fid))
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("Fixture %1 is both mapped and deleted").arg(fid)));
                return;
            }
            consumed.insert(fid);
            deleted.append(fid);
        }

        // Final address map: untouched sources + new targets, checked for
        // overlaps before anything changes (there is no rollback).
        QHash<quint32, quint32> owner; // absolute address -> owning (new) id
        QList<Fixture *> untouched;
        for (Fixture *fixture : doc->fixtures())
        {
            if (fixture == nullptr || consumed.contains(fixture->id()))
                continue;
            untouched.append(fixture);
            for (quint32 i = 0; i < fixture->channels(); i++)
                owner.insert(fixture->universeAddress() + i, fixture->id());
        }
        quint32 nextId = 0;
        for (Fixture *fixture : doc->fixtures())
            if (fixture != nullptr && fixture->id() >= nextId)
                nextId = fixture->id() + 1;

        QList<quint32> targetIds;
        for (const Mapping &mp : plan)
        {
            quint32 newId = nextId++;
            targetIds.append(newId);
            quint32 base = (mp.universe << 9) | quint32(mp.address);
            for (int i = 0; i < mp.target.channelCount(); i++)
            {
                if (owner.contains(base + i))
                {
                    QJsonObject details;
                    details.insert(QStringLiteral("address"), mp.address + i);
                    details.insert(QStringLiteral("universe"), int(mp.universe));
                    details.insert(QStringLiteral("sourceFixtureId"), QString::number(mp.source->id()));
                    session->send(ApiEnvelope::buildErrorResponse(id, QStringLiteral("FIXTURES_ADDRESS_OVERLAP"),
                        QStringLiteral("Address %1 in universe %2 overlaps another fixture").arg(mp.address + i).arg(mp.universe + 1), details));
                    return;
                }
                owner.insert(base + i, newId);
            }
        }

        // Build the replacement list and the remapper tables
        QObject tmp;
        FixtureRemapper remapper;
        QList<Fixture *> replacement;
        for (Fixture *fixture : untouched)
        {
            replacement.append(cloneFixture(doc, fixture, &tmp));
            for (quint32 ch = 0; ch < fixture->channels(); ch++)
                remapper.addChannelRemap(fixture->id(), ch, fixture->id(), ch);
        }

        QJsonArray newFixturesJson;
        QJsonArray replacedJson;
        QLCFixtureDef *genDef = nullptr;
        QLCFixtureMode *genMode = nullptr;
        for (int i = 0; i < plan.count(); i++)
        {
            const Mapping &mp = plan.at(i);
            Fixture *tgt = new Fixture(&tmp);
            tgt->setID(targetIds.at(i));
            tgt->setName(mp.name);
            tgt->setUniverse(mp.universe);
            tgt->setAddress(quint32(mp.address));
            if (mp.target.isGeneric())
            {
                // replaceFixtures() only needs channels() for a generic dimmer,
                // but a real definition keeps channel() lookups valid below.
                if (genDef == nullptr || genMode->channels().count() != mp.target.genericChannels)
                {
                    genDef = tgt->genericDimmerDef(mp.target.genericChannels);
                    genMode = tgt->genericDimmerMode(genDef, mp.target.genericChannels);
                }
                tgt->setFixtureDefinition(genDef, genMode);
            }
            else
            {
                tgt->setFixtureDefinition(mp.target.def, mp.target.mode);
            }

            for (const auto &pair : mp.channelMap)
            {
                remapper.addChannelRemap(mp.source->id(), pair.first, tgt->id(), pair.second);
                if (mp.source->channelCanFade(int(pair.first)) == false)
                    tgt->setChannelCanFade(int(pair.second), false);
                if (mp.source->channelModifier(pair.first) != nullptr)
                    tgt->setChannelModifier(pair.second, mp.source->channelModifier(pair.first));
            }
            replacement.append(tgt);
            replacedJson.append(QString::number(mp.source->id()));
        }

        // The VC widgets' map has to be built before applyRemap() (the
        // engine tables stay valid, but keep the two consistent anyway).
        QMap<SceneValue, SceneValue> vcMap;
        for (int i = 0; i < remapper.sourceList().count(); i++)
            vcMap.insert(remapper.sourceList().at(i), remapper.targetList().at(i));

        remapper.applyRemap(doc, replacement);

        ApiVcHost *vcHost = dynamic_cast<ApiVcHost *>(m_server->parent());
        if (vcHost != nullptr)
            vcHost->vcRemapChannels(vcMap);

        doc->setModified();

        QJsonArray monitorItems;
        for (quint32 newId : targetIds)
        {
            Fixture *created = doc->fixture(newId);
            if (created == nullptr)
                continue;
            newFixturesJson.append(fixtureSummaryToJson(created));
            monitorItems.append(ApiMonitorDomain::itemToJson(doc, newId, 0, 0, doc->monitorProperties()->containsFixture(newId)));
        }
        QJsonArray deletedJson;
        for (quint32 fid : deleted)
            deletedJson.append(QString::number(fid));

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("fixtureIds"), [&]() { QJsonArray a; for (quint32 t : targetIds) a.append(QString::number(t)); return a; }());
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("fixtures"), newFixturesJson);
        data.insert(QStringLiteral("replacedFixtureIds"), replacedJson);
        data.insert(QStringLiteral("deletedFixtureIds"), deletedJson);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.remap.applied"), data, session->clientId(), false);

        QJsonObject mon;
        mon.insert(QStringLiteral("items"), monitorItems);
        mon.insert(QStringLiteral("removed"), QJsonArray());
        mon.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.monitor.changed"), mon, session->clientId(), false);
    });
}
