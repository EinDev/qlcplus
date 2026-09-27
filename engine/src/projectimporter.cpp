/*
  Q Light Controller Plus
  projectimporter.cpp

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

#include <QXmlStreamReader>
#include <QFileInfo>
#include <QDebug>
#include <QUrl>
#include <algorithm>

#include "projectimporter.h"

#include "qlcfixturedefcache.h"
#include "monitorproperties.h"
#include "rgbscriptscache.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "fixturegroup.h"
#include "mediaassets.h"
#include "qlcpalette.h"
#include "collection.h"
#include "chaserstep.h"
#include "efxfixture.h"
#include "rgbmatrix.h"
#include "sequence.h"
#include "fixture.h"
#include "qlcfile.h"
#include "script.h"
#include "chaser.h"
#include "scene.h"
#include "audio.h"
#include "video.h"
#include "efx.h"
#include "doc.h"

#define KXMLQLCImportWorkspace QStringLiteral("Workspace")

ProjectImporter::ProjectImporter(Doc *targetDoc)
    : m_doc(targetDoc)
{
    m_importDoc = new Doc(nullptr);

    // share the target Doc's fixture definitions: a fixture is imported by
    // manufacturer/model, so both sides must resolve them the same way
    delete m_importDoc->fixtureDefCache();
    m_importDoc->setFixtureDefinitionCache(m_doc->fixtureDefCache());

    m_importDoc->rgbScriptsCache()->load(RGBScriptsCache::systemScriptsDirectory());
    m_importDoc->rgbScriptsCache()->load(RGBScriptsCache::userScriptsDirectory());
}

ProjectImporter::~ProjectImporter()
{
    // the fixture cache belongs to the target Doc
    m_importDoc->setFixtureDefinitionCache(nullptr);
    delete m_importDoc;
}

Doc *ProjectImporter::sourceDoc() const
{
    return m_importDoc;
}

bool ProjectImporter::loadFile(const QString &fileName)
{
    QString localFilename = fileName;
    if (localFilename.startsWith("file:"))
        localFilename = QUrl(fileName).toLocalFile();

    if (localFilename.isEmpty())
        return false;

    QXmlStreamReader *reader = QLCFile::getXMLReader(localFilename);
    if (reader == nullptr || reader->device() == nullptr || reader->hasError())
    {
        qWarning() << Q_FUNC_INFO << "Unable to read from" << localFilename;
        if (reader != nullptr)
            QLCFile::releaseXMLReader(reader);
        return false;
    }

    bool retval = load(*reader, QFileInfo(localFilename).absolutePath());
    QLCFile::releaseXMLReader(reader);
    return retval;
}

bool ProjectImporter::loadData(const QByteArray &xml, const QString &workspacePath)
{
    QXmlStreamReader reader(xml);
    return load(reader, workspacePath);
}

bool ProjectImporter::load(QXmlStreamReader &reader, const QString &workspacePath)
{
    while (!reader.atEnd())
    {
        if (reader.readNext() == QXmlStreamReader::DTD)
            break;
    }
    if (reader.hasError())
        return false;

    resetSelection();
    m_importDoc->clearContents();

    /* Set the workspace path before loading the new XML. In this way local files
       can be loaded even if the workspace file has been moved */
    m_importDoc->setWorkspacePath(workspacePath);

    bool retval = false;
    if (reader.dtdName() == KXMLQLCImportWorkspace)
        retval = loadXML(reader);
    else
        qWarning() << Q_FUNC_INFO << "not a workspace file";

    // if not present, set initial monitor properties
    MonitorProperties *monProps = m_importDoc->monitorProperties();
    for (Fixture *fixture : m_importDoc->fixtures())
    {
        if (monProps->containsFixture(fixture->id()) == false)
            monProps->setFixtureFlags(fixture->id(), 0, 0, 0);
    }

    return retval;
}

bool ProjectImporter::loadXML(QXmlStreamReader &doc)
{
    if (doc.readNextStartElement() == false)
        return false;

    if (doc.name() != KXMLQLCImportWorkspace)
    {
        qWarning() << Q_FUNC_INFO << "Workspace node not found";
        return false;
    }

    while (doc.readNextStartElement())
    {
        if (doc.name() == KXMLQLCEngine)
            m_importDoc->loadXML(doc, false);
        else
            doc.skipCurrentElement();
    }

    return true;
}

void ProjectImporter::resetSelection()
{
    m_fixtureIDList.clear();
    m_linkedItems.clear();
    m_fixtureGroupIDList.clear();
    m_paletteIDList.clear();
    m_functionIDList.clear();
    m_fixtureIDRemap.clear();
    m_fixtureGroupIDRemap.clear();
    m_paletteIDRemap.clear();
    m_functionIDRemap.clear();
    m_createdFixtures.clear();
    m_skippedFixtures.clear();
}

/*********************************************************************
 * Selection
 *********************************************************************/

void ProjectImporter::selectFixture(quint32 fixtureID, bool withLinked)
{
    if (m_importDoc->fixture(fixtureID) == nullptr)
        return;

    if (m_fixtureIDList.contains(fixtureID) == false)
        m_fixtureIDList.append(fixtureID);

    if (withLinked == false)
        return;

    MonitorProperties *monProps = m_importDoc->monitorProperties();
    for (quint32 subID : monProps->fixtureIDList(fixtureID))
    {
        if (subID != 0 && monProps->fixtureLinkedIndex(subID) > 0)
        {
            QPair<quint32, quint32> item(fixtureID, subID);
            if (m_linkedItems.contains(item) == false)
                m_linkedItems.append(item);
        }
    }
}

void ProjectImporter::deselectFixture(quint32 fixtureID)
{
    m_fixtureIDList.removeOne(fixtureID);
}

void ProjectImporter::selectLinkedItem(quint32 fixtureID, quint16 headIndex, quint16 linkedIndex)
{
    QPair<quint32, quint32> item(fixtureID, m_importDoc->monitorProperties()->fixtureSubID(headIndex, linkedIndex));
    if (m_linkedItems.contains(item) == false)
        m_linkedItems.append(item);
}

void ProjectImporter::deselectLinkedItem(quint32 fixtureID, quint16 headIndex, quint16 linkedIndex)
{
    m_linkedItems.removeOne(QPair<quint32, quint32>(fixtureID,
                            m_importDoc->monitorProperties()->fixtureSubID(headIndex, linkedIndex)));
}

void ProjectImporter::selectFixtureGroup(quint32 groupID)
{
    FixtureGroup *group = m_importDoc->fixtureGroup(groupID);
    if (group == nullptr)
        return;

    if (m_fixtureGroupIDList.contains(groupID) == false)
        m_fixtureGroupIDList.append(groupID);

    for (quint32 id : group->fixtureList())
    {
        if (m_fixtureIDList.contains(id) == false)
            m_fixtureIDList.append(id);
    }
}

void ProjectImporter::deselectFixtureGroup(quint32 groupID)
{
    m_fixtureGroupIDList.removeOne(groupID);
}

void ProjectImporter::selectFunction(quint32 functionID)
{
    if (m_importDoc->function(functionID) == nullptr)
        return;

    QList<quint32> fixtures, groups, palettes, functions;
    functionDependencies(functionID, fixtures, groups, palettes, functions);
    functions.prepend(functionID);

    for (quint32 id : groups)
        selectFixtureGroup(id);
    for (quint32 id : fixtures)
        if (m_fixtureIDList.contains(id) == false)
            m_fixtureIDList.append(id);
    for (quint32 id : palettes)
        if (m_paletteIDList.contains(id) == false)
            m_paletteIDList.append(id);
    for (quint32 id : functions)
        if (m_functionIDList.contains(id) == false)
            m_functionIDList.append(id);
}

void ProjectImporter::deselectFunction(quint32 functionID)
{
    m_functionIDList.removeOne(functionID);
}

void ProjectImporter::functionDependencies(quint32 functionID, QList<quint32> &fixtures, QList<quint32> &groups,
                                           QList<quint32> &palettes, QList<quint32> &functions) const
{
    collectDependencies(functionID, fixtures, groups, palettes, functions);
    functions.removeAll(functionID);
}

void ProjectImporter::collectDependencies(quint32 fid, QList<quint32> &fixtures, QList<quint32> &groups,
                                          QList<quint32> &palettes, QList<quint32> &functions) const
{
    Function *func = m_importDoc->function(fid);
    if (func == nullptr)
        return;

    QList<quint32> funcList;
    QList<quint32> fxList;
    QList<quint32> fxGroupList;
    QList<quint32> paletteList;

    switch (func->type())
    {
        // a Scene can reference fixtures, fixture groups and palettes
        case Function::SceneType:
        {
            Scene *scene = qobject_cast<Scene *>(func);
            fxList = scene->components();
            fxGroupList = scene->fixtureGroups();
            paletteList = scene->palettes();
        }
        break;

        // EFX needs only fixtures
        case Function::EFXType:
            fxList = func->components();
        break;

        // RGB Matrix requires a fixture group
        case Function::RGBMatrixType:
        {
            RGBMatrix *rgbm = qobject_cast<RGBMatrix *>(func);
            fxList = rgbm->components();
            fxGroupList.append(rgbm->fixtureGroup());
        }
        break;

        // these return only Function IDs (a Sequence: its bound Scene)
        case Function::ChaserType:
        case Function::SequenceType:
        case Function::CollectionType:
            funcList = func->components();
        break;

        // Scripts are a mix: they can control Fixtures AND Functions
        case Function::ScriptType:
        {
            Script *script = qobject_cast<Script *>(func);
            funcList = script->functionList();
            fxList = script->fixtureList();
        }
        break;

        // Audio/Video go here. No dependency
        default:
        break;
    }

    for (quint32 groupID : fxGroupList)
    {
        FixtureGroup *group = m_importDoc->fixtureGroup(groupID);
        if (groupID == FixtureGroup::invalidId() || group == nullptr || groups.contains(groupID))
            continue;

        groups.append(groupID);
        for (quint32 id : group->fixtureList())
            if (fixtures.contains(id) == false)
                fixtures.append(id);
    }

    for (quint32 paletteID : paletteList)
    {
        if (paletteID != QLCPalette::invalidId() && palettes.contains(paletteID) == false)
            palettes.append(paletteID);
    }

    for (quint32 fixtureID : fxList)
    {
        if (m_importDoc->fixture(fixtureID) != nullptr && fixtures.contains(fixtureID) == false)
            fixtures.append(fixtureID);
    }

    for (quint32 functionID : funcList)
    {
        if (m_importDoc->function(functionID) != nullptr && functions.contains(functionID) == false)
        {
            functions.append(functionID);
            collectDependencies(functionID, fixtures, groups, palettes, functions);
        }
    }
}

/*********************************************************************
 * Import
 *********************************************************************/

void ProjectImporter::apply()
{
    // try to preserve the Fixture order
    std::sort(m_fixtureIDList.begin(), m_fixtureIDList.end());
    importFixtures();

    importPalettes();

    /* Functions need to be imported respecting their
     * dependency order. Otherwise ID remapping will be
     * messed up */
    while (!m_functionIDList.isEmpty())
        importFunctionID(m_functionIDList.first());
}

void ProjectImporter::getAvailableFixtureAddress(int channels, int &universe, int &address)
{
    int freeCounter = 0;
    quint32 absAddress = (universe << 9) + address;

    while (1)
    {
        // a fixture never straddles two universes: restart the count at each boundary
        if ((absAddress & 0x1FF) == 0)
            freeCounter = 0;

        if (m_doc->fixtureForAddress(absAddress) == Fixture::invalidId())
            freeCounter++;
        else
            freeCounter = 0;

        if (freeCounter == channels)
        {
            universe = (absAddress >> 9);
            address = absAddress - (universe * 512) - (channels - 1);
            return;
        }

        absAddress++;
    }
}

void ProjectImporter::importFixtures()
{
    MonitorProperties *importMonProps = m_importDoc->monitorProperties();
    MonitorProperties *monProps = m_doc->monitorProperties();

    /* ************************ Import fixtures ************************ */
    for (quint32 importID : m_fixtureIDList)
    {
        bool matchFound = false;
        Fixture *importFixture = m_importDoc->fixture(importID);
        if (importFixture == nullptr)
            continue;

        /* Check if a Fixture with the same name already exists in m_doc.
         * If it does, check also if the ID needs to be remapped */
        for (Fixture *docFixture : m_doc->fixtures())
        {
            if (docFixture->name() == importFixture->name())
            {
                m_fixtureIDRemap[importID] = docFixture->id();
                matchFound = true;
                break;
            }
        }

        /* if no match is found, it means a new Fixture needs to be created
         * in m_doc, which implies finding an available address and ID remapping */
        if (matchFound)
            continue;

        // Attempt to preserve original universe/address.
        // Will be checked later if available
        int uniIdx = importFixture->universe();
        int address = importFixture->address();

        QLCFixtureDef *importDef = importFixture->fixtureDef();
        QLCFixtureMode *importMode = importFixture->fixtureMode();
        QLCFixtureDef *fxiDef = importDef == nullptr ? nullptr :
                                m_doc->fixtureDefCache()->fixtureDef(importDef->manufacturer(), importDef->model());
        QLCFixtureMode *fxiMode = nullptr;

        if (fxiDef != nullptr && importMode != nullptr)
            fxiMode = fxiDef->mode(importMode->name());

        Fixture *fxi = new Fixture(m_doc);
        fxi->setName(importFixture->name());

        getAvailableFixtureAddress(importFixture->channels(), uniIdx, address);
        fxi->setUniverse(uniIdx);
        fxi->setAddress(address);

        if (fxiDef == nullptr && fxiMode == nullptr)
        {
            if (importDef != nullptr && importDef->model() == "Generic")
            {
                fxiDef = fxi->genericDimmerDef(importFixture->channels());
                fxiMode = fxi->genericDimmerMode(fxiDef, importFixture->channels());
            }
            else
            {
                qWarning() << "Import: no definition for fixture" << importFixture->name();
                m_skippedFixtures.append(importID);
                delete fxi;
                continue;
            }
        }

        fxi->setFixtureDefinition(fxiDef, fxiMode);

        if (m_doc->addFixture(fxi) == true)
        {
            m_fixtureIDRemap[importID] = fxi->id();
            m_createdFixtures.append(importID);
        }
        else
        {
            qWarning() << "ERROR: Failed to add fixture" << importFixture->name()
                       << "to universe" << uniIdx << "@address" << address;
            m_skippedFixtures.append(importID);
            delete fxi;
        }
    }

    /* ******************** Import linked fixtures ********************* */
    for (const QPair<quint32, quint32> &item : m_linkedItems)
    {
        quint32 fixtureID = item.first;
        quint16 headIndex = importMonProps->fixtureHeadIndex(item.second);
        quint16 linkedIndex = importMonProps->fixtureLinkedIndex(item.second);

        // if no original fixture was imported, skip linked
        if (m_fixtureIDList.contains(fixtureID) == false || m_fixtureIDRemap.contains(fixtureID) == false)
            continue;

        QString name = importMonProps->fixtureName(fixtureID, headIndex, linkedIndex);
        monProps->setFixtureName(m_fixtureIDRemap[fixtureID], headIndex, linkedIndex, name);
    }

    /* ********************* Import fixture groups ********************* */
    for (quint32 groupID : m_fixtureGroupIDList)
    {
        bool matchFound = false;
        FixtureGroup *importGroup = m_importDoc->fixtureGroup(groupID);
        if (importGroup == nullptr)
            continue;

        for (FixtureGroup *docGroup : m_doc->fixtureGroups())
        {
            if (docGroup->name() == importGroup->name())
            {
                m_fixtureGroupIDRemap[groupID] = docGroup->id();
                matchFound = true;
                break;
            }
        }

        /* if no match is found, it means a new FixtureGroup needs to be created
         * in m_doc, which implies ID remapping */
        if (matchFound)
            continue;

        FixtureGroup *newGroup = new FixtureGroup(m_doc);
        newGroup->setName(importGroup->name());
        newGroup->setSize(importGroup->size());

        QMap<QLCPoint, GroupHead> headsMap = importGroup->headsMap();
        QMap<QLCPoint, GroupHead>::const_iterator i = headsMap.constBegin();
        while (i != headsMap.constEnd())
        {
            QLCPoint p = i.key();
            GroupHead head = i.value();

            if (m_fixtureIDRemap.contains(head.fxi))
            {
                head.fxi = m_fixtureIDRemap[head.fxi];
                newGroup->assignHead(p, head);
            }

            ++i;
        }

        if (m_doc->addFixtureGroup(newGroup) == true)
        {
            m_fixtureGroupIDRemap[groupID] = newGroup->id();
        }
        else
        {
            qWarning() << "ERROR: Failed to add fixture group" << newGroup->name();
            delete newGroup;
        }
    }
}

void ProjectImporter::importPalettes()
{
    for (quint32 paletteID : m_paletteIDList)
    {
        QLCPalette *importPalette = m_importDoc->palette(paletteID);
        if (importPalette == nullptr)
            continue;

        bool matchFound = false;

        /* Check if a Palette with the same name already exists in m_doc.
         * If it does, check also if the ID needs to be remapped */
        for (QLCPalette *docPalette : m_doc->palettes())
        {
            if (docPalette->name() == importPalette->name())
            {
                m_paletteIDRemap[paletteID] = docPalette->id();
                matchFound = true;
                break;
            }
        }

        if (matchFound)
            continue;

        QLCPalette *palette = new QLCPalette(importPalette->type());
        palette->setName(importPalette->name());

        palette->setValues(importPalette->values());
        palette->setFanningType(importPalette->fanningType());
        palette->setFanningLayout(importPalette->fanningLayout());
        palette->setFanningAmount(importPalette->fanningAmount());
        palette->setFanningValue(importPalette->fanningValue());

        if (m_doc->addPalette(palette) == true)
        {
            m_paletteIDRemap[paletteID] = palette->id();
        }
        else
        {
            qWarning() << "ERROR: Failed to add Palette" << palette->name();
            delete palette;
        }
    }
}

void ProjectImporter::importFunctionID(quint32 funcID)
{
    Function *importFunction = m_importDoc->function(funcID);
    if (importFunction == nullptr)
    {
        m_functionIDList.removeOne(funcID);
        return;
    }

    QList<quint32> funcList;

    // 1. Get a list of Function ID upon importFunction depends on
    switch (importFunction->type())
    {
        // these will return only Function IDs
        case Function::ChaserType:
        case Function::SequenceType:
        case Function::CollectionType:
            funcList = importFunction->components();
        break;

        // Scripts are a mix: they can control Fixtures AND Functions
        case Function::ScriptType:
        {
            Script *script = qobject_cast<Script *>(importFunction);
            funcList = script->functionList();
        }
        break;
        default:
        break;
    }

    // 2. import the dependecies first, if any. Removed from the list before
    //    recursing, so a function reachable twice is still imported once
    m_functionIDList.removeOne(funcID);
    for (quint32 depID : funcList)
    {
        if (m_functionIDList.contains(depID))
            importFunctionID(depID);
    }

    // 3. Finally create a copy of the original Function. This will always create a new ID
    Function *docFunction = importFunction->createCopy(m_doc, true);
    if (docFunction == nullptr)
        return;
    m_functionIDRemap[funcID] = docFunction->id();

    // 4. Check Fixture/Function remapping depending on the Function type
    switch (docFunction->type())
    {
        case Function::SceneType:
        {
            Scene *scene = qobject_cast<Scene *>(docFunction);
            // create a copy of the existing components
            QList<SceneValue> sceneValues = scene->values();
            QList<quint32> fixtureGroupList = scene->fixtureGroups();
            QList<quint32> paletteList = scene->palettes();

            // point of no return. Delete everything
            scene->clear();

            // add referenced fixture groups
            for (quint32 groupID : fixtureGroupList)
            {
                if (m_fixtureGroupIDRemap.contains(groupID))
                    scene->addFixtureGroup(m_fixtureGroupIDRemap[groupID]);
            }

            // add referenced palettes
            for (quint32 paletteID : paletteList)
            {
                if (m_paletteIDRemap.contains(paletteID))
                    scene->addPalette(m_paletteIDRemap[paletteID]);
            }

            // remap values against existing/remapped fixtures
            for (SceneValue scv : sceneValues)
            {
                // add a value only if it is present in the remapping map,
                // otherwise it means the Fixture disappeared
                if (m_fixtureIDRemap.contains(scv.fxi))
                {
                    scv.fxi = m_fixtureIDRemap[scv.fxi];
                    scene->setValue(scv);
                }
            }
        }
        break;
        case Function::CollectionType:
        {
            Collection *collection = qobject_cast<Collection *>(docFunction);
            // create a copy of the existing function IDs
            QList<quint32> members = collection->functions();

            // point of no return. Empty the Collection
            for (quint32 id : members)
                collection->removeFunction(id);

            // Add only the functions that have been imported,
            // with their remapped IDs
            for (quint32 id : members)
            {
                if (m_functionIDRemap.contains(id))
                    collection->addFunction(m_functionIDRemap[id]);
            }
        }
        break;
        case Function::ChaserType:
        {
            Chaser *chaser = qobject_cast<Chaser *>(docFunction);
            QList<int> removeList;

            for (int i = 0; i < chaser->stepsCount(); i++)
            {
                ChaserStep *step = chaser->stepAt(i);
                if (m_functionIDRemap.contains(step->fid))
                {
                    step->fid = m_functionIDRemap[step->fid];
                }
                else
                {
                    /* this might mean:
                     * - same ID (nothing to do)
                     * - missing ID (add step index to remove list)
                     */
                    if (m_doc->function(step->fid) == nullptr)
                        removeList.append(i);
                }
            }

            for (int i = removeList.count() - 1; i >= 0; i--)
                chaser->removeStep(removeList.at(i));
        }
        break;
        case Function::SequenceType:
        {
            Sequence *sequence = qobject_cast<Sequence *>(docFunction);
            quint32 boundSceneID = sequence->boundSceneID();

            if (boundSceneID != Function::invalidId() &&
                m_functionIDRemap.contains(boundSceneID))
                    sequence->setBoundSceneID(m_functionIDRemap[boundSceneID]);
        }
        break;
        case Function::EFXType:
        {
            // EFX heads reference FIXTURES (ImportManager used to look them up in the
            // function remap, so imported EFX kept pointing at the source ids)
            EFX *efx = qobject_cast<EFX *>(docFunction);
            for (EFXFixture *efxFixture : efx->fixtures())
            {
                GroupHead head(efxFixture->head());

                if (m_fixtureIDRemap.contains(head.fxi))
                {
                    head.fxi = m_fixtureIDRemap[head.fxi];
                    efxFixture->setHead(head);
                }
            }
        }
        break;
        case Function::RGBMatrixType:
        {
            RGBMatrix *rgbm = qobject_cast<RGBMatrix *>(docFunction);
            if (rgbm->fixtureGroup() == FixtureGroup::invalidId())
                break;

            if (m_fixtureGroupIDRemap.contains(rgbm->fixtureGroup()))
                rgbm->setFixtureGroup(m_fixtureGroupIDRemap[rgbm->fixtureGroup()]);
        }
        break;
        case Function::AudioType:
        case Function::VideoType:
        {
            // createCopy() left the source as the absolute path resolved
            // against the imported project (load() set that workspace path).
            // Copy it into THIS project's media store and repoint the copy
            // without the full setters' side effects: Audio::copyFrom()
            // already restored the BPM analysis, and relinkSource() keeps it,
            // the name and the decoder as they are. Streams stay streams. A
            // big file comes back as the source path and is relinked once the
            // background copy lands.
            QString source;
            if (docFunction->type() == Function::AudioType)
                source = static_cast<Audio *>(docFunction)->getSourceFileName();
            else
                source = static_cast<Video *>(docFunction)->sourceUrl();

            if (source.isEmpty() == false && source.contains("://") == false)
            {
                QString stored = m_doc->assets()->importOrKeep(source);
                if (stored != source)
                {
                    if (docFunction->type() == Function::AudioType)
                        static_cast<Audio *>(docFunction)->relinkSource(stored);
                    else
                        static_cast<Video *>(docFunction)->relinkSource(stored);
                }
            }
        }
        break;
        default:
        break;
    }
}
