/*
  Q Light Controller Plus - Control API
  apipalettedomain.cpp

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

#include "apipalettedomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "qlcpalette.h"
#include "doc.h"

namespace {

QJsonObject paletteSummaryToJson(QLCPalette *palette)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), int(palette->id()));
    obj.insert(QStringLiteral("name"), palette->name());
    obj.insert(QStringLiteral("type"), QLCPalette::typeToString(palette->type()));
    return obj;
}

// Direct mirror of QLCPalette::values()'s QVariantList - see palette.yaml's
// PaletteValues schema / palette-notes.md for why this is a plain positional
// array rather than a per-type named-field object.
QJsonArray paletteValuesToJson(QLCPalette *palette)
{
    QJsonArray arr;
    for (const QVariant &v : palette->values())
        arr.append(QJsonValue::fromVariant(v));
    return arr;
}

QJsonObject paletteDetailToJson(QLCPalette *palette)
{
    QJsonObject obj = paletteSummaryToJson(palette);
    obj.insert(QStringLiteral("values"), paletteValuesToJson(palette));
    return obj;
}

// Applies a PaletteValues JSON array to a freshly-constructed (or
// about-to-be-updated) QLCPalette via the correct arity-specific setValue()
// overload, matching how QLCPalette::loadXML() itself dispatches by type
// (qlcpalette.cpp). An empty/absent array is a no-op, leaving the palette at
// whatever resetValues()/its constructor already left it at.
void applyValuesFromJson(QLCPalette *palette, const QJsonArray &values)
{
    if (values.isEmpty())
        return;

    switch (values.count())
    {
    case 1:
        palette->setValue(values.at(0).toVariant());
        break;
    case 2:
        palette->setValue(values.at(0).toVariant(), values.at(1).toVariant());
        break;
    default:
        palette->setValue(values.at(0).toVariant(), values.at(1).toVariant(), values.at(2).toVariant());
        break;
    }
}

} // namespace

ApiPaletteDomain::ApiPaletteDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods();

    // Old-style string-based connect() deliberately - see ApiIoDomain's own
    // constructor comment for the full MinGW-cross-DLL rationale (Doc lives
    // in qlcplusengine.dll, this class in the qlcplusapiserver static lib).
    connect(m_doc, SIGNAL(paletteAdded(quint32)), this, SLOT(slotPaletteAdded(quint32)));
    connect(m_doc, SIGNAL(paletteRemoved(quint32)), this, SLOT(slotPaletteRemoved(quint32)));
}

void ApiPaletteDomain::slotPaletteAdded(quint32 id)
{
    QLCPalette *palette = m_doc->palette(id);
    if (palette == nullptr)
        return;

    QJsonObject data;
    data.insert(QStringLiteral("palette"), paletteDetailToJson(palette));
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    // Structural (§4a): always delivered, not subscribe-gated.
    m_server->broadcast(QStringLiteral("palette.created"), data, m_pendingOriginClientId, false);
}

void ApiPaletteDomain::slotPaletteRemoved(quint32 id)
{
    QJsonObject data;
    data.insert(QStringLiteral("paletteId"), int(id));
    data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
    m_server->broadcast(QStringLiteral("palette.deleted"), data, m_pendingOriginClientId, false);
}

void ApiPaletteDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("palette.list"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonArray palettes;
        for (QLCPalette *palette : doc->palettes())
            palettes.append(paletteSummaryToJson(palette));

        QJsonObject result;
        result.insert(QStringLiteral("palettes"), palettes);
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("palette.get"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 paletteId = quint32(params.value(QStringLiteral("paletteId")).toInt());
        QLCPalette *palette = doc->palette(paletteId);
        if (palette == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such palette")));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, paletteDetailToJson(palette)));
    });

    dispatcher->registerMethod(QStringLiteral("palette.create"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        QLCPalette::PaletteType type = QLCPalette::stringToType(params.value(QStringLiteral("type")).toString());
        if (type == QLCPalette::Undefined)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("Unknown or missing palette type")));
            return;
        }

        QLCPalette *palette = new QLCPalette(type);
        palette->setName(params.value(QStringLiteral("name")).toString());
        applyValuesFromJson(palette, params.value(QStringLiteral("values")).toArray());

        // paletteAdded (relayed to Doc::setModified()/bumpRevision(), and to
        // this domain's own broadcast via slotPaletteAdded) fires
        // synchronously within addPalette() - doc->docRevision() and
        // palette->id() below already reflect the added palette. Unlike
        // InputOutputMap::addUniverse(), Doc::addPalette() assigns the id to
        // the palette object itself before returning, so there's no need to
        // pre-compute it the way ApiIoDomain::io.universe.create does.
        m_pendingOriginClientId = session->clientId();
        bool added = doc->addPalette(palette);
        m_pendingOriginClientId.clear();

        if (added == false)
        {
            delete palette;
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                                                            QStringLiteral("Could not add palette")));
            return;
        }

        QJsonObject result;
        result.insert(QStringLiteral("paletteId"), int(palette->id()));
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("palette.update"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        quint32 paletteId = quint32(params.value(QStringLiteral("paletteId")).toInt());
        QLCPalette *palette = doc->palette(paletteId);
        if (palette == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such palette")));
            return;
        }

        bool changed = false;

        if (params.contains(QStringLiteral("name")))
        {
            QString newName = params.value(QStringLiteral("name")).toString();
            if (newName != palette->name())
            {
                palette->setName(newName);
                changed = true;
            }
        }

        if (params.contains(QStringLiteral("values")))
        {
            QVariantList previous = palette->values();
            applyValuesFromJson(palette, params.value(QStringLiteral("values")).toArray());
            if (palette->values() != previous)
                changed = true;
        }

        // Doc has no paletteChanged/paletteUpdated signal (see doc.h) -
        // rename/setValue don't touch Doc's modified/revision state on
        // their own, so this handler must bump it itself, same as
        // qmlui/palettemanager.cpp's own updatePalette() methods do. Only
        // do so - and only broadcast - if something actually changed, same
        // no-op guard those methods use.
        if (changed)
            doc->setModified();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        if (changed)
        {
            QJsonObject data;
            data.insert(QStringLiteral("palette"), paletteDetailToJson(palette));
            data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            m_server->broadcast(QStringLiteral("palette.updated"), data, session->clientId(), false);
        }
    });

    dispatcher->registerMethod(QStringLiteral("palette.delete"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        quint32 paletteId = quint32(params.value(QStringLiteral("paletteId")).toInt());
        if (doc->palette(paletteId) == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such palette")));
            return;
        }

        // paletteRemoved (relayed to Doc::setModified()/bumpRevision(), and
        // to this domain's own broadcast via slotPaletteRemoved) fires
        // synchronously within deletePalette() - doc->docRevision() below
        // already reflects the deletion.
        m_pendingOriginClientId = session->clientId();
        doc->deletePalette(paletteId);
        m_pendingOriginClientId.clear();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}
