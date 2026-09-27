/*
  Q Light Controller Plus - Control API
  apivcinputdomain.cpp

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
#include <QKeySequence>

#include "apivcinputdomain.h"
#include "apivchost.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "doc.h"
#include "inputoutputmap.h"

namespace {

const QString kHostUnavailable = QStringLiteral("App instance not available");
const QString kWidgetId = QStringLiteral("widgetId");
const QString kControlId = QStringLiteral("controlId");
const QString kUniverse = QStringLiteral("universe");
const QString kChannel = QStringLiteral("channel");
const QString kKeySequence = QStringLiteral("keySequence");
const QString kBaseRevision = QStringLiteral("baseRevision");
const QString kDocRevision = QStringLiteral("docRevision");

/** Strict whole-number check within [0, 2^32-1] - QJsonValue::toInt() would turn "abc"/true/1.5
 *  into 0/1/1, and a composited channel (page << 16 | channel) needs the full unsigned range. */
bool jsonIsUInt32(const QJsonValue &v, quint32 *out)
{
    if (v.isDouble() == false)
        return false;
    double d = v.toDouble();
    if (d < 0 || d > 4294967295.0 || d != double(quint32(d)))
        return false;
    if (out) *out = quint32(d);
    return true;
}

bool jsonIsByte(const QJsonValue &v)
{
    quint32 n = 0;
    return jsonIsUInt32(v, &n) && n <= 255;
}

} // namespace

ApiVcInputDomain::ApiVcInputDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
    , m_detectWidgetId(ApiVcHost::InvalidWidgetId)
    , m_detectControlId(0)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    ApiDispatcher *d = m_server->dispatcher();
    registerInputSourceMethods(d);
    registerKeySequenceMethods(d);
    registerDetectMethods(d);
}

ApiVcInputDomain::~ApiVcInputDomain()
{
    stopDetection();
}

/*****************************************************************************
 * Shared helpers
 *****************************************************************************/

ApiVcHost *ApiVcInputDomain::vcHost() const
{
    return dynamic_cast<ApiVcHost *>(m_server->parent());
}

ApiVcHost *ApiVcInputDomain::requireHost(ApiSession *session, const QString &id)
{
    ApiVcHost *host = vcHost();
    if (host == nullptr)
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
    return host;
}

bool ApiVcInputDomain::parseWidgetId(const QString &s, quint32 &outId)
{
    bool ok = false;
    quint32 v = s.toUInt(&ok);
    if (ok == false)
        return false;
    outId = v;
    return true;
}

bool ApiVcInputDomain::checkRevision(ApiSession *session, const QString &id, const QJsonObject &params)
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

bool ApiVcInputDomain::resolveWidget(ApiSession *session, const QString &id, const QJsonObject &params,
                                     ApiVcHost *host, quint32 *outId)
{
    quint32 wid = ApiVcHost::InvalidWidgetId;
    if (parseWidgetId(params.value(kWidgetId).toString(), wid) == false || host->vcWidgetExists(wid) == false)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such widget")));
        return false;
    }
    *outId = wid;
    return true;
}

bool ApiVcInputDomain::resolveControl(ApiSession *session, const QString &id, const QJsonObject &params, ApiVcHost *host,
                                      quint32 widgetId, bool needKeyboard, quint32 *outControlId)
{
    QJsonArray controls = host->vcWidgetExternalControls(widgetId);
    QJsonObject details;
    details.insert(QStringLiteral("externalControls"), controls);

    quint32 controlId = 0;
    if (jsonIsUInt32(params.value(kControlId), &controlId) == false)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("controlId must be one of the widget's external control ids"), details));
        return false;
    }

    for (const QJsonValue &v : controls)
    {
        QJsonObject c = v.toObject();
        if (quint32(c.value(kControlId).toInt()) != controlId)
            continue;
        if (needKeyboard && c.value(QStringLiteral("allowKeyboard")).toBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Control '%1' (%2) cannot be bound to a keyboard sequence")
                                                                .arg(c.value(QStringLiteral("name")).toString(), QString::number(controlId)),
                                                            details));
            return false;
        }
        *outControlId = controlId;
        return true;
    }

    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                    QStringLiteral("Widget %1 has no external control %2")
                                                        .arg(QString::number(widgetId), QString::number(controlId)),
                                                    details));
    return false;
}

void ApiVcInputDomain::replyRevision(ApiSession *session, const QString &id)
{
    QJsonObject result;
    result.insert(kDocRevision, int(m_doc->docRevision()));
    session->send(ApiEnvelope::buildOkResponse(id, result));
}

void ApiVcInputDomain::broadcastInputSourcesChanged(ApiVcHost *host, quint32 widgetId, const QString &originClientId)
{
    QJsonObject data;
    data.insert(kWidgetId, QString::number(widgetId));
    data.insert(QStringLiteral("inputSources"), host->vcWidgetInputSources(widgetId));
    data.insert(kDocRevision, int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("vc.widget.inputSourcesChanged"), data, originClientId, false);
}

void ApiVcInputDomain::broadcastKeySequencesChanged(ApiVcHost *host, quint32 widgetId, const QString &originClientId)
{
    QJsonObject data;
    data.insert(kWidgetId, QString::number(widgetId));
    data.insert(QStringLiteral("keySequences"), host->vcWidgetKeySequences(widgetId));
    data.insert(kDocRevision, int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("vc.widget.keySequencesChanged"), data, originClientId, false);
}

/*****************************************************************************
 * vc.widget.inputSource.set / remove
 *****************************************************************************/

void ApiVcInputDomain::registerInputSourceMethods(ApiDispatcher *d)
{
    d->registerMethod(QStringLiteral("vc.widget.inputSource.set"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, host, &wid) == false)
            return;
        quint32 controlId = 0;
        if (resolveControl(session, id, params, host, wid, false, &controlId) == false)
            return;

        quint32 universe = 0, channel = 0;
        if (jsonIsUInt32(params.value(kUniverse), &universe) == false || jsonIsUInt32(params.value(kChannel), &channel) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("universe and channel must be non-negative integers")));
            return;
        }

        // Custom feedback (PopupCustomFeedback.qml): values are DMX bytes, the MIDI feedback routing
        // is a 1-based table index (0 = the input profile's own routing). Only the keys present are
        // applied; the host keeps everything else as it is.
        QJsonObject feedback;
        for (const QString &key : { QStringLiteral("lowerValue"), QStringLiteral("upperValue"), QStringLiteral("monitorValue") })
        {
            if (params.contains(key) == false)
                continue;
            if (jsonIsByte(params.value(key)) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("%1 must be an integer 0..255").arg(key)));
                return;
            }
            feedback.insert(key, params.value(key).toInt());
        }
        for (const QString &key : { QStringLiteral("lowerChannel"), QStringLiteral("upperChannel"), QStringLiteral("monitorChannel") })
        {
            if (params.contains(key) == false)
                continue;
            quint32 n = 0;
            if (jsonIsUInt32(params.value(key), &n) == false || n > 65535)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("%1 must be a non-negative integer (0 = input profile routing)").arg(key)));
                return;
            }
            feedback.insert(key, int(n));
        }

        QString error;
        if (host->vcWidgetInputSourceSet(wid, controlId, universe, channel, feedback, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            error.isEmpty() ? QStringLiteral("Unable to set the input source") : error));
            return;
        }

        m_doc->setModified();
        broadcastInputSourcesChanged(host, wid, session->clientId());
        replyRevision(session, id);
    });

    d->registerMethod(QStringLiteral("vc.widget.inputSource.remove"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, host, &wid) == false)
            return;

        quint32 controlId = 0, universe = 0, channel = 0;
        if (jsonIsUInt32(params.value(kControlId), &controlId) == false ||
            jsonIsUInt32(params.value(kUniverse), &universe) == false ||
            jsonIsUInt32(params.value(kChannel), &channel) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("controlId, universe and channel must be non-negative integers")));
            return;
        }

        QString error;
        if (host->vcWidgetInputSourceRemove(wid, controlId, universe, channel, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            error.isEmpty() ? QStringLiteral("No such input source on this widget") : error));
            return;
        }

        m_doc->setModified();
        broadcastInputSourcesChanged(host, wid, session->clientId());
        replyRevision(session, id);
    });
}

/*****************************************************************************
 * vc.widget.keySequence.set / remove
 *****************************************************************************/

void ApiVcInputDomain::registerKeySequenceMethods(ApiDispatcher *d)
{
    d->registerMethod(QStringLiteral("vc.widget.keySequence.set"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, host, &wid) == false)
            return;
        quint32 controlId = 0;
        if (resolveControl(session, id, params, host, wid, true, &controlId) == false)
            return;

        // Qt's portable spelling ("Ctrl+Shift+K", "F", "Return") is what VCWidget::saveXML() writes
        // and what the QML learn stores (QKeySequence(e->key() | e->modifiers()).toString()). A single
        // chord only: the VC matches one key event, never a multi-key sequence.
        QString text = params.value(kKeySequence).toString().trimmed();
        QKeySequence seq = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (text.isEmpty() || seq.isEmpty() || seq.count() != 1 || seq[0] == QKeyCombination::fromCombined(Qt::Key_unknown))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("keySequence '%1' is not a valid single key combination (portable spelling, e.g. \"Ctrl+Shift+K\")").arg(text)));
            return;
        }
        QString canonical = seq.toString(QKeySequence::PortableText);

        QString error;
        if (host->vcWidgetKeySequenceSet(wid, controlId, canonical, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            error.isEmpty() ? QStringLiteral("Unable to set the key sequence") : error));
            return;
        }

        m_doc->setModified();
        broadcastKeySequencesChanged(host, wid, session->clientId());
        replyRevision(session, id);
    });

    d->registerMethod(QStringLiteral("vc.widget.keySequence.remove"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr || checkRevision(session, id, params) == false)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, host, &wid) == false)
            return;

        QString text = params.value(kKeySequence).toString().trimmed();
        QKeySequence seq = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (text.isEmpty() || seq.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("keySequence must be a key combination in portable spelling")));
            return;
        }

        QString error;
        if (host->vcWidgetKeySequenceRemove(wid, seq.toString(QKeySequence::PortableText), &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            error.isEmpty() ? QStringLiteral("No such key sequence on this widget") : error));
            return;
        }

        m_doc->setModified();
        broadcastKeySequencesChanged(host, wid, session->clientId());
        replyRevision(session, id);
    });
}

/*****************************************************************************
 * vc.widget.inputDetect.start / stop
 *****************************************************************************/

void ApiVcInputDomain::registerDetectMethods(ApiDispatcher *d)
{
    d->registerMethod(QStringLiteral("vc.widget.inputDetect.start"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = requireHost(session, id);
        if (host == nullptr)
            return;

        quint32 wid = ApiVcHost::InvalidWidgetId;
        if (resolveWidget(session, id, params, host, &wid) == false)
            return;
        quint32 controlId = 0;
        if (resolveControl(session, id, params, host, wid, false, &controlId) == false)
            return;

        if (m_detectSession.isNull() == false && m_detectSession != session)
        {
            QJsonObject details;
            details.insert(QStringLiteral("clientId"), m_detectSession->clientId());
            details.insert(kWidgetId, QString::number(m_detectWidgetId));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                            QStringLiteral("Another client is already auto-detecting an input source"), details));
            return;
        }

        // Re-arming from the same client just re-targets; a fresh arm connects the listener once.
        if (m_detectSession.isNull())
        {
            connect(m_doc->inputOutputMap(), SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)),
                    this, SLOT(slotInputValueChanged(quint32,quint32,uchar,QString)));
            connect(session, &ApiSession::disconnected, this, &ApiVcInputDomain::slotDetectSessionDisconnected);
        }
        m_detectSession = session;
        m_detectWidgetId = wid;
        m_detectControlId = controlId;

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    d->registerMethod(QStringLiteral("vc.widget.inputDetect.stop"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        // Per the spec, stop cancels whichever widget currently holds the (single) slot, from any client.
        stopDetection();
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });
}

void ApiVcInputDomain::stopDetection()
{
    if (m_detectSession.isNull() == false)
        disconnect(m_detectSession, &ApiSession::disconnected, this, &ApiVcInputDomain::slotDetectSessionDisconnected);
    disconnect(m_doc->inputOutputMap(), SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)),
               this, SLOT(slotInputValueChanged(quint32,quint32,uchar,QString)));
    m_detectSession.clear();
    m_detectWidgetId = ApiVcHost::InvalidWidgetId;
    m_detectControlId = 0;
}

void ApiVcInputDomain::slotDetectSessionDisconnected(ApiSession *session)
{
    if (m_detectSession == session)
        stopDetection();
}

void ApiVcInputDomain::slotInputValueChanged(quint32 universe, quint32 channel, uchar value, const QString &key)
{
    Q_UNUSED(value)
    Q_UNUSED(key)

    if (m_detectSession.isNull())
        return;

    // Keep what we need, then disarm before touching the host: the host call may itself emit
    // signals that re-enter here (a widget sending feedback), and a stale slot must not bind twice.
    ApiSession *session = m_detectSession.data();
    QString clientId = session->clientId();
    quint32 wid = m_detectWidgetId;
    quint32 controlId = m_detectControlId;
    stopDetection();

    ApiVcHost *host = vcHost();
    if (host == nullptr || host->vcWidgetExists(wid) == false)
        return; // the widget went away while we were listening - nothing to bind to

    QString error;
    if (host->vcWidgetInputSourceSet(wid, controlId, universe, channel, QJsonObject(), &error) == false)
        return;

    m_doc->setModified();
    // Per the spec, the bound source is a document change every client needs (vc.widget.
    // inputSourcesChanged, §4a) - the requester recognises its own detection by originClientId.
    broadcastInputSourcesChanged(host, wid, clientId);
}
