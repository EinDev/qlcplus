/*
  Q Light Controller Plus
  FixtureGroupsBar.qml

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
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "."

/**
  * A horizontal bar displaying a button for every Fixture Group of the project.
  * Clicking on a button selects all the fixtures of the represented group.
  * It is meant to be anchored at the bottom of the 2D/3D views.
  */
Rectangle
{
    id: barRoot
    height: UISettings.bigItemHeight * 0.55
    color: UISettings.bgMedium
    border.width: 1
    border.color: UISettings.bgStrong
    clip: true

    // don't let mouse events fall through to the view behind, otherwise
    // clicking on the bar empty space would reset the fixture selection
    // or zoom the view in/out
    MouseArea
    {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => { wheel.accepted = true }
    }

    ListView
    {
        id: groupsListView
        anchors.fill: parent
        anchors.margins: 1
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        spacing: 2
        clip: true

        model: fixtureGroupEditor ? fixtureGroupEditor.groupsListModel : null

        delegate:
            Rectangle
            {
                id: groupButton
                width: UISettings.bigItemHeight
                height: groupsListView.height
                color:
                {
                    // referencing selectedFixturesCount here (its value is unused)
                    // forces this binding to re-evaluate whenever the fixture
                    // selection changes, since isGroupFullySelected() itself has
                    // no NOTIFY signal of its own to bind against
                    var dep = contextManager ? contextManager.selectedFixturesCount : 0

                    if (dep >= 0 && contextManager && contextManager.isGroupFullySelected(modelData.mValue))
                        return UISettings.highlight

                    return gMouseArea.containsMouse ? UISettings.bgLighter : UISettings.bgControl
                }
                border.width: 1
                border.color: UISettings.bgStrong

                // the group icon, as a watermark on the left of the cell background
                Image
                {
                    anchors.left: parent.left
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    height: parent.height * 0.75
                    width: height
                    source: modelData.mIcon
                    sourceSize: Qt.size(width, height)
                    opacity: 0.30
                }
/*
                // the number of fixtures, on the right of the cell background
                RobotoText
                {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    label: modelData.mCount
                    fontSize: UISettings.textSizeDefault * 0.85
                    labelColor: UISettings.fgLight
                    fontBold: true
                }
*/
                // the group name, spanning the whole cell
                RobotoText
                {
                    anchors.fill: parent
                    anchors.margins: 3
                    label: modelData.mLabel
                    fontSize: UISettings.textSizeDefault * 0.8
                    fontBold: true
                    //labelColor: UISettings.bgStrong
                    wrapText: true
                    textHAlign: Text.AlignHCenter
                }

                MouseArea
                {
                    id: gMouseArea
                    anchors.fill: parent
                    hoverEnabled: true

                    onClicked:
                    {
                        // clicking an already fully-selected group deselects it
                        var deselect = contextManager.isGroupFullySelected(modelData.mValue)

                        contextManager.resetFixtureSelection()

                        if (deselect === false)
                            contextManager.setFixtureGroupSelection(modelData.mValue, true, false)
                    }
                }
            }

        ScrollBar.horizontal: CustomScrollBar { orientation: Qt.Horizontal }
    }

    RobotoText
    {
        anchors.centerIn: parent
        visible: groupsListView.count === 0
        label: qsTr("No fixture group available")
        labelColor: UISettings.fgMedium
    }
}
