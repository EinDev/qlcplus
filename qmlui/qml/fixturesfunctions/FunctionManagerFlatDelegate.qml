/*
  Q Light Controller Plus
  FunctionManagerFlatDelegate.qml

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

/* Single, fixed-height row for FunctionManager.qml's functionsListView, one row per
 * currently-visible entry of the flat TreeFlatModel. Unlike the Fixture Groups tree,
 * folders can nest arbitrarily deep here (a folder's children can themselves be folders
 * or functions), so the row visual is picked by model.hasChildren rather than a fixed
 * depth. Consolidates what used to be two nested "forward the event up" handlers (in
 * TreeNodeDelegate.qml and FunctionManager.qml itself) into one, since there's only one
 * level now. Relies on ids from its enclosing FunctionManager.qml tree (fDragItem,
 * functionsListView, fmContainer, mainView, functionManager) being visible the same way
 * TreeNodeDelegate.qml already relied on them from a separate file. */
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
            item.z = 2
            item.textLabel = Qt.binding(function() { return model.label })
            item.isSelected = Qt.binding(function() { return model.isSelected })
            item.dragItem = fDragItem

            if (model.hasChildren)
            {
                item.nodePath = Qt.binding(function() { return model.path })
                item.dropKeys = "function"
            }
            else
            {
                item.cRef = Qt.binding(function() { return model.classRef })
            }
        }

        Connections
        {
            target: rowLoader.item

            function onMouseEvent(type, iID, iType, qItem, mouseMods)
            {
                switch (type)
                {
                    case App.Pressed:
                        var posnInWindow = qItem.mapToItem(mainView, qItem.x, qItem.y)
                        fDragItem.parent = mainView
                        fDragItem.x = posnInWindow.x - (fDragItem.width / 4)
                        fDragItem.y = posnInWindow.y - (fDragItem.height / 4)
                        fDragItem.modifiers = mouseMods
                    break;
                    case App.Clicked:
                        model.isSelected = (mouseMods & Qt.ControlModifier) ? 2 : 1

                        if (qItem.itemType === App.FunctionDragItem)
                            functionManager.selectFunctionID(iID, mouseMods & Qt.ControlModifier)
                        else
                            functionManager.selectFolder(qItem.nodePath, mouseMods & Qt.ControlModifier)
                    break;
                    case App.DoubleClicked:
                        if (model.hasChildren)
                            model.isExpanded = !model.isExpanded
                        else if (fmContainer.allowEditing)
                            fmContainer.loadFunctionEditor(iID, iType)
                        else
                            fmContainer.doubleClicked(iID, iType)
                    break;
                    case App.DragStarted:
                        if (!model.isSelected)
                        {
                            model.isSelected = 1
                            // invalidate the modifiers to force a single selection
                            mouseMods = -1
                        }

                        if (mouseMods === -1)
                            functionManager.selectFunctionID(iID, false)

                        fDragItem.itemsList = functionManager.selectedFunctionsID()
                        fDragItem.itemLabel = qItem.textLabel
                        if (qItem.hasOwnProperty("itemIcon"))
                            fDragItem.itemIcon = qItem.itemIcon
                        else
                            fDragItem.itemIcon = ""
                        functionsListView.dragActive = true
                    break;
                    case App.DragFinished:
                        fDragItem.Drag.drop()
                        fDragItem.parent = functionsListView
                        fDragItem.x = 0
                        fDragItem.y = 0
                        functionsListView.dragActive = false
                    break;
                }
            }
        }

        Connections
        {
            ignoreUnknownSignals: true
            target: rowLoader.item

            function onPathChanged(oldPath, newPath)
            {
                functionManager.setFolderPath(oldPath, newPath, true)
            }
        }

        Connections
        {
            ignoreUnknownSignals: true
            target: rowLoader.item

            function onItemsDropped(path)
            {
                functionManager.moveFunctions(path)
            }
        }
    }
}
