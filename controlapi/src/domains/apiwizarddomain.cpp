/*
  Q Light Controller Plus - Control API
  apiwizarddomain.cpp

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
#include <QSet>

#include "apiwizarddomain.h"
#include "apimonitordomain.h"
#include "apidocchanges.h"
#include "apiwizardhost.h"
#include "apienvelope.h"
#include "apisession.h"
#include "apiserver.h"

#include "fixturegroup.h"
#include "doc.h"

namespace {

// Wire spellings, index == the engine enum value (StageWizard::ShowType / FixtureRole,
// MonitorProperties::StageType)
const char *const kShowTypes[] = { "ClubNight", "Concert", "Theatrical", "Architectural", "Custom" };
const char *const kRoles[] = { "Key", "Fill", "Back", "Side", "Effect", "Strip", "Blinder", "Hazer", "Floor" };
const char *const kStageTypes[] = { "Simple", "Box", "Rock", "Theatre" };

// StageWizard::EffectFlag, in StageWizard::buildEffectsModel()'s display order
struct EffectDef { const char *id; int flag; const char *name; const char *family; };
const EffectDef kEffects[] = {
    { "ColorPalette",   1 << 0,  "Color Palette",    "Color" },
    { "ColorRainbow",   1 << 9,  "Color Rainbow",    "Color" },
    { "SplitColor",     1 << 10, "Split Color",      "Color" },
    { "GoboPalette",    1 << 1,  "Gobo Palette",     "Color" },
    { "Shutter",        1 << 2,  "Shutter Effects",  "Intensity" },
    { "BlinderHit",     1 << 11, "Blinder Hit",      "Intensity" },
    { "StrobeChase",    1 << 12, "Strobe Chase",     "Intensity" },
    { "Heartbeat",      1 << 13, "Heartbeat",        "Intensity" },
    { "PositionPreset", 1 << 3,  "Position Presets", "Movement" },
    { "FlyOut",         1 << 4,  "Fly Out",          "Movement" },
    { "FlyIn",          1 << 5,  "Fly In",           "Movement" },
    { "CircleChase",    1 << 6,  "Circle Chase",     "Movement" },
    { "FigureEight",    1 << 7,  "Figure Eight",     "Movement" },
    { "AudienceSweep",  1 << 8,  "Audience Sweep",   "Movement" },
    { "PixelChase",     1 << 14, "Pixel Chase",      "Matrix" },
    { "Wave",           1 << 15, "Wave",             "Matrix" },
    { "Fireworks",      1 << 16, "Fireworks",        "Matrix" },
    { "Plasma",         1 << 17, "Plasma",           "Matrix" },
    { "Marquee",        1 << 18, "Marquee",          "Matrix" },
    { "AmbientLoop",    1 << 22, "Ambient Loop",     "Show Cues" },
};

template <int N>
int indexOf(const char *const (&table)[N], const QString &name)
{
    for (int i = 0; i < N; i++)
        if (name == QLatin1String(table[i]))
            return i;
    return -1;
}

template <int N>
QString nameAt(const char *const (&table)[N], int index)
{
    if (index < 0 || index >= N)
        return QString();
    return QString::fromLatin1(table[index]);
}

int effectFlag(const QString &id)
{
    for (const EffectDef &e : kEffects)
        if (id == QLatin1String(e.id))
            return e.flag;
    return 0;
}

QString effectId(int flag)
{
    for (const EffectDef &e : kEffects)
        if (e.flag == flag)
            return QString::fromLatin1(e.id);
    return QString();
}

QJsonArray idsToJson(const QJsonArray &uintIds)
{
    QJsonArray out;
    for (const QJsonValue &v : uintIds)
        out.append(QString::number(quint32(v.toDouble())));
    return out;
}

bool parseId(const QJsonValue &v, quint32 &out)
{
    bool ok = false;
    if (v.isString())
        out = v.toString().toUInt(&ok);
    else if (v.isDouble() && v.toDouble() >= 0)
    {
        out = quint32(v.toDouble());
        ok = true;
    }
    return ok;
}

QJsonObject staticCatalogues()
{
    QJsonObject result;

    // WizardStep1ShowType.qml
    struct ShowTypeText { const char *name; const char *description; const char *stage; int stageType; const char *tags; };
    const ShowTypeText showTypes[] = {
        { "Club Night", "Fast chasers, strobe hits, RGB chases, BPM-locked effects. Built for high energy dance floors.",
          "Box / Club", 1, "Fast,Strobe,RGB,BPM" },
        { "Concert / Live", "Position presets, color washes, audience blinders, movement EFX. Designed for live performances.",
          "Rock Stage", 2, "Wash,Position,Blinder,EFX" },
        { "Theatrical", "Scene-based, slow fades, warm colors, gobo patterns, position presets. For theatre and dance.",
          "Theatre", 3, "Scenes,Slow Fade,Gobo,Warm" },
        { "Architectural", "Gentle pixel chases, color blends, ambient loops. Ideal for permanent installations.",
          "Open Space", 0, "Pixel,Ambient,Soft,Loop" },
        { "Custom", "Choose your own effects and settings in the next steps. Nothing is pre-selected.",
          "Any", 0, "Manual,Flexible" },
    };
    QJsonArray showTypesJson;
    for (int i = 0; i < 5; i++)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), nameAt(kShowTypes, i));
        o.insert(QStringLiteral("name"), QString::fromUtf8(showTypes[i].name));
        o.insert(QStringLiteral("description"), QString::fromUtf8(showTypes[i].description));
        o.insert(QStringLiteral("stage"), QString::fromUtf8(showTypes[i].stage));
        o.insert(QStringLiteral("stageType"), nameAt(kStageTypes, showTypes[i].stageType));
        o.insert(QStringLiteral("tags"), QJsonArray::fromStringList(QString::fromUtf8(showTypes[i].tags).split(QLatin1Char(','))));
        showTypesJson.append(o);
    }
    result.insert(QStringLiteral("showTypes"), showTypesJson);

    // WizardStep2Fixtures.qml role names, WizardStep3Venue.qml placement blurbs
    const char *const roleNames[] = { "Key Light", "Fill Light", "Back Light", "Side Light", "Effect",
                                      "Strip / Bar", "Blinder", "Hazer", "Floor" };
    const char *const placements[] = {
        "Front truss, high \xe2\x80\x94 aimed at stage centre ~45\xc2\xb0",
        "Mid-height, forward position \xe2\x80\x94 supplemental wash",
        "Rear truss, high \xe2\x80\x94 backlighting from behind",
        "Wing booms, alternating left/right \xe2\x80\x94 side fill",
        "Top truss, centre \xe2\x80\x94 aerial mid-air beams",
        "Full-width batten across top front \xe2\x80\x94 colour wash",
        "Front edge, high \xe2\x80\x94 horizontal, aimed at audience",
        "Centre, mid-height \xe2\x80\x94 atmospheric haze",
        "Floor level, front \xe2\x80\x94 aimed straight up" };
    QJsonArray rolesJson;
    for (int i = 0; i < 9; i++)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), nameAt(kRoles, i));
        o.insert(QStringLiteral("name"), QString::fromUtf8(roleNames[i]));
        o.insert(QStringLiteral("placement"), QString::fromUtf8(placements[i]));
        rolesJson.append(o);
    }
    result.insert(QStringLiteral("roles"), rolesJson);

    // WizardStep3Venue.qml
    struct StageText { const char *name; const char *description; const char *bestFor; };
    const StageText stages[] = {
        { "Open Space", "Plain floor with no scenic elements. Good for temporary rigs and general-purpose events.", "Architectural,Custom" },
        { "Box / Club", "Four walls and a ceiling. Truss along the perimeter. Best for club nights and small venues.", "Club Night" },
        { "Rock Stage", "Raised stage with front truss and vertical columns. Standard for concerts and live shows.", "Concert / Live" },
        { "Theatre", "Proscenium arch, front-of-house bars, side booms. Classic theatrical rig.", "Theatrical" },
    };
    QJsonArray stagesJson;
    for (int i = 0; i < 4; i++)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), nameAt(kStageTypes, i));
        o.insert(QStringLiteral("name"), QString::fromUtf8(stages[i].name));
        o.insert(QStringLiteral("description"), QString::fromUtf8(stages[i].description));
        o.insert(QStringLiteral("bestFor"), QJsonArray::fromStringList(QString::fromUtf8(stages[i].bestFor).split(QLatin1Char(','))));
        stagesJson.append(o);
    }
    result.insert(QStringLiteral("stageTypes"), stagesJson);

    QJsonArray effectsJson;
    for (const EffectDef &e : kEffects)
    {
        QJsonObject o;
        o.insert(QStringLiteral("id"), QString::fromLatin1(e.id));
        o.insert(QStringLiteral("name"), QString::fromLatin1(e.name));
        o.insert(QStringLiteral("family"), QString::fromLatin1(e.family));
        effectsJson.append(o);
    }
    result.insert(QStringLiteral("effects"), effectsJson);

    return result;
}

/** The capability flags of a host group/role entry, copied as they are */
void copyCapabilities(const QJsonObject &from, QJsonObject &to)
{
    for (const char *key : { "hasMovement", "hasRGB", "hasColorWheel", "hasGobo", "hasShutter", "hasDimmer" })
    {
        QString k = QString::fromLatin1(key);
        to.insert(k, from.value(k).toBool());
    }
}

} // namespace

ApiWizardDomain::ApiWizardDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    registerMethods();
}

ApiWizardHost *ApiWizardDomain::host() const
{
    return dynamic_cast<ApiWizardHost *>(m_server->parent());
}

bool ApiWizardDomain::parseChoices(const QJsonObject &json, ApiWizardChoices &choices, QString *error) const
{
    choices.showType = indexOf(kShowTypes, json.value(QStringLiteral("showType")).toString());
    if (choices.showType < 0)
    {
        *error = QStringLiteral("choices.showType must be one of ClubNight, Concert, Theatrical, Architectural, Custom");
        return false;
    }

    const QJsonArray groups = json.value(QStringLiteral("groups")).toArray();
    if (groups.isEmpty())
    {
        *error = QStringLiteral("choices.groups must list at least one group");
        return false;
    }

    QSet<quint32> seenGroups;
    bool anyFixtures = false;
    for (const QJsonValue &gv : groups)
    {
        const QJsonObject g = gv.toObject();
        ApiWizardGroupChoice gc;

        if (g.contains(QStringLiteral("groupId")))
        {
            if (parseId(g.value(QStringLiteral("groupId")), gc.groupId) == false ||
                m_doc->fixtureGroup(gc.groupId) == nullptr)
            {
                *error = QStringLiteral("No such fixture group: ") + g.value(QStringLiteral("groupId")).toVariant().toString();
                return false;
            }
            if (seenGroups.contains(gc.groupId))
            {
                *error = QStringLiteral("Fixture group listed twice: ") + QString::number(gc.groupId);
                return false;
            }
            seenGroups.insert(gc.groupId);
            gc.name = m_doc->fixtureGroup(gc.groupId)->name();
        }
        else
        {
            gc.name = g.value(QStringLiteral("name")).toString().trimmed();
            if (gc.name.isEmpty())
            {
                *error = QStringLiteral("A new group needs a name");
                return false;
            }
        }

        if (g.contains(QStringLiteral("fixtureIds")))
        {
            gc.hasFixtureIds = true;
            for (const QJsonValue &fv : g.value(QStringLiteral("fixtureIds")).toArray())
            {
                quint32 fid;
                if (parseId(fv, fid) == false || m_doc->fixture(fid) == nullptr)
                {
                    *error = QStringLiteral("No such fixture: ") + fv.toVariant().toString();
                    return false;
                }
                if (gc.fixtureIds.contains(fid) == false)
                    gc.fixtureIds.append(fid);
            }
        }

        if (gc.hasFixtureIds ? gc.fixtureIds.isEmpty() == false
                             : (gc.groupId != UINT_MAX && m_doc->fixtureGroup(gc.groupId)->fixtureList().isEmpty() == false))
            anyFixtures = true;

        if (g.contains(QStringLiteral("role")))
        {
            gc.role = indexOf(kRoles, g.value(QStringLiteral("role")).toString());
            if (gc.role < 0)
            {
                *error = QStringLiteral("Unknown role: ") + g.value(QStringLiteral("role")).toString();
                return false;
            }
        }

        choices.groups.append(gc);
    }

    if (anyFixtures == false)
    {
        *error = QStringLiteral("No selected group contains fixtures");
        return false;
    }

    if (json.contains(QStringLiteral("stageType")))
    {
        choices.stageType = indexOf(kStageTypes, json.value(QStringLiteral("stageType")).toString());
        if (choices.stageType < 0)
        {
            *error = QStringLiteral("choices.stageType must be one of Simple, Box, Rock, Theatre");
            return false;
        }
    }

    if (json.value(QStringLiteral("envSize")).isObject())
    {
        const QJsonObject env = json.value(QStringLiteral("envSize")).toObject();
        choices.hasEnvSize = true;
        choices.envWidth = env.value(QStringLiteral("width")).toDouble();
        choices.envHeight = env.value(QStringLiteral("height")).toDouble();
        choices.envDepth = env.value(QStringLiteral("depth")).toDouble();
        if (choices.envWidth <= 0 || choices.envHeight <= 0 || choices.envDepth <= 0)
        {
            *error = QStringLiteral("choices.envSize needs positive width, height and depth");
            return false;
        }
    }

    if (json.value(QStringLiteral("effects")).isArray())
    {
        choices.hasEffects = true;
        for (const QJsonValue &ev : json.value(QStringLiteral("effects")).toArray())
        {
            int flag = effectFlag(ev.toString());
            if (flag == 0)
            {
                *error = QStringLiteral("Unknown effect: ") + ev.toString();
                return false;
            }
            choices.effectFlags.append(flag);
        }
    }

    if (json.value(QStringLiteral("controller")).isObject())
    {
        const QJsonObject c = json.value(QStringLiteral("controller")).toObject();
        const QJsonValue uni = c.value(QStringLiteral("universe"));
        choices.hasController = true;
        choices.ctrlUniverse = (uni.isDouble() && uni.toInt() >= 0) ? uni.toInt() : -1;
        choices.ctrlMap = c.value(QStringLiteral("map")).toBool(true);
        choices.ctrlFeedback = c.value(QStringLiteral("feedback")).toBool(true);
        choices.ctrlColors = c.value(QStringLiteral("colors")).toBool(true);
    }

    return true;
}

QJsonObject ApiWizardDomain::previewToWire(const QJsonObject &preview) const
{
    QJsonObject out;
    out.insert(QStringLiteral("showType"), nameAt(kShowTypes, preview.value(QStringLiteral("showType")).toInt()));

    QJsonArray groups;
    for (const QJsonValue &gv : preview.value(QStringLiteral("groups")).toArray())
    {
        const QJsonObject g = gv.toObject();
        QJsonObject o;
        const QJsonValue gid = g.value(QStringLiteral("groupId"));
        o.insert(QStringLiteral("groupId"), gid.isDouble() ? QJsonValue(QString::number(quint32(gid.toDouble()))) : QJsonValue());
        o.insert(QStringLiteral("name"), g.value(QStringLiteral("name")).toString());
        o.insert(QStringLiteral("fixtureIds"), idsToJson(g.value(QStringLiteral("fixtureIds")).toArray()));
        o.insert(QStringLiteral("role"), nameAt(kRoles, g.value(QStringLiteral("role")).toInt()));
        copyCapabilities(g, o);
        groups.append(o);
    }
    out.insert(QStringLiteral("groups"), groups);

    out.insert(QStringLiteral("placesFixtures"), preview.value(QStringLiteral("placesFixtures")).toBool());
    out.insert(QStringLiteral("stageType"), nameAt(kStageTypes, preview.value(QStringLiteral("stageType")).toInt()));
    out.insert(QStringLiteral("envSize"), preview.value(QStringLiteral("envSize")).toObject());

    QJsonArray effects;
    for (const QJsonValue &ev : preview.value(QStringLiteral("effects")).toArray())
    {
        QJsonObject e = ev.toObject();
        QJsonObject o;
        o.insert(QStringLiteral("id"), effectId(e.value(QStringLiteral("flag")).toInt()));
        o.insert(QStringLiteral("name"), e.value(QStringLiteral("name")).toString());
        o.insert(QStringLiteral("family"), e.value(QStringLiteral("family")).toString());
        o.insert(QStringLiteral("enabled"), e.value(QStringLiteral("enabled")).toBool());
        o.insert(QStringLiteral("available"), e.value(QStringLiteral("available")).toBool());
        o.insert(QStringLiteral("preview"), e.value(QStringLiteral("preview")).toString());
        effects.append(o);
    }
    out.insert(QStringLiteral("effects"), effects);
    out.insert(QStringLiteral("controller"), preview.value(QStringLiteral("controller")).toObject());
    out.insert(QStringLiteral("summary"), preview.value(QStringLiteral("summary")).toArray());
    return out;
}

void ApiWizardDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("core.wizard.getOptions"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        ApiWizardHost *h = host();
        if (h == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                                                            QStringLiteral("The Show Wizard needs the desktop app")));
            return;
        }

        QJsonObject result = staticCatalogues();
        const QJsonObject project = h->wizardProjectOptions();

        QJsonArray groups;
        for (const QJsonValue &gv : project.value(QStringLiteral("groups")).toArray())
        {
            const QJsonObject g = gv.toObject();
            QJsonObject o;
            o.insert(QStringLiteral("groupId"), QString::number(quint32(g.value(QStringLiteral("groupId")).toDouble())));
            o.insert(QStringLiteral("name"), g.value(QStringLiteral("name")).toString());
            o.insert(QStringLiteral("fixtureIds"), idsToJson(g.value(QStringLiteral("fixtureIds")).toArray()));
            o.insert(QStringLiteral("suggestedRole"), nameAt(kRoles, g.value(QStringLiteral("suggestedRole")).toInt()));
            copyCapabilities(g, o);
            groups.append(o);
        }
        result.insert(QStringLiteral("groups"), groups);
        result.insert(QStringLiteral("controllers"), project.value(QStringLiteral("controllers")).toArray());
        result.insert(QStringLiteral("envSize"), project.value(QStringLiteral("envSize")).toObject());
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("core.wizard.preview"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiWizardHost *h = host();
        if (h == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                                                            QStringLiteral("The Show Wizard needs the desktop app")));
            return;
        }

        ApiWizardChoices choices;
        QString error;
        if (parseChoices(params.value(QStringLiteral("choices")).toObject(), choices, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, error));
            return;
        }

        QJsonObject preview = h->wizardPreview(choices, &error);
        if (preview.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, error));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, previewToWire(preview)));
    });

    dispatcher->registerMethod(QStringLiteral("core.wizard.generate"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiWizardHost *h = host();
        if (h == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                                                            QStringLiteral("The Show Wizard needs the desktop app")));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        ApiWizardChoices choices;
        QString error;
        if (parseChoices(params.value(QStringLiteral("choices")).toObject(), choices, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, error));
            return;
        }

        ApiDocChanges changes(doc, m_server);
        if (h->wizardGenerate(choices, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            error.isEmpty() ? QStringLiteral("Nothing was generated") : error));
            return;
        }
        doc->setModified();

        // The created events go out first so the response can carry every created id
        changes.broadcastCreated(session->clientId());

        // New groups are placed on the 3D stage: announce the stage and the moved fixtures
        bool placed = false;
        for (const ApiWizardGroupChoice &g : choices.groups)
            if (g.groupId == UINT_MAX)
                placed = true;
        if (placed)
        {
            QJsonObject stage;
            stage.insert(QStringLiteral("stage"), ApiMonitorDomain::stageToJson(doc));
            stage.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            m_server->broadcast(QStringLiteral("fixtures.monitor.changed"), stage, session->clientId(), false);

            QJsonArray items;
            for (const ApiWizardGroupChoice &g : choices.groups)
                if (g.groupId == UINT_MAX)
                    for (quint32 fid : g.fixtureIds)
                        items.append(ApiMonitorDomain::itemToJson(doc, fid, 0, 0, true));
            QJsonObject moved;
            moved.insert(QStringLiteral("items"), items);
            moved.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            m_server->broadcast(QStringLiteral("fixtures.monitor.changed"), moved, session->clientId(), false);
        }

        QJsonObject data;
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        data.insert(QStringLiteral("fixtureGroupIds"), changes.createdGroupIds);
        data.insert(QStringLiteral("functionIds"), changes.createdFunctionIds);
        data.insert(QStringLiteral("paletteIds"), changes.createdPaletteIds);
        data.insert(QStringLiteral("vcPageIndexes"), changes.createdPageIndexes);
        data.insert(QStringLiteral("vcWidgetIds"), changes.createdWidgetIds);
        session->send(ApiEnvelope::buildOkResponse(id, data));
        m_server->broadcast(QStringLiteral("core.wizard.generated"), data, session->clientId(), false);
    });
}
