/*
  Q Light Controller Plus
  ImportFunctionsFlatDelegate.qml

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

import org.qlcplus.classes 1.0
import "."

/* Single, fixed-height row for PopupImportProject.qml's functionsListView, one row per
 * currently-visible entry of the flat TreeFlatModel. Folders can nest arbitrarily deep
 * (same as FunctionManager's tree), so the row visual is picked by model.hasChildren
 * rather than a fixed depth. Pure checkbox-selection UI: no drag, no rename. */
Item
{
    id: flatRow
    height: UISettings.listItemHeight

    readonly property int rowIndent: model.depth * 20

    Loader
    {
        id: rowLoader
        x: flatRow.rowIndent
        y: 0
        width: flatRow.width - x
        height: flatRow.height
        source: model.hasChildren ? "qrc:/TreeNodeRow.qml" : "qrc:/FunctionDelegate.qml"

        onLoaded:
        {
            item.textLabel = Qt.binding(function() { return model.label })
            item.isSelected = Qt.binding(function() { return model.isSelected })
            item.isCheckable = Qt.binding(function() { return model.isCheckable })
            item.isChecked = Qt.binding(function() { return model.isChecked })

            if (model.hasChildren)
            {
                item.itemType = App.FolderDragItem
                item.nodePath = Qt.binding(function() { return model.path })
            }
            else
            {
                item.cRef = Qt.binding(function() { return model.classRef })
                item.itemType = App.FunctionDragItem
            }
        }

        Connections
        {
            target: rowLoader.item

            function onMouseEvent(type, iID, iType, qItem, mouseMods)
            {
                switch (type)
                {
                    case App.Clicked:
                        model.isSelected = (mouseMods & Qt.ControlModifier) ? 2 : 1
                    break;
                    case App.DoubleClicked:
                        if (model.hasChildren)
                            model.isExpanded = !model.isExpanded
                    break;
                    case App.Checked:
                        model.isChecked = iType
                    break;
                }
            }
        }
    }
}
