/*
  Q Light Controller Plus - Control API
  apipalettedomain.h

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

#ifndef APIPALETTEDOMAIN_H
#define APIPALETTEDOMAIN_H

#include <QObject>
#include <QString>

class ApiServer;
class ApiIoDomain;
class Doc;

/**
 * palette.* domain (see docs/api-spec/fragments/palette.yaml and
 * palette-notes.md): CRUD on QLCPalette resources (engine/src/qlcpalette.h),
 * a §4a document-state resource. Same constructor-injected-Doc*,
 * connect-to-existing-signals pattern as ApiIoDomain/ApiCoreDomain.
 *
 * Unlike io.universe.*, Doc::addPalette()/deletePalette() already call
 * Doc::setModified() themselves (see doc.cpp) and their signals
 * (paletteAdded/paletteRemoved) fire synchronously within the mutator call -
 * so create/delete broadcasts are wired from those two slots, same as
 * ApiIoDomain::slotUniverseAdded. There is no paletteChanged/paletteUpdated
 * signal at all, though (rename/value changes don't touch Doc's modified
 * state on their own - see qmlui/palettemanager.cpp's own updatePalette()
 * methods, which call doc->setModified() by hand for exactly this reason),
 * so palette.update's handler calls doc->setModified() and broadcasts
 * palette.updated directly, with no slot involved.
 *
 * palette.apply is live control (§4b), not document state: it hands the
 * palette's QLCPalette::valuesFromFixtures() result (the desktop's
 * PaletteManager::previewPalette maths, fanning included) to
 * ApiIoDomain::overrideChannels(), the Simple Desk override path
 * fixtures.monitor.aimAt also uses, so the usual release clears it.
 */
class ApiPaletteDomain : public QObject
{
    Q_OBJECT

public:
    ApiPaletteDomain(Doc *doc, ApiServer *server, ApiIoDomain *ioDomain, QObject *parent = nullptr);

private:
    void registerMethods();

private slots:
    void slotPaletteAdded(quint32 id);
    void slotPaletteRemoved(quint32 id);

private:
    Doc *m_doc;
    ApiServer *m_server;
    ApiIoDomain *m_ioDomain;

    /** Requesting client's id, stashed by palette.create's handler just
     *  before calling Doc::addPalette() (whose paletteAdded signal fires
     *  synchronously on this same thread - see ApiIoDomain's
     *  m_pendingOriginClientId for the same pattern and its full rationale)
     *  so slotPaletteAdded() can attribute the broadcast to the right
     *  client. Cleared unconditionally right after the call returns.
     *  palette.delete does not need an equivalent: Doc::deletePalette()
     *  always emits paletteRemoved when it succeeds (no "already at that
     *  value, no-op" case unlike setGrandMasterValue/setBlackout), so
     *  slotPaletteRemoved() can safely read this same field within the same
     *  synchronous call stack. */
    QString m_pendingOriginClientId;
};

#endif
