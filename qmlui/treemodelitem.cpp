/*
  Q Light Controller Plus
  treemodelitem.cpp

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

#include <QQmlEngine>
#include <QDebug>

#include "treemodelitem.h"
#include "treemodel.h"

TreeModelItem::TreeModelItem(QString label, QObject *parent)
    : QObject(parent)
    , m_label(label)
    , m_path(QString())
    , m_flags(0)
    , m_children(nullptr)
{
}

TreeModelItem::~TreeModelItem()
{
    //qDebug() << "!!! WARNING TreeModelItem destroyed WARNING !!!";
    if (hasChildren())
    {
        // A subtree being torn down must not announce its own teardown: the tree
        // that owns this item already emitted structureAboutToChange() before
        // deleting it (see TreeModel::clear()/removeItem()), and a bubbled
        // structureChanged from in here would reach that owner's listeners while
        // the owner is still mid-deletion - letting them re-walk a half-deleted
        // tree and cache pointers to items/child trees freed a moment later.
        // ~QObject unblocks signals again for destroyed(), so QPointer-style
        // consumers are unaffected. ~TreeModel() clears the items itself.
        m_children->blockSignals(true);
        delete m_children;
        m_children = nullptr;
    }
}

QString TreeModelItem::label() const
{
    return m_label;
}

void TreeModelItem::setLabel(QString label)
{
    m_label = label;
}

QString TreeModelItem::path() const
{
    return m_path;
}

void TreeModelItem::setPath(QString path)
{
    m_path = path;
}

void TreeModelItem::setFlags(int flags)
{
    m_flags = flags;
}

void TreeModelItem::setFlag(int flag, bool enable)
{
    if (enable)
        m_flags |= flag;
    else
        m_flags &= ~flag;
}

int TreeModelItem::flags() const
{
    return m_flags;
}

QVariant TreeModelItem::data(int index) const
{
    //qDebug() << "Getting data at" << index << label();
    if (index < 0 || index >= m_data.count())
        return QVariant();

    return m_data.at(index);
}

QVariantList TreeModelItem::data() const
{
    return m_data;
}

void TreeModelItem::setData(QVariantList data)
{
    m_data = data;
}

void TreeModelItem::setRoleData(int index, QVariant value)
{
    if (index < 0 || index >= m_data.count())
        return;

    m_data[index] = value;
}

bool TreeModelItem::setChildrenColumns(QStringList columns)
{
    bool childrenTreeCreated = false;
    if (m_children == nullptr)
    {
        m_children = new TreeModel();
        QQmlEngine::setObjectOwnership(m_children, QQmlEngine::CppOwnership);
        childrenTreeCreated = true;
    }
    m_children->setColumnNames(columns);

    return childrenTreeCreated;
}

bool TreeModelItem::addChild(QString label, QVariantList data, QStringList columns, bool sorting, QString path, int flags)
{
    bool childrenTreeCreated = false;
    if (m_children == nullptr)
    {
        m_children = new TreeModel();
        QQmlEngine::setObjectOwnership(m_children, QQmlEngine::CppOwnership);
        childrenTreeCreated = true;
    }
    m_children->setColumnNames(columns);
    m_children->enableSorting(sorting);
    m_children->addItem(label, data, path, flags);

    return childrenTreeCreated;
}

bool TreeModelItem::hasChildren() const
{
    if (m_children != nullptr)
        return true;

    return false;
}

TreeModel *TreeModelItem::children() const
{
    return m_children;
}

void TreeModelItem::printItem(int tab) const
{
    qDebug() << QString("%1%2").arg(QString(tab, QChar(0x20))).arg(label()) << m_path << m_data;
}
