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
#include "doc.h"

namespace {

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
        channelList.append(chObj);
    }
    obj.insert(QStringLiteral("channelList"), channelList);

    return obj;
}

// address (0-based, within a universe) .. address+channels-1 must all fit in
// a single universe's 512-channel space - Fixture/Doc's own absolute
// addressing (universe<<9 | address) has no overflow guard of its own (an
// out-of-range footprint silently encroaches into the next universe's own
// address 0 instead of erroring), so this must be checked here.
bool validAddressRange(int address, int channels)
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
            int addr = requested + n * (channels + gap);
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
            int blockSize = channels * quantity + gap * quantity;
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
            int addr = baseAddress + n * (channelCount + gap);
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
            int addr = baseAddress + n * (channelCount + gap);
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
        if (hasUniverse == false && hasAddress == false && hasName == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("At least one of universe/address/name must be given")));
            return;
        }

        quint32 newUniverse = hasUniverse ? quint32(params.value(QStringLiteral("universe")).toInt())
                                           : fixture->universe();
        int newAddress = hasAddress ? params.value(QStringLiteral("address")).toInt()
                                     : int(fixture->address());

        if (hasUniverse || hasAddress)
        {
            if (validAddressRange(newAddress, int(fixture->channels())) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("Address %1 does not fit this fixture's %2 channels in a 512-channel universe")
                        .arg(newAddress).arg(fixture->channels())));
                return;
            }
            if (doc->inputOutputMap()->universe(newUniverse) == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such universe")));
                return;
            }
            if (rangeIsFree(doc, newUniverse, quint32(newAddress), fixture->channels(), fixture->id()) == false)
            {
                QJsonObject details;
                details.insert(QStringLiteral("address"), newAddress);
                session->send(ApiEnvelope::buildErrorResponse(id, QStringLiteral("FIXTURES_ADDRESS_OVERLAP"),
                    QStringLiteral("Address %1 overlaps an existing fixture").arg(newAddress), details));
                return;
            }
        }

        // Fixture::setAddress()/setUniverse() are two separate setters, each
        // independently emitting changed(), and Doc::slotFixtureChanged()
        // re-tracks this fixture's occupied addresses on every single call -
        // when both are changing there's an unavoidable transient step where
        // the fixture is briefly tracked at (new address, old universe) [this
        // order] or (old address, new universe) [the other order] before the
        // second call lands, matching a documented existing quirk in
        // Doc::slotFixtureChanged() (doc.cpp). The overlap check above only
        // guards the final state; a pathological transient collision with
        // some other real fixture during this brief window is a pre-existing
        // engine limitation, not something introduced or fixable here.
        if (hasAddress)
            fixture->setAddress(quint32(newAddress));
        if (hasUniverse)
            fixture->setUniverse(newUniverse);
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
}
