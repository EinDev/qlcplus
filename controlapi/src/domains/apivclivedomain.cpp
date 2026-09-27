/*
  Q Light Controller Plus - Control API
  apivclivedomain.cpp

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
#include <limits>

#include "apivclivedomain.h"
#include "apivchost.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "doc.h"
#include "fixture.h"
#include "fixturegroup.h"
#include "function.h"
#include "inputoutputmap.h"

namespace {

const QString kHostUnavailable = QStringLiteral("App instance not available");
const QString kWidgetId = QStringLiteral("widgetId");
const QString kBaseRevision = QStringLiteral("baseRevision");
const QString kDocRevision = QStringLiteral("docRevision");
const QString kTypeConfig = QStringLiteral("typeConfig");
const QStringList kXyPadTypes = { QStringLiteral("XYPad") };
const QStringList kClockTypes = { QStringLiteral("Clock") };
const QStringList kAnimationTypes = { QStringLiteral("Animation") };
const QStringList kAudioTypes = { QStringLiteral("AudioTriggers") };
const QStringList kBarTypes = { QStringLiteral("None"), QStringLiteral("DMXBar"), QStringLiteral("FunctionBar"), QStringLiteral("VCWidgetBar") };

/** Strict whole-number check (see ApiVcDomain's jsonIsInteger): QJsonValue::toInt() would turn
 *  "abc"/true/1.5 into 0/1/1. */
bool jsonIsInteger(const QJsonValue &v)
{
    if (v.isDouble() == false)
        return false;
    double d = v.toDouble();
    if (d < -2147483648.0 || d > 2147483647.0)
        return false;
    return d == double(qint32(d));
}

bool jsonIntIn(const QJsonValue &v, int lo, int hi)
{
    return jsonIsInteger(v) && v.toInt() >= lo && v.toInt() <= hi;
}

/** A wire id string ("42") to quint32; false for anything else (including the empty string). */
bool parseId(const QJsonValue &v, quint32 &out)
{
    if (v.isString() == false)
        return false;
    bool ok = false;
    out = v.toString().toUInt(&ok);
    return ok;
}

void sendError(ApiSession *session, const QString &id, const QString &code, const QString &message)
{
    session->send(ApiEnvelope::buildErrorResponse(id, code, message));
}

bool presetExists(const QJsonArray &presets, int presetId)
{
    for (const QJsonValue &v : presets)
    {
        if (v.toObject().value(QStringLiteral("presetId")).toInt(-1) == presetId)
            return true;
    }
    return false;
}

/** {fixtureId, headIndex} or {fixtureGroupId} entries, validated against the Doc. */
bool validateHeadsParam(Doc *doc, const QJsonValue &v, QString *error)
{
    if (v.isArray() == false || v.toArray().isEmpty())
    {
        *error = QStringLiteral("heads must be a non-empty array of {fixtureId, headIndex} or {fixtureGroupId}");
        return false;
    }
    for (const QJsonValue &item : v.toArray())
    {
        QJsonObject h = item.toObject();
        quint32 ref = 0;
        if (h.contains(QStringLiteral("fixtureGroupId")))
        {
            if (parseId(h.value(QStringLiteral("fixtureGroupId")), ref) == false || doc->fixtureGroup(ref) == nullptr)
            {
                *error = QStringLiteral("No such fixture group '%1'").arg(h.value(QStringLiteral("fixtureGroupId")).toString());
                return false;
            }
            continue;
        }
        Fixture *fixture = parseId(h.value(QStringLiteral("fixtureId")), ref) ? doc->fixture(ref) : nullptr;
        if (fixture == nullptr)
        {
            *error = QStringLiteral("No such fixture '%1'").arg(h.value(QStringLiteral("fixtureId")).toString());
            return false;
        }
        if (h.contains(QStringLiteral("headIndex")) && jsonIntIn(h.value(QStringLiteral("headIndex")), 0, fixture->heads() - 1) == false)
        {
            *error = QStringLiteral("headIndex must be 0..%1 for fixture '%2'").arg(fixture->heads() - 1).arg(fixture->name());
            return false;
        }
    }
    return true;
}

} // namespace

ApiVcLiveDomain::ApiVcLiveDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    ApiDispatcher *d = m_server->dispatcher();
    registerSliderMethods(d);
    registerXyPadMethods(d);
    registerClockMethods(d);
    registerAnimationMethods(d);
    registerAudioTriggersMethods(d);

    if (ApiVcHost *host = vcHost())
        host->vcSetLiveListenerExt(this);
}

ApiVcLiveDomain::~ApiVcLiveDomain()
{
    if (ApiVcHost *host = vcHost())
        host->vcSetLiveListenerExt(nullptr);
}

/*****************************************************************************
 * Shared helpers
 *****************************************************************************/

ApiVcHost *ApiVcLiveDomain::vcHost() const
{
    return dynamic_cast<ApiVcHost *>(m_server->parent());
}

ApiVcHost *ApiVcLiveDomain::requireHost(ApiSession *session, const QString &id)
{
    ApiVcHost *host = vcHost();
    if (host == nullptr)
        sendError(session, id, ApiEnvelope::ErrInternal, kHostUnavailable);
    return host;
}

bool ApiVcLiveDomain::parseWidgetId(const QString &s, quint32 &outId)
{
    bool ok = false;
    quint32 v = s.toUInt(&ok);
    if (ok == false)
        return false;
    outId = v;
    return true;
}

bool ApiVcLiveDomain::checkRevision(ApiSession *session, const QString &id, const QJsonObject &params)
{
    quint32 baseRevision = quint32(params.value(kBaseRevision).toInt());
    if (baseRevision == m_doc->docRevision())
        return true;

    QJsonObject details;
    details.insert(kDocRevision, int(m_doc->docRevision()));
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                    QStringLiteral("baseRevision is stale"), details));
    return false;
}

bool ApiVcLiveDomain::resolveWidget(ApiSession *session, const QString &id, const QJsonObject &params,
                                    const QStringList &allowedTypes, bool live, ApiVcHost *host, quint32 *outId)
{
    quint32 wid = ApiVcHost::InvalidWidgetId;
    if (parseWidgetId(params.value(kWidgetId).toString(), wid) == false || host->vcWidgetExists(wid) == false)
    {
        sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such widget"));
        return false;
    }

    QString type = host->vcWidgetType(wid);
    if (allowedTypes.contains(type) == false)
    {
        QJsonObject details;
        details.insert(QStringLiteral("widgetType"), type);
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("Widget %1 is a %2, not a %3")
                                                            .arg(QString::number(wid), type, allowedTypes.join(QStringLiteral("/"))),
                                                        details));
        return false;
    }

    // Live input is refused on a disabled widget like the on-screen items do (their MouseArea is off).
    if (live && host->vcWidgetSnapshot(wid).value(QStringLiteral("isDisabled")).toBool())
    {
        sendError(session, id, ApiEnvelope::ErrInvalidState, QStringLiteral("Widget is disabled"));
        return false;
    }

    *outId = wid;
    return true;
}

void ApiVcLiveDomain::replyRevision(ApiSession *session, const QString &id)
{
    QJsonObject result;
    result.insert(kDocRevision, int(m_doc->docRevision()));
    session->send(ApiEnvelope::buildOkResponse(id, result));
}

void ApiVcLiveDomain::broadcastStructural(ApiVcHost *host, quint32 widgetId, ApiSession *session,
                                          const QString &topic, const QString &listKey)
{
    QJsonObject snapshot = host->vcWidgetSnapshot(widgetId);
    QString origin = session->clientId();

    QJsonObject list;
    list.insert(kWidgetId, QString::number(widgetId));
    list.insert(listKey, snapshot.value(kTypeConfig).toObject().value(listKey).toArray());
    list.insert(kDocRevision, int(m_doc->docRevision()));
    m_server->broadcast(topic, list, origin, false);

    QJsonObject data;
    data.insert(QStringLiteral("widget"), snapshot);
    data.insert(kDocRevision, int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("vc.widget.configChanged"), data, origin, false);
}

void ApiVcLiveDomain::broadcastLive(const QString &topic, quint32 widgetId, QJsonObject data, bool gated)
{
    data.insert(kWidgetId, QString::number(widgetId));
    m_server->broadcast(topic, data, m_liveOriginClientId, gated);
}

/*****************************************************************************
 * ApiVcLiveListenerExt
 *****************************************************************************/

void ApiVcLiveDomain::vcXyPadFloorPositionChanged(quint32 widgetId, double x, double y, double z)
{
    QJsonObject data;
    data.insert(QStringLiteral("x"), x);
    data.insert(QStringLiteral("y"), y);
    data.insert(QStringLiteral("z"), z);
    // Same gesture and rate as vc.xyPad.positionChanged, which the live-interaction slice delivers
    // ungated - kept consistent with it (the spec's gating recommendation covered both together).
    broadcastLive(QStringLiteral("vc.xyPad.floorPositionChanged"), widgetId, data);
}

void ApiVcLiveDomain::vcXyPadActivePresetChanged(quint32 widgetId, int presetId)
{
    QJsonObject data;
    data.insert(QStringLiteral("activePresetId"), presetId);
    broadcastLive(QStringLiteral("vc.xyPad.activePresetChanged"), widgetId, data);
}

void ApiVcLiveDomain::vcClockTimeChanged(quint32 widgetId, int currentTime, bool running)
{
    QJsonObject data;
    data.insert(QStringLiteral("currentTime"), currentTime);
    data.insert(QStringLiteral("running"), running);
    // 1 Hz per Clock widget, 10 Hz per running Stopwatch/Countdown: subscribe-gated per the spec.
    broadcastLive(QStringLiteral("vc.clock.timeChanged"), widgetId, data, true);
}

void ApiVcLiveDomain::vcAnimationFaderLevelChanged(quint32 widgetId, int level)
{
    QJsonObject data;
    data.insert(QStringLiteral("level"), level);
    broadcastLive(QStringLiteral("vc.animation.faderLevelChanged"), widgetId, data);
}

void ApiVcLiveDomain::vcAnimationActivePresetChanged(quint32 widgetId, int presetId, int knobPresetId, int knobValue)
{
    QJsonObject data;
    data.insert(QStringLiteral("activePresetId"), presetId);
    if (knobPresetId >= 0)
    {
        // A knob turn (vc.animation.setPresetKnobValue): which knob and its new value.
        data.insert(QStringLiteral("knobPresetId"), knobPresetId);
        data.insert(QStringLiteral("knobValue"), knobValue);
    }
    broadcastLive(QStringLiteral("vc.animation.activePresetChanged"), widgetId, data);
}

void ApiVcLiveDomain::vcAnimationStyleChanged(quint32 widgetId, int algorithmIndex, const QStringList &colors)
{
    QJsonObject data;
    data.insert(QStringLiteral("algorithmIndex"), algorithmIndex);
    data.insert(QStringLiteral("colors"), QJsonArray::fromStringList(colors));
    broadcastLive(QStringLiteral("vc.animation.styleChanged"), widgetId, data);
}

void ApiVcLiveDomain::vcAudioTriggersCaptureEnabledChanged(quint32 widgetId, bool enabled)
{
    QJsonObject data;
    data.insert(QStringLiteral("enabled"), enabled);
    broadcastLive(QStringLiteral("vc.audioTriggers.captureEnabledChanged"), widgetId, data);
}

void ApiVcLiveDomain::vcAudioTriggersLevelsChanged(quint32 widgetId, const QList<int> &levels)
{
    QJsonArray arr;
    for (int v : levels)
        arr.append(v);
    QJsonObject data;
    data.insert(QStringLiteral("levels"), arr);
    // Audio-capture rate: subscribe-gated per the spec.
    broadcastLive(QStringLiteral("vc.audioTriggers.levelsChanged"), widgetId, data, true);
}

void ApiVcLiveDomain::vcSliderMonitorChanged(quint32 widgetId, int monitorValue, bool isOverriding)
{
    QJsonObject data;
    data.insert(QStringLiteral("monitorValue"), monitorValue);
    data.insert(QStringLiteral("isOverriding"), isOverriding);
    // Follows the monitored universe's writes: subscribe-gated per the spec.
    broadcastLive(QStringLiteral("vc.slider.monitorValueChanged"), widgetId, data, true);
}

void ApiVcLiveDomain::vcXyPadFixturePositionsChanged(quint32 widgetId, const QList<QPointF> &positions)
{
    QJsonArray arr;
    for (const QPointF &p : positions)
    {
        QJsonObject o;
        o.insert(QStringLiteral("x"), p.x());
        o.insert(QStringLiteral("y"), p.y());
        arr.append(o);
    }
    QJsonObject data;
    data.insert(QStringLiteral("positions"), arr);
    // Follows the universe output while the heads move: subscribe-gated.
    broadcastLive(QStringLiteral("vc.xyPad.fixturePositionsChanged"), widgetId, data, true);
}

/*****************************************************************************
 * vc.slider.resetOverride
 *****************************************************************************/

void ApiVcLiveDomain::registerSliderMethods(ApiDispatcher *d)
{
    static const QStringList sliderTypes = { QStringLiteral("Slider") };
    // --- vc.slider.resetOverride (live) ---
    d->registerMethod(QStringLiteral("vc.slider.resetOverride"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr)
            return;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, sliderTypes, true, host, &wid) == false)
            return;
        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcSliderResetOverride(wid, &error);
        m_liveOriginClientId.clear();
        if (ok == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidState, error);
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });
}

/*****************************************************************************
 * vc.xyPad.*
 *****************************************************************************/

void ApiVcLiveDomain::registerXyPadMethods(ApiDispatcher *d)
{
    // --- vc.xyPad.setFloorPosition (live) ---
    d->registerMethod(QStringLiteral("vc.xyPad.setFloorPosition"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr)
            return;
        QJsonValue xv = params.value(QStringLiteral("x")), yv = params.value(QStringLiteral("y")), zv = params.value(QStringLiteral("z"));
        if (xv.isDouble() == false || yv.isDouble() == false || zv.isDouble() == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("x, y and z must be numbers (metres)"));
            return;
        }
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kXyPadTypes, true, host, &wid) == false)
            return;

        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcXyPadSetFloorPosition(wid, xv.toDouble(), yv.toDouble(), zv.toDouble(), &error);
        m_liveOriginClientId.clear();
        if (ok == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidState, error);
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.xyPad.fixture.add ---
    d->registerMethod(QStringLiteral("vc.xyPad.fixture.add"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kXyPadTypes, false, host, &wid) == false)
            return;

        // Exactly one of fixtureGroupId / fixtureId (+ optional headIndex) / universe.
        int given = int(params.contains(QStringLiteral("fixtureGroupId"))) + int(params.contains(QStringLiteral("fixtureId")))
                  + int(params.contains(QStringLiteral("universe")));
        if (given != 1)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams,
                      QStringLiteral("Give exactly one of fixtureGroupId, fixtureId (optionally with headIndex) or universe"));
            return;
        }

        ApiVcHost::XyPadAddKind kind = ApiVcHost::XyPadAddFixture;
        quint32 ref = 0;
        int headIndex = -1;
        if (params.contains(QStringLiteral("fixtureGroupId")))
        {
            if (parseId(params.value(QStringLiteral("fixtureGroupId")), ref) == false || m_doc->fixtureGroup(ref) == nullptr)
            {
                sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such fixture group"));
                return;
            }
            kind = ApiVcHost::XyPadAddGroup;
        }
        else if (params.contains(QStringLiteral("fixtureId")))
        {
            Fixture *fixture = parseId(params.value(QStringLiteral("fixtureId")), ref) ? m_doc->fixture(ref) : nullptr;
            if (fixture == nullptr)
            {
                sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such fixture"));
                return;
            }
            if (params.contains(QStringLiteral("headIndex")))
            {
                if (jsonIntIn(params.value(QStringLiteral("headIndex")), 0, fixture->heads() - 1) == false)
                {
                    sendError(session, id, ApiEnvelope::ErrInvalidParams,
                              QStringLiteral("headIndex must be 0..%1 for fixture '%2'").arg(fixture->heads() - 1).arg(fixture->name()));
                    return;
                }
                headIndex = params.value(QStringLiteral("headIndex")).toInt();
                kind = ApiVcHost::XyPadAddHead;
            }
        }
        else
        {
            int universes = int(m_doc->inputOutputMap()->universesCount());
            if (jsonIntIn(params.value(QStringLiteral("universe")), 0, universes - 1) == false)
            {
                sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such universe"));
                return;
            }
            ref = quint32(params.value(QStringLiteral("universe")).toInt());
            kind = ApiVcHost::XyPadAddUniverse;
        }

        QString error;
        int addedPresetId = -1;
        if (host->vcXyPadAddFixtures(wid, kind, ref, headIndex, &addedPresetId, &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, error);
            return;
        }
        m_doc->setModified();
        broadcastStructural(host, wid, session, QStringLiteral("vc.xyPad.fixturesChanged"), QStringLiteral("fixtures"));
        if (addedPresetId >= 0)
        {
            // A dropped group gets its own preset (VCXYPad::addGroup()).
            QJsonObject presets;
            presets.insert(kWidgetId, QString::number(wid));
            presets.insert(QStringLiteral("presets"), host->vcWidgetPresets(wid));
            presets.insert(kDocRevision, int(m_doc->docRevision()));
            m_server->broadcast(QStringLiteral("vc.xyPad.presetsChanged"), presets, session->clientId(), false);
        }
        QJsonObject result;
        result.insert(kDocRevision, int(m_doc->docRevision()));
        if (addedPresetId >= 0)
            result.insert(QStringLiteral("presetId"), addedPresetId);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // --- vc.xyPad.fixture.remove ---
    d->registerMethod(QStringLiteral("vc.xyPad.fixture.remove"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kXyPadTypes, false, host, &wid) == false)
            return;
        QString error;
        if (validateHeadsParam(m_doc, params.value(QStringLiteral("heads")), &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, error);
            return;
        }
        if (host->vcXyPadRemoveHeads(wid, params.value(QStringLiteral("heads")).toArray(), &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrNotFound, error);
            return;
        }
        m_doc->setModified();
        broadcastStructural(host, wid, session, QStringLiteral("vc.xyPad.fixturesChanged"), QStringLiteral("fixtures"));
        replyRevision(session, id);
    });

    // --- vc.xyPad.setHeadsRange ---
    d->registerMethod(QStringLiteral("vc.xyPad.setHeadsRange"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kXyPadTypes, false, host, &wid) == false)
            return;
        QString error;
        if (validateHeadsParam(m_doc, params.value(QStringLiteral("heads")), &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, error);
            return;
        }
        for (const QString &key : { QStringLiteral("xMin"), QStringLiteral("xMax"), QStringLiteral("yMin"), QStringLiteral("yMax") })
        {
            if (jsonIntIn(params.value(key), 0, 100000) == false)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("%1 must be a non-negative integer in the pad's display units").arg(key));
                return;
            }
        }
        if (params.value(QStringLiteral("xReverse")).isBool() == false || params.value(QStringLiteral("yReverse")).isBool() == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("xReverse and yReverse must be booleans"));
            return;
        }
        if (host->vcXyPadSetHeadsRange(wid, params.value(QStringLiteral("heads")).toArray(),
                                       params.value(QStringLiteral("xMin")).toInt(), params.value(QStringLiteral("xMax")).toInt(),
                                       params.value(QStringLiteral("xReverse")).toBool(),
                                       params.value(QStringLiteral("yMin")).toInt(), params.value(QStringLiteral("yMax")).toInt(),
                                       params.value(QStringLiteral("yReverse")).toBool(), &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, error);
            return;
        }
        m_doc->setModified();
        broadcastStructural(host, wid, session, QStringLiteral("vc.xyPad.fixturesChanged"), QStringLiteral("fixtures"));
        replyRevision(session, id);
    });

    // --- vc.xyPad.preset.move / vc.animation.preset.move (same shape, one handler) ---
    auto presetMove = [this](const QStringList &types, const QString &topic)
    {
        return [this, types, topic](ApiSession *session, const QString &id, const QJsonObject &params)
        {
            ApiVcHost *host = requireHost(session, id);
            if (host == nullptr || checkRevision(session, id, params) == false)
                return;
            quint32 wid = ApiVcHost::InvalidWidgetId;
            if (resolveWidget(session, id, params, types, false, host, &wid) == false)
                return;
            QJsonValue pv = params.value(QStringLiteral("presetId"));
            if (jsonIsInteger(pv) == false || presetExists(host->vcWidgetPresets(wid), pv.toInt()) == false)
            {
                sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such preset"));
                return;
            }
            QString direction = params.value(QStringLiteral("direction")).toString();
            if (direction != QStringLiteral("up") && direction != QStringLiteral("down"))
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("direction must be 'up' or 'down'"));
                return;
            }
            QString error;
            int newId = host->vcWidgetPresetMove(wid, pv.toInt(), direction == QStringLiteral("up"), &error);
            if (newId < 0)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidState, error.isEmpty() ? QStringLiteral("Unable to move preset") : error);
                return;
            }
            m_doc->setModified();
            broadcastStructural(host, wid, session, topic, QStringLiteral("presets"));
            // The engine swaps ids on a move: tell the caller where its preset went.
            QJsonObject result;
            result.insert(kDocRevision, int(m_doc->docRevision()));
            result.insert(QStringLiteral("presetId"), newId);
            session->send(ApiEnvelope::buildOkResponse(id, result));
        };
    };
    d->registerMethod(QStringLiteral("vc.xyPad.preset.move"), presetMove(kXyPadTypes, QStringLiteral("vc.xyPad.presetsChanged")));
    d->registerMethod(QStringLiteral("vc.animation.preset.move"), presetMove(kAnimationTypes, QStringLiteral("vc.animation.presetsChanged")));

    // --- vc.xyPad.preset.rename ---
    d->registerMethod(QStringLiteral("vc.xyPad.preset.rename"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kXyPadTypes, false, host, &wid) == false)
            return;
        QJsonValue pv = params.value(QStringLiteral("presetId"));
        if (jsonIsInteger(pv) == false || presetExists(host->vcWidgetPresets(wid), pv.toInt()) == false)
        {
            sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such preset"));
            return;
        }
        QJsonValue name = params.value(QStringLiteral("name"));
        if (name.isString() == false || name.toString().trimmed().isEmpty())
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("name must be a non-empty string"));
            return;
        }
        QString error;
        if (host->vcXyPadRenamePreset(wid, pv.toInt(), name.toString(), &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidState, error);
            return;
        }
        m_doc->setModified();
        broadcastStructural(host, wid, session, QStringLiteral("vc.xyPad.presetsChanged"), QStringLiteral("presets"));
        replyRevision(session, id);
    });
}

/*****************************************************************************
 * vc.clock.*
 *****************************************************************************/

void ApiVcLiveDomain::registerClockMethods(ApiDispatcher *d)
{
    // --- vc.clock.playPause / vc.clock.reset (live) ---
    struct Cmd { const char *method; bool play; };
    static const Cmd cmds[] = { { "vc.clock.playPause", true }, { "vc.clock.reset", false } };
    for (const Cmd &c : cmds)
    {
        bool play = c.play;
        d->registerMethod(QString::fromLatin1(c.method), [this, play](ApiSession *session, const QString &id, const QJsonObject &params)
        {
            ApiVcHost *host = requireHost(session, id);
            if (host == nullptr)
                return;
            quint32 wid = ApiVcHost::InvalidWidgetId;
            if (resolveWidget(session, id, params, kClockTypes, true, host, &wid) == false)
                return;
            QString error;
            m_liveOriginClientId = session->clientId();
            bool ok = play ? host->vcClockPlayPause(wid, &error) : host->vcClockReset(wid, &error);
            m_liveOriginClientId.clear();
            if (ok == false)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidState, error);
                return;
            }
            session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
        });
    }

    // --- vc.clock.schedule.add ---
    d->registerMethod(QStringLiteral("vc.clock.schedule.add"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kClockTypes, false, host, &wid) == false)
            return;
        QJsonValue fv = params.value(QStringLiteral("functionIds"));
        if (fv.isArray() == false || fv.toArray().isEmpty())
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("functionIds must be a non-empty array of function ids"));
            return;
        }
        QList<quint32> ids;
        for (const QJsonValue &v : fv.toArray())
        {
            quint32 fid = 0;
            if (parseId(v, fid) == false || m_doc->function(fid) == nullptr)
            {
                sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such function '%1'").arg(v.toString()));
                return;
            }
            ids.append(fid);
        }
        QString error;
        if (host->vcClockAddSchedules(wid, ids, &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidState, error);
            return;
        }
        m_doc->setModified();
        broadcastStructural(host, wid, session, QStringLiteral("vc.clock.schedulesChanged"), QStringLiteral("schedules"));
        replyRevision(session, id);
    });

    // --- vc.clock.schedule.update / remove ---
    auto scheduleIndex = [this](ApiSession *session, const QString &id, const QJsonObject &params, ApiVcHost *host, quint32 wid, int *outIndex)
    {
        int count = host->vcWidgetSnapshot(wid).value(kTypeConfig).toObject().value(QStringLiteral("schedules")).toArray().size();
        if (jsonIntIn(params.value(QStringLiteral("index")), 0, count - 1) == false)
        {
            sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such schedule (index must be 0..%1)").arg(count - 1));
            return false;
        }
        *outIndex = params.value(QStringLiteral("index")).toInt();
        return true;
    };

    d->registerMethod(QStringLiteral("vc.clock.schedule.update"), [this, scheduleIndex](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kClockTypes, false, host, &wid) == false)
            return;
        int index = -1;
        if (scheduleIndex(session, id, params, host, wid, &index) == false)
            return;

        QJsonObject patch;
        if (params.contains(QStringLiteral("startTime")))
        {
            if (jsonIntIn(params.value(QStringLiteral("startTime")), 0, 86399) == false)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("startTime must be 0..86399 seconds since midnight"));
                return;
            }
            patch.insert(QStringLiteral("startTime"), params.value(QStringLiteral("startTime")).toInt());
        }
        if (params.contains(QStringLiteral("stopTime")))
        {
            if (jsonIntIn(params.value(QStringLiteral("stopTime")), -1, 86399) == false)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("stopTime must be -1 (none) or 0..86399 seconds since midnight"));
                return;
            }
            patch.insert(QStringLiteral("stopTime"), params.value(QStringLiteral("stopTime")).toInt());
        }
        if (params.contains(QStringLiteral("weekFlags")))
        {
            if (jsonIntIn(params.value(QStringLiteral("weekFlags")), 0, 255) == false)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("weekFlags must be 0..255 (bits 0-6 Mon..Sun, bit 7 repeat)"));
                return;
            }
            patch.insert(QStringLiteral("weekFlags"), params.value(QStringLiteral("weekFlags")).toInt());
        }
        if (patch.isEmpty())
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("Nothing to update: give startTime, stopTime and/or weekFlags"));
            return;
        }
        QString error;
        if (host->vcClockUpdateSchedule(wid, index, patch, &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidState, error);
            return;
        }
        m_doc->setModified();
        broadcastStructural(host, wid, session, QStringLiteral("vc.clock.schedulesChanged"), QStringLiteral("schedules"));
        replyRevision(session, id);
    });

    d->registerMethod(QStringLiteral("vc.clock.schedule.remove"), [this, scheduleIndex](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kClockTypes, false, host, &wid) == false)
            return;
        int index = -1;
        if (scheduleIndex(session, id, params, host, wid, &index) == false)
            return;
        QString error;
        if (host->vcClockRemoveSchedule(wid, index, &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidState, error);
            return;
        }
        m_doc->setModified();
        broadcastStructural(host, wid, session, QStringLiteral("vc.clock.schedulesChanged"), QStringLiteral("schedules"));
        replyRevision(session, id);
    });
}

/*****************************************************************************
 * vc.animation.*
 *****************************************************************************/

void ApiVcLiveDomain::registerAnimationMethods(ApiDispatcher *d)
{
    // --- vc.animation.setFaderLevel (live) ---
    d->registerMethod(QStringLiteral("vc.animation.setFaderLevel"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr)
            return;
        if (jsonIntIn(params.value(QStringLiteral("level")), 0, 255) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("level must be an integer 0..255"));
            return;
        }
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kAnimationTypes, true, host, &wid) == false)
            return;
        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcAnimationSetFaderLevel(wid, params.value(QStringLiteral("level")).toInt(), &error);
        m_liveOriginClientId.clear();
        if (ok == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidState, error);
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.animation.setPresetKnobValue (live) ---
    d->registerMethod(QStringLiteral("vc.animation.setPresetKnobValue"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr)
            return;
        if (jsonIntIn(params.value(QStringLiteral("value")), 0, 255) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("value must be an integer 0..255"));
            return;
        }
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kAnimationTypes, true, host, &wid) == false)
            return;
        QJsonValue pv = params.value(QStringLiteral("presetId"));
        if (jsonIsInteger(pv) == false || presetExists(host->vcWidgetPresets(wid), pv.toInt()) == false)
        {
            sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such preset"));
            return;
        }
        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcAnimationSetPresetKnobValue(wid, pv.toInt(), params.value(QStringLiteral("value")).toInt(), &error);
        m_liveOriginClientId.clear();
        if (ok == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, error);
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });
    // vc.animation.preset.move is registered next to vc.xyPad.preset.move (registerXyPadMethods).
}

/*****************************************************************************
 * vc.audioTriggers.*
 *****************************************************************************/

void ApiVcLiveDomain::registerAudioTriggersMethods(ApiDispatcher *d)
{
    // --- vc.audioTriggers.setCaptureEnabled (live) ---
    d->registerMethod(QStringLiteral("vc.audioTriggers.setCaptureEnabled"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr)
            return;
        if (params.value(QStringLiteral("enabled")).isBool() == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("enabled must be a boolean"));
            return;
        }
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kAudioTypes, true, host, &wid) == false)
            return;
        QString error;
        m_liveOriginClientId = session->clientId();
        bool ok = host->vcAudioTriggersSetCaptureEnabled(wid, params.value(QStringLiteral("enabled")).toBool(), &error);
        m_liveOriginClientId.clear();
        if (ok == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidState, error);
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // --- vc.audioTriggers.setBarConfig ---
    d->registerMethod(QStringLiteral("vc.audioTriggers.setBarConfig"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;
        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, kAudioTypes, false, host, &wid) == false)
            return;

        QJsonArray bars = host->vcWidgetSnapshot(wid).value(kTypeConfig).toObject().value(QStringLiteral("bars")).toArray();
        if (jsonIntIn(params.value(QStringLiteral("index")), 0, bars.size() - 1) == false)
        {
            sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such bar (index must be 0..%1)").arg(bars.size() - 1));
            return;
        }
        int index = params.value(QStringLiteral("index")).toInt();
        QString type = bars.at(index).toObject().value(QStringLiteral("type")).toString();

        QJsonObject patch;
        if (params.contains(QStringLiteral("type")))
        {
            type = params.value(QStringLiteral("type")).toString();
            if (kBarTypes.contains(type) == false)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("type must be None, DMXBar, FunctionBar or VCWidgetBar"));
                return;
            }
            patch.insert(QStringLiteral("type"), type);
        }
        for (const QString &key : { QStringLiteral("minThreshold"), QStringLiteral("maxThreshold") })
        {
            if (params.contains(key) == false)
                continue;
            if (jsonIntIn(params.value(key), 0, 255) == false)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("%1 must be an integer 0..255").arg(key));
                return;
            }
            patch.insert(key, params.value(key).toInt());
        }
        if (params.contains(QStringLiteral("functionId")))
        {
            quint32 fid = 0;
            if (type != QStringLiteral("FunctionBar"))
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("functionId applies to a FunctionBar (set type FunctionBar first or in the same call)"));
                return;
            }
            if (parseId(params.value(QStringLiteral("functionId")), fid) == false || m_doc->function(fid) == nullptr)
            {
                sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such function"));
                return;
            }
            patch.insert(QStringLiteral("functionId"), QString::number(fid));
        }
        if (params.contains(QStringLiteral("triggeredWidgetId")))
        {
            quint32 target = 0;
            if (type != QStringLiteral("VCWidgetBar"))
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("triggeredWidgetId applies to a VCWidgetBar (set type VCWidgetBar first or in the same call)"));
                return;
            }
            if (parseId(params.value(QStringLiteral("triggeredWidgetId")), target) == false || host->vcWidgetExists(target) == false)
            {
                sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such widget to trigger"));
                return;
            }
            if (target == wid)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("An audio triggers widget cannot trigger itself"));
                return;
            }
            patch.insert(QStringLiteral("triggeredWidgetId"), QString::number(target));
        }
        if (params.contains(QStringLiteral("dmxChannels")))
        {
            QJsonValue cv = params.value(QStringLiteral("dmxChannels"));
            if (type != QStringLiteral("DMXBar"))
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("dmxChannels applies to a DMXBar (set type DMXBar first or in the same call)"));
                return;
            }
            if (cv.isArray() == false)
            {
                sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("dmxChannels must be an array of {fixtureId, channel}"));
                return;
            }
            QJsonArray channels;
            for (const QJsonValue &v : cv.toArray())
            {
                QJsonObject c = v.toObject();
                quint32 fid = 0;
                Fixture *fixture = parseId(c.value(QStringLiteral("fixtureId")), fid) ? m_doc->fixture(fid) : nullptr;
                if (fixture == nullptr)
                {
                    sendError(session, id, ApiEnvelope::ErrNotFound, QStringLiteral("No such fixture '%1'").arg(c.value(QStringLiteral("fixtureId")).toString()));
                    return;
                }
                if (jsonIntIn(c.value(QStringLiteral("channel")), 0, int(fixture->channels()) - 1) == false)
                {
                    sendError(session, id, ApiEnvelope::ErrInvalidParams,
                              QStringLiteral("channel must be 0..%1 for fixture '%2'").arg(int(fixture->channels()) - 1).arg(fixture->name()));
                    return;
                }
                QJsonObject clean;
                clean.insert(QStringLiteral("fixtureId"), QString::number(fid));
                clean.insert(QStringLiteral("channel"), c.value(QStringLiteral("channel")).toInt());
                channels.append(clean);
            }
            patch.insert(QStringLiteral("dmxChannels"), channels);
        }
        if (patch.isEmpty())
        {
            sendError(session, id, ApiEnvelope::ErrInvalidParams, QStringLiteral("Nothing to update: give type, thresholds, functionId, triggeredWidgetId or dmxChannels"));
            return;
        }

        QString error;
        if (host->vcAudioTriggersSetBarConfig(wid, index, patch, &error) == false)
        {
            sendError(session, id, ApiEnvelope::ErrInvalidState, error);
            return;
        }
        m_doc->setModified();
        broadcastStructural(host, wid, session, QStringLiteral("vc.audioTriggers.barsChanged"), QStringLiteral("bars"));
        replyRevision(session, id);
    });
}
