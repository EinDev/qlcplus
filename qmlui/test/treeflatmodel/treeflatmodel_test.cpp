/*
  Q Light Controller - Unit test
  treeflatmodel_test.cpp

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

#include <QtTest>

#include "treeflatmodel_test.h"
#include "treeflatmodel.h"
#include "treemodel.h"

namespace
{
    /** Build a small tree: a plain top-level leaf ("Leaf A"), then a folder
     *  ("Folder") with two leaf children ("Child 1", "Child 2"), initially
     *  collapsed. Mirrors the shape every real consumer (Fixture Groups,
     *  Functions, ...) uses: some top-level items are leaves, some have
     *  children that only become visible once expanded. */
    void buildSampleTree(TreeModel &tree)
    {
        tree.setColumnNames(QStringList() << "id");
        tree.enableSorting(false);

        tree.addItem("Leaf A", QVariantList() << 1);
        tree.addItem("Child 1", QVariantList() << 10, "Folder");
        tree.addItem("Child 2", QVariantList() << 11, "Folder");
    }
}

void TreeFlatModel_Test::flattenTopLevelOnly()
{
    TreeModel tree;
    buildSampleTree(tree);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);

    // Folder starts collapsed - only the 2 top-level rows should be visible
    QCOMPARE(flat.rowCount(), 2);
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::LabelRole).toString(), QString("Leaf A"));
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::DepthRole).toInt(), 0);
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::LabelRole).toString(), QString("Folder"));
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::DepthRole).toInt(), 0);
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::HasChildrenRole).toBool(), true);
}

void TreeFlatModel_Test::expandRevealsChildrenIncrementally()
{
    TreeModel tree;
    buildSampleTree(tree);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    QCOMPARE(flat.rowCount(), 2);

    // Expanding via the *source* TreeModel directly (not through the flat model)
    // must still be picked up, since TreeModel::roleChanged bubbles regardless of
    // which "door" the change came through.
    tree.setItemRoleData("Folder", true, TreeModel::IsExpandedRole);

    QCOMPARE(flat.rowCount(), 4);
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::LabelRole).toString(), QString("Folder"));
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::LabelRole).toString(), QString("Child 1"));
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::DepthRole).toInt(), 1);
    QCOMPARE(flat.data(flat.index(3), TreeFlatModel::LabelRole).toString(), QString("Child 2"));
    QCOMPARE(flat.data(flat.index(3), TreeFlatModel::DepthRole).toInt(), 1);

    tree.setItemRoleData("Folder", false, TreeModel::IsExpandedRole);
    QCOMPARE(flat.rowCount(), 2);
}

void TreeFlatModel_Test::setDataForwardsToOwnerAndTogglesExpansion()
{
    TreeModel tree;
    buildSampleTree(tree);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);

    // This is exactly what QML's "model.isExpanded = true" does on a delegate.
    bool ok = flat.setData(flat.index(1), true, TreeFlatModel::IsExpandedRole);
    QCOMPARE(ok, true);

    // The write must land on the real, underlying TreeModel...
    QCOMPARE(tree.data(tree.index(1), TreeModel::IsExpandedRole).toBool(), true);
    // ...and the flat model must have picked up its own bubbled roleChanged and
    // incrementally inserted the now-visible children.
    QCOMPARE(flat.rowCount(), 4);
}

void TreeFlatModel_Test::customRoleRoundTrip()
{
    TreeModel tree;
    buildSampleTree(tree);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);

    // "id" is one of the custom columns TreeFlatModel resolves against whatever
    // columns the current source tree actually has (see rebuild()).
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::IdRole).toInt(), 1);

    // IsSelectedRole is a fixed role forwarded straight through; round-trip it.
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::IsSelectedRole).toBool(), false);
    flat.setData(flat.index(0), 1, TreeFlatModel::IsSelectedRole);
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::IsSelectedRole).toBool(), true);
}

void TreeFlatModel_Test::clearInvalidatesRowsImmediately()
{
    TreeModel tree;
    buildSampleTree(tree);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    QCOMPARE(flat.rowCount(), 2);

    // Regression test: TreeModel::clear() (what e.g. FunctionManager::
    // updateFunctionsTree() calls on every structural change) deletes every
    // TreeModelItem immediately. Before this was fixed, TreeFlatModel only
    // learned about such changes via a screen's own higher-level "tree changed"
    // signal, leaving a window where its cached rows pointed at already-freed
    // TreeModelItems - reading them there returned garbage (observed in the wild
    // as blank labels after reopening the Functions panel). TreeFlatModel now
    // connects directly to the source model's own rowsAboutToBeRemoved/
    // modelAboutToBeReset signals and drops every row synchronously and
    // immediately, before clear() gets to delete anything - so this must already
    // read 0, with no explicit rebuild() call in between.
    tree.clear();
    QCOMPARE(flat.rowCount(), 0);
}

void TreeFlatModel_Test::rebuildAfterClearReflectsNewData()
{
    TreeModel tree;
    buildSampleTree(tree);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);

    tree.clear();
    QCOMPARE(flat.rowCount(), 0);

    tree.addItem("New Scene 0", QVariantList() << 42);
    flat.rebuild();

    QCOMPARE(flat.rowCount(), 1);
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::LabelRole).toString(), QString("New Scene 0"));
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::IdRole).toInt(), 42);
}

void TreeFlatModel_Test::incrementalAddItemIsReflectedWithoutExplicitRebuild()
{
    TreeModel tree;
    buildSampleTree(tree);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    QCOMPARE(flat.rowCount(), 2);

    // This is exactly what FunctionManager::addFunctionTreeItem() does when a single new
    // function is created while the panel is open: TreeModel::addItem() directly, with no
    // "list changed"-style signal emitted at all. TreeFlatModel must pick this up on its
    // own via the source model's own rowsInserted, without any explicit rebuild() call.
    tree.addItem("Leaf B", QVariantList() << 2);

    QCOMPARE(flat.rowCount(), 3);
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::LabelRole).toString(), QString("Leaf B"));
}

void TreeFlatModel_Test::incrementalRemoveItemIsReflectedWithoutExplicitRebuild()
{
    TreeModel tree;
    buildSampleTree(tree);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    QCOMPARE(flat.rowCount(), 2);

    // Mirrors FunctionManager::deleteFunction(), which calls TreeModel::removeItem()
    // directly with no accompanying signal. Without reacting to the source's own
    // rowsRemoved, the earlier rowsAboutToBeRemoved-triggered safety-empty (see
    // clearInvalidatesRowsImmediately) would leave the flat model permanently empty
    // instead of settling back to the correct, smaller row count.
    tree.removeItem("Leaf A");

    QCOMPARE(flat.rowCount(), 1);
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::LabelRole).toString(), QString("Folder"));
}

QTEST_APPLESS_MAIN(TreeFlatModel_Test)
