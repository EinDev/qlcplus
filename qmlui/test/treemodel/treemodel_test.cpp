/*
  Q Light Controller - Unit test
  treemodel_test.cpp

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
#include <QQmlEngine>
#include <QQmlContext>

#include "treemodel_test.h"
#include "treemodel.h"
#include "treeflatmodel.h"

// TreeModel::setData()/TreeFlatModel::slotSourceRoleChanged() both look up a
// "contextManager" QML context property and call its isBatchSelection() slot
// dynamically (via QMetaObject::invokeMethod(), see commit 5bf602c57 - it's
// called this way specifically so these files don't need to #include
// contextmanager.h). This stand-in lets tests actually flip that flag and
// verify the suppression path fires, without any real ContextManager/
// QQuickView/Doc involved. Deliberately NOT in the anonymous namespace below:
// older moc has trouble with QObject-derived types declared inside one.
class FakeContextManager : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE bool isBatchSelection() const { return m_batch; }
    void setBatchSelection(bool enable) { m_batch = enable; }
private:
    bool m_batch = false;
};

namespace
{
    /** A folder ("Group") with one child ("Fixture B"), plus a top-level leaf
     *  ("Fixture A") - mirrors the Fixture Groups/Fixtures tree shape that
     *  ContextManager's mass-selection code (setBatchSelection/
     *  resetFixtureSelection/setRectangleSelection/...) actually drives via
     *  FixtureManager::setItemRoleData(). */
    void buildFixtureLikeTree(TreeModel &tree)
    {
        tree.setColumnNames(QStringList() << "id");
        tree.enableSorting(false);

        tree.addItem("Fixture A", QVariantList() << 1);
        tree.addItem("Fixture B", QVariantList() << 2, "Group");
    }
}

// ContextManager's batch-selection path (setFixtureSelection(), see
// contextmanager.cpp) sets IsSelectedRole to 2 (not 1) for every fixture it
// selects during a rectangle-select/select-all/reset, specifically so that
// selecting fixture N doesn't wipe out fixtures 1..N-1 that were just
// selected in the same batch - only a plain single click (value 1) is meant
// to exclusively select. This pins that contract down.
void TreeModel_Test::additiveSelectionDoesNotTriggerSingleSelection()
{
    TreeModel tree;
    tree.setColumnNames(QStringList() << "id");
    tree.enableSorting(false);
    tree.addItem("Fixture A", QVariantList() << 1);
    tree.addItem("Fixture B", QVariantList() << 2);

    tree.setItemRoleData("Fixture A", 2, TreeModel::IsSelectedRole);
    tree.setItemRoleData("Fixture B", 2, TreeModel::IsSelectedRole);

    QCOMPARE(tree.data(tree.index(0), TreeModel::IsSelectedRole).toBool(), true);
    QCOMPARE(tree.data(tree.index(1), TreeModel::IsSelectedRole).toBool(), true);
}

// TreeModel::setSingleSelection() used to only recurse into a child tree when
// it *wasn't* itself the original sender, to avoid double-walking. That guard
// was removed this session (it was a no-op anyway: setSingleSelection() is
// never connected as a slot, so sender() was always null) but the recursive
// walk itself is exactly what lets a single-select exclude an already
// selected item several levels down - e.g. Function Manager style trees.
void TreeModel_Test::singleSelectionDeselectsPreviousChoiceIncludingDescendants()
{
    TreeModel tree;
    buildFixtureLikeTree(tree);

    QString childPath = QString("Group") + TreeModel::separator() + "Fixture B";
    tree.setItemRoleData(childPath, 1, TreeModel::IsSelectedRole);

    TreeModelItem *group = tree.itemAtPath("Group");
    QVERIFY(group != nullptr);
    QCOMPARE(group->children()->data(group->children()->index(0), TreeModel::IsSelectedRole).toBool(), true);

    // Exclusively selecting the top-level leaf must reach down into "Group"
    // and deselect "Fixture B".
    tree.setItemRoleData("Fixture A", 1, TreeModel::IsSelectedRole);

    QCOMPARE(tree.data(tree.index(0), TreeModel::IsSelectedRole).toBool(), true);
    QCOMPARE(group->children()->data(group->children()->index(0), TreeModel::IsSelectedRole).toBool(), false);
}

// ContextManager::setBatchSelection(false) - once a mass selection change
// (reset/select-all/rectangle-select/...) is done - forces a whole-tree
// refresh via fixtureTree()->setItemRoleData("", 0, TreeModel::IsSelectedRole)
// instead of relying on the (suppressed, during the batch) per-item
// dataChanged signals. This is the notify-only broadcast that makes that
// possible: it must not touch any item's actual selection flag, only tell
// listeners "re-read everything".
void TreeModel_Test::emptyPathBroadcastRefreshesEveryRow()
{
    TreeModel tree;
    buildFixtureLikeTree(tree);

    tree.setItemRoleData("Fixture A", 1, TreeModel::IsSelectedRole);
    QCOMPARE(tree.data(tree.index(0), TreeModel::IsSelectedRole).toBool(), true);

    bool sawNullBroadcast = false;
    int broadcastRole = -1;
    QObject::connect(&tree, &TreeModel::roleChanged,
                      [&](TreeModelItem *item, int role, const QVariant &) {
                          if (item == nullptr)
                          {
                              sawNullBroadcast = true;
                              broadcastRole = role;
                          }
                      });
    QSignalSpy dataSpy(&tree, &TreeModel::dataChanged);

    tree.setItemRoleData(QString(), 0, TreeModel::IsSelectedRole);

    QCOMPARE(sawNullBroadcast, true);
    QCOMPARE(broadcastRole, (int)TreeModel::IsSelectedRole);
    QCOMPARE(dataSpy.count(), 1);
    QCOMPARE(dataSpy.at(0).at(0).toModelIndex(), tree.index(0));
    QCOMPARE(dataSpy.at(0).at(1).toModelIndex(), tree.index(tree.rowCount() - 1));

    // Purely a notification - the value (0) passed in must NOT have
    // deselected "Fixture A".
    QCOMPARE(tree.data(tree.index(0), TreeModel::IsSelectedRole).toBool(), true);
}

// End-to-end version of the above through TreeFlatModel, which is what the
// real Fixture Groups/Fixtures panel is actually bound to (see the
// "flatten the Fixture Groups tree" commits). This is the concrete chain
// ContextManager::setBatchSelection(false) relies on to make the fixture
// list visually refresh after a rectangle-select/reset/etc: root TreeModel's
// empty-path broadcast -> TreeFlatModel::slotSourceRoleChanged(nullptr, ...)
// -> a full-range dataChanged on the flattened list.
void TreeModel_Test::emptyPathBroadcastReachesFlattenedList()
{
    TreeModel tree;
    buildFixtureLikeTree(tree);
    tree.setItemRoleData("Group", true, TreeModel::IsExpandedRole);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    QCOMPARE(flat.rowCount(), 3); // Fixture A, Group, Fixture B

    QString childPath = QString("Group") + TreeModel::separator() + "Fixture B";
    tree.setItemRoleData(childPath, 2, TreeModel::IsSelectedRole);

    QSignalSpy flatDataSpy(&flat, &TreeFlatModel::dataChanged);

    tree.setItemRoleData(QString(), 0, TreeModel::IsSelectedRole);

    QVERIFY(flatDataSpy.count() >= 1);
    int minRow = flat.rowCount();
    int maxRow = -1;
    for (const QList<QVariant> &call : flatDataSpy)
    {
        minRow = qMin(minRow, call.at(0).toModelIndex().row());
        maxRow = qMax(maxRow, call.at(1).toModelIndex().row());
    }
    QCOMPARE(minRow, 0);
    QCOMPARE(maxRow, flat.rowCount() - 1);

    // The flattened "Fixture B" row must still read as selected - the
    // broadcast is a repaint signal, not a data mutation.
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::IsSelectedRole).toBool(), true);
}

// ContextManager::refreshGroupSelectionRoles() (added to fix "selecting every
// fixture of a group doesn't highlight the group in the Fixture Groups list")
// pushes a Fixture Group's own derived "fully selected" state onto its
// top-level tree row via FixtureManager::setGroupItemRoleData(), which is
// just tree.setItemRoleData(group->name(), ..., IsSelectedRole) - a
// root-level, single-segment path lookup that happens to target a row with
// children. Pin down that this targets only the group's own row and never
// touches its members' independently-tracked selection state, in either
// direction, and that both are visible as distinct rows through
// TreeFlatModel (which is what FixtureGroupFlatDelegate.qml actually binds
// its "isSelected" to for every row, group and fixture alike).
void TreeModel_Test::groupRowRoleIsIndependentOfMemberRows()
{
    TreeModel tree;
    buildFixtureLikeTree(tree);

    // The group's only member gets selected first (as ContextManager's
    // per-fixture path already does via FixtureManager::setItemRoleData()).
    QString childPath = QString("Group") + TreeModel::separator() + "Fixture B";
    tree.setItemRoleData(childPath, 2, TreeModel::IsSelectedRole);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    tree.setItemRoleData("Group", true, TreeModel::IsExpandedRole);
    QCOMPARE(flat.rowCount(), 3); // Fixture A, Group, Fixture B

    // The group itself is now fully selected (its one and only member is) -
    // this is refreshGroupSelectionRoles() pushing that derived state onto
    // the group's own row.
    tree.setItemRoleData("Group", 2, TreeModel::IsSelectedRole);

    TreeModelItem *group = tree.itemAtPath("Group");
    QVERIFY(group != nullptr);
    QCOMPARE(tree.data(tree.index(1), TreeModel::IsSelectedRole).toBool(), true);
    QCOMPARE(group->children()->data(group->children()->index(0), TreeModel::IsSelectedRole).toBool(), true);
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::IsSelectedRole).toBool(), true); // Group row
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::IsSelectedRole).toBool(), true); // Fixture B row

    // Deselecting the group's row (e.g. some other, not-in-this-group fixture
    // got selected too, so the group is no longer *fully* selected) must not
    // reach down and deselect the member that's still actually selected.
    tree.setItemRoleData("Group", 0, TreeModel::IsSelectedRole);

    QCOMPARE(tree.data(tree.index(1), TreeModel::IsSelectedRole).toBool(), false);
    QCOMPARE(group->children()->data(group->children()->index(0), TreeModel::IsSelectedRole).toBool(), true);
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::IsSelectedRole).toBool(), false);
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::IsSelectedRole).toBool(), true);
}

// Pins down the actual suppression mechanism itself, using a real
// isBatchSelection() call through QMetaObject::invokeMethod() rather than
// just asserting on its downstream effects (as groupRowRoleIsIndependentOf-
// MemberRows and the emptyPathBroadcast* tests above do, with no QML context
// set up at all - qmlEngine(this) is simply null there, so the batch check
// never engages and those tests exercise the non-batch broadcast path only).
void TreeModel_Test::batchSelectionSuppressesDataChangedUntilFlagCleared()
{
    QQmlEngine engine;
    FakeContextManager fakeCM;
    engine.rootContext()->setContextProperty("contextManager", &fakeCM);

    TreeModel tree;
    tree.setColumnNames(QStringList() << "id");
    tree.enableSorting(false);
    tree.addItem("Fixture A", QVariantList() << 1);
    tree.addItem("Fixture B", QVariantList() << 2);
    QQmlEngine::setContextForObject(&tree, engine.rootContext());

    QSignalSpy dataSpy(&tree, &TreeModel::dataChanged);

    fakeCM.setBatchSelection(true);
    tree.setItemRoleData("Fixture A", 2, TreeModel::IsSelectedRole);
    QCOMPARE(dataSpy.count(), 0);
    // the flag itself is still applied - setData() mutates state before it
    // ever checks batch mode, only the dataChanged notify is suppressed.
    QCOMPARE(tree.data(tree.index(0), TreeModel::IsSelectedRole).toBool(), true);

    fakeCM.setBatchSelection(false);
    tree.setItemRoleData("Fixture B", 2, TreeModel::IsSelectedRole);
    QCOMPARE(dataSpy.count(), 1);
    QCOMPARE(tree.data(tree.index(1), TreeModel::IsSelectedRole).toBool(), true);
}

// Same mechanism, one layer further out: TreeFlatModel::slotSourceRoleChanged()
// does its own independent isBatchSelection() check (via qmlEngine(this) on
// the flat model itself, not the source tree), so it suppresses its own
// dataChanged regardless of whether the nested TreeModel that actually owns
// "Fixture B" (see buildFixtureLikeTree()) has any QML context at all -
// roleChanged() is emitted unconditionally before any batch check and always
// bubbles up to the root tree TreeFlatModel is bound to.
void TreeModel_Test::batchSelectionSuppressesFlatModelDataChanged()
{
    QQmlEngine engine;
    FakeContextManager fakeCM;
    engine.rootContext()->setContextProperty("contextManager", &fakeCM);

    TreeModel tree;
    buildFixtureLikeTree(tree);
    tree.setItemRoleData("Group", true, TreeModel::IsExpandedRole);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    QQmlEngine::setContextForObject(&flat, engine.rootContext());
    QCOMPARE(flat.rowCount(), 3); // Fixture A, Group, Fixture B

    QString childPath = QString("Group") + TreeModel::separator() + "Fixture B";
    QSignalSpy flatDataSpy(&flat, &TreeFlatModel::dataChanged);

    fakeCM.setBatchSelection(true);
    tree.setItemRoleData(childPath, 2, TreeModel::IsSelectedRole);
    QCOMPARE(flatDataSpy.count(), 0);
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::IsSelectedRole).toBool(), true);

    fakeCM.setBatchSelection(false);
    tree.setItemRoleData(childPath, 0, TreeModel::IsSelectedRole);
    QCOMPARE(flatDataSpy.count(), 1);
    QCOMPARE(flat.data(flat.index(2), TreeFlatModel::IsSelectedRole).toBool(), false);
}

// Structural changes (TreeModel::addItem()/removeItem()) are never gated by
// the batch-selection flag at all - only setData()'s notify is. This pins
// down that a structural change made *while* a mass selection change happens
// to be in progress elsewhere still leaves the tree (and its flattened view)
// fully consistent once the batch ends.
void TreeModel_Test::structuralChangeDuringBatchSelectionStaysConsistent()
{
    QQmlEngine engine;
    FakeContextManager fakeCM;
    engine.rootContext()->setContextProperty("contextManager", &fakeCM);

    TreeModel tree;
    tree.setColumnNames(QStringList() << "id");
    tree.enableSorting(false);
    tree.addItem("Fixture A", QVariantList() << 1);
    tree.addItem("Fixture B", QVariantList() << 2);
    QQmlEngine::setContextForObject(&tree, engine.rootContext());

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    QCOMPARE(flat.rowCount(), 2);

    fakeCM.setBatchSelection(true);
    tree.setItemRoleData("Fixture A", 2, TreeModel::IsSelectedRole);
    tree.addItem("Fixture C", QVariantList() << 3);
    tree.setItemRoleData("Fixture C", 2, TreeModel::IsSelectedRole);
    QVERIFY(tree.removeItem("Fixture B"));
    fakeCM.setBatchSelection(false);

    QCOMPARE(tree.rowCount(), 2); // Fixture A, Fixture C
    QCOMPARE(tree.data(tree.index(0), TreeModel::LabelRole).toString(), QString("Fixture A"));
    QCOMPARE(tree.data(tree.index(1), TreeModel::LabelRole).toString(), QString("Fixture C"));
    QCOMPARE(tree.data(tree.index(0), TreeModel::IsSelectedRole).toBool(), true);
    QCOMPARE(tree.data(tree.index(1), TreeModel::IsSelectedRole).toBool(), true);

    // the flat list rebuilds unconditionally on any structural change (see
    // TreeFlatModel::slotSourceStructureChanged()), independent of the batch
    // flag's state at the time - it must reflect the same final shape.
    QCOMPARE(flat.rowCount(), 2);
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::LabelRole).toString(), QString("Fixture A"));
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::LabelRole).toString(), QString("Fixture C"));
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::IsSelectedRole).toBool(), true);
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::IsSelectedRole).toBool(), true);
}

// Mirrors the hazard behind commit 7f0183986 ("fix TreeFlatModel reading
// freed TreeModelItems after tree rebuild"), for the role-changed
// notification path specifically. removeItem() actually deletes the
// TreeModelItem (see TreeModel::removeItem()).
//
// This used to be an open FINDING: removing an item nested inside an
// already-expanded group did NOT make TreeFlatModel drop that row from
// m_indexOfItem/m_rows, because TreeFlatModel only ever connected to the
// ROOT TreeModel's own QAbstractItemModel::rowsInserted/rowsRemoved, and a
// nested removeItem() fires those signals on the descendant TreeModel
// instance that actually owns the item, not the root - so the root's
// signals never fired, rebuild() never ran, and m_indexOfItem kept mapping
// the now-dangling item to its old row. A stale IsSelectedRole notification
// for that dangling item was merely harmless-but-stale (slotSourceRoleChanged
// returns before dereferencing `item` for that role), but the same path for
// IsExpandedRole (treeflatmodel.cpp, `item->hasChildren()`) would have
// dereferenced the freed pointer - a genuine use-after-free.
//
// Fixed by adding TreeModel::structureChanged(), emitted by every tree level
// whenever ITS OWN rows change and bubbled from every descendant up to the
// root exactly the way roleChanged already bubbles (wired in addItem(), see
// the structureChanged connect() calls alongside the existing roleChanged
// ones). TreeFlatModel now connects to structureChanged instead of the raw
// per-instance rowsInserted/rowsRemoved, so a nested removal correctly
// triggers rebuild() and purges the dangling item from m_indexOfItem - this
// test's rowCount() check below would have failed before the fix (the row
// would still be there) and the two roleChanged emissions at the end are now
// provably unreachable-but-still-checked-safe, rather than merely lucky.
void TreeModel_Test::staleItemRoleChangeIsIgnoredSafely()
{
    TreeModel tree;
    buildFixtureLikeTree(tree);
    tree.setItemRoleData("Group", true, TreeModel::IsExpandedRole);

    TreeFlatModel flat;
    flat.setSourceModel(&tree);
    QCOMPARE(flat.rowCount(), 3);

    QString childPath = QString("Group") + TreeModel::separator() + "Fixture B";
    TreeModelItem *childItem = tree.itemAtPath(childPath);
    QVERIFY(childItem != nullptr);

    QVERIFY(tree.removeItem(childPath));

    // The actual fix, pinned down: a nested-only removal now reaches
    // TreeFlatModel (via structureChanged bubbling) and rebuilds, dropping
    // the removed row. Before the fix this stayed at 3.
    QCOMPARE(flat.rowCount(), 2);
    QCOMPARE(flat.data(flat.index(0), TreeFlatModel::LabelRole).toString(), QString("Fixture A"));
    QCOMPARE(flat.data(flat.index(1), TreeFlatModel::LabelRole).toString(), QString("Group"));

    // childItem is dangling at this point (removeItem() deleted it) - used
    // here purely as a pointer value, exactly as slotSourceRoleChanged()'s
    // own m_indexOfItem.value(item, -1) lookup does. Since rebuild() already
    // purged it above, that lookup now correctly misses for both roles -
    // including IsExpandedRole, the one that used to be genuinely unsafe.
    // Reaching the end of this test at all (no crash) is what would have
    // failed before the fix for the IsExpandedRole line specifically.
    tree.roleChanged(childItem, TreeModel::IsSelectedRole, 1);
    tree.roleChanged(childItem, TreeModel::IsExpandedRole, true);
}

// TreeModel::removeItem() looks up a top-level (single-segment) path by
// scanning m_items for a matching label and only calls beginRemoveRows()/
// endRemoveRows() once it finds one - a path that doesn't match anything
// must fall through to returning false without mutating m_items at all,
// rather than e.g. removing nothing but still emitting the row-removal
// signals (which would desync any attached view's row count from the model's
// actual rowCount()).
void TreeModel_Test::removingNonExistentTopLevelItemLeavesTreeUnchanged()
{
    TreeModel tree;
    tree.setColumnNames(QStringList() << "id");
    tree.enableSorting(false);
    tree.addItem("Fixture A", QVariantList() << 1);
    tree.addItem("Fixture B", QVariantList() << 2);

    QSignalSpy rowsRemovedSpy(&tree, &TreeModel::rowsRemoved);

    QCOMPARE(tree.removeItem("Fixture Z"), false);

    QCOMPARE(tree.rowCount(), 2);
    QCOMPARE(rowsRemovedSpy.count(), 0);
    QCOMPARE(tree.data(tree.index(0), TreeModel::LabelRole).toString(), QString("Fixture A"));
    QCOMPARE(tree.data(tree.index(1), TreeModel::LabelRole).toString(), QString("Fixture B"));
}

// QTEST_APPLESS_MAIN (no QCoreApplication at all) was enough for every test
// above this point, but QQmlEngine - needed to actually exercise the
// isBatchSelection() context-property lookup below - requires one to exist.
// QTEST_GUILESS_MAIN still needs no window/display, just the event loop.
QTEST_GUILESS_MAIN(TreeModel_Test)
#include "treemodel_test.moc"
