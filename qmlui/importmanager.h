/*
  Q Light Controller Plus
  importmanager.h

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

#ifndef IMPORTMANAGER_H
#define IMPORTMANAGER_H

#include <QQuickView>

#include "treemodel.h"

class ProjectImporter;
class Doc;

class ImportManager final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariant groupsTreeModel READ groupsTreeModel NOTIFY groupsTreeModelChanged)
    Q_PROPERTY(QVariant functionsTreeModel READ functionsTreeModel NOTIFY functionsTreeModelChanged)
    Q_PROPERTY(QString fixtureSearchFilter READ fixtureSearchFilter WRITE setFixtureSearchFilter NOTIFY fixtureSearchFilterChanged)
    Q_PROPERTY(QString functionSearchFilter READ functionSearchFilter WRITE setFunctionSearchFilter NOTIFY functionSearchFilterChanged)

public:
    ImportManager(QQuickView *view, Doc *doc, QObject *parent = 0);
    ~ImportManager();

    bool loadWorkspace(const QString &fileName);

    void apply();

private:
    /** Method called recursively to check/uncheck all the sub-items of a tree */
    void setChildrenChecked(TreeModel *tree, bool checked) const;

    /** Update fixture/group ID lists for a single tree item */
    void updateFixtureItemID(const QVariantList &itemData, bool checked);

    /** Method called recursively to update fixture ID lists
     *  when a parent node is checked/unchecked */
    void updateChildrenFixtureIDs(TreeModel *tree, bool checked);

private:
    /** Reference to the QML view root */
    QQuickView *m_view;
    /** Reference to the project workspace */
    Doc *m_doc;
    /** The project to import from, the selection and the actual import
     *  (engine/src/projectimporter.h, shared with the control API) */
    ProjectImporter *m_importer;

    /*********************************************************************
     * Fixture tree
     *********************************************************************/
public:
    QVariant groupsTreeModel();

    /** Get/Set a string to filter Group/Fixture/Channel names */
    QString fixtureSearchFilter() const;
    void setFixtureSearchFilter(QString searchFilter);

    /** Method called recursively to update Fixture items checked state */
    void checkFixtureTree(TreeModel *tree) const;

protected slots:
    void slotFixtureTreeDataChanged(TreeModelItem *item, int role, const QVariant &value);

private:
    /** Data model used by the QML UI to represent groups/fixtures/channels */
    TreeModel *m_fixtureTree;
    /** Flag to filter signals when updating checked states */
    bool m_fixtureTreeUpdating;
    /** A string to filter the displayed fixture tree items */
    QString m_fixtureSearchFilter;

    /*********************************************************************
     * Function tree
     *********************************************************************/
public:
    QVariant functionsTreeModel();

    /** Get/Set a string to filter Group/Fixture/Channel names */
    QString functionSearchFilter() const;
    void setFunctionSearchFilter(QString searchFilter);

private:
    /** Update a tree suitable to be displayed by the UI */
    void updateFunctionsTree();

    /** Method called recursively to create a map of ID / TreeModelItems */
    void checkFunctionTree(TreeModel *tree) const;

protected slots:
    void slotFunctionTreeDataChanged(TreeModelItem *item, int role, const QVariant &value);

signals:
    /** Notify the listeners that the fixture tree model has changed */
    void groupsTreeModelChanged();
    /** Notify the listeners that the fixture search filter has changed */
    void fixtureSearchFilterChanged();

    /** Notify the listeners that the function tree model has changed */
    void functionsTreeModelChanged();
    /** Notify the listeners that the function search filter has changed */
    void functionSearchFilterChanged();

private:
    /** Data model used by the QML UI to represent groups/fixtures/channels */
    TreeModel *m_functionTree;
    /** Flag to filter signals when updating checked states */
    bool m_functionTreeUpdating;
    /** A string to filter the displayed fixture tree items */
    QString m_functionSearchFilter;
};
#endif /* IMPORTMANAGER_H */
