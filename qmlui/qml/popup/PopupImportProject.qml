/*
  Q Light Controller Plus
  PopupImportProject.qml

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

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "."

CustomPopupDialog
{
    id: popupRoot
    width: mainView.width * 0.75
    height: mainView.height * 0.75
    title: qsTr("Import from project")
    standardButtons: Dialog.Cancel | Dialog.Apply
    onVisibleChanged: mainView.setDimScreen(visible)

    onClicked: (role) =>
    {
        if (role === Dialog.Cancel)
            qlcplus.cancelImport()
        else if (role === Dialog.Apply)
            qlcplus.importFromWorkspace()

        close()
    }

    contentItem:
        GridLayout
        {
            columnSpacing: UISettings.iconSizeMedium

            columns: 2
            rows: 2

            // row 1
            RobotoText
            {
                label: qsTr("Fixtures")
            }
            RobotoText
            {
                label: qsTr("Functions")
            }

            // row 2
            Rectangle
            {
                Layout.fillWidth: true
                height: UISettings.iconSizeMedium
                z: 5
                color: UISettings.bgMedium
                radius: 5
                border.width: 2
                border.color: UISettings.borderColorDark

                Text
                {
                    id: fxSearchIcon
                    x: 6
                    width: height
                    height: parent.height - 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: "gray"
                    font.family: UISettings.fontAwesomeFontName
                    font.pixelSize: height - 6
                    text: FontAwesome.fa_magnifying_glass
                }

                TextInput
                {
                    x: fxSearchIcon.width + 14
                    y: 3
                    height: parent.height - 6
                    width: parent.width - x
                    color: UISettings.fgMain
                    text: importManager.fixtureSearchFilter
                    font.family: UISettings.robotoFontName
                    font.pixelSize: height - 6
                    selectionColor: UISettings.highlightPressed
                    selectByMouse: true

                    onTextEdited: importManager.fixtureSearchFilter = text
                }
            }

            Rectangle
            {
                Layout.fillWidth: true
                height: UISettings.iconSizeMedium
                z: 5
                color: UISettings.bgMedium
                radius: 5
                border.width: 2
                border.color: UISettings.borderColorDark

                Text
                {
                    id: funcSearchIcon
                    x: 6
                    width: height
                    height: parent.height - 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: "gray"
                    font.family: UISettings.fontAwesomeFontName
                    font.pixelSize: height - 6
                    text: FontAwesome.fa_magnifying_glass
                }

                TextInput
                {
                    x: funcSearchIcon.width + 14
                    y: 3
                    height: parent.height - 6
                    width: parent.width - x
                    color: UISettings.fgMain
                    text: importManager.functionSearchFilter
                    font.family: UISettings.robotoFontName
                    font.pixelSize: height - 6
                    selectionColor: UISettings.highlightPressed
                    selectByMouse: true

                    onTextEdited: importManager.functionSearchFilter = text
                }
            }

            // row 3 - Fixtures tree
            TreeFlatModel
            {
                id: flatGroupsModel
                sourceModel: popupRoot.visible ? importManager.groupsTreeModel : null
            }

            Connections
            {
                target: importManager
                function onGroupsTreeModelChanged() { flatGroupsModel.rebuild() }
            }

            ListView
            {
                id: groupListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                z: 4
                boundsBehavior: Flickable.StopAtBounds

                property bool dragActive: false

                model: flatGroupsModel
                delegate: ImportGroupsFlatDelegate
                {
                    width: groupListView.width - (gEditScrollBar.visible ? gEditScrollBar.width : 0)
                }

                ScrollBar.vertical: CustomScrollBar { id: gEditScrollBar }
            } // ListView

            // Functions tree
            TreeFlatModel
            {
                id: flatImportFunctionsModel
                sourceModel: popupRoot.visible ? importManager.functionsTreeModel : null
            }

            Connections
            {
                target: importManager
                function onFunctionsTreeModelChanged() { flatImportFunctionsModel.rebuild() }
            }

            ListView
            {
                id: functionsListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                z: 4
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                model: flatImportFunctionsModel
                delegate: ImportFunctionsFlatDelegate
                {
                    width: functionsListView.width - (fMgrScrollBar.visible ? fMgrScrollBar.width : 0)
                }

                ScrollBar.vertical: CustomScrollBar { id: fMgrScrollBar }
            } // ListView
        }
}
