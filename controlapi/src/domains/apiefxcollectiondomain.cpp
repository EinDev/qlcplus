/*
  Q Light Controller Plus - Control API
  apiefxcollectiondomain.cpp

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
#include <QPolygonF>
#include <QRandomGenerator>
#include <QSet>
#include <limits>

#include "apiefxcollectiondomain.h"
#include "apifunctionsdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "collection.h"
#include "efx.h"
#include "efxfixture.h"
#include "fixture.h"
#include "function.h"
#include "qlcfixturemode.h"
#include "doc.h"

namespace {

// Same id resolution as apifunctionsdomain.cpp's findFunction (a private
// helper there): "functionId" or its alias "id", string or number, both
// missing/non-numeric and unknown reported as NOT_FOUND by the callers.
Function *findFunction(Doc *doc, const QJsonObject &params)
{
    QJsonValue value = params.value(QStringLiteral("functionId"));
    if (value.isUndefined())
        value = params.value(QStringLiteral("id"));

    bool ok = false;
    quint32 functionId = 0;
    if (value.isDouble())
    {
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

// A quint32 id carried as a JSON string (the wire convention) or number.
bool parseId(const QJsonValue &value, quint32 &id)
{
    bool ok = false;
    if (value.isDouble())
    {
        double d = value.toDouble();
        ok = d >= 0 && d <= double(std::numeric_limits<quint32>::max());
        id = ok ? quint32(d) : 0;
    }
    else
    {
        id = value.toString().toUInt(&ok);
    }
    return ok;
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

// §4a check, sending the CONFLICT response itself. Returns false when the
// caller must bail out.
bool checkRevision(Doc *doc, ApiSession *session, const QString &id, const QJsonObject &params)
{
    quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
    if (baseRevision != doc->docRevision())
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                        QStringLiteral("baseRevision is stale"), conflictDetails(doc)));
        return false;
    }
    return true;
}

// See the class comment: guarantees the mutation bumped docRevision at least
// once, whatever the engine call did on its own.
void ensureBumped(Doc *doc, quint32 revisionBefore)
{
    if (doc->docRevision() == revisionBefore)
        doc->setModified();
}

void sendNotFound(ApiSession *session, const QString &id, const QString &message)
{
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, message));
}

void sendInvalidParams(ApiSession *session, const QString &id, const QString &message)
{
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, message));
}

/*****************************************************************************
 * EFX JSON
 *****************************************************************************/

// The spec's FunctionsEfxFixtureModeEnum spells the pan/tilt mode "PanTilt";
// the engine's own string (KXMLQLCEFXFixtureModePanTilt, also what the XML
// carries) is "Position" - mapped explicitly here, both ways.
QString modeToApi(EFXFixture::Mode mode)
{
    switch (mode)
    {
    case EFXFixture::Dimmer: return QStringLiteral("Dimmer");
    case EFXFixture::RGB: return QStringLiteral("RGB");
    default:
    case EFXFixture::PanTilt: return QStringLiteral("PanTilt");
    }
}

bool modeFromApi(const QString &str, EFXFixture::Mode &mode)
{
    if (str == QStringLiteral("PanTilt") || str == KXMLQLCEFXFixtureModePanTilt)
        mode = EFXFixture::PanTilt;
    else if (str == QStringLiteral("Dimmer"))
        mode = EFXFixture::Dimmer;
    else if (str == QStringLiteral("RGB"))
        mode = EFXFixture::RGB;
    else
        return false;
    return true;
}

QJsonObject efxFixtureToJson(Doc *doc, EFXFixture *ef)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("fixture"), QString::number(ef->head().fxi));
    obj.insert(QStringLiteral("head"), ef->head().head);
    obj.insert(QStringLiteral("direction"), Function::directionToString(ef->direction()));
    obj.insert(QStringLiteral("startOffset"), ef->startOffset());
    obj.insert(QStringLiteral("mode"), modeToApi(ef->mode()));

    // EFXFixture::modeList() Q_ASSERTs the fixture still exists - a
    // participant whose fixture was unpatched (EFX::slotFixtureRemoved drops
    // it, but be defensive) reports no available modes instead of asserting.
    QJsonArray modes;
    if (doc->fixture(ef->head().fxi) != nullptr)
    {
        for (const QString &m : ef->modeList())
            modes.append(modeToApi(EFXFixture::stringToMode(m)));
    }
    obj.insert(QStringLiteral("availableModes"), modes);
    return obj;
}

QJsonArray efxFixturesToJson(Doc *doc, EFX *efx)
{
    QJsonArray arr;
    for (EFXFixture *ef : efx->fixtures())
        arr.append(efxFixtureToJson(doc, ef));
    return arr;
}

// The parameter slice shared by FunctionsEfxDetail and functions.efx.changed.
QJsonObject efxParamsToJson(EFX *efx)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("functionId"), QString::number(efx->id()));
    obj.insert(QStringLiteral("algorithm"), EFX::algorithmToString(efx->algorithm()));
    obj.insert(QStringLiteral("propagationMode"), EFX::propagationModeToString(efx->propagationMode()));
    obj.insert(QStringLiteral("width"), efx->width());
    obj.insert(QStringLiteral("height"), efx->height());
    obj.insert(QStringLiteral("rotation"), efx->rotation());
    obj.insert(QStringLiteral("startOffset"), efx->startOffset());
    obj.insert(QStringLiteral("isRelative"), efx->isRelative());
    obj.insert(QStringLiteral("xOffset"), efx->xOffset());
    obj.insert(QStringLiteral("yOffset"), efx->yOffset());
    obj.insert(QStringLiteral("xFrequency"), efx->xFrequency());
    obj.insert(QStringLiteral("yFrequency"), efx->yFrequency());
    obj.insert(QStringLiteral("xPhase"), efx->xPhase());
    obj.insert(QStringLiteral("yPhase"), efx->yPhase());
    obj.insert(QStringLiteral("dimmerControlEnabled"), efx->dimmerControlEnabled());
    return obj;
}

QJsonObject efxDetailToJson(Doc *doc, EFX *efx)
{
    QJsonObject obj = efxParamsToJson(efx);
    obj.insert(QStringLiteral("fixtures"), efxFixturesToJson(doc, efx));
    QJsonArray algorithms;
    for (const QString &name : EFX::algorithmList())
        algorithms.append(name);
    obj.insert(QStringLiteral("algorithms"), algorithms);
    obj.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return obj;
}

QJsonArray polygonToJson(const QPolygonF &polygon)
{
    QJsonArray arr;
    for (const QPointF &pt : polygon)
        arr.append(QJsonArray{ pt.x(), pt.y() });
    return arr;
}

// qmlui/efxeditor.cpp EFXEditor::updateAlgorithmData(), minus its "prepend
// so the first fixture draws on top" reversal - entries are emitted in
// EFX::fixtures() order and the client decides the paint order.
QJsonObject efxPreviewToJson(EFX *efx, bool includeFixturePaths)
{
    QPolygonF polygon;
    efx->preview(polygon);

    QVector<QPolygonF> fixturePaths;
    if (includeFixturePaths)
        efx->previewFixtures(fixturePaths);

    QJsonArray fixtures;
    int index = 0;
    for (EFXFixture *ef : efx->fixtures())
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("fixture"), QString::number(ef->head().fxi));
        obj.insert(QStringLiteral("head"), ef->head().head);
        obj.insert(QStringLiteral("step"), ef->direction() == Function::Forward ? 1 : -1);

        int pathIdx = 0;
        if (ef->startOffset() == 0)
        {
            pathIdx = ef->direction() == Function::Forward ? 0 : polygon.count() - 1;
        }
        else
        {
            float x = 0, y = 0;
            float distance = 1000.0;
            efx->calculatePoint(ef->direction(), ef->startOffset(), 0, &x, &y);
            for (int i = 0; i < polygon.count(); i++)
            {
                QPointF delta = QPointF(x, y) - polygon.at(i);
                qreal pointsDist = delta.manhattanLength();
                if (pointsDist < distance)
                {
                    pathIdx = i;
                    distance = pointsDist;
                }
            }
        }
        obj.insert(QStringLiteral("startIndex"), qMax(0, pathIdx));

        if (includeFixturePaths && index < fixturePaths.size())
            obj.insert(QStringLiteral("path"), polygonToJson(fixturePaths.at(index)));

        fixtures.append(obj);
        index++;
    }

    QJsonObject result;
    result.insert(QStringLiteral("functionId"), QString::number(efx->id()));
    result.insert(QStringLiteral("pattern"), polygonToJson(polygon));
    result.insert(QStringLiteral("fixtures"), fixtures);
    return result;
}

/*****************************************************************************
 * Collection JSON
 *****************************************************************************/

QJsonArray idListToJson(const QList<quint32> &ids)
{
    QJsonArray arr;
    for (quint32 id : ids)
        arr.append(QString::number(id));
    return arr;
}

QJsonObject collectionDetailToJson(Doc *doc, Collection *collection)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("functionId"), QString::number(collection->id()));
    obj.insert(QStringLiteral("functions"), idListToJson(collection->functions()));
    obj.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return obj;
}

// Why a member can't join: self, already a member, or it (transitively)
// contains the collection - Function::contains() is overridden by
// Collection, Chaser and Show, so the walk covers every nesting Function
// type the engine has. Empty string = fine.
QString memberRejection(Doc *doc, Collection *collection, quint32 memberId, bool allowExisting)
{
    if (memberId == collection->id())
        return QStringLiteral("A Collection cannot contain itself");
    Function *member = doc->function(memberId);
    if (member == nullptr)
        return QStringLiteral("No such member function: %1").arg(memberId);
    if (allowExisting == false && collection->functions().contains(memberId))
        return QStringLiteral("Function %1 is already a member").arg(memberId);
    if (member->contains(collection->id()))
        return QStringLiteral("Function %1 contains this Collection - adding it would create a loop").arg(memberId);
    return QString();
}

} // namespace

/*****************************************************************************
 * Domain
 *****************************************************************************/

ApiEfxCollectionDomain::ApiEfxCollectionDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    ApiFunctionsDomain::setTypeDetailProvider(Function::EFXType, [](Function *function)
    {
        return efxDetailToJson(function->doc(), qobject_cast<EFX *>(function));
    });
    ApiFunctionsDomain::setTypeDetailProvider(Function::CollectionType, [](Function *function)
    {
        return collectionDetailToJson(function->doc(), qobject_cast<Collection *>(function));
    });

    registerMethods();
}

void ApiEfxCollectionDomain::registerMethods()
{
    registerCollectionMethods();
    registerEfxMethods();
}

void ApiEfxCollectionDomain::broadcastEfxChanged(EFX *efx, const QString &originClientId)
{
    QJsonObject data = efxParamsToJson(efx);
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("functions.efx.changed"), data, originClientId, false);
}

void ApiEfxCollectionDomain::broadcastEfxFixturesChanged(EFX *efx, const QString &originClientId)
{
    QJsonObject data;
    data.insert(QStringLiteral("functionId"), QString::number(efx->id()));
    data.insert(QStringLiteral("fixtures"), efxFixturesToJson(m_doc, efx));
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("functions.efx.fixturesChanged"), data, originClientId, false);
}

void ApiEfxCollectionDomain::broadcastCollectionMembersChanged(Collection *collection, const QString &originClientId)
{
    QJsonObject data;
    data.insert(QStringLiteral("functionId"), QString::number(collection->id()));
    data.insert(QStringLiteral("functions"), idListToJson(collection->functions()));
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("functions.collection.membersChanged"), data, originClientId, false);
}

/*****************************************************************************
 * functions.collection.*
 *****************************************************************************/

void ApiEfxCollectionDomain::registerCollectionMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("functions.collection.addFunction"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Collection *collection = qobject_cast<Collection *>(findFunction(doc, params));
        if (collection == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such Collection"));
            return;
        }

        quint32 memberId = 0;
        if (parseId(params.value(QStringLiteral("memberFunctionId")), memberId) == false)
        {
            sendInvalidParams(session, id, QStringLiteral("memberFunctionId is required"));
            return;
        }
        if (doc->function(memberId) == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such member function: %1").arg(memberId));
            return;
        }

        if (checkRevision(doc, session, id, params) == false)
            return;

        QString rejection = memberRejection(doc, collection, memberId, false);
        if (rejection.isEmpty() == false)
        {
            sendInvalidParams(session, id, rejection);
            return;
        }

        int index = -1;
        QJsonValue indexValue = params.value(QStringLiteral("index"));
        if (indexValue.isDouble())
        {
            index = indexValue.toInt();
            if (index < 0 || index > collection->functions().count())
                index = -1;
        }

        quint32 before = doc->docRevision();
        if (collection->addFunction(memberId, index) == false)
        {
            sendInvalidParams(session, id, QStringLiteral("Collection::addFunction refused function %1").arg(memberId));
            return;
        }
        ensureBumped(doc, before);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        broadcastCollectionMembersChanged(collection, session->clientId());
    });

    dispatcher->registerMethod(QStringLiteral("functions.collection.removeFunction"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Collection *collection = qobject_cast<Collection *>(findFunction(doc, params));
        if (collection == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such Collection"));
            return;
        }

        quint32 memberId = 0;
        if (parseId(params.value(QStringLiteral("memberFunctionId")), memberId) == false)
        {
            sendInvalidParams(session, id, QStringLiteral("memberFunctionId is required"));
            return;
        }
        if (collection->functions().contains(memberId) == false)
        {
            sendNotFound(session, id, QStringLiteral("Function %1 is not a member of this Collection").arg(memberId));
            return;
        }

        if (checkRevision(doc, session, id, params) == false)
            return;

        quint32 before = doc->docRevision();
        collection->removeFunction(memberId);
        ensureBumped(doc, before);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        broadcastCollectionMembersChanged(collection, session->clientId());
    });

    dispatcher->registerMethod(QStringLiteral("functions.collection.setMembers"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Collection *collection = qobject_cast<Collection *>(findFunction(doc, params));
        if (collection == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such Collection"));
            return;
        }

        QJsonValue functionsValue = params.value(QStringLiteral("functions"));
        if (functionsValue.isArray() == false)
        {
            sendInvalidParams(session, id, QStringLiteral("functions must be an array of function ids"));
            return;
        }

        // Validate the whole list before touching anything, so a rejected
        // request leaves the Collection exactly as it was.
        QList<quint32> newMembers;
        QSet<quint32> seen;
        for (const QJsonValue &v : functionsValue.toArray())
        {
            quint32 memberId = 0;
            if (parseId(v, memberId) == false)
            {
                sendInvalidParams(session, id, QStringLiteral("functions contains a non-numeric id"));
                return;
            }
            if (doc->function(memberId) == nullptr)
            {
                sendNotFound(session, id, QStringLiteral("No such member function: %1").arg(memberId));
                return;
            }
            if (seen.contains(memberId))
            {
                sendInvalidParams(session, id, QStringLiteral("Function %1 is listed twice").arg(memberId));
                return;
            }
            QString rejection = memberRejection(doc, collection, memberId, true);
            if (rejection.isEmpty() == false)
            {
                sendInvalidParams(session, id, rejection);
                return;
            }
            seen.insert(memberId);
            newMembers.append(memberId);
        }

        if (checkRevision(doc, session, id, params) == false)
            return;

        quint32 before = doc->docRevision();
        if (collection->functions() != newMembers)
        {
            for (quint32 existing : collection->functions())
                collection->removeFunction(existing);
            for (quint32 memberId : std::as_const(newMembers))
                collection->addFunction(memberId);
        }
        ensureBumped(doc, before);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        broadcastCollectionMembersChanged(collection, session->clientId());
    });
}

/*****************************************************************************
 * functions.efx.*
 *****************************************************************************/

void ApiEfxCollectionDomain::registerEfxMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("functions.efx.setParameters"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        EFX *efx = qobject_cast<EFX *>(findFunction(doc, params));
        if (efx == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such EFX"));
            return;
        }

        // Validate the enum-valued parameters before the revision check and
        // before applying anything: a bad algorithm name must not leave a
        // half-applied width behind it.
        EFX::Algorithm algorithm = efx->algorithm();
        bool hasAlgorithm = params.contains(QStringLiteral("algorithm"));
        if (hasAlgorithm)
        {
            QString name = params.value(QStringLiteral("algorithm")).toString();
            if (EFX::algorithmList().contains(name) == false)
            {
                sendInvalidParams(session, id, QStringLiteral("Unknown algorithm \"%1\"").arg(name));
                return;
            }
            algorithm = EFX::stringToAlgorithm(name);
        }

        EFX::PropagationMode propagation = efx->propagationMode();
        bool hasPropagation = params.contains(QStringLiteral("propagationMode"));
        if (hasPropagation)
        {
            QString name = params.value(QStringLiteral("propagationMode")).toString();
            if (name != KXMLQLCEFXPropagationModeParallel && name != KXMLQLCEFXPropagationModeSerial &&
                name != KXMLQLCEFXPropagationModeAsymmetric)
            {
                sendInvalidParams(session, id, QStringLiteral("Unknown propagationMode \"%1\"").arg(name));
                return;
            }
            propagation = EFX::stringToPropagationMode(name);
        }

        if (checkRevision(doc, session, id, params) == false)
            return;

        quint32 before = doc->docRevision();
        if (hasAlgorithm)
            efx->setAlgorithm(algorithm);
        if (hasPropagation)
            efx->setPropagationMode(propagation);

        // Integer parameters: the engine setters clamp to their own ranges
        // (0-127 for width/height, 0-359 for angles, 0-255 for offsets,
        // 0-32 for frequencies - see efx.cpp), so out-of-range input is
        // clamped rather than rejected, matching the Qt editor's spin boxes.
        struct IntParam { const char *name; void (EFX::*setter)(int); };
        static const IntParam intParams[] = {
            { "width", &EFX::setWidth },
            { "height", &EFX::setHeight },
            { "rotation", &EFX::setRotation },
            { "startOffset", &EFX::setStartOffset },
            { "xOffset", &EFX::setXOffset },
            { "yOffset", &EFX::setYOffset },
            { "xFrequency", &EFX::setXFrequency },
            { "yFrequency", &EFX::setYFrequency },
            { "xPhase", &EFX::setXPhase },
            { "yPhase", &EFX::setYPhase },
        };
        for (const IntParam &p : intParams)
        {
            QJsonValue v = params.value(QLatin1String(p.name));
            if (v.isDouble())
                (efx->*(p.setter))(v.toInt());
        }

        QJsonValue relative = params.value(QStringLiteral("isRelative"));
        if (relative.isBool())
            efx->setIsRelative(relative.toBool());

        QJsonValue dimmer = params.value(QStringLiteral("dimmerControlEnabled"));
        if (dimmer.isBool())
            efx->setDimmerControlEnabled(dimmer.toBool());

        ensureBumped(doc, before);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        broadcastEfxChanged(efx, session->clientId());
    });

    dispatcher->registerMethod(QStringLiteral("functions.efx.addFixture"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        EFX *efx = qobject_cast<EFX *>(findFunction(doc, params));
        if (efx == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such EFX"));
            return;
        }

        quint32 fixtureId = 0;
        if (parseId(params.value(QStringLiteral("fixture")), fixtureId) == false)
        {
            sendInvalidParams(session, id, QStringLiteral("fixture is required"));
            return;
        }
        Fixture *fixture = doc->fixture(fixtureId);
        if (fixture == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such fixture: %1").arg(fixtureId));
            return;
        }
        // Fixture::heads() dereferences the mode unguarded
        int headCount = fixture->fixtureMode() != nullptr ? fixture->heads() : 1;
        if (headCount < 1)
            headCount = 1;

        bool allHeads = params.value(QStringLiteral("allHeads")).toBool(false);
        int head = params.value(QStringLiteral("head")).toInt(0);
        if (allHeads == false && (head < 0 || head >= headCount))
        {
            sendInvalidParams(session, id, QStringLiteral("head %1 is out of range (fixture %2 has %3 head(s))")
                              .arg(head).arg(fixtureId).arg(headCount));
            return;
        }

        if (checkRevision(doc, session, id, params) == false)
            return;

        // EFX::addFixture() never rejects a duplicate head (its own @todo),
        // so the check lives here - same as EFXEditor::addFixture skipping
        // heads already in the list would, if it did.
        QList<int> heads;
        if (allHeads)
        {
            for (int h = 0; h < headCount; h++)
                if (efx->fixture(fixtureId, h) == nullptr)
                    heads.append(h);
        }
        else if (efx->fixture(fixtureId, head) == nullptr)
        {
            heads.append(head);
        }
        if (heads.isEmpty())
        {
            sendInvalidParams(session, id, allHeads
                              ? QStringLiteral("Every head of fixture %1 is already in this EFX").arg(fixtureId)
                              : QStringLiteral("Fixture %1 head %2 is already in this EFX").arg(fixtureId).arg(head));
            return;
        }

        quint32 before = doc->docRevision();
        for (int h : std::as_const(heads))
        {
            EFXFixture *ef = new EFXFixture(efx);
            ef->setHead(GroupHead(fixtureId, h));
            efx->addFixture(ef);
        }
        ensureBumped(doc, before);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        broadcastEfxFixturesChanged(efx, session->clientId());
    });

    dispatcher->registerMethod(QStringLiteral("functions.efx.removeFixture"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        EFX *efx = qobject_cast<EFX *>(findFunction(doc, params));
        if (efx == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such EFX"));
            return;
        }

        quint32 fixtureId = 0;
        if (parseId(params.value(QStringLiteral("fixture")), fixtureId) == false)
        {
            sendInvalidParams(session, id, QStringLiteral("fixture is required"));
            return;
        }
        int head = params.value(QStringLiteral("head")).toInt(0);
        EFXFixture *ef = efx->fixture(fixtureId, head);
        if (ef == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("Fixture %1 head %2 is not in this EFX").arg(fixtureId).arg(head));
            return;
        }

        if (checkRevision(doc, session, id, params) == false)
            return;

        quint32 before = doc->docRevision();
        // The EFXFixture* overload emits changed(); the (id, head) one
        // doesn't. Neither deletes the object and ~EFX only frees what is
        // still listed, so it is freed here - unless the EFX is running, in
        // which case MasterTimer's thread may still be walking it (the Qt
        // editor leaks it unconditionally, keeping the pointer for undo).
        efx->removeFixture(ef);
        if (efx->isRunning() == false)
            delete ef;
        ensureBumped(doc, before);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        broadcastEfxFixturesChanged(efx, session->clientId());
    });

    dispatcher->registerMethod(QStringLiteral("functions.efx.setFixtureParameters"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        EFX *efx = qobject_cast<EFX *>(findFunction(doc, params));
        if (efx == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such EFX"));
            return;
        }

        quint32 fixtureId = 0;
        if (parseId(params.value(QStringLiteral("fixture")), fixtureId) == false)
        {
            sendInvalidParams(session, id, QStringLiteral("fixture is required"));
            return;
        }
        int head = params.value(QStringLiteral("head")).toInt(0);
        EFXFixture *ef = efx->fixture(fixtureId, head);
        if (ef == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("Fixture %1 head %2 is not in this EFX").arg(fixtureId).arg(head));
            return;
        }

        bool hasDirection = params.contains(QStringLiteral("direction"));
        Function::Direction direction = ef->direction();
        if (hasDirection)
        {
            QString name = params.value(QStringLiteral("direction")).toString();
            if (name != QStringLiteral("Forward") && name != QStringLiteral("Backward"))
            {
                sendInvalidParams(session, id, QStringLiteral("direction must be Forward or Backward"));
                return;
            }
            direction = Function::stringToDirection(name);
        }

        bool hasMode = params.contains(QStringLiteral("mode"));
        EFXFixture::Mode mode = ef->mode();
        if (hasMode && modeFromApi(params.value(QStringLiteral("mode")).toString(), mode) == false)
        {
            sendInvalidParams(session, id, QStringLiteral("mode must be PanTilt, Dimmer or RGB"));
            return;
        }

        if (checkRevision(doc, session, id, params) == false)
            return;

        quint32 before = doc->docRevision();
        if (hasDirection)
            ef->setDirection(direction);
        if (hasMode)
            ef->setMode(mode);
        QJsonValue offset = params.value(QStringLiteral("startOffset"));
        if (offset.isDouble())
            ef->setStartOffset(offset.toInt()); // clamped to 0-359 by the engine
        ensureBumped(doc, before);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        broadcastEfxFixturesChanged(efx, session->clientId());
    });

    dispatcher->registerMethod(QStringLiteral("functions.efx.reorderFixture"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        EFX *efx = qobject_cast<EFX *>(findFunction(doc, params));
        if (efx == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such EFX"));
            return;
        }

        quint32 fixtureId = 0;
        if (parseId(params.value(QStringLiteral("fixture")), fixtureId) == false)
        {
            sendInvalidParams(session, id, QStringLiteral("fixture is required"));
            return;
        }
        int head = params.value(QStringLiteral("head")).toInt(0);
        EFXFixture *ef = efx->fixture(fixtureId, head);
        if (ef == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("Fixture %1 head %2 is not in this EFX").arg(fixtureId).arg(head));
            return;
        }

        QString move = params.value(QStringLiteral("move")).toString();
        if (move != QStringLiteral("raise") && move != QStringLiteral("lower"))
        {
            sendInvalidParams(session, id, QStringLiteral("move must be raise or lower"));
            return;
        }

        if (checkRevision(doc, session, id, params) == false)
            return;

        quint32 before = doc->docRevision();
        bool moved = move == QStringLiteral("raise") ? efx->raiseFixture(ef) : efx->lowerFixture(ef);
        if (moved == false)
        {
            sendInvalidParams(session, id, move == QStringLiteral("raise")
                              ? QStringLiteral("Fixture %1 head %2 is already first").arg(fixtureId).arg(head)
                              : QStringLiteral("Fixture %1 head %2 is already last").arg(fixtureId).arg(head));
            return;
        }
        ensureBumped(doc, before);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        broadcastEfxFixturesChanged(efx, session->clientId());
    });

    dispatcher->registerMethod(QStringLiteral("functions.efx.setFixturesOffset"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        EFX *efx = qobject_cast<EFX *>(findFunction(doc, params));
        if (efx == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such EFX"));
            return;
        }

        QJsonValue offsetValue = params.value(QStringLiteral("offset"));
        if (offsetValue.isDouble() == false)
        {
            sendInvalidParams(session, id, QStringLiteral("offset is required"));
            return;
        }
        int offset = offsetValue.toInt();

        QString mode = params.value(QStringLiteral("mode")).toString(QStringLiteral("Increasing"));
        if (mode != QStringLiteral("Absolute") && mode != QStringLiteral("Increasing") && mode != QStringLiteral("Random"))
        {
            sendInvalidParams(session, id, QStringLiteral("mode must be Absolute, Increasing or Random"));
            return;
        }

        if (checkRevision(doc, session, id, params) == false)
            return;

        // EFXEditor::setFixturesOffset(), verbatim
        quint32 before = doc->docRevision();
        QList<EFXFixture *> fixtures = efx->fixtures();
        if (mode == QStringLiteral("Absolute"))
        {
            for (EFXFixture *ef : fixtures)
                ef->setStartOffset(offset);
        }
        else
        {
            QList<int> offsets;
            int currentOffset = 0;
            for (int i = 0; i < fixtures.count(); i++)
            {
                offsets.append(currentOffset % 360);
                currentOffset += offset;
            }
            if (mode == QStringLiteral("Random"))
            {
                for (int i = offsets.count() - 1; i > 0; i--)
                {
                    int j = QRandomGenerator::global()->generate() % (i + 1);
                    qSwap(offsets[i], offsets[j]);
                }
            }
            for (int i = 0; i < fixtures.count(); i++)
                fixtures.at(i)->setStartOffset(offsets.at(i));
        }
        ensureBumped(doc, before);

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));
        broadcastEfxFixturesChanged(efx, session->clientId());
    });

    dispatcher->registerMethod(QStringLiteral("functions.efx.getPreview"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        EFX *efx = qobject_cast<EFX *>(findFunction(doc, params));
        if (efx == nullptr)
        {
            sendNotFound(session, id, QStringLiteral("No such EFX"));
            return;
        }
        bool includePaths = params.value(QStringLiteral("includeFixturePaths")).toBool(false);
        session->send(ApiEnvelope::buildOkResponse(id, efxPreviewToJson(efx, includePaths)));
    });
}
