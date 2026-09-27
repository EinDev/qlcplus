/*
  Q Light Controller Plus - Control API
  apiiodomain.cpp

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
#include <QSet>

#include "apiiodomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "inputoutputmap.h"
#include "ioplugincache.h"
#include "qlcioplugin.h"
#include "qlcinputprofile.h"
#include "grandmaster.h"
#include "outputpatch.h"
#include "inputpatch.h"
#include "universe.h"
#include "genericfader.h"
#include "fadechannel.h"
#include "qlcchannel.h"
#include "fixture.h"
#include "scene.h"
#include "mastertimer.h"
#include "keypadparser.h"
#include "doc.h"

namespace {

QJsonObject pluginParametersToJson(const QMap<QString, QVariant> &parameters)
{
    QJsonObject obj;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it)
        obj.insert(it.key(), QJsonValue::fromVariant(it.value()));
    return obj;
}

QJsonValue inputPatchToJson(InputPatch *patch)
{
    if (patch == nullptr)
        return QJsonValue();

    QJsonObject obj;
    obj.insert(QStringLiteral("pluginName"), patch->pluginName());
    obj.insert(QStringLiteral("input"), int(patch->input()));
    obj.insert(QStringLiteral("inputUID"), patch->inputUID());
    obj.insert(QStringLiteral("inputName"), patch->inputName());
    // InputPatch::profileName() returns the translated "None" placeholder
    // when unset (see KInputNone in inputpatch.h) rather than an empty
    // string - check the actual profile pointer so the spec's
    // [string, null] contract gets a real null instead of that placeholder.
    obj.insert(QStringLiteral("profileName"), patch->profile() != nullptr
               ? QJsonValue(patch->profileName())
               : QJsonValue());
    obj.insert(QStringLiteral("parameters"), pluginParametersToJson(patch->getPluginParameters()));
    return obj;
}

QJsonObject outputPatchToJson(OutputPatch *patch, int index)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("index"), index);
    obj.insert(QStringLiteral("pluginName"), patch->pluginName());
    obj.insert(QStringLiteral("output"), int(patch->output()));
    obj.insert(QStringLiteral("outputUID"), patch->outputUID());
    obj.insert(QStringLiteral("outputName"), patch->outputName());
    obj.insert(QStringLiteral("parameters"), pluginParametersToJson(patch->getPluginParameters()));
    obj.insert(QStringLiteral("paused"), patch->paused());
    obj.insert(QStringLiteral("blackout"), patch->blackout());
    return obj;
}

QJsonObject universeSummaryToJson(Universe *universe)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), int(universe->id()));
    obj.insert(QStringLiteral("name"), universe->name());
    obj.insert(QStringLiteral("passthrough"), universe->passthrough());
    obj.insert(QStringLiteral("monitor"), universe->monitor());
    obj.insert(QStringLiteral("usedChannels"), universe->usedChannels());
    obj.insert(QStringLiteral("totalChannels"), universe->totalChannels());
    obj.insert(QStringLiteral("isPatched"), universe->isPatched());
    obj.insert(QStringLiteral("inputPatched"), universe->inputPatch() != nullptr);
    obj.insert(QStringLiteral("outputPatchesCount"), universe->outputPatchesCount());
    obj.insert(QStringLiteral("hasFeedback"), universe->hasFeedback());
    return obj;
}

QJsonObject universeDetailToJson(Universe *universe)
{
    QJsonObject obj = universeSummaryToJson(universe);

    obj.insert(QStringLiteral("inputPatch"), inputPatchToJson(universe->inputPatch()));

    QJsonArray outputPatches;
    for (int i = 0; i < universe->outputPatchesCount(); i++)
    {
        OutputPatch *patch = universe->outputPatch(i);
        if (patch != nullptr)
            outputPatches.append(outputPatchToJson(patch, i));
    }
    obj.insert(QStringLiteral("outputPatches"), outputPatches);

    OutputPatch *feedback = universe->feedbackPatch();
    obj.insert(QStringLiteral("feedbackPatch"), feedback != nullptr
               ? QJsonValue(outputPatchToJson(feedback, 0))
               : QJsonValue());

    return obj;
}

// Spec strings (docs/api-spec/fragments/io.yaml IoGrandMasterChannelMode/
// IoGrandMasterValueMode) deliberately spelled out here rather than reusing
// GrandMaster::channelModeToString()/valueModeToString(): those serialize
// the .qxw persistence form, which for AllChannels is the abbreviated "All"
// (see KXMLQLCGMChannelModeAllChannels in grandmaster.cpp) - not the spec's
// "AllChannels".
QString grandMasterChannelModeToJson(GrandMaster::ChannelMode mode)
{
    switch (mode)
    {
    case GrandMaster::AllChannels:
        return QStringLiteral("AllChannels");
    default:
    case GrandMaster::Intensity:
        return QStringLiteral("Intensity");
    }
}

QString grandMasterValueModeToJson(GrandMaster::ValueMode mode)
{
    switch (mode)
    {
    case GrandMaster::Reduce:
        return QStringLiteral("Reduce");
    default:
    case GrandMaster::Limit:
        return QStringLiteral("Limit");
    }
}

QJsonObject grandMasterStateToJson(InputOutputMap *ioMap)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("value"), int(ioMap->grandMasterValue()));
    obj.insert(QStringLiteral("channelMode"), grandMasterChannelModeToJson(ioMap->grandMasterChannelMode()));
    obj.insert(QStringLiteral("valueMode"), grandMasterValueModeToJson(ioMap->grandMasterValueMode()));
    return obj;
}

/*********************************************************************
 * Plugins / patches (io.plugin.list, io.patch.set/remove)
 *********************************************************************/

// QLCIOPlugin::Capability bitmask -> io.yaml IoPluginSummary.capabilities
QJsonArray pluginCapabilitiesToJson(int capabilities)
{
    QJsonArray arr;
    if (capabilities & QLCIOPlugin::Output)   arr.append(QStringLiteral("Output"));
    if (capabilities & QLCIOPlugin::Input)    arr.append(QStringLiteral("Input"));
    if (capabilities & QLCIOPlugin::Feedback) arr.append(QStringLiteral("Feedback"));
    if (capabilities & QLCIOPlugin::Infinite) arr.append(QStringLiteral("Infinite"));
    if (capabilities & QLCIOPlugin::RDM)      arr.append(QStringLiteral("RDM"));
    if (capabilities & QLCIOPlugin::Beats)    arr.append(QStringLiteral("Beats"));
    return arr;
}

// One plugin line list. "index" is the web UI contract's spelling, "line"
// io.yaml IoPluginLine's - both carry the same 0-based plugin line number
// that io.patch.set takes as "line".
QJsonArray pluginLinesToJson(const QStringList &names, const QStringList &uids)
{
    QJsonArray arr;
    for (int i = 0; i < names.count(); i++)
    {
        QJsonObject line;
        line.insert(QStringLiteral("index"), i);
        line.insert(QStringLiteral("line"), i);
        line.insert(QStringLiteral("name"), names.at(i));
        line.insert(QStringLiteral("uid"), i < uids.count() ? uids.at(i) : QString());
        arr.append(line);
    }
    return arr;
}

// IoPluginSummary + the contract's inline inputLines/outputLines (so a
// patch dialog needs one round-trip, not one per plugin).
QJsonObject pluginToJson(QLCIOPlugin *plugin)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), plugin->name());
    obj.insert(QStringLiteral("capabilities"), pluginCapabilitiesToJson(plugin->capabilities()));
    obj.insert(QStringLiteral("description"), plugin->pluginInfo());
    obj.insert(QStringLiteral("canConfigure"), plugin->canConfigure());
    obj.insert(QStringLiteral("supportsFeedback"), (plugin->capabilities() & QLCIOPlugin::Feedback) != 0);
    obj.insert(QStringLiteral("inputLines"), pluginLinesToJson(plugin->inputs(), plugin->inputsUID()));
    obj.insert(QStringLiteral("outputLines"), pluginLinesToJson(plugin->outputs(), plugin->outputsUID()));
    return obj;
}

QJsonObject inputProfileSummaryToJson(QLCInputProfile *profile)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), profile->name());
    obj.insert(QStringLiteral("manufacturer"), profile->manufacturer());
    obj.insert(QStringLiteral("model"), profile->model());
    obj.insert(QStringLiteral("type"), QLCInputProfile::typeToString(profile->type()));
    return obj;
}

enum PatchDirection { PatchInput, PatchOutput, PatchFeedback };

// io.patch.set/remove's "direction" (web UI contract) / "patchType"
// (io.yaml) - either spelling, same three values.
bool parsePatchDirection(const QJsonObject &params, PatchDirection &direction)
{
    QString str = params.value(QStringLiteral("direction")).toString();
    if (str.isEmpty())
        str = params.value(QStringLiteral("patchType")).toString();
    if (str == QStringLiteral("input"))         direction = PatchInput;
    else if (str == QStringLiteral("output"))   direction = PatchOutput;
    else if (str == QStringLiteral("feedback")) direction = PatchFeedback;
    else return false;
    return true;
}

// First present of several alias spellings (contract vs io.yaml), as string.
QString stringParam(const QJsonObject &params, const char *primary, const char *alias)
{
    QJsonValue value = params.value(QLatin1String(primary));
    if (value.isUndefined())
        value = params.value(QLatin1String(alias));
    return value.toString();
}

// §4a check for the io.universe.update/delete and io.patch.* methods added
// for the web UI. Unlike io.universe.create (which requires it), these
// enforce baseRevision only when the client sends one: the web UI contract
// lists none for them, and rejecting every such call with CONFLICT would
// make the methods unusable for a client written to that contract.
// Documented as a deliberate deviation in io-notes.md.
bool baseRevisionAccepted(Doc *doc, ApiSession *session, const QString &id, const QJsonObject &params)
{
    if (params.contains(QStringLiteral("baseRevision")) == false)
        return true;
    quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
    if (baseRevision == doc->docRevision())
        return true;
    QJsonObject details;
    details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                    QStringLiteral("baseRevision is stale"), details));
    return false;
}

// Shared universeId lookup for the same methods: NOT_FOUND (matching
// io.universe.get) for a missing/invalid id.
Universe *findUniverseParam(Doc *doc, ApiSession *session, const QString &id, const QJsonObject &params)
{
    int universeId = params.value(QStringLiteral("universeId")).toInt(-1);
    Universe *universe = universeId >= 0 ? doc->inputOutputMap()->universe(quint32(universeId)) : nullptr;
    if (universe == nullptr)
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No such universe")));
    return universe;
}

} // namespace

ApiIoDomain::ApiIoDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , DMXSource()
    , m_doc(doc)
    , m_server(server)
    , m_keyPadParser(new KeyPadParser())
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods();

    m_doc->masterTimer()->registerDMXSource(this);

    // Old-style string-based connect() deliberately, not the modern pointer
    // syntax: InputOutputMap/Universe live in qlcplusengine.dll, and this
    // class lives in a separate static library (qlcplusapiserver) linked
    // into whatever finally consumes it - referencing their staticMetaObject
    // as a data symbol across that boundary-through-a-static-lib is exactly
    // the MinGW auto-import edge case the codebase's own qmlui/contextmanager.cpp
    // and qmlui/simpledesk.cpp already sidestep by using SIGNAL()/SLOT() for
    // these same two signals - confirmed by reproducing the "signal not
    // found" runtime warning with pointer syntax and it disappearing here.
    InputOutputMap *ioMap = m_doc->inputOutputMap();
    connect(ioMap, SIGNAL(universeAdded(quint32)), this, SLOT(slotUniverseAdded(quint32)));
    connect(ioMap, SIGNAL(universeRemoved(quint32)), this, SLOT(slotUniverseRemoved(quint32)));
    connect(ioMap, SIGNAL(grandMasterValueChanged(uchar)), this, SLOT(slotGrandMasterValueChanged(uchar)));
    connect(ioMap, SIGNAL(blackoutChanged(bool)), this, SLOT(slotBlackoutChanged(bool)));

    for (Universe *universe : ioMap->universes())
        watchUniverse(universe);
}

ApiIoDomain::~ApiIoDomain()
{
    m_doc->masterTimer()->unregisterDMXSource(this);
    delete m_keyPadParser;
}

void ApiIoDomain::watchUniverse(Universe *universe)
{
    connect(universe, SIGNAL(universeWritten(quint32,QByteArray)), this, SLOT(slotUniverseWritten(quint32,QByteArray)));
}

void ApiIoDomain::slotUniverseAdded(quint32 id)
{
    Universe *universe = m_doc->inputOutputMap()->universe(id);
    if (universe == nullptr)
        return;

    if (m_hasPendingUniverseName)
    {
        universe->setName(m_pendingUniverseName);
        m_hasPendingUniverseName = false;
        m_pendingUniverseName.clear();
    }

    watchUniverse(universe);

    QJsonObject data;
    data.insert(QStringLiteral("universe"), universeDetailToJson(universe));
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    // Structural (§4a): always delivered, not subscribe-gated.
    m_server->broadcast(QStringLiteral("io.universe.created"), data, m_pendingOriginClientId, false);
}

void ApiIoDomain::slotUniverseRemoved(quint32 id)
{
    m_lastUniverseSnapshot.remove(id);
    {
        // The Universe (and with it the GenericFader our shared pointer
        // co-owns) is gone - drop our side too, plus any held Simple Desk
        // values addressed into it, so a later universe with the same id
        // starts clean. writeDMX() already bounds-checks universe ids
        // against the live list, so this is hygiene, not a crash fix.
        QMutexLocker locker(&m_simpleDeskMutex);
        m_simpleDeskFaders.remove(id);
        QMutableHashIterator<quint32, uchar> it(m_simpleDeskValues);
        while (it.hasNext())
        {
            it.next();
            if ((it.key() >> 9) == id)
                it.remove();
        }
    }

    QJsonObject data;
    data.insert(QStringLiteral("universeId"), int(id));
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("io.universe.deleted"), data, m_pendingOriginClientId, false);
}

void ApiIoDomain::broadcastUniverseUpdated(Universe *universe, const QString &originClientId)
{
    QJsonObject data;
    data.insert(QStringLiteral("universe"), universeDetailToJson(universe));
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("io.universe.updated"), data, originClientId, false);
}

void ApiIoDomain::slotUniverseWritten(quint32 id, const QByteArray &postGMValues)
{
    QString topic = QStringLiteral("io.dmx.universe.%1.changed").arg(id);

    QByteArray &previous = m_lastUniverseSnapshot[id];
    QJsonArray changes;
    int max = qMax(previous.size(), postGMValues.size());
    for (int channel = 0; channel < max; channel++)
    {
        uchar oldValue = channel < previous.size() ? uchar(previous.at(channel)) : 0;
        uchar newValue = channel < postGMValues.size() ? uchar(postGMValues.at(channel)) : 0;
        if (oldValue != newValue)
        {
            QJsonObject change;
            change.insert(QStringLiteral("channel"), channel);
            change.insert(QStringLiteral("value"), int(newValue));
            changes.append(change);
        }
    }
    previous = postGMValues;

    if (changes.isEmpty())
        return;

    QJsonObject data;
    data.insert(QStringLiteral("universeId"), int(id));
    data.insert(QStringLiteral("changes"), changes);
    // High-frequency (§5): subscribe-gated, unlike every other event here.
    m_server->broadcast(topic, data, QString(), true);
}

void ApiIoDomain::slotGrandMasterValueChanged(uchar value)
{
    Q_UNUSED(value)
    // IoGrandMasterState requires channelMode/valueMode alongside value, so
    // read the full current state from the map rather than just relaying the
    // signal's own parameter.
    QJsonObject data = grandMasterStateToJson(m_doc->inputOutputMap());
    // Live (§4b) but low-frequency: always delivered, not subscribe-gated.
    m_server->broadcast(QStringLiteral("io.grandMaster.changed"), data, m_pendingOriginClientId, false);
}

void ApiIoDomain::slotBlackoutChanged(bool blackout)
{
    QJsonObject data;
    data.insert(QStringLiteral("blackout"), blackout);
    m_server->broadcast(QStringLiteral("io.blackout.changed"), data, m_pendingOriginClientId, false);
}

void ApiIoDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("io.universe.list"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonArray universes;
        for (Universe *universe : doc->inputOutputMap()->universes())
            universes.append(universeSummaryToJson(universe));

        QJsonObject result;
        result.insert(QStringLiteral("universes"), universes);
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("io.universe.get"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 universeId = quint32(params.value(QStringLiteral("universeId")).toInt());
        Universe *universe = doc->inputOutputMap()->universe(universeId);
        if (universe == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such universe")));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, universeDetailToJson(universe)));
    });

    dispatcher->registerMethod(QStringLiteral("io.universe.create"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
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

        // InputOutputMap::addUniverse() (with no explicit id, as here) always
        // assigns the next sequential id - universesCount() before the call.
        quint32 newUniverseId = doc->inputOutputMap()->universesCount();

        if (params.value(QStringLiteral("name")).isString())
        {
            m_pendingUniverseName = params.value(QStringLiteral("name")).toString();
            m_hasPendingUniverseName = true;
        }

        // universeAdded (relayed to Doc::setModified()/bumpRevision(), and to
        // ApiIoDomain's own broadcast via slotUniverseAdded, which also
        // applies m_pendingUniverseName if set above) fires synchronously
        // within addUniverse() since InputOutputMap and Doc share this thread -
        // doc->docRevision() below already reflects the new value.
        m_pendingOriginClientId = session->clientId();
        bool added = doc->inputOutputMap()->addUniverse();
        m_pendingOriginClientId.clear();

        if (added == false)
        {
            m_hasPendingUniverseName = false;
            m_pendingUniverseName.clear();
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                                                            QStringLiteral("Could not add a universe")));
            return;
        }

        QJsonObject result;
        result.insert(QStringLiteral("universeId"), int(newUniverseId));
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // io.universe.update {universeId, name?, passthrough?, baseRevision?} -> {docRevision}
    dispatcher->registerMethod(QStringLiteral("io.universe.update"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (baseRevisionAccepted(doc, session, id, params) == false)
            return;
        Universe *universe = findUniverseParam(doc, session, id, params);
        if (universe == nullptr)
            return;

        QJsonValue name = params.value(QStringLiteral("name"));
        QJsonValue passthrough = params.value(QStringLiteral("passthrough"));
        bool hasName = name.isString();
        bool hasPassthrough = passthrough.isBool();
        if (hasName == false && hasPassthrough == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("At least one of name (string) or passthrough (boolean) is required")));
            return;
        }

        int index = int(universe->id());
        if (hasName)
            doc->inputOutputMap()->setUniverseName(index, name.toString());
        if (hasPassthrough)
            doc->inputOutputMap()->setUniversePassthrough(index, passthrough.toBool());
        // Neither setter touches Doc (qmlui's InputOutputManager calls
        // setModified() itself after them) - both are saved to the .qxw.
        doc->setModified();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
        broadcastUniverseUpdated(universe, session->clientId());
    });

    // io.universe.delete {universeId, force?, baseRevision?} -> {docRevision}
    dispatcher->registerMethod(QStringLiteral("io.universe.delete"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (baseRevisionAccepted(doc, session, id, params) == false)
            return;
        Universe *universe = findUniverseParam(doc, session, id, params);
        if (universe == nullptr)
            return;

        InputOutputMap *ioMap = doc->inputOutputMap();
        quint32 universeId = universe->id();
        if (ioMap->universesCount() <= 1)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            QStringLiteral("The last universe cannot be deleted")));
            return;
        }
        // InputOutputMap::removeUniverse() refuses anything that would leave
        // a gap in the (index == id) universe list - surface that as a
        // parameter problem rather than the engine's silent false.
        if (universeId != ioMap->universesCount() - 1)
        {
            QJsonObject details;
            details.insert(QStringLiteral("deletableUniverseId"), int(ioMap->universesCount() - 1));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("Only the highest-numbered universe can be deleted (universe ids must stay contiguous)"),
                details));
            return;
        }

        // Fixtures patched into it: qmlui deletes them silently along with
        // the universe (InputOutputManager::removeLastUniverse()). Over the
        // API that is too destructive to be implicit - refuse unless the
        // client says force:true, in which case they are unpatched first
        // (with the fixtures domain's own event, so every client learns).
        QList<quint32> fixtureIds;
        for (Fixture *fixture : doc->fixtures())
            if (fixture->universe() == universeId)
                fixtureIds.append(fixture->id());
        if (fixtureIds.isEmpty() == false && params.value(QStringLiteral("force")).toBool(false) == false)
        {
            QJsonArray idsJson;
            for (quint32 fxId : std::as_const(fixtureIds))
                idsJson.append(QString::number(fxId));
            QJsonObject details;
            details.insert(QStringLiteral("fixtureIds"), idsJson);
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                QStringLiteral("Universe still has %1 fixture(s) patched into it; unpatch them or pass force:true")
                    .arg(fixtureIds.count()),
                details));
            return;
        }

        if (fixtureIds.isEmpty() == false)
        {
            QJsonArray deletedIdsJson;
            for (quint32 fxId : std::as_const(fixtureIds))
            {
                doc->deleteFixture(fxId);
                deletedIdsJson.append(QString::number(fxId));
            }
            QJsonObject data;
            data.insert(QStringLiteral("fixtureIds"), deletedIdsJson);
            data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            m_server->broadcast(QStringLiteral("fixtures.unpatched"), data, session->clientId(), false);
        }

        // universeRemoved fires synchronously inside removeUniverse():
        // Doc::setModified() (connected first, in Doc's ctor) bumps the
        // revision, then slotUniverseRemoved() broadcasts io.universe.deleted.
        m_pendingOriginClientId = session->clientId();
        bool removed = ioMap->removeUniverse(int(universeId));
        m_pendingOriginClientId.clear();
        if (removed == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                            QStringLiteral("The engine refused to remove the universe")));
            return;
        }

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // io.plugin.list {} -> {plugins: [IoPluginSummary + inputLines/outputLines]}
    dispatcher->registerMethod(QStringLiteral("io.plugin.list"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonArray plugins;
        for (QLCIOPlugin *plugin : doc->ioPluginCache()->plugins())
            plugins.append(pluginToJson(plugin));
        QJsonObject result;
        result.insert(QStringLiteral("plugins"), plugins);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // io.inputProfile.list {} -> {profiles: [{name, manufacturer, model, type}], profilesRevision}
    dispatcher->registerMethod(QStringLiteral("io.inputProfile.list"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        InputOutputMap *ioMap = doc->inputOutputMap();
        QJsonArray profiles;
        for (const QString &name : ioMap->profileNames())
        {
            QLCInputProfile *profile = ioMap->profile(name);
            if (profile != nullptr)
                profiles.append(inputProfileSummaryToJson(profile));
        }
        QJsonObject result;
        result.insert(QStringLiteral("profiles"), profiles);
        // §4c library counter, bumped by ApiIoConfigDomain's
        // io.inputProfile.save/delete (apiioconfigdomain.cpp).
        result.insert(QStringLiteral("profilesRevision"), int(profilesRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // io.patch.set {universeId, direction|patchType, plugin|pluginName, line,
    //               profile|profileName?, index?, baseRevision?} -> {docRevision}
    dispatcher->registerMethod(QStringLiteral("io.patch.set"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (baseRevisionAccepted(doc, session, id, params) == false)
            return;
        Universe *universe = findUniverseParam(doc, session, id, params);
        if (universe == nullptr)
            return;

        PatchDirection direction;
        if (parsePatchDirection(params, direction) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("direction must be one of input, output, feedback")));
            return;
        }

        QString pluginName = stringParam(params, "plugin", "pluginName");
        QLCIOPlugin *plugin = doc->ioPluginCache()->plugin(pluginName);
        if (plugin == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such plugin")));
            return;
        }

        int requiredCapability = direction == PatchInput ? QLCIOPlugin::Input
                               : direction == PatchOutput ? QLCIOPlugin::Output
                               : QLCIOPlugin::Feedback;
        if ((plugin->capabilities() & requiredCapability) == 0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                QStringLiteral("Plugin \"%1\" does not support %2 lines").arg(pluginName,
                    direction == PatchInput ? QStringLiteral("input")
                    : direction == PatchOutput ? QStringLiteral("output") : QStringLiteral("feedback"))));
            return;
        }

        QJsonValue lineValue = params.value(QStringLiteral("line"));
        int line = lineValue.isDouble() ? lineValue.toInt(-1) : -1;
        // Plugins advertising Infinite (e.g. loopback-style ones) accept any
        // line number; everything else must name a line it actually lists.
        int lineCount = direction == PatchInput ? plugin->inputs().count() : plugin->outputs().count();
        if (line < 0 || ((plugin->capabilities() & QLCIOPlugin::Infinite) == 0 && line >= lineCount))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("line must be a plugin line index in 0..%1").arg(qMax(0, lineCount - 1))));
            return;
        }

        // Input profile, three cases (matching the web UI's expectations of
        // QML's setInputProfile(): a line change re-sends the current
        // profile, an explicit "" clears it): key absent -> keep whatever
        // profile the current input patch has; "" -> none; name -> must exist.
        bool hasProfile = params.contains(QStringLiteral("profile")) || params.contains(QStringLiteral("profileName"));
        QString profileName = stringParam(params, "profile", "profileName");
        if (direction == PatchInput && hasProfile == false &&
            universe->inputPatch() != nullptr && universe->inputPatch()->profile() != nullptr)
            profileName = universe->inputPatch()->profileName();
        if (direction == PatchInput && profileName.isEmpty() == false &&
            doc->inputOutputMap()->profile(profileName) == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such input profile")));
            return;
        }

        // Output patch slot. The contract has no index (one output per
        // universe in mind), so the default is 0 = replace/create the
        // primary patch; io.yaml's "omit to append" is reachable by passing
        // index == outputPatchesCount explicitly.
        int outputIndex = params.value(QStringLiteral("index")).toInt(0);
        if (direction == PatchOutput && (outputIndex < 0 || outputIndex > universe->outputPatchesCount()))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("index must be in 0..%1").arg(universe->outputPatchesCount())));
            return;
        }

        InputOutputMap *ioMap = doc->inputOutputMap();
        quint32 universeId = universe->id();
        bool ok = false;
        switch (direction)
        {
        case PatchInput:
            ok = ioMap->setInputPatch(universeId, pluginName, QString(), QString(), quint32(line), profileName);
            break;
        case PatchOutput:
            ok = ioMap->setOutputPatch(universeId, pluginName, QString(), QString(), quint32(line), false, outputIndex);
            break;
        case PatchFeedback:
            ok = ioMap->setOutputPatch(universeId, pluginName, QString(), QString(), quint32(line), true);
            break;
        }
        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                QStringLiteral("The plugin refused to open the line (see the engine log)")));
            return;
        }

        doc->setModified(); // patches are saved to the .qxw; the engine doesn't flag this itself

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
        broadcastUniverseUpdated(universe, session->clientId());
    });

    // io.patch.remove {universeId, direction|patchType, index?, baseRevision?} -> {docRevision}
    dispatcher->registerMethod(QStringLiteral("io.patch.remove"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (baseRevisionAccepted(doc, session, id, params) == false)
            return;
        Universe *universe = findUniverseParam(doc, session, id, params);
        if (universe == nullptr)
            return;

        PatchDirection direction;
        if (parsePatchDirection(params, direction) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("direction must be one of input, output, feedback")));
            return;
        }

        InputOutputMap *ioMap = doc->inputOutputMap();
        quint32 universeId = universe->id();
        // An unknown/empty plugin name resolves to a null plugin, which
        // together with invalidLine() is the engine's own "remove" spelling
        // (Universe::setInputPatch/setOutputPatch/setFeedbackPatch).
        const QString none;
        switch (direction)
        {
        case PatchInput:
            if (universe->inputPatch() == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("Universe has no input patch")));
                return;
            }
            ioMap->setInputPatch(universeId, none, QString(), QString(), QLCIOPlugin::invalidLine());
            // A feedback line is meaningless without its input - drop it
            // too, as InputOutputManager::removeInputPatch() does.
            if (universe->feedbackPatch() != nullptr)
                ioMap->setOutputPatch(universeId, none, QString(), QString(), QLCIOPlugin::invalidLine(), true);
            break;
        case PatchOutput:
        {
            int count = universe->outputPatchesCount();
            if (count == 0)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("Universe has no output patch")));
                return;
            }
            if (params.contains(QStringLiteral("index")))
            {
                int index = params.value(QStringLiteral("index")).toInt(-1);
                if (index < 0 || index >= count)
                {
                    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                        QStringLiteral("No output patch at index %1").arg(index)));
                    return;
                }
                ioMap->setOutputPatch(universeId, none, QString(), QString(), QLCIOPlugin::invalidLine(), false, index);
            }
            else
            {
                // No index (contract form): remove every output patch,
                // highest first so indices stay valid while removing.
                for (int index = count - 1; index >= 0; index--)
                    ioMap->setOutputPatch(universeId, none, QString(), QString(), QLCIOPlugin::invalidLine(), false, index);
            }
            break;
        }
        case PatchFeedback:
            if (universe->feedbackPatch() == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("Universe has no feedback patch")));
                return;
            }
            ioMap->setOutputPatch(universeId, none, QString(), QString(), QLCIOPlugin::invalidLine(), true);
            break;
        }

        doc->setModified();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
        broadcastUniverseUpdated(universe, session->clientId());
    });

    dispatcher->registerMethod(QStringLiteral("io.grandMaster.get"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        session->send(ApiEnvelope::buildOkResponse(id, grandMasterStateToJson(doc->inputOutputMap())));
    });

    dispatcher->registerMethod(QStringLiteral("io.grandMaster.setValue"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        int value = qBound(0, params.value(QStringLiteral("value")).toInt(), 255);
        // setGrandMasterValue() is a no-op (and never emits) when value
        // already matches the current one, so clear the pending origin
        // again right after the call rather than relying on the slot to -
        // it may never run. See m_pendingOriginClientId's own comment.
        m_pendingOriginClientId = session->clientId();
        doc->inputOutputMap()->setGrandMasterValue(uchar(value));
        m_pendingOriginClientId.clear();
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    dispatcher->registerMethod(QStringLiteral("io.blackout.get"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonObject result;
        result.insert(QStringLiteral("blackout"), doc->inputOutputMap()->blackout());
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("io.blackout.set"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        m_pendingOriginClientId = session->clientId();
        doc->inputOutputMap()->setBlackout(params.value(QStringLiteral("blackout")).toBool());
        m_pendingOriginClientId.clear();
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    dispatcher->registerMethod(QStringLiteral("io.blackout.toggle"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        m_pendingOriginClientId = session->clientId();
        bool newState = doc->inputOutputMap()->toggleBlackout();
        m_pendingOriginClientId.clear();
        QJsonObject result;
        result.insert(QStringLiteral("blackout"), newState);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("io.dmx.universe.get"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 universeId = quint32(params.value(QStringLiteral("universeId")).toInt());
        Universe *universe = doc->inputOutputMap()->universe(universeId);
        if (universe == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such universe")));
            return;
        }

        const QByteArray *values = universe->postGMValues();
        QJsonArray jsonValues;
        for (int i = 0; i < values->size(); i++)
            jsonValues.append(int(uchar(values->at(i))));

        QJsonObject result;
        result.insert(QStringLiteral("universeId"), int(universeId));
        result.insert(QStringLiteral("values"), jsonValues);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("io.simpleDesk.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 universeId = quint32(params.value(QStringLiteral("universeId")).toInt());
        if (m_doc->inputOutputMap()->universe(universeId) == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such universe")));
            return;
        }

        QJsonArray channels;
        {
            QMutexLocker locker(&m_simpleDeskMutex);
            quint32 start = universeId * 512;
            for (auto it = m_simpleDeskValues.constBegin(); it != m_simpleDeskValues.constEnd(); ++it)
            {
                if (it.key() >= start && it.key() < start + 512)
                    channels.append(simpleDeskChannelToJson(it.key()));
            }
        }

        QJsonObject result;
        result.insert(QStringLiteral("universeId"), int(universeId));
        result.insert(QStringLiteral("channels"), channels);
        // slidersNumber/currentPage mirror qmlui SimpleDesk's own channel-view
        // pagination (PreviewContext), a UI-viewport concept this headless
        // domain has no equivalent state for - reporting 0/0 rather than
        // omitting the (required) fields. Revisit if a client ever needs
        // this for anything beyond satisfying the schema.
        result.insert(QStringLiteral("slidersNumber"), 0);
        result.insert(QStringLiteral("currentPage"), 0);
        // Server-side keypad history (io.simpleDesk.sendKeypadCommand), so a
        // client can seed its list on load and then follow
        // io.simpleDesk.commandHistoryChanged. Spec extension, see io-notes.md.
        result.insert(QStringLiteral("commandHistory"), QJsonArray::fromStringList(m_keypadCommandHistory));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("io.simpleDesk.setChannel"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 address = quint32(params.value(QStringLiteral("address")).toInt());
        int value = qBound(0, params.value(QStringLiteral("value")).toInt(), 255);

        {
            // setChanged() must be inside the same locked scope as the data
            // mutation, not after it - otherwise writeDMX() (which holds this
            // same lock across its own hasChanged()/setChanged(false)) can
            // interleave between the unlock and this call, drain with
            // hasChanged() still false, and leave the new value unapplied
            // until whatever the next tick happens to be. Mirrors
            // SimpleDesk::setValue()'s own mutex scope (simpledesk.cpp).
            QMutexLocker locker(&m_simpleDeskMutex);
            m_simpleDeskValues[address] = uchar(value);
            setChanged(true); // DMXSource::setChanged() - picked up by writeDMX()
        }

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        QJsonObject data;
        data.insert(QStringLiteral("address"), int(address));
        data.insert(QStringLiteral("value"), value);
        data.insert(QStringLiteral("overridden"), true);
        m_server->broadcast(QStringLiteral("io.simpleDesk.channelChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("io.simpleDesk.setChannels"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        // Bulk variant of setChannel above, for high-frequency multi-channel
        // writers (e.g. a live animation client) that would otherwise pay
        // per-WebSocket-message serialization/dispatch overhead once per
        // channel per frame. Validate every entry before applying any of
        // them - same all-or-nothing discipline as vc.widget.reposition's
        // "widgets" array (apivcdomain.cpp) - so a single malformed entry
        // can't leave the request partially applied.
        QJsonArray items = params.value(QStringLiteral("channels")).toArray();
        if (items.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("channels must not be empty")));
            return;
        }

        QList<QPair<quint32, uchar>> entries;
        entries.reserve(items.size());
        for (const QJsonValue &v : items)
        {
            QJsonObject item = v.toObject();
            if (item.contains(QStringLiteral("address")) == false || item.contains(QStringLiteral("value")) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("Each channels entry needs address and value")));
                return;
            }

            int value = item.value(QStringLiteral("value")).toInt(-1);
            if (value < 0 || value > 255)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("value must be 0-255")));
                return;
            }

            quint32 address = quint32(item.value(QStringLiteral("address")).toInt());
            entries.append(qMakePair(address, uchar(value)));
        }

        {
            // Same locked-scope discipline as setChannel above (see its own
            // comment): setChanged() must stay inside this scope, but only
            // needs to be called once for the whole batch - it's just a flag
            // telling writeDMX() "something in m_simpleDeskValues changed
            // since your last tick", not a per-channel counter.
            QMutexLocker locker(&m_simpleDeskMutex);
            for (const auto &entry : std::as_const(entries))
                m_simpleDeskValues[entry.first] = entry.second;
            setChanged(true); // DMXSource::setChanged() - picked up by writeDMX()
        }

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        for (const auto &entry : std::as_const(entries))
        {
            QJsonObject data;
            data.insert(QStringLiteral("address"), int(entry.first));
            data.insert(QStringLiteral("value"), int(entry.second));
            data.insert(QStringLiteral("overridden"), true);
            m_server->broadcast(QStringLiteral("io.simpleDesk.channelChanged"), data, session->clientId(), false);
        }
    });

    dispatcher->registerMethod(QStringLiteral("io.simpleDesk.resetChannel"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 address = quint32(params.value(QStringLiteral("address")).toInt());

        // The restored value (the fixture channel's default, or 0 for a raw/
        // unpatched address) is knowable synchronously from static fixture-
        // profile data - no need to wait for writeDMX() to actually touch the
        // Universe before reporting it in the channelChanged event below.
        quint32 fxID = m_doc->fixtureForAddress(address);
        Fixture *fixture = m_doc->fixture(fxID);
        int restoredValue = 0;
        if (fixture != nullptr)
        {
            const QLCChannel *ch = fixture->channel(address - fixture->universeAddress());
            if (ch != nullptr)
                restoredValue = ch->defaultValue();
        }

        {
            // See setChannel's handler above for why setChanged() must stay
            // inside this locked scope.
            QMutexLocker locker(&m_simpleDeskMutex);
            m_simpleDeskValues.remove(address);
            m_simpleDeskCommandQueue.append(qMakePair(int(SimpleDeskResetChannel), address));
            setChanged(true);
        }

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        QJsonObject data;
        data.insert(QStringLiteral("address"), int(address));
        data.insert(QStringLiteral("value"), restoredValue);
        data.insert(QStringLiteral("overridden"), false);
        m_server->broadcast(QStringLiteral("io.simpleDesk.channelChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("io.simpleDesk.resetUniverse"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 universeId = quint32(params.value(QStringLiteral("universeId")).toInt());
        if (m_doc->inputOutputMap()->universe(universeId) == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such universe")));
            return;
        }

        {
            // See setChannel's handler above for why setChanged() must stay
            // inside this locked scope.
            QMutexLocker locker(&m_simpleDeskMutex);
            quint32 start = universeId * 512;
            QMutableHashIterator<quint32, uchar> it(m_simpleDeskValues);
            while (it.hasNext())
            {
                it.next();
                if (it.key() >= start && it.key() < start + 512)
                    it.remove();
            }
            m_simpleDeskCommandQueue.append(qMakePair(int(SimpleDeskResetUniverse), universeId));
            setChanged(true);
        }

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        QJsonObject data;
        data.insert(QStringLiteral("universeId"), int(universeId));
        m_server->broadcast(QStringLiteral("io.simpleDesk.universeReset"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("io.simpleDesk.setUniverseFilter"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        m_simpleDeskUniverseFilter = quint32(params.value(QStringLiteral("universeId")).toInt());
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        QJsonObject data;
        data.insert(QStringLiteral("universeId"), int(m_simpleDeskUniverseFilter));
        m_server->broadcast(QStringLiteral("io.simpleDesk.universeFilterChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("io.simpleDesk.dump"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != m_doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        // channelGroups empty/omitted -> every channel of every patched
        // fixture (SimpleDesk's "all channels" dump mode); otherwise only
        // channels whose QLCChannel::Group is in the given set.
        bool allChannels = true;
        QSet<int> allowedGroups;
        for (const QJsonValue &v : params.value(QStringLiteral("channelGroups")).toArray())
        {
            allChannels = false;
            allowedGroups.insert(int(QLCChannel::stringToGroup(v.toString())));
        }
        bool nonZeroOnly = params.value(QStringLiteral("nonZeroOnly")).toBool();

        // fixtureIds empty/omitted -> every patched fixture; otherwise only
        // these (PopupDMXDump.qml's "Dump the selected fixture channels").
        QSet<quint32> allowedFixtures;
        for (const QJsonValue &v : params.value(QStringLiteral("fixtureIds")).toArray())
        {
            quint32 fxId = Fixture::invalidId();
            if (v.isString())
            {
                bool idOk = false;
                quint32 parsed = v.toString().toUInt(&idOk);
                if (idOk)
                    fxId = parsed;
            }
            else if (v.isDouble() && v.toDouble() >= 0)
            {
                fxId = quint32(v.toDouble());
            }
            if (m_doc->fixture(fxId) == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such fixture: %1").arg(v.toVariant().toString())));
                return;
            }
            allowedFixtures.insert(fxId);
        }

        // 1 - snapshot live pre-GM universe output, same source
        // qmlui/functionmanager.cpp's dumpDmxValues() reads: this captures
        // the full composed effect at this instant, not just this domain's
        // own m_simpleDeskValues (part of what's live could be coming from
        // a currently-running Function instead of a Simple Desk override).
        QList<Universe *> ua = m_doc->inputOutputMap()->claimUniverses();
        QByteArray preGMValues(ua.size() * 512, 0);
        for (int i = 0; i < ua.count(); i++)
        {
            const int offset = i * 512;
            preGMValues.replace(offset, 512, ua.at(i)->preGMValues());
            if (ua.at(i)->passthrough())
            {
                for (int j = 0; j < 512; j++)
                {
                    const int ofs = offset + j;
                    preGMValues[ofs] = char(ua.at(i)->applyPassthrough(j, uchar(preGMValues[ofs])));
                }
            }
        }
        m_doc->inputOutputMap()->releaseUniverses(false);

        // This domain's own held overrides take precedence over the live
        // snapshot above, same as SimpleDesk's own dumpDmxValues() override
        // rule - a value just set via setChannel() this tick may not have
        // reached the Universe's preGMValues() yet (writeDMX() runs on
        // MasterTimer's own thread, see this class's own doc comment).
        {
            QMutexLocker locker(&m_simpleDeskMutex);
            for (auto it = m_simpleDeskValues.constBegin(); it != m_simpleDeskValues.constEnd(); ++it)
            {
                if (int(it.key()) < preGMValues.size())
                    preGMValues[int(it.key())] = char(it.value());
            }
        }

        // 2 - resolve target Scene: merge into an existing one, or create new.
        QJsonValue targetIdValue = params.value(QStringLiteral("targetSceneId"));
        bool creatingNew = targetIdValue.isNull() || targetIdValue.isUndefined();
        Scene *targetScene = nullptr;
        QString requestedName = params.value(QStringLiteral("name")).toString();

        if (creatingNew)
        {
            targetScene = new Scene(m_doc);
            if (requestedName.isEmpty() == false)
                targetScene->setName(requestedName);
        }
        else
        {
            bool ok = false;
            quint32 sceneId = targetIdValue.toString().toUInt(&ok);
            targetScene = ok ? qobject_cast<Scene *>(m_doc->function(sceneId)) : nullptr;
            if (targetScene == nullptr)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                                QStringLiteral("No such Scene")));
                return;
            }
        }

        // 3 - collect + write values, across every patched fixture or the
        // fixtureIds subset (the client's own fixture selection), filtered
        // by channelGroups and nonZeroOnly.
        for (Fixture *fixture : m_doc->fixtures())
        {
            if (allowedFixtures.isEmpty() == false && allowedFixtures.contains(fixture->id()) == false)
                continue;
            quint32 baseAddress = fixture->universeAddress();
            for (quint32 chIndex = 0; chIndex < fixture->channels(); chIndex++)
            {
                if (allChannels == false)
                {
                    const QLCChannel *ch = fixture->channel(chIndex);
                    if (ch == nullptr || allowedGroups.contains(int(ch->group())) == false)
                        continue;
                }

                int address = int(baseAddress + chIndex);
                if (address < 0 || address >= preGMValues.size())
                    continue;

                uchar value = uchar(preGMValues.at(address));
                if (nonZeroOnly && value == 0)
                    continue;

                targetScene->setValue(SceneValue(fixture->id(), chIndex, value));
            }
        }

        // 4 - persist if new (an existing target Scene's setValue() calls
        // above already bumped docRevision themselves via Doc::slotFunctionChanged,
        // since it's already connected/added).
        if (creatingNew)
        {
            if (requestedName.isEmpty())
                targetScene->setName(QStringLiteral("%1 %2").arg(targetScene->name()).arg(targetScene->id()));

            if (m_doc->addFunction(targetScene) == false)
            {
                delete targetScene;
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                                QStringLiteral("Could not add Scene")));
                return;
            }
        }

        QJsonObject result;
        result.insert(QStringLiteral("sceneId"), QString::number(targetScene->id()));
        result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        // The structural side effect is reported via the shared functions.*
        // events (functions-core.yaml) rather than a bespoke io.simpleDesk
        // event - see io-notes.md.
        QJsonObject data;
        data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        if (creatingNew)
        {
            QJsonObject functionSummary;
            functionSummary.insert(QStringLiteral("id"), QString::number(targetScene->id()));
            functionSummary.insert(QStringLiteral("name"), targetScene->name());
            functionSummary.insert(QStringLiteral("type"), QStringLiteral("Scene"));
            functionSummary.insert(QStringLiteral("path"), targetScene->path(true));
            functionSummary.insert(QStringLiteral("hidden"), targetScene->isVisible() == false);
            data.insert(QStringLiteral("function"), functionSummary);
            m_server->broadcast(QStringLiteral("functions.created"), data, session->clientId(), false);
        }
        else
        {
            data.insert(QStringLiteral("functionId"), QString::number(targetScene->id()));
            m_server->broadcast(QStringLiteral("functions.updated"), data, session->clientId(), false);
        }
    });

    // io.simpleDesk.sendKeypadCommand {command, universeId?} -> {accepted, channelsChanged, history}
    //
    // The desktop keypad grammar (engine/src/keypadparser.h: AT/THRU/FULL/ZERO/
    // BY/+/-/+%/-%), evaluated by the engine's own parser so the browser and
    // the desktop agree on every edge case. universeId defaults to the Simple
    // Desk universe filter (io.simpleDesk.setUniverseFilter); the web UI has
    // one desk per tab and passes it explicitly. Spec extension (io-notes.md):
    // the fragment only specifies {command}.
    dispatcher->registerMethod(QStringLiteral("io.simpleDesk.sendKeypadCommand"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        // Normalise like KeyPad.qml/the web keypad do before the engine sees
        // it: KeyPadParser skips any token it doesn't know as "not a number",
        // so a raw "1 THRU 4 @ 50" would otherwise read 50 as a channel.
        QString command = params.value(QStringLiteral("command")).toString().toUpper();
        command.replace(QLatin1Char('@'), QStringLiteral(" AT "));
        QStringList tokens = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        tokens.removeAll(QStringLiteral("ENTER"));
        command = tokens.join(QLatin1Char(' '));
        if (command.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("command must not be empty")));
            return;
        }

        quint32 universeId = params.contains(QStringLiteral("universeId"))
                ? quint32(params.value(QStringLiteral("universeId")).toInt())
                : m_simpleDeskUniverseFilter;
        Universe *universe = m_doc->inputOutputMap()->universe(universeId);
        if (universe == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such universe")));
            return;
        }

        // +%/-% and a bare channel selection read the channel's current level
        // from here (KeyPadParser indexes it with at(channel - 1), so it must
        // be a full 512 bytes). Pre-GM values: the Grand Master must not skew
        // a relative change.
        QByteArray uniData = universe->preGMValues();
        if (uniData.size() < 512)
            uniData.append(QByteArray(512 - uniData.size(), char(0)));

        QList<SceneValue> values = m_keyPadParser->parseCommand(m_doc, command, uniData);
        QList<QPair<quint32, uchar>> entries;
        entries.reserve(values.count());
        for (const SceneValue &scv : std::as_const(values))
        {
            if (scv.channel >= 512)
                continue;
            entries.append(qMakePair((universeId << 9) + scv.channel, scv.value));
        }

        // Same bookkeeping as SimpleDesk::sendKeypadCommand(): every
        // syntactically usable command is remembered, even one that only
        // selects channels ("1 THRU 4") for the next command to act on.
        m_keypadCommandHistory.prepend(command);
        while (m_keypadCommandHistory.count() > 10) // MAX_KEYPAD_HISTORY in qmlui/simpledesk.cpp
            m_keypadCommandHistory.removeLast();

        QJsonObject result;
        result.insert(QStringLiteral("accepted"), true);
        result.insert(QStringLiteral("channelsChanged"), entries.count());
        result.insert(QStringLiteral("history"), QJsonArray::fromStringList(m_keypadCommandHistory));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        applySimpleDeskValues(entries, session->clientId());

        QJsonObject data;
        data.insert(QStringLiteral("history"), QJsonArray::fromStringList(m_keypadCommandHistory));
        m_server->broadcast(QStringLiteral("io.simpleDesk.commandHistoryChanged"), data, session->clientId(), false);
    });
}

void ApiIoDomain::applySimpleDeskValues(const QList<QPair<quint32, uchar>> &entries, const QString &originClientId)
{
    if (entries.isEmpty())
        return;

    {
        // Same locked-scope discipline as setChannel (see its own comment):
        // setChanged() must stay inside this scope, once for the whole batch.
        QMutexLocker locker(&m_simpleDeskMutex);
        for (const auto &entry : entries)
            m_simpleDeskValues[entry.first] = entry.second;
        setChanged(true); // DMXSource::setChanged() - picked up by writeDMX()
    }

    for (const auto &entry : entries)
    {
        QJsonObject data;
        data.insert(QStringLiteral("address"), int(entry.first));
        data.insert(QStringLiteral("value"), int(entry.second));
        data.insert(QStringLiteral("overridden"), true);
        m_server->broadcast(QStringLiteral("io.simpleDesk.channelChanged"), data, originClientId, false);
    }
}

QJsonObject ApiIoDomain::simpleDeskChannelToJson(quint32 address) const
{
    // Caller must hold m_simpleDeskMutex - reads m_simpleDeskValues directly.
    quint32 universeId = address >> 9;
    quint32 channel = address & 0x01FF;
    quint32 fxID = m_doc->fixtureForAddress(address);
    Fixture *fixture = m_doc->fixture(fxID);

    QJsonObject obj;
    obj.insert(QStringLiteral("address"), int(address));
    obj.insert(QStringLiteral("universeId"), int(universeId));
    obj.insert(QStringLiteral("channel"), int(channel));
    obj.insert(QStringLiteral("value"), int(m_simpleDeskValues.value(address)));
    obj.insert(QStringLiteral("overridden"), true);

    if (fixture != nullptr)
    {
        const QLCChannel *ch = fixture->channel(channel - fixture->address());
        obj.insert(QStringLiteral("fixtureId"), int(fxID));
        obj.insert(QStringLiteral("group"), ch != nullptr ? QJsonValue(QLCChannel::groupToString(ch->group()))
                                                            : QJsonValue());
    }
    else
    {
        obj.insert(QStringLiteral("fixtureId"), QJsonValue());
        obj.insert(QStringLiteral("group"), QJsonValue());
    }
    return obj;
}

FadeChannel *ApiIoDomain::simpleDeskFader(const QList<Universe *> &universes, quint32 universeId,
                                           quint32 fixtureId, quint32 channel)
{
    QSharedPointer<GenericFader> fader = m_simpleDeskFaders.value(universeId, QSharedPointer<GenericFader>());
    if (fader.isNull())
    {
        fader = universes[universeId]->requestFader(Universe::SimpleDesk);
        // Named so io.dmx.channel.inspect (ApiToolsDomain) can attribute it
        fader->setName(simpleDeskFaderName());
        m_simpleDeskFaders[universeId] = fader;
    }
    return fader->getChannelFader(m_doc, universes[universeId], fixtureId, channel);
}

void ApiIoDomain::overrideChannels(const QList<QPair<quint32, uchar>> &entries, const QString &originClientId)
{
    if (entries.isEmpty())
        return;

    {
        // Same locked-scope discipline as io.simpleDesk.setChannels (see its
        // handler above): the data mutation and setChanged() share one scope.
        QMutexLocker locker(&m_simpleDeskMutex);
        for (const auto &entry : entries)
            m_simpleDeskValues[entry.first] = entry.second;
        setChanged(true);
    }

    for (const auto &entry : entries)
    {
        QJsonObject data;
        data.insert(QStringLiteral("address"), int(entry.first));
        data.insert(QStringLiteral("value"), int(entry.second));
        data.insert(QStringLiteral("overridden"), true);
        m_server->broadcast(QStringLiteral("io.simpleDesk.channelChanged"), data, originClientId, false);
    }
}

void ApiIoDomain::writeDMX(MasterTimer *timer, QList<Universe *> universes)
{
    Q_UNUSED(timer)

    QMutexLocker locker(&m_simpleDeskMutex);

    for (const QPair<int, quint32> &command : std::as_const(m_simpleDeskCommandQueue))
    {
        if (command.first == SimpleDeskResetUniverse)
        {
            quint32 universeId = command.second;
            if (universeId >= quint32(universes.count()))
                continue;

            QSharedPointer<GenericFader> fader = m_simpleDeskFaders.value(universeId, QSharedPointer<GenericFader>());
            if (fader.isNull())
                continue;

            QHashIterator<quint32, FadeChannel> it(fader->channels());
            while (it.hasNext())
            {
                it.next();
                FadeChannel fc = it.value();
                Fixture *fixture = m_doc->fixture(fc.fixture());
                quint32 chIndex = fc.channel() & 0x01FF;
                if (fixture != nullptr)
                {
                    const QLCChannel *ch = fixture->channel(chIndex);
                    if (ch != nullptr)
                        universes[universeId]->setChannelDefaultValue(fixture->address() + chIndex, ch->defaultValue());
                }
                else
                {
                    universes[universeId]->reset(chIndex, 1);
                }
            }
            universes[universeId]->dismissFader(fader);
            m_simpleDeskFaders.remove(universeId);
        }
        else // SimpleDeskResetChannel
        {
            quint32 address = command.second;
            quint32 universeId = address >> 9;
            if (universeId >= quint32(universes.count()))
                continue;

            QSharedPointer<GenericFader> fader = m_simpleDeskFaders.value(universeId, QSharedPointer<GenericFader>());
            if (fader.isNull())
                continue;

            quint32 fxID = m_doc->fixtureForAddress(address);
            Fixture *fixture = m_doc->fixture(fxID);
            quint32 chIndex = fixture != nullptr ? address - fixture->universeAddress() : (address & 0x01FF);

            FadeChannel fc(m_doc, fxID, chIndex);
            fader->remove(&fc);
            universes[universeId]->reset(address & 0x01FF, 1);
            if (fixture != nullptr)
            {
                const QLCChannel *ch = fixture->channel(chIndex);
                if (ch != nullptr)
                    universes[universeId]->setChannelDefaultValue(address & 0x01FF, ch->defaultValue());
            }
        }
    }
    m_simpleDeskCommandQueue.clear();

    if (hasChanged())
    {
        QHashIterator<quint32, uchar> it(m_simpleDeskValues);
        while (it.hasNext())
        {
            it.next();
            quint32 address = it.key();
            quint32 universeId = address >> 9;
            if (universeId >= quint32(universes.count()))
                continue;

            quint32 fxID = m_doc->fixtureForAddress(address);
            Fixture *fixture = m_doc->fixture(fxID);
            // Unlike qmlui/simpledesk.cpp's own writeDMX() (which passes the
            // raw absolute address as "channel" for a non-fixture value -
            // only correct when the value's universe index is 0), mask to
            // the within-universe channel explicitly here: this domain's
            // "address" encoding ((universeId<<9)+channel, per io.yaml) means
            // that shortcut would silently misresolve for any universe > 0.
            quint32 channel = fixture != nullptr ? address - fixture->universeAddress() : (address & 0x01FF);

            FadeChannel *fc = simpleDeskFader(universes, universeId, fxID, channel);
            fc->setCurrent(it.value());
            fc->setTarget(it.value());
            fc->addFlag(FadeChannel::Override);
        }
        setChanged(false);
    }
}
