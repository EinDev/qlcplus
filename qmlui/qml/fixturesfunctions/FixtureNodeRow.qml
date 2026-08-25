/*
  Q Light Controller Plus
  FixtureNodeRow.qml

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

/* Pure row visual + interaction for a single Fixture tree node. Extracted out of
 * FixtureNodeDelegate.qml so it can be reused both by the original recursive
 * delegate (unchanged behavior) and by a flat, single-level ListView delegate for
 * the Fixture Groups tree. */
Item
{
    id: nodeRow
    width: 350
    height: UISettings.listItemHeight

    property Fixture cRef
    property string textLabel
    property string itemIcon
    property int itemType: App.FixtureDragItem
    property int itemID
    property int subID
    property int linkedIndex: 0
    property bool inGroup: false
    property bool isSelected: false
    property bool isCheckable: false
    property bool isChecked: false
    property bool showFlags: false
    property int itemFlags: 0
    property string nodePath
    /** True if this fixture has heads/channels that could be expanded below it.
      * The original recursive delegate derived this from its own nodeChildren
      * model; as a standalone row it's passed in directly instead. */
    property bool hasChildren: false
    property Item dragItem

    signal mouseEvent(int type, int iID, int iType, var qItem, int mouseMods)
    signal pathChanged(string oldPath, string newPath)

    onCRefChanged: itemIcon = cRef ? cRef.iconResource(true) : ""
    onItemIDChanged: linkedIndex = fixtureManager.fixtureLinkedIndex(itemID)

    Rectangle
    {
        id: nodeBgRect
        color: nodeIconImg.visible ? "transparent" : UISettings.sectionHeader
        width: nodeRow.width
        height: UISettings.listItemHeight
        z: 1

        // icon background for contrast
        Rectangle
        {
            visible: itemIcon == "" ? false : true
            y: 1
            width: visible ? parent.height - 2 : 0
            height: width
            color: UISettings.bgControl
            radius: height / 6
            border.width: 1
            border.color: UISettings.fgMedium
        }

        // selection rectangle
        Rectangle
        {
            anchors.fill: parent
            radius: 3
            color: UISettings.highlight
            visible: isSelected
        }

        RowLayout
        {
            width: parent.width

            CustomCheckBox
            {
                id: fxCheckBox
                visible: isCheckable
                implicitWidth: UISettings.listItemHeight
                implicitHeight: implicitWidth
                checked: isChecked
                onClicked: nodeRow.mouseEvent(App.Checked, -1, checked, nodeRow, 0)
            }

            Image
            {
                id: nodeIconImg
                visible: itemIcon == "" ? false : true
                x: 1
                y: 1
                width: nodeBgRect.height - 2
                height: width
                source: itemIcon
                sourceSize: Qt.size(width, height)

                // expand indicator
                Text
                {
                    visible: nodeRow.hasChildren
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    color: UISettings.fgMain
                    font.family: UISettings.fontAwesomeFontName
                    font.pixelSize: parent.height / 3
                    text: FontAwesome.fa_square_plus
                }
            }

            Text
            {
                visible: linkedIndex
                color: UISettings.fgMain
                font.family: UISettings.fontAwesomeFontName
                font.pixelSize: UISettings.listItemHeight - 6
                text: FontAwesome.fa_link
            }

            CustomTextInput
            {
                id: nodeLabel
                Layout.fillWidth: true
                text: textLabel
                originalText: text

                onTextConfirmed: (text) =>
                {
                    if (fixtureManager.renameFixture(itemID, text) === false)
                    {
                        fmGenericPopup.message = qsTr("An item with the same name already exists.\nPlease provide a different name.")
                        fmGenericPopup.open()
                        nodeLabel.text = textLabel
                    }
                    else
                    {
                        nodeRow.pathChanged(nodePath, text)
                    }
                }
            }

            // DMX address range
            RobotoText
            {
                visible: !showFlags
                implicitWidth: width
                implicitHeight: UISettings.listItemHeight
                label: cRef ? "" + (cRef.address + 1) + "-" + (cRef.address + cRef.channels) : ""
            }

            // divider
            Rectangle
            {
                visible: showFlags
                width: 1
                height: parent.height
            }

            // fixture modes
            Rectangle
            {
                id: fxModes
                visible: showFlags
                width: UISettings.chPropsModesWidth
                height: parent.height
                color: "transparent"
                z: 1

                CustomComboBox
                {
                    visible: showFlags
                    implicitWidth: parent.width
                    height: UISettings.listItemHeight
                    textRole: ""
                    model: showFlags ? fixtureManager.fixtureModes(itemID) : null
                    currentIndex: showFlags ? fixtureManager.fixtureModeIndex(itemID) : -1

                    onActivated:
                    {
                        if (!visible)
                            return

                        if (fixtureManager.setFixtureModeIndex(itemID, index) === false)
                        {
                            // show error popup on overlapping
                            fmGenericPopup.message = textLabel + " <" + currentText + "> " + qsTr("mode overlaps with another fixture!")
                            fmGenericPopup.open()
                            currentIndex = fixtureManager.fixtureModeIndex(itemID)
                        }
                    }
                }
            }

            // divider
            Rectangle
            {
                visible: showFlags
                width: 1
                height: parent.height
            }

            // fixture flags
            Rectangle
            {
                id: fxFlags
                visible: showFlags
                width: UISettings.chPropsFlagsWidth
                height: parent.height
                color: "transparent"
                z: 1

                Row
                {
                    height: parent.height
                    spacing: 2

                    IconButton
                    {
                        height: parent.height - 2
                        width: height
                        border.width: 0
                        faSource: checked ? FontAwesome.fa_eye : FontAwesome.fa_eye_slash
                        faColor: checked ? "#00FF00" : UISettings.fgMedium
                        bgColor: "transparent"
                        checkedColor: "transparent"
                        checkable: true
                        checked: itemFlags & MonitorProperties.HiddenFlag ? false : true
                        tooltip: qsTr("Show/Hide this fixture")
                        onToggled:
                        {
                            if (itemFlags & MonitorProperties.HiddenFlag)
                                fixtureManager.setItemRoleData(itemID, -1, "flags", (itemFlags & ~MonitorProperties.HiddenFlag))
                            else
                                fixtureManager.setItemRoleData(itemID, -1, "flags", itemFlags | MonitorProperties.HiddenFlag)
                        }
                    }

                    IconButton
                    {
                        height: parent.height - 2
                        width: height
                        border.width: 0
                        faSource: checked ? FontAwesome.fa_lock : FontAwesome.fa_lock_open
                        faColor: checked ? "#00FF00" : UISettings.fgMedium
                        bgColor: "transparent"
                        checkedColor: "transparent"
                        checkable: true
                        checked: itemFlags & MonitorProperties.LockedFlag ? true : false
                        tooltip: qsTr("Lock/Unlock position")
                        onToggled:
                        {
                            if (itemFlags & MonitorProperties.LockedFlag)
                                fixtureManager.setItemRoleData(itemID, -1, "flags", (itemFlags & ~MonitorProperties.LockedFlag))
                            else
                                fixtureManager.setItemRoleData(itemID, -1, "flags", itemFlags | MonitorProperties.LockedFlag)
                        }
                    }

                    IconButton
                    {
                        height: parent.height - 2
                        width: height
                        border.width: 0
                        faSource: FontAwesome.fa_arrows_left_right
                        faColor: checked ? "#00FF00" : UISettings.fgMedium
                        bgColor: "transparent"
                        checkedColor: "transparent"
                        checkable: true
                        checked: itemFlags & MonitorProperties.InvertedPanFlag ? true : false
                        tooltip: qsTr("Invert Pan")
                        onToggled:
                        {
                            if (itemFlags & MonitorProperties.InvertedPanFlag)
                                fixtureManager.setItemRoleData(itemID, -1, "flags", (itemFlags & ~MonitorProperties.InvertedPanFlag))
                            else
                                fixtureManager.setItemRoleData(itemID, -1, "flags", itemFlags | MonitorProperties.InvertedPanFlag)
                        }
                    }


                    IconButton
                    {
                        height: parent.height - 2
                        width: height
                        border.width: 0
                        faSource: FontAwesome.fa_arrows_up_down
                        faColor: checked ? "#00FF00" : UISettings.fgMedium
                        bgColor: "transparent"
                        checkedColor: "transparent"
                        checkable: true
                        checked: itemFlags & MonitorProperties.InvertedTiltFlag ? true : false
                        tooltip: qsTr("Invert Tilt")
                        onToggled:
                        {
                            if (itemFlags & MonitorProperties.InvertedTiltFlag)
                                fixtureManager.setItemRoleData(itemID, -1, "flags", (itemFlags & ~MonitorProperties.InvertedTiltFlag))
                            else
                                fixtureManager.setItemRoleData(itemID, -1, "flags", itemFlags | MonitorProperties.InvertedTiltFlag)
                        }
                    }
                }
            }

            Rectangle { visible: showFlags; width: 1; height: parent.height } // divider
            Rectangle { visible: showFlags; width: UISettings.chPropsCanFadeWidth; height: parent.height; color: "transparent" } // stub
            Rectangle { visible: showFlags; width: 1; height: parent.height } // divider
            Rectangle { visible: showFlags; width: UISettings.chPropsPrecedenceWidth; height: parent.height; color: "transparent" } // stub
            Rectangle { visible: showFlags; width: 1; height: parent.height } // divider
            Rectangle { visible: showFlags; width: UISettings.chPropsModifierWidth; height: parent.height; color: "transparent" } // stub
        } // RowLayout

        // separator line
        Rectangle
        {
            width: parent.width
            height: 1
            y: parent.height - 1
            color: UISettings.bgLight
        }

        MouseArea
        {
            x: fxCheckBox.visible ? fxCheckBox.width : 0
            width: (showFlags ? fxModes.x : parent.width) - x
            height: parent.height

            property bool dragActive: drag.active

            onDragActiveChanged:
            {
                console.log("Drag changed on node: " + textLabel)
                nodeRow.mouseEvent(dragActive ? App.DragStarted : App.DragFinished, cRef ? cRef.id : -1, -1, nodeRow, 0)
            }

            drag.target: dragItem

            onPressed: (mouse) =>
            {
                nodeRow.mouseEvent(App.Pressed, cRef ? cRef.id : -1, -1, nodeRow, mouse.modifiers)
            }
            onClicked: (mouse) =>
            {
                nodeLabel.forceActiveFocus()
                nodeRow.mouseEvent(App.Clicked, itemID, -1, nodeRow, mouse.modifiers)
            }
            onDoubleClicked: (mouse) =>
            {
                nodeRow.mouseEvent(App.DoubleClicked, itemID, -1, nodeRow, mouse.modifiers)
            }
        }
    }
}
