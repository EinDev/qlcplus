/*
  Q Light Controller Plus
  projectimporter.h

  Copyright (c) Massimo Callegari

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

#ifndef PROJECTIMPORTER_H
#define PROJECTIMPORTER_H

#include <QByteArray>
#include <QString>
#include <QList>
#include <QPair>
#include <QMap>

class QXmlStreamReader;
class Doc;

/** @addtogroup engine Engine
 * @{
 */

/**
 * "Import from project": loads another workspace (.qxw) into a private Doc and copies a selection
 * of its fixtures, fixture groups, palettes and functions into a target Doc, remapping every id
 * reference on the way. This is the UI-free core of qmlui's ImportManager (which adds the QML
 * check-box trees on top) and of the control API's core.project.import.
 *
 * Selection rules (what checking an item in the import popup does):
 *  - a function brings its whole dependency closure: fixtures, fixture groups (and their
 *    fixtures), palettes and the functions it runs (chaser steps, collection members, sequence
 *    bound scene, script-started functions);
 *  - a fixture group brings its fixtures;
 *  - a fixture can bring its linked (monitor-only) items, which only carry a name.
 *
 * Import rules (apply()):
 *  - a fixture, group or palette whose NAME already exists in the target is matched, not copied;
 *  - new fixtures keep their universe/address when free, else take the next free range (never
 *    straddling a universe boundary); a fixture whose definition the target cannot resolve is
 *    skipped (skippedFixtures());
 *  - every function is copied with a NEW id, dependencies first, and its references rewritten.
 */
class ProjectImporter
{
public:
    explicit ProjectImporter(Doc *targetDoc);
    ~ProjectImporter();

    /** Load the workspace file at $fileName (a "file:" URL is accepted). Resets the selection. */
    bool loadFile(const QString &fileName);

    /** Load a workspace from its XML text. Relative media paths resolve against $workspacePath. */
    bool loadData(const QByteArray &xml, const QString &workspacePath);

    /** The loaded source project (read-only use) */
    Doc *sourceDoc() const;

    /*********************************************************************
     * Selection
     *********************************************************************/
    void selectFixture(quint32 fixtureID, bool withLinked);
    void deselectFixture(quint32 fixtureID);
    void selectLinkedItem(quint32 fixtureID, quint16 headIndex, quint16 linkedIndex);
    void deselectLinkedItem(quint32 fixtureID, quint16 headIndex, quint16 linkedIndex);

    /** Select a fixture group and its fixtures */
    void selectFixtureGroup(quint32 groupID);
    void deselectFixtureGroup(quint32 groupID);

    /** Select a function and its dependency closure */
    void selectFunction(quint32 functionID);
    /** Deselect a function only (its dependencies stay selected, like unchecking it in the popup) */
    void deselectFunction(quint32 functionID);

    QList<quint32> selectedFixtures() const { return m_fixtureIDList; }
    QList<quint32> selectedFixtureGroups() const { return m_fixtureGroupIDList; }
    QList<quint32> selectedPalettes() const { return m_paletteIDList; }
    QList<quint32> selectedFunctions() const { return m_functionIDList; }

    /** The dependency closure of $functionID in the source project, without touching the
     *  selection. $functions does not include $functionID itself. */
    void functionDependencies(quint32 functionID, QList<quint32> &fixtures, QList<quint32> &groups,
                              QList<quint32> &palettes, QList<quint32> &functions) const;

    /*********************************************************************
     * Import
     *********************************************************************/
    /** Copy the selection into the target Doc. Consumes the function selection. */
    void apply();

    /** Source id -> target id, filled by apply() */
    QMap<quint32, quint32> fixtureRemap() const { return m_fixtureIDRemap; }
    QMap<quint32, quint32> fixtureGroupRemap() const { return m_fixtureGroupIDRemap; }
    QMap<quint32, quint32> paletteRemap() const { return m_paletteIDRemap; }
    QMap<quint32, quint32> functionRemap() const { return m_functionIDRemap; }

    /** Source fixtures apply() created anew / could not create */
    QList<quint32> createdFixtures() const { return m_createdFixtures; }
    QList<quint32> skippedFixtures() const { return m_skippedFixtures; }

private:
    bool load(QXmlStreamReader &reader, const QString &workspacePath);
    bool loadXML(QXmlStreamReader &doc);
    void resetSelection();

    /** First free address with room for $channels channels, starting from $universe/$address */
    void getAvailableFixtureAddress(int channels, int &universe, int &address);

    void importFixtures();
    void importPalettes();

    /** Import $funcID, satisfying its function dependencies first */
    void importFunctionID(quint32 funcID);

    /** Add the dependencies of $fid to the given lists, recursively */
    void collectDependencies(quint32 fid, QList<quint32> &fixtures, QList<quint32> &groups,
                             QList<quint32> &palettes, QList<quint32> &functions) const;

private:
    Doc *m_doc;
    Doc *m_importDoc;

    QList<quint32> m_fixtureIDList;
    /** Linked items to import, as (fixture id, MonitorProperties sub id) */
    QList<QPair<quint32, quint32>> m_linkedItems;
    QList<quint32> m_fixtureGroupIDList;
    QList<quint32> m_paletteIDList;
    QList<quint32> m_functionIDList;

    QMap<quint32, quint32> m_fixtureIDRemap;
    QMap<quint32, quint32> m_fixtureGroupIDRemap;
    QMap<quint32, quint32> m_paletteIDRemap;
    QMap<quint32, quint32> m_functionIDRemap;

    QList<quint32> m_createdFixtures;
    QList<quint32> m_skippedFixtures;
};

/** @} */

#endif
