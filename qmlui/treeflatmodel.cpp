/*
  Q Light Controller Plus
  treeflatmodel.cpp

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

#include "treeflatmodel.h"
#include "treemodelitem.h"

TreeFlatModel::TreeFlatModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_sourceModel(nullptr)
{
}

TreeFlatModel::~TreeFlatModel()
{
}

QObject *TreeFlatModel::sourceModel() const
{
    return m_sourceModel;
}

void TreeFlatModel::setSourceModel(QObject *model)
{
    TreeModel *tree = qobject_cast<TreeModel *>(model);
    if (tree == m_sourceModel)
        return;

    if (m_sourceModel != nullptr)
    {
        disconnect(m_sourceModel, &TreeModel::roleChanged, this, &TreeFlatModel::slotSourceRoleChanged);
        disconnect(m_sourceModel, &QAbstractItemModel::modelAboutToBeReset, this, &TreeFlatModel::slotSourceInvalidated);
        disconnect(m_sourceModel, &QAbstractItemModel::rowsAboutToBeRemoved, this, &TreeFlatModel::slotSourceInvalidated);
        disconnect(m_sourceModel, &QAbstractItemModel::rowsInserted, this, &TreeFlatModel::slotSourceStructureChanged);
        disconnect(m_sourceModel, &QAbstractItemModel::rowsRemoved, this, &TreeFlatModel::slotSourceStructureChanged);
    }

    m_sourceModel = tree;

    if (m_sourceModel != nullptr)
    {
        connect(m_sourceModel, &TreeModel::roleChanged, this, &TreeFlatModel::slotSourceRoleChanged);
        connect(m_sourceModel, &QAbstractItemModel::modelAboutToBeReset, this, &TreeFlatModel::slotSourceInvalidated);
        connect(m_sourceModel, &QAbstractItemModel::rowsAboutToBeRemoved, this, &TreeFlatModel::slotSourceInvalidated);
        // TreeModel::addItem()/removeItem() (e.g. FunctionManager adding/deleting a single
        // function) mutate the root tree directly with no accompanying "list changed"
        // signal at all - without these, a single incremental add or remove at the root
        // level would never be reflected here until some unrelated full rebuild happened to
        // be triggered later.
        connect(m_sourceModel, &QAbstractItemModel::rowsInserted, this, &TreeFlatModel::slotSourceStructureChanged);
        connect(m_sourceModel, &QAbstractItemModel::rowsRemoved, this, &TreeFlatModel::slotSourceStructureChanged);
    }

    emit sourceModelChanged();
    rebuild();
}

void TreeFlatModel::slotSourceInvalidated()
{
    if (m_rows.isEmpty())
        return;

    beginResetModel();
    m_rows.clear();
    m_indexOfItem.clear();
    endResetModel();
}

void TreeFlatModel::slotSourceStructureChanged()
{
    rebuild();
}

void TreeFlatModel::appendSubtree(TreeModel *tree, int depth, QVector<FlatRow> &out)
{
    if (tree == nullptr)
        return;

    const QList<TreeModelItem *> items = tree->items();
    for (TreeModelItem *item : items)
    {
        out.append({ item, tree, depth });
        if ((item->flags() & TreeModel::Expanded) && item->hasChildren())
            appendSubtree(item->children(), depth + 1, out);
    }
}

void TreeFlatModel::reindexFrom(int from)
{
    for (int i = from; i < m_rows.count(); i++)
        m_indexOfItem[m_rows.at(i).item] = i;
}

void TreeFlatModel::rebuild()
{
    beginResetModel();

    m_rows.clear();
    m_indexOfItem.clear();
    m_customRoleToOwnerRole.clear();

    if (m_sourceModel != nullptr)
    {
        static const QVector<QPair<int, QString>> customRoles = {
            { ClassRefRole, QStringLiteral("classRef") },
            { TypeRole, QStringLiteral("type") },
            { IdRole, QStringLiteral("id") },
            { SubIdRole, QStringLiteral("subid") },
            { ChIdxRole, QStringLiteral("chIdx") },
            { InGroupRole, QStringLiteral("inGroup") },
            { FlagsRole, QStringLiteral("flags") },
            { CanFadeRole, QStringLiteral("canFade") },
            { PrecedenceRole, QStringLiteral("precedence") },
            { ModifierRole, QStringLiteral("modifier") }
        };

        for (const auto &pair : customRoles)
        {
            int ownerRole = m_sourceModel->roleIndex(pair.second);
            if (ownerRole >= 0)
                m_customRoleToOwnerRole[pair.first] = ownerRole;
        }

        appendSubtree(m_sourceModel, 0, m_rows);
        reindexFrom(0);
    }

    endResetModel();
}

int TreeFlatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_rows.count();
}

QVariant TreeFlatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.count())
        return QVariant();

    const FlatRow &row = m_rows.at(index.row());

    if (role == DepthRole)
        return row.depth;

    if (role >= TreeModel::LabelRole && role < TreeModel::FixedRolesEnd)
        return row.owner->data(row.owner->index(row.owner->items().indexOf(row.item)), role);

    int ownerRole = m_customRoleToOwnerRole.value(role, -1);
    if (ownerRole < 0)
        return QVariant();

    return row.owner->data(row.owner->index(row.owner->items().indexOf(row.item)), ownerRole);
}

bool TreeFlatModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.count())
        return false;

    const FlatRow &row = m_rows.at(index.row());
    int ownerRow = row.owner->items().indexOf(row.item);
    if (ownerRow < 0)
        return false;

    int ownerRole = role;
    if (role < TreeModel::LabelRole || role >= TreeModel::FixedRolesEnd)
    {
        ownerRole = m_customRoleToOwnerRole.value(role, -1);
        if (ownerRole < 0)
            return false;
    }

    return row.owner->setData(row.owner->index(ownerRow), value, ownerRole);
}

QHash<int, QByteArray> TreeFlatModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[LabelRole] = "label";
    roles[PathRole] = "path";
    roles[IsExpandedRole] = "isExpanded";
    roles[IsSelectedRole] = "isSelected";
    roles[IsCheckableRole] = "isCheckable";
    roles[IsCheckedRole] = "isChecked";
    roles[ItemsCountRole] = "itemsCount";
    roles[HasChildrenRole] = "hasChildren";
    roles[ClassRefRole] = "classRef";
    roles[TypeRole] = "type";
    roles[IdRole] = "id";
    roles[SubIdRole] = "subid";
    roles[ChIdxRole] = "chIdx";
    roles[InGroupRole] = "inGroup";
    roles[FlagsRole] = "flags";
    roles[CanFadeRole] = "canFade";
    roles[PrecedenceRole] = "precedence";
    roles[ModifierRole] = "modifier";
    roles[DepthRole] = "depth";

    return roles;
}

void TreeFlatModel::slotSourceRoleChanged(TreeModelItem *item, int role, const QVariant &value)
{
    int row = m_indexOfItem.value(item, -1);
    if (row < 0)
        return;

    emit dataChanged(index(row), index(row));

    if (role != TreeModel::IsExpandedRole)
        return;

    const int depth = m_rows.at(row).depth;
    const bool expanded = value.toBool();

    if (expanded && item->hasChildren())
    {
        QVector<FlatRow> newRows;
        appendSubtree(item->children(), depth + 1, newRows);
        if (newRows.isEmpty())
            return;

        beginInsertRows(QModelIndex(), row + 1, row + newRows.count());
        for (int i = 0; i < newRows.count(); i++)
            m_rows.insert(row + 1 + i, newRows.at(i));
        reindexFrom(row + 1);
        endInsertRows();
    }
    else if (!expanded)
    {
        int end = row + 1;
        while (end < m_rows.count() && m_rows.at(end).depth > depth)
            end++;

        if (end == row + 1)
            return;

        beginRemoveRows(QModelIndex(), row + 1, end - 1);
        for (int i = row + 1; i < end; i++)
            m_indexOfItem.remove(m_rows.at(i).item);
        m_rows.remove(row + 1, end - row - 1);
        reindexFrom(row + 1);
        endRemoveRows();
    }
}
