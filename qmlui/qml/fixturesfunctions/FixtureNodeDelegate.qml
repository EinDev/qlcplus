/*
  Q Light Controller Plus
  FixtureNodeDelegate.qml

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

import org.qlcplus.classes 1.0
import "."

Column
{
    id: nodeContainer
    width: 350
    //height: nodeLabel.height + isExpanded ? nodeChildrenView.height : 0

    property Fixture cRef
    property string textLabel
    property string itemIcon
    property int itemType: App.FixtureDragItem
    property int itemID
    property int subID
    property bool inGroup: false
    property bool isExpanded: false
    property bool isSelected: false
    property bool isCheckable: false
    property bool isChecked: false
    property bool showFlags: false
    property int itemFlags: 0
    property string nodePath
    property var nodeChildren
    property Item dragItem

    signal toggled(bool expanded, int newHeight)
    signal mouseEvent(int type, int iID, int iType, var qItem, int mouseMods)
    signal pathChanged(string oldPath, string newPath)

    FixtureNodeRow
    {
        id: rowItem
        width: nodeContainer.width
        cRef: nodeContainer.cRef
        textLabel: nodeContainer.textLabel
        itemIcon: nodeContainer.itemIcon
        itemType: nodeContainer.itemType
        itemID: nodeContainer.itemID
        subID: nodeContainer.subID
        inGroup: nodeContainer.inGroup
        isSelected: nodeContainer.isSelected
        isCheckable: nodeContainer.isCheckable
        isChecked: nodeContainer.isChecked
        showFlags: nodeContainer.showFlags
        itemFlags: nodeContainer.itemFlags
        nodePath: nodeContainer.nodePath
        hasChildren: nodeContainer.nodeChildren !== undefined
        dragItem: nodeContainer.dragItem

        onMouseEvent: (type, iID, iType, qItem, mouseMods) => nodeContainer.mouseEvent(type, iID, iType, qItem, mouseMods)
        onPathChanged: (oldPath, newPath) => nodeContainer.pathChanged(oldPath, newPath)
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
                    //width: nodeChildrenView.width
                    x: 20
                    //height: 35
                    source: type == App.ChannelDragItem ? "qrc:/FixtureChannelDelegate.qml" : "qrc:/FixtureHeadDelegate.qml"
                    onLoaded:
                    {
                        item.width = Qt.binding(function() { return nodeChildrenView.width })
                        item.textLabel = label
                        item.isSelected = Qt.binding(function() { return model.isSelected })
                        item.isCheckable = model.isCheckable
                        item.isChecked = Qt.binding(function() { return model.isChecked })
                        item.dragItem = dragItem
                        item.itemType = type

                        if (item.hasOwnProperty('cRef'))
                            item.cRef = classRef

                        item.itemID = id

                        if (type == App.ChannelDragItem)
                        {
                            console.log("Channel node, fixture " + cRef + " index: " + chIdx + " label: " + label)
                            item.isCheckable = isCheckable
                            item.isChecked = Qt.binding(function() { return isChecked })
                            item.chIndex = chIdx
                            item.itemIcon = cRef ? fixtureManager.channelIcon(cRef.id, chIdx) : ""

                            if (model.flags !== undefined && item.hasOwnProperty("itemFlags"))
                            {
                                item.showFlags = true
                                item.itemFlags = Qt.binding(function() { return model.flags })
                                item.canFade = Qt.binding(function() { return model.canFade })
                                item.precedence = Qt.binding(function() { return model.precedence })
                                item.modifier = Qt.binding(function() { return model.modifier })
                            }
                        }
                        else
                        {
                            console.log("Head node, fixture " + cRef + " index: " + chIdx + " label: " + label)
                            item.headIndex = chIdx
                        }
                    }
                    Connections
                    {
                        target: item
                        function onMouseEvent(type, iID, iType, qItem, mouseMods)
                        {
                            console.log("Got fixture tree node child mouse event")
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
                                    {
                                        console.log("Channel " + index + " got checked")
                                        model.isChecked = iType
                                    }
                                break;
                                case App.DragStarted:
                                    if (qItem === item && !model.isSelected)
                                    {
                                        model.isSelected = 1
                                        // invalidate the modifiers to force a single selection
                                        mouseMods = -1
                                    }
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
                } // Loader
        } // Component
    } // Repeater
}
