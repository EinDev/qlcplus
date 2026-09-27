/*
  Q Light Controller Plus - Control API
  apifunctionsdomain.cpp

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
#include <limits>
#include <QJsonValue>
#include <QFileInfo>
#include <QSet>
#include <QHash>

#include "apifunctionsdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "function.h"
#include "functionparent.h"
#include "mastertimer.h"
#include "universe.h"
#include "scenevalue.h"
#include "scene.h"
#include "chaser.h"
#include "chaserstep.h"
#include "sequence.h"
#include "efx.h"
#include "collection.h"
// scriptwrapper.h, not script.h: on a qmlui build the engine DLL compiles the
// JavaScript scriptv4 Script, and instantiateFunction()'s `new Script(doc)`
// must allocate that class' size, not the legacy line-command Script's.
#include "scriptwrapper.h"
#include "rgbmatrix.h"
#include "show.h"
#include "audio.h"
#include "audiodecoder.h"
#include "audioparameters.h"
#include "video.h"
#include "mediaassets.h"
#include "fixture.h"
#include "doc.h"

namespace {

// functionId is carried as a JSON string on the wire (functions-core.yaml),
// unlike io.*'s plain-integer universeId - toUInt()'s ok-flag distinguishes
// "missing/non-numeric" from a real (if unpatched) id, both of which are
// reported as NOT_FOUND here rather than INVALID_PARAMS, matching how
// ApiIoDomain treats an out-of-range universeId. "id" is accepted as an
// alias of "functionId" (the shorter spelling the web UI contract uses,
// e.g. functions.pause {id, paused}); a JSON number is tolerated too.
Function *findFunction(Doc *doc, const QJsonObject &params)
{
    QJsonValue value = params.value(QStringLiteral("functionId"));
    if (value.isUndefined())
        value = params.value(QStringLiteral("id"));

    bool ok = false;
    quint32 functionId = 0;
    if (value.isDouble())
    {
        // Range-check before the cast: converting a double outside quint32's
        // range (1e300, -1) to quint32 is undefined behaviour, not "some id".
        double d = value.toDouble();
        ok = d >= 0 && d <= double(std::numeric_limits<quint32>::max());
        functionId = ok ? quint32(d) : 0;
    }
    else
    {
        functionId = value.toString().toUInt(&ok);
    }
    return ok ? doc->function(functionId) : nullptr;
}

Function::TempoType tempoTypeFromJson(const QJsonValue &value)
{
    QString str = value.toString();
    if (str == QStringLiteral("Time"))
        return Function::Time;
    if (str == QStringLiteral("Beats"))
        return Function::Beats;
    return Function::Original;
}

QJsonObject conflictDetails(Doc *doc)
{
    QJsonObject details;
    details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return details;
}

QJsonObject docRevisionResult(Doc *doc)
{
    QJsonObject result;
    result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return result;
}

QString sceneValueKey(quint32 fxi, quint32 channel)
{
    return QStringLiteral("%1.%2").arg(fxi).arg(channel);
}

// Splits a FunctionsSceneValues map key ("<fixtureId>.<channel>") - see
// functions-core.yaml's FunctionsSceneValues schema. Returns false (leaving
// fxi/channel untouched) for anything malformed, so callers can skip a bad
// entry rather than crash on it.
bool parseSceneValueKey(const QString &key, quint32 &fxi, quint32 &channel)
{
    int dot = key.indexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == key.length() - 1)
        return false;

    bool okFxi = false;
    bool okCh = false;
    quint32 parsedFxi = key.left(dot).toUInt(&okFxi);
    quint32 parsedCh = key.mid(dot + 1).toUInt(&okCh);
    if (okFxi == false || okCh == false)
        return false;

    fxi = parsedFxi;
    channel = parsedCh;
    return true;
}

QJsonObject sceneValueListToJson(const QList<SceneValue> &values)
{
    QJsonObject obj;
    for (const SceneValue &sv : values)
        obj.insert(sceneValueKey(sv.fxi, sv.channel), int(sv.value));
    return obj;
}

QJsonArray idListToJson(const QList<quint32> &ids)
{
    QJsonArray arr;
    for (quint32 id : ids)
        arr.append(QString::number(id));
    return arr;
}

QJsonArray sceneChannelGroupRefsToJson(Scene *scene)
{
    QJsonArray arr;
    QList<quint32> ids = scene->channelGroups();
    QList<uchar> levels = scene->channelGroupsLevels();
    for (int i = 0; i < ids.count(); i++)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), QString::number(ids.at(i)));
        obj.insert(QStringLiteral("level"), i < levels.count() ? int(levels.at(i)) : 0);
        arr.append(obj);
    }
    return arr;
}

QJsonObject sceneDetailToJson(Scene *scene)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("functionId"), QString::number(scene->id()));
    obj.insert(QStringLiteral("values"), sceneValueListToJson(scene->values()));
    obj.insert(QStringLiteral("fixtures"), idListToJson(scene->fixtures()));
    obj.insert(QStringLiteral("fixtureGroups"), idListToJson(scene->fixtureGroups()));
    obj.insert(QStringLiteral("channelGroups"), sceneChannelGroupRefsToJson(scene));
    obj.insert(QStringLiteral("palettes"), idListToJson(scene->palettes()));
    quint32 blendFid = scene->blendFunctionID();
    obj.insert(QStringLiteral("blendFunctionId"), blendFid == Function::invalidId()
               ? QJsonValue() : QJsonValue(QString::number(blendFid)));
    return obj;
}

// duration is authored server-side, not trusted from the client - mirrors
// ChaserStep::loadXML's own behaviour (functions-core.yaml's FunctionsChaserStep/
// FunctionsSequenceStep description): fadeIn+hold wins, except hold's own
// infinite sentinel (Function::infiniteSpeed()) propagates through unchanged.
quint32 deriveStepDuration(quint32 fadeIn, quint32 hold)
{
    if (hold == Function::infiniteSpeed())
        return hold;
    return fadeIn + hold;
}

// Builds a ChaserStep from a FunctionsChaserStep/FunctionsSequenceStep wire
// object. Which shape applies is resolved server-side from the target
// function's own type (isSequence), not a wire discriminator field - see
// functions.steps.addStep/replaceStep's params.step description.
bool chaserStepFromJson(Chaser *chaser, bool isSequence, const QJsonObject &stepObj,
                         ChaserStep &outStep, QString &errorMessage)
{
    outStep = ChaserStep();
    outStep.fadeIn = quint32(stepObj.value(QStringLiteral("fadeIn")).toDouble());
    outStep.hold = quint32(stepObj.value(QStringLiteral("hold")).toDouble());
    outStep.fadeOut = quint32(stepObj.value(QStringLiteral("fadeOut")).toDouble());
    outStep.duration = deriveStepDuration(outStep.fadeIn, outStep.hold);
    outStep.note = stepObj.value(QStringLiteral("note")).toString();

    if (isSequence)
    {
        Sequence *sequence = qobject_cast<Sequence *>(chaser);
        outStep.fid = sequence->boundSceneID();

        QJsonObject valuesObj = stepObj.value(QStringLiteral("values")).toObject();
        for (auto it = valuesObj.constBegin(); it != valuesObj.constEnd(); ++it)
        {
            quint32 fxi = 0;
            quint32 channel = 0;
            if (parseSceneValueKey(it.key(), fxi, channel) == false)
                continue;
            int value = qBound(0, it.value().toInt(), 255);
            outStep.values.append(SceneValue(fxi, channel, uchar(value)));
        }
    }
    else
    {
        bool ok = false;
        quint32 targetId = stepObj.value(QStringLiteral("targetFunctionId")).toString().toUInt(&ok);
        if (ok == false)
        {
            errorMessage = QStringLiteral("Invalid or missing targetFunctionId");
            return false;
        }
        outStep.fid = targetId;
    }

    return true;
}

QJsonObject chaserStepToJson(const ChaserStep &step, bool isSequence)
{
    QJsonObject obj;
    if (isSequence == false)
        obj.insert(QStringLiteral("targetFunctionId"), QString::number(step.fid));
    obj.insert(QStringLiteral("fadeIn"), double(step.fadeIn));
    obj.insert(QStringLiteral("hold"), double(step.hold));
    obj.insert(QStringLiteral("fadeOut"), double(step.fadeOut));
    obj.insert(QStringLiteral("duration"), double(step.duration));
    if (step.note.isEmpty() == false)
        obj.insert(QStringLiteral("note"), step.note);
    if (isSequence)
        obj.insert(QStringLiteral("values"), sceneValueListToJson(step.values));
    return obj;
}

QJsonObject chaserDetailToJson(Chaser *chaser, bool isSequence)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("functionId"), QString::number(chaser->id()));

    QJsonArray steps;
    for (const ChaserStep &step : chaser->steps())
        steps.append(chaserStepToJson(step, isSequence));
    obj.insert(QStringLiteral("steps"), steps);

    obj.insert(QStringLiteral("fadeInMode"), Chaser::speedModeToString(chaser->fadeInMode()));
    obj.insert(QStringLiteral("fadeOutMode"), Chaser::speedModeToString(chaser->fadeOutMode()));
    obj.insert(QStringLiteral("durationMode"), Chaser::speedModeToString(chaser->durationMode()));
    return obj;
}

QJsonObject sequenceDetailToJson(Sequence *sequence)
{
    // Sequence is-a Chaser (shares steps/speed-mode storage) - reuse the
    // Chaser detail shape and just add the one Sequence-specific field.
    QJsonObject obj = chaserDetailToJson(sequence, true);
    obj.insert(QStringLiteral("boundSceneId"), QString::number(sequence->boundSceneID()));
    return obj;
}

// Non-Scene/Chaser/Sequence types intentionally return only the minimal
// {functionId} shape for now (see this task's own scope note) rather than
// the fuller FunctionsAudioDetail/FunctionsRgbMatrixDetail/FunctionsScriptDetail/
// FunctionsShowDetail/FunctionsVideoDetail schemas (functions-advanced.yaml
// territory) - a deliberate, tracked gap, not an oversight.
/**
 * Audio/Video source as the API reports it: `source` is the path relative
 * to the workspace directory for a managed copy (what the .qxw stores), the
 * raw path/URL otherwise; `managed` says whether it lives in the project's
 * media store; `importPending` is true while a background copy of it is
 * still running (the function is relinked to the copy once that ends).
 * `origin` is the absolute path a managed copy was imported from (null
 * when unknown or external), `originAvailable` whether that file still
 * exists, `originChanged` whether it differs from the copy - the
 * functions.media.reload trigger.
 */
void mediaSourceToJson(Doc *doc, Function *function, const QString &source, QJsonObject &obj)
{
    MediaAssets *assets = doc->assets();
    bool managed = assets->isManaged(source);
    // an untitled project has no workspace path yet: normalize would then
    // relativize against the process CWD, so keep the absolute path there
    bool relative = managed && doc->workspacePath().isEmpty() == false;
    obj.insert(QStringLiteral("source"), relative ? doc->normalizeComponentPath(source) : source);
    obj.insert(QStringLiteral("managed"), managed);
    obj.insert(QStringLiteral("importPending"), assets->pendingImports().contains(source));

    MediaOrigin origin = assets->originOf(function);
    obj.insert(QStringLiteral("origin"), origin.isValid() ? QJsonValue(origin.path) : QJsonValue());
    obj.insert(QStringLiteral("originAvailable"), assets->originAvailable(function));
    obj.insert(QStringLiteral("originChanged"), assets->originChanged(function));
}

QString bpmStateToString(Audio::BpmAnalysisState state)
{
    switch (state)
    {
    case Audio::Analyzing: return QStringLiteral("analyzing");
    case Audio::Done:      return QStringLiteral("done");
    case Audio::Failed:    return QStringLiteral("failed");
    default:               return QStringLiteral("notAnalyzed");
    }
}

/** FunctionsAudioConfig (functions-advanced.yaml): the editable Audio
 *  properties plus what AudioEditor.qml shows read-only (decoder info, BPM) */
QJsonObject audioConfigToJson(Audio *audio)
{
    QJsonObject cfg;
    cfg.insert(QStringLiteral("sourceFileName"), audio->getSourceFileName());
    cfg.insert(QStringLiteral("volume"), audio->volume());
    cfg.insert(QStringLiteral("duration"), double(audio->totalDuration()));
    cfg.insert(QStringLiteral("audioDevice"), audio->audioDevice());
    cfg.insert(QStringLiteral("muted"), audio->muted());

    QJsonObject bpm;
    bpm.insert(QStringLiteral("state"), bpmStateToString(audio->bpmAnalysisState()));
    bpm.insert(QStringLiteral("value"), audio->detectedBpm());
    bpm.insert(QStringLiteral("confidence"), audio->bpmConfidence());
    cfg.insert(QStringLiteral("bpm"), bpm);

    AudioDecoder *decoder = audio->getAudioDecoder();
    AudioParameters ap = decoder != nullptr ? decoder->audioParameters() : AudioParameters();
    cfg.insert(QStringLiteral("sampleRate"), decoder != nullptr ? int(ap.sampleRate()) : 0);
    cfg.insert(QStringLiteral("channels"), decoder != nullptr ? ap.channels() : 0);
    cfg.insert(QStringLiteral("bitrate"), decoder != nullptr ? decoder->bitrate() : 0);
    return cfg;
}

QString outputModeToString(Video::OutputMode mode)
{
    switch (mode)
    {
    case Video::Fullscreen: return QStringLiteral("fullscreen");
    case Video::Spout:      return QStringLiteral("spout");
    default:                return QStringLiteral("windowed");
    }
}

/** FunctionsVideoConfig (functions-advanced.yaml) */
QJsonObject videoConfigToJson(Video *video)
{
    QJsonObject cfg;
    cfg.insert(QStringLiteral("sourceUrl"), video->sourceUrl());
    cfg.insert(QStringLiteral("isPicture"), video->isPicture());

    QRect geometry = video->customGeometry();
    if (geometry.isNull())
    {
        cfg.insert(QStringLiteral("customGeometry"), QJsonValue());
    }
    else
    {
        QJsonObject g;
        g.insert(QStringLiteral("x"), geometry.x());
        g.insert(QStringLiteral("y"), geometry.y());
        g.insert(QStringLiteral("width"), geometry.width());
        g.insert(QStringLiteral("height"), geometry.height());
        cfg.insert(QStringLiteral("customGeometry"), g);
    }

    QJsonObject rot;
    rot.insert(QStringLiteral("x"), double(video->rotation().x()));
    rot.insert(QStringLiteral("y"), double(video->rotation().y()));
    rot.insert(QStringLiteral("z"), double(video->rotation().z()));
    cfg.insert(QStringLiteral("rotation"), rot);

    cfg.insert(QStringLiteral("zIndex"), video->zIndex());
    cfg.insert(QStringLiteral("screen"), video->screen());
    cfg.insert(QStringLiteral("fullscreen"), video->fullscreen());
    cfg.insert(QStringLiteral("outputMode"), outputModeToString(video->outputMode()));

    QJsonObject spout;
    spout.insert(QStringLiteral("width"), video->spoutSize().width());
    spout.insert(QStringLiteral("height"), video->spoutSize().height());
    cfg.insert(QStringLiteral("spoutSize"), spout);
    // read-only: derived from the function name (Video::defaultSpoutSenderName)
    cfg.insert(QStringLiteral("spoutSenderName"), video->defaultSpoutSenderName());

    cfg.insert(QStringLiteral("volume"), video->volume());
    cfg.insert(QStringLiteral("muted"), video->muted());

    QSize res = video->resolution();
    if (res.isValid() && res.isEmpty() == false)
    {
        QJsonObject r;
        r.insert(QStringLiteral("width"), res.width());
        r.insert(QStringLiteral("height"), res.height());
        cfg.insert(QStringLiteral("resolution"), r);
    }
    cfg.insert(QStringLiteral("detectedDurationMs"), double(video->totalDuration()));
    cfg.insert(QStringLiteral("videoCodec"), video->videoCodec());
    cfg.insert(QStringLiteral("audioCodec"), video->audioCodec());
    return cfg;
}

QJsonObject audioDetailToJson(Doc *doc, Audio *audio)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("functionId"), QString::number(audio->id()));
    mediaSourceToJson(doc, audio, audio->getSourceFileName(), obj);
    obj.insert(QStringLiteral("config"), audioConfigToJson(audio));
    obj.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return obj;
}

QJsonObject videoDetailToJson(Doc *doc, Video *video)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("functionId"), QString::number(video->id()));
    mediaSourceToJson(doc, video, video->sourceUrl(), obj);
    obj.insert(QStringLiteral("config"), videoConfigToJson(video));
    obj.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return obj;
}

QString reloadStatusToString(MediaAssets::ReloadStatus status)
{
    switch (status)
    {
    case MediaAssets::Reloaded:   return QStringLiteral("reloaded");
    case MediaAssets::Unchanged:  return QStringLiteral("unchanged");
    case MediaAssets::Queued:     return QStringLiteral("queued");
    case MediaAssets::Missing:    return QStringLiteral("missing");
    case MediaAssets::NotManaged: return QStringLiteral("notManaged");
    case MediaAssets::Failed:     return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

/**
 * Apply a functions.create/functions.update `source` param to an Audio or
 * Video: a local file is copied into the project's media store first
 * (MediaAssets::importOrKeep - a large file comes back as the source path
 * and is relinked when the background copy lands), a `scheme://` URL is
 * passed through for Video. Returns false with @error set when the file
 * does not exist or the type takes no source. The full setters rename the
 * function after the file, so callers re-apply an explicit name afterwards.
 */
bool applyMediaSource(Doc *doc, Function *function, const QString &source, QString *error)
{
    bool isUrl = source.contains(QStringLiteral("://"));

    if (function->type() != Function::AudioType && function->type() != Function::VideoType)
    {
        *error = QStringLiteral("source is only valid for Audio and Video functions");
        return false;
    }
    if (isUrl && function->type() == Function::AudioType)
    {
        *error = QStringLiteral("Audio source must be a local file");
        return false;
    }
    if (isUrl == false && QFileInfo(source).isFile() == false)
    {
        *error = QStringLiteral("Media file not found: ") + source;
        return false;
    }

    QString stored = isUrl ? source : doc->assets()->importOrKeep(source);
    if (function->type() == Function::AudioType)
    {
        Audio *audio = static_cast<Audio *>(function);
        audio->setSourceFileName(stored);
        audio->requestBpmDetection(false);
    }
    else
    {
        static_cast<Video *>(function)->setSourceUrl(stored);
    }
    return true;
}

QHash<int, ApiFunctionsDomain::TypeDetailProvider> &typeDetailProviders()
{
    static QHash<int, ApiFunctionsDomain::TypeDetailProvider> providers;
    return providers;
}

QJsonObject typeDetailToJson(Function *function)
{
    switch (function->type())
    {
    case Function::SceneType:
        return sceneDetailToJson(qobject_cast<Scene *>(function));
    case Function::ChaserType:
        return chaserDetailToJson(qobject_cast<Chaser *>(function), false);
    case Function::SequenceType:
        return sequenceDetailToJson(qobject_cast<Sequence *>(function));
    case Function::AudioType:
        return audioDetailToJson(function->doc(), static_cast<Audio *>(function));
    case Function::VideoType:
        return videoDetailToJson(function->doc(), static_cast<Video *>(function));
    default:
    {
        ApiFunctionsDomain::TypeDetailProvider provider = typeDetailProviders().value(function->type());
        if (provider)
            return provider(function);
        QJsonObject obj;
        obj.insert(QStringLiteral("functionId"), QString::number(function->id()));
        return obj;
    }
    }
}

QJsonArray attributeFlagsToJson(int flags)
{
    QJsonArray arr;
    if (flags & Function::Multiply)
        arr.append(QStringLiteral("Multiply"));
    if (flags & Function::LastWins)
        arr.append(QStringLiteral("LastWins"));
    if (flags & Function::Single)
        arr.append(QStringLiteral("Single"));
    return arr;
}

QJsonArray attributesToJson(Function *function)
{
    QJsonArray arr;
    QList<Attribute> attrs = function->attributes();
    for (int i = 0; i < attrs.count(); i++)
    {
        const Attribute &a = attrs.at(i);
        QJsonObject obj;
        obj.insert(QStringLiteral("index"), i);
        obj.insert(QStringLiteral("name"), a.m_name);
        obj.insert(QStringLiteral("value"), a.m_value);
        obj.insert(QStringLiteral("min"), a.m_min);
        obj.insert(QStringLiteral("max"), a.m_max);
        obj.insert(QStringLiteral("overridden"), a.m_isOverridden);
        obj.insert(QStringLiteral("overrideValue"), a.m_overrideValue);
        obj.insert(QStringLiteral("flags"), attributeFlagsToJson(a.m_flags));
        arr.append(obj);
    }
    return arr;
}

QJsonObject functionSummaryToJson(Function *function)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(function->id()));
    obj.insert(QStringLiteral("name"), function->name());
    obj.insert(QStringLiteral("type"), Function::typeToString(function->type()));
    obj.insert(QStringLiteral("path"), function->path(true));
    obj.insert(QStringLiteral("hidden"), function->isVisible() == false);
    // §4b live run-state, additive (functions.status.changed carries the
    // same two flags). Read straight off the engine flags - both are plain
    // bools MasterTimer's thread writes and anyone may read.
    obj.insert(QStringLiteral("running"), function->isRunning());
    obj.insert(QStringLiteral("paused"), function->isPaused());
    return obj;
}

QJsonObject functionDetailToJson(Function *function)
{
    QJsonObject obj = functionSummaryToJson(function);
    obj.insert(QStringLiteral("runOrder"), Function::runOrderToString(function->runOrder()));
    obj.insert(QStringLiteral("direction"), Function::directionToString(function->direction()));
    obj.insert(QStringLiteral("tempoType"), Function::tempoTypeToString(function->tempoType()));
    obj.insert(QStringLiteral("fadeInSpeed"), double(function->fadeInSpeed()));
    obj.insert(QStringLiteral("fadeOutSpeed"), double(function->fadeOutSpeed()));
    obj.insert(QStringLiteral("duration"), double(function->duration()));
    obj.insert(QStringLiteral("totalDuration"), double(function->totalDuration()));
    obj.insert(QStringLiteral("blendMode"), Universe::blendModeToString(function->blendMode()));
    obj.insert(QStringLiteral("attributes"), attributesToJson(function));
    obj.insert(QStringLiteral("typeDetail"), typeDetailToJson(function));
    return obj;
}

QString defaultFunctionName(Function::Type type)
{
    switch (type)
    {
    case Function::SceneType:      return QStringLiteral("New Scene");
    case Function::ChaserType:     return QStringLiteral("New Chaser");
    case Function::EFXType:        return QStringLiteral("New EFX");
    case Function::CollectionType: return QStringLiteral("New Collection");
    case Function::ScriptType:     return QStringLiteral("New Script");
    case Function::RGBMatrixType:  return QStringLiteral("New RGB Matrix");
    case Function::ShowType:       return QStringLiteral("New Show");
    case Function::SequenceType:   return QStringLiteral("New Sequence");
    case Function::AudioType:      return QStringLiteral("New Audio");
    case Function::VideoType:      return QStringLiteral("New Video");
    default:                       return QStringLiteral("New Function");
    }
}

// Sequence is deliberately excluded here - functions.create's Sequence
// branch needs to create+add a hidden bound Scene first (see
// FunctionsCreateRequest.params' own description in functions-core.yaml)
// before the Sequence itself can be constructed, so it's handled directly
// by the functions.create handler instead of through this generic factory.
Function *instantiateFunction(Doc *doc, Function::Type type)
{
    switch (type)
    {
    case Function::SceneType:      return new Scene(doc);
    case Function::ChaserType:     return new Chaser(doc);
    case Function::EFXType:        return new EFX(doc);
    case Function::CollectionType: return new Collection(doc);
    case Function::ScriptType:     return new Script(doc);
    case Function::RGBMatrixType:  return new RGBMatrix(doc);
    case Function::ShowType:       return new Show(doc);
    case Function::AudioType:      return new Audio(doc);
    case Function::VideoType:      return new Video(doc);
    default:                       return nullptr;
    }
}

} // namespace

ApiFunctionsDomain::ApiFunctionsDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    // engine-DLL object: string-based connection
    connect(m_doc->assets(), SIGNAL(originReloaded(quint32,QString,QString,quint32)),
            this, SLOT(slotMediaOriginReloaded(quint32,QString,QString,quint32)));

    // Run-state feed for functions.status.changed. MasterTimer emits these
    // from its own thread for every start/stop whatever the source (API,
    // VC, UI) - Qt::AutoConnection queues them onto this thread. Engine-DLL
    // objects, so string-based connects (see apiiodomain.cpp for why).
    connect(m_doc->masterTimer(), SIGNAL(functionStarted(quint32)),
            this, SLOT(slotFunctionStarted(quint32)));
    connect(m_doc->masterTimer(), SIGNAL(functionStopped(quint32)),
            this, SLOT(slotFunctionStopped(quint32)));
    connect(m_doc, SIGNAL(functionAdded(quint32)),
            this, SLOT(slotFunctionAdded(quint32)));
    for (Function *function : m_doc->functions())
        watchFunction(function);

    registerMethods();
}

void ApiFunctionsDomain::watchFunction(Function *function)
{
    if (function == nullptr)
        return;
    connect(function, SIGNAL(pauseChanged(quint32,bool)),
            this, SLOT(slotFunctionPauseChanged(quint32,bool)), Qt::UniqueConnection);
}

void ApiFunctionsDomain::broadcastStatus(quint32 id, bool running, bool paused)
{
    QJsonObject data;
    // Both spellings: "functionId" is the functions-core.yaml
    // FunctionsStatusData field, "id" the web UI contract's.
    data.insert(QStringLiteral("id"), QString::number(id));
    data.insert(QStringLiteral("functionId"), QString::number(id));
    data.insert(QStringLiteral("running"), running);
    data.insert(QStringLiteral("paused"), paused);
    Function *function = m_doc->function(id);
    data.insert(QStringLiteral("elapsed"), function != nullptr && running ? int(function->elapsed()) : 0);
    m_server->broadcast(QStringLiteral("functions.status.changed"), data, QString(), false);
}

void ApiFunctionsDomain::slotFunctionStarted(quint32 id)
{
    Function *function = m_doc->function(id);
    if (function == nullptr)
        return; // deleted between MasterTimer's emit and delivery here
    // Re-read the flags: this slot runs queued, and the function may already
    // have stopped again in the meantime (a stopped event follows either way).
    broadcastStatus(id, function->isRunning(), function->isPaused());
}

void ApiFunctionsDomain::slotFunctionStopped(quint32 id)
{
    // Function::postRun() clears m_paused along with m_running before
    // stopped() is emitted, so a stopped function is never paused.
    broadcastStatus(id, false, false);
}

void ApiFunctionsDomain::slotFunctionPauseChanged(quint32 id, bool paused)
{
    Function *function = m_doc->function(id);
    if (function == nullptr)
        return;
    broadcastStatus(id, function->isRunning(), paused);
}

void ApiFunctionsDomain::slotFunctionAdded(quint32 id)
{
    watchFunction(m_doc->function(id));
}

void ApiFunctionsDomain::slotMediaOriginReloaded(quint32 functionId, QString oldPath, QString newPath, quint32 oldDuration)
{
    Q_UNUSED(oldPath)
    Q_UNUSED(newPath)
    Q_UNUSED(oldDuration)

    Function *function = m_doc->function(functionId);
    if (function == nullptr)
        return;

    QJsonObject data = typeDetailToJson(function);
    data.insert(QStringLiteral("status"), reloadStatusToString(MediaAssets::Reloaded));
    m_server->broadcast(QStringLiteral("functions.media.reloaded"), data, QString(), false);
}

void ApiFunctionsDomain::setTypeDetailProvider(int functionType, TypeDetailProvider provider)
{
    typeDetailProviders().insert(functionType, provider);
}

QJsonObject ApiFunctionsDomain::typeDetail(Function *function)
{
    return typeDetailToJson(function);
}

QJsonObject ApiFunctionsDomain::summary(Function *function)
{
    return functionSummaryToJson(function);
}

QJsonArray ApiFunctionsDomain::attributes(Function *function)
{
    return attributesToJson(function);
}

bool ApiFunctionsDomain::applyMediaSource(Doc *doc, Function *function, const QString &source, QString *error)
{
    return ::applyMediaSource(doc, function, source, error);
}

void ApiFunctionsDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("functions.start"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        uint fadeIn = params.contains(QStringLiteral("overrideFadeIn"))
                ? uint(params.value(QStringLiteral("overrideFadeIn")).toInt())
                : Function::defaultSpeed();
        uint fadeOut = params.contains(QStringLiteral("overrideFadeOut"))
                ? uint(params.value(QStringLiteral("overrideFadeOut")).toInt())
                : Function::defaultSpeed();
        uint duration = params.contains(QStringLiteral("overrideDuration"))
                ? uint(params.value(QStringLiteral("overrideDuration")).toInt())
                : Function::defaultSpeed();
        Function::TempoType tempoType = params.contains(QStringLiteral("overrideTempoType"))
                ? tempoTypeFromJson(params.value(QStringLiteral("overrideTempoType")))
                : Function::Original;

        // optional start offset (FunctionsStartRequest.startTime): a Show
        // plays from that point of its timeline, media seeks into the file
        quint32 startTime = quint32(qMax(0, params.value(QStringLiteral("startTime")).toInt()));
        function->start(doc->masterTimer(), FunctionParent::master(FunctionParent::ControlApi),
                         startTime, fadeIn, fadeOut, duration, tempoType);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    dispatcher->registerMethod(QStringLiteral("functions.stop"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        bool preserveAttributes = params.value(QStringLiteral("preserveAttributes")).toBool(false);
        function->stop(FunctionParent::master(FunctionParent::ControlApi), preserveAttributes);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // Registered under both names: functions.setPause is functions-core.yaml's
    // spelling, functions.pause the web UI contract's ({id, paused}). Same
    // handler, same semantics (findFunction() accepts either id spelling).
    // The resulting functions.status.changed is broadcast from
    // slotFunctionPauseChanged() via Function::pauseChanged - so it fires
    // only when the flag actually changed, and also for pauses requested by
    // anything other than this API.
    ApiDispatcher::Handler setPauseHandler = [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        QJsonValue paused = params.value(QStringLiteral("paused"));
        if (paused.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("paused must be a boolean")));
            return;
        }

        function->setPause(paused.toBool());
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    };
    dispatcher->registerMethod(QStringLiteral("functions.setPause"), setPauseHandler);
    dispatcher->registerMethod(QStringLiteral("functions.pause"), setPauseHandler);

    dispatcher->registerMethod(QStringLiteral("functions.stopAll"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        // Prefer the host's own "stop everything" (qmlui's Q_INVOKABLE
        // App::stopAllFunctions(): disables the Function Manager preview and
        // shuts fullscreen video down before MasterTimer::stopAllFunctions())
        // so this matches the toolbar action exactly; reached via the meta
        // object rather than ApiProjectHost so controlapi stays App-free.
        // Fallback for a bare Doc (tests, headless): MasterTimer directly,
        // which stops functions started by anyone, VC widgets included.
        // Either way the call blocks until MasterTimer has actually flushed
        // its list (normally one or two 20ms ticks) - functions.status.changed
        // events for each stopped function follow via slotFunctionStopped().
        QObject *host = m_server->parent();
        if (host != nullptr && host->metaObject()->indexOfMethod("stopAllFunctions()") >= 0)
            QMetaObject::invokeMethod(host, "stopAllFunctions", Qt::DirectConnection);
        else
            doc->masterTimer()->stopAllFunctions();

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    /*********************************************************************
     * Generic CRUD (functions-core.yaml, all 10 Function types)
     *********************************************************************/

    dispatcher->registerMethod(QStringLiteral("functions.list"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        // Function::Type is a bitmask enum - OR the requested types together
        // so filtering is a single AND-test per function, same trick the
        // engine itself uses (Doc::functionsByType()).
        int typeMask = 0;
        for (const QJsonValue &v : params.value(QStringLiteral("typeFilter")).toArray())
            typeMask |= int(Function::stringToType(v.toString()));

        QString pathFilter = params.value(QStringLiteral("pathFilter")).toString();

        QJsonArray functions;
        for (Function *function : doc->functions())
        {
            if (typeMask != 0 && (int(function->type()) & typeMask) == 0)
                continue;
            if (pathFilter.isEmpty() == false && function->path(true).startsWith(pathFilter) == false)
                continue;
            functions.append(functionSummaryToJson(function));
        }

        QJsonObject result;
        result.insert(QStringLiteral("functions"), functions);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("functions.get"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        session->send(ApiEnvelope::buildOkResponse(id, functionDetailToJson(function)));
    });

    dispatcher->registerMethod(QStringLiteral("functions.create"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        Function::Type type = Function::stringToType(params.value(QStringLiteral("type")).toString());
        if (type == Function::Undefined)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Unknown function type")));
            return;
        }

        // A Sequence depends on a bound Scene from the moment it exists
        // (FunctionsSequenceDetail.boundSceneId is required/non-null) - create
        // a hidden Scene first, mirroring qmlui/functionmanager.cpp's own
        // "New Sequence" action, and clean it back up if the Sequence itself
        // then fails to add.
        Scene *boundScene = nullptr;
        Function *function = nullptr;
        if (type == Function::SequenceType)
        {
            boundScene = new Scene(doc);
            boundScene->setVisible(false);
            boundScene->setName(QStringLiteral("New Sequence Scene"));
            if (doc->addFunction(boundScene) == false)
            {
                delete boundScene;
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                                QStringLiteral("Could not add the Sequence's bound Scene")));
                return;
            }

            Sequence *sequence = new Sequence(doc);
            sequence->setBoundSceneID(boundScene->id());
            function = sequence;
        }
        else
        {
            function = instantiateFunction(doc, type);
        }

        if (function == nullptr)
        {
            if (boundScene != nullptr)
                doc->deleteFunction(boundScene->id());
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                                                            QStringLiteral("Function type not creatable via the API")));
            return;
        }

        QString name = params.value(QStringLiteral("name")).toString();
        function->setName(name.isEmpty() ? defaultFunctionName(type) : name);

        if (params.value(QStringLiteral("path")).isString())
            function->setPath(params.value(QStringLiteral("path")).toString());

        // fixtures is only meaningful for a handful of types at creation
        // time (functions-core.yaml's own wording: "ignored by types that
        // don't take fixtures at creation time") - Scene is the one handled
        // here; every other type simply ignores the field for now.
        if (type == Function::SceneType)
        {
            Scene *scene = qobject_cast<Scene *>(function);
            for (const QJsonValue &v : params.value(QStringLiteral("fixtures")).toArray())
            {
                bool ok = false;
                quint32 fixtureId = v.toString().toUInt(&ok);
                if (ok && doc->fixture(fixtureId) != nullptr)
                    scene->addFixture(fixtureId);
            }
        }

        // Audio/Video: the media file to use, validated before the function
        // exists so a bad path leaves nothing behind
        QString source = params.value(QStringLiteral("source")).toString();
        if (params.contains(QStringLiteral("source")))
        {
            bool isUrl = source.contains(QStringLiteral("://"));
            QString sourceError;
            if (type != Function::AudioType && type != Function::VideoType)
                sourceError = QStringLiteral("source is only valid for Audio and Video functions");
            else if (isUrl && type == Function::AudioType)
                sourceError = QStringLiteral("Audio source must be a local file");
            else if (isUrl == false && QFileInfo(source).isFile() == false)
                sourceError = QStringLiteral("Media file not found: ") + source;

            if (sourceError.isEmpty() == false)
            {
                delete function;
                if (boundScene != nullptr)
                    doc->deleteFunction(boundScene->id());
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, sourceError));
                return;
            }
        }

        if (doc->addFunction(function) == false)
        {
            delete function;
            if (boundScene != nullptr)
                doc->deleteFunction(boundScene->id());
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                            QStringLiteral("Could not add function")));
            return;
        }

        if (params.contains(QStringLiteral("source")))
        {
            QString sourceError;
            applyMediaSource(doc, function, source, &sourceError);
            // the setters name the function after the file - an explicit
            // name wins, like the Function Manager's own editors
            if (name.isEmpty() == false)
                function->setName(name);
        }

        QJsonObject result;
        result.insert(QStringLiteral("functionId"), QString::number(function->id()));
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        // Only the function the client actually asked to create gets a
        // functions.created broadcast - the auto-created hidden bound Scene
        // is an implementation detail (also true of the real Function
        // Manager's own "New Sequence" action), not a resource the client
        // asked for; it's still fully visible/queryable afterward via
        // functions.get's boundSceneId or a plain functions.list.
        QJsonObject data;
        data.insert(QStringLiteral("function"), functionSummaryToJson(function));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.created"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.delete"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        quint32 functionId = function->id();
        if (doc->deleteFunction(functionId) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                            QStringLiteral("Could not delete function")));
            return;
        }

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(functionId));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.deleted"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.rename"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        // Function::setName() emits nameChanged() only, which Doc relays to
        // setModified() (slotFunctionNameChanged) - no explicit setModified()
        // needed here.
        function->setName(params.value(QStringLiteral("name")).toString());

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(function->id()));
        data.insert(QStringLiteral("name"), function->name());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.renamed"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.move"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        QString path = params.value(QStringLiteral("path")).toString();
        QJsonArray requestedIds = params.value(QStringLiteral("functionIds")).toArray();

        QJsonArray movedIds;
        for (const QJsonValue &v : requestedIds)
        {
            bool ok = false;
            quint32 functionId = v.toString().toUInt(&ok);
            Function *function = ok ? doc->function(functionId) : nullptr;
            if (function == nullptr)
                continue;

            // Function::setPath() doesn't emit changed()/setModified() on its
            // own (unlike most other Function property setters) - bumped
            // explicitly below instead.
            function->setPath(path);
            movedIds.append(QString::number(functionId));
        }

        if (requestedIds.isEmpty() == false && movedIds.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function(s)")));
            return;
        }

        if (movedIds.isEmpty() == false)
            doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionIds"), movedIds);
        data.insert(QStringLiteral("path"), path);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.moved"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.update"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        if (params.contains(QStringLiteral("source")))
        {
            // Audio/Video only: validated first so nothing else changes on a
            // bad path. Renames the function after the new file, like the
            // editors' own "Replace file" does.
            QString sourceError;
            if (applyMediaSource(doc, function, params.value(QStringLiteral("source")).toString(), &sourceError) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, sourceError));
                return;
            }
        }

        if (params.contains(QStringLiteral("runOrder")))
            function->setRunOrder(Function::stringToRunOrder(params.value(QStringLiteral("runOrder")).toString()));
        if (params.contains(QStringLiteral("direction")))
            function->setDirection(Function::stringToDirection(params.value(QStringLiteral("direction")).toString()));
        if (params.contains(QStringLiteral("tempoType")))
            function->setTempoType(Function::stringToTempoType(params.value(QStringLiteral("tempoType")).toString()));
        if (params.contains(QStringLiteral("fadeInSpeed")))
            function->setFadeInSpeed(quint32(params.value(QStringLiteral("fadeInSpeed")).toDouble()));
        if (params.contains(QStringLiteral("fadeOutSpeed")))
            function->setFadeOutSpeed(quint32(params.value(QStringLiteral("fadeOutSpeed")).toDouble()));
        if (params.contains(QStringLiteral("duration")))
            function->setDuration(quint32(params.value(QStringLiteral("duration")).toDouble()));
        if (params.contains(QStringLiteral("blendMode")))
            // Universe::setBlendMode() doesn't emit changed()/setModified()
            // on its own - bumped explicitly below regardless of which
            // optional fields were actually present, since this whole call
            // is already gated behind an explicit baseRevision (i.e. the
            // client always intends a structural mutation by calling this).
            function->setBlendMode(Universe::stringToBlendMode(params.value(QStringLiteral("blendMode")).toString()));

        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(function->id()));
        data.insert(QStringLiteral("runOrder"), Function::runOrderToString(function->runOrder()));
        data.insert(QStringLiteral("direction"), Function::directionToString(function->direction()));
        data.insert(QStringLiteral("tempoType"), Function::tempoTypeToString(function->tempoType()));
        data.insert(QStringLiteral("fadeInSpeed"), double(function->fadeInSpeed()));
        data.insert(QStringLiteral("fadeOutSpeed"), double(function->fadeOutSpeed()));
        data.insert(QStringLiteral("duration"), double(function->duration()));
        data.insert(QStringLiteral("blendMode"), Universe::blendModeToString(function->blendMode()));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.updated"), data, session->clientId(), false);
    });

    /*********************************************************************
     * Audio/Video media (functions-advanced.yaml)
     *********************************************************************/

    // Re-import a managed copy from the file it was imported from (the
    // editors' Reload button). The previous copy stays on disk; a large
    // origin is copied in the background and applied when it lands
    // (status "queued", functions.media.reloaded follows). An external
    // (unmanaged) source is re-probed in place via the full setter.
    dispatcher->registerMethod(QStringLiteral("functions.media.reload"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }
        if (function->type() != Function::AudioType && function->type() != Function::VideoType)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Only Audio and Video functions have a media source")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        MediaAssets *assets = doc->assets();
        MediaAssets::ReloadStatus status;
        QString error;
        QString source = assets->sourceOf(function);

        if (source.isEmpty() == false && assets->isManaged(source) == false)
        {
            // external file: same path through the full setter re-reads
            // duration/decoder (a Video is probed by App on originReloaded)
            if (QFileInfo(source).isFile() == false)
            {
                status = MediaAssets::Missing;
                error = QStringLiteral("Media file not found: ") + source;
            }
            else
            {
                QString name = function->name();
                if (function->type() == Function::AudioType)
                {
                    Audio *audio = static_cast<Audio *>(function);
                    audio->setSourceFileName(source);
                    audio->requestBpmDetection(false);
                }
                else
                {
                    static_cast<Video *>(function)->setSourceUrl(source);
                }
                function->setName(name);
                status = MediaAssets::Unchanged;
            }
        }
        else
        {
            QString stored = assets->importOrigin(function, &status, &error);
            if (status == MediaAssets::Reloaded)
                assets->applyReload(function, stored);
        }

        if (status == MediaAssets::Failed)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, error));
            return;
        }

        if (status == MediaAssets::Reloaded)
            doc->setModified();

        // functions.media.reloaded is broadcast from slotMediaOriginReloaded
        // (fired by applyReload above, or later by a queued background copy)
        QJsonObject result = typeDetailToJson(function);
        result.insert(QStringLiteral("status"), reloadStatusToString(status));
        if (error.isEmpty() == false)
            result.insert(QStringLiteral("error"), error);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    /*********************************************************************
     * Scene-specific (functions-core.yaml)
     *********************************************************************/

    dispatcher->registerMethod(QStringLiteral("functions.scene.setValues"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Scene *scene = qobject_cast<Scene *>(findFunction(doc, params));
        if (scene == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such Scene")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        QJsonObject newValues = params.value(QStringLiteral("values")).toObject();

        // Full replacement of the value list only - deliberately NOT
        // Scene::clear() (which would also drop fixtures()/fixtureGroups()/
        // palettes() membership, collateral damage the "values" field has no
        // business causing - see functions-core-notes.md). Removed entries
        // first, then (re)apply every entry in the new map; Scene::setValue()
        // re-adds the owning fixture automatically if needed.
        for (const SceneValue &sv : scene->values())
        {
            if (newValues.contains(sceneValueKey(sv.fxi, sv.channel)) == false)
                scene->unsetValue(sv.fxi, sv.channel);
        }
        for (auto it = newValues.constBegin(); it != newValues.constEnd(); ++it)
        {
            quint32 fxi = 0;
            quint32 channel = 0;
            if (parseSceneValueKey(it.key(), fxi, channel) == false)
                continue;
            int value = qBound(0, it.value().toInt(), 255);
            scene->setValue(fxi, channel, uchar(value));
        }

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject replaceOp;
        replaceOp.insert(QStringLiteral("op"), QStringLiteral("replace"));
        replaceOp.insert(QStringLiteral("path"), QStringLiteral("/values"));
        replaceOp.insert(QStringLiteral("value"), sceneValueListToJson(scene->values()));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(scene->id()));
        data.insert(QStringLiteral("patch"), QJsonArray{ replaceOp });
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.scene.valuesChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.scene.setValue"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Scene *scene = qobject_cast<Scene *>(findFunction(doc, params));
        if (scene == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such Scene")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        bool ok = false;
        quint32 fixtureId = params.value(QStringLiteral("fixture")).toString().toUInt(&ok);
        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid fixture")));
            return;
        }
        quint32 channel = quint32(params.value(QStringLiteral("channel")).toInt());
        int value = qBound(0, params.value(QStringLiteral("value")).toInt(), 255);

        bool existed = scene->checkValue(SceneValue(fixtureId, channel, 0));
        scene->setValue(fixtureId, channel, uchar(value));

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject op;
        op.insert(QStringLiteral("op"), existed ? QStringLiteral("replace") : QStringLiteral("add"));
        op.insert(QStringLiteral("path"), QStringLiteral("/values/%1").arg(sceneValueKey(fixtureId, channel)));
        op.insert(QStringLiteral("value"), value);

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(scene->id()));
        data.insert(QStringLiteral("patch"), QJsonArray{ op });
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.scene.valuesChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.scene.unsetValue"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Scene *scene = qobject_cast<Scene *>(findFunction(doc, params));
        if (scene == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such Scene")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        bool ok = false;
        quint32 fixtureId = params.value(QStringLiteral("fixture")).toString().toUInt(&ok);
        if (ok == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid fixture")));
            return;
        }
        quint32 channel = quint32(params.value(QStringLiteral("channel")).toInt());

        // Scene::unsetValue() unconditionally emits changed() even when the
        // key didn't exist - matches this method being effectively idempotent.
        scene->unsetValue(fixtureId, channel);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject op;
        op.insert(QStringLiteral("op"), QStringLiteral("remove"));
        op.insert(QStringLiteral("path"), QStringLiteral("/values/%1").arg(sceneValueKey(fixtureId, channel)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(scene->id()));
        data.insert(QStringLiteral("patch"), QJsonArray{ op });
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.scene.valuesChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.scene.setMembers"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Scene *scene = qobject_cast<Scene *>(findFunction(doc, params));
        if (scene == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such Scene")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        // None of Scene's membership add/remove* setters below emit
        // changed()/setModified() on their own (only setValue()/unsetValue()
        // and slotFixtureRemoved() do - see scene.cpp) - bumped explicitly
        // once at the end instead, and only if something actually changed.
        bool anyChanged = false;

        if (params.value(QStringLiteral("fixtures")).isArray())
        {
            QSet<quint32> newIds;
            for (const QJsonValue &v : params.value(QStringLiteral("fixtures")).toArray())
            {
                bool ok = false;
                quint32 fid = v.toString().toUInt(&ok);
                if (ok)
                    newIds.insert(fid);
            }
            for (quint32 existingId : scene->fixtures())
            {
                if (newIds.contains(existingId) == false)
                {
                    scene->removeFixture(existingId);
                    anyChanged = true;
                }
            }
            for (quint32 newId : std::as_const(newIds))
            {
                if (scene->fixtures().contains(newId) == false)
                {
                    scene->addFixture(newId);
                    anyChanged = true;
                }
            }
        }

        if (params.value(QStringLiteral("fixtureGroups")).isArray())
        {
            QSet<quint32> newIds;
            for (const QJsonValue &v : params.value(QStringLiteral("fixtureGroups")).toArray())
            {
                bool ok = false;
                quint32 gid = v.toString().toUInt(&ok);
                if (ok)
                    newIds.insert(gid);
            }
            for (quint32 existingId : scene->fixtureGroups())
            {
                if (newIds.contains(existingId) == false)
                {
                    scene->removeFixtureGroup(existingId);
                    anyChanged = true;
                }
            }
            for (quint32 newId : std::as_const(newIds))
            {
                if (scene->fixtureGroups().contains(newId) == false)
                {
                    scene->addFixtureGroup(newId);
                    anyChanged = true;
                }
            }
        }

        if (params.value(QStringLiteral("palettes")).isArray())
        {
            QSet<quint32> newIds;
            for (const QJsonValue &v : params.value(QStringLiteral("palettes")).toArray())
            {
                bool ok = false;
                quint32 pid = v.toString().toUInt(&ok);
                if (ok)
                    newIds.insert(pid);
            }
            for (quint32 existingId : scene->palettes())
            {
                if (newIds.contains(existingId) == false)
                {
                    scene->removePalette(existingId);
                    anyChanged = true;
                }
            }
            for (quint32 newId : std::as_const(newIds))
            {
                if (scene->palettes().contains(newId) == false)
                {
                    scene->addPalette(newId);
                    anyChanged = true;
                }
            }
        }

        if (params.value(QStringLiteral("channelGroups")).isArray())
        {
            QJsonArray refs = params.value(QStringLiteral("channelGroups")).toArray();
            QSet<quint32> newIds;
            QHash<quint32, uchar> newLevels;
            for (const QJsonValue &v : refs)
            {
                QJsonObject ref = v.toObject();
                bool ok = false;
                quint32 gid = ref.value(QStringLiteral("id")).toString().toUInt(&ok);
                if (ok == false)
                    continue;
                newIds.insert(gid);
                newLevels.insert(gid, uchar(qBound(0, ref.value(QStringLiteral("level")).toInt(), 255)));
            }
            for (quint32 existingId : scene->channelGroups())
            {
                if (newIds.contains(existingId) == false)
                {
                    scene->removeChannelGroup(existingId);
                    anyChanged = true;
                }
            }
            for (auto it = newLevels.constBegin(); it != newLevels.constEnd(); ++it)
            {
                if (scene->channelGroups().contains(it.key()) == false)
                {
                    scene->addChannelGroup(it.key());
                    anyChanged = true;
                }
                scene->setChannelGroupLevel(it.key(), it.value());
            }
        }

        if (anyChanged)
            doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(scene->id()));
        data.insert(QStringLiteral("fixtures"), idListToJson(scene->fixtures()));
        data.insert(QStringLiteral("fixtureGroups"), idListToJson(scene->fixtureGroups()));
        data.insert(QStringLiteral("channelGroups"), sceneChannelGroupRefsToJson(scene));
        data.insert(QStringLiteral("palettes"), idListToJson(scene->palettes()));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.scene.membersChanged"), data, session->clientId(), false);
    });

    /*********************************************************************
     * Chaser/Sequence step CRUD, shared (functions-core.yaml)
     *********************************************************************/

    dispatcher->registerMethod(QStringLiteral("functions.steps.addStep"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Chaser *chaser = qobject_cast<Chaser *>(findFunction(doc, params));
        if (chaser == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such Chaser/Sequence")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        bool isSequence = chaser->type() == Function::SequenceType;
        ChaserStep step;
        QString errorMessage;
        if (chaserStepFromJson(chaser, isSequence, params.value(QStringLiteral("step")).toObject(), step, errorMessage) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, errorMessage));
            return;
        }

        // Chaser::addStep() silently no-ops (yet still reports success and
        // still emits changed()) for an index beyond the current size - clamp
        // here so the reported/broadcast index always matches where the step
        // actually landed.
        int stepsCountBefore = chaser->stepsCount();
        int requestedIndex = params.contains(QStringLiteral("index")) ? params.value(QStringLiteral("index")).toInt() : -1;
        int insertIndex = (requestedIndex < 0 || requestedIndex > stepsCountBefore) ? stepsCountBefore : requestedIndex;

        if (chaser->addStep(step, insertIndex) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("A Chaser/Sequence cannot target itself")));
            return;
        }

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject op;
        op.insert(QStringLiteral("op"), QStringLiteral("add"));
        op.insert(QStringLiteral("path"), QStringLiteral("/steps/%1").arg(insertIndex));
        op.insert(QStringLiteral("value"), chaserStepToJson(step, isSequence));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(chaser->id()));
        data.insert(QStringLiteral("patch"), QJsonArray{ op });
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(isSequence ? QStringLiteral("functions.sequence.stepsChanged") : QStringLiteral("functions.chaser.stepsChanged"),
                             data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.steps.replaceStep"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Chaser *chaser = qobject_cast<Chaser *>(findFunction(doc, params));
        if (chaser == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such Chaser/Sequence")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        bool isSequence = chaser->type() == Function::SequenceType;
        ChaserStep step;
        QString errorMessage;
        if (chaserStepFromJson(chaser, isSequence, params.value(QStringLiteral("step")).toObject(), step, errorMessage) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, errorMessage));
            return;
        }

        int index = params.value(QStringLiteral("index")).toInt();
        if (chaser->replaceStep(step, index) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("index out of range")));
            return;
        }

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject op;
        op.insert(QStringLiteral("op"), QStringLiteral("replace"));
        op.insert(QStringLiteral("path"), QStringLiteral("/steps/%1").arg(index));
        op.insert(QStringLiteral("value"), chaserStepToJson(step, isSequence));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(chaser->id()));
        data.insert(QStringLiteral("patch"), QJsonArray{ op });
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(isSequence ? QStringLiteral("functions.sequence.stepsChanged") : QStringLiteral("functions.chaser.stepsChanged"),
                             data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.steps.removeStep"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Chaser *chaser = qobject_cast<Chaser *>(findFunction(doc, params));
        if (chaser == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such Chaser/Sequence")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        int index = params.value(QStringLiteral("index")).toInt();
        if (chaser->removeStep(index) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("index out of range")));
            return;
        }

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject op;
        op.insert(QStringLiteral("op"), QStringLiteral("remove"));
        op.insert(QStringLiteral("path"), QStringLiteral("/steps/%1").arg(index));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(chaser->id()));
        data.insert(QStringLiteral("patch"), QJsonArray{ op });
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(chaser->type() == Function::SequenceType ? QStringLiteral("functions.sequence.stepsChanged") : QStringLiteral("functions.chaser.stepsChanged"),
                             data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.steps.moveStep"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Chaser *chaser = qobject_cast<Chaser *>(findFunction(doc, params));
        if (chaser == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such Chaser/Sequence")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
            return;
        }

        int sourceIndex = params.value(QStringLiteral("sourceIndex")).toInt();
        int destIndex = params.value(QStringLiteral("destIndex")).toInt();
        if (chaser->moveStep(sourceIndex, destIndex) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid sourceIndex/destIndex")));
            return;
        }

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject op;
        op.insert(QStringLiteral("op"), QStringLiteral("move"));
        op.insert(QStringLiteral("from"), QStringLiteral("/steps/%1").arg(sourceIndex));
        op.insert(QStringLiteral("path"), QStringLiteral("/steps/%1").arg(destIndex));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(chaser->id()));
        data.insert(QStringLiteral("patch"), QJsonArray{ op });
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(chaser->type() == Function::SequenceType ? QStringLiteral("functions.sequence.stepsChanged") : QStringLiteral("functions.chaser.stepsChanged"),
                             data, session->clientId(), false);
    });
}
