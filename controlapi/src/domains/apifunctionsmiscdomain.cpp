/*
  Q Light Controller Plus - Control API
  apifunctionsmiscdomain.cpp

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
#include <limits>

#include "apifunctionsmiscdomain.h"
#include "apifunctionsdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "apivchost.h"
#include "inputoutputmap.h"
#include "chaseraction.h"
#include "chaserstep.h"
#include "scenevalue.h"
#include "function.h"
#include "sequence.h"
#include "universe.h"
#include "fixture.h"
#include "chaser.h"
#include "scene.h"
#include "doc.h"

namespace
{

bool parseId(const QJsonValue &value, quint32 &out)
{
    bool ok = false;
    if (value.isDouble())
    {
        double d = value.toDouble();
        ok = d >= 0 && d <= double(std::numeric_limits<quint32>::max());
        out = ok ? quint32(d) : 0;
        return ok;
    }
    out = value.toString().toUInt(&ok);
    return ok;
}

/** functionId (or its alias id), as a string or a number - see
 *  apifunctionsdomain.cpp's findFunction() */
Function *findFunction(Doc *doc, const QJsonObject &params)
{
    QJsonValue value = params.value(QStringLiteral("functionId"));
    if (value.isUndefined())
        value = params.value(QStringLiteral("id"));
    quint32 fid = 0;
    return parseId(value, fid) ? doc->function(fid) : nullptr;
}

QJsonObject docRevisionResult(Doc *doc)
{
    QJsonObject result;
    result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return result;
}

bool checkRevision(Doc *doc, ApiSession *session, const QString &id, const QJsonObject &params)
{
    quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt(-1));
    if (baseRevision == doc->docRevision())
        return true;
    QJsonObject details;
    details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                    QStringLiteral("baseRevision is stale"), details));
    return false;
}

void notFound(ApiSession *session, const QString &id, const QString &what)
{
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, what));
}

void invalid(ApiSession *session, const QString &id, const QString &what)
{
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, what));
}

bool isSpeedModeName(const QString &str)
{
    return str == QStringLiteral("Default") || str == QStringLiteral("Common") || str == QStringLiteral("PerStep");
}

QJsonObject sceneValuesToJson(const QList<SceneValue> &values)
{
    QJsonObject obj;
    for (const SceneValue &sv : values)
        obj.insert(QStringLiteral("%1.%2").arg(sv.fxi).arg(sv.channel), int(sv.value));
    return obj;
}

/** FunctionsSequenceStep, the same shape functions.get returns */
QJsonObject sequenceStepToJson(const ChaserStep &step)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("fadeIn"), double(step.fadeIn));
    obj.insert(QStringLiteral("hold"), double(step.hold));
    obj.insert(QStringLiteral("fadeOut"), double(step.fadeOut));
    obj.insert(QStringLiteral("duration"), double(step.duration));
    if (step.note.isEmpty() == false)
        obj.insert(QStringLiteral("note"), step.note);
    obj.insert(QStringLiteral("values"), sceneValuesToJson(step.values));
    return obj;
}

QJsonArray sequenceStepsToJson(Chaser *chaser)
{
    QJsonArray steps;
    for (const ChaserStep &step : chaser->steps())
        steps.append(sequenceStepToJson(step));
    return steps;
}

ChaserActionType actionFromString(const QString &str, bool *ok)
{
    *ok = true;
    if (str == QStringLiteral("nextStep"))
        return ChaserNextStep;
    if (str == QStringLiteral("previousStep"))
        return ChaserPreviousStep;
    if (str == QStringLiteral("setStepIndex"))
        return ChaserSetStepIndex;
    if (str == QStringLiteral("stopStep"))
        return ChaserStopStep;
    if (str == QStringLiteral("pause"))
        return ChaserPauseRequest;
    *ok = false;
    return ChaserNoAction;
}

int fadeModeFromString(const QString &str)
{
    if (str == QStringLiteral("Blended"))
        return Chaser::Blended;
    if (str == QStringLiteral("Crossfade"))
        return Chaser::Crossfade;
    if (str == QStringLiteral("BlendedCrossfade"))
        return Chaser::BlendedCrossfade;
    return Chaser::FromFunction;
}

/** Live pre-Grand-Master value of every channel @scene holds, the same
 *  snapshot FunctionManager::dumpDmxValues() takes (passthrough applied) */
QList<SceneValue> captureLiveValues(Doc *doc, Scene *scene)
{
    QList<Universe *> ua = doc->inputOutputMap()->claimUniverses();
    QList<SceneValue> captured;
    for (const SceneValue &sv : scene->values())
    {
        Fixture *fxi = doc->fixture(sv.fxi);
        if (fxi == nullptr || sv.channel >= fxi->channels())
            continue;
        quint32 universe = fxi->universe();
        if (universe >= quint32(ua.count()))
            continue;
        quint32 address = fxi->address() + sv.channel;
        if (address >= 512)
            continue;
        Universe *u = ua.at(int(universe));
        uchar value = uchar(u->preGMValues().at(int(address)));
        if (u->passthrough())
            value = u->applyPassthrough(int(address), value);
        captured.append(SceneValue(sv.fxi, sv.channel, value));
    }
    doc->inputOutputMap()->releaseUniverses(false);
    return captured;
}

} // namespace

ApiFunctionsMiscDomain::ApiFunctionsMiscDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);
    registerMethods();

    for (Function *function : m_doc->functions())
        watchChaser(function->id());
    connect(m_doc, SIGNAL(functionAdded(quint32)), this, SLOT(slotFunctionAdded(quint32)));
}

void ApiFunctionsMiscDomain::watchChaser(quint32 id)
{
    Chaser *chaser = qobject_cast<Chaser *>(m_doc->function(id));
    if (chaser != nullptr)
        connect(chaser, SIGNAL(currentStepChanged(int)), this, SLOT(slotCurrentStepChanged(int)), Qt::UniqueConnection);
}

void ApiFunctionsMiscDomain::slotFunctionAdded(quint32 id)
{
    watchChaser(id);
}

void ApiFunctionsMiscDomain::slotCurrentStepChanged(int stepNumber)
{
    // emitted by the runner on the MasterTimer thread, delivered here queued:
    // find the sender among the document's functions by pointer value only
    // (it may have been deleted since, so it must not be dereferenced first)
    QObject *source = sender();
    Chaser *chaser = nullptr;
    for (Function *function : m_doc->functions())
    {
        if (function == source)
            chaser = qobject_cast<Chaser *>(function);
    }
    if (chaser == nullptr)
        return;
    QJsonObject data;
    data.insert(QStringLiteral("functionId"), QString::number(chaser->id()));
    data.insert(QStringLiteral("stepIndex"), stepNumber);
    // live (4b), at most one per step change - not subscribe-gated
    m_server->broadcast(QStringLiteral("functions.chaser.currentStepChanged"), data, QString(), false);
}

ApiVcHost *ApiFunctionsMiscDomain::vcHost() const
{
    return dynamic_cast<ApiVcHost *>(m_server->parent());
}

void ApiFunctionsMiscDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    /*********************************************************************
     * Chaser / Sequence
     *********************************************************************/

    dispatcher->registerMethod(QStringLiteral("functions.chaser.setSpeedModes"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Chaser *chaser = qobject_cast<Chaser *>(findFunction(doc, params));
        if (chaser == nullptr)
            return notFound(session, id, QStringLiteral("No such Chaser/Sequence"));
        if (checkRevision(doc, session, id, params) == false)
            return;

        const QStringList keys = { QStringLiteral("fadeInMode"), QStringLiteral("fadeOutMode"), QStringLiteral("durationMode") };
        for (const QString &key : keys)
        {
            if (params.contains(key) && isSpeedModeName(params.value(key).toString()) == false)
                return invalid(session, id, QStringLiteral("%1 must be Default, Common or PerStep").arg(key));
        }

        bool changed = false;
        if (params.contains(keys.at(0)))
        {
            Chaser::SpeedMode mode = Chaser::stringToSpeedMode(params.value(keys.at(0)).toString());
            changed = changed || mode != chaser->fadeInMode();
            chaser->setFadeInMode(mode);
        }
        if (params.contains(keys.at(1)))
        {
            Chaser::SpeedMode mode = Chaser::stringToSpeedMode(params.value(keys.at(1)).toString());
            changed = changed || mode != chaser->fadeOutMode();
            chaser->setFadeOutMode(mode);
        }
        if (params.contains(keys.at(2)))
        {
            Chaser::SpeedMode mode = Chaser::stringToSpeedMode(params.value(keys.at(2)).toString());
            changed = changed || mode != chaser->durationMode();
            chaser->setDurationMode(mode);
        }
        // the setters emit changed() (-> Doc::setModified) even for the same
        // value; a no-op call still answers with the current revision
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        if (changed == false)
            return;

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(chaser->id()));
        data.insert(QStringLiteral("fadeInMode"), Chaser::speedModeToString(chaser->fadeInMode()));
        data.insert(QStringLiteral("fadeOutMode"), Chaser::speedModeToString(chaser->fadeOutMode()));
        data.insert(QStringLiteral("durationMode"), Chaser::speedModeToString(chaser->durationMode()));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.chaser.changed"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.chaser.setAction"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Chaser *chaser = qobject_cast<Chaser *>(findFunction(doc, params));
        if (chaser == nullptr)
            return notFound(session, id, QStringLiteral("No such Chaser/Sequence"));

        bool ok = false;
        ChaserAction action;
        action.m_action = actionFromString(params.value(QStringLiteral("action")).toString(), &ok);
        if (ok == false)
            return invalid(session, id, QStringLiteral("action must be nextStep, previousStep, setStepIndex, stopStep or pause"));

        action.m_stepIndex = -1;
        if (action.m_action == ChaserSetStepIndex)
        {
            if (params.value(QStringLiteral("stepIndex")).isDouble() == false)
                return invalid(session, id, QStringLiteral("stepIndex is required for setStepIndex"));
            action.m_stepIndex = params.value(QStringLiteral("stepIndex")).toInt();
            if (action.m_stepIndex < 0 || action.m_stepIndex >= chaser->stepsCount())
                return invalid(session, id, QStringLiteral("stepIndex out of range"));
        }
        action.m_masterIntensity = qBound(0.0, params.value(QStringLiteral("masterIntensity")).toDouble(1.0), 1.0);
        action.m_stepIntensity = qBound(0.0, params.value(QStringLiteral("stepIntensity")).toDouble(1.0), 1.0);
        action.m_fadeMode = fadeModeFromString(params.value(QStringLiteral("fadeMode")).toString());

        // A stopped Chaser keeps the action as its startup action (the next
        // start begins at that step) - Chaser::setAction() handles both.
        chaser->setAction(action);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    dispatcher->registerMethod(QStringLiteral("functions.sequence.setBoundScene"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Sequence *sequence = qobject_cast<Sequence *>(findFunction(doc, params));
        if (sequence == nullptr)
            return notFound(session, id, QStringLiteral("No such Sequence"));
        quint32 sceneId = 0;
        Scene *scene = parseId(params.value(QStringLiteral("sceneId")), sceneId) ? qobject_cast<Scene *>(doc->function(sceneId)) : nullptr;
        if (scene == nullptr)
            return notFound(session, id, QStringLiteral("sceneId is not a Scene"));
        if (checkRevision(doc, session, id, params) == false)
            return;

        if (sequence->boundSceneID() != sceneId)
        {
            sequence->setBoundSceneID(sceneId);
            // every step's fid is the bound Scene (ChaserStep of a Sequence):
            // keep them in line, the values are left as they are
            for (int i = 0; i < sequence->stepsCount(); i++)
            {
                ChaserStep step = sequence->steps().at(i);
                step.fid = sceneId;
                sequence->replaceStep(step, i);
            }
            doc->setModified();
        }
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(sequence->id()));
        data.insert(QStringLiteral("boundSceneId"), QString::number(sequence->boundSceneID()));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.sequence.changed"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.sequence.applyDumpValues"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Sequence *sequence = qobject_cast<Sequence *>(findFunction(doc, params));
        if (sequence == nullptr)
            return notFound(session, id, QStringLiteral("No such Sequence"));
        Scene *scene = qobject_cast<Scene *>(doc->function(sequence->boundSceneID()));
        if (scene == nullptr)
            return session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                                                                   QStringLiteral("The Sequence has no bound Scene")));
        bool captureLive = params.value(QStringLiteral("captureLive")).toBool();
        if (captureLive == false && params.value(QStringLiteral("values")).isObject() == false)
            return invalid(session, id, QStringLiteral("values (object) or captureLive: true is required"));
        if (checkRevision(doc, session, id, params) == false)
            return;

        QList<SceneValue> values;
        if (captureLive)
        {
            values = captureLiveValues(doc, scene);
        }
        else
        {
            QJsonObject obj = params.value(QStringLiteral("values")).toObject();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            {
                int dot = it.key().indexOf(QLatin1Char('.'));
                bool okF = false, okC = false;
                quint32 fxi = it.key().left(dot).toUInt(&okF);
                quint32 ch = it.key().mid(dot + 1).toUInt(&okC);
                if (dot <= 0 || okF == false || okC == false)
                    continue;
                values.append(SceneValue(fxi, ch, uchar(qBound(0, it.value().toInt(), 255))));
            }
        }

        int target = params.value(QStringLiteral("targetStepIndex")).isDouble()
                ? params.value(QStringLiteral("targetStepIndex")).toInt() : -1;
        if (target >= sequence->stepsCount())
            target = -1;
        sequence->applyDumpValues(values, target);
        doc->setModified();

        QJsonObject result = docRevisionResult(doc);
        result.insert(QStringLiteral("stepIndex"), target < 0 ? sequence->stepsCount() - 1 : target);
        result.insert(QStringLiteral("capturedChannels"), values.count());
        session->send(ApiEnvelope::buildOkResponse(id, result));

        // applyDumpValues() re-normalises every step against the bound
        // Scene's channel set, so one whole-array replace, not a single-step op
        QJsonObject op;
        op.insert(QStringLiteral("op"), QStringLiteral("replace"));
        op.insert(QStringLiteral("path"), QStringLiteral("/steps"));
        op.insert(QStringLiteral("value"), sequenceStepsToJson(sequence));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(sequence->id()));
        data.insert(QStringLiteral("patch"), QJsonArray{ op });
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.sequence.stepsChanged"), data, session->clientId(), false);
    });

    /*********************************************************************
     * Generic
     *********************************************************************/

    dispatcher->registerMethod(QStringLiteral("functions.adjustAttribute"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
            return notFound(session, id, QStringLiteral("No such function"));
        if (params.value(QStringLiteral("value")).isDouble() == false)
            return invalid(session, id, QStringLiteral("value must be a number"));

        QList<Attribute> attrs = function->attributes();
        int index = -1;
        if (params.contains(QStringLiteral("attributeIndex")))
            index = params.value(QStringLiteral("attributeIndex")).toInt(-1);
        else if (params.contains(QStringLiteral("attributeName")))
            index = function->getAttributeIndex(params.value(QStringLiteral("attributeName")).toString());
        else
            return invalid(session, id, QStringLiteral("attributeIndex or attributeName is required"));
        if (index < 0 || index >= attrs.count())
            return notFound(session, id, QStringLiteral("No such attribute"));

        const Attribute &attr = attrs.at(index);
        qreal value = qBound(attr.m_min, params.value(QStringLiteral("value")).toDouble(), attr.m_max);
        function->adjustAttribute(value, index);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), QString::number(function->id()));
        data.insert(QStringLiteral("attributeIndex"), index);
        data.insert(QStringLiteral("attributeName"), attr.m_name);
        data.insert(QStringLiteral("value"), function->getAttributeValue(index));
        m_server->broadcast(QStringLiteral("functions.attributeChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.tap"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
            return notFound(session, id, QStringLiteral("No such function"));
        function->tap();
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    dispatcher->registerMethod(QStringLiteral("functions.clone"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QList<Function *> sources;
        QJsonArray ids = params.value(QStringLiteral("functionIds")).toArray();
        if (ids.isEmpty() && (params.contains(QStringLiteral("functionId")) || params.contains(QStringLiteral("id"))))
            ids.append(params.contains(QStringLiteral("functionId")) ? params.value(QStringLiteral("functionId")) : params.value(QStringLiteral("id")));
        if (ids.isEmpty())
            return invalid(session, id, QStringLiteral("functionIds is required"));
        for (const QJsonValue &v : ids)
        {
            quint32 fid = 0;
            Function *f = parseId(v, fid) ? doc->function(fid) : nullptr;
            if (f == nullptr)
                return notFound(session, id, QStringLiteral("No such function: %1").arg(v.toVariant().toString()));
            sources.append(f);
        }
        if (checkRevision(doc, session, id, params) == false)
            return;

        // FunctionManager::cloneFunctions(): copy, " (Copy)" suffix, and a
        // Sequence gets its own copy of the bound Scene
        QJsonArray created;
        QList<Function *> copies;
        for (Function *func : sources)
        {
            Function *copy = func->createCopy(doc, false);
            if (copy == nullptr)
                continue;
            copy->setName(copy->name() + QStringLiteral(" (Copy)"));
            if (doc->addFunction(copy) == false)
            {
                delete copy;
                continue;
            }
            if (func->type() == Function::SequenceType)
            {
                Sequence *sequence = qobject_cast<Sequence *>(copy);
                Function *scene = doc->function(sequence->boundSceneID());
                Function *sceneCopy = scene != nullptr ? scene->createCopy(doc) : nullptr;
                if (sceneCopy != nullptr)
                {
                    sequence->setBoundSceneID(sceneCopy->id());
                    for (int i = 0; i < sequence->stepsCount(); i++)
                    {
                        ChaserStep step = sequence->steps().at(i);
                        step.fid = sceneCopy->id();
                        sequence->replaceStep(step, i);
                    }
                }
            }
            created.append(QString::number(copy->id()));
            copies.append(copy);
        }
        if (copies.isEmpty())
            return session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                                   QStringLiteral("Nothing could be cloned")));
        doc->setModified();

        QJsonObject result = docRevisionResult(doc);
        result.insert(QStringLiteral("functionIds"), created);
        session->send(ApiEnvelope::buildOkResponse(id, result));

        for (Function *copy : std::as_const(copies))
        {
            QJsonObject data;
            data.insert(QStringLiteral("function"), ApiFunctionsDomain::summary(copy));
            data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            m_server->broadcast(QStringLiteral("functions.created"), data, session->clientId(), false);
        }
    });

    dispatcher->registerMethod(QStringLiteral("functions.usage"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 fid = 0;
        QJsonValue value = params.value(QStringLiteral("functionId"));
        if (value.isUndefined())
            value = params.value(QStringLiteral("id"));
        if (parseId(value, fid) == false)
            return invalid(session, id, QStringLiteral("functionId must be a function id string"));

        // Doc::getUsage(): pairs of (function id, step / position)
        QJsonArray functions;
        QList<quint32> usage = doc->getUsage(fid);
        for (int i = 0; i + 1 < usage.count(); i += 2)
        {
            Function *f = doc->function(usage.at(i));
            if (f == nullptr)
                continue;
            QJsonObject obj;
            obj.insert(QStringLiteral("functionId"), QString::number(f->id()));
            obj.insert(QStringLiteral("name"), f->name());
            obj.insert(QStringLiteral("type"), Function::typeToString(f->type()));
            obj.insert(QStringLiteral("position"), int(usage.at(i + 1)));
            functions.append(obj);
        }

        QJsonArray widgets;
        ApiVcHost *host = vcHost();
        if (host != nullptr)
        {
            for (quint32 wid : host->vcWidgetsUsingFunction(fid))
                widgets.append(host->vcWidgetSnapshot(wid));
        }

        QJsonObject result;
        result.insert(QStringLiteral("functions"), functions);
        result.insert(QStringLiteral("widgets"), widgets);
        result.insert(QStringLiteral("vcAvailable"), host != nullptr);
        result.insert(QStringLiteral("isStartupFunction"), doc->startupFunction() == fid);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    /*********************************************************************
     * Project autostart function (core.yaml)
     *********************************************************************/

    dispatcher->registerMethod(QStringLiteral("core.project.setStartupFunction"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QJsonValue value = params.value(QStringLiteral("functionId"));
        quint32 fid = Function::invalidId();
        if (value.isNull() == false && value.isUndefined() == false)
        {
            if (parseId(value, fid) == false || doc->function(fid) == nullptr)
                return notFound(session, id, QStringLiteral("No such function"));
        }
        if (checkRevision(doc, session, id, params) == false)
            return;

        if (doc->startupFunction() != fid)
        {
            doc->setStartupFunction(fid);
            // Doc::setStartupFunction() does not mark the document modified,
            // but the id is saved into the .qxw (<Workspace Autostart=...>)
            doc->setModified();
        }
        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("startupFunctionId"), fid == Function::invalidId() ? QJsonValue() : QJsonValue(QString::number(fid)));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("core.project.startupFunctionChanged"), data, session->clientId(), false);
    });
}
