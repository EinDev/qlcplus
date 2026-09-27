/*
  Q Light Controller Plus
  importmanager.cpp

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
#include <QQmlContext>

#include "importmanager.h"
#include "projectimporter.h"
#include "treemodelitem.h"
#include "fixturemanager.h"

#include "qlcfixturedefcache.h"
#include "monitorproperties.h"
#include "rgbscriptscache.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "fixtureutils.h"
#include "qlcpalette.h"
#include "mediaassets.h"
#include "collection.h"
#include "audio.h"
#include "video.h"
#include "rgbmatrix.h"
#include "sequence.h"
#include "qlcfile.h"
#include "chaser.h"
#include "scriptwrapper.h"
#include "scene.h"
#include "efx.h"
#include "doc.h"
#include "app.h"

ImportManager::ImportManager(QQuickView *view, Doc *doc, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_doc(doc)
    , m_importer(new ProjectImporter(doc))
    , m_fixtureTree(nullptr)
    , m_fixtureTreeUpdating(false)
    , m_functionTree(nullptr)
    , m_functionTreeUpdating(false)
{
    m_view->rootContext()->setContextProperty("importManager", this);
}

ImportManager::~ImportManager()
{
    delete m_importer;

    if (m_functionTree != nullptr)
        m_functionTree->clear();
    if (m_fixtureTree != nullptr)
        m_fixtureTree->clear();

    delete m_functionTree;
    delete m_fixtureTree;
}

bool ImportManager::loadWorkspace(const QString &fileName)
{
    return m_importer->loadFile(fileName);
}

void ImportManager::apply()
{
    m_importer->apply();
}

void ImportManager::setChildrenChecked(TreeModel *tree, bool checked) const
{
    if (tree == nullptr)
        return;

    for (TreeModelItem *item : tree->items())
    {
        tree->setItemRoleData(item, checked, TreeModel::IsCheckedRole);

        if (item->hasChildren())
            setChildrenChecked(item->children(), checked);
    }
}

void ImportManager::updateFixtureItemID(const QVariantList &itemData, bool checked)
{
    // itemData must be "classRef" << "type" << "id" << "subid" << "chIdx" << "inGroup";
    if (itemData.count() != 6)
        return;

    int itemType = itemData.at(1).toInt();
    quint32 itemID = itemData.at(2).toUInt();

    if (itemType == App::FixtureDragItem)
    {
        quint32 fixtureID = FixtureUtils::itemFixtureID(itemID);
        quint16 headIndex = FixtureUtils::itemHeadIndex(itemID);
        quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

        if (checked)
        {
            m_importer->selectFixture(fixtureID, false);
            if (linkedIndex > 0)
                m_importer->selectLinkedItem(fixtureID, headIndex, linkedIndex);
        }
        else
        {
            if (linkedIndex > 0)
                m_importer->deselectLinkedItem(fixtureID, headIndex, linkedIndex);
            else
                m_importer->deselectFixture(fixtureID);
        }
    }
    else if (itemType == App::FixtureGroupDragItem)
    {
        // the group's fixtures are its children in the tree and get checked with it
        if (checked)
            m_importer->selectFixtureGroup(itemID);
        else
            m_importer->deselectFixtureGroup(itemID);
    }
}

void ImportManager::updateChildrenFixtureIDs(TreeModel *tree, bool checked)
{
    if (tree == nullptr)
        return;

    for (TreeModelItem *item : tree->items())
    {
        updateFixtureItemID(item->data(), checked);

        if (item->hasChildren())
            updateChildrenFixtureIDs(item->children(), checked);
    }
}

/*********************************************************************
 * Fixture tree
 *********************************************************************/

QVariant ImportManager::groupsTreeModel()
{
    if (m_fixtureTree == nullptr)
    {
        m_fixtureTree = new TreeModel(this);
        QQmlEngine::setObjectOwnership(m_fixtureTree, QQmlEngine::CppOwnership);
        QStringList treeColumns;
        treeColumns << "classRef" << "type" << "id" << "subid" << "chIdx" << "inGroup";
        m_fixtureTree->setColumnNames(treeColumns);
        m_fixtureTree->enableSorting(false);

        FixtureManager::updateGroupsTree(m_importer->sourceDoc(), m_fixtureTree, m_fixtureSearchFilter,
                                         FixtureManager::ShowCheckBoxes | FixtureManager::ShowHeads | FixtureManager::ShowLinked);

        connect(m_fixtureTree, SIGNAL(roleChanged(TreeModelItem*,int,const QVariant&)),
                this, SLOT(slotFixtureTreeDataChanged(TreeModelItem*,int,const QVariant&)));
    }

    return QVariant::fromValue(m_fixtureTree);
}

void ImportManager::slotFixtureTreeDataChanged(TreeModelItem *item, int role, const QVariant &value)
{
    if (role != TreeModel::IsCheckedRole)
        return;

    bool checked = value.toBool();

    if (m_fixtureTreeUpdating)
        return;

    m_fixtureTreeUpdating = true;

    qDebug() << "Fixture tree data changed" << value.toInt() << "data" << item->data() << "value" << value;

    if (item->hasChildren())
    {
        setChildrenChecked(item->children(), checked);
        updateChildrenFixtureIDs(item->children(), checked);
    }

    updateFixtureItemID(item->data(), checked);

    // when a linked fixture is manually checked,
    // visually update the base fixture in the tree
    QVariantList itemData = item->data();
    if (checked && itemData.count() == 6 &&
        itemData.at(1).toInt() == App::FixtureDragItem)
    {
        quint32 itemID = itemData.at(2).toUInt();
        if (FixtureUtils::itemLinkedIndex(itemID) > 0)
            checkFixtureTree(m_fixtureTree);
    }

    m_fixtureTreeUpdating = false;

    qDebug() << "Selected fixtures:" << m_importer->selectedFixtures().count();
}

QString ImportManager::fixtureSearchFilter() const
{
    return m_fixtureSearchFilter;
}

void ImportManager::setFixtureSearchFilter(QString searchFilter)
{
    if (m_fixtureSearchFilter == searchFilter)
        return;

    int currLen = m_fixtureSearchFilter.length();

    m_fixtureSearchFilter = searchFilter;

    if (searchFilter.length() >= SEARCH_MIN_CHARS ||
        (currLen >= SEARCH_MIN_CHARS && searchFilter.length() < SEARCH_MIN_CHARS))
    {
        FixtureManager::updateGroupsTree(m_importer->sourceDoc(), m_fixtureTree, m_fixtureSearchFilter,
                                         FixtureManager::ShowCheckBoxes | FixtureManager::ShowGroups);
        checkFixtureTree(m_fixtureTree);
        emit groupsTreeModelChanged();
    }

    emit fixtureSearchFilterChanged();
}

void ImportManager::checkFixtureTree(TreeModel *tree) const
{
    if (tree == nullptr)
        return;

    for (TreeModelItem *item : tree->items())
    {
        QVariantList itemData = item->data();

        // itemData must be "classRef" << "type" << "id" << "subid" << "chIdx" << "inGroup";
        if (itemData.count() == 6 && itemData.at(1).toInt() == App::FixtureDragItem)
        {
            quint32 itemID = itemData.at(2).toUInt();
            quint32 fixtureID = FixtureUtils::itemFixtureID(itemID);
            quint16 linkedIndex = FixtureUtils::itemLinkedIndex(itemID);

            if (m_importer->selectedFixtures().contains(fixtureID) && linkedIndex == 0)
                tree->setItemRoleData(item, true, TreeModel::IsCheckedRole);
        }

        if (item->hasChildren())
            checkFixtureTree(item->children());
    }
}

/*********************************************************************
 * Function tree
 *********************************************************************/

void ImportManager::updateFunctionsTree()
{
    m_functionTree->clear();

    for (Function *func : m_importer->sourceDoc()->functions())
    {
        // hidden helpers (a Sequence's bound scene, ...) come along as dependencies;
        // skip them without ending the walk
        if (func == nullptr || func->isVisible() == false)
            continue;

        bool expandAll = m_functionSearchFilter.length() >= SEARCH_MIN_CHARS;

        QQmlEngine::setObjectOwnership(func, QQmlEngine::CppOwnership);

        if (m_functionSearchFilter.length() < SEARCH_MIN_CHARS || func->name().toLower().contains(m_functionSearchFilter))
        {
            QVariantList params;
            params.append(QVariant::fromValue(func));
            QString fPath = func->path(true).replace("/", TreeModel::separator());
            m_functionTree->addItem(func->name(), params, fPath, expandAll ? TreeModel::Expanded : 0);
        }
    }

    checkFunctionTree(m_functionTree);
}

QVariant ImportManager::functionsTreeModel()
{
    if (m_functionTree == nullptr)
    {
        m_functionTree = new TreeModel(this);
        QQmlEngine::setObjectOwnership(m_functionTree, QQmlEngine::CppOwnership);
        QStringList treeColumns;
        treeColumns << "classRef";
        m_functionTree->setColumnNames(treeColumns);
        m_functionTree->enableSorting(false);
        m_functionTree->setCheckable(true);

        updateFunctionsTree();

        connect(m_functionTree, SIGNAL(roleChanged(TreeModelItem*,int,const QVariant&)),
                this, SLOT(slotFunctionTreeDataChanged(TreeModelItem*,int,const QVariant&)));
    }

    return QVariant::fromValue(m_functionTree);
}

void ImportManager::checkFunctionTree(TreeModel *tree) const
{
    if (tree == nullptr)
        return;

    for (TreeModelItem *item : tree->items())
    {
        if (item->data().count() == 0)
            continue;

        QVariant cRef = item->data().first();
        if (cRef.canConvert<Function *>())
        {
            Function *func = cRef.value<Function *>();
            if (func != nullptr)
            {
                if (m_importer->selectedFunctions().contains(func->id()))
                    tree->setItemRoleData(item, true, TreeModel::IsCheckedRole);
            }
        }
        if (item->hasChildren())
            checkFunctionTree(item->children());
    }
}

void ImportManager::slotFunctionTreeDataChanged(TreeModelItem *item, int role, const QVariant &value)
{
    if (role != TreeModel::IsCheckedRole)
        return;

    bool checked = value.toBool();

    if (m_functionTreeUpdating)
        return;

    m_functionTreeUpdating = true;

    qDebug() << "Function tree data changed" << value.toInt() << "data" << item->data();

    if (item->hasChildren())
        setChildrenChecked(item->children(), checked);

    QVariantList itemData = item->data();
    if (itemData.isEmpty())
        return;

    QVariant cRef = item->data().first();
    if (cRef.canConvert<Function *>())
    {
        Function *func = cRef.value<Function *>();
        if (checked)
        {
            if (m_importer->selectedFunctions().contains(func->id()) == false)
            {
                m_importer->selectFunction(func->id());
                checkFunctionTree(m_functionTree);
                checkFixtureTree(m_fixtureTree);
            }
        }
        else
        {
            m_importer->deselectFunction(func->id());
        }
    }

    m_functionTreeUpdating = false;

    qDebug() << "Selected functions:" << m_importer->selectedFunctions().count();
}

QString ImportManager::functionSearchFilter() const
{
    return m_functionSearchFilter;
}

void ImportManager::setFunctionSearchFilter(QString searchFilter)
{
    if (m_functionSearchFilter == searchFilter)
        return;

    int currLen = m_functionSearchFilter.length();

    m_functionSearchFilter = searchFilter;

    if (searchFilter.length() >= SEARCH_MIN_CHARS ||
        (currLen >= SEARCH_MIN_CHARS && searchFilter.length() < SEARCH_MIN_CHARS))
    {
        updateFunctionsTree();
        emit functionsTreeModelChanged();
    }

    emit functionSearchFilterChanged();
}
