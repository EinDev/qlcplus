/*
  Q Light Controller Plus - Control API
  apimonitordomain.cpp

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

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QSet>
#include <QtMath>

#include "apimonitordomain.h"
#include "apiiodomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "monitorproperties.h"
#include "monitorlayout.h"
#include "qlcfixturemode.h"
#include "qlcfixturehead.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#include "scenevalue.h"
#include "fixture.h"
#include "doc.h"

namespace {

struct ItemKey
{
    quint32 fixtureId = 0;
    quint16 headIndex = 0;
    quint16 linkedIndex = 0;
};

bool parseKey(const QJsonObject &obj, ItemKey &key)
{
    bool ok = false;
    key.fixtureId = obj.value(QStringLiteral("fixtureId")).toString().toUInt(&ok);
    if (ok == false)
        return false;
    int head = obj.value(QStringLiteral("headIndex")).toInt(0);
    int linked = obj.value(QStringLiteral("linkedIndex")).toInt(0);
    if (head < 0 || head > 0xFFFF || linked < 0 || linked > 0xFFFF)
        return false;
    key.headIndex = quint16(head);
    key.linkedIndex = quint16(linked);
    return true;
}

QJsonObject keyToJson(const ItemKey &key)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("fixtureId"), QString::number(key.fixtureId));
    obj.insert(QStringLiteral("headIndex"), int(key.headIndex));
    obj.insert(QStringLiteral("linkedIndex"), int(key.linkedIndex));
    return obj;
}

QJsonObject vectorToJson(const QVector3D &v)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), double(v.x()));
    obj.insert(QStringLiteral("y"), double(v.y()));
    obj.insert(QStringLiteral("z"), double(v.z()));
    return obj;
}

bool parseVector(const QJsonValue &value, QVector3D &out)
{
    if (value.isObject() == false)
        return false;
    QJsonObject obj = value.toObject();
    if (obj.contains(QStringLiteral("x")) == false || obj.contains(QStringLiteral("y")) == false ||
        obj.contains(QStringLiteral("z")) == false)
        return false;
    out = QVector3D(float(obj.value(QStringLiteral("x")).toDouble()),
                    float(obj.value(QStringLiteral("y")).toDouble()),
                    float(obj.value(QStringLiteral("z")).toDouble()));
    return true;
}

QString gridUnitsToString(MonitorProperties::GridUnits units)
{
    return units == MonitorProperties::Feet ? QStringLiteral("Feet") : QStringLiteral("Meters");
}

bool gridUnitsFromString(const QString &s, MonitorProperties::GridUnits &out)
{
    if (s == QStringLiteral("Meters")) { out = MonitorProperties::Meters; return true; }
    if (s == QStringLiteral("Feet")) { out = MonitorProperties::Feet; return true; }
    return false;
}

QString povToString(MonitorProperties::PointOfView pov)
{
    switch (pov)
    {
        case MonitorProperties::TopView: return QStringLiteral("TopView");
        case MonitorProperties::FrontView: return QStringLiteral("FrontView");
        case MonitorProperties::RightSideView: return QStringLiteral("RightSideView");
        case MonitorProperties::LeftSideView: return QStringLiteral("LeftSideView");
        default: return QStringLiteral("Undefined");
    }
}

bool povFromString(const QString &s, MonitorProperties::PointOfView &out)
{
    if (s == QStringLiteral("Undefined")) { out = MonitorProperties::Undefined; return true; }
    if (s == QStringLiteral("TopView")) { out = MonitorProperties::TopView; return true; }
    if (s == QStringLiteral("FrontView")) { out = MonitorProperties::FrontView; return true; }
    if (s == QStringLiteral("RightSideView")) { out = MonitorProperties::RightSideView; return true; }
    if (s == QStringLiteral("LeftSideView")) { out = MonitorProperties::LeftSideView; return true; }
    return false;
}

QString stageTypeToString(MonitorProperties::StageType type)
{
    switch (type)
    {
        case MonitorProperties::StageBox: return QStringLiteral("Box");
        case MonitorProperties::StageRock: return QStringLiteral("Rock");
        case MonitorProperties::StageTheatre: return QStringLiteral("Theatre");
        default: return QStringLiteral("Simple");
    }
}

bool stageTypeFromString(const QString &s, MonitorProperties::StageType &out)
{
    if (s == QStringLiteral("Simple")) { out = MonitorProperties::StageSimple; return true; }
    if (s == QStringLiteral("Box")) { out = MonitorProperties::StageBox; return true; }
    if (s == QStringLiteral("Rock")) { out = MonitorProperties::StageRock; return true; }
    if (s == QStringLiteral("Theatre")) { out = MonitorProperties::StageTheatre; return true; }
    return false;
}

struct FlagName
{
    const char *name;
    quint32 flag;
};

const FlagName kFlagNames[] =
{
    { "hidden", MonitorProperties::HiddenFlag },
    { "invertPan", MonitorProperties::InvertedPanFlag },
    { "invertTilt", MonitorProperties::InvertedTiltFlag },
    { "locked", MonitorProperties::LockedFlag },
    { "invertPositionX", MonitorProperties::InvertedPositionXFlag },
    { "invertPositionY", MonitorProperties::InvertedPositionYFlag },
    { "invertPositionZ", MonitorProperties::InvertedPositionZFlag },
    { "invertRotationX", MonitorProperties::InvertedRotationXFlag },
    { "invertRotationY", MonitorProperties::InvertedRotationYFlag },
    { "invertRotationZ", MonitorProperties::InvertedRotationZFlag },
};

QJsonObject flagsToJson(quint32 flags)
{
    QJsonObject obj;
    for (const FlagName &fn : kFlagNames)
        obj.insert(QLatin1String(fn.name), (flags & fn.flag) != 0);
    return obj;
}

/** §4a baseRevision check - sends the CONFLICT response itself on mismatch. */
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

/** Parses params["items"] into keys, validating that every fixture exists
 *  and every head index is within the fixture's mode. Sends the error itself. */
bool parseItemKeys(Doc *doc, const QJsonObject &params, ApiSession *session, const QString &id,
                   QList<ItemKey> &keys, bool allowEmpty = false)
{
    QJsonArray items = params.value(QStringLiteral("items")).toArray();
    if (items.isEmpty() && allowEmpty == false)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("items must not be empty")));
        return false;
    }

    for (const QJsonValue &v : items)
    {
        ItemKey key;
        if (parseKey(v.toObject(), key) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Invalid item key")));
            return false;
        }
        Fixture *fixture = doc->fixture(key.fixtureId);
        if (fixture == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                QStringLiteral("No such fixture: %1").arg(key.fixtureId)));
            return false;
        }
        if (fixture->fixtureMode() != nullptr && key.headIndex >= fixture->heads() && key.headIndex != 0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("Fixture %1 has no head %2").arg(key.fixtureId).arg(key.headIndex)));
            return false;
        }
        keys.append(key);
    }
    return true;
}

MonitorLayout::Item layoutItem(Doc *doc, const ItemKey &key)
{
    MonitorProperties *monProps = doc->monitorProperties();
    MonitorLayout::Item item;
    item.fixtureId = key.fixtureId;
    item.headIndex = key.headIndex;
    item.linkedIndex = key.linkedIndex;
    item.position = monProps->fixturePosition(key.fixtureId, key.headIndex, key.linkedIndex);
    item.rotation = monProps->fixtureRotation(key.fixtureId, key.headIndex, key.linkedIndex);
    item.locked = (monProps->fixtureFlags(key.fixtureId, key.headIndex, key.linkedIndex) & MonitorProperties::LockedFlag) != 0;
    Fixture *fixture = doc->fixture(key.fixtureId);
    item.size2D = MonitorLayout::item2DDimension(fixture != nullptr ? fixture->fixtureMode() : nullptr,
                                                 monProps->pointOfView());
    return item;
}

} // namespace

/*****************************************************************************
 * Serializers
 *****************************************************************************/

QJsonObject ApiMonitorDomain::stageToJson(Doc *doc)
{
    MonitorProperties *monProps = doc->monitorProperties();
    QJsonObject obj;
    obj.insert(QStringLiteral("gridSize"), vectorToJson(monProps->gridSize()));
    obj.insert(QStringLiteral("gridUnits"), gridUnitsToString(monProps->gridUnits()));
    obj.insert(QStringLiteral("pointOfView"), povToString(monProps->pointOfView()));
    obj.insert(QStringLiteral("stageType"), stageTypeToString(monProps->stageType()));
    obj.insert(QStringLiteral("showLabels"), monProps->labelsVisible());
    obj.insert(QStringLiteral("backgroundImage"), monProps->commonBackgroundImage());
    obj.insert(QStringLiteral("gridCenter"), vectorToJson(MonitorLayout::gridCenterPosition(monProps)));
    return obj;
}

QJsonObject ApiMonitorDomain::itemToJson(Doc *doc, quint32 fixtureId, quint16 headIndex, quint16 linkedIndex, bool placed)
{
    MonitorProperties *monProps = doc->monitorProperties();
    Fixture *fixture = doc->fixture(fixtureId);

    QJsonObject obj;
    obj.insert(QStringLiteral("fixtureId"), QString::number(fixtureId));
    obj.insert(QStringLiteral("headIndex"), int(headIndex));
    obj.insert(QStringLiteral("linkedIndex"), int(linkedIndex));
    obj.insert(QStringLiteral("placed"), placed);

    if (placed)
    {
        PreviewItem item = monProps->fixtureItem(fixtureId, headIndex, linkedIndex);
        obj.insert(QStringLiteral("itemName"), item.m_name);
        obj.insert(QStringLiteral("position"), vectorToJson(item.m_position));
        obj.insert(QStringLiteral("rotation"), vectorToJson(item.m_rotation));
        obj.insert(QStringLiteral("gelColor"), item.m_color.isValid() ? QJsonValue(item.m_color.name()) : QJsonValue());
        obj.insert(QStringLiteral("fixedZoom"), item.m_zoom);
        obj.insert(QStringLiteral("rotationScale"), double(item.m_rotationScale));
        obj.insert(QStringLiteral("positionRange"), double(item.m_positionRange));
        obj.insert(QStringLiteral("flags"), flagsToJson(item.m_flags));
    }
    else
    {
        obj.insert(QStringLiteral("itemName"), QString());
        obj.insert(QStringLiteral("position"), vectorToJson(MonitorLayout::gridCenterPosition(monProps)));
        obj.insert(QStringLiteral("rotation"), vectorToJson(QVector3D(0, 0, 0)));
        obj.insert(QStringLiteral("gelColor"), QJsonValue());
        obj.insert(QStringLiteral("fixedZoom"), 0);
        obj.insert(QStringLiteral("rotationScale"), 1.0);
        obj.insert(QStringLiteral("positionRange"), 800.0);
        obj.insert(QStringLiteral("flags"), flagsToJson(0));
    }

    if (fixture != nullptr)
    {
        obj.insert(QStringLiteral("name"), fixture->name());
        obj.insert(QStringLiteral("fixtureType"), fixture->typeString());
        obj.insert(QStringLiteral("universe"), int(fixture->universe()));
        obj.insert(QStringLiteral("address"), int(fixture->address()));
        obj.insert(QStringLiteral("channels"), int(fixture->channels()));

        QLCFixtureMode *mode = fixture->fixtureMode();
        int heads = mode != nullptr ? fixture->heads() : 0;
        obj.insert(QStringLiteral("heads"), heads);
        QJsonArray headChannels;
        for (int h = 0; h < heads; h++)
        {
            QJsonArray chans;
            for (quint32 ch : fixture->head(h).channels())
                chans.append(int(ch));
            headChannels.append(chans);
        }
        obj.insert(QStringLiteral("headChannels"), headChannels);

        QSizeF size = MonitorLayout::item2DDimension(mode, MonitorProperties::TopView);
        QSizeF front = MonitorLayout::item2DDimension(mode, MonitorProperties::FrontView);
        QJsonObject physical;
        physical.insert(QStringLiteral("width"), size.width());
        physical.insert(QStringLiteral("depth"), size.height());
        physical.insert(QStringLiteral("height"), front.height());
        if (mode != nullptr)
        {
            physical.insert(QStringLiteral("focusPanMax"), mode->physical().focusPanMax());
            physical.insert(QStringLiteral("focusTiltMax"), mode->physical().focusTiltMax());
        }
        obj.insert(QStringLiteral("physical"), physical);
        obj.insert(QStringLiteral("hasPan"), mode != nullptr &&
                   fixture->channelNumber(QLCChannel::Pan, QLCChannel::MSB) != QLCChannel::invalid());
        obj.insert(QStringLiteral("hasTilt"), mode != nullptr &&
                   fixture->channelNumber(QLCChannel::Tilt, QLCChannel::MSB) != QLCChannel::invalid());
    }

    return obj;
}

/*****************************************************************************
 * Domain
 *****************************************************************************/

ApiMonitorDomain::ApiMonitorDomain(Doc *doc, ApiServer *server, ApiIoDomain *ioDomain, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
    , m_ioDomain(ioDomain)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods();
}

void ApiMonitorDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    // fixtures.monitor.get {} -> {stage, items, docRevision}
    dispatcher->registerMethod(QStringLiteral("fixtures.monitor.get"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        MonitorProperties *monProps = doc->monitorProperties();
        QJsonArray items;
        QSet<quint32> placedFixtures;

        for (quint32 fid : monProps->fixtureItemsID())
        {
            if (doc->fixture(fid) == nullptr)
                continue;
            placedFixtures.insert(fid);
            for (quint32 subID : monProps->fixtureIDList(fid))
                items.append(itemToJson(doc, fid, monProps->fixtureHeadIndex(subID), monProps->fixtureLinkedIndex(subID), true));
        }

        for (Fixture *fixture : doc->fixtures())
        {
            if (fixture == nullptr || placedFixtures.contains(fixture->id()))
                continue;
            items.append(itemToJson(doc, fixture->id(), 0, 0, false));
        }

        QJsonObject result;
        result.insert(QStringLiteral("stage"), stageToJson(doc));
        result.insert(QStringLiteral("items"), items);
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // fixtures.monitor.setStage {gridSize?, gridUnits?, pointOfView?, stageType?, showLabels?, backgroundImage?, baseRevision}
    dispatcher->registerMethod(QStringLiteral("fixtures.monitor.setStage"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        MonitorProperties *monProps = doc->monitorProperties();
        QVector3D gridSize;
        MonitorProperties::GridUnits units = monProps->gridUnits();
        MonitorProperties::PointOfView pov = monProps->pointOfView();
        MonitorProperties::StageType stageType = monProps->stageType();

        bool hasGridSize = params.contains(QStringLiteral("gridSize"));
        if (hasGridSize && parseVector(params.value(QStringLiteral("gridSize")), gridSize) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("gridSize must be {x, y, z}")));
            return;
        }
        if (hasGridSize && (gridSize.x() <= 0 || gridSize.y() < 0 || gridSize.z() < 0))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("gridSize must be positive")));
            return;
        }
        if (params.contains(QStringLiteral("gridUnits")) &&
            gridUnitsFromString(params.value(QStringLiteral("gridUnits")).toString(), units) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("gridUnits must be Meters or Feet")));
            return;
        }
        if (params.contains(QStringLiteral("pointOfView")) &&
            povFromString(params.value(QStringLiteral("pointOfView")).toString(), pov) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Unknown pointOfView")));
            return;
        }
        if (params.contains(QStringLiteral("stageType")) &&
            stageTypeFromString(params.value(QStringLiteral("stageType")).toString(), stageType) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Unknown stageType")));
            return;
        }

        if (hasGridSize)
            monProps->setGridSize(gridSize);
        if (params.contains(QStringLiteral("gridUnits")))
            monProps->setGridUnits(units);
        // setPointOfView() re-derives the grid depth when leaving Undefined,
        // so it runs after an explicit gridSize and the stage is re-read below.
        if (params.contains(QStringLiteral("pointOfView")))
            monProps->setPointOfView(pov);
        if (params.contains(QStringLiteral("stageType")))
            monProps->setStageType(stageType);
        if (params.contains(QStringLiteral("showLabels")))
            monProps->setLabelsVisible(params.value(QStringLiteral("showLabels")).toBool());
        if (params.contains(QStringLiteral("backgroundImage")))
            monProps->setCommonBackgroundImage(params.value(QStringLiteral("backgroundImage")).toString());

        doc->setModified();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("stage"), stageToJson(doc));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.monitor.changed"), data, session->clientId(), false);
    });

    // fixtures.monitor.getBackground {} -> {path, mimeType, contentBase64}
    // Read-only: the bytes of the stage's common 2D background picture, so a
    // remote client can draw the file it picked on the engine host. Only the
    // file the stage already references is ever read.
    dispatcher->registerMethod(QStringLiteral("fixtures.monitor.getBackground"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        const QString path = doc->monitorProperties()->commonBackgroundImage();
        if (path.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No background image set")));
            return;
        }
        QFileInfo info(path);
        const QString suffix = info.suffix().toLower();
        static const QHash<QString, QString> mimeTypes = {
            { QStringLiteral("png"), QStringLiteral("image/png") },
            { QStringLiteral("jpg"), QStringLiteral("image/jpeg") },
            { QStringLiteral("jpeg"), QStringLiteral("image/jpeg") },
            { QStringLiteral("bmp"), QStringLiteral("image/bmp") },
            { QStringLiteral("gif"), QStringLiteral("image/gif") },
            { QStringLiteral("svg"), QStringLiteral("image/svg+xml") },
            { QStringLiteral("webp"), QStringLiteral("image/webp") }
        };
        if (mimeTypes.contains(suffix) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("The background is not a picture file: ") + path));
            return;
        }
        // 16 MB is far above any sensible stage plan and keeps a frame bounded
        if (info.isFile() == false || info.size() > 16 * 1024 * 1024)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("Background image not readable: ") + path));
            return;
        }
        QFile file(info.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("Background image not readable: ") + path));
            return;
        }
        QJsonObject result;
        result.insert(QStringLiteral("path"), path);
        result.insert(QStringLiteral("mimeType"), mimeTypes.value(suffix));
        result.insert(QStringLiteral("contentBase64"), QString::fromLatin1(file.readAll().toBase64()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // fixtures.monitor.setPlacement {items: [FixturesMonitorPlacementUpdate], baseRevision}
    dispatcher->registerMethod(QStringLiteral("fixtures.monitor.setPlacement"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        QList<ItemKey> keys;
        if (parseItemKeys(doc, params, session, id, keys) == false)
            return;

        MonitorProperties *monProps = doc->monitorProperties();
        QJsonArray updates = params.value(QStringLiteral("items")).toArray();

        // Validate everything before touching the document
        for (int i = 0; i < keys.count(); i++)
        {
            const ItemKey &key = keys.at(i);
            QJsonObject upd = updates.at(i).toObject();
            QVector3D tmp;
            if (upd.contains(QStringLiteral("position")) && parseVector(upd.value(QStringLiteral("position")), tmp) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("position must be {x, y, z}")));
                return;
            }
            if (upd.contains(QStringLiteral("rotation")) && parseVector(upd.value(QStringLiteral("rotation")), tmp) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("rotation must be {x, y, z}")));
                return;
            }
            if (upd.contains(QStringLiteral("gelColor")) && upd.value(QStringLiteral("gelColor")).isNull() == false &&
                QColor::isValidColorName(upd.value(QStringLiteral("gelColor")).toString()) == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("gelColor must be #rrggbb or null")));
                return;
            }
            if (upd.value(QStringLiteral("remove")).toBool(false))
            {
                if (key.linkedIndex == 0)
                {
                    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                        QStringLiteral("Only a linked copy (linkedIndex >= 1) can be removed")));
                    return;
                }
                if (monProps->containsItem(key.fixtureId, key.headIndex, key.linkedIndex) == false)
                {
                    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                        QStringLiteral("Fixture %1 has no linked item %2").arg(key.fixtureId).arg(key.linkedIndex)));
                    return;
                }
            }
        }

        QJsonArray itemsJson;
        QJsonArray removedJson;
        QJsonArray skippedJson;

        for (int i = 0; i < keys.count(); i++)
        {
            const ItemKey &key = keys.at(i);
            QJsonObject upd = updates.at(i).toObject();

            if (upd.value(QStringLiteral("remove")).toBool(false))
            {
                monProps->removeFixture(key.fixtureId, key.headIndex, key.linkedIndex);
                removedJson.append(keyToJson(key));
                continue;
            }

            if (monProps->containsItem(key.fixtureId, key.headIndex, key.linkedIndex) == false)
            {
                // New item: a linked copy starts as a copy of its base item
                // (ContextManager::setLinkedFixture does the same), a new
                // base item at the stage centre.
                PreviewItem base;
                if (monProps->containsItem(key.fixtureId, key.headIndex, 0))
                    base = monProps->fixtureItem(key.fixtureId, key.headIndex, 0);
                else
                    base.m_position = MonitorLayout::gridCenterPosition(monProps);
                base.m_flags &= ~quint32(MonitorProperties::LockedFlag);
                monProps->setFixtureItem(key.fixtureId, key.headIndex, key.linkedIndex, base);
            }

            quint32 flags = monProps->fixtureFlags(key.fixtureId, key.headIndex, key.linkedIndex);
            bool unlocking = upd.contains(QStringLiteral("locked")) && upd.value(QStringLiteral("locked")).toBool() == false;
            bool locked = (flags & MonitorProperties::LockedFlag) && unlocking == false;
            bool wantsMove = upd.contains(QStringLiteral("position")) || upd.contains(QStringLiteral("rotation"));

            if (wantsMove && locked)
            {
                skippedJson.append(keyToJson(key));
            }
            else
            {
                QVector3D v;
                if (upd.contains(QStringLiteral("position")) && parseVector(upd.value(QStringLiteral("position")), v))
                    monProps->setFixturePosition(key.fixtureId, key.headIndex, key.linkedIndex, v);
                if (upd.contains(QStringLiteral("rotation")) && parseVector(upd.value(QStringLiteral("rotation")), v))
                    monProps->setFixtureRotation(key.fixtureId, key.headIndex, key.linkedIndex, v);
            }

            if (upd.contains(QStringLiteral("gelColor")))
            {
                QJsonValue gel = upd.value(QStringLiteral("gelColor"));
                monProps->setFixtureGelColor(key.fixtureId, key.headIndex, key.linkedIndex,
                                             gel.isNull() ? QColor() : QColor(gel.toString()));
            }
            if (upd.contains(QStringLiteral("fixedZoom")))
            {
                // Like ContextManager::setFixedZoom(): only Dimmer-type fixtures
                Fixture *fixture = doc->fixture(key.fixtureId);
                if (fixture != nullptr && fixture->type() == QLCFixtureDef::Dimmer)
                    monProps->setFixtureFixedZoom(key.fixtureId, key.headIndex, key.linkedIndex,
                                                  upd.value(QStringLiteral("fixedZoom")).toInt());
            }
            if (upd.contains(QStringLiteral("rotationScale")))
                monProps->setFixtureRotationScale(key.fixtureId, key.headIndex, key.linkedIndex,
                                                  float(upd.value(QStringLiteral("rotationScale")).toDouble(1.0)));
            if (upd.contains(QStringLiteral("positionRange")))
                monProps->setFixturePositionRange(key.fixtureId, key.headIndex, key.linkedIndex,
                                                  float(upd.value(QStringLiteral("positionRange")).toDouble(800.0)));
            if (upd.contains(QStringLiteral("itemName")))
                monProps->setFixtureName(key.fixtureId, key.headIndex, key.linkedIndex,
                                         upd.value(QStringLiteral("itemName")).toString());

            for (const FlagName &fn : kFlagNames)
            {
                QString name = QLatin1String(fn.name);
                if (upd.contains(name) == false)
                    continue;
                if (upd.value(name).toBool())
                    flags |= fn.flag;
                else
                    flags &= ~fn.flag;
            }
            monProps->setFixtureFlags(key.fixtureId, key.headIndex, key.linkedIndex, flags);

            itemsJson.append(itemToJson(doc, key.fixtureId, key.headIndex, key.linkedIndex, true));
        }

        doc->setModified();

        QJsonObject result;
        result.insert(QStringLiteral("items"), itemsJson);
        result.insert(QStringLiteral("skippedLocked"), skippedJson);
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("items"), itemsJson);
        data.insert(QStringLiteral("removed"), removedJson);
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.monitor.changed"), data, session->clientId(), false);
    });

    // fixtures.monitor.arrange {items, op, args, baseRevision} -> {items, skippedLocked, docRevision}
    dispatcher->registerMethod(QStringLiteral("fixtures.monitor.arrange"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (checkBaseRevision(doc, params, session, id) == false)
            return;

        QList<ItemKey> keys;
        if (parseItemKeys(doc, params, session, id, keys) == false)
            return;

        MonitorProperties *monProps = doc->monitorProperties();
        int pov = monProps->pointOfView();
        QString op = params.value(QStringLiteral("op")).toString();
        QJsonObject args = params.value(QStringLiteral("args")).toObject();

        QList<MonitorLayout::Item> items;
        for (const ItemKey &key : keys)
            items.append(layoutItem(doc, key));

        QList<MonitorLayout::Item> result;
        if (op == QStringLiteral("circle"))
        {
            result = MonitorLayout::arrangeCircle(MonitorLayout::sortedByAddress(doc, items), pov,
                                                  args.value(QStringLiteral("diameter")).toDouble(2000),
                                                  args.value(QStringLiteral("lookAtCenter")).toBool(false));
        }
        else if (op == QStringLiteral("grid"))
        {
            result = MonitorLayout::arrangeGrid(MonitorLayout::groupOrdered(doc, items), pov,
                                                args.value(QStringLiteral("width")).toDouble(2000),
                                                args.value(QStringLiteral("height")).toDouble(2000),
                                                args.value(QStringLiteral("columns")).toInt(0),
                                                args.value(QStringLiteral("angle")).toDouble(0));
        }
        else if (op == QStringLiteral("line"))
        {
            result = MonitorLayout::arrangeLine(MonitorLayout::sortedByAddress(doc, items), pov,
                                                args.value(QStringLiteral("length")).toDouble(2000),
                                                args.value(QStringLiteral("angle")).toDouble(0),
                                                args.value(QStringLiteral("lookAtCenter")).toBool(false));
        }
        else if (op == QStringLiteral("rotate"))
        {
            result = MonitorLayout::rotateAroundCentroid(MonitorLayout::sortedByAddress(doc, items), pov,
                                                         args.value(QStringLiteral("angle")).toDouble(0));
        }
        else if (op == QStringLiteral("center"))
        {
            result = MonitorLayout::moveToCenter(MonitorLayout::sortedByAddress(doc, items),
                                                 MonitorLayout::gridCenterPosition(monProps));
        }
        else if (op == QStringLiteral("align"))
        {
            QString edge = args.value(QStringLiteral("edge")).toString();
            int alignment = edge == QStringLiteral("top") ? Qt::AlignTop : edge == QStringLiteral("left") ? Qt::AlignLeft : 0;
            if (alignment == 0)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("align needs args.edge = left|top")));
                return;
            }
            result = MonitorLayout::align(items, pov, alignment);
        }
        else if (op == QStringLiteral("distribute"))
        {
            QString dir = args.value(QStringLiteral("direction")).toString();
            int direction = dir == QStringLiteral("horizontal") ? Qt::Horizontal : dir == QStringLiteral("vertical") ? Qt::Vertical : 0;
            if (direction == 0)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("distribute needs args.direction = horizontal|vertical")));
                return;
            }
            if (items.count() < 3)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                                QStringLiteral("distribute needs at least 3 items")));
                return;
            }
            result = MonitorLayout::distribute(items, monProps, direction);
        }
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("Unknown op (circle, grid, line, rotate, center, align, distribute)")));
            return;
        }

        QJsonArray itemsJson;
        QJsonArray skippedJson;
        for (const MonitorLayout::Item &item : result)
        {
            if (item.positionChanged)
                monProps->setFixturePosition(item.fixtureId, item.headIndex, item.linkedIndex, item.position);
            if (item.rotationChanged)
                monProps->setFixtureRotation(item.fixtureId, item.headIndex, item.linkedIndex, item.rotation);
            ItemKey key;
            key.fixtureId = item.fixtureId;
            key.headIndex = item.headIndex;
            key.linkedIndex = item.linkedIndex;
            if (item.locked && item.positionChanged == false && item.rotationChanged == false &&
                op != QStringLiteral("align") && op != QStringLiteral("distribute"))
                skippedJson.append(keyToJson(key));
            itemsJson.append(itemToJson(doc, item.fixtureId, item.headIndex, item.linkedIndex, true));
        }

        doc->setModified();

        QJsonObject res;
        res.insert(QStringLiteral("items"), itemsJson);
        res.insert(QStringLiteral("skippedLocked"), skippedJson);
        res.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, res));

        QJsonObject data;
        data.insert(QStringLiteral("items"), itemsJson);
        data.insert(QStringLiteral("removed"), QJsonArray());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("fixtures.monitor.changed"), data, session->clientId(), false);
    });

    // fixtures.monitor.detectArrangement {items} -> {circleDiameter, lineLength, lineAngle, centroid}
    dispatcher->registerMethod(QStringLiteral("fixtures.monitor.detectArrangement"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QList<ItemKey> keys;
        if (parseItemKeys(doc, params, session, id, keys, true) == false)
            return;

        int pov = doc->monitorProperties()->pointOfView();
        QList<MonitorLayout::Item> items;
        for (const ItemKey &key : keys)
            items.append(layoutItem(doc, key));

        qreal angleRad = 0, length = 0;
        MonitorLayout::detectedLineFit(items, pov, angleRad, length);

        QJsonObject result;
        result.insert(QStringLiteral("circleDiameter"), MonitorLayout::detectedCircleDiameter(items, pov));
        result.insert(QStringLiteral("lineLength"), length);
        result.insert(QStringLiteral("lineAngle"), qRadiansToDegrees(angleRad));
        result.insert(QStringLiteral("centroid"), vectorToJson(MonitorLayout::centroid(items)));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // fixtures.monitor.aimAt {items, point} -> {fixtures, channels}
    dispatcher->registerMethod(QStringLiteral("fixtures.monitor.aimAt"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QList<ItemKey> keys;
        if (parseItemKeys(doc, params, session, id, keys) == false)
            return;

        QVector3D point;
        if (parseVector(params.value(QStringLiteral("point")), point) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("point must be {x, y, z} (mm)")));
            return;
        }

        MonitorProperties *monProps = doc->monitorProperties();
        float unitScale = monProps->gridUnits() == MonitorProperties::Meters ? 1.0f : 0.3048f;
        QVector3D gridMeters = monProps->gridSize() * unitScale;
        QVector3D pointMeters = point / 1000.0f;

        QJsonArray fixturesJson;
        QJsonArray channelsJson;
        QList<QPair<quint32, uchar>> entries;
        QSet<quint32> done; // one aim per fixture - its Pan/Tilt channels are shared by every item

        for (const ItemKey &key : keys)
        {
            if (done.contains(key.fixtureId))
                continue;
            Fixture *fixture = doc->fixture(key.fixtureId);
            if (fixture == nullptr || fixture->fixtureMode() == nullptr)
                continue;

            // MonitorProperties::fixtureBeamPosition() returns the beam origin
            // in metres centred on the stage (what the 3D scene uses); it still
            // hands back the root position when no LightEmitter data exists.
            // Uncentre it so it shares pointMeters' frame - the Qt tool adds
            // the same gridSize/2 to both sides, only the difference matters.
            QVector3D beamPos;
            QMatrix4x4 rotMatrix;
            MonitorProperties::fixtureBeamPosition(monProps, fixture, key.headIndex, beamPos, rotMatrix);
            QVector3D lightPos(beamPos.x() + gridMeters.x() / 2.0f, beamPos.y(), beamPos.z() + gridMeters.z() / 2.0f);
            QMatrix4x4 lightMatrix = MonitorLayout::lightMatrixFromRotation(rotMatrix);
            quint32 itemFlags = monProps->fixtureFlags(key.fixtureId, key.headIndex, key.linkedIndex);

            bool hasPan = false, hasTilt = false;
            qreal panDeg = 0, tiltDeg = 0;
            if (MonitorLayout::aimPanTilt(fixture, itemFlags, pointMeters, lightPos, lightMatrix,
                                          hasPan, panDeg, hasTilt, tiltDeg) == false)
                continue;

            done.insert(key.fixtureId);
            QJsonObject fx;
            fx.insert(QStringLiteral("fixtureId"), QString::number(fixture->id()));
            fx.insert(QStringLiteral("headIndex"), int(key.headIndex));

            QList<SceneValue> values;
            if (hasPan)
            {
                fx.insert(QStringLiteral("panDegrees"), panDeg);
                values.append(fixture->positionToValues(QLCChannel::Pan, float(panDeg)));
            }
            if (hasTilt)
            {
                fx.insert(QStringLiteral("tiltDegrees"), tiltDeg);
                values.append(fixture->positionToValues(QLCChannel::Tilt, float(tiltDeg)));
            }
            fixturesJson.append(fx);

            for (const SceneValue &sv : values)
            {
                quint32 address = fixture->universeAddress() + sv.channel;
                entries.append(qMakePair(address, sv.value));
                QJsonObject ch;
                ch.insert(QStringLiteral("address"), int(address));
                ch.insert(QStringLiteral("value"), int(sv.value));
                channelsJson.append(ch);
            }
        }

        if (m_ioDomain != nullptr && entries.isEmpty() == false)
            m_ioDomain->overrideChannels(entries, session->clientId());

        QJsonObject result;
        result.insert(QStringLiteral("fixtures"), fixturesJson);
        result.insert(QStringLiteral("channels"), channelsJson);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}
