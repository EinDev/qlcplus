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

#include "treemodel_test.h"
#include "treemodel.h"
#include "treeflatmodel.h"

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

QTEST_APPLESS_MAIN(TreeModel_Test)
