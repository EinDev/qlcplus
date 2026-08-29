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

#include "apiiodomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "inputoutputmap.h"
#include "grandmaster.h"
#include "outputpatch.h"
#include "inputpatch.h"
#include "universe.h"
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

} // namespace

ApiIoDomain::ApiIoDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods();

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
    connect(ioMap, SIGNAL(grandMasterValueChanged(uchar)), this, SLOT(slotGrandMasterValueChanged(uchar)));
    connect(ioMap, SIGNAL(blackoutChanged(bool)), this, SLOT(slotBlackoutChanged(bool)));

    for (Universe *universe : ioMap->universes())
        watchUniverse(universe);
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
}
