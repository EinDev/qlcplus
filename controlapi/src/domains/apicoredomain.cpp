/*
  Q Light Controller Plus - Control API
  apicoredomain.cpp

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
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QBuffer>
#include <QSettings>
#include <QTimer>

#include "apicoredomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "apiprojecthost.h"
#include "inputoutputmap.h"
#include "mastertimer.h"
#include "function.h"

#define MASTERTIMER_FREQUENCY "mastertimer/frequency"

/** core.bpm.tap: a pause longer than this between two taps starts a new
 *  tap run instead of contributing a (meaninglessly long) interval. */
#define TAP_RESET_INTERVAL_MS 2000
/** core.bpm.tap: the tempo is the mean of at most this many most recent
 *  intervals - enough to smooth jitter, few enough to follow a tempo change
 *  within a bar. */
#define TAP_WINDOW 4
/** Upper BPM bound for core.bpm.set/tap - the same cap qmlui's
 *  BeatGeneratorsPanel.qml applies to its own tap button. */
#define BPM_MAX 1000
/** core.history.changed coalescing window, see ApiCoreDomain::slotHistoryChanged() */
#define HISTORY_COALESCE_MS 50

namespace {

// Web UI contract spelling ("disabled"|"internal"|"plugin"|"audio") -
// deliberately NOT InputOutputMap::beatTypeToString(), whose capitalized
// form is the .qxw persistence spelling.
QString beatGeneratorToJson(InputOutputMap::BeatGeneratorType type)
{
    switch (type)
    {
    case InputOutputMap::Internal: return QStringLiteral("internal");
    case InputOutputMap::Plugin:   return QStringLiteral("plugin");
    case InputOutputMap::Audio:    return QStringLiteral("audio");
    default:
    case InputOutputMap::Disabled: return QStringLiteral("disabled");
    }
}

bool beatGeneratorFromJson(const QString &str, InputOutputMap::BeatGeneratorType &type)
{
    if (str == QStringLiteral("disabled"))      type = InputOutputMap::Disabled;
    else if (str == QStringLiteral("internal")) type = InputOutputMap::Internal;
    else if (str == QStringLiteral("plugin"))   type = InputOutputMap::Plugin;
    else if (str == QStringLiteral("audio"))    type = InputOutputMap::Audio;
    else return false;
    return true;
}

// core.bpm.get result / core.bpm.changed data. No beatsPerBar: the engine
// has no bar concept at generator level (only Show has a per-show
// beatsDivision), so the contract's optional field is simply absent.
QJsonObject bpmStateToJson(InputOutputMap *ioMap)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("bpm"), ioMap->bpmNumber());
    obj.insert(QStringLiteral("generator"), beatGeneratorToJson(ioMap->beatGeneratorType()));
    return obj;
}

// Shared by core.project.get and the core.project.loaded broadcast - see
// CoreProjectMetadata in docs/api-spec/fragments/core.yaml.
QJsonObject projectMetadataToJson(Doc *doc, ApiProjectHost *host)
{
    QJsonObject obj;
    QString path = host != nullptr ? host->fileName() : QString();
    obj.insert(QStringLiteral("filePath"), path.isEmpty() ? QJsonValue() : QJsonValue(path));
    obj.insert(QStringLiteral("fileName"), path.isEmpty() ? QJsonValue() : QJsonValue(QFileInfo(path).fileName()));
    obj.insert(QStringLiteral("isModified"), doc->isModified());
    obj.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    // docRevisionAtLastSave not tracked by Doc today, could be added or ignored
    obj.insert(QStringLiteral("docRevisionAtLastSave"), QJsonValue());
    // creator: engine doesn't expose this easily in Doc
    obj.insert(QStringLiteral("creator"), QJsonValue());
    // the autostart function (core.project.setStartupFunction), null when unset
    quint32 startup = doc->startupFunction();
    obj.insert(QStringLiteral("startupFunctionId"), startup == Function::invalidId()
               ? QJsonValue() : QJsonValue(QString::number(startup)));
    return obj;
}

} // namespace

ApiCoreDomain::ApiCoreDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
    , m_historyTimer(new QTimer(this))
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    m_tapClock.start();
    m_historyTimer->setSingleShot(true);
    m_historyTimer->setInterval(HISTORY_COALESCE_MS);
    connect(m_historyTimer, &QTimer::timeout, this, &ApiCoreDomain::slotBroadcastHistoryChanged);

    registerMethods();

    connect(m_doc, SIGNAL(modeChanged(Doc::Mode)),
            this, SLOT(slotModeChanged(Doc::Mode)));
    connect(m_doc, SIGNAL(docRevisionChanged(quint32)),
            this, SLOT(slotDocRevisionChanged(quint32)));

    // Beat generator feed (engine DLL: string-based connects, see
    // apiiodomain.cpp). beat() arrives from InputOutputMap on this thread
    // for Internal (via MasterTimer's queued beat) and from the audio
    // capture / input thread otherwise - auto connection queues those.
    InputOutputMap *ioMap = m_doc->inputOutputMap();
    connect(ioMap, SIGNAL(bpmNumberChanged(int)), this, SLOT(slotBpmNumberChanged(int)));
    connect(ioMap, SIGNAL(beatGeneratorTypeChanged()), this, SLOT(slotBeatGeneratorTypeChanged()));
    connect(ioMap, SIGNAL(beat()), this, SLOT(slotBeat()));

    // Connected via the QObject the domain's methods actually live on
    // (whatever ApiServer's parent is, normally qmlui's App) rather than
    // through the ApiProjectHost interface, which - being a plain, non-
    // QObject interface so this module needn't link App itself - can't be
    // the target of a signal/slot connection.
    QObject *host = m_server->parent();
    if (host != nullptr)
    {
        connect(host, SIGNAL(recentFilesChanged()),
                this, SLOT(slotRecentFilesChanged()));
        connect(host, SIGNAL(workingPathChanged(QString)),
                this, SLOT(slotWorkingPathChanged(QString)));
        // App::historyChanged (relay of Tardis::historyChanged) - see
        // apiprojecthost.h's undo/redo block. Silently absent on a host
        // that has no such signal.
        connect(host, SIGNAL(historyChanged()),
                this, SLOT(slotHistoryChanged()));
    }
}

bool ApiCoreDomain::requireUndoHost(ApiSession *session, const QString &id, ApiProjectHost **host) const
{
    *host = projectHost();
    if (*host != nullptr)
        return true;
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                                                    QStringLiteral("No undo/redo engine available in this server")));
    return false;
}

QJsonObject ApiCoreDomain::historyStateToJson() const
{
    ApiProjectHost *host = projectHost();
    QJsonObject obj;
    bool canUndo = host != nullptr && host->canUndo();
    bool canRedo = host != nullptr && host->canRedo();
    obj.insert(QStringLiteral("canUndo"), canUndo);
    obj.insert(QStringLiteral("canRedo"), canRedo);
    if (canUndo)
        obj.insert(QStringLiteral("undoText"), host->undoText());
    if (canRedo)
        obj.insert(QStringLiteral("redoText"), host->redoText());
    obj.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    return obj;
}

bool ApiCoreDomain::ensureInternalBeatGenerator(ApiSession *session, const QString &id)
{
    InputOutputMap *ioMap = m_doc->inputOutputMap();
    switch (ioMap->beatGeneratorType())
    {
    case InputOutputMap::Internal:
        return true;
    case InputOutputMap::Disabled:
        // Same as picking "Internal" in the toolbar's beat panel: the only
        // generator whose tempo is set by hand, and the one qmlui itself
        // enables at startup (App::initDoc()).
        m_pendingOriginClientId = session->clientId();
        ioMap->setBeatGeneratorType(InputOutputMap::Internal);
        m_pendingOriginClientId.clear();
        return true;
    default:
    {
        QJsonObject details;
        details.insert(QStringLiteral("generator"), beatGeneratorToJson(ioMap->beatGeneratorType()));
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
            QStringLiteral("The tempo is detected from the active beat source; set generator to \"internal\" first"),
            details));
        return false;
    }
    }
}

ApiProjectHost *ApiCoreDomain::projectHost() const
{
    return dynamic_cast<ApiProjectHost *>(m_server->parent());
}

void ApiCoreDomain::registerMethods()
{
    ApiDispatcher *d = m_server->dispatcher();

    // core.project.new
    d->registerMethod(QStringLiteral("core.project.new"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        ApiProjectHost *a = projectHost();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        a->newWorkspace();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        broadcastProjectLoaded(QStringLiteral("new"), session->clientId());
    });

    // core.project.open
    d->registerMethod(QStringLiteral("core.project.open"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiProjectHost *a = projectHost();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        QString source = params.value(QStringLiteral("source")).toString();
        bool ok = false;

        if (source == QStringLiteral("path"))
        {
            QString path = params.value(QStringLiteral("path")).toString();
            ok = a->loadWorkspace(path);
        }
        else if (source == QStringLiteral("upload"))
        {
            // fileName is ignored by loadXML(QByteArray) but used by core spec.
            // Actually, we should set the fileName in App so Save works.
            QByteArray content = QByteArray::fromBase64(params.value(QStringLiteral("contentBase64")).toString().toUtf8());
            a->slotLoadDocFromMemory(content);
            a->setFileName(params.value(QStringLiteral("fileName")).toString());
            ok = true;
        }
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("Invalid source")));
            return;
        }

        if (ok)
        {
            QJsonObject result;
            result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            // warningsHtml: not easily available from App today without changes to capture Doc::errorLog()
            result.insert(QStringLiteral("warningsHtml"), QJsonValue::Null);
            session->send(ApiEnvelope::buildOkResponse(id, result));

            broadcastProjectLoaded(QStringLiteral("opened"), session->clientId());
        }
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("Failed to open project")));
        }
    });

    // core.project.close
    d->registerMethod(QStringLiteral("core.project.close"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        ApiProjectHost *a = projectHost();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        a->newWorkspace();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        broadcastProjectLoaded(QStringLiteral("closed"), session->clientId());
    });

    // core.project.save
    d->registerMethod(QStringLiteral("core.project.save"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        ApiProjectHost *a = projectHost();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        QString fileName = a->fileName();
        if (fileName.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, QStringLiteral("Project never saved, use saveAs")));
            return;
        }

        if (a->saveWorkspace(fileName))
        {
            QJsonObject result;
            result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            result.insert(QStringLiteral("filePath"), fileName);
            session->send(ApiEnvelope::buildOkResponse(id, result));

            QJsonObject data;
            data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            data.insert(QStringLiteral("filePath"), fileName);
            data.insert(QStringLiteral("fileName"), QFileInfo(fileName).fileName());
            m_server->broadcast(QStringLiteral("core.project.saved"), data, session->clientId(), false);
        }
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("Failed to save project")));
        }
    });

    // core.project.saveAs
    d->registerMethod(QStringLiteral("core.project.saveAs"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiProjectHost *a = projectHost();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        QString target = params.value(QStringLiteral("target")).toString();
        if (target == QStringLiteral("serverPath"))
        {
            QString path = params.value(QStringLiteral("path")).toString();
            if (path.endsWith(QStringLiteral(".qxw"), Qt::CaseInsensitive) == false)
                path += QStringLiteral(".qxw");

            if (a->saveWorkspace(path))
            {
                QJsonObject result;
                result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
                result.insert(QStringLiteral("filePath"), path);
                session->send(ApiEnvelope::buildOkResponse(id, result));

                QJsonObject data;
                data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
                data.insert(QStringLiteral("filePath"), path);
                data.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
                m_server->broadcast(QStringLiteral("core.project.saved"), data, session->clientId(), false);
            }
            else
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("Failed to save project")));
            }
        }
        else if (target == QStringLiteral("download"))
        {
            // App doesn't have a direct "serialize to memory" method exposed easily without XML streamer
            // But we can use saveXML to a QBuffer if we had access to it.
            // For now, let's just return an error or try to implement it.
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotImplemented, QStringLiteral("Download not yet implemented")));
        }
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("Invalid target")));
        }
    });

    // core.project.get
    d->registerMethod(QStringLiteral("core.project.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        session->send(ApiEnvelope::buildOkResponse(id, projectMetadataToJson(m_doc, projectHost())));
    });

    // core.project.recentFiles
    d->registerMethod(QStringLiteral("core.project.recentFiles"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        ApiProjectHost *a = projectHost();
        QJsonArray files;
        if (a)
        {
            for (const QString &path : a->recentFiles())
            {
                QJsonObject entry;
                entry.insert(QStringLiteral("filePath"), path);
                entry.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
                files.append(entry);
            }
        }
        QJsonObject result;
        result.insert(QStringLiteral("files"), files);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.mode.get
    d->registerMethod(QStringLiteral("core.mode.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonObject result;
        result.insert(QStringLiteral("mode"), m_doc->mode() == Doc::Design ? QStringLiteral("design") : QStringLiteral("operate"));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.mode.set
    d->registerMethod(QStringLiteral("core.mode.set"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QString modeStr = params.value(QStringLiteral("mode")).toString();
        Doc::Mode mode;
        if (modeStr == QStringLiteral("design")) mode = Doc::Design;
        else if (modeStr == QStringLiteral("operate")) mode = Doc::Operate;
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("Invalid mode")));
            return;
        }

        m_pendingOriginClientId = session->clientId();
        m_doc->setMode(mode);
        m_pendingOriginClientId.clear();
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    /*********************************************************************
     * Beat generator (§4b live state, no baseRevision)
     *********************************************************************/

    // core.bpm.get {} -> {bpm, generator}
    d->registerMethod(QStringLiteral("core.bpm.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        session->send(ApiEnvelope::buildOkResponse(id, bpmStateToJson(m_doc->inputOutputMap())));
    });

    // core.bpm.set {bpm?, generator?} -> {}
    d->registerMethod(QStringLiteral("core.bpm.set"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        InputOutputMap *ioMap = m_doc->inputOutputMap();
        bool hasBpm = params.contains(QStringLiteral("bpm"));
        bool hasGenerator = params.contains(QStringLiteral("generator"));
        if (hasBpm == false && hasGenerator == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("bpm and/or generator required")));
            return;
        }

        InputOutputMap::BeatGeneratorType generator = ioMap->beatGeneratorType();
        if (hasGenerator &&
            beatGeneratorFromJson(params.value(QStringLiteral("generator")).toString(), generator) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("generator must be one of disabled, internal, plugin, audio")));
            return;
        }

        int bpm = -1;
        if (hasBpm)
        {
            QJsonValue bpmValue = params.value(QStringLiteral("bpm"));
            // Range-check the double itself before rounding: qRound() on a
            // value outside int's range (1e300) is undefined behaviour, so
            // the int comparison below could not be relied on to catch it.
            double bpmDouble = bpmValue.isDouble() ? bpmValue.toDouble() : -1.0;
            bpm = (bpmDouble >= 0.0 && bpmDouble <= double(BPM_MAX)) ? qRound(bpmDouble) : -1;
            if (bpm < 0 || bpm > BPM_MAX)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("bpm must be a number between 0 (off) and %1").arg(BPM_MAX)));
                return;
            }
        }

        // Validate everything above before mutating anything below.
        if (hasGenerator)
        {
            m_pendingOriginClientId = session->clientId();
            ioMap->setBeatGeneratorType(generator);
            m_pendingOriginClientId.clear();
        }

        if (hasBpm)
        {
            if (bpm == 0)
            {
                // "BPM: Off" - the toolbar shows exactly this when the
                // generator is disabled, so 0 maps to disabling it.
                m_pendingOriginClientId = session->clientId();
                ioMap->setBeatGeneratorType(InputOutputMap::Disabled);
                m_pendingOriginClientId.clear();
            }
            else
            {
                if (ensureInternalBeatGenerator(session, id) == false)
                    return;
                m_pendingOriginClientId = session->clientId();
                ioMap->setBpmNumber(bpm);
                m_pendingOriginClientId.clear();
            }
        }

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // core.bpm.tap {} -> {bpm, tapCount}
    d->registerMethod(QStringLiteral("core.bpm.tap"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        if (ensureInternalBeatGenerator(session, id) == false)
            return;

        qint64 now = m_tapClock.elapsed();
        if (m_tapTimesMs.isEmpty() == false && now - m_tapTimesMs.last() > TAP_RESET_INTERVAL_MS)
            m_tapTimesMs.clear();
        m_tapTimesMs.append(now);
        while (m_tapTimesMs.count() > TAP_WINDOW + 1)
            m_tapTimesMs.removeFirst();

        if (m_tapTimesMs.count() >= 2)
        {
            double meanIntervalMs = double(m_tapTimesMs.last() - m_tapTimesMs.first()) / (m_tapTimesMs.count() - 1);
            int bpm = qBound(1, qRound(60000.0 / meanIntervalMs), BPM_MAX);
            m_pendingOriginClientId = session->clientId();
            m_doc->inputOutputMap()->setBpmNumber(bpm);
            m_pendingOriginClientId.clear();
        }

        QJsonObject result;
        result.insert(QStringLiteral("bpm"), m_doc->inputOutputMap()->bpmNumber());
        result.insert(QStringLiteral("tapCount"), int(m_tapTimesMs.count()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    /*********************************************************************
     * Undo / redo (ApiProjectHost -> qmlui Tardis)
     *********************************************************************/

    // core.history.get {} -> {canUndo, canRedo, undoText?, redoText?, docRevision, entries}
    d->registerMethod(QStringLiteral("core.history.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        ApiProjectHost *host = nullptr;
        if (requireUndoHost(session, id, &host) == false)
            return;
        QJsonObject result = historyStateToJson();
        // Tardis exposes only the next undo/redo step, not the stack - the
        // spec's entries list stays empty rather than being invented.
        result.insert(QStringLiteral("entries"), QJsonArray());
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.undo / core.redo {steps?} -> {ok, description?, stepsApplied, canUndo, canRedo, docRevision}
    auto undoRedoHandler = [this](bool undo)
    {
        return [this, undo](ApiSession *session, const QString &id, const QJsonObject &params)
        {
            ApiProjectHost *host = nullptr;
            if (requireUndoHost(session, id, &host) == false)
                return;

            int steps = qMax(1, params.value(QStringLiteral("steps")).toInt(1));
            QString description = undo ? host->undoText() : host->redoText();
            int applied = 0;
            for (; applied < steps; applied++)
            {
                bool done = undo ? host->undo() : host->redo();
                if (done == false)
                    break;
            }

            // The matching core.history.changed follows via the host's
            // historyChanged relay (coalesced), and any structural step
            // bumped docRevision through the engine setters it re-invoked.
            QJsonObject result = historyStateToJson();
            result.insert(QStringLiteral("ok"), applied > 0);
            result.insert(QStringLiteral("stepsApplied"), applied);
            result.insert(QStringLiteral("direction"), undo ? QStringLiteral("undo") : QStringLiteral("redo"));
            if (applied > 0 && description.isEmpty() == false)
                result.insert(QStringLiteral("description"), description);
            session->send(ApiEnvelope::buildOkResponse(id, result));
        };
    };
    d->registerMethod(QStringLiteral("core.undo"), undoRedoHandler(true));
    d->registerMethod(QStringLiteral("core.redo"), undoRedoHandler(false));

    // core.settings.get
    d->registerMethod(QStringLiteral("core.settings.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        ApiProjectHost *a = projectHost();
        QSettings settings;
        QJsonObject result;
        result.insert(QStringLiteral("locale"), settings.value(QStringLiteral(SETTINGS_LANGUAGE)).toString());
        result.insert(QStringLiteral("defaultWorkingPath"), a ? a->workingPath() : QString());
        result.insert(QStringLiteral("masterTimerFrequencyHz"), settings.value(QStringLiteral(MASTERTIMER_FREQUENCY)).toInt());

        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.settings.set
    d->registerMethod(QStringLiteral("core.settings.set"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiProjectHost *a = projectHost();
        QSettings settings;

        if (params.contains(QStringLiteral("locale")))
            settings.setValue(QStringLiteral(SETTINGS_LANGUAGE), params.value(QStringLiteral("locale")).toString());

        if (params.contains(QStringLiteral("defaultWorkingPath")) && a)
            a->setWorkingPath(params.value(QStringLiteral("defaultWorkingPath")).toString());

        if (params.contains(QStringLiteral("masterTimerFrequencyHz")))
            settings.setValue(QStringLiteral(MASTERTIMER_FREQUENCY), params.value(QStringLiteral("masterTimerFrequencyHz")).toInt());

        // Refresh result
        QJsonObject result;
        result.insert(QStringLiteral("locale"), settings.value(QStringLiteral(SETTINGS_LANGUAGE)).toString());
        result.insert(QStringLiteral("defaultWorkingPath"), a ? a->workingPath() : QString());
        result.insert(QStringLiteral("masterTimerFrequencyHz"), settings.value(QStringLiteral(MASTERTIMER_FREQUENCY)).toInt());

        m_server->broadcast(QStringLiteral("core.settings.changed"), result, session->clientId(), false);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.fs.list - read-only listing of one directory on the engine host,
    // so a remote client can pick files for the server-side-path methods
    // (core.project.open, functions.audio/video.setSource, ...). Mirrors
    // qmlui/folderbrowser.cpp: dirs first, case-insensitive name order,
    // hidden entries and ./.. never listed, glob name filters apply to files
    // only. Nothing is ever created, renamed or deleted here.
    d->registerMethod(QStringLiteral("core.fs.list"), [](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QJsonArray roots;
        QJsonObject home;
        home.insert(QStringLiteral("name"), QStringLiteral("Home"));
        home.insert(QStringLiteral("path"), QDir::homePath());
        roots.append(home);
        const QFileInfoList drives = QDir::drives();
        for (const QFileInfo &drive : drives)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), drive.absolutePath());
            entry.insert(QStringLiteral("path"), drive.absolutePath());
            roots.append(entry);
        }

        QJsonObject result;
        result.insert(QStringLiteral("roots"), roots);

        QString path = params.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty())
        {
            result.insert(QStringLiteral("path"), QString());
            result.insert(QStringLiteral("parent"), QJsonValue());
            result.insert(QStringLiteral("entries"), QJsonArray());
            session->send(ApiEnvelope::buildOkResponse(id, result));
            return;
        }

        QDir dir(path);
        if (dir.isRelative())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("path must be absolute (or empty for the roots)")));
            return;
        }
        if (dir.exists() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such directory: ") + path));
            return;
        }

        bool includeFiles = params.value(QStringLiteral("includeFiles")).toBool(true);
        QStringList filters;
        const QJsonArray extensions = params.value(QStringLiteral("extensions")).toArray();
        for (const QJsonValue &v : extensions)
        {
            QString pattern = v.toString().trimmed();
            if (pattern.isEmpty() == false)
                filters << pattern;
        }

        QDir::Filters flags = QDir::AllDirs | QDir::NoDotAndDotDot;
        if (includeFiles)
            flags |= QDir::Files;
        dir.setFilter(flags);
        dir.setSorting(QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        if (filters.isEmpty() == false)
            dir.setNameFilters(filters); // AllDirs: directories ignore the name filter

        QJsonArray entries;
        const QFileInfoList infos = dir.entryInfoList();
        for (const QFileInfo &info : infos)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), info.fileName());
            entry.insert(QStringLiteral("path"), info.absoluteFilePath());
            entry.insert(QStringLiteral("isDir"), info.isDir());
            entry.insert(QStringLiteral("size"), info.isDir() ? 0.0 : double(info.size()));
            QDateTime mtime = info.lastModified();
            entry.insert(QStringLiteral("mtime"), mtime.isValid() ? double(mtime.toMSecsSinceEpoch()) : 0.0);
            entries.append(entry);
        }

        QDir parent(dir);
        bool hasParent = dir.isRoot() == false && parent.cdUp();

        result.insert(QStringLiteral("path"), dir.absolutePath());
        result.insert(QStringLiteral("parent"), hasParent ? QJsonValue(parent.absolutePath()) : QJsonValue());
        result.insert(QStringLiteral("entries"), entries);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}

void ApiCoreDomain::slotModeChanged(Doc::Mode mode)
{
    QJsonObject data;
    data.insert(QStringLiteral("mode"), mode == Doc::Design ? QStringLiteral("design") : QStringLiteral("operate"));
    m_server->broadcast(QStringLiteral("core.mode.changed"), data, m_pendingOriginClientId, false);
}

void ApiCoreDomain::slotBpmNumberChanged(int bpm)
{
    Q_UNUSED(bpm)
    m_server->broadcast(QStringLiteral("core.bpm.changed"), bpmStateToJson(m_doc->inputOutputMap()),
                        m_pendingOriginClientId, false);
}

void ApiCoreDomain::slotBeatGeneratorTypeChanged()
{
    m_server->broadcast(QStringLiteral("core.bpm.changed"), bpmStateToJson(m_doc->inputOutputMap()),
                        m_pendingOriginClientId, false);
}

void ApiCoreDomain::slotBeat()
{
    // Ungated on purpose, despite 00-conventions.md §5's ">~2Hz" rule of
    // thumb: the web UI contract has clients merely listen for core.beat
    // (no subscribe step), and the frame is tiny and never faster than
    // BPM_MAX/60 Hz. Revisit if a client count makes this measurable.
    QJsonObject data;
    data.insert(QStringLiteral("bpm"), m_doc->inputOutputMap()->bpmNumber());
    m_server->broadcast(QStringLiteral("core.beat"), data, QString(), false);
}

void ApiCoreDomain::slotHistoryChanged()
{
    m_historyTimer->start(); // (re)arms the single-shot - coalesces bursts
}

void ApiCoreDomain::slotBroadcastHistoryChanged()
{
    m_server->broadcast(QStringLiteral("core.history.changed"), historyStateToJson(), QString(), false);
}

void ApiCoreDomain::slotDocRevisionChanged(quint32 revision)
{
    Q_UNUSED(revision)
    // Deliberately empty: docRevisionChanged fires for every structural
    // change, not just a full document replace, so it cannot be used to
    // broadcast core.project.loaded (Doc doesn't record *why* it was
    // bumped). core.project.new/open/close call broadcastProjectLoaded()
    // themselves instead, where the reason is unambiguous.
}

void ApiCoreDomain::broadcastProjectLoaded(const QString &reason, const QString &originClientId)
{
    QJsonObject data;
    data.insert(QStringLiteral("reason"), reason);
    data.insert(QStringLiteral("project"), projectMetadataToJson(m_doc, projectHost()));
    m_server->broadcast(QStringLiteral("core.project.loaded"), data, originClientId, false);
}

void ApiCoreDomain::slotRecentFilesChanged()
{
    ApiProjectHost *a = projectHost();
    QJsonArray files;
    if (a)
    {
        for (const QString &path : a->recentFiles())
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("filePath"), path);
            entry.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
            files.append(entry);
        }
    }
    QJsonObject data;
    data.insert(QStringLiteral("files"), files);
    m_server->broadcast(QStringLiteral("core.project.recentFilesChanged"), data, QString(), false);
}

void ApiCoreDomain::slotWorkingPathChanged(QString path)
{
    Q_UNUSED(path)
    // Part of core.settings.changed
    QSettings settings;
    QJsonObject data;
    data.insert(QStringLiteral("locale"), settings.value(QStringLiteral(SETTINGS_LANGUAGE)).toString());
    data.insert(QStringLiteral("defaultWorkingPath"), path);
    data.insert(QStringLiteral("masterTimerFrequencyHz"), settings.value(QStringLiteral(MASTERTIMER_FREQUENCY)).toInt());
    m_server->broadcast(QStringLiteral("core.settings.changed"), data, QString(), false);
}
