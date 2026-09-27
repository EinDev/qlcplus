/*
  Q Light Controller Plus - Control API
  apidocchanges.h

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

#ifndef APIDOCCHANGES_H
#define APIDOCCHANGES_H

#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QString>

class ApiServer;
class Doc;

/**
 * Before/after snapshot of what a bulk document edit created (the Show Wizard's generation, a
 * project import), so the edit can be announced with every domain's usual created events instead
 * of a one-off "everything changed" topic: construct before the edit, call broadcastCreated()
 * after it. Covers fixtures (fixtures.patched), fixture groups (fixtures.group.created), functions
 * (functions.created, id order so a chaser's steps precede it) and - when ApiServer's parent is an
 * ApiVcHost - Virtual Console pages (vc.page.created) and widgets (vc.widget.created). Palettes
 * need nothing: ApiPaletteDomain broadcasts palette.created from Doc::paletteAdded itself.
 */
class ApiDocChanges
{
public:
    ApiDocChanges(Doc *doc, ApiServer *server);

    /** Broadcast the created events for everything that did not exist at construction time.
     *  Fills the created* members (ids as strings, page indexes as ints). */
    void broadcastCreated(const QString &originClientId);

    QJsonArray createdFixtureIds;
    QJsonArray createdGroupIds;
    QJsonArray createdFunctionIds;
    QJsonArray createdPaletteIds;
    QJsonArray createdPageIndexes;
    QJsonArray createdWidgetIds;

private:
    Doc *m_doc;
    ApiServer *m_server;
    QSet<quint32> m_fixtures;
    QSet<quint32> m_groups;
    QSet<quint32> m_functions;
    QSet<quint32> m_palettes;
    QSet<quint32> m_widgets;
    int m_pageCount;
};

#endif
