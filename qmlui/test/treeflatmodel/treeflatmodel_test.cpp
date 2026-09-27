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
#include <QCoreApplication>

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

    /** Flush the coalesced, queued rebuild() that TreeFlatModel::
     *  slotSourceStructureChanged() posts for every signal of a burst after the
     *  first (see commit 2c4eeeb8b). Needs a live QCoreApplication - hence
     *  QTEST_GUILESS_MAIN below - since posted events are never delivered
     *  without one. */
    void flushDeferredRebuild()
    {
        QCoreApplication::sendPostedEvents();
    }

    struct VisibleRow
    {
        QString label;
        int depth;
        bool hasChildren;
    };

    /** The rows a flattened view of $tree must show, computed independently of
     *  TreeFlatModel by walking the tree: every item, plus - for expanded ones -
     *  their visible descendants. */
    void collectVisibleRows(const TreeModel *tree, int depth, QList<VisibleRow> &out)
    {
        for (TreeModelItem *item : tree->items())
        {
            out.append({ item->label(), depth,
                         item->hasChildren() || (item->flags() & TreeModel::EmptyNode) });
            if ((item->flags() & TreeModel::Expanded) && item->hasChildren())
                collectVisibleRows(item->children(), depth + 1, out);
        }
    }

    /** Read EVERY role of EVERY row through data(), the way a ListView delegate
     *  does, so any FlatRow still pointing at a freed TreeModelItem or a freed
     *  TreeModel `owner` gets dereferenced right here, in the test, rather than
     *  later in the real UI. */
    void readEveryRoleOfEveryRow(const TreeFlatModel &flat)
    {
        const QMetaEnum roles = QMetaEnum::fromType<TreeFlatModel::FlatRoles>();
        for (int row = 0; row < flat.rowCount(); row++)
            for (int i = 0; i < roles.keyCount(); i++)
                (void)flat.data(flat.index(row), roles.value(i));
    }

    /** Every row still shown must be a live one: a row whose item its owner no
     *  longer knows about reads back as an empty label. */
    void verifyEveryRowIsLive(const TreeFlatModel &flat)
    {
        readEveryRoleOfEveryRow(flat);
        for (int row = 0; row < flat.rowCount(); row++)
            QVERIFY2(!flat.data(flat.index(row), TreeFlatModel::LabelRole).toString().isEmpty(),
                     qPrintable(QString("row %1 references an item its owner no longer holds").arg(row)));
    }

    /** Assert the flat model shows exactly what the tree currently contains. */
    void verifyFlatMatchesTree(const TreeFlatModel &flat, const TreeModel &tree)
    {
        QList<VisibleRow> expected;
        collectVisibleRows(&tree, 0, expected);

        QCOMPARE(flat.rowCount(), expected.count());
        for (int i = 0; i < expected.count(); i++)
        {
            QCOMPARE(flat.data(flat.index(i), TreeFlatModel::LabelRole).toString(), expected.at(i).label);
            QCOMPARE(flat.data(flat.index(i), TreeFlatModel::DepthRole).toInt(), expected.at(i).depth);
            QCOMPARE(flat.data(flat.index(i), TreeFlatModel::HasChildrenRole).toBool(), expected.at(i).hasChildren);
        }
        readEveryRoleOfEveryRow(flat);
    }

    /** A tree shaped like FunctionManager::m_functionTree: "classRef"/"type"
     *  columns, alphabetic sorting on (folders sort before leaves). */
    void setupFunctionLikeTree(TreeModel &tree)
    {
        tree.setColumnNames(QStringList() << "classRef" << "type");
        tree.enableSorting(true);
    }

    QVariantList functionParams(int id)
    {
        return QVariantList() << id << 1;
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

// Mirrors, call for call, what FunctionManager does when the only function inside
// a folder is deleted and the folder then goes away: FunctionManager::
// deleteFunction() -> TreeModel::removeItem("Snapshots`x") (a NESTED removal: the
// root tree only forwards it to the folder's child TreeModel, so the root's own
// rowsAboutToBeRemoved never fires), then FunctionManager::deleteFolder() ->
// TreeModel::removeItem("Snapshots") on the root. The folder is expanded, so
// before the delete the flat model holds a row for "x" whose `owner` is the
// folder's child TreeModel.
void TreeFlatModel_Test::deletingLastFunctionOfExpandedFolderMirrorsFunctionManager()
{
    TreeModel tree;
    setupFunctionLikeTree(tree);
    tree.addItem("Scene 1", functionParams(1));
    tree.addItem("x", functionParams(2), "Snapshots");

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    verifyFlatMatchesTree(flat, tree);
    QCOMPARE(flat.rowCount(), 2); // Snapshots, Scene 1

    // user expands the folder (what a click on the folder row does)
    QVERIFY(flat.setData(flat.index(0), true, TreeFlatModel::IsExpandedRole));
    verifyFlatMatchesTree(flat, tree);
    QCOMPARE(flat.rowCount(), 3); // Snapshots, x, Scene 1

    // deleteFunction(): the folder stays behind, empty
    QVERIFY(tree.removeItem(QString("Snapshots") + TreeModel::separator() + "x"));
    // a lone removal is a burst of one: reflected synchronously, no event loop needed
    verifyFlatMatchesTree(flat, tree);
    QCOMPARE(flat.rowCount(), 2); // Snapshots (now empty), Scene 1
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::LabelRole).toString(), QString("Snapshots"));

    // deleteFolder(): a separate user action, i.e. after the event loop has run
    // in between - now the folder itself, and its child TreeModel, go away
    flushDeferredRebuild();
    QVERIFY(tree.removeItem("Snapshots"));
    verifyFlatMatchesTree(flat, tree);
    QCOMPARE(flat.rowCount(), 1);
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::LabelRole).toString(), QString("Scene 1"));

    flushDeferredRebuild();
    verifyFlatMatchesTree(flat, tree);
}

// The create-folder-then-delete sequence arriving in ONE synchronous call stack
// (e.g. a pipelined control API batch), so a coalesced rebuild is already pending
// when the removal happens and slotSourceStructureChanged() takes its deferred
// path. This is the case commit 859f491a7 only half-fixed: it made the FIRST
// signal of a burst rebuild synchronously, but a removal arriving later in the
// same burst still left the flat model holding rows into the subtree that removal
// freed - including, for an expanded nested folder, that folder's child TreeModel
// as a row's `owner` - until the deferred rebuild ran. TreeModel::
// structureAboutToChange now bubbles from any depth BEFORE the deletion and the
// flat model drops every row on it, regardless of what the coalescing decides to
// do with the structureChanged that follows.
void TreeFlatModel_Test::createFolderThenDeleteInOneBurstNeverDanglesOwner()
{
    TreeModel tree;
    setupFunctionLikeTree(tree);
    tree.addItem("Scene 1", functionParams(1));

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    flushDeferredRebuild();
    QCOMPARE(flat.rowCount(), 1);

    // create into a brand-new nested folder: the first signal of the burst
    // rebuilds synchronously and schedules the deferred one
    const QString subPath = QString("Snapshots") + TreeModel::separator() + "Sub";
    tree.addItem("x", functionParams(2), subPath);
    tree.setItemRoleData("Snapshots", true, TreeModel::IsExpandedRole);
    tree.setItemRoleData(subPath, true, TreeModel::IsExpandedRole);
    verifyFlatMatchesTree(flat, tree);
    QCOMPARE(flat.rowCount(), 4); // Snapshots, Sub, x, Scene 1

    // ... then, still in the same burst, delete the nested folder: that frees
    // its child TreeModel, which row "x" referenced as its owner
    QVERIFY(tree.removeItem(subPath));

    // whatever the coalescing decided (rows dropped and waiting for the deferred
    // rebuild, or already rebuilt), no row may reference freed memory
    verifyEveryRowIsLive(flat);

    flushDeferredRebuild();
    verifyFlatMatchesTree(flat, tree);
    QCOMPARE(flat.rowCount(), 2); // Snapshots (empty), Scene 1

    // same for a nested LEAF removal in one burst - the plain deleteFunction() shape
    tree.addItem("y", functionParams(3), "Snapshots");
    QVERIFY(tree.removeItem(QString("Snapshots") + TreeModel::separator() + "y"));
    verifyEveryRowIsLive(flat);
    flushDeferredRebuild();
    verifyFlatMatchesTree(flat, tree);
    QCOMPARE(flat.rowCount(), 2);
}

// The exact shape of the 2026-09-15 crash: FunctionManager::updateFunctionsTree()
// (any filter/search change, or a project (re)load) does TreeModel::clear() and
// then repopulates and re-expands the same tree, with the Function Manager's flat
// list attached the whole time. clear() deletes items last-to-first, and each
// folder's destructor tears down that folder's child TreeModel. Before the fix,
// the FIRST folder's teardown bubbled a structureChanged out of the middle of
// clear()'s loop - the flat model's synchronous first-of-burst rebuild()
// (859f491a7) then re-walked the half-cleared tree and cached the not-yet-deleted
// items and, for every still-expanded folder among them, that folder's child
// TreeModel as the `owner` of its visible rows. The very same loop freed all of
// them a moment later, and every later structureChanged of the burst (including
// clear()'s own, after the deletions) was swallowed by the coalescing.
// restoreExpandedPaths()' roleChanged then made the QML ListView re-read
// model.hasChildren on such a row: TreeModel::items() on a freed TreeModel - the
// access violation in the crash report.
void TreeFlatModel_Test::clearWithExpandedFoldersDropsEveryRowBeforeAnyDeletion()
{
    TreeModel tree;
    setupFunctionLikeTree(tree);
    // sorted: folders "A" and "Z" first, then the leaf. clear() walks from the
    // END, so "Z"'s teardown is the first bubbled signal, while "A" (expanded,
    // with a child TreeModel the flat model references) is still alive to be
    // snapshotted.
    tree.addItem("a1", functionParams(1), "A");
    tree.addItem("z1", functionParams(2), "Z");
    tree.addItem("Scene 1", functionParams(3));
    tree.setItemRoleData("A", true, TreeModel::IsExpandedRole);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    flushDeferredRebuild();
    verifyFlatMatchesTree(flat, tree);
    QCOMPARE(flat.rowCount(), 4); // A, a1, Z, Scene 1

    // --- updateFunctionsTree(), step by step ---
    tree.clear();
    // Nothing exists any more, so nothing may be shown: the only valid row count
    // right here is 0. Before the fix this read 2 (A and a1, both already freed).
    QCOMPARE(flat.rowCount(), 0);

    // repopulate (addFunctionTreeItem() per function, same burst) ...
    tree.addItem("a1", functionParams(1), "A");
    tree.addItem("z1", functionParams(2), "Z");
    tree.addItem("Scene 1", functionParams(3));
    // ... and restoreExpandedPaths(): the roleChanged this emits is what made
    // the real UI re-read data() on the stale rows
    tree.setItemRoleData("A", true, TreeModel::IsExpandedRole);

    // must be safe to read at any point of the burst ...
    verifyEveryRowIsLive(flat);

    // ... and correct once the coalesced rebuild has run
    flushDeferredRebuild();
    verifyFlatMatchesTree(flat, tree);
    QCOMPARE(flat.rowCount(), 4);
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::LabelRole).toString(), QString("a1"));
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::DepthRole).toInt(), 1);
}

void TreeFlatModel_Test::fullPathDistinguishesSameNamedFoldersAtDifferentDepths()
{
    // the Function Manager's "A/X" vs "X" case: two folders share the name "X",
    // one at the root and one nested under "A"
    TreeModel tree;
    tree.setColumnNames(QStringList() << "id");
    tree.enableSorting(false);
    tree.addItem("nested func", QVariantList() << 1, QString("A`X"));
    tree.addItem("root func", QVariantList() << 2, "X");
    tree.addItem("Empty", QVariantList() << 3, "A", TreeModel::EmptyNode);
    tree.setItemRoleData("A", true, TreeModel::IsExpandedRole);
    tree.setItemRoleData("A`X", true, TreeModel::IsExpandedRole);
    tree.setItemRoleData("X", true, TreeModel::IsExpandedRole);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);

    QStringList labels, paths, fullPaths;
    for (int i = 0; i < flat.rowCount(); i++)
    {
        labels << flat.data(flat.index(i), TreeFlatModel::LabelRole).toString();
        paths << flat.data(flat.index(i), TreeFlatModel::PathRole).toString();
        fullPaths << flat.data(flat.index(i), TreeFlatModel::FullPathRole).toString();
    }

    QCOMPARE(labels, QStringList() << "A" << "X" << "nested func" << "Empty" << "X" << "root func");
    // TreeModel's own path role is only the item's own segment: ambiguous
    QCOMPARE(paths.at(1), QString("X"));
    QCOMPARE(paths.at(4), QString("X"));
    // the flat model's full path is not
    QCOMPARE(fullPaths, QStringList() << "A" << "A`X" << "A`X`nested func" << "A`Empty" << "X" << "X`root func");
    // the role must be reachable from QML as "fullPath"
    const QAbstractItemModel *model = &flat;
    QCOMPARE(model->roleNames().value(TreeFlatModel::FullPathRole), QByteArray("fullPath"));
}

void TreeFlatModel_Test::fullPathFollowsFolderRename()
{
    TreeModel tree;
    tree.setColumnNames(QStringList() << "id");
    tree.enableSorting(false);
    tree.addItem("f", QVariantList() << 1, QString("A`X"));
    tree.setItemRoleData("A", true, TreeModel::IsExpandedRole);
    tree.setItemRoleData("A`X", true, TreeModel::IsExpandedRole);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    QCOMPARE(flat.rowCount(), 3);
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::FullPathRole).toString(), QString("A`X`f"));

    QSignalSpy spy(&flat, &TreeFlatModel::dataChanged);

    // rename "A" to "B" the way FunctionManager::setFolderPath(isRelative) does:
    // label first, then the path role through the new tree path
    tree.setItemRoleData("A", "B", TreeModel::LabelRole);
    tree.setItemRoleData("B", "B", TreeModel::PathRole);

    QCOMPARE(flat.rowCount(), 3);
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::FullPathRole).toString(), QString("B"));
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::FullPathRole).toString(), QString("B`X"));
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::FullPathRole).toString(), QString("B`X`f"));

    // and the descendants were told their full path changed
    bool descendantsNotified = false;
    for (int i = 0; i < spy.count(); i++)
    {
        QModelIndex top = spy.at(i).at(0).toModelIndex();
        QModelIndex bottom = spy.at(i).at(1).toModelIndex();
        QVector<int> roles = spy.at(i).at(2).value<QVector<int>>();
        if (top.row() == 1 && bottom.row() == 2 && roles.contains(TreeFlatModel::FullPathRole))
            descendantsNotified = true;
    }
    QVERIFY(descendantsNotified);
}

// The coalesced rebuild is a queued QMetaObject::invokeMethod(): without a
// QCoreApplication instance, posted events are never delivered, so the tests
// above that flush it need a guiless app (no window/display, just the event
// queue) instead of QTEST_APPLESS_MAIN.
QTEST_GUILESS_MAIN(TreeFlatModel_Test)
