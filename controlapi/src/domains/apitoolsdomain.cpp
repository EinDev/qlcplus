/*
  Q Light Controller Plus - Control API
  apitoolsdomain.cpp

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

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QSharedPointer>

#include <algorithm>

#include "apitoolsdomain.h"
#include "apicoredomain.h"
#include "apiiodomain.h"
#include "apiprojecthost.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"

#include "doc.h"
#include "fixture.h"
#include "function.h"
#include "functionparent.h"
#include "genericdmxsource.h"
#include "genericfader.h"
#include "fadechannel.h"
#include "inputoutputmap.h"
#include "qlcchannel.h"
#include "show.h"
#include "showfunction.h"
#include "track.h"
#include "universe.h"

namespace
{

QJsonValue idOrNull(quint32 id)
{
    return id == Function::invalidId() ? QJsonValue() : QJsonValue(QString::number(id));
}

QJsonArray fadeFlagsToJson(int flags)
{
    static const QList<QPair<int, const char *>> names = {
        { FadeChannel::HTP, "HTP" }, { FadeChannel::LTP, "LTP" }, { FadeChannel::Fine, "Fine" },
        { FadeChannel::Intensity, "Intensity" }, { FadeChannel::CanFade, "CanFade" },
        { FadeChannel::Flashing, "Flashing" }, { FadeChannel::Relative, "Relative" },
        { FadeChannel::Override, "Override" }, { FadeChannel::SetTarget, "SetTarget" },
        { FadeChannel::AutoRemove, "AutoRemove" }, { FadeChannel::CrossFade, "CrossFade" },
        { FadeChannel::ForceLTP, "ForceLTP" } };
    QJsonArray arr;
    for (const auto &n : names)
        if (flags & n.first)
            arr.append(QString::fromLatin1(n.second));
    return arr;
}

/** FunctionParent::MasterId as a stable camelCase name (functionparent.h) */
QString masterIdName(quint32 id)
{
    switch (FunctionParent::MasterId(id))
    {
        case FunctionParent::GenericOverride: return QStringLiteral("genericOverride");
        case FunctionParent::EngineSelfStop: return QStringLiteral("engineSelfStop");
        case FunctionParent::MasterTimerStopAll: return QStringLiteral("masterTimerStopAll");
        case FunctionParent::ProjectAutostart: return QStringLiteral("projectAutostart");
        case FunctionParent::FunctionManagerPreview: return QStringLiteral("functionManagerPreview");
        case FunctionParent::FunctionManagerScenePreview: return QStringLiteral("functionManagerScenePreview");
        case FunctionParent::FunctionManagerDelete: return QStringLiteral("functionManagerDelete");
        case FunctionParent::FunctionEditorPreview: return QStringLiteral("functionEditorPreview");
        case FunctionParent::ChaserEditorStepPreview: return QStringLiteral("chaserEditorStepPreview");
        case FunctionParent::ShowManagerPlayback: return QStringLiteral("showManagerPlayback");
        case FunctionParent::TardisUndoRedo: return QStringLiteral("tardisUndoRedo");
        case FunctionParent::WebAccess: return QStringLiteral("webAccess");
        case FunctionParent::VideoWindowClosed: return QStringLiteral("videoWindowClosed");
        case FunctionParent::ScriptStopFunction: return QStringLiteral("scriptStopFunction");
        case FunctionParent::ControlApi: return QStringLiteral("controlApi");
        default: return QStringLiteral("unknown");
    }
}

/** GenericDMXSource::Feature as a stable camelCase name (genericdmxsource.h) */
QString featureName(GenericDMXSource::Feature feature)
{
    switch (feature)
    {
        case GenericDMXSource::DragPositionPush: return QStringLiteral("dragPositionPush");
        case GenericDMXSource::PersistedTransformRestore: return QStringLiteral("persistedTransformRestore");
        case GenericDMXSource::PositionPickPoint: return QStringLiteral("positionPickPoint");
        case GenericDMXSource::FixtureConsoleChannelSet: return QStringLiteral("fixtureConsoleChannelSet");
        case GenericDMXSource::IntensityTool: return QStringLiteral("intensityTool");
        case GenericDMXSource::ColorTool: return QStringLiteral("colorTool");
        case GenericDMXSource::PositionCenterTool: return QStringLiteral("positionCenterTool");
        case GenericDMXSource::PresetTool: return QStringLiteral("presetTool");
        case GenericDMXSource::FixtureHighlight: return QStringLiteral("fixtureHighlight");
        case GenericDMXSource::PositionTool: return QStringLiteral("positionTool");
        case GenericDMXSource::BeamTool: return QStringLiteral("beamTool");
        case GenericDMXSource::PalettePreview: return QStringLiteral("palettePreview");
        case GenericDMXSource::DumpUndoRedo: return QStringLiteral("dumpUndoRedo");
        case GenericDMXSource::SceneEditorPreview: return QStringLiteral("sceneEditorPreview");
        case GenericDMXSource::SceneEditorExternalControlHighlight: return QStringLiteral("sceneEditorExternalControlHighlight");
        case GenericDMXSource::Unspecified:
        default: return QStringLiteral("unspecified");
    }
}

/** What started $f, one entry per Function::sources() item */
QJsonArray startedByToJson(Doc *doc, Function *f)
{
    QJsonArray arr;
    for (const FunctionParent &p : f->sources())
    {
        QJsonObject o;
        switch (p.type())
        {
            case FunctionParent::Function:
            {
                o.insert(QStringLiteral("type"), QStringLiteral("function"));
                o.insert(QStringLiteral("id"), QString::number(p.id()));
                Function *parent = doc->function(p.id());
                if (parent != nullptr)
                {
                    o.insert(QStringLiteral("name"), parent->name());
                    o.insert(QStringLiteral("functionType"), parent->typeString());
                }
                break;
            }
            case FunctionParent::AutoVCWidget:
            case FunctionParent::ManualVCWidget:
                // A Virtual Console widget id (vc.widget.get resolves it)
                o.insert(QStringLiteral("type"), p.type() == FunctionParent::AutoVCWidget
                                                     ? QStringLiteral("autoVCWidget") : QStringLiteral("manualVCWidget"));
                o.insert(QStringLiteral("id"), QString::number(p.id()));
                break;
            default:
                o.insert(QStringLiteral("type"), QStringLiteral("master"));
                o.insert(QStringLiteral("id"), QString::number(p.id()));
                o.insert(QStringLiteral("name"), masterIdName(p.id()));
                break;
        }
        arr.append(o);
    }
    return arr;
}

QJsonObject functionRef(Function *f)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), QString::number(f->id()));
    o.insert(QStringLiteral("name"), f->name());
    o.insert(QStringLiteral("type"), f->typeString());
    o.insert(QStringLiteral("running"), f->isRunning());
    return o;
}

int showItemCount(Show *show)
{
    int count = 0;
    for (Track *track : show->tracks())
        count += track->showFunctions().count();
    return count;
}

/** The Show id a request names (showId, string or number) */
Show *findShow(Doc *doc, const QJsonObject &params, ApiSession *session, const QString &id)
{
    QJsonValue v = params.value(QStringLiteral("showId"));
    QString idStr = v.isDouble() ? QString::number(v.toInt()) : v.toString();
    bool ok = false;
    quint32 fid = idStr.toUInt(&ok);
    Show *show = ok ? qobject_cast<Show *>(doc->function(fid)) : nullptr;
    if (show == nullptr)
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No Show with id %1").arg(idStr)));
    return show;
}

/** params.bpm as an int in 1..1000, or -1 */
int bpmParam(const QJsonObject &params)
{
    QJsonValue v = params.value(QStringLiteral("bpm"));
    if (v.isDouble() == false)
        return -1;
    double d = v.toDouble();
    if (d < 1 || d > 1000 || d != double(int(d)))
        return -1;
    return int(d);
}

} // namespace

ApiToolsDomain::ApiToolsDomain(Doc *doc, ApiServer *server, ApiIoDomain *ioDomain, ApiCoreDomain *coreDomain,
                               QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
    , m_ioDomain(ioDomain)
    , m_coreDomain(coreDomain)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    // A new/opened project starts with nothing converted or dismissed
    // (engine DLL signals: string-based connects, see apiiodomain.cpp)
    connect(m_doc, SIGNAL(cleared()), this, SLOT(slotProjectChanged()));
    connect(m_doc, SIGNAL(loaded()), this, SLOT(slotProjectChanged()));

    registerMethods();
}

double ApiToolsDomain::legacyUnitToMs(int bpm)
{
    if (bpm <= 0)
        return 0.0;
    // Old encoding (TimeUtils.js posToBeat(), pre-398388c7a) stored
    // beatCount * 1000: realMs = pseudo * (60000 / bpm) / 1000
    return (60000.0 / bpm) / 1000.0;
}

void ApiToolsDomain::slotProjectChanged()
{
    m_handledShows.clear();
}

QString ApiToolsDomain::projectCreatorVersion(QString *source) const
{
    ApiProjectHost *host = dynamic_cast<ApiProjectHost *>(m_server->parent());
    QString path = host != nullptr ? host->fileName() : QString();
    if (path.isEmpty() == false)
    {
        *source = QStringLiteral("file");
        QFile file(path);
        QString version;
        if (file.open(QIODevice::ReadOnly))
            ApiCoreDomain::validateWorkspaceXml(file.readAll(), &version);
        return version;
    }
    if (m_coreDomain != nullptr && m_coreDomain->hasUploadedProject())
    {
        *source = QStringLiteral("upload");
        return m_coreDomain->uploadedCreatorVersion();
    }
    *source = QStringLiteral("none");
    return QString();
}

QList<Show *> ApiToolsDomain::flaggedShows(QString *creatorVersion, QString *source) const
{
    *creatorVersion = projectCreatorVersion(source);
    QList<Show *> shows;
    if (*source == QStringLiteral("none"))
        return shows;
    for (Show *show : m_doc->possiblyAffectedLegacyBeatShows(*creatorVersion))
        if (m_handledShows.contains(show->id()) == false)
            shows.append(show);
    return shows;
}

QJsonObject ApiToolsDomain::inspectChannel(quint32 universeId, quint32 channel) const
{
    const quint32 address = (universeId << 9) + channel;
    quint32 fxID = m_doc->fixtureForAddress(address);
    Fixture *fixture = m_doc->fixture(fxID);
    // Fixture::universeAddress() is in the same (universe << 9 | address)
    // domain as $address; Fixture::address() is only the low 9 bits.
    quint32 relChannel = fixture != nullptr ? address - fixture->universeAddress() : channel;

    QJsonObject result;
    result.insert(QStringLiteral("universeId"), int(universeId));
    result.insert(QStringLiteral("channel"), int(channel));
    result.insert(QStringLiteral("address"), int(address));

    if (fixture != nullptr)
    {
        QJsonObject fx;
        fx.insert(QStringLiteral("id"), QString::number(fixture->id()));
        fx.insert(QStringLiteral("name"), fixture->name());
        fx.insert(QStringLiteral("channelIndex"), int(relChannel));
        const QLCChannel *ch = fixture->channel(relChannel);
        fx.insert(QStringLiteral("channelName"), ch != nullptr ? QJsonValue(ch->name()) : QJsonValue());
        fx.insert(QStringLiteral("group"), ch != nullptr ? QJsonValue(QLCChannel::groupToString(ch->group())) : QJsonValue());
        result.insert(QStringLiteral("fixture"), fx);
    }
    else
    {
        result.insert(QStringLiteral("fixture"), QJsonValue());
    }

    uchar overrideValue = 0;
    bool overridden = m_ioDomain != nullptr && m_ioDomain->simpleDeskOverride(address, &overrideValue);
    result.insert(QStringLiteral("simpleDeskOverride"), overridden ? QJsonValue(int(overrideValue)) : QJsonValue());

    QJsonArray faders;
    QJsonValue lastWrite;
    QList<Universe *> ua = m_doc->inputOutputMap()->claimUniverses();
    if (int(universeId) < ua.count())
    {
        Universe *uni = ua.at(int(universeId));
        result.insert(QStringLiteral("preGMValue"), int(uni->preGMValue(int(channel))));
        result.insert(QStringLiteral("postGMValue"), int(uni->postGMValue(int(channel))));

        // FadeChannels of a non-fixture address are keyed by the within-
        // universe channel (ApiIoDomain::writeDMX, Function faders alike)
        quint32 hash = GenericFader::channelHash(fixture != nullptr ? fxID : Fixture::invalidId(), relChannel);
        for (const QSharedPointer<GenericFader> &fader : uni->faders())
        {
            if (fader.isNull())
                continue;
            QHash<quint32, FadeChannel> channels = fader->channels();
            if (channels.contains(hash) == false)
                continue;

            FadeChannel fc = channels.value(hash);
            QJsonObject fo;
            fo.insert(QStringLiteral("name"), fader->name());
            fo.insert(QStringLiteral("priority"), fader->priority());
            fo.insert(QStringLiteral("intensity"), fader->intensity());
            fo.insert(QStringLiteral("paused"), fader->isPaused());
            fo.insert(QStringLiteral("fadingOut"), fader->isFadingOut());
            fo.insert(QStringLiteral("start"), int(fc.start()));
            fo.insert(QStringLiteral("current"), int(fc.current()));
            fo.insert(QStringLiteral("target"), int(fc.target()));
            fo.insert(QStringLiteral("fadeTimeMs"), int(fc.fadeTime()));
            fo.insert(QStringLiteral("elapsedMs"), int(fc.elapsed()));
            fo.insert(QStringLiteral("ready"), fc.isReady());
            fo.insert(QStringLiteral("flags"), fadeFlagsToJson(fc.flags()));

            Function *f = fader->parentFunctionID() != Function::invalidId()
                              ? m_doc->function(fader->parentFunctionID()) : nullptr;
            GenericDMXSource::Feature feature;
            if (f != nullptr)
            {
                fo.insert(QStringLiteral("source"), QStringLiteral("function"));
                fo.insert(QStringLiteral("function"), functionRef(f));
                fo.insert(QStringLiteral("startedBy"), startedByToJson(m_doc, f));
            }
            else if (fader->name() == ApiIoDomain::simpleDeskFaderName())
            {
                fo.insert(QStringLiteral("source"), QStringLiteral("controlApiSimpleDesk"));
            }
            else if (GenericDMXSource::findFeatureForFader(fader.data(), fixture != nullptr ? fxID : Fixture::invalidId(),
                                                           relChannel, feature))
            {
                fo.insert(QStringLiteral("source"), QStringLiteral("desktopTool"));
                fo.insert(QStringLiteral("feature"), featureName(feature));
            }
            else
            {
                // The desktop Simple Desk, Virtual Console widgets in level
                // mode, CueStacks and Scripts request faders without tagging
                // them (see SimpleDesk::debugChannelInfo()).
                fo.insert(QStringLiteral("source"), QStringLiteral("unidentified"));
            }
            faders.append(fo);
        }

        Universe::LastChannelWrite lw;
        if (uni->lastChannelWrite(int(channel), lw))
        {
            QJsonObject lo;
            lo.insert(QStringLiteral("value"), int(lw.value));
            lo.insert(QStringLiteral("faderName"), lw.faderName);
            lo.insert(QStringLiteral("timestampMs"), double(lw.timestampMs));
            lo.insert(QStringLiteral("ageMs"), double(QDateTime::currentMSecsSinceEpoch() - lw.timestampMs));
            Function *f = lw.parentFunctionID != Function::invalidId() ? m_doc->function(lw.parentFunctionID) : nullptr;
            lo.insert(QStringLiteral("functionId"), idOrNull(lw.parentFunctionID));
            lo.insert(QStringLiteral("function"), f != nullptr ? QJsonValue(functionRef(f)) : QJsonValue());
            lastWrite = lo;
        }
    }
    m_doc->inputOutputMap()->releaseUniverses(false);

    result.insert(QStringLiteral("faders"), faders);
    result.insert(QStringLiteral("lastWrite"), lastWrite);
    return result;
}

void ApiToolsDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();

    // io.dmx.channel.inspect {universeId, channel} -> IoDmxChannelInspectResult
    dispatcher->registerMethod(QStringLiteral("io.dmx.channel.inspect"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QJsonValue u = params.value(QStringLiteral("universeId"));
        QJsonValue c = params.value(QStringLiteral("channel"));
        const quint32 count = m_doc->inputOutputMap()->universesCount();
        if (u.isDouble() == false || c.isDouble() == false ||
            u.toDouble() < 0 || u.toDouble() >= double(count) ||
            c.toDouble() < 0 || c.toDouble() > 511)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("universeId (0..%1) and channel (0..511) are required").arg(int(count) - 1)));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, inspectChannel(quint32(u.toInt()), quint32(c.toInt()))));
    });

    // functions.show.legacyTiming.get {showId?, bpm?}
    //   -> {creatorVersion, source, shows: [...], preview?: [...]}
    dispatcher->registerMethod(QStringLiteral("functions.show.legacyTiming.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QString version, source;
        QList<Show *> shows = flaggedShows(&version, &source);

        QJsonObject result;
        result.insert(QStringLiteral("creatorVersion"), version.isEmpty() ? QJsonValue() : QJsonValue(version));
        result.insert(QStringLiteral("source"), source);
        QJsonArray list;
        for (Show *show : shows)
        {
            QJsonObject o;
            o.insert(QStringLiteral("id"), QString::number(show->id()));
            o.insert(QStringLiteral("name"), show->name());
            o.insert(QStringLiteral("bpm"), show->timeDivisionBPM());
            o.insert(QStringLiteral("beatsDivision"), show->beatsDivision());
            o.insert(QStringLiteral("itemCount"), showItemCount(show));
            list.append(o);
        }
        result.insert(QStringLiteral("shows"), list);

        // Optional conversion preview (LegacyShowTimingConvertDialog's
        // "before / after" table): the first and last 3 items by start time
        if (params.contains(QStringLiteral("showId")))
        {
            Show *show = findShow(m_doc, params, session, id);
            if (show == nullptr)
                return;
            int bpm = params.contains(QStringLiteral("bpm")) ? bpmParam(params) : show->timeDivisionBPM();
            double msPerUnit = legacyUnitToMs(bpm);
            if (msPerUnit <= 0.0)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("bpm must be an integer between 1 and 1000")));
                return;
            }
            QList<ShowFunction *> items;
            for (Track *track : show->tracks())
                items << track->showFunctions();
            std::sort(items.begin(), items.end(), [](ShowFunction *a, ShowFunction *b) {
                return a->startTime() < b->startTime();
            });
            const int edge = 3;
            QJsonArray preview;
            for (int i = 0; i < items.count(); i++)
            {
                if (items.count() > edge * 2 && i >= edge && i < items.count() - edge)
                    continue;
                ShowFunction *sf = items.at(i);
                Function *target = m_doc->function(sf->functionID());
                QJsonObject row;
                row.insert(QStringLiteral("name"), target != nullptr ? target->name() : QStringLiteral("Unknown function"));
                row.insert(QStringLiteral("oldStart"), double(sf->startTime()));
                row.insert(QStringLiteral("newStart"), double(qRound(sf->startTime() * msPerUnit)));
                row.insert(QStringLiteral("oldDuration"), double(sf->duration()));
                row.insert(QStringLiteral("newDuration"), double(qRound(sf->duration() * msPerUnit)));
                preview.append(row);
            }
            result.insert(QStringLiteral("previewBpm"), bpm);
            result.insert(QStringLiteral("preview"), preview);
        }
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // functions.show.legacyTiming.convert {showId, bpm, baseRevision} -> {docRevision, itemsChanged}
    dispatcher->registerMethod(QStringLiteral("functions.show.legacyTiming.convert"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShow(m_doc, params, session, id);
        if (show == nullptr)
            return;
        if (quint32(params.value(QStringLiteral("baseRevision")).toInt(-1)) != m_doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }
        double msPerUnit = legacyUnitToMs(bpmParam(params));
        if (msPerUnit <= 0.0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("bpm must be an integer between 1 and 1000")));
            return;
        }

        // Same per-item arithmetic as ShowManager::convertLegacyBeatShow().
        // Not recorded on the desktop undo stack (API edits never are).
        int changed = 0;
        for (Track *track : show->tracks())
        {
            for (ShowFunction *sf : track->showFunctions())
            {
                quint32 newStart = quint32(qRound(sf->startTime() * msPerUnit));
                quint32 newDuration = quint32(qRound(sf->duration() * msPerUnit));
                if (newStart == sf->startTime() && newDuration == sf->duration())
                    continue;
                sf->setStartTime(newStart);
                sf->setDuration(newDuration);
                changed++;
            }
        }
        m_handledShows.insert(show->id());
        if (changed > 0)
            m_doc->setModified();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        result.insert(QStringLiteral("itemsChanged"), changed);
        session->send(ApiEnvelope::buildOkResponse(id, result));

        if (changed > 0)
        {
            QJsonObject data;
            data.insert(QStringLiteral("functionId"), QString::number(show->id()));
            data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            m_server->broadcast(QStringLiteral("functions.updated"), data, session->clientId(), false);
        }
    });

    // functions.show.legacyTiming.dismiss {showId} -> {} : "already correct",
    // stop listing this Show until the next project load (§4b, not saved -
    // re-saving the project with this build clears the flag for good)
    dispatcher->registerMethod(QStringLiteral("functions.show.legacyTiming.dismiss"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Show *show = findShow(m_doc, params, session, id);
        if (show == nullptr)
            return;
        m_handledShows.insert(show->id());
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });
}
