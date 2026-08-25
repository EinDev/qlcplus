/*
  Q Light Controller Plus
  TreeNodeRow.qml

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

/* Pure row visual + interaction for a single "folder" tree node (Universe/Group/
 * Function folder/etc). Extracted out of TreeNodeDelegate.qml so it can be reused
 * both by the original recursive delegate (unchanged behavior) and by a flat,
 * single-level ListView delegate for the Fixture Groups tree. */
Item
{
    id: nodeRow
    width: 350
    height: UISettings.listItemHeight

    property var cRef
    property string textLabel
    property string itemIcon: "qrc:/folder.svg"
    property int itemType: App.GenericDragItem
    property int itemID: cRef ? cRef.id : -1

    property bool isSelected: false
    property bool isCheckable: false
    property bool isChecked: false

    property string nodePath
    property Item dragItem
    property string dropKeys: ""

    signal mouseEvent(int type, int iID, int iType, var qItem, int mouseMods)
    signal pathChanged(string oldPath, string newPath)
    signal itemsDropped(string path)

    Rectangle
    {
        id: nodeBgRect
        color: nodeIconImg.visible ? "transparent" : UISettings.sectionHeader
        width: nodeRow.width
        height: UISettings.listItemHeight

        // selection rectangle
        Rectangle
        {
            anchors.fill: parent
            radius: 3
            color: UISettings.highlight
            visible: isSelected || tnDropArea.containsDrag
        }

        Row
        {
            CustomCheckBox
            {
                id: nodeCheckBox
                visible: isCheckable
                implicitWidth: UISettings.listItemHeight
                implicitHeight: implicitWidth
                checked: isChecked
                onCheckedChanged: nodeRow.mouseEvent(App.Checked, -1, checked, nodeRow, 0)
            }

            Image
            {
                id: nodeIconImg
                visible: itemIcon == "" ? false : true
                width: nodeBgRect.height
                height: width
                source: itemIcon
                sourceSize: Qt.size(width, height)
            }

            CustomTextInput
            {
                id: nodeLabel
                width: nodeBgRect.width - x - 1
                text: cRef ? cRef.name : textLabel
                originalText: text

                onTextConfirmed: (text) => nodeRow.pathChanged(nodePath, text)
            }
        } // Row

        MouseArea
        {
            x: nodeCheckBox.visible ? nodeCheckBox.width : 0
            width: parent.width
            height: parent.height

            property bool dragActive: drag.active

            onDragActiveChanged:
            {
                console.log("Drag changed on node: " + textLabel)
                nodeRow.mouseEvent(dragActive ? App.DragStarted : App.DragFinished,
                                    cRef ? cRef.id : -1, nodeRow.itemType, nodeRow, 0)
            }

            drag.target: dragItem

            onPressed: (mouse) => nodeRow.mouseEvent(App.Pressed, cRef ? cRef.id : -1, nodeRow.itemType,
                                                nodeRow, mouse.modifiers)
            onClicked: (mouse) =>
            {
                nodeLabel.forceActiveFocus()
                nodeRow.mouseEvent(App.Clicked, cRef ? cRef.id : -1, nodeRow.itemType,
                                         nodeRow, mouse.modifiers)
            }
            onDoubleClicked: (mouse) => nodeRow.mouseEvent(App.DoubleClicked, cRef ? cRef.id : -1,
                                                                 nodeRow.itemType, nodeRow, mouse.modifiers)
        }

        DropArea
        {
            id: tnDropArea
            anchors.fill: parent
            keys: nodeRow.dropKeys.length ? [ nodeRow.dropKeys ] : []

            onDropped: (drop) =>
            {
                console.log("Item dropped here. x: " + drop.x + " y: " + drop.y + ", items: " + drop.source.itemsList.length)
                nodeRow.itemsDropped(nodePath)
            }
        }
    }
}
