/*
  Q Light Controller Plus - Control API
  apifixturesdomain.cpp

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
#include <QJsonValue>
#include <QVector3D>

#include <algorithm>

#include "apifixturesdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "inputoutputmap.h"
#include "universe.h"
#include "qlcfixturedefcache.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "qlcfixturehead.h"
#include "qlcphysical.h"
#include "qlccapability.h"
#include "qlcchannel.h"
#include "fixture.h"
#include "fixturegroup.h"
#include "grouphead.h"
#include "qlcpoint.h"
#include "channelmodifier.h"
#include "monitorproperties.h"
#include "doc.h"

namespace {

QJsonObject physicalToJson(const QLCPhysical &physical); // defined below

// fixtures.yaml's FixturesDefinitionRef "generic" case attaches a procedural
// def whose manufacturer AND model are both this literal string
// (Fixture::genericDimmerDef(), fixture.cpp) - this is the spec's own
// documented discriminator for FixturesPatchedFixture.isGeneric, not
// nullness/identity of fixtureDef().
bool isGenericFixture(Fixture *fixture)
{
    QLCFixtureDef *def = fixture->fixtureDef();
    return def != nullptr && def->manufacturer() == KXMLFixtureGeneric && def->model() == KXMLFixtureGeneric;
}

QJsonObject fixtureSummaryToJson(Fixture *fixture)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(fixture->id()));
    obj.insert(QStringLiteral("name"), fixture->name());
    obj.insert(QStringLiteral("universe"), int(fixture->universe()));
    obj.insert(QStringLiteral("address"), int(fixture->address()));
    obj.insert(QStringLiteral("channels"), int(fixture->channels()));
    obj.insert(QStringLiteral("heads"), fixture->heads());
    obj.insert(QStringLiteral("fixtureType"), fixture->typeString());
    obj.insert(QStringLiteral("isGeneric"), isGenericFixture(fixture));
    obj.insert(QStringLiteral("crossUniverse"), fixture->crossUniverse());

    QLCFixtureDef *def = fixture->fixtureDef();
    if (def != nullptr)
    {
        obj.insert(QStringLiteral("manufacturer"), def->manufacturer());
        obj.insert(QStringLiteral("model"), def->model());
    }
    QLCFixtureMode *mode = fixture->fixtureMode();
    if (mode != nullptr)
        obj.insert(QStringLiteral("mode"), mode->name());

    return obj;
}

QJsonObject fixtureDetailToJson(Fixture *fixture)
{
    QJsonObject obj = fixtureSummaryToJson(fixture);
    const QList<int> forcedHTP = fixture->forcedHTPChannels();
    const QList<int> forcedLTP = fixture->forcedLTPChannels();

    QJsonArray channelList;
    for (quint32 i = 0; i < fixture->channels(); i++)
    {
        const QLCChannel *ch = fixture->channel(i);
        if (ch == nullptr)
            continue;

        QJsonObject chObj;
        chObj.insert(QStringLiteral("index"), int(i));
        chObj.insert(QStringLiteral("name"), ch->name());
        chObj.insert(QStringLiteral("group"), QLCChannel::groupToString(ch->group()));
        if (ch->group() == QLCChannel::Intensity && ch->colour() != QLCChannel::NoColour)
            chObj.insert(QStringLiteral("colour"), QLCChannel::colourToString(ch->colour()));
        chObj.insert(QStringLiteral("absoluteAddress"), int(fixture->channelAddress(i)));
        // Per-channel patch-time behaviour (fixtures.channel.setBehaviour):
        // forced HTP/LTP, fade exclusion and the attached modifier template.
        chObj.insert(QStringLiteral("canFade"), fixture->channelCanFade(int(i)));
        QString precedence = QStringLiteral("auto");
        if (forcedHTP.contains(int(i)))
            precedence = QStringLiteral("htp");
        else if (forcedLTP.contains(int(i)))
            precedence = QStringLiteral("ltp");
        chObj.insert(QStringLiteral("precedence"), precedence);
        ChannelModifier *mod = fixture->channelModifier(i);
        chObj.insert(QStringLiteral("modifier"), mod ? QJsonValue(mod->name()) : QJsonValue());
        channelList.append(chObj);
    }
    obj.insert(QStringLiteral("channelList"), channelList);

    // Modes the fixture can be switched to with fixtures.update {mode}, and
    // the current mode's physical block (fixture summary / universe summary).
    // Served here because generic dimmer / RGB panel definitions are not in
    // the definition cache, so fixtures.defs.getModel cannot answer for them.
    QJsonArray availableModes;
    QLCFixtureDef *def = fixture->fixtureDef();
    if (def != nullptr)
    {
        for (QLCFixtureMode *mode : def->modes())
        {
            QJsonObject m;
            m.insert(QStringLiteral("name"), mode->name());
            m.insert(QStringLiteral("channelCount"), mode->channels().count());
            availableModes.append(m);
        }
    }
    obj.insert(QStringLiteral("availableModes"), availableModes);
    if (fixture->fixtureMode() != nullptr)
        obj.insert(QStringLiteral("physical"), physicalToJson(fixture->fixtureMode()->physical()));

    return obj;
}

// address (0-based, within a universe) .. address+channels-1 must all fit in
// a single universe's 512-channel space - Fixture/Doc's own absolute
// addressing (universe<<9 | address) has no overflow guard of its own (an
// out-of-range footprint silently encroaches into the next universe's own
// address 0 instead of erroring), so this must be checked here.
bool validAddressRange(qint64 address, qint64 channels)
{
    return address >= 0 && channels >= 1 && (address + channels) <= 512;
}

// Mirrors the overlap check qmlui/fixturebrowser.cpp's availableChannel()
// does via the same Doc::fixtureForAddress() lookup - true if every channel
// in [address, address+channels) of universeId is either unpatched or
// belongs to excludeFixtureId (used by fixtures.update so a fixture doesn't
// conflict with its own current footprint when address/universe aren't
// actually changing).
bool rangeIsFree(Doc *doc, quint32 universeId, quint32 address, quint32 channels, quint32 excludeFixtureId)
{
    quint32 absAddress = (universeId << 9) | (address & 0x01FFu);
    for (quint32 i = 0; i < channels; i++)
    {
        quint32 owner = doc->fixtureForAddress(absAddress + i);
        if (owner != Fixture::invalidId() && owner != excludeFixtureId)
            return false;
    }
    return true;
}

} // namespace

namespace {

/*********************************************************************
 * Fixture definition library (fixtures.defs.*) serializers
 *********************************************************************/

// fixtures.yaml FixturesPhysical - QLCPhysical flattened.
QJsonObject physicalToJson(const QLCPhysical &physical)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("width"), physical.width());
    obj.insert(QStringLiteral("height"), physical.height());
    obj.insert(QStringLiteral("depth"), physical.depth());
    obj.insert(QStringLiteral("weight"), physical.weight());
    obj.insert(QStringLiteral("powerConsumption"), physical.powerConsumption());
    obj.insert(QStringLiteral("dmxConnector"), physical.dmxConnector());
    obj.insert(QStringLiteral("bulbType"), physical.bulbType());
    obj.insert(QStringLiteral("bulbLumens"), physical.bulbLumens());
    obj.insert(QStringLiteral("bulbColourTemperature"), physical.bulbColourTemperature());
    obj.insert(QStringLiteral("lensName"), physical.lensName());
    obj.insert(QStringLiteral("lensDegreesMin"), physical.lensDegreesMin());
    obj.insert(QStringLiteral("lensDegreesMax"), physical.lensDegreesMax());
    obj.insert(QStringLiteral("focusType"), physical.focusType());
    obj.insert(QStringLiteral("focusPanMax"), physical.focusPanMax());
    obj.insert(QStringLiteral("focusTiltMax"), physical.focusTiltMax());
    obj.insert(QStringLiteral("layoutWidth"), physical.layoutSize().width());
    obj.insert(QStringLiteral("layoutHeight"), physical.layoutSize().height());
    return obj;
}

QString capabilityPresetTypeToJson(QLCCapability::PresetType type)
{
    switch (type)
    {
    case QLCCapability::SingleColor: return QStringLiteral("SingleColor");
    case QLCCapability::DoubleColor: return QStringLiteral("DoubleColor");
    case QLCCapability::SingleValue: return QStringLiteral("SingleValue");
    case QLCCapability::DoubleValue: return QStringLiteral("DoubleValue");
    case QLCCapability::Picture:     return QStringLiteral("Picture");
    default:
    case QLCCapability::None:        return QStringLiteral("None");
    }
}

// fixtures.yaml FixturesCapability (the read-oriented summary shape).
QJsonObject capabilityToJson(QLCCapability *cap)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("min"), int(cap->min()));
    obj.insert(QStringLiteral("max"), int(cap->max()));
    obj.insert(QStringLiteral("name"), cap->name());
    obj.insert(QStringLiteral("preset"), QLCCapability::presetToString(cap->preset()));
    QLCCapability::PresetType presetType = cap->presetType();
    obj.insert(QStringLiteral("presetType"), capabilityPresetTypeToJson(presetType));
    if (presetType == QLCCapability::SingleColor || presetType == QLCCapability::DoubleColor)
    {
        QColor color1 = cap->resource(0).value<QColor>();
        if (color1.isValid())
            obj.insert(QStringLiteral("color1"), color1.name());
        if (presetType == QLCCapability::DoubleColor)
        {
            QColor color2 = cap->resource(1).value<QColor>();
            if (color2.isValid())
                obj.insert(QStringLiteral("color2"), color2.name());
        }
    }
    return obj;
}

// fixtures.yaml FixturesModeChannel: QLCChannel as arranged within a mode.
// "group" is the plain QLCChannel::groupToString() spelling the web UI
// contract asks for ("Intensity", "Colour", "Pan", ...).
QJsonObject modeChannelToJson(quint32 index, QLCChannel *channel)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("index"), int(index));
    obj.insert(QStringLiteral("name"), channel->name());
    obj.insert(QStringLiteral("group"), QLCChannel::groupToString(channel->group()));
    obj.insert(QStringLiteral("preset"), QLCChannel::presetToString(channel->preset()));
    if (channel->group() == QLCChannel::Intensity)
        obj.insert(QStringLiteral("colour"), QLCChannel::colourToString(channel->colour()));
    obj.insert(QStringLiteral("controlByte"), channel->controlByte() == QLCChannel::LSB
               ? QStringLiteral("LSB") : QStringLiteral("MSB"));
    obj.insert(QStringLiteral("defaultValue"), int(channel->defaultValue()));
    QJsonArray capabilities;
    for (QLCCapability *cap : channel->capabilities())
        capabilities.append(capabilityToJson(cap));
    obj.insert(QStringLiteral("capabilities"), capabilities);
    return obj;
}

QJsonArray modeChannelsToJson(QLCFixtureMode *mode)
{
    QJsonArray channels;
    QVector<QLCChannel *> list = mode->channels();
    for (int i = 0; i < list.count(); i++)
        channels.append(modeChannelToJson(quint32(i), list.at(i)));
    return channels;
}

QJsonArray modeHeadsToJson(QLCFixtureMode *mode)
{
    QJsonArray heads;
    const QVector<QLCFixtureHead> &list = mode->heads();
    for (int i = 0; i < list.count(); i++)
    {
        QJsonArray channels;
        for (quint32 ch : list.at(i).channels())
            channels.append(int(ch));
        QJsonObject head;
        head.insert(QStringLiteral("index"), i);
        head.insert(QStringLiteral("channels"), channels);
        heads.append(head);
    }
    return heads;
}

// fixtures.defs.getModel's per-mode entry: fixtures.yaml's browsing-level
// {name, channelCount} plus the web UI contract's inline channel list, so
// an Add Fixture dialog can show a mode's channels without one more
// round-trip per mode.
QJsonObject modeSummaryToJson(QLCFixtureMode *mode)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), mode->name());
    obj.insert(QStringLiteral("channelCount"), mode->channels().count());
    obj.insert(QStringLiteral("channels"), modeChannelsToJson(mode));
    return obj;
}

// fixtures.defs.getMode result (fixtures.yaml FixturesDefsGetModeOkResponse).
QJsonObject modeDetailToJson(QLCFixtureDef *def, QLCFixtureMode *mode)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("manufacturer"), def->manufacturer());
    obj.insert(QStringLiteral("model"), def->model());
    obj.insert(QStringLiteral("mode"), mode->name());
    obj.insert(QStringLiteral("channelCount"), mode->channels().count());
    quint32 masterIntensity = mode->masterIntensityChannel();
    obj.insert(QStringLiteral("masterIntensityChannel"), masterIntensity == QLCChannel::invalid()
               ? QJsonValue() : QJsonValue(int(masterIntensity)));
    obj.insert(QStringLiteral("physical"), physicalToJson(mode->physical()));
    obj.insert(QStringLiteral("useGlobalPhysical"), mode->useGlobalPhysical());
    obj.insert(QStringLiteral("channels"), modeChannelsToJson(mode));
    obj.insert(QStringLiteral("heads"), modeHeadsToJson(mode));
    return obj;
}

// Shared manufacturer+model lookup for fixtures.defs.getModel/getMode.
// QLCFixtureDefCache::fixtureDef() lazily loads the definition file on
// first access (QLCFixtureDef::checkLoaded()) - until then only the names
// from FixturesMap.xml are known - so modes()/channels() below are
// complete. Unknown -> NOT_FOUND, never an empty definition.
QLCFixtureDef *findDefinitionParam(Doc *doc, ApiSession *session, const QString &id, const QJsonObject &params)
{
    QString manufacturer = params.value(QStringLiteral("manufacturer")).toString();
    QString model = params.value(QStringLiteral("model")).toString();
    QLCFixtureDef *def = (manufacturer.isEmpty() || model.isEmpty()) ? nullptr
                       : doc->fixtureDefCache()->fixtureDef(manufacturer, model);
    if (def == nullptr)
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No such fixture definition")));
    return def;
}

} // namespace

ApiFixturesDomain::ApiFixturesDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods();
}

void ApiFixturesDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    /*********************************************************************
     * 1. Fixture definition library browsing (read-only)
     *********************************************************************/

    // fixtures.defs.listManufacturers {} -> {manufacturers: [string]}
    dispatcher->registerMethod(QStringLiteral("fixtures.defs.listManufacturers"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QStringList manufacturers = doc->fixtureDefCache()->manufacturers();
        manufacturers.sort(Qt::CaseInsensitive);
        QJsonObject result;
        result.insert(QStringLiteral("manufacturers"), QJsonArray::fromStringList(manufacturers));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // fixtures.defs.listModels {manufacturer} -> {models: [string], modelDetails: [{model, isUser}]}
    dispatcher->registerMethod(QStringLiteral("fixtures.defs.listModels"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QString manufacturer = params.value(QStringLiteral("manufacturer")).toString();
        // fixtureCache() is the manufacturer -> {model -> isUser} index built
        // from FixturesMap.xml at startup: answers without loading a single
        // definition file, unlike fixtureDef()->isUser() would.
        QMap<QString, QMap<QString, bool>> cache = doc->fixtureDefCache()->fixtureCache();
        if (manufacturer.isEmpty() || cache.contains(manufacturer) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such manufacturer")));
            return;
        }

        // "models" is the plain name list the web UI contract asks for;
        // "modelDetails" carries fixtures.yaml's {model, isUser} objects.
        QJsonArray models;
        QJsonArray modelDetails;
        const QMap<QString, bool> &byModel = cache.value(manufacturer);
        for (auto it = byModel.constBegin(); it != byModel.constEnd(); ++it)
        {
            models.append(it.key());
            QJsonObject detail;
            detail.insert(QStringLiteral("model"), it.key());
            detail.insert(QStringLiteral("isUser"), it.value());
            modelDetails.append(detail);
        }
        QJsonObject result;
        result.insert(QStringLiteral("manufacturer"), manufacturer);
        result.insert(QStringLiteral("models"), models);
        result.insert(QStringLiteral("modelDetails"), modelDetails);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // fixtures.defs.getModel {manufacturer, model}
    //   -> {manufacturer, model, type, fixtureType, author, isUser, physical, modes: [{name, channelCount, channels}]}
    dispatcher->registerMethod(QStringLiteral("fixtures.defs.getModel"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QLCFixtureDef *def = findDefinitionParam(doc, session, id, params);
        if (def == nullptr)
            return;

        QJsonObject result;
        result.insert(QStringLiteral("manufacturer"), def->manufacturer());
        result.insert(QStringLiteral("model"), def->model());
        // Same value under both keys: "type" is the web UI contract's name,
        // "fixtureType" fixtures.yaml's.
        QString type = QLCFixtureDef::typeToString(def->type());
        result.insert(QStringLiteral("type"), type);
        result.insert(QStringLiteral("fixtureType"), type);
        result.insert(QStringLiteral("author"), def->author());
        result.insert(QStringLiteral("isUser"), def->isUser());
        result.insert(QStringLiteral("physical"), physicalToJson(def->physical()));
        QJsonArray modes;
        for (QLCFixtureMode *mode : def->modes())
            modes.append(modeSummaryToJson(mode));
        result.insert(QStringLiteral("modes"), modes);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // fixtures.defs.getMode {manufacturer, model, mode} -> FixturesDefsGetModeOkResponse.result
    dispatcher->registerMethod(QStringLiteral("fixtures.defs.getMode"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QLCFixtureDef *def = findDefinitionParam(doc, session, id, params);
        if (def == nullptr)
            return;
        QLCFixtureMode *mode = def->mode(params.value(QStringLiteral("mode")).toString());
        if (mode == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such mode in this definition")));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, modeDetailToJson(def, mode)));
    });

    /*********************************************************************
     * 2. Patching
     *********************************************************************/

    dispatcher->registerMethod(QStringLiteral("fixtures.list"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        bool hasFilter = params.contains(QStringLiteral("universe"));
        int filterUniverse = hasFilter ? params.value(QStringLiteral("universe")).toInt() : -1;

        QJsonArray fixturesJson;
        for (Fixture *fixture : doc->fixtures())
        {
            if (hasFilter && int(fixture->universe()) != filterUniverse)
                continue;
            fixturesJson.append(fixtureSummaryToJson(fixture));
        }

        QJsonObject result;
        result.insert(QStringLiteral("fixtures"), fixturesJson);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.get"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        bool ok = false;
        quint32 fixtureId = params.value(QStringLiteral("fixtureId")).toString().toUInt(&ok);
        Fixture *fixture = ok ? doc->fixture(fixtureId) : nullptr;
        if (fixture == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such fixture")));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, fixtureDetailToJson(fixture)));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.findAvailableAddress"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        int universeId = params.value(QStringLiteral("universe")).toInt(-1);
        int channels = params.value(QStringLiteral("channels")).toInt(0);
        int quantity = params.value(QStringLiteral("quantity")).toInt(1);
        int gap = params.value(QStringLiteral("gap")).toInt(0);
        int requested = params.value(QStringLiteral("requestedAddress")).toInt(0);

        quint32 excludeId = Fixture::invalidId();
        if (params.contains(QStringLiteral("excludeFixtureId")))
        {
            bool ok = false;
            quint32 parsed = params.value(QStringLiteral("excludeFixtureId")).toString().toUInt(&ok);
            if (ok)
                excludeId = parsed;
        }

        if (universeId < 0 || channels < 1 || quantity < 1 || gap < 0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid universe/channels/quantity/gap")));
            return;
        }

        if (doc->inputOutputMap()->universe(quint32(universeId)) == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such universe")));
            return;
        }

        // 1 - try the requested address for every instance in the batch, laid
        // out exactly as fixtures.patch would (address, address+channels+gap,
        // ...), same as FixtureBrowser::availableChannel()'s first pass.
        bool fits = true;
        for (int n = 0; n < quantity && fits; n++)
        {
            qint64 addr = qint64(requested) + qint64(n) * (qint64(channels) + gap);
            if (validAddressRange(addr, channels) == false ||
                rangeIsFree(doc, quint32(universeId), quint32(addr), quint32(channels), excludeId) == false)
            {
                fits = false;
            }
        }

        QJsonObject result;
        if (fits)
        {
            result.insert(QStringLiteral("available"), true);
            result.insert(QStringLiteral("address"), requested);
        }
        else
        {
            // 2 - fall back to a plain sliding-window scan for the first
            // contiguous free block big enough for the whole batch
            // (channels*quantity + gap*quantity, matching
            // FixtureBrowser::availableChannel()'s own span, which reserves a
            // trailing gap after the last fixture too) - a straightforward
            // O(512*blockSize) scan rather than reproducing that method's own
            // quirkier single-pass counter loop (see its comments), since the
            // universe is capped at 512 channels this is trivially fast and
            // easier to verify correct.
            qint64 blockSize = (qint64(channels) + gap) * quantity;
            int found = -1;
            for (int start = 0; start + blockSize <= 512 && found < 0; start++)
            {
                if (rangeIsFree(doc, quint32(universeId), quint32(start), quint32(blockSize), excludeId))
                    found = start;
            }

            if (found >= 0)
            {
                result.insert(QStringLiteral("available"), true);
                result.insert(QStringLiteral("address"), found);
            }
            else
            {
                result.insert(QStringLiteral("available"), false);
            }
        }

        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.patch"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
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

        int universeId = params.value(QStringLiteral("universe")).toInt(-1);
        int baseAddress = params.value(QStringLiteral("address")).toInt(-1);
        if (universeId < 0 || doc->inputOutputMap()->universe(quint32(universeId)) == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such universe")));
            return;
        }

        int quantity = params.value(QStringLiteral("quantity")).toInt(1);
        if (quantity < 1)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("quantity must be >= 1")));
            return;
        }
        int gap = params.value(QStringLiteral("gap")).toInt(0);
        if (gap < 0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("gap must be >= 0")));
            return;
        }

        // Resolve FixturesDefinitionRef: exactly one of {manufacturer+model+
        // mode, generic} - see fixtures.yaml's own schema doc. The web UI
        // contract spells the same fields flat at the top level
        // ({manufacturer, model, mode, universe, address, quantity, gap}) -
        // accepted as an alias whenever no "definition" object is given.
        QJsonObject defObj = params.value(QStringLiteral("definition")).toObject();
        if (params.contains(QStringLiteral("definition")) == false)
        {
            for (const QString &key : { QStringLiteral("manufacturer"), QStringLiteral("model"),
                                        QStringLiteral("mode"), QStringLiteral("generic") })
                if (params.contains(key))
                    defObj.insert(key, params.value(key));
        }
        bool hasGeneric = defObj.contains(QStringLiteral("generic"));
        bool hasManufacturer = defObj.contains(QStringLiteral("manufacturer"));
        bool hasModel = defObj.contains(QStringLiteral("model"));
        bool hasMode = defObj.contains(QStringLiteral("mode"));
        bool hasNamed = hasManufacturer || hasModel || hasMode;

        if (hasGeneric == hasNamed) // both or neither
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("definition must be exactly one of {manufacturer+model+mode, generic}")));
            return;
        }
        if (hasNamed && (hasManufacturer == false || hasModel == false || hasMode == false))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("manufacturer, model and mode must all be given together")));
            return;
        }

        int channelCount = 0;
        QLCFixtureDef *namedDef = nullptr;
        QLCFixtureMode *namedMode = nullptr;
        QString model;

        if (hasGeneric)
        {
            channelCount = defObj.value(QStringLiteral("generic")).toObject()
                               .value(QStringLiteral("channels")).toInt(0);
            if (channelCount < 1)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("generic.channels must be >= 1")));
                return;
            }
        }
        else
        {
            QString manufacturer = defObj.value(QStringLiteral("manufacturer")).toString();
            model = defObj.value(QStringLiteral("model")).toString();
            QString modeName = defObj.value(QStringLiteral("mode")).toString();

            namedDef = doc->fixtureDefCache()->fixtureDef(manufacturer, model);
            if (namedDef == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such fixture definition")));
                return;
            }
            namedMode = namedDef->mode(modeName);
            if (namedMode == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such fixture mode")));
                return;
            }
            channelCount = namedMode->channels().size();
        }

        // Pre-validate every instance's address range + overlap up front, so
        // a bulk patch either fully succeeds or makes no change at all
        // (matches this domain's other mutations, which never partially
        // apply on error).
        for (int n = 0; n < quantity; n++)
        {
            qint64 addr = qint64(baseAddress) + qint64(n) * (qint64(channelCount) + gap);
            if (validAddressRange(addr, channelCount) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("Address %1 (fixture %2 of %3) does not fit in a 512-channel universe")
                        .arg(addr).arg(n + 1).arg(quantity)));
                return;
            }
            if (rangeIsFree(doc, quint32(universeId), quint32(addr), quint32(channelCount), Fixture::invalidId()) == false)
            {
                QJsonObject details;
                details.insert(QStringLiteral("address"), addr);
                session->send(ApiEnvelope::buildErrorResponse(id, QStringLiteral("FIXTURES_ADDRESS_OVERLAP"),
                    QStringLiteral("Address %1 overlaps an existing fixture").arg(addr), details));
                return;
            }
        }

        QString baseName = params.value(QStringLiteral("name")).toString();
        if (baseName.isEmpty())
            baseName = hasGeneric ? QStringLiteral("Generic Dimmer") : model;

        QLCFixtureDef *genDef = nullptr;
        QLCFixtureMode *genMode = nullptr;
        QJsonArray fixturesJson;
        QJsonArray fixtureIdsJson;

        for (int n = 0; n < quantity; n++)
        {
            qint64 addr = qint64(baseAddress) + qint64(n) * (qint64(channelCount) + gap);
            Fixture *fxi = new Fixture(doc);
            fxi->setUniverse(quint32(universeId));
            fxi->setAddress(quint32(addr));

            if (hasGeneric)
            {
                // A bulk-patched batch of generic dimmers shares one
                // QLCFixtureDef/QLCFixtureMode pair across every instance,
                // exactly like qmlui/fixturemanager.cpp's own addFixture()
                // does - safe because Fixture::~Fixture() never deletes
                // m_fixtureDef/m_fixtureMode for a generic definition either
                // (see fixture.cpp; it's a well-known existing leak, not
                // something introduced here).
                if (genDef == nullptr)
                {
                    genDef = fxi->genericDimmerDef(channelCount);
                    genMode = fxi->genericDimmerMode(genDef, channelCount);
                }
                fxi->setFixtureDefinition(genDef, genMode);
            }
            else
            {
                fxi->setFixtureDefinition(namedDef, namedMode);
            }

            if (doc->addFixture(fxi) == false)
            {
                delete fxi;
                // Pre-validated above, so this should not happen in practice;
                // fixtures created by earlier loop iterations (if any) stay
                // patched - see this method's own doc comment on why a full
                // rollback isn't attempted here.
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                                QStringLiteral("Could not add fixture")));
                return;
            }

            // The engine appends " [<n>]" unconditionally using the new
            // fixture's own id+1, not a per-batch ordinal - see
            // FixturesPatchRequest.name's own doc comment in fixtures.yaml,
            // mirroring FixtureManager::addFixture() exactly.
            fxi->setName(QStringLiteral("%1 [%2]").arg(baseName).arg(fxi->id() + 1));

            fixtureIdsJson.append(QString::number(fxi->id()));
            fixturesJson.append(fixtureSummaryToJson(fxi));
        }

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("fixtureIds"), fixtureIdsJson);
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        data.insert(QStringLiteral("fixtures"), fixturesJson);
        m_server->broadcast(QStringLiteral("fixtures.patched"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.update"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
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

        bool ok = false;
        quint32 fixtureId = params.value(QStringLiteral("fixtureId")).toString().toUInt(&ok);
        Fixture *fixture = ok ? doc->fixture(fixtureId) : nullptr;
        if (fixture == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such fixture")));
            return;
        }

        bool hasUniverse = params.contains(QStringLiteral("universe"));
        bool hasAddress = params.contains(QStringLiteral("address"));
        bool hasName = params.contains(QStringLiteral("name"));
        bool hasMode = params.contains(QStringLiteral("mode"));
        if (hasUniverse == false && hasAddress == false && hasName == false && hasMode == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("At least one of universe/address/name/mode must be given")));
            return;
        }

        // Mode change: any mode of the fixture's own definition (for a
        // generic dimmer / RGB panel row that is just its single mode), the
        // same list FixtureManager::setFixtureModeIndex() offers.
        QLCFixtureMode *newMode = fixture->fixtureMode();
        if (hasMode)
        {
            QString modeName = params.value(QStringLiteral("mode")).toString();
            QLCFixtureDef *def = fixture->fixtureDef();
            QLCFixtureMode *found = nullptr;
            if (def != nullptr)
            {
                for (QLCFixtureMode *m : def->modes())
                    if (m->name() == modeName)
                        found = m;
            }
            if (found == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                    QStringLiteral("No mode \"%1\" in this fixture's definition").arg(modeName)));
                return;
            }
            newMode = found;
        }
        bool modeChanges = hasMode && newMode != fixture->fixtureMode();
        quint32 newChannels = modeChanges ? quint32(newMode->channels().count()) : fixture->channels();

        quint32 newUniverse = hasUniverse ? quint32(params.value(QStringLiteral("universe")).toInt())
                                           : fixture->universe();
        int newAddress = hasAddress ? params.value(QStringLiteral("address")).toInt()
                                     : int(fixture->address());

        if (hasUniverse || hasAddress || modeChanges)
        {
            if (validAddressRange(newAddress, int(newChannels)) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("Address %1 does not fit this fixture's %2 channels in a 512-channel universe")
                        .arg(newAddress).arg(newChannels)));
                return;
            }
            if (doc->inputOutputMap()->universe(newUniverse) == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such universe")));
                return;
            }
            if (rangeIsFree(doc, newUniverse, quint32(newAddress), newChannels, fixture->id()) == false)
            {
                QJsonObject details;
                details.insert(QStringLiteral("address"), newAddress);
                session->send(ApiEnvelope::buildErrorResponse(id, QStringLiteral("FIXTURES_ADDRESS_OVERLAP"),
                    QStringLiteral("Address %1 overlaps an existing fixture").arg(newAddress), details));
                return;
            }
        }

        // Fixture::setAddress()/setUniverse() each emit changed(), and
        // Doc::slotFixtureChanged() re-tracks the fixture's footprint on every
        // emit. Applied one after the other, the first emit would track the
        // fixture at a transient (new address, OLD universe) position, which
        // can collide with another fixture there even though the final
        // position was validated free above - Doc::slotFixtureChanged()'s
        // Q_ASSERT(!m_addresses.contains(i)) then aborts a Debug build and a
        // Release build silently steals that fixture's address entries.
        // Apply both with signals blocked and emit changed() exactly once
        // (setID() to the same id does that, the idiom qmlui's
        // FixtureManager::pasteFromClipboard() already uses): the slot first
        // drops every address owned by this id, then adds the final footprint.
        //
        // A mode change goes through the same single changed(): the new
        // footprint is tracked once, at its final position (the Qt UI's
        // FixtureManager::setFixtureModeIndex() never re-tracks the address
        // map at all). Per-channel settings on indices the new mode no longer
        // has are dropped BEFORE switching - Fixture::setChannelModifier()
        // refuses indices >= channels() afterwards, so they could never be
        // cleared again. Functions are left untouched, exactly like the Qt
        // UI: Scene values on vanished channels stay (Scene::postLoad() drops
        // them on the next load), so switching back restores them.
        QList<int> forcedHTP = fixture->forcedHTPChannels();
        QList<int> forcedLTP = fixture->forcedLTPChannels();
        if (modeChanges)
        {
            for (quint32 i = newChannels; i < fixture->channels(); i++)
            {
                fixture->setChannelModifier(i, nullptr);
                fixture->setChannelCanFade(int(i), true);
            }
            auto prune = [newChannels](QList<int> &list)
            {
                list.erase(std::remove_if(list.begin(), list.end(),
                                          [newChannels](int idx) { return idx < 0 || quint32(idx) >= newChannels; }),
                           list.end());
            };
            prune(forcedHTP);
            prune(forcedLTP);
        }

        if (hasAddress || hasUniverse || modeChanges)
        {
            fixture->blockSignals(true);
            if (modeChanges)
            {
                // the lists must be set while the old (longer or shorter)
                // mode is gone: setForced*Channels() checks the count only
                fixture->setFixtureDefinition(fixture->fixtureDef(), newMode);
                fixture->setForcedHTPChannels(forcedHTP);
                fixture->setForcedLTPChannels(forcedLTP);
            }
            if (hasAddress)
                fixture->setAddress(quint32(newAddress));
            if (hasUniverse)
                fixture->setUniverse(newUniverse);
            fixture->blockSignals(false);
            fixture->setID(fixture->id());

            // Push the (new) footprint's HTP/LTP, default values and
            // modifiers into its universe, as Doc::addFixture() does.
            doc->updateFixtureChannelCapabilities(fixture->id(), forcedHTP, forcedLTP);
        }
        if (hasName)
            fixture->setName(params.value(QStringLiteral("name")).toString());

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("fixture"), fixtureSummaryToJson(fixture));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.updated"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.unpatch"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
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

        QJsonArray idsParam = params.value(QStringLiteral("fixtureIds")).toArray();
        if (idsParam.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("fixtureIds must not be empty")));
            return;
        }

        // Validate every id exists before deleting any of them, so a bulk
        // unpatch either fully succeeds or makes no change at all.
        QList<quint32> targetIds;
        for (const QJsonValue &v : idsParam)
        {
            bool ok = false;
            quint32 fxId = v.toString().toUInt(&ok);
            if (ok == false || doc->fixture(fxId) == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                    QStringLiteral("No such fixture: %1").arg(v.toString())));
                return;
            }
            targetIds.append(fxId);
        }

        QJsonArray deletedIdsJson;
        for (quint32 fxId : std::as_const(targetIds))
        {
            doc->deleteFixture(fxId);
            deletedIdsJson.append(QString::number(fxId));
        }

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("fixtureIds"), deletedIdsJson);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.unpatched"), data, session->clientId(), false);
    });

    /*********************************************************************
     * Generic RGB panel (FixtureManager::addRGBPanel, RGBPanelProperties.qml)
     *********************************************************************/

    // fixtures.createRgbPanel {name?, universe, address, columns, rows,
    //   components?, direction?, startCorner?, displacement?,
    //   physicalWidth?, physicalHeight?, baseRevision}
    //   -> {docRevision, fixtureIds, groupId}
    // One generic "RGB panel" row fixture per row (per column when the
    // direction is vertical) plus a fixture group laid out like the physical
    // panel. Unlike the Qt UI this validates every row's footprint before
    // creating anything and never creates universes: a row that does not
    // fit the rest of a universe moves to address 0 of the NEXT EXISTING
    // universe (the Qt UI's rollover), and fails with INVALID_PARAMS when
    // there is none.
    dispatcher->registerMethod(QStringLiteral("fixtures.createRgbPanel"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
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

        auto invalid = [session, id](const QString &message)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, message));
        };

        int universeId = params.value(QStringLiteral("universe")).toInt(-1);
        int address = params.value(QStringLiteral("address")).toInt(-1);
        int columns = params.value(QStringLiteral("columns")).toInt(0);
        int rows = params.value(QStringLiteral("rows")).toInt(0);
        int phyWidth = params.value(QStringLiteral("physicalWidth")).toInt(1000);
        int phyHeightTotal = params.value(QStringLiteral("physicalHeight")).toInt(1000);
        if (universeId < 0 || doc->inputOutputMap()->universe(quint32(universeId)) == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such universe")));
            return;
        }
        // Same limits as RGBPanelProperties.qml's spin boxes.
        if (columns < 1 || columns > 170 || rows < 1 || rows > 999)
            return invalid(QStringLiteral("columns must be 1-170 and rows 1-999"));
        if (address < 0 || address > 511)
            return invalid(QStringLiteral("address must be 0-511"));
        if (phyWidth < 1 || phyHeightTotal < 1)
            return invalid(QStringLiteral("physicalWidth/physicalHeight must be >= 1"));

        static const QStringList componentNames = { QStringLiteral("RGB"), QStringLiteral("BGR"), QStringLiteral("BRG"),
                                                    QStringLiteral("GBR"), QStringLiteral("GRB"), QStringLiteral("RGBW"),
                                                    QStringLiteral("RBG") };
        QString compName = params.value(QStringLiteral("components")).toString(QStringLiteral("RGB"));
        int compIndex = componentNames.indexOf(compName);
        if (compIndex < 0)
            return invalid(QStringLiteral("components must be one of ") + componentNames.join(QStringLiteral(", ")));
        Fixture::Components components = Fixture::Components(compIndex); // same order as the enum

        QString direction = params.value(QStringLiteral("direction")).toString(QStringLiteral("horizontal"));
        QString corner = params.value(QStringLiteral("startCorner")).toString(QStringLiteral("topLeft"));
        QString displacement = params.value(QStringLiteral("displacement")).toString(QStringLiteral("snake"));
        if (direction != QStringLiteral("horizontal") && direction != QStringLiteral("vertical"))
            return invalid(QStringLiteral("direction must be horizontal or vertical"));
        if (corner != QStringLiteral("topLeft") && corner != QStringLiteral("topRight") &&
            corner != QStringLiteral("bottomLeft") && corner != QStringLiteral("bottomRight"))
            return invalid(QStringLiteral("startCorner must be topLeft, topRight, bottomLeft or bottomRight"));
        if (displacement != QStringLiteral("snake") && displacement != QStringLiteral("zigzag"))
            return invalid(QStringLiteral("displacement must be snake or zigzag"));
        bool snake = displacement == QStringLiteral("snake");
        bool left = corner == QStringLiteral("topLeft") || corner == QStringLiteral("bottomLeft");
        bool top = corner == QStringLiteral("topLeft") || corner == QStringLiteral("topRight");

        QString name = params.value(QStringLiteral("name")).toString().simplified();
        if (name.isEmpty())
            name = QStringLiteral("RGB Panel");

        // Rows are the fixtures; a vertical panel is transposed (one fixture
        // per physical column), exactly like FixtureManager::addRGBPanel().
        const int groupColumns = columns, groupRows = rows;
        int transpose = 0;
        if (direction == QStringLiteral("vertical"))
        {
            std::swap(columns, rows);
            transpose = 1;
        }
        const int perHead = components == Fixture::RGBW ? 4 : 3;
        const int rowChannels = columns * perHead;
        if (rowChannels > 512)
            return invalid(QStringLiteral("One row needs %1 channels, more than a universe holds").arg(rowChannels));

        // Lay out every row first: universe rollover onto existing universes
        // only, and every footprint free.
        QList<QPair<quint32, int>> placement;
        quint32 uni = quint32(universeId);
        int addr = address;
        for (int i = 0; i < rows; i++)
        {
            if (addr + rowChannels > 512)
            {
                uni++;
                addr = 0;
                if (doc->inputOutputMap()->universe(uni) == nullptr)
                    return invalid(QStringLiteral("Row %1 does not fit: the panel needs universe %2, which does not exist")
                                   .arg(i + 1).arg(uni + 1));
            }
            if (rangeIsFree(doc, uni, quint32(addr), quint32(rowChannels), Fixture::invalidId()) == false)
            {
                QJsonObject details;
                details.insert(QStringLiteral("universe"), int(uni));
                details.insert(QStringLiteral("address"), addr);
                session->send(ApiEnvelope::buildErrorResponse(id, QStringLiteral("FIXTURES_ADDRESS_OVERLAP"),
                    QStringLiteral("Row %1 (universe %2, address %3) overlaps an existing fixture")
                        .arg(i + 1).arg(uni + 1).arg(addr + 1), details));
                return;
            }
            placement.append(qMakePair(uni, addr));
            addr += rowChannels;
        }

        FixtureGroup *grp = new FixtureGroup(doc);
        grp->setSize(QSize(groupColumns, groupRows));
        if (doc->addFixtureGroup(grp) == false)
        {
            delete grp;
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                            QStringLiteral("Could not add fixture group")));
            return;
        }
        for (FixtureGroup *other : doc->fixtureGroups())
        {
            if (other != grp && other->name() == name)
            {
                name = QStringLiteral("%1 [%2]").arg(name).arg(grp->id());
                break;
            }
        }
        grp->setName(name);

        int currRow = 0, rowInc = 1, xPosStart = 0, xPosEnd = columns - 1, xPosInc = 1;
        if (transpose)
        {
            if (left == false) { currRow = rows - 1; rowInc = -1; }
            if (top == false) { xPosStart = columns - 1; xPosEnd = 0; xPosInc = -1; }
        }
        else
        {
            if (top == false) { currRow = rows - 1; rowInc = -1; }
            if (left == false) { xPosStart = columns - 1; xPosEnd = 0; xPosInc = -1; }
        }

        MonitorProperties *monProps = doc->monitorProperties();
        float gridUnits = monProps->gridUnits() == MonitorProperties::Meters ? 1000.0f : 304.8f;
        qreal phyHeight = qreal(phyHeightTotal) / qreal(groupRows); // before the transpose, like the Qt UI
        qreal xPos = params.value(QStringLiteral("x")).toDouble(0);
        qreal yPos = params.value(QStringLiteral("y")).toDouble(0);

        QLCFixtureDef *rowDef = nullptr;
        QLCFixtureMode *rowMode = nullptr;
        QJsonArray fixturesJson, fixtureIdsJson;
        for (int i = 0; i < rows; i++)
        {
            Fixture *fxi = new Fixture(doc);
            fxi->setName(QStringLiteral("%1 - Row %2").arg(name).arg(i + 1));
            if (rowDef == nullptr)
                rowDef = fxi->genericRGBPanelDef(columns, components, false);
            if (rowMode == nullptr)
                rowMode = fxi->genericRGBPanelMode(rowDef, components, false, quint32(phyWidth), quint32(phyHeight));
            fxi->setFixtureDefinition(rowDef, rowMode);
            fxi->setUniverse(placement.at(i).first);
            fxi->setAddress(quint32(placement.at(i).second));
            if (doc->addFixture(fxi) == false)
            {
                delete fxi;
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                                QStringLiteral("Could not add row %1").arg(i + 1)));
                return;
            }

            // Head placement in the group grid: zig-zag runs every row the
            // same way, snake reverses every other row.
            bool forward = snake == false || i % 2 == 0;
            int x = forward ? xPosStart : xPosEnd;
            int inc = forward ? xPosInc : -xPosInc;
            for (int h = 0; h < fxi->heads(); h++)
            {
                if (transpose)
                    grp->assignHead(QLCPoint(currRow, x), GroupHead(fxi->id(), h));
                else
                    grp->assignHead(QLCPoint(x, currRow), GroupHead(fxi->id(), h));
                x += inc;
            }

            // 2D/3D placement, same maths as FixtureManager::addRGBPanel().
            QVector3D pos;
            QVector3D rot(0, 0, 0);
            bool odd = snake && (i % 2);
            switch (monProps->pointOfView())
            {
                case MonitorProperties::TopView:
                    pos = QVector3D(xPos, 1000, yPos);
                    if (odd) rot.setY(180);
                break;
                case MonitorProperties::LeftSideView:
                    pos = QVector3D(0, yPos, xPos);
                    rot.setY(odd ? -90 : 90);
                    rot.setZ(-90);
                break;
                case MonitorProperties::RightSideView:
                    pos = QVector3D(0, yPos, (monProps->gridSize().z() * gridUnits) - xPos);
                    rot.setY(odd ? 90 : -90);
                    rot.setZ(90);
                break;
                default:
                    pos = QVector3D(xPos, (monProps->gridSize().y() * gridUnits) - yPos, 0);
                    if (odd) rot.setZ(180);
                    rot.setX(-90);
                break;
            }
            monProps->setFixturePosition(fxi->id(), 0, 0, pos);
            monProps->setFixtureRotation(fxi->id(), 0, 0, rot);
            yPos += phyHeight;
            currRow += rowInc;

            fixtureIdsJson.append(QString::number(fxi->id()));
            fixturesJson.append(fixtureSummaryToJson(fxi));
        }
        doc->setModified();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("fixtureIds"), fixtureIdsJson);
        result.insert(QStringLiteral("groupId"), QString::number(grp->id()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        data.insert(QStringLiteral("fixtures"), fixturesJson);
        m_server->broadcast(QStringLiteral("fixtures.patched"), data, session->clientId(), false);

        // The group is built here, not by fixtures.group.create, so its
        // event is sent here too (heads included).
        QJsonObject groupJson;
        groupJson.insert(QStringLiteral("id"), QString::number(grp->id()));
        groupJson.insert(QStringLiteral("name"), grp->name());
        QJsonObject size;
        size.insert(QStringLiteral("columns"), grp->size().width());
        size.insert(QStringLiteral("rows"), grp->size().height());
        groupJson.insert(QStringLiteral("size"), size);
        QJsonArray heads;
        QMap<QLCPoint, GroupHead> map = grp->headsMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        {
            QJsonObject h;
            h.insert(QStringLiteral("x"), it.key().x());
            h.insert(QStringLiteral("y"), it.key().y());
            h.insert(QStringLiteral("fixtureId"), QString::number(it.value().fxi));
            h.insert(QStringLiteral("headIndex"), it.value().head);
            heads.append(h);
        }
        groupJson.insert(QStringLiteral("heads"), heads);
        QJsonObject groupData;
        groupData.insert(QStringLiteral("group"), groupJson);
        groupData.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.group.created"), groupData, session->clientId(), false);
    });
}
