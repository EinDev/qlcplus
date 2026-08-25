/*
  Q Light Controller Plus
  TreeNodeDelegate.qml

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

Column
{
    id: nodeContainer
    width: 350
    //height: nodeLabel.height + nodeChildrenView.height

    property var cRef
    property string textLabel
    property string itemIcon: "qrc:/folder.svg"
    property int itemType: App.GenericDragItem
    property int itemID: cRef ? cRef.id : -1

    property bool isExpanded: false
    property bool isSelected: false
    property bool isCheckable: false
    property bool isChecked: false

    property string nodePath
    property var nodeChildren
    property string childrenDelegate: "qrc:/FunctionDelegate.qml"
    property string subTreeDelegate: "qrc:/TreeNodeDelegate.qml"
    property Item dragItem
    property string dropKeys: ""

    signal toggled(bool expanded, int newHeight)
    signal mouseEvent(int type, int iID, int iType, var qItem, int mouseMods)
    signal pathChanged(string oldPath, string newPath)
    signal itemsDropped(string path)

    TreeNodeRow
    {
        id: rowItem
        width: nodeContainer.width
        cRef: nodeContainer.cRef
        textLabel: nodeContainer.textLabel
        itemIcon: nodeContainer.itemIcon
        itemType: nodeContainer.itemType
        itemID: nodeContainer.itemID
        isSelected: nodeContainer.isSelected
        isCheckable: nodeContainer.isCheckable
        isChecked: nodeContainer.isChecked
        nodePath: nodeContainer.nodePath
        dragItem: nodeContainer.dragItem
        dropKeys: nodeContainer.dropKeys

        onMouseEvent: (type, iID, iType, qItem, mouseMods) => nodeContainer.mouseEvent(type, iID, iType, qItem, mouseMods)
        onPathChanged: (oldPath, newPath) => nodeContainer.pathChanged(oldPath, newPath)
        onItemsDropped: (path) => nodeContainer.itemsDropped(path)
    }

    Repeater
    {
        id: nodeChildrenView
        visible: isExpanded
        width: nodeContainer.width - 20
        model: visible ? nodeChildren : null
        delegate:
            Component
            {
                Loader
                {
                    width: nodeChildrenView.width
                    x: 20
                    //height: 35
                    source: hasChildren ? subTreeDelegate : childrenDelegate
                    onLoaded:
                    {
                        item.textLabel = Qt.binding(function() { return label })
                        item.isSelected = Qt.binding(function() { return model.isSelected })
                        item.isCheckable = model.isCheckable
                        item.isChecked = Qt.binding(function() { return model.isChecked })
                        item.dragItem = dragItem
                        if (item.hasOwnProperty("itemType"))
                        {
                            if (typeof type !== "undefined")
                                item.itemType = type
                            else if (model && model.type !== undefined)
                                item.itemType = model.type
                        }

                        if (item.hasOwnProperty('itemIcon'))
                            item.itemIcon = nodeContainer.itemIcon

                        if (model.classRef !== undefined && item.hasOwnProperty('cRef'))
                            item.cRef = classRef

                        if (model.classRef !== undefined && item.hasOwnProperty('itemID'))
                            item.itemID = id

                        if (item.hasOwnProperty('inGroup'))
                            item.inGroup = inGroup

                        if (item.hasOwnProperty('subID'))
                            item.subID = subid

                        //console.log("Item flags: " + model.flags);
                        if (model.flags !== undefined && item.hasOwnProperty("itemFlags"))
                        {
                            item.showFlags = true
                            item.itemFlags = Qt.binding(function() { return model.flags })
                        }

                        if (hasChildren)
                        {
                            item.nodePath = Qt.binding(function() { return nodePath + '`' + path })
                            item.isExpanded = Qt.binding(function() { return isExpanded })
                            item.nodeChildren = Qt.binding(function() { return childrenModel })
                            if (item.hasOwnProperty('dropKeys'))
                                item.dropKeys = Qt.binding(function() { return nodeContainer.dropKeys })
                            if (item.hasOwnProperty('childrenDelegate'))
                                item.childrenDelegate = childrenDelegate

                            //console.log("Item path: " + item.nodePath + ", label: " + label)
                        }
                    }
                    Connections
                    {
                        target: item
                        function onMouseEvent(type, iID, iType, qItem, mouseMods)
                        {
                            switch (type)
                            {
                                case App.Clicked:
                                    if (qItem === item)
                                    {
                                        model.isSelected = (mouseMods & Qt.ControlModifier) ? 2 : 1
                                        if (model.hasChildren)
                                            model.isExpanded = item.isExpanded
                                    }
                                break;
                                case App.Checked:
                                    if (qItem === item)
                                        model.isChecked = iType
                                break;
                                case App.DragStarted:
                                    if (qItem === item && !model.isSelected)
                                    {
                                        model.isSelected = 1
                                        // invalidate the modifiers to force a single selection
                                        mouseMods = -1
                                    }
                                break;
                                case App.DoubleClicked:
                                    if (qItem === item && model.hasChildren)
                                        model.isExpanded = !model.isExpanded
                                break;
                            }

                            // forward the event to the parent node
                            nodeContainer.mouseEvent(type, iID, iType, qItem, mouseMods)
                        }
                    }
                    Connections
                    {
                        ignoreUnknownSignals: true
                        target: item
                        function onPathChanged(oldPath, newPath)
                        {
                            nodeContainer.pathChanged(oldPath, newPath)
                        }
                    }
                    Connections
                    {
                        ignoreUnknownSignals: true
                        target: item
                        function onItemsDropped(path)
                        {
                            nodeContainer.itemsDropped(path)
                        }
                    }
                }
        }
    }
}
