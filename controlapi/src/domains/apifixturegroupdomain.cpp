/*
  Q Light Controller Plus - Control API
  apifixturegroupdomain.cpp

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
#include <QMap>

#include "apifixturegroupdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "fixturegroup.h"
#include "grouphead.h"
#include "qlcpoint.h"
#include "fixture.h"
#include "doc.h"

namespace {

/** Wire IDs are strings (docs/api-spec/fragments/fixtures-notes.md's
 *  "IDs as strings on the wire" decision) even though the engine's own
 *  FixtureGroup/Fixture ids are quint32 - parse defensively, INVALID_PARAMS
 *  on anything that isn't a clean unsigned integer string. */
bool parseId(const QJsonValue &value, quint32 &out)
{
    bool ok = false;
    out = value.toString().toUInt(&ok);
    return ok;
}

QJsonObject headEntryToJson(const QLCPoint &pt, const GroupHead &head)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), pt.x());
    obj.insert(QStringLiteral("y"), pt.y());
    obj.insert(QStringLiteral("fixtureId"), QString::number(head.fxi));
    obj.insert(QStringLiteral("headIndex"), head.head);
    return obj;
}

// FixtureGroup::size() is a QSize - width maps to the spec's "columns" and
// height to "rows", matching FixtureGroup::saveXML()'s own X/Y attributes
// (X == width/columns, Y == height/rows).
QJsonObject groupSizeToJson(const QSize &size)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("columns"), size.width());
    obj.insert(QStringLiteral("rows"), size.height());
    return obj;
}

QJsonObject groupSummaryToJson(FixtureGroup *grp)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(grp->id()));
    obj.insert(QStringLiteral("name"), grp->name());
    obj.insert(QStringLiteral("size"), groupSizeToJson(grp->size()));
    obj.insert(QStringLiteral("headCount"), grp->headList().count());
    return obj;
}

QJsonObject groupToJson(FixtureGroup *grp)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), QString::number(grp->id()));
    obj.insert(QStringLiteral("name"), grp->name());
    obj.insert(QStringLiteral("size"), groupSizeToJson(grp->size()));

    QJsonArray heads;
    QMap<QLCPoint, GroupHead> map = grp->headsMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        heads.append(headEntryToJson(it.key(), it.value()));
    obj.insert(QStringLiteral("heads"), heads);

    return obj;
}

/** §4a baseRevision check, shared by every mutating method below - same
 *  shape as ApiIoDomain's io.universe.create handler. Sends the CONFLICT
 *  response itself on mismatch. */
bool checkBaseRevision(Doc *doc, const QJsonObject &params, ApiSession *session, const QString &id)
{
    quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
    if (baseRevision != doc->docRevision())
    {
        QJsonObject details;
        details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                        QStringLiteral("baseRevision is stale"), details));
        return false;
    }
    return true;
}

/** Parses params["groupId"] and looks it up in doc, sending INVALID_PARAMS/
 *  NOT_FOUND itself on failure. Returns nullptr on either failure; outGroupId
 *  is only meaningful when a non-null FixtureGroup* is returned. */
FixtureGroup *findGroupOrRespond(Doc *doc, const QJsonObject &params, ApiSession *session,
                                  const QString &id, quint32 &outGroupId)
{
    if (parseId(params.value(QStringLiteral("groupId")), outGroupId) == false)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("Invalid groupId")));
        return nullptr;
    }

    FixtureGroup *grp = doc->fixtureGroup(outGroupId);
    if (grp == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No such fixture group")));
        return nullptr;
    }

    return grp;
}

} // namespace

ApiFixtureGroupDomain::ApiFixtureGroupDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods();

    // Old-style string-based connect() deliberately, not the modern pointer
    // syntax - Doc lives in qlcplusengine.dll and this class lives in the
    // separate qlcplusapiserver static library; see ApiIoDomain's own
    // constructor comment for the MinGW auto-import edge case this avoids.
    connect(m_doc, SIGNAL(fixtureGroupChanged(quint32)), this, SLOT(slotFixtureGroupChanged(quint32)));
}

void ApiFixtureGroupDomain::slotFixtureGroupChanged(quint32 id)
{
    FixtureGroup *grp = m_doc->fixtureGroup(id);
    if (grp == nullptr)
        return; // gone already (e.g. deleted as part of the same cascade) - nothing to report

    if (m_pendingChangeKind == ChangeRenamed)
    {
        QJsonObject data;
        data.insert(QStringLiteral("groupId"), QString::number(id));
        data.insert(QStringLiteral("name"), m_pendingRenamedName);
        data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.group.renamed"), data, m_pendingOriginClientId, false);
    }
    else
    {
        QJsonObject data;
        data.insert(QStringLiteral("group"), groupToJson(grp));
        data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.group.updated"), data, m_pendingOriginClientId, false);
    }
}

void ApiFixtureGroupDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("fixtures.group.list"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonArray groups;
        for (FixtureGroup *grp : doc->fixtureGroups())
            groups.append(groupSummaryToJson(grp));

        QJsonObject result;
        result.insert(QStringLiteral("groups"), groups);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.get"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 groupId;
        FixtureGroup *grp = findGroupOrRespond(doc, params, session, id, groupId);
        if (grp == nullptr)
            return;

        session->send(ApiEnvelope::buildOkResponse(id, groupToJson(grp)));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.create"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        QString name = params.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("name is required")));
            return;
        }

        int columns = params.contains(QStringLiteral("columns")) ? params.value(QStringLiteral("columns")).toInt() : 1;
        int rows = params.contains(QStringLiteral("rows")) ? params.value(QStringLiteral("rows")).toInt() : 1;
        if (columns < 1 || rows < 1)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("columns/rows must be >= 1")));
            return;
        }

        // Fully configure the group BEFORE handing it to Doc::addFixtureGroup():
        // the changed()->Doc connection is only wired up inside that call, so
        // setName()/setSize() here don't spuriously fire it (or bump
        // docRevision) ahead of the one clean bump addFixtureGroup() itself
        // causes via setModified().
        FixtureGroup *grp = new FixtureGroup(doc);
        grp->setName(name);
        grp->setSize(QSize(columns, rows));

        bool added = doc->addFixtureGroup(grp);
        if (added == false)
        {
            delete grp;
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                            QStringLiteral("Could not add fixture group")));
            return;
        }

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("groupId"), QString::number(grp->id()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        // Built explicitly here rather than from a Doc::fixtureGroupAdded
        // connection - see this class's header comment for why (Doc emits
        // that signal BEFORE bumping docRevision, so a signal-driven
        // broadcast would report a stale revision).
        QJsonObject data;
        data.insert(QStringLiteral("group"), groupToJson(grp));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.group.created"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.rename"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 groupId;
        FixtureGroup *grp = findGroupOrRespond(doc, params, session, id, groupId);
        if (grp == nullptr)
            return;

        QString name = params.value(QStringLiteral("name")).toString();

        // See this class's header comment: FixtureGroup::setName() emits the
        // same generic changed() signal every other mutator does, so
        // slotFixtureGroupChanged() needs to be told which wire topic this
        // particular change should become.
        m_pendingChangeKind = ChangeRenamed;
        m_pendingRenamedName = name;
        m_pendingOriginClientId = session->clientId();
        grp->setName(name); // no-op (no signal, no revision bump) if name is unchanged
        m_pendingOriginClientId.clear();
        m_pendingChangeKind = ChangeUpdated;
        m_pendingRenamedName.clear();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.delete"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 groupId;
        if (findGroupOrRespond(doc, params, session, id, groupId) == nullptr)
            return;

        bool deleted = doc->deleteFixtureGroup(groupId);
        if (deleted == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                                                            QStringLiteral("Could not delete fixture group")));
            return;
        }

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        // Built explicitly here rather than from a Doc::fixtureGroupRemoved
        // connection - same stale-revision-ordering reason as create's own
        // comment above.
        QJsonObject data;
        data.insert(QStringLiteral("groupId"), QString::number(groupId));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.group.deleted"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.setSize"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 groupId;
        FixtureGroup *grp = findGroupOrRespond(doc, params, session, id, groupId);
        if (grp == nullptr)
            return;

        int columns = params.value(QStringLiteral("columns")).toInt();
        int rows = params.value(QStringLiteral("rows")).toInt();
        if (columns < 1 || rows < 1)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("columns/rows must be >= 1")));
            return;
        }

        m_pendingOriginClientId = session->clientId();
        // FixtureGroup::setSize() always emits changed() unconditionally,
        // even when the size doesn't actually change - see fixturegroup.cpp.
        // Shrinking doesn't drop now-out-of-bounds heads (fixtures.yaml's
        // own inline note); they stay in headsMap() until resigned.
        grp->setSize(QSize(columns, rows));
        m_pendingOriginClientId.clear();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.assignFixture"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 groupId;
        FixtureGroup *grp = findGroupOrRespond(doc, params, session, id, groupId);
        if (grp == nullptr)
            return;

        quint32 fixtureId;
        if (parseId(params.value(QStringLiteral("fixtureId")), fixtureId) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid fixtureId")));
            return;
        }

        // FixtureGroup::assignFixture() Q_ASSERTs on a null Fixture* - a
        // debug build would abort the process on a bad fixtureId, so this
        // must be checked here rather than left to the engine call.
        if (doc->fixture(fixtureId) == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such fixture")));
            return;
        }

        bool hasXY = params.contains(QStringLiteral("x")) && params.contains(QStringLiteral("y"));
        QLCPoint pt; // null point (the default QLCPoint()) means "auto-place"
        if (hasXY)
            pt = QLCPoint(params.value(QStringLiteral("x")).toInt(), params.value(QStringLiteral("y")).toInt());

        m_pendingOriginClientId = session->clientId();
        grp->assignFixture(fixtureId, pt);
        m_pendingOriginClientId.clear();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.assignHead"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 groupId;
        FixtureGroup *grp = findGroupOrRespond(doc, params, session, id, groupId);
        if (grp == nullptr)
            return;

        quint32 fixtureId;
        if (parseId(params.value(QStringLiteral("fixtureId")), fixtureId) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid fixtureId")));
            return;
        }

        Fixture *fxi = doc->fixture(fixtureId);
        if (fxi == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such fixture")));
            return;
        }

        int headIndex = params.value(QStringLiteral("headIndex")).toInt();
        if (headIndex < 0 || headIndex >= qMax(1, fxi->heads()))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("headIndex out of range")));
            return;
        }

        GroupHead newHead(fixtureId, headIndex);

        // NOTE for the merge/review pass (see fixturegroup-notes.md): despite
        // FixtureGroup::assignHead()'s own header comment claiming "if the
        // fixture head is already present in the group, it is moved [...] if
        // another fixture head occupies the new point, the two fixture heads
        // will simply switch places", the actual fixturegroup.cpp body does
        // neither - it flatly refuses (returns false, no-op) to place a head
        // that's already present anywhere in the group, and an explicit (x,y)
        // placement just overwrites whatever was there with no swap. The real
        // qmlui/fixturegroupeditor.cpp achieves an actual "move" by calling
        // resignHead() then assignHead() itself (see its transformSelection()).
        // This handler reproduces that same composition using only real
        // engine primitives (swap()/resignHead()/assignHead()), so the API's
        // documented assignHead contract (move; swap-on-collision) is
        // actually honoured, without inventing new engine-level behaviour.
        QLCPoint oldPt;
        bool hasOld = false;
        {
            QMap<QLCPoint, GroupHead> map = grp->headsMap();
            for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            {
                if (it.value() == newHead)
                {
                    oldPt = it.key();
                    hasOld = true;
                    break;
                }
            }
        }

        bool hasXY = params.contains(QStringLiteral("x")) && params.contains(QStringLiteral("y"));

        m_pendingOriginClientId = session->clientId();
        if (hasXY)
        {
            // NOTE: QLCPoint(0,0).isNull() is true (QPoint semantics), which
            // is also the engine's own auto-place sentinel - an explicit
            // {x:0,y:0} on a *new* head (no oldPt) is indistinguishable from
            // "auto-place" once it reaches FixtureGroup::assignHead(). Not
            // worth working around for this slice; flagged in the notes file.
            QLCPoint targetPt(params.value(QStringLiteral("x")).toInt(), params.value(QStringLiteral("y")).toInt());
            if (hasOld)
            {
                if (oldPt != targetPt)
                    grp->swap(oldPt, targetPt); // true move-with-swap, via the real engine primitive
                // else: already exactly there - nothing to do
            }
            else
            {
                grp->assignHead(targetPt, newHead);
            }
        }
        else
        {
            // Auto-place: resign the head's current slot first (if any) so
            // FixtureGroup::assignHead()'s own "head already present
            // anywhere -> refuse" guard doesn't reject the move.
            if (hasOld)
                grp->resignHead(oldPt);
            grp->assignHead(QLCPoint(), newHead);
        }
        m_pendingOriginClientId.clear();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.unassignHead"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 groupId;
        FixtureGroup *grp = findGroupOrRespond(doc, params, session, id, groupId);
        if (grp == nullptr)
            return;

        QLCPoint pt(params.value(QStringLiteral("x")).toInt(), params.value(QStringLiteral("y")).toInt());

        m_pendingOriginClientId = session->clientId();
        // resignHead() is a safe no-op (no signal, no revision bump) if the
        // cell is already empty - see FixtureGroup::resignHead().
        grp->resignHead(pt);
        m_pendingOriginClientId.clear();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.unassignFixture"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 groupId;
        FixtureGroup *grp = findGroupOrRespond(doc, params, session, id, groupId);
        if (grp == nullptr)
            return;

        quint32 fixtureId;
        if (parseId(params.value(QStringLiteral("fixtureId")), fixtureId) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid fixtureId")));
            return;
        }

        m_pendingOriginClientId = session->clientId();
        // resignFixture() doesn't require the fixture to still exist in Doc
        // or even to currently be in this group (see engine/test/
        // fixturegroup/fixturegroup_test.cpp's own "remove a nonexistent
        // fixture" case) - and unconditionally emits changed() either way.
        grp->resignFixture(fixtureId);
        m_pendingOriginClientId.clear();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.swapHeads"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 groupId;
        FixtureGroup *grp = findGroupOrRespond(doc, params, session, id, groupId);
        if (grp == nullptr)
            return;

        QLCPoint a(params.value(QStringLiteral("ax")).toInt(), params.value(QStringLiteral("ay")).toInt());
        QLCPoint b(params.value(QStringLiteral("bx")).toInt(), params.value(QStringLiteral("by")).toInt());

        m_pendingOriginClientId = session->clientId();
        grp->swap(a, b);
        m_pendingOriginClientId.clear();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("fixtures.group.reset"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        quint32 groupId;
        FixtureGroup *grp = findGroupOrRespond(doc, params, session, id, groupId);
        if (grp == nullptr)
            return;

        m_pendingOriginClientId = session->clientId();
        grp->reset();
        m_pendingOriginClientId.clear();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}
